#include <brpc/server.h>
#include <gflags/gflags.h>

#include <memory>
#include <string>
#include <vector>

#include "channel_manager.hpp"
#include "entities-odb.hxx"
#include "etcd_client.hpp"
#include "file.pb.h"
#include "flags.hpp"
#include "logger.hpp"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "mq.hpp"
#include "odb/database.hpp"
#include "odb/entities.hpp"
#include "speech.pb.h"
#include "util.hpp"

// ============================================================
// 消息转发子服务（核心链路）
//   客户端消息 → 网关 → 本服务：
//     ① 校验会话存在且发送者是成员（防越权）
//     ② 语音消息顺带调语音子服务做 ASR（失败不阻断）
//     ③ 调消息存储子服务持久化（MySQL 权威）
//     ④ 投递 RabbitMQ 广播（MessageDelivery），各网关节点消费后
//        经 WebSocket 推给在线成员
//   注：本服务不做"转发"本身，只负责"确定投递目标 + 投递到总线"。
// ============================================================
using namespace im;  // NOLINT

namespace {

class MsgTransmitServiceImpl : public MsgTransmitService {
 public:
  MsgTransmitServiceImpl(odb::mysql::database* db, ChannelManager* channels, MqPublisher* mq)
      : db_(db), channels_(channels), mq_(mq) {}

  void GetTransmitTarget(google::protobuf::RpcController* cntl, const MsgTransmitReq* req,
                         MsgTransmitResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;

    const std::string& session_id = req->chat_session_id();
    const std::string& sender_id = req->user_id();
    if (session_id.empty() || sender_id.empty()) {
      resp->set_success(false);
      resp->set_errmsg("会话或发送者为空");
      return;
    }

    // ① 会话存在 + 发送者是成员 + 收集全部成员（投递目标）
    std::vector<std::string> members;
    try {
      odb::transaction t(db_->begin());
      std::unique_ptr<ChatSession> sess(
          db_->query_one<ChatSession>(odb::query<ChatSession>::id == session_id));
      if (!sess) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("会话不存在");
        return;
      }
      bool sender_in = false;
      auto rs = db_->query<ChatSessionMember>(
          odb::query<ChatSessionMember>::session_id == session_id);
      for (auto it = rs.begin(); it != rs.end(); ++it) {
        members.push_back(it->user_id());
        if (it->user_id() == sender_id) sender_in = true;
      }
      t.commit();
      if (!sender_in) {
        resp->set_success(false);
        resp->set_errmsg("你不在该会话中");
        return;
      }
    } catch (const std::exception& e) {
      LOG_ERROR("会话校验失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("会话校验失败");
      return;
    }

    // 组装消息
    MessageInfo msg;
    msg.set_message_id(uuid());
    msg.set_chat_session_id(session_id);
    msg.set_sender_id(sender_id);
    msg.set_type(req->content().type());
    msg.set_content(req->content().content());
    msg.set_file_id(req->content().file_id());
    msg.set_file_name(req->content().file_name());
    msg.set_file_size(req->content().file_size());
    msg.set_create_time(now_seconds());

    // ② 语音消息 → ASR（失败不阻断，仅记录）
    if (req->content().type() == MESSAGE_TYPE_VOICE && !req->content().file_id().empty()) {
      std::string asr = try_asr(req->content().file_id());
      if (!asr.empty()) msg.set_asr_text(asr);
    }

    // ③ 持久化（MySQL 权威；存储服务内部再做 ES 异步双写）
    if (!persist(msg)) {
      resp->set_success(false);
      resp->set_errmsg("消息持久化失败");
      return;
    }

    // ④ 投递到 MQ 广播（各网关节点消费后推送在线成员）
    MessageDelivery delivery;
    delivery.set_chat_session_id(session_id);
    *delivery.mutable_message() = msg;
    for (const auto& m : members) delivery.add_member_user_ids(m);
    if (!mq_->publish("message.new", delivery.SerializeAsString())) {
      // 已持久化成功，投递失败不回滚（客户端重连后拉历史可补齐）
      LOG_ERROR("MQ 投递失败，消息已入库: {}", msg.message_id());
    }

    LOG_INFO("消息转发: session={} sender={} type={} members={} msg_id={}", session_id, sender_id,
             static_cast<int>(msg.type()), members.size(), msg.message_id());
    resp->set_success(true);
    resp->set_errmsg("ok");
    resp->set_new_message_id(msg.message_id());
    resp->set_create_time(msg.create_time());
  }

 private:
  // 调语音子服务转写（服务未上线则静默跳过）
  std::string try_asr(const std::string& file_id) {
    auto* file_ch = channels_->channel("file_server");
    if (file_ch == nullptr) return "";
    FileService_Stub file_stub(file_ch);
    brpc::Controller file_cntl;
    GetSingleReq freq;
    freq.set_file_id(file_id);
    GetSingleResp fresp;
    file_stub.GetSingle(&file_cntl, &freq, &fresp, nullptr);
    if (file_cntl.Failed() || !fresp.success()) return "";

    auto* speech_ch = channels_->channel("speech_server");
    if (speech_ch == nullptr) return "";
    SpeechService_Stub speech_stub(speech_ch);
    brpc::Controller scntl;
    SpeechRecognitionReq sreq;
    sreq.set_speech_content(fresp.data().file_content());
    sreq.set_audio_format("wav");
    SpeechRecognitionResp sresp;
    speech_stub.SpeechRecognition(&scntl, &sreq, &sresp, nullptr);
    if (scntl.Failed() || !sresp.success()) {
      LOG_WARN("ASR 失败: {}", scntl.Failed() ? scntl.ErrorText() : sresp.errmsg());
      return "";
    }
    return sresp.recognized_text();
  }

  bool persist(const MessageInfo& msg) {
    auto* ch = channels_->channel("message_storage_server");
    if (ch == nullptr) {
      LOG_ERROR("消息存储服务无可用节点");
      return false;
    }
    MsgStorageService_Stub stub(ch);
    brpc::Controller cntl;
    SaveMessageReq req;
    *req.mutable_message() = msg;
    SaveMessageResp resp;
    stub.SaveMessage(&cntl, &req, &resp, nullptr);
    if (cntl.Failed() || !resp.success()) {
      LOG_ERROR("持久化调用失败: {}", cntl.Failed() ? cntl.ErrorText() : resp.errmsg());
      return false;
    }
    return true;
  }

  odb::mysql::database* db_;
  ChannelManager* channels_;
  MqPublisher* mq_;
};

}  // namespace

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  init_logger(FLAGS_log_dir, FLAGS_service_name, FLAGS_log_level);
  LOG_INFO("{} 启动, 端口 {}", FLAGS_service_name, FLAGS_listen_port);

  auto db = create_db();
  ChannelManager channels(FLAGS_etcd_endpoints);
  channels.discover("message_storage_server");
  channels.discover("file_server");
  channels.discover("speech_server");

  MqPublisher mq(FLAGS_rabbitmq_exchange);
  if (!mq.connect()) {
    LOG_WARN("MQ 连接失败（启动后发布时会自动重连）");
  }

  ServiceRegistry registry(FLAGS_etcd_endpoints, FLAGS_service_name, FLAGS_instance_id,
                           FLAGS_register_host, FLAGS_listen_port, FLAGS_etcd_lease_ttl,
                           FLAGS_etcd_keepalive_interval);
  if (!registry.start()) {
    LOG_ERROR("注册失败，退出");
    return 1;
  }

  brpc::Server server;
  MsgTransmitServiceImpl impl(db.get(), &channels, &mq);
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
