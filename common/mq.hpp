#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <amqp.h>

// ============================================================
// RabbitMQ 封装（rabbitmq-c）：topic 交换机 + 持久化消息
// - MqPublisher: 消息转发子服务投递消息
// - MqSubscriber: 网关节点订阅广播队列，收到后经 WebSocket 推送
// 断线自动重连（2 秒间隔）。
// ============================================================
namespace im {

class MqPublisher {
 public:
  explicit MqPublisher(std::string exchange);
  ~MqPublisher();

  bool connect();  // 建连 + 声明交换机（topic, durable）
  // 发布持久化消息；连接断开时自动重连一次
  bool publish(const std::string& routing_key, const std::string& body);

 private:
  bool declare_exchange();

  amqp_connection_state_t conn_ = nullptr;
  std::string exchange_;
  std::mutex mutex_;
};

class MqSubscriber {
 public:
  // routing_key / body 交给上层处理（例如 protobuf 反序列化后推送）
  using MessageCallback = std::function<void(const std::string& routing_key,
                                             const std::string& body)>;
  // queue: 网关节点独占队列（建议带 instance_id 后缀）；binding_key: 如 "message.new"
  MqSubscriber(std::string host, int port, std::string user, std::string password,
               std::string exchange, std::string queue, std::string binding_key,
               MessageCallback cb);
  ~MqSubscriber();

  bool start();  // 启动后台消费线程（断线重连）
  void stop();

 private:
  void consume_loop();
  bool setup(amqp_connection_state_t conn);  // 声明交换机/队列/绑定
  bool consume_once(amqp_connection_state_t conn);

  std::string host_;
  int port_ = 5672;
  std::string user_;
  std::string password_;
  std::string exchange_;
  std::string queue_;
  std::string binding_key_;
  MessageCallback cb_;

  amqp_connection_state_t conn_ = nullptr;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace im
