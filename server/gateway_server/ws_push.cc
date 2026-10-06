#include "ws_push.hpp"

// 注意：Ubuntu 的 libwebsocketpp-dev 0.8.2 里 config/asio_no_tls.hpp 内容有误
// （实际定义的是 struct asio，且 include guard 与 asio.hpp 相同），
// 因此这里使用 websocketpp::config::asio —— 同为无 TLS 的 asio 传输。
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "flags.hpp"
#include "logger.hpp"
#include "redis_client.hpp"
#include "util.hpp"

namespace im {

namespace {

using WsServer = websocketpp::server<websocketpp::config::asio>;
using ConnHdl = websocketpp::connection_hdl;

// weak_ptr 比较器：用作 map key
struct HdlLess {
  bool operator()(const ConnHdl& a, const ConnHdl& b) const {
    return std::owner_less<ConnHdl>()(a, b);
  }
};

class WsPusherImpl : public WsPusher {
 public:
  WsPusherImpl(int port, std::string instance_id)
      : port_(port), instance_id_(std::move(instance_id)), redis_() {}

  bool start() override {
    try {
      server_.init_asio();
      server_.set_reuse_addr(true);
      server_.clear_access_channels(websocketpp::log::alevel::all);
      server_.clear_error_channels(websocketpp::log::elevel::all);

      server_.set_open_handler([this](ConnHdl hdl) { on_open(hdl); });
      server_.set_close_handler([this](ConnHdl hdl) { on_close(hdl); });
      server_.set_message_handler(
          [this](ConnHdl hdl, WsServer::message_ptr msg) { on_message(hdl, msg); });

      server_.listen(static_cast<uint16_t>(port_));
      server_.start_accept();
      running_ = true;
      thread_ = std::thread([this]() {
        try {
          server_.run();
        } catch (const std::exception& e) {
          LOG_ERROR("WS 事件循环退出: {}", e.what());
        }
      });
      LOG_INFO("WebSocket 推送就绪: 端口 {}", port_);
      return true;
    } catch (const std::exception& e) {
      LOG_ERROR("WebSocket 启动失败: {}", e.what());
      return false;
    }
  }

  void stop() override {
    if (!running_) return;
    running_ = false;
    try {
      server_.stop_listening();
      server_.stop();
    } catch (...) {
    }
    if (thread_.joinable()) thread_.join();
  }

  bool push_to_user(const std::string& user_id, const ServerPush& push) override {
    std::vector<ConnHdl> targets;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      auto it = user_conns_.find(user_id);
      if (it == user_conns_.end() || it->second.empty()) return false;
      targets.assign(it->second.begin(), it->second.end());
    }
    std::string payload = push.SerializeAsString();
    bool sent = false;
    for (const auto& hdl : targets) {
      try {
        server_.send(hdl, payload, websocketpp::frame::opcode::binary);
        sent = true;
      } catch (const std::exception& e) {
        LOG_WARN("WS 推送失败（连接可能已断）: {}", e.what());
      }
    }
    return sent;
  }

  bool is_online(const std::string& user_id) override {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = user_conns_.find(user_id);
    return it != user_conns_.end() && !it->second.empty();
  }

 private:
  void on_open(const ConnHdl& hdl) {
    std::lock_guard<std::mutex> lk(mutex_);
    unbound_.insert(hdl);
    LOG_INFO("WS 连接建立（等待鉴权帧）");
  }

  void on_close(const ConnHdl& hdl) {
    std::lock_guard<std::mutex> lk(mutex_);
    unbound_.erase(hdl);
    auto it = hdl_user_.find(hdl);
    if (it != hdl_user_.end()) {
      const std::string uid = it->second;
      hdl_user_.erase(it);
      auto u = user_conns_.find(uid);
      if (u != user_conns_.end()) {
        u->second.erase(hdl);
        if (u->second.empty()) {
          user_conns_.erase(u);
          // 该用户已无连接：清在线标记（仅当标记指向本节点）
          auto node = redis_.get("online:" + uid);
          if (node && *node == instance_id_) redis_.del("online:" + uid);
          LOG_INFO("用户离线: {}", uid);
        }
      }
    }
  }

  void on_message(const ConnHdl& hdl, const WsServer::message_ptr& msg) {
    const std::string& payload = msg->get_payload();
    // 已绑定连接：当前仅支持心跳/忽略
    {
      std::lock_guard<std::mutex> lk(mutex_);
      if (hdl_user_.find(hdl) != hdl_user_.end()) return;
    }
    // 未绑定 → 视为鉴权帧
    ClientRequest req;
    if (!req.ParseFromString(payload) || req.token().empty()) {
      ServerResponse bad;
      bad.set_success(false);
      bad.set_errmsg("鉴权帧格式错误：需为 ClientRequest 且带 token");
      send_response(hdl, bad);
      return;
    }
    auto uid = redis_.get("auth:token:" + req.token());
    if (!uid) {
      ServerResponse bad;
      bad.set_success(false);
      bad.set_errmsg("未登录或登录已过期");
      send_response(hdl, bad);
      return;
    }
    {
      std::lock_guard<std::mutex> lk(mutex_);
      unbound_.erase(hdl);
      hdl_user_[hdl] = *uid;
      user_conns_[*uid].insert(hdl);
    }
    redis_.setex("online:" + *uid, instance_id_, 300);  // 在线标记（连接期间续期）
    ServerResponse ok;
    ok.set_request_id(req.request_id());
    ok.set_success(true);
    ok.set_errmsg("ok");
    send_response(hdl, ok);
    LOG_INFO("WS 鉴权成功，绑定用户: {}", *uid);
  }

  void send_response(const ConnHdl& hdl, const ServerResponse& resp) {
    try {
      server_.send(hdl, resp.SerializeAsString(), websocketpp::frame::opcode::binary);
    } catch (const std::exception& e) {
      LOG_WARN("WS 回包失败: {}", e.what());
    }
  }

  int port_;
  std::string instance_id_;
  RedisClient redis_;
  WsServer server_;
  std::thread thread_;
  std::atomic<bool> running_{false};

  std::mutex mutex_;
  std::set<ConnHdl, HdlLess> unbound_;
  std::map<ConnHdl, std::string, HdlLess> hdl_user_;
  std::map<std::string, std::set<ConnHdl, HdlLess>> user_conns_;
};

}  // namespace

std::unique_ptr<WsPusher> make_ws_pusher(int port, const std::string& instance_id) {
  return std::make_unique<WsPusherImpl>(port, instance_id);
}

}  // namespace im
