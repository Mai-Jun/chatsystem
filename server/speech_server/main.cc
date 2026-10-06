#include <brpc/server.h>
#include <gflags/gflags.h>

#include <string>

#include "baidu_asr.hpp"
#include "etcd_client.hpp"
#include "flags.hpp"
#include "logger.hpp"
#include "speech.pb.h"
#include "util.hpp"

// ============================================================
// 语音识别子服务（M8）
//   调用方：消息转发子服务处理语音消息时顺带调用（try_asr，失败不阻断），
//           或客户端经网关直达（REQ_TYPE_SPEECH_RECOGNITION）。
//   上游：百度云短语音识别 REST API，密钥经 conf/speech_server.local.flags
//         注入（不入 git）。
//   开发模式：未配置密钥时返回占位转写文本，保证语音消息全链路
//           （转发→ASR→入库 asr_text）可验收——与短信验证码开发期
//           固定码同一思路；填入真实密钥重启即切正式识别。
//   本服务无状态：不连 MySQL / Redis / MQ。
// ============================================================
using namespace im;  // NOLINT

namespace {

// 开发模式占位转写（明显可辨，避免与真实转写混淆）
const char* kDevPlaceholder = "（开发模式）语音消息";

class SpeechServiceImpl : public SpeechService {
 public:
  void SpeechRecognition(google::protobuf::RpcController* cntl, const SpeechRecognitionReq* req,
                         SpeechRecognitionResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    if (req->speech_content().empty()) {
      resp->set_success(false);
      resp->set_errmsg("音频内容为空");
      return;
    }
    const std::string format = req->audio_format().empty() ? "wav" : req->audio_format();

    if (!baidu_keys_configured()) {
      resp->set_success(true);
      resp->set_errmsg("开发模式（未配置百度密钥）");
      resp->set_recognized_text(kDevPlaceholder);
      LOG_INFO("语音识别(开发模式): format={} bytes={}", format, req->speech_content().size());
      return;
    }

    std::string text, errmsg;
    if (!asr_.recognize(req->speech_content(), format, &text, &errmsg)) {
      LOG_WARN("语音识别失败: format={} bytes={} err={}", format, req->speech_content().size(),
               errmsg);
      resp->set_success(false);
      resp->set_errmsg(errmsg);
      return;
    }
    LOG_INFO("语音识别成功: format={} bytes={} text={}", format, req->speech_content().size(),
             text);
    resp->set_success(true);
    resp->set_errmsg("ok");
    resp->set_recognized_text(text);
  }

 private:
  BaiduAsrClient asr_;
};

}  // namespace

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  init_logger(FLAGS_log_dir, FLAGS_service_name, FLAGS_log_level);
  LOG_INFO("{} 启动, 端口 {}", FLAGS_service_name, FLAGS_listen_port);

  if (baidu_keys_configured()) {
    LOG_INFO("百度 ASR 密钥已配置, 正式识别模式, dev_pid={}", FLAGS_baidu_dev_pid);
  } else {
    LOG_WARN("未配置百度密钥(baidu_api_key/baidu_secret_key), 语音识别处于开发模式: 返回占位文本");
  }

  ServiceRegistry registry(FLAGS_etcd_endpoints, FLAGS_service_name, FLAGS_instance_id,
                           FLAGS_register_host, FLAGS_listen_port, FLAGS_etcd_lease_ttl,
                           FLAGS_etcd_keepalive_interval);
  if (!registry.start()) {
    LOG_ERROR("注册失败，退出");
    return 1;
  }

  brpc::Server server;
  SpeechServiceImpl impl;
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
