#include <brpc/server.h>
#include <gflags/gflags.h>
#include <httplib.h>

#include <memory>
#include <string>

#include "channel_manager.hpp"
#include "file.pb.h"
#include "flags.hpp"
#include "gateway.pb.h"
#include "logger.hpp"
#include "redis_client.hpp"
#include "user.pb.h"
#include "etcd_client.hpp"

// ============================================================
// 入口网关（M4 雏形）：
//   POST /gateway  统一入口：ClientRequest{request_id,type,token,body}
//     → token 鉴权（需要登录的 type 从 Redis 取 user_id 注入子请求）
//     → 按 type 经 etcd 发现 + brpc 负载均衡分发到对应子服务
//     → ServerResponse{request_id,success,errmsg,body} 回包
//   GET  /health   存活探针
// WebSocket 推送在 M7 接入。
// ============================================================
using namespace im;  // NOLINT

namespace {

RedisClient* g_redis = nullptr;
ChannelManager* g_channels = nullptr;

bool requires_auth(RequestType t) {
  switch (t) {
    case REQ_TYPE_SEND_SMS_CODE:
    case REQ_TYPE_REGISTER:
    case REQ_TYPE_LOGIN:
      return false;
    case REQ_TYPE_UNKNOWN:
      return false;
    default:
      return true;
  }
}

// 取子服务通道（type → 服务名 + 是否已上线）
brpc::Channel* channel_for(RequestType t, std::string* service_name) {
  switch (t) {
    case REQ_TYPE_SEND_SMS_CODE:
    case REQ_TYPE_REGISTER:
    case REQ_TYPE_LOGIN:
    case REQ_TYPE_LOGOUT:
    case REQ_TYPE_GET_USER_INFO:
    case REQ_TYPE_SET_USER_INFO:
    case REQ_TYPE_SET_USER_AVATAR:
      *service_name = "user_server";
      return g_channels->channel("user_server");
    case REQ_TYPE_PUT_SINGLE_FILE:
    case REQ_TYPE_GET_SINGLE_FILE:
      *service_name = "file_server";
      return g_channels->channel("file_server");
    default:
      return nullptr;
  }
}

// 组 ServerResponse 并写回
void reply(httplib::Response& res, const std::string& request_id, bool success,
           const std::string& errmsg, const google::protobuf::MessageLite* msg = nullptr) {
  ServerResponse sresp;
  sresp.set_request_id(request_id);
  sresp.set_success(success);
  sresp.set_errmsg(errmsg);
  if (msg != nullptr) sresp.set_body(msg->SerializeAsString());
  res.set_content(sresp.SerializeAsString(), "application/x-protobuf");
}

// 通用：解析 body 为指定 Req 类型
template <typename ReqT>
bool parse_body(const std::string& body, ReqT* out) {
  return out->ParseFromString(body);
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

  // 网关自身注册（多网关节点时由 etcd 负载均衡；M7 WS 推送同样按此扩容）
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

  svr.Post("/gateway", [](const httplib::Request& req, httplib::Response& res) {
    ClientRequest creq;
    if (!creq.ParseFromString(req.body)) {
      reply(res, "", false, "请求解析失败（应为 protobuf ClientRequest）");
      return;
    }
    LOG_INFO("网关收到请求: id={} type={}", creq.request_id(),
             static_cast<int>(creq.type()));

    // 未知类型
    if (creq.type() == REQ_TYPE_UNKNOWN) {
      reply(res, creq.request_id(), false, "未知请求类型");
      return;
    }

    // token 鉴权
    std::string auth_uid;
    if (requires_auth(creq.type())) {
      auto uid = redis.get("auth:token:" + creq.token());
      if (!uid) {
        reply(res, creq.request_id(), false, "未登录或登录已过期");
        return;
      }
      auth_uid = *uid;
    }

    // 子服务通道
    std::string service_name;
    auto* channel = channel_for(creq.type(), &service_name);
    if (channel == nullptr) {
      reply(res, creq.request_id(), false, "服务尚未上线: " + service_name);
      return;
    }

    // -------- 用户子服务 --------
    if (creq.type() == REQ_TYPE_SEND_SMS_CODE) {
      SendSmsCodeReq r;
      if (!parse_body(creq.body(), &r)) return reply(res, creq.request_id(), false, "body 解析失败");
      UserService_Stub stub(channel);
      brpc::Controller cntl;
      SendSmsCodeResp out;
      stub.SendSmsCode(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }
    if (creq.type() == REQ_TYPE_REGISTER) {
      UserRegisterReq r;
      if (!parse_body(creq.body(), &r)) return reply(res, creq.request_id(), false, "body 解析失败");
      UserService_Stub stub(channel);
      brpc::Controller cntl;
      UserRegisterResp out;
      stub.UserRegister(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }
    if (creq.type() == REQ_TYPE_LOGIN) {
      UserLoginReq r;
      if (!parse_body(creq.body(), &r)) return reply(res, creq.request_id(), false, "body 解析失败");
      UserService_Stub stub(channel);
      brpc::Controller cntl;
      UserLoginResp out;
      stub.UserLogin(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }
    if (creq.type() == REQ_TYPE_LOGOUT) {
      UserLogoutReq r;
      r.set_token(creq.token());
      UserService_Stub stub(channel);
      brpc::Controller cntl;
      UserLogoutResp out;
      stub.UserLogout(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }
    if (creq.type() == REQ_TYPE_GET_USER_INFO) {
      GetUserInfoReq r;
      if (!parse_body(creq.body(), &r)) return reply(res, creq.request_id(), false, "body 解析失败");
      r.set_user_id(auth_uid);  // 鉴权注入，防伪造
      UserService_Stub stub(channel);
      brpc::Controller cntl;
      GetUserInfoResp out;
      stub.GetUserInfo(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }
    if (creq.type() == REQ_TYPE_SET_USER_INFO) {
      SetUserInfoReq r;
      if (!parse_body(creq.body(), &r)) return reply(res, creq.request_id(), false, "body 解析失败");
      r.set_user_id(auth_uid);
      UserService_Stub stub(channel);
      brpc::Controller cntl;
      SetUserInfoResp out;
      stub.SetUserInfo(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }
    if (creq.type() == REQ_TYPE_SET_USER_AVATAR) {
      SetUserAvatarReq r;
      if (!parse_body(creq.body(), &r)) return reply(res, creq.request_id(), false, "body 解析失败");
      r.set_user_id(auth_uid);
      UserService_Stub stub(channel);
      brpc::Controller cntl;
      SetUserAvatarResp out;
      stub.SetUserAvatar(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }

    // -------- 文件子服务 --------
    if (creq.type() == REQ_TYPE_PUT_SINGLE_FILE) {
      PutSingleReq r;
      if (!parse_body(creq.body(), &r)) return reply(res, creq.request_id(), false, "body 解析失败");
      FileService_Stub stub(channel);
      brpc::Controller cntl;
      PutSingleResp out;
      stub.PutSingle(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }
    if (creq.type() == REQ_TYPE_GET_SINGLE_FILE) {
      GetSingleReq r;
      if (!parse_body(creq.body(), &r)) return reply(res, creq.request_id(), false, "body 解析失败");
      FileService_Stub stub(channel);
      brpc::Controller cntl;
      GetSingleResp out;
      stub.GetSingle(&cntl, &r, &out, nullptr);
      if (cntl.Failed()) return reply(res, creq.request_id(), false, cntl.ErrorText());
      reply(res, creq.request_id(), out.success(), out.errmsg(), &out);
      return;
    }

    reply(res, creq.request_id(), false, "服务尚未上线: type=" + std::to_string(creq.type()));
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
