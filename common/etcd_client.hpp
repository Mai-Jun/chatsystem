#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// etcd v3 HTTP/JSON 客户端 + 服务注册/发现
// 说明：不引入 etcd-cpp-apiv3（依赖 gRPC 太重），直接调 etcd 的
//       grpc-gateway HTTP 接口（/v3/kv/put、/v3/lease/...）。
//       注册: 租约(TTL) + 周期续约，进程宕机租约到期自动摘除。
//       发现: 定时 range 拉取服务前缀下的全部节点。
// ============================================================
namespace im {

// 底层 KV / 租约操作（非线程安全的连接，内部加锁）
class EtcdClient {
 public:
  explicit EtcdClient(const std::string& endpoints);

  bool put(const std::string& key, const std::string& value, int64_t lease_id = 0);
  bool del_prefix(const std::string& prefix);
  bool del_key(const std::string& key);
  // 前缀查询：返回 {key -> value}
  bool range_prefix(const std::string& prefix, std::map<std::string, std::string>* out);

  int64_t lease_grant(int64_t ttl_seconds);  // 失败返回 0
  bool lease_keepalive(int64_t lease_id);
  bool lease_revoke(int64_t lease_id);

 private:
  // endpoints 形如 "http://127.0.0.1:2379,http://..."，取第一个
  bool parse_endpoint(const std::string& endpoints, std::string* host, int* port);
  // etcd v3 的 key/value 是 base64
  std::string b64(const std::string& raw);

  std::string host_;
  int port_ = 2379;
  std::mutex mutex_;
};

// 节点注册：启动时注册自己，后台线程续约，析构时主动注销
class ServiceRegistry {
 public:
  // register_host: 本节点对外可达地址（如 127.0.0.1 或内网 IP）
  ServiceRegistry(const std::string& endpoints, const std::string& service_name,
                  const std::string& instance_id, const std::string& register_host,
                  int listen_port, int lease_ttl, int keepalive_interval);
  ~ServiceRegistry();

  bool start();   // 注册 + 启动续约线程
  void stop();    // 主动注销并停线程

 private:
  std::string registry_key() const;

  std::shared_ptr<EtcdClient> client_;
  std::string service_name_;
  std::string instance_id_;
  std::string register_host_;
  int listen_port_ = 0;
  int lease_ttl_ = 10;
  int keepalive_interval_ = 3;
  int64_t lease_id_ = 0;
  std::atomic<bool> running_{false};
  std::thread keepalive_thread_;
};

// 服务发现：轮询某服务前缀下的全部节点，变更时回调
class ServiceDiscovery {
 public:
  using ChangeCallback = std::function<void(const std::vector<std::string>& /*addrs*/)>;

  ServiceDiscovery(const std::string& endpoints, const std::string& service_name,
                   int poll_interval_ms, ChangeCallback cb);
  ~ServiceDiscovery();

  void start();
  void stop();
  std::vector<std::string> current_addrs();  // 当前节点快照 "ip:port"

 private:
  void poll_loop();

  std::shared_ptr<EtcdClient> client_;
  std::string service_name_;
  int poll_interval_ms_ = 3000;
  ChangeCallback cb_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::mutex mutex_;
  std::vector<std::string> addrs_;
};

}  // namespace im
