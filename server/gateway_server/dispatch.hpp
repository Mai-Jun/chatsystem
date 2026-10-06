#pragma once
#include <string>
#include <type_traits>

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <google/protobuf/message_lite.h>

#include "gateway.pb.h"

// ============================================================
// 网关分发框架
//
// 拆分动机（重要）：本项目的目标服务器仅 1.6G 内存 / 2 核，
// 若把 brpc + redis++ + 全部 protobuf 头文件塞进单个编译单元，
// GCC 峰值内存可达 1GB+，会把机器压垮（sshd 失联）。
// 因此按子服务拆分为多个 .cc，每个只包含自己需要的 pb.h。
//
// 各 dispatcher 返回 true 表示"该类型由我处理"（已填好 result）。
// ============================================================
namespace im {
namespace gateway {

struct Result {
  bool success = false;
  std::string errmsg;
  std::string body;  // 序列化后的 Resp（可为空）
};

struct Ctx {
  RequestType type = REQ_TYPE_UNKNOWN;
  const std::string* body = nullptr;     // ClientRequest.body
  const std::string* token = nullptr;    // ClientRequest.token（登出等直接用）
  const std::string* user_id = nullptr;  // 鉴权注入的 user_id（可为空串）
  brpc::Channel* channel = nullptr;      // 目标子服务通道
};

// 各子服务分发实现（分文件，降低单 TU 内存峰值）
bool dispatch_user(const Ctx& ctx, Result* out);      // user_server  (handlers_user.cc)
bool dispatch_friend(const Ctx& ctx, Result* out);    // friend_server (handlers_friend.cc)
bool dispatch_msg(const Ctx& ctx, Result* out);       // message_storage_server
bool dispatch_file(const Ctx& ctx, Result* out);      // file_server
bool dispatch_speech(const Ctx& ctx, Result* out);    // speech_server (M8)
bool dispatch_transmit(const Ctx& ctx, Result* out);  // message_server (handlers_transmit.cc)

// 探测请求类型是否有 user_id 字段（注册/登录/验证码类请求没有）
template <typename T, typename = void>
struct has_user_id : std::false_type {};
template <typename T>
struct has_user_id<T, std::void_t<decltype(std::declval<T&>().set_user_id(std::string()))>>
    : std::true_type {};

// 通用转发：解析 body → (若有 user_id 字段则注入) → 调子服务 → 填充 result
template <typename ReqT, typename RespT, typename StubT, typename MethodT>
void forward(const Ctx& ctx, MethodT method, Result* out) {
  ReqT req;
  if (ctx.body != nullptr && !ctx.body->empty() && !req.ParseFromString(*ctx.body)) {
    out->success = false;
    out->errmsg = "body 解析失败";
    return;
  }
  if constexpr (has_user_id<ReqT>::value) {
    if (ctx.user_id != nullptr && !ctx.user_id->empty()) {
      req.set_user_id(*ctx.user_id);  // 以服务端鉴权结果为准，防客户端伪造
    }
  }
  StubT stub(ctx.channel);
  brpc::Controller cntl;
  RespT resp;
  (stub.*method)(&cntl, &req, &resp, nullptr);
  if (cntl.Failed()) {
    out->success = false;
    out->errmsg = "子服务调用失败: " + cntl.ErrorText();
    return;
  }
  out->success = resp.success();
  out->errmsg = resp.errmsg();
  out->body = resp.SerializeAsString();
}

}  // namespace gateway
}  // namespace im
