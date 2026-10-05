#pragma once
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <sw/redis++/redis++.h>

#include "flags.hpp"
#include "logger.hpp"

// ============================================================
// Redis 封装（redis-plus-plus）：登录态/在线状态/验证码/热点缓存
// 所有接口失败返回 false / nullopt 并打日志，不向上抛异常
// ============================================================
namespace im {

class RedisClient {
 public:
  RedisClient() { init_from_flags(); }
  explicit RedisClient(std::unique_ptr<sw::redis::Redis> redis) : redis_(std::move(redis)) {}

  bool set(const std::string& key, const std::string& val) {
    try {
      redis_->set(key, val);
      return true;
    } catch (const std::exception& e) {
      LOG_ERROR("redis set 失败: {}", e.what());
      return false;
    }
  }

  bool setex(const std::string& key, const std::string& val, int ttl_seconds) {
    try {
      redis_->set(key, val, std::chrono::seconds(ttl_seconds));
      return true;
    } catch (const std::exception& e) {
      LOG_ERROR("redis setex 失败: {}", e.what());
      return false;
    }
  }

  std::optional<std::string> get(const std::string& key) {
    try {
      auto v = redis_->get(key);
      if (!v) return std::nullopt;
      return *v;
    } catch (const std::exception& e) {
      LOG_ERROR("redis get 失败: {}", e.what());
      return std::nullopt;
    }
  }

  bool del(const std::string& key) {
    try {
      redis_->del(key);
      return true;
    } catch (const std::exception& e) {
      LOG_ERROR("redis del 失败: {}", e.what());
      return false;
    }
  }

  bool exists(const std::string& key) {
    try {
      return redis_->exists(key) > 0;
    } catch (const std::exception& e) {
      LOG_ERROR("redis exists 失败: {}", e.what());
      return false;
    }
  }

  bool hset(const std::string& key, const std::string& field, const std::string& val) {
    try {
      redis_->hset(key, field, val);
      return true;
    } catch (const std::exception& e) {
      LOG_ERROR("redis hset 失败: {}", e.what());
      return false;
    }
  }

  std::optional<std::string> hget(const std::string& key, const std::string& field) {
    try {
      auto v = redis_->hget(key, field);
      if (!v) return std::nullopt;
      return *v;
    } catch (const std::exception& e) {
      LOG_ERROR("redis hget 失败: {}", e.what());
      return std::nullopt;
    }
  }

  bool expire(const std::string& key, int ttl_seconds) {
    try {
      return redis_->expire(key, std::chrono::seconds(ttl_seconds));
    } catch (const std::exception& e) {
      LOG_ERROR("redis expire 失败: {}", e.what());
      return false;
    }
  }

 private:
  void init_from_flags() {
    sw::redis::ConnectionOptions opts;
    opts.host = FLAGS_redis_host;
    opts.port = FLAGS_redis_port;
    opts.connect_timeout = std::chrono::milliseconds(2000);
    opts.socket_timeout = std::chrono::milliseconds(2000);
    redis_ = std::make_unique<sw::redis::Redis>(opts);
  }

  std::unique_ptr<sw::redis::Redis> redis_;
};

}  // namespace im
