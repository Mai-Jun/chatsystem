#include <memory>
#include <string>

#include "flags.hpp"
#include "friend.pb.h"
#include "logger.hpp"
#include "message_transmit.pb.h"
#include "mq.hpp"
#include "ws_push.hpp"

// ============================================================
// MQ → WebSocket 桥接（网关节点）
// 每个网关节点持有一个独占队列（gateway.<instance_id>），绑定 "#" 接收全部事件；
// 收到后按 routing_key 解析并只推给"本节点在线"的目标用户——
// 因此多网关节点部署时无需互相寻址，广播天然覆盖。
// ============================================================
namespace im {

namespace {

void handle_message_new(WsPusher* pusher, const std::string& body) {
  MessageDelivery delivery;
  if (!delivery.ParseFromString(body)) {
    LOG_WARN("MessageDelivery 解析失败");
    return;
  }
  ServerPush push;
  push.set_type(PUSH_TYPE_NEW_MESSAGE);
  push.set_body(delivery.message().SerializeAsString());
  int pushed = 0;
  for (const auto& uid : delivery.member_user_ids()) {
    if (pusher->push_to_user(uid, push)) ++pushed;
  }
  LOG_INFO("推送新消息: session={} msg_id={} 在线送达 {} 人", delivery.chat_session_id(),
           delivery.message().message_id(), pushed);
}

void handle_friend_apply(WsPusher* pusher, const std::string& body) {
  FriendApplyInfo info;
  if (!info.ParseFromString(body)) {
    LOG_WARN("FriendApplyInfo 解析失败");
    return;
  }
  ServerPush push;
  push.set_type(PUSH_TYPE_FRIEND_APPLY);
  push.set_body(info.SerializeAsString());
  if (pusher->push_to_user(info.peer_id(), push)) {
    LOG_INFO("推送好友申请: -> {}", info.peer_id());
  }
}

void handle_group_event(WsPusher* pusher, const std::string& body) {
  GroupEventInfo info;
  if (!info.ParseFromString(body)) {
    LOG_WARN("GroupEventInfo 解析失败");
    return;
  }
  ServerPush push;
  push.set_type(PUSH_TYPE_GROUP_EVENT);
  push.set_body(info.SerializeAsString());
  if (pusher->push_to_user(info.user_id(), push)) {
    LOG_INFO("推送群事件: -> {}", info.user_id());
  }
}

}  // namespace

std::shared_ptr<void> start_mq_bridge(WsPusher* pusher, const std::string& instance_id) {
  std::string queue = "gateway." + instance_id;
  auto sub = std::make_shared<MqSubscriber>(
      FLAGS_rabbitmq_host, FLAGS_rabbitmq_port, FLAGS_rabbitmq_user, FLAGS_rabbitmq_password,
      FLAGS_rabbitmq_exchange, queue, "#",
      [pusher](const std::string& routing_key, const std::string& body) {
        if (routing_key == "message.new") {
          handle_message_new(pusher, body);
        } else if (routing_key == "friend.apply") {
          handle_friend_apply(pusher, body);
        } else if (routing_key == "group.event") {
          handle_group_event(pusher, body);
        } else {
          LOG_DEBUG("忽略未知 routing_key: {}", routing_key);
        }
      });
  if (!sub->start()) {
    LOG_ERROR("MQ 桥接启动失败（离线用户将收不到实时推送，登录后拉历史可补齐）");
  }
  return sub;
}

}  // namespace im
