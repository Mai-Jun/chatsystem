#include "etcd_client.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "logger.hpp"
#include "util.hpp"

namespace im {

using nlohmann::json;

EtcdClient::EtcdClient(const std::string& endpoints) {
  std::string first = endpoints;
  auto comma = first.find(',');
  if (comma != std::string::npos) first = first.substr(0, comma);
  if (!parse_endpoint(first, &host_, &port_)) {
    LOG_ERROR("etcd endpoints 解析失败: {}", endpoints);
  }
}

bool EtcdClient::parse_endpoint(const std::string& endpoints, std::string* host, int* port) {
  // 支持 "http://host:port" 或 "host:port"
  std::string s = endpoints;
  auto scheme = s.find("://");
  if (scheme != std::string::npos) s = s.substr(scheme + 3);
  auto colon = s.rfind(':');
  if (colon == std::string::npos) return false;
  *host = s.substr(0, colon);
  try {
    *port = std::stoi(s.substr(colon + 1));
  } catch (...) {
    return false;
  }
  return !host->empty() && *port > 0;
}

std::string EtcdClient::b64(const std::string& raw) { return base64_encode(raw); }

bool EtcdClient::put(const std::string& key, const std::string& value, int64_t lease_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(3, 0);
  json req;
  req["key"] = b64(key);
  req["value"] = b64(value);
  if (lease_id > 0) req["lease"] = lease_id;
  auto res = cli.Post("/v3/kv/put", req.dump(), "application/json");
  if (!res || res->status != 200) {
    LOG_WARN("etcd put 失败: key={} status={}", key, res ? res->status : -1);
    return false;
  }
  return true;
}

bool EtcdClient::del_key(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(3, 0);
  json req;
  req["key"] = b64(key);
  auto res = cli.Post("/v3/kv/deleterange", req.dump(), "application/json");
  return res && res->status == 200;
}

bool EtcdClient::del_prefix(const std::string& prefix) {
  std::lock_guard<std::mutex> lock(mutex_);
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(3, 0);
  // range_end = prefix + 1（最后一个字节 +1），即覆盖整个前缀区间
  std::string range_end = prefix;
  range_end[range_end.size() - 1] += 1;
  json req;
  req["key"] = b64(prefix);
  req["range_end"] = b64(range_end);
  auto res = cli.Post("/v3/kv/deleterange", req.dump(), "application/json");
  return res && res->status == 200;
}

bool EtcdClient::range_prefix(const std::string& prefix, std::map<std::string, std::string>* out) {
  if (out == nullptr) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(3, 0);
  std::string range_end = prefix;
  range_end[range_end.size() - 1] += 1;
  json req;
  req["key"] = b64(prefix);
  req["range_end"] = b64(range_end);
  auto res = cli.Post("/v3/kv/range", req.dump(), "application/json");
  if (!res || res->status != 200) {
    LOG_WARN("etcd range 失败: prefix={} status={}", prefix, res ? res->status : -1);
    return false;
  }
  out->clear();
  try {
    json resp = json::parse(res->body);
    if (resp.contains("kvs")) {
      for (auto& kv : resp["kvs"]) {
        std::string k = base64_decode(kv.value("key", ""));
        std::string v = base64_decode(kv.value("value", ""));
        (*out)[k] = v;
      }
    }
  } catch (const std::exception& e) {
    LOG_ERROR("etcd range 响应解析失败: {}", e.what());
    return false;
  }
  return true;
}

int64_t EtcdClient::lease_grant(int64_t ttl_seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(3, 0);
  json req;
  req["TTL"] = ttl_seconds;
  auto res = cli.Post("/v3/lease/grant", req.dump(), "application/json");
  if (!res || res->status != 200) {
    LOG_WARN("etcd lease grant 失败: status={}", res ? res->status : -1);
    return 0;
  }
  try {
    json resp = json::parse(res->body);
    // ID 字段是字符串形式的大整数
    return std::stoll(resp.value("ID", "0"));
  } catch (const std::exception& e) {
    LOG_ERROR("etcd lease grant 解析失败: {}", e.what());
    return 0;
  }
}

bool EtcdClient::lease_keepalive(int64_t lease_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(3, 0);
  json req;
  req["ID"] = lease_id;
  auto res = cli.Post("/v3/lease/keepalive", req.dump(), "application/json");
  return res && res->status == 200;
}

bool EtcdClient::lease_revoke(int64_t lease_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  httplib::Client cli(host_, port_);
  cli.set_connection_timeout(3, 0);
  cli.set_read_timeout(3, 0);
  json req;
  req["ID"] = lease_id;
  auto res = cli.Post("/v3/lease/revoke", req.dump(), "application/json");
  return res && res->status == 200;
}

// ---------------- ServiceRegistry ----------------

ServiceRegistry::ServiceRegistry(const std::string& endpoints, const std::string& service_name,
                                 const std::string& instance_id, const std::string& register_host,
                                 int listen_port, int lease_ttl, int keepalive_interval)
    : service_name_(service_name),
      instance_id_(instance_id),
      register_host_(register_host),
      listen_port_(listen_port),
      lease_ttl_(lease_ttl),
      keepalive_interval_(keepalive_interval) {
  client_ = std::make_shared<EtcdClient>(endpoints);
}

std::string ServiceRegistry::registry_key() const {
  return "/im/registry/" + service_name_ + "/" + instance_id_;
}

bool ServiceRegistry::start() {
  lease_id_ = client_->lease_grant(lease_ttl_);
  if (lease_id_ == 0) {
    LOG_ERROR("服务注册失败: 租约授予失败 ({})", registry_key());
    return false;
  }
  if (!client_->put(registry_key(), register_host_ + ":" + std::to_string(listen_port_), lease_id_)) {
    LOG_ERROR("服务注册失败: key 写入失败 ({});", registry_key());
    return false;
  }
  running_ = true;
  keepalive_thread_ = std::thread([this]() {
    while (running_) {
      std::this_thread::sleep_for(std::chrono::seconds(keepalive_interval_));
      if (!running_) break;
      if (!client_->lease_keepalive(lease_id_)) {
        // 续约失败：重新授予租约并重写 key（etcd 重启等情况）
        LOG_WARN("租约续约失败，尝试重新注册: {}", registry_key());
        int64_t new_lease = client_->lease_grant(lease_ttl_);
        if (new_lease != 0) {
          lease_id_ = new_lease;
          client_->put(registry_key(), register_host_ + ":" + std::to_string(listen_port_), lease_id_);
        }
      }
    }
  });
  LOG_INFO("服务注册成功: {} = {}:{} (lease={})", registry_key(), register_host_, listen_port_, lease_id_);
  return true;
}

void ServiceRegistry::stop() {
  if (!running_) return;
  running_ = false;
  if (keepalive_thread_.joinable()) keepalive_thread_.join();
  client_->del_key(registry_key());
  client_->lease_revoke(lease_id_);
  LOG_INFO("服务注销: {}", registry_key());
}

ServiceRegistry::~ServiceRegistry() { stop(); }

// ---------------- ServiceDiscovery ----------------

ServiceDiscovery::ServiceDiscovery(const std::string& endpoints, const std::string& service_name,
                                   int poll_interval_ms, ChangeCallback cb)
    : service_name_(service_name), poll_interval_ms_(poll_interval_ms), cb_(std::move(cb)) {
  client_ = std::make_shared<EtcdClient>(endpoints);
}

ServiceDiscovery::~ServiceDiscovery() { stop(); }

void ServiceDiscovery::start() {
  running_ = true;
  thread_ = std::thread([this]() { poll_loop(); });
}

void ServiceDiscovery::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

std::vector<std::string> ServiceDiscovery::current_addrs() {
  std::lock_guard<std::mutex> lock(mutex_);
  return addrs_;
}

void ServiceDiscovery::poll_loop() {
  std::string prefix = "/im/registry/" + service_name_ + "/";
  while (running_) {
    std::map<std::string, std::string> kv;
    if (client_->range_prefix(prefix, &kv)) {
      std::vector<std::string> new_addrs;
      for (auto& [k, v] : kv) new_addrs.push_back(v);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (new_addrs != addrs_) {
          addrs_ = new_addrs;
          if (cb_) cb_(addrs_);
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
  }
}

}  // namespace im
