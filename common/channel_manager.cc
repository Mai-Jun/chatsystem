#include "channel_manager.hpp"

#include "logger.hpp"

namespace im {

ChannelManager::ChannelManager(const std::string& etcd_endpoints)
    : etcd_endpoints_(etcd_endpoints) {}

void ChannelManager::rebuild(const std::string& service_name,
                             const std::vector<std::string>& addrs) {
  if (addrs.empty()) return;
  // brpc list:// 命名服务：list://ip:port,ip:port...
  std::string uri = "list://";
  for (size_t i = 0; i < addrs.size(); ++i) {
    if (i > 0) uri += ",";
    uri += addrs[i];
  }
  auto ch = std::make_shared<brpc::Channel>();
  brpc::ChannelOptions opts;
  opts.protocol = brpc::PROTOCOL_BAIDU_STD;
  opts.connection_type = "pooled";
  opts.connect_timeout_ms = 1000;
  opts.timeout_ms = 5000;
  opts.max_retry = 2;
  if (ch->Init(uri.c_str(), "rr", &opts) != 0) {
    LOG_ERROR("brpc 通道重建失败: service={} uri={}", service_name, uri);
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  entries_[service_name].channel = ch;
  LOG_INFO("brpc 通道已更新: service={} nodes={}", service_name, addrs.size());
}

void ChannelManager::discover(const std::string& service_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(service_name);
  if (it != entries_.end()) return;  // 已在发现

  Entry entry;
  auto self = this;
  entry.discovery = std::make_shared<ServiceDiscovery>(
      etcd_endpoints_, service_name, 2000,
      [self, service_name](const std::vector<std::string>& addrs) {
        self->rebuild(service_name, addrs);
      });
  entry.discovery->start();
  entries_[service_name] = std::move(entry);
  LOG_INFO("启动服务发现: {}", service_name);
}

brpc::Channel* ChannelManager::channel(const std::string& service_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(service_name);
  if (it == entries_.end()) return nullptr;
  return it->second.channel.get();
}

}  // namespace im
