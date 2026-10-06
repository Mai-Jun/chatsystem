#include <brpc/server.h>
#include <gflags/gflags.h>
#include <httplib.h>

#include <memory>
#include <string>

#include "channel_manager.hpp"
#include "etcd_client.hpp"
#include "file.pb.h"
#include "flags.hpp"
#include "friend.pb.h"
#include "gateway.pb.h"
#include "logger.hpp"
#include "message_storage.pb.h"
#include "redis_client.hpp"
#include "user.pb.h"

// ============================================================
// 入口网关：客户端唯一入口
//   POST /gateway   ClientRequest{request_id,type,token,body}
//     ① 按 type 判定是否需要登录
//     ② 需要则用 token 查 Redis 得 user_id（并注入子请求，防客户端伪造）
//     ③ 经 etcd 发现 + brpc 负载均衡转发到对应子服务
//     ④ ServerResponse{request_id,success,errmsg,body} 回包
//   GET  /health    存活探针
// WebSocket 推送在 M7 接入。
// ============================================================
using namespace im;  // NOLINT

namespace {

RedisClient* g_redis = nullptr;
ChannelManager* g_channels = nullptr;

struct Route {
  const char* service;      // 目标子服务名（etcd 注册名）
  bool need_auth;
  bool inject_user_id;      // 是否用 token 解析出的 user_id 覆盖请求体
};

// 请求类型 → 路由表（新增服务只加一行）
bool route_of(RequestType t, Route* out) {
  switch (t) {
    // 用户子服务（免登录）
    case REQ_TYPE_SEND_SMS_CODE:
      *out = {"user_server", false, false}; return true;
    case REQ_TYPE_REGISTER:
      *out = {"user_server", false, false}; return true;
    case REQ_TYPE_LOGIN:
      *out = {"user_server", false, false}; return true;
    // 用户子服务（需登录）
    case REQ_TYPE_LOGOUT:
      *out = {"user_server", true, false}; return true;
    case REQ_TYPE_GET_USER_INFO:
    case REQ_TYPE_SET_USER_INFO:
    case REQ_TYPE_SET_USER_AVATAR:
      *out = {"user_server", true, true}; return true;
    // 好友子服务
    case REQ_TYPE_SEARCH_USER:
    case REQ_TYPE_SEND_FRIEND_APPLY:
    case REQ_TYPE_PROCESS_FRIEND_APPLY:
    case REQ_TYPE_DEL_FRIEND:
    case REQ_TYPE_GET_FRIEND_LIST:
    case REQ_TYPE_GET_PENDING_EVENTS:
    case REQ_TYPE_CREATE_GROUP_SESSION:
    case REQ_TYPE_GET_SESSION_MEMBER:
    case REQ_TYPE_GET_SESSION_LIST:
      *out = {"friend_server", true, true}; return true;
    // 消息存储
    case REQ_TYPE_GET_HISTORY:
      *out = {"message_storage_server", true, false}; return true;
    case REQ_TYPE_SEARCH_HISTORY:
      *out = {"message_storage_server", true, true}; return true;
    // 文件
    case REQ_TYPE_PUT_SINGLE_FILE:
    case REQ_TYPE_PUT_BATCH_FILE:
    case REQ_TYPE_GET_SINGLE_FILE:
    case REQ_TYPE_GET_BATCH_FILE:
      *out = {"file_server", true, false}; return true;
    default:
      return false;
  }
}

void reply(httplib::Response& res, const std::string& request_id, bool success,
           const std::string& errmsg, const google::protobuf::MessageLite* msg = nullptr) {
  ServerResponse sresp;
  sresp.set_request_id(request_id);
  sresp.set_success(success);
  sresp.set_errmsg(errmsg);
  if (msg != nullptr) sresp.set_body(msg->SerializeAsString());
  res.set_content(sresp.SerializeAsString(), "application/x-protobuf");
}

// 统一转发：解析 body → (可选)注入 user_id → 调子服务 → 回包
template <typename ReqT, typename RespT, typename StubT, typename MethodT>
void forward(const std::string& request_id, const std::string& body, brpc::Channel* channel,
             MethodT method, httplib::Response& res, const std::string& user_id) {
  ReqT req;
  if (!body.empty() && !req.ParseFromString(body)) {
    reply(res, request_id, false, "body 解析失败");
    return;
  }
  if (!user_id.empty()) req.set_user_id(user_id);
  StubT stub(channel);
  brpc::Controller cntl;
  RespT out;
  (stub.*method)(&cntl, &req, &out, nullptr);
  if (cntl.Failed()) {
    LOG_ERROR("转发失败: {}", cntl.ErrorText());
    reply(res, request_id, false, "子服务调用失败: " + cntl.ErrorText());
    return;
  }
  reply(res, request_id, out.success(), out.errmsg(), &out);
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
    const std::string& rid = creq.request_id();

    Route route{};
    if (!route_of(creq.type(), &route)) {
      reply(res, rid, false, "未知或未实现的请求类型: " + std::to_string(creq.type()));
      return;
    }

    // 鉴权
    std::string user_id;
    if (route.need_auth) {
      auto uid = redis.get("auth:token:" + creq.token());
      if (!uid) {
        reply(res, rid, false, "未登录或登录已过期");
        return;
      }
      user_id = *uid;
    }
    if (!route.inject_user_id) user_id.clear();

    // 取通道
    auto* channel = channels.channel(route.service);
    if (channel == nullptr) {
      reply(res, rid, false, std::string("服务暂无可用节点: ") + route.service);
      return;
    }

    switch (creq.type()) {
      // ---- 用户 ----
      case REQ_TYPE_SEND_SMS_CODE:
        forward<SendSmsCodeReq, SendSmsCodeResp, UserService_Stub>(
            rid, creq.body(), channel, &UserService_Stub::SendSmsCode, res, user_id);
        break;
      case REQ_TYPE_REGISTER:
        forward<UserRegisterReq, UserRegisterResp, UserService_Stub>(
            rid, creq.body(), channel, &UserService_Stub::UserRegister, res, user_id);
        break;
      case REQ_TYPE_LOGIN:
        forward<UserLoginReq, UserLoginResp, UserService_Stub>(
            rid, creq.body(), channel, &UserService_Stub::UserLogin, res, user_id);
        break;
      case REQ_TYPE_LOGOUT: {
        // token 来自外壳，不经 body
        UserLogoutReq lreq;
        lreq.set_token(creq.token());
        UserService_Stub stub(channel);
        brpc::Controller cntl;
        UserLogoutResp out;
        stub.UserLogout(&cntl, &lreq, &out, nullptr);
        if (cntl.Failed()) reply(res, rid, false, cntl.ErrorText());
        else reply(res, rid, out.success(), out.errmsg(), &out);
        break;
      }
      case REQ_TYPE_GET_USER_INFO:
        forward<GetUserInfoReq, GetUserInfoResp, UserService_Stub>(
            rid, creq.body(), channel, &UserService_Stub::GetUserInfo, res, user_id);
        break;
      case REQ_TYPE_SET_USER_INFO:
        forward<SetUserInfoReq, SetUserInfoResp, UserService_Stub>(
            rid, creq.body(), channel, &UserService_Stub::SetUserInfo, res, user_id);
        break;
      case REQ_TYPE_SET_USER_AVATAR:
        forward<SetUserAvatarReq, SetUserAvatarResp, UserService_Stub>(
            rid, creq.body(), channel, &UserService_Stub::SetUserAvatar, res, user_id);
        break;

      // ---- 好友 ----
      case REQ_TYPE_SEARCH_USER:
        forward<SearchUserReq, SearchUserResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::SearchUser, res, user_id);
        break;
      case REQ_TYPE_SEND_FRIEND_APPLY:
        forward<SendFriendApplyReq, SendFriendApplyResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::SendFriendApply, res, user_id);
        break;
      case REQ_TYPE_PROCESS_FRIEND_APPLY:
        forward<ProcessFriendApplyReq, ProcessFriendApplyResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::ProcessFriendApply, res, user_id);
        break;
      case REQ_TYPE_DEL_FRIEND:
        forward<DelFriendReq, DelFriendResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::DelFriend, res, user_id);
        break;
      case REQ_TYPE_GET_FRIEND_LIST:
        forward<GetFriendListReq, GetFriendListResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::GetFriendList, res, user_id);
        break;
      case REQ_TYPE_GET_PENDING_EVENTS:
        forward<GetPendingEventsReq, GetPendingEventsResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::GetPendingEvents, res, user_id);
        break;
      case REQ_TYPE_CREATE_GROUP_SESSION:
        forward<CreateGroupSessionReq, CreateGroupSessionResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::CreateGroupSession, res, user_id);
        break;
      case REQ_TYPE_GET_SESSION_MEMBER:
        forward<GetSessionMemberReq, GetSessionMemberResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::GetSessionMember, res, user_id);
        break;
      case REQ_TYPE_GET_SESSION_LIST:
        forward<GetChatSessionListReq, GetChatSessionListResp, FriendService_Stub>(
            rid, creq.body(), channel, &FriendService_Stub::GetChatSessionList, res, user_id);
        break;

      // ---- 消息存储 ----
      case REQ_TYPE_GET_HISTORY:
        forward<GetHistoryReq, GetHistoryResp, MsgStorageService_Stub>(
            rid, creq.body(), channel, &MsgStorageService_Stub::GetHistoryMessage, res, user_id);
        break;
      case REQ_TYPE_SEARCH_HISTORY:
        forward<SearchHistoryReq, SearchHistoryResp, MsgStorageService_Stub>(
            rid, creq.body(), channel, &MsgStorageService_Stub::SearchHistoryMessage, res, user_id);
        break;

      // ---- 文件 ----
      case REQ_TYPE_PUT_SINGLE_FILE:
        forward<PutSingleReq, PutSingleResp, FileService_Stub>(
            rid, creq.body(), channel, &FileService_Stub::PutSingle, res, user_id);
        break;
      case REQ_TYPE_PUT_BATCH_FILE:
        forward<PutBatchReq, PutBatchResp, FileService_Stub>(
            rid, creq.body(), channel, &FileService_Stub::PutBatch, res, user_id);
        break;
      case REQ_TYPE_GET_SINGLE_FILE:
        forward<GetSingleReq, GetSingleResp, FileService_Stub>(
            rid, creq.body(), channel, &FileService_Stub::GetSingle, res, user_id);
        break;
      case REQ_TYPE_GET_BATCH_FILE:
        forward<GetBatchReq, GetBatchResp, FileService_Stub>(
            rid, creq.body(), channel, &FileService_Stub::GetBatch, res, user_id);
        break;

      default:
        reply(res, rid, false, "未实现的分发分支");
        break;
    }
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
