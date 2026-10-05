#include "mq.hpp"

#include <amqp_tcp_socket.h>
#include <chrono>

#include "flags.hpp"
#include "logger.hpp"

namespace im {

namespace {

// noblock 等待用的 1 秒超时
timeval one_sec_tv() {
  timeval tv{};
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  return tv;
}

// 打开连接并登录（不声明任何对象）
bool open_connection(const std::string& host, int port, const std::string& user,
                     const std::string& password, amqp_connection_state_t* out) {
  amqp_connection_state_t conn = amqp_new_connection();
  amqp_socket_t* socket = amqp_tcp_socket_new(conn);
  if (socket == nullptr) {
    amqp_destroy_connection(conn);
    return false;
  }
  if (amqp_socket_open(socket, host.c_str(), port) != AMQP_STATUS_OK) {
    amqp_destroy_connection(conn);
    return false;
  }
  amqp_rpc_reply_t r = amqp_login(conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
                                  user.c_str(), password.c_str());
  if (r.reply_type != AMQP_RESPONSE_NORMAL) {
    amqp_destroy_connection(conn);
    return false;
  }
  amqp_channel_open(conn, 1);
  r = amqp_get_rpc_reply(conn);
  if (r.reply_type != AMQP_RESPONSE_NORMAL) {
    amqp_destroy_connection(conn);
    return false;
  }
  *out = conn;
  return true;
}

void close_connection(amqp_connection_state_t conn) {
  if (conn == nullptr) return;
  amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
  amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
  amqp_destroy_connection(conn);
}

}  // namespace

// ---------------- MqPublisher ----------------

MqPublisher::MqPublisher(std::string exchange) : exchange_(std::move(exchange)) {}

MqPublisher::~MqPublisher() { close_connection(conn_); }

bool MqPublisher::declare_exchange() {
  amqp_exchange_declare(conn_, 1, amqp_cstring_bytes(exchange_.c_str()),
                        amqp_cstring_bytes("topic"), 0 /*passive*/, 1 /*durable*/, 0 /*auto_del*/,
                        0 /*internal*/, amqp_empty_table);
  amqp_rpc_reply_t r = amqp_get_rpc_reply(conn_);
  if (r.reply_type != AMQP_RESPONSE_NORMAL) {
    LOG_ERROR("MQ 声明交换机失败: {} err={}", exchange_,
              amqp_error_string2(r.library_error));
    return false;
  }
  return true;
}

bool MqPublisher::connect() {
  if (!open_connection(FLAGS_rabbitmq_host, FLAGS_rabbitmq_port, FLAGS_rabbitmq_user,
                       FLAGS_rabbitmq_password, &conn_)) {
    LOG_ERROR("MQ 连接失败");
    return false;
  }
  if (!declare_exchange()) {
    close_connection(conn_);
    conn_ = nullptr;
    return false;
  }
  return true;
}

bool MqPublisher::publish(const std::string& routing_key, const std::string& body) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (conn_ == nullptr && !connect()) return false;

  amqp_basic_properties_t props;
  props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
  props.content_type = amqp_cstring_bytes("application/octet-stream");
  props.delivery_mode = 2;  // 持久化
  amqp_bytes_t payload = amqp_cstring_bytes(body.c_str());
  int rc = amqp_basic_publish(conn_, 1, amqp_cstring_bytes(exchange_.c_str()),
                              amqp_cstring_bytes(routing_key.c_str()), 0, 0, &props, payload);
  if (rc != AMQP_STATUS_OK) {
    LOG_WARN("MQ publish 失败 rc={}，尝试重连重发", rc);
    close_connection(conn_);
    conn_ = nullptr;
    if (!connect()) return false;
    rc = amqp_basic_publish(conn_, 1, amqp_cstring_bytes(exchange_.c_str()),
                            amqp_cstring_bytes(routing_key.c_str()), 0, 0, &props, payload);
    return rc == AMQP_STATUS_OK;
  }
  return true;
}

// ---------------- MqSubscriber ----------------

