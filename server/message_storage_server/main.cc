#include <brpc/server.h>
#include <gflags/gflags.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "entities-odb.hxx"
#include "es_client.hpp"
#include "flags.hpp"
#include "logger.hpp"
#include "message_storage.pb.h"
#include "odb/database.hpp"
#include "odb/entities.hpp"
#include "etcd_client.hpp"
#include "util.hpp"

// ============================================================
// 消息存储子服务：
//   SaveMessage          MySQL 权威写入 + ES 异步双写（失败不影响主流程）
//   GetHistoryMessage    MySQL 游标分页（cursor=0 取最新一页，按时间升序返回）
//   SearchHistoryMessage ES(ik 分词) 优先；ES 不可用自动降级 MySQL LIKE
// 设计说明：MySQL 为权威存储，ES 仅作搜索索引层——ES 挂掉不丢消息。
// ============================================================
using namespace im;  // NOLINT

namespace {

const int kDefaultPageLimit = 50;

class AsyncIndexer {
 public:
  explicit AsyncIndexer(EsClient* es) : es_(es) {
    worker_ = std::thread([this]() { run(); });
  }
  ~AsyncIndexer() {
    {
      std::lock_guard<std::mutex> lk(m_);
      stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  // 入队（队列上限 10000，超出丢弃并告警，避免 ES 长期不可用时撑爆内存）
  void push(const std::string& doc_id, const std::string& body) {
    {
      std::lock_guard<std::mutex> lk(m_);
      if (q_.size() >= 10000) {
        LOG_WARN("ES 索引队列已满，丢弃文档 {}", doc_id);
        return;
      }
      q_.emplace_back(doc_id, body);
    }
    cv_.notify_one();
  }

 private:
  void run() {
    while (true) {
      std::pair<std::string, std::string> item;
      {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this]() { return stop_ || !q_.empty(); });
        if (stop_ && q_.empty()) return;
        item = q_.front();
        q_.pop_front();
      }
      if (!es_->index_doc("im-message", item.first, item.second)) {
        LOG_WARN("ES 索引写入失败（已降级，搜索将回落 MySQL LIKE）: {}", item.first);
      }
    }
  }

  EsClient* es_;
  std::deque<std::pair<std::string, std::string>> q_;
  std::mutex m_;
  std::condition_variable cv_;
  bool stop_ = false;
  std::thread worker_;
};

void fill_message(const Message& m, MessageInfo* info) {
  info->set_message_id(m.id());
  info->set_chat_session_id(m.session_id());
  info->set_sender_id(m.sender_id());
  info->set_type(static_cast<MessageType>(m.type()));
  info->set_content(m.content());
  info->set_file_id(m.file_id());
  info->set_file_name(m.file_name());
  info->set_file_size(m.file_size());
  info->set_asr_text(m.asr_text());
  info->set_create_time(m.create_time());
}

std::string message_to_es_json(const MessageInfo& m) {
  // 手工拼 JSON（字段均为受控类型；content 做最小转义）
  auto esc = [](const std::string& s) {
    std::string out;
    for (char c : s) {
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c);
      }
    }
    return out;
  };
  return "{\"message_id\":\"" + esc(m.message_id()) + "\",\"session_id\":\"" +
         esc(m.chat_session_id()) + "\",\"sender_id\":\"" + esc(m.sender_id()) +
         "\",\"type\":" + std::to_string(static_cast<int>(m.type())) + ",\"content\":\"" +
         esc(m.content()) + "\",\"asr_text\":\"" + esc(m.asr_text()) + "\",\"file_id\":\"" +
         esc(m.file_id()) + "\",\"create_time\":" + std::to_string(m.create_time()) + "}";
}

class MsgStorageServiceImpl : public MsgStorageService {
 public:
  MsgStorageServiceImpl(odb::mysql::database* db, EsClient* es, AsyncIndexer* indexer)
      : db_(db), es_(es), indexer_(indexer) {}

