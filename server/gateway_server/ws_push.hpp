#pragma once
#include <memory>
#include <string>

#include "gateway.pb.h"

// ============================================================
// WebSocket 推送的轻量接口
//
// 刻意不在本头文件里包含 websocketpp/asio/brpc 等重头文件：
// 具体实现在 ws_push.cc 中（pimpl），以保持网关主文件编译内存低
// （目标服务器 1.6G 内存，见 docs/env.md 编译约束）。
//
// 客户端协议：
//   ① 连接 ws://host:9001
//   ② 首帧发送鉴权帧：ClientRequest（token 必填，body/type 忽略）
//   ③ 网关校验 token（Redis）→ 绑定连接↔user_id → 回 ServerResponse{success}
//   ④ 之后网关通过 ServerPush 主动推送（新消息/好友申请/群事件）
// ============================================================
namespace im {

class WsPusher {
 public:
  virtual ~WsPusher() = default;
  virtual bool start() = 0;
  virtual void stop() = 0;
  // 推送给某用户在本节点上的全部连接（离线返回 false）
  virtual bool push_to_user(const std::string& user_id, const ServerPush& push) = 0;
  virtual bool is_online(const std::string& user_id) = 0;
};

// 工厂：port=WebSocket 端口；instance_id 用于 Redis 在线状态标记
std::unique_ptr<WsPusher> make_ws_pusher(int port, const std::string& instance_id);

// 启动 MQ→WS 桥接：订阅广播队列（绑定 "#"），按 routing_key 分发：
//   message.new   → MessageDelivery  → 推给会话全部在线成员（PUSH_TYPE_NEW_MESSAGE）
//   friend.apply  → FriendApplyInfo  → 推给被申请人    （PUSH_TYPE_FRIEND_APPLY）
//   group.event   → GroupEventInfo   → 推给被邀请人    （PUSH_TYPE_GROUP_EVENT）
// 返回值由调用方持有（析构即停止订阅）；用 shared_ptr<void> 隐藏实现类型。
std::shared_ptr<void> start_mq_bridge(WsPusher* pusher, const std::string& instance_id);

}  // namespace im
