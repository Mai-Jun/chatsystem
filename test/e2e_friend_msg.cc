// M5/M6/M7/M8 端到端验收（经网关）：
//   双用户注册 → 搜索 → 好友申请 → 待处理事件 → 同意(建单聊会话) →
//   好友列表/会话列表 → 发消息(转发+持久化) → 历史消息 → 关键字搜索 →
//   建群/群事件/群消息 → 语音(上传wav→转发带ASR入库→网关直达识别)
// 仅在服务器上运行（读取验证码需要访问 redis 容器）
// M8 语音步骤要求 speech_server 在线（未配置百度密钥时走开发模式占位转写）
#include <httplib.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "file.pb.h"
#include "friend.pb.h"
#include "gateway.pb.h"
#include "logger.hpp"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "speech.pb.h"
#include "user.pb.h"
#include "util.hpp"

using namespace im;  // NOLINT

namespace {

ServerResponse send(const ClientRequest& creq) {
  httplib::Client cli("127.0.0.1", 9000);
  auto res = cli.Post("/gateway", creq.SerializeAsString(), "application/x-protobuf");
  ServerResponse sresp;
  if (!res || !sresp.ParseFromString(res->body)) {
    sresp.set_success(false);
    sresp.set_errmsg("网关无响应");
  }
  return sresp;
}

ClientRequest make_req(RequestType t, const std::string& token,
                       const google::protobuf::MessageLite& body) {
  ClientRequest r;
  r.set_request_id(uuid());
  r.set_type(t);
  r.set_token(token);
  r.set_body(body.SerializeAsString());
  return r;
}

std::string read_code_from_redis(const std::string& phone) {
  std::string cmd = "docker exec im-redis redis-cli get 'sms:code:" + phone + "'";
  FILE* f = popen(cmd.c_str(), "r");
  if (f == nullptr) return "";
  char buf[64] = {0};
  if (fgets(buf, sizeof(buf), f) == nullptr) {
    pclose(f);
    return "";
  }
  pclose(f);
  std::string code(buf);
  while (!code.empty() && (code.back() == '\n' || code.back() == '\r')) code.pop_back();
  return code;
}

int g_failures = 0;
#define CHECK(cond, name)                                        \
  do {                                                           \
    if (cond) {                                                  \
      printf("[PASS] " name "\n");                               \
    } else {                                                     \
      printf("[FAIL] " name "\n");                               \
      ++g_failures;                                              \
    }                                                            \
  } while (0)

std::string register_user(const std::string& phone, const std::string& nickname,
                          std::string* out_token) {
  SendSmsCodeReq sreq;
  sreq.set_phone(phone);
  auto r = send(make_req(REQ_TYPE_SEND_SMS_CODE, "", sreq));
  if (!r.success()) {
    printf("  ! 发码失败: %s\n", r.errmsg().c_str());
    return "";
  }
  std::string code = read_code_from_redis(phone);
  if (code.empty()) {
    printf("  ! 读取验证码失败\n");
    return "";
  }
  UserRegisterReq reg;
  reg.set_phone(phone);
  reg.set_sms_code(code);
  reg.set_nickname(nickname);
  reg.set_password("pass123");
  auto rr = send(make_req(REQ_TYPE_REGISTER, "", reg));
  UserRegisterResp reg_resp;
  if (!rr.success() || !reg_resp.ParseFromString(rr.body())) {
    printf("  ! 注册失败: %s\n", rr.errmsg().c_str());
    return "";
  }
  UserLoginReq login;
  login.set_phone(phone);
  login.set_login_type(LOGIN_BY_PASSWORD);
  login.set_password("pass123");
  auto rl = send(make_req(REQ_TYPE_LOGIN, "", login));
  UserLoginResp login_resp;
  if (!rl.success() || !login_resp.ParseFromString(rl.body())) {
    printf("  ! 登录失败: %s\n", rl.errmsg().c_str());
    return "";
  }
  *out_token = login_resp.token();
  return reg_resp.user_id();
}

// 极简 WAV：44 字节标准头 + 1 秒 16kHz/16bit 单声道静音
// （开发模式 ASR 不校验语音内容，真实密钥下需替换为真人语音）
std::string make_wav() {
  const uint32_t sample_rate = 16000;
  const uint32_t data_len = sample_rate * 2 * 1;  // 16bit = 2 字节/样本
  std::string w;
  w.reserve(44 + data_len);
  auto u32 = [&](uint32_t v) { w.append(reinterpret_cast<const char*>(&v), 4); };
  auto u16 = [&](uint16_t v) { w.append(reinterpret_cast<const char*>(&v), 2); };
  w += "RIFF";
  u32(36 + data_len);
  w += "WAVE";
  w += "fmt ";
  u32(16);            // fmt 块长度
  u16(1);             // PCM
  u16(1);             // 单声道
  u32(sample_rate);   // 采样率
  u32(sample_rate * 2);  // 字节率
  u16(2);             // 块对齐
  u16(16);            // 位深
  w += "data";
  u32(data_len);
  w.append(data_len, '\0');
  return w;
}

}  // namespace

