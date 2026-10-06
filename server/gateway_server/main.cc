#include <brpc/server.h>
#include <gflags/gflags.h>
#include <httplib.h>

#include <string>

#include "channel_manager.hpp"
#include "dispatch.hpp"
#include "etcd_client.hpp"
#include "flags.hpp"
#include "gateway.pb.h"
#include "logger.hpp"
#include "redis_client.hpp"
#include "ws_push.hpp"

// ============================================================
// 入口网关：客户端唯一入口
//   POST /gateway  ClientRequest{request_id,type,token,body}
//     ① 路由表判定目标子服务与是否需要登录
//     ② 需要登录则用 token 查 Redis 拿 user_id（注入子请求，防伪造）
//     ③ 交给对应 dispatcher（分文件实现，见 handlers_*.cc）
//     ④ 组 ServerResponse 回包
//   GET  /health   存活探针
// 注：本文件刻意不包含任何子服务的 pb.h，以降低编译内存峰值
//     （服务器仅 1.6G 内存，详见 docs/env.md 的编译约束）。
// ============================================================
using namespace im;  // NOLINT

namespace {

RedisClient* g_redis = nullptr;
ChannelManager* g_channels = nullptr;

struct Route {
  const char* service;  // etcd 注册的子服务名
  bool need_auth;
  bool inject_user_id;
};

bool route_of(RequestType t, Route* out) {
  switch (t) {
    case REQ_TYPE_SEND_SMS_CODE:
    case REQ_TYPE_REGISTER:
    case REQ_TYPE_LOGIN:
      *out = {"user_server", false, false};
      return true;
    case REQ_TYPE_LOGOUT:
      *out = {"user_server", true, false};
      return true;
    case REQ_TYPE_GET_USER_INFO:
    case REQ_TYPE_SET_USER_INFO:
    case REQ_TYPE_SET_USER_AVATAR:
      *out = {"user_server", true, true};
      return true;
    case REQ_TYPE_SEARCH_USER:
    case REQ_TYPE_SEND_FRIEND_APPLY:
    case REQ_TYPE_PROCESS_FRIEND_APPLY:
    case REQ_TYPE_DEL_FRIEND:
    case REQ_TYPE_GET_FRIEND_LIST:
    case REQ_TYPE_GET_PENDING_EVENTS:
    case REQ_TYPE_CREATE_GROUP_SESSION:
    case REQ_TYPE_GET_SESSION_MEMBER:
    case REQ_TYPE_GET_SESSION_LIST:
      *out = {"friend_server", true, true};
      return true;
    case REQ_TYPE_GET_HISTORY:
      *out = {"message_storage_server", true, false};
      return true;
    case REQ_TYPE_SEARCH_HISTORY:
      *out = {"message_storage_server", true, true};
      return true;
    case REQ_TYPE_TRANSMIT_MESSAGE:
      *out = {"message_server", true, true};  // 发消息：user_id 由网关注入
      return true;
    case REQ_TYPE_PUT_SINGLE_FILE:
    case REQ_TYPE_PUT_BATCH_FILE:
    case REQ_TYPE_GET_SINGLE_FILE:
    case REQ_TYPE_GET_BATCH_FILE:
      *out = {"file_server", true, false};
      return true;
    case REQ_TYPE_SPEECH_RECOGNITION:
      *out = {"speech_server", true, false};
      return true;
    default:
      return false;
  }
}

void write_response(httplib::Response& res, const std::string& request_id,
                    const gateway::Result& r) {
  ServerResponse sresp;
  sresp.set_request_id(request_id);
  sresp.set_success(r.success);
  sresp.set_errmsg(r.errmsg);
  sresp.set_body(r.body);
  res.set_content(sresp.SerializeAsString(), "application/x-protobuf");
}

}  // namespace

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  init_logger(FLAGS_log_dir, FLAGS_service_name, FLAGS_log_level);
  LOG_INFO("{} 启动, HTTP 端口 {}", FLAGS_service_name, FLAGS_http_port);

  static RedisClient redis;
  static ChannelManager channels(FLAGS_etcd_endpoints);
  g_redis = &redis;
  g_channels = &channels;

  // 启动对各子服务的节点发现（漏掉会导致对应通道永远为空）
  for (const char* s : {"user_server", "friend_server", "message_storage_server", "file_server",
                        "message_server", "speech_server"}) {
    channels.discover(s);
  }

  ServiceRegistry registry(FLAGS_etcd_endpoints, FLAGS_service_name, FLAGS_instance_id,
                           FLAGS_register_host, FLAGS_http_port, FLAGS_etcd_lease_ttl,
                           FLAGS_etcd_keepalive_interval);
  if (!registry.start()) {
    LOG_ERROR("注册失败，退出");
    return 1;
  }

  httplib::Server svr;

  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("ok", "text/plain");
  });

  // WebSocket 推送 + MQ 桥接（本节点在线用户接收实时消息）
  auto pusher = make_ws_pusher(FLAGS_ws_port, FLAGS_instance_id);
  if (!pusher->start()) {
    LOG_WARN("WebSocket 未启动（HTTP 请求仍可用，实时推送不可用）");
  }
  auto mq_bridge = start_mq_bridge(pusher.get(), FLAGS_instance_id);

  svr.Post("/gateway", [](const httplib::Request& req, httplib::Response& res) {
    ClientRequest creq;
    if (!creq.ParseFromString(req.body)) {
      gateway::Result bad;
      bad.errmsg = "请求解析失败（应为 protobuf ClientRequest）";
      write_response(res, "", bad);
      return;
    }
    const std::string& rid = creq.request_id();

    Route route{};
    if (!route_of(creq.type(), &route)) {
      gateway::Result bad;
      bad.errmsg = "未知或未实现的请求类型: " + std::to_string(creq.type());
      write_response(res, rid, bad);
      return;
    }

    // 鉴权
    std::string user_id;
    if (route.need_auth) {
      auto uid = g_redis->get("auth:token:" + creq.token());
      if (!uid) {
        gateway::Result bad;
        bad.errmsg = "未登录或登录已过期";
        write_response(res, rid, bad);
        return;
      }
      user_id = *uid;
    }
    if (!route.inject_user_id) user_id.clear();

    // 通道
    auto* channel = g_channels->channel(route.service);
    if (channel == nullptr) {
      gateway::Result bad;
      bad.errmsg = std::string("服务暂无可用节点: ") + route.service;
      write_response(res, rid, bad);
      return;
    }

    gateway::Ctx ctx;
    ctx.type = creq.type();
    ctx.body = &creq.body();
    ctx.token = &creq.token();
    ctx.user_id = &user_id;
    ctx.channel = channel;

    gateway::Result result;
    bool handled = gateway::dispatch_user(ctx, &result) ||
                   gateway::dispatch_friend(ctx, &result) ||
                   gateway::dispatch_msg(ctx, &result) ||
                   gateway::dispatch_file(ctx, &result) ||
                   gateway::dispatch_speech(ctx, &result) ||
                   gateway::dispatch_transmit(ctx, &result);
    if (!handled) {
      result.success = false;
      result.errmsg = "未实现的分发分支: " + std::to_string(creq.type());
    }
    write_response(res, rid, result);
  });

  LOG_INFO("{} 就绪: POST /gateway", FLAGS_service_name);
  if (!svr.listen("0.0.0.0", static_cast<unsigned short>(FLAGS_http_port))) {
    LOG_ERROR("HTTP 监听失败: 端口 {}", FLAGS_http_port);
    return 1;
  }
  registry.stop();
  LOG_INFO("{} 退出", FLAGS_service_name);
  return 0;
}
