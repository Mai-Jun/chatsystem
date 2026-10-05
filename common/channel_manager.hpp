#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <brpc/channel.h>

#include "etcd_client.hpp"

// ============================================================
// brpc 通道管理：对每个子服务类型维护一条通道（负载均衡到全部存活节点）
// 内部持有一个 ServiceDiscovery，节点列表变化时重建通道。
// 用法:
//   ChannelManager mgr(FLAGS_etcd_endpoints);
//   mgr.discover("user_server");                       // 启动对某服务的发现
//   auto* ch = mgr.channel("user_server");             // 取通道发 RPC
// ============================================================
namespace im {

class ChannelManager {
 public:
  explicit ChannelManager(const std::string& etcd_endpoints);

  // 启动对某服务的发现（幂等；重复调用仅忽略）
  void discover(const std::string& service_name);

  // 获取该服务当前可用通道；从未发现过或尚无节点时返回 nullptr
  brpc::Channel* channel(const std::string& service_name);

 private:
  struct Entry {
    std::shared_ptr<ServiceDiscovery> discovery;
    std::shared_ptr<brpc::Channel> channel;
  };

  void rebuild(const std::string& service_name, const std::vector<std::string>& addrs);

  std::string etcd_endpoints_;
  std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;
};

}  // namespace im
