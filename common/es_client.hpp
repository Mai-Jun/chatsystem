#pragma once
#include <optional>
#include <mutex>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "logger.hpp"

// ============================================================
// Elasticsearch 轻量客户端（HTTP/JSON）：
// 消息存储子服务写 MySQL 后异步双写索引；关键字搜索走这里。
// ES 不可用时上层降级 MySQL LIKE。
// ============================================================
namespace im {

class EsClient {
 public:
  explicit EsClient(const std::string& es_host) {
    // es_host 形如 "http://127.0.0.1:9200"
    std::string s = es_host;
    auto scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    auto colon = s.rfind(':');
    host_ = (colon == std::string::npos) ? s : s.substr(0, colon);
    port_ = (colon == std::string::npos) ? 9200 : std::stoi(s.substr(colon + 1));
  }

  // 建索引（幂等；body 为 settings+mappings JSON）
  bool create_index(const std::string& index, const std::string& body) {
    std::lock_guard<std::mutex> lock(mutex_);
    httplib::Client cli(host_, port_);
    auto res = cli.Put(("/" + index).c_str(), body, "application/json");
    // 400 + resource_already_exists_exception 视为成功
    return res && (res->status == 200 || res->status == 201);
  }

  bool index_doc(const std::string& index, const std::string& doc_id, const std::string& body) {
    std::lock_guard<std::mutex> lock(mutex_);
    httplib::Client cli(host_, port_);
    auto res = cli.Put(("/" + index + "/_doc/" + doc_id).c_str(), body, "application/json");
    return res && (res->status == 200 || res->status == 201);
  }

  // 搜索：query_json 为 ES DSL 的 {"query":...,"from":..,"size":..} 部分
  std::optional<std::string> search(const std::string& index, const std::string& query_json) {
    std::lock_guard<std::mutex> lock(mutex_);
    httplib::Client cli(host_, port_);
    auto res = cli.Post(("/" + index + "/_search").c_str(), query_json, "application/json");
    if (!res || res->status != 200) {
      LOG_WARN("ES 搜索失败: status={}", res ? res->status : -1);
      return std::nullopt;
    }
    return res->body;
  }

  bool healthy() {
    std::lock_guard<std::mutex> lock(mutex_);
    httplib::Client cli(host_, port_);
    auto res = cli.Get("/_cluster/health");
    return res && res->status == 200;
  }

 private:
  std::string host_;
  int port_ = 9200;
  std::mutex mutex_;
};

}  // namespace im