  // ---------------- 持久化 ----------------
  void SaveMessage(google::protobuf::RpcController* cntl, const SaveMessageReq* req,
                   SaveMessageResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    const auto& m = req->message();
    if (m.chat_session_id().empty() || m.sender_id().empty()) {
      resp->set_success(false);
      resp->set_errmsg("会话或发送者为空");
      return;
    }
    std::string id = m.message_id().empty() ? uuid() : m.message_id();
    int64_t ctime = m.create_time() == 0 ? now_seconds() : m.create_time();
    try {
      odb::transaction t(db_->begin());
      db_->persist(Message(id, m.chat_session_id(), m.sender_id(),
                           static_cast<int>(m.type()), m.content(), m.file_id(), m.file_name(),
                           m.file_size(), m.asr_text()));
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("消息入库失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("消息入库失败");
      return;
    }
    // ES 异步双写（回填 id/时间戳后再索引）
    MessageInfo idx = m;
    idx.set_message_id(id);
    idx.set_create_time(ctime);
    indexer_->push(id, message_to_es_json(idx));
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 历史消息 ----------------
  void GetHistoryMessage(google::protobuf::RpcController* cntl, const GetHistoryReq* req,
                         GetHistoryResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    int limit = req->limit() <= 0 ? kDefaultPageLimit : std::min(req->limit(), 200);
    try {
      odb::transaction t(db_->begin());
      std::vector<Message> page;
      if (req->cursor_timestamp() > 0) {
        // 取该时间戳之前的一页
        auto rs = db_->query<Message>(
            odb::query<Message>::session_id == req->chat_session_id() &&
            odb::query<Message>::create_time < req->cursor_timestamp() &&
            "ORDER BY create_time DESC LIMIT " + std::to_string(limit));
        for (auto it = rs.begin(); it != rs.end(); ++it) page.push_back(*it);
      } else {
        auto rs = db_->query<Message>(
            odb::query<Message>::session_id == req->chat_session_id() &&
            "ORDER BY create_time DESC LIMIT " + std::to_string(limit));
        for (auto it = rs.begin(); it != rs.end(); ++it) page.push_back(*it);
      }
      // 统一按时间升序返回，便于客户端顺序渲染
      std::reverse(page.begin(), page.end());
      for (const auto& m : page) fill_message(m, resp->add_messages());
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("历史消息查询失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("查询失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 关键字搜索 ----------------
  void SearchHistoryMessage(google::protobuf::RpcController* cntl, const SearchHistoryReq* req,
                            SearchHistoryResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    if (req->keyword().empty()) {
      resp->set_success(false);
      resp->set_errmsg("关键字为空");
      return;
    }
    // 1) 优先 ES
    if (es_->healthy()) {
      auto esc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
          if (c == '"' || c == '\\') o.push_back('\\');
          o.push_back(c);
        }
        return o;
      };
      std::string query =
          "{\"size\":100,\"query\":{\"bool\":{\"must\":[{\"multi_match\":{\"query\":\"" +
          esc(req->keyword()) + "\",\"fields\":[\"content\",\"asr_text\"]}}]";
      if (!req->chat_session_id().empty()) {
        query += ",{\"term\":{\"session_id\":\"" + esc(req->chat_session_id()) + "\"}}";
      }
      query += "]}},\"sort\":[{\"create_time\":\"asc\"}]}";
      auto body = es_->search("im-message", query);
      if (body) {
        try {
          auto j = nlohmann::json::parse(*body);
          for (const auto& hit : j["hits"]["hits"]) {
            const auto& src = hit["_source"];
            auto* info = resp->add_messages();
            info->set_message_id(src.value("message_id", ""));
            info->set_chat_session_id(src.value("session_id", ""));
            info->set_sender_id(src.value("sender_id", ""));
            info->set_type(static_cast<MessageType>(src.value("type", 0)));
            info->set_content(src.value("content", ""));
            info->set_asr_text(src.value("asr_text", ""));
            info->set_file_id(src.value("file_id", ""));
            info->set_create_time(src.value("create_time", static_cast<int64_t>(0)));
          }
          resp->set_success(true);
          resp->set_errmsg("ok(es)");
          return;
        } catch (const std::exception& e) {
          LOG_WARN("ES 结果解析失败，降级 LIKE: {}", e.what());
        }
      }
    }
    // 2) 降级：MySQL LIKE
    LOG_INFO("搜索降级 MySQL LIKE: keyword={}", req->keyword());
    try {
      odb::transaction t(db_->begin());
      std::string cond = "%" + req->keyword() + "%";
      std::vector<Message> hits;
      if (req->chat_session_id().empty()) {
        auto rs = db_->query<Message>(odb::query<Message>::content.like(cond) ||
                                      odb::query<Message>::asr_text.like(cond) &&
                                      "ORDER BY create_time ASC LIMIT 100");
        for (auto it = rs.begin(); it != rs.end(); ++it) hits.push_back(*it);
      } else {
        auto rs = db_->query<Message>(
            (odb::query<Message>::content.like(cond) || odb::query<Message>::asr_text.like(cond)) &&
            odb::query<Message>::session_id == req->chat_session_id() &&
            "ORDER BY create_time ASC LIMIT 100");
        for (auto it = rs.begin(); it != rs.end(); ++it) hits.push_back(*it);
      }
      for (const auto& m : hits) fill_message(m, resp->add_messages());
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("LIKE 搜索失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("搜索失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok(like-fallback)");
  }

 private:
  odb::mysql::database* db_;
  EsClient* es_;
  AsyncIndexer* indexer_;
};

}  // namespace

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  init_logger(FLAGS_log_dir, FLAGS_service_name, FLAGS_log_level);
  LOG_INFO("{} 启动, 端口 {}", FLAGS_service_name, FLAGS_listen_port);

  auto db = create_db();
  static EsClient es(FLAGS_es_host);
  static AsyncIndexer indexer(&es);
  LOG_INFO("ES 状态: {}", es.healthy() ? "可用（搜索走 ES）" : "不可用（搜索降级 LIKE）");

  ServiceRegistry registry(FLAGS_etcd_endpoints, FLAGS_service_name, FLAGS_instance_id,
                           FLAGS_register_host, FLAGS_listen_port, FLAGS_etcd_lease_ttl,
                           FLAGS_etcd_keepalive_interval);
  if (!registry.start()) {
    LOG_ERROR("注册失败，退出");
    return 1;
  }

  brpc::Server server;
  MsgStorageServiceImpl impl(db.get(), &es, &indexer);
  if (server.AddService(&impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
    LOG_ERROR("AddService 失败");
    return 1;
  }
  brpc::ServerOptions options;
  if (server.Start(FLAGS_listen_port, &options) != 0) {
    LOG_ERROR("brpc 启动失败: 端口 {}", FLAGS_listen_port);
    return 1;
  }
  LOG_INFO("{} 就绪: {}:{}", FLAGS_service_name, FLAGS_register_host, FLAGS_listen_port);
  server.RunUntilAskedToQuit();

  registry.stop();
  LOG_INFO("{} 退出", FLAGS_service_name);
  return 0;
}