int main() {
  init_logger("/tmp", "e2e_fm", "info");

  std::string phone_a = "198" + std::to_string(now_milliseconds()).substr(6);
  std::string phone_b = "197" + std::to_string(now_milliseconds()).substr(6);
  std::string token_a, token_b;

  // ---- 双用户注册登录 ----
  std::string uid_a = register_user(phone_a, "用户A", &token_a);
  std::string uid_b = register_user(phone_b, "用户B", &token_b);
  CHECK(!uid_a.empty() && !uid_b.empty(), "双用户注册登录");
  if (g_failures) return 1;

  // ---- M5: 搜索用户 ----
  SearchUserReq search_req;
  search_req.set_user_id(uid_a);
  search_req.set_keyword(phone_b);
  auto r_search = send(make_req(REQ_TYPE_SEARCH_USER, token_a, search_req));
  SearchUserResp search_resp;
  bool found_b = r_search.success() && search_resp.ParseFromString(r_search.body()) &&
                 search_resp.result_size() >= 1 &&
                 search_resp.result(0).user_id() == uid_b;
  CHECK(found_b, "搜索用户（按手机号）");

  // ---- M5: 发送好友申请 ----
  SendFriendApplyReq apply_req;
  apply_req.set_user_id(uid_a);
  apply_req.set_peer_id(uid_b);
  apply_req.set_apply_note("我是A，加个好友");
  auto r_apply = send(make_req(REQ_TYPE_SEND_FRIEND_APPLY, token_a, apply_req));
  SendFriendApplyResp apply_resp;
  CHECK(r_apply.success() && apply_resp.ParseFromString(r_apply.body()),
        "发送好友申请");

  // ---- M5: B 的待处理事件（应看到 A 的申请）----
  GetPendingEventsReq events_req;
  auto r_events = send(make_req(REQ_TYPE_GET_PENDING_EVENTS, token_b, events_req));
  GetPendingEventsResp events_resp;
  bool seen = r_events.success() && events_resp.ParseFromString(r_events.body()) &&
              events_resp.friend_apply_events_size() >= 1 &&
              events_resp.friend_apply_events(0).user_id() == uid_a;
  CHECK(seen, "B 待处理事件含 A 的申请");

  // ---- M5: B 同意申请（应创建单聊会话）----
  ProcessFriendApplyReq process_req;
  process_req.set_user_id(uid_b);
  process_req.set_apply_id(events_resp.friend_apply_events(0).apply_id());
  process_req.set_status(APPLY_STATUS_AGREE);
  auto r_process = send(make_req(REQ_TYPE_PROCESS_FRIEND_APPLY, token_b, process_req));
  ProcessFriendApplyResp process_resp;
  std::string session_id;
  if (r_process.success() && process_resp.ParseFromString(r_process.body()) &&
      process_resp.session_info().chat_session_id().empty() == false) {
    session_id = process_resp.session_info().chat_session_id();
  }
  CHECK(!session_id.empty(), "同意申请并创建单聊会话");

  // ---- M5: A 的好友列表（应含 B）----
  GetFriendListReq fl_req;
  auto r_fl = send(make_req(REQ_TYPE_GET_FRIEND_LIST, token_a, fl_req));
  GetFriendListResp fl_resp;
  bool friend_ok = r_fl.success() && fl_resp.ParseFromString(r_fl.body());
  bool b_in_list = false;
  for (const auto& u : fl_resp.friend_list()) {
    if (u.user_id() == uid_b) b_in_list = true;
  }
  CHECK(friend_ok && b_in_list, "A 的好友列表含 B");

  // ---- M5: A 的会话列表（应含单聊会话）----
  GetChatSessionListReq sl_req;
  auto r_sl = send(make_req(REQ_TYPE_GET_SESSION_LIST, token_a, sl_req));
  GetChatSessionListResp sl_resp;
  bool sess_in_list = false;
  if (r_sl.success() && sl_resp.ParseFromString(r_sl.body())) {
    for (const auto& s : sl_resp.session_list()) {
      if (s.chat_session_id() == session_id) sess_in_list = true;
    }
  }
  CHECK(sess_in_list, "A 的会话列表含单聊会话");

  // ---- M7: A 发消息（转发 → 持久化 → MQ 广播）----
  MsgTransmitReq tx_req;
  tx_req.set_chat_session_id(session_id);
  tx_req.mutable_content()->set_type(MESSAGE_TYPE_TEXT);
  tx_req.mutable_content()->set_content("hello e2e 你好");
  auto r_tx = send(make_req(REQ_TYPE_TRANSMIT_MESSAGE, token_a, tx_req));
  MsgTransmitResp tx_resp;
  std::string msg_id;
  if (r_tx.success() && tx_resp.ParseFromString(r_tx.body())) msg_id = tx_resp.new_message_id();
  CHECK(!msg_id.empty(), "发送消息（转发+持久化）");

  // ---- M6: 历史消息（应含刚发的消息）----
  GetHistoryReq hist_req;
  hist_req.set_chat_session_id(session_id);
  hist_req.set_limit(10);
  auto r_hist = send(make_req(REQ_TYPE_GET_HISTORY, token_a, hist_req));
  GetHistoryResp hist_resp;
  bool hist_ok = r_hist.success() && hist_resp.ParseFromString(r_hist.body()) &&
                 hist_resp.messages_size() >= 1 &&
                 hist_resp.messages(0).message_id() == msg_id &&
                 hist_resp.messages(0).content() == "hello e2e 你好";
  CHECK(hist_ok, "历史消息含刚发的消息");

  // ---- M6: 关键字搜索（ES 不可用 → LIKE 降级）----
  SearchHistoryReq sh_req;
  sh_req.set_user_id(uid_a);
  sh_req.set_chat_session_id(session_id);
  sh_req.set_keyword("hello e2e");
  auto r_sh = send(make_req(REQ_TYPE_SEARCH_HISTORY, token_a, sh_req));
  SearchHistoryResp sh_resp;
  bool search_ok = r_sh.success() && sh_resp.ParseFromString(r_sh.body()) &&
                   sh_resp.messages_size() >= 1;
  CHECK(search_ok, "关键字搜索（降级 LIKE）");

  // ---- M5: 群聊（A 建群拉 B）----
  CreateGroupSessionReq group_req;
  group_req.set_user_id(uid_a);
  group_req.set_group_name("测试群");
  group_req.add_member_ids(uid_b);
  auto r_group = send(make_req(REQ_TYPE_CREATE_GROUP_SESSION, token_a, group_req));
  CreateGroupSessionResp group_resp;
  std::string group_id;
  if (r_group.success() && group_resp.ParseFromString(r_group.body())) {
    group_id = group_resp.session_info().chat_session_id();
  }
  CHECK(!group_id.empty(), "创建群聊会话");

  // ---- M5: B 的待处理事件（应含群事件，且被消费）----
  auto r_events2 = send(make_req(REQ_TYPE_GET_PENDING_EVENTS, token_b, events_req));
  GetPendingEventsResp events2_resp;
  bool group_event_seen = false;
  if (r_events2.success() && events2_resp.ParseFromString(r_events2.body())) {
    for (const auto& e : events2_resp.group_events()) {
      if (e.group_session_id() == group_id) group_event_seen = true;
    }
  }
  CHECK(group_event_seen, "B 收到群聊事件");

  // ---- M7: 群消息（A 在群里发消息）----
  MsgTransmitReq gtx_req;
  gtx_req.set_chat_session_id(group_id);
  gtx_req.mutable_content()->set_type(MESSAGE_TYPE_TEXT);
  gtx_req.mutable_content()->set_content("群消息测试");
  auto r_gtx = send(make_req(REQ_TYPE_TRANSMIT_MESSAGE, token_a, gtx_req));
  MsgTransmitResp gtx_resp;
  bool gtx_ok = r_gtx.success() && gtx_resp.ParseFromString(r_gtx.body()) &&
                !gtx_resp.new_message_id().empty();
  CHECK(gtx_ok, "群消息发送");

  // ---- M8: 上传 wav 文件 ----
  std::string wav_bytes = make_wav();
  PutSingleReq wav_put;
  wav_put.mutable_data()->set_file_name("e2e_voice.wav");
  wav_put.mutable_data()->set_file_content(wav_bytes);
  auto r_put = send(make_req(REQ_TYPE_PUT_SINGLE_FILE, token_a, wav_put));
  PutSingleResp put_resp;
  std::string wav_id;
  if (r_put.success() && put_resp.ParseFromString(r_put.body())) wav_id = put_resp.file_id();
  CHECK(!wav_id.empty(), "上传语音 wav 文件");

  // ---- M8: 发语音消息（转发子服务顺带调 speech_server 做 ASR）----
  MsgTransmitReq vtx_req;
  vtx_req.set_chat_session_id(session_id);
  vtx_req.mutable_content()->set_type(MESSAGE_TYPE_VOICE);
  vtx_req.mutable_content()->set_file_id(wav_id);
  auto r_vtx = send(make_req(REQ_TYPE_TRANSMIT_MESSAGE, token_a, vtx_req));
  MsgTransmitResp vtx_resp;
  std::string voice_msg_id;
  if (r_vtx.success() && vtx_resp.ParseFromString(r_vtx.body())) {
    voice_msg_id = vtx_resp.new_message_id();
  }
  CHECK(!voice_msg_id.empty(), "发送语音消息");

  // ---- M8: 历史消息中该语音消息带 asr_text ----
  GetHistoryReq vhist_req;
  vhist_req.set_chat_session_id(session_id);
  vhist_req.set_limit(10);
  auto r_vhist = send(make_req(REQ_TYPE_GET_HISTORY, token_a, vhist_req));
  GetHistoryResp vhist_resp;
  bool asr_ok = false;
  if (r_vhist.success() && vhist_resp.ParseFromString(r_vhist.body())) {
    for (const auto& m : vhist_resp.messages()) {
      if (m.message_id() == voice_msg_id && m.type() == MESSAGE_TYPE_VOICE &&
          !m.asr_text().empty()) {
        asr_ok = true;
      }
    }
  }
  CHECK(asr_ok, "语音消息转写入库（asr_text 有值）");

  // ---- M8: 网关直达语音识别（REQ_TYPE_SPEECH_RECOGNITION）----
  SpeechRecognitionReq sr_req;
  sr_req.set_speech_content(wav_bytes);
  sr_req.set_audio_format("wav");
  auto r_sr = send(make_req(REQ_TYPE_SPEECH_RECOGNITION, token_a, sr_req));
  SpeechRecognitionResp sr_resp;
  bool sr_ok = r_sr.success() && sr_resp.ParseFromString(r_sr.body()) && sr_resp.success() &&
               !sr_resp.recognized_text().empty();
  CHECK(sr_ok, "语音识别接口（网关直达 speech_server）");

  printf("\n=== E2E 汇总: %d 项失败 ===\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