MqSubscriber::MqSubscriber(std::string host, int port, std::string user, std::string password,
                           std::string exchange, std::string queue, std::string binding_key,
                           MessageCallback cb)
    : host_(std::move(host)),
      port_(port),
      user_(std::move(user)),
      password_(std::move(password)),
      exchange_(std::move(exchange)),
      queue_(std::move(queue)),
      binding_key_(std::move(binding_key)),
      cb_(std::move(cb)) {}

MqSubscriber::~MqSubscriber() { stop(); }

bool MqSubscriber::setup(amqp_connection_state_t conn) {
  amqp_exchange_declare(conn, 1, amqp_cstring_bytes(exchange_.c_str()),
                        amqp_cstring_bytes("topic"), 0, 1, 0, 0, amqp_empty_table);
  amqp_rpc_reply_t r = amqp_get_rpc_reply(conn);
  if (r.reply_type != AMQP_RESPONSE_NORMAL) return false;

  amqp_queue_declare(conn, 1, amqp_cstring_bytes(queue_.c_str()), 0 /*passive*/, 1 /*durable*/,
                     0 /*exclusive*/, 0 /*auto_del*/, amqp_empty_table);
  r = amqp_get_rpc_reply(conn);
  if (r.reply_type != AMQP_RESPONSE_NORMAL) return false;

  amqp_queue_bind(conn, 1, amqp_cstring_bytes(queue_.c_str()),
                  amqp_cstring_bytes(exchange_.c_str()), amqp_cstring_bytes(binding_key_.c_str()),
                  amqp_empty_table);
  r = amqp_get_rpc_reply(conn);
  if (r.reply_type != AMQP_RESPONSE_NORMAL) return false;

  amqp_basic_consume(conn, 1, amqp_cstring_bytes(queue_.c_str()), amqp_empty_bytes, 0, 1 /*no_ack*/,
                     0, amqp_empty_table);
  r = amqp_get_rpc_reply(conn);
  return r.reply_type == AMQP_RESPONSE_NORMAL;
}

bool MqSubscriber::consume_once(amqp_connection_state_t conn) {
  amqp_frame_t frame;
  timeval tv = one_sec_tv();
  amqp_maybe_release_buffers(conn);
  amqp_rpc_reply_t r = amqp_simple_wait_frame_noblock(conn, &frame, &tv);
  if (r.reply_type != AMQP_RESPONSE_NORMAL) return false;

  if (frame.frame_type != AMQP_FRAME_METHOD) return true;
  if (frame.payload.method.id != AMQP_BASIC_DELIVER_METHOD) return true;

  auto* deliver = reinterpret_cast<amqp_basic_deliver_t*>(frame.payload.method.decoded);

  // header
  amqp_simple_wait_frame_noblock(conn, &frame, &tv);
  if (frame.frame_type != AMQP_FRAME_HEADER) return true;
  uint64_t body_size = frame.payload.properties.body_size;

  std::string body;
  body.reserve(static_cast<size_t>(body_size));
  while (body.size() < body_size) {
    amqp_simple_wait_frame_noblock(conn, &frame, &tv);
    if (frame.frame_type != AMQP_FRAME_BODY) break;
    body.append(reinterpret_cast<char*>(frame.payload.body_fragment.bytes),
                frame.payload.body_fragment.len);
  }

  amqp_bytes_t rk = deliver->routing_key;
  if (cb_) cb_(std::string(reinterpret_cast<char*>(rk.bytes), rk.len), body);
  return true;
}

bool MqSubscriber::start() {
  if (running_) return true;
  running_ = true;
  thread_ = std::thread([this]() { consume_loop(); });
  return true;
}

void MqSubscriber::stop() {
  if (!running_) return;
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

void MqSubscriber::consume_loop() {
  while (running_) {
    amqp_connection_state_t conn = nullptr;
    if (!open_connection(host_, port_, user_, password_, &conn)) {
      LOG_WARN("MQ 消费连接失败，2 秒后重试");
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }
    if (!setup(conn)) {
      LOG_WARN("MQ 声明失败，2 秒后重试");
      close_connection(conn);
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }
    LOG_INFO("MQ 消费就绪: queue={} binding={}", queue_, binding_key_);
    while (running_) {
      if (!consume_once(conn)) {
        if (!running_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        continue;
      }
    }
    close_connection(conn);
  }
}

}  // namespace im
