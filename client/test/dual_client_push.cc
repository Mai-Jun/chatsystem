// M9 双客户端实时推送验证（M7 遗留项：双客户端互发消息 + WS 实时推送）
// 两个独立 GatewayClient 实例模拟 A/B 两端：
//   注册登录 A、B → A 加 B → B 同意（自动建单聊会话）
//   → 双端 WS 上线 → A 发消息 → 断言 B 实时收到推送、A 收到自己的广播回显
//   → B 断开 WS（离线）→ A 再发一条 → B 重连 → 拉历史断言离线消息可补齐
// 在服务器上运行：./client/build-server/dual_client_push [host] [http_port] [ws_port]
// 退出码 0 = 全部通过
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>

#include <cstdio>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "friend.pb.h"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "protocol/gateway_client.hpp"
#include "user.pb.h"

using namespace im;  // NOLINT

namespace {

int g_failures = 0;

void check(bool cond, const std::string& name) {
  printf("[%s] %s\n", cond ? "PASS" : "FAIL", name.c_str());
  if (!cond) ++g_failures;
}

template <class RespT>
bool sync_call(GatewayClient& c, RequestType type, const google::protobuf::MessageLite& req,
               RespT* resp, QString* errmsg) {
  QEventLoop loop;
  bool ok = false;
  c.call_p<RespT>(type, req, [&](bool o, const QString& e, const RespT& r) {
    ok = o;
    if (resp) *resp = r;
    if (errmsg) *errmsg = e;
    loop.quit();
  });
  loop.exec();
  return ok;
}

// 轮询等待条件成立（推送是异步的，用事件循环推进）
bool wait_for(const std::function<bool()>& pred, int timeout_ms) {
  if (pred()) return true;
  QEventLoop loop;
  QTimer poll;
  poll.setInterval(50);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
    if (pred()) loop.quit();
  });
  QTimer guard;
  guard.setSingleShot(true);
  QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  guard.start(timeout_ms);
  loop.exec();
  return pred();
}

struct Account {
  std::string phone;
  std::string uid;
  QString token;
  GatewayClient* client = nullptr;
};

// 发码 → 固定码注册 → 密码登录（开发模式旁路，与 protocol_smoke 一致）
bool signup(Account& acc, const std::string& nickname) {
  QString err;
  SendSmsCodeReq sms;
  sms.set_phone(acc.phone);
  SendSmsCodeResp sms_resp;
  if (!sync_call(*acc.client, REQ_TYPE_SEND_SMS_CODE, sms, &sms_resp, &err)) return false;

  UserRegisterReq reg;
  reg.set_phone(acc.phone);
  reg.set_sms_code("666666");
  reg.set_nickname(nickname);
  reg.set_password("pass123");
  UserRegisterResp reg_resp;
  if (!sync_call(*acc.client, REQ_TYPE_REGISTER, reg, &reg_resp, &err) || !reg_resp.success()) {
    printf("      注册失败: %s\n", qPrintable(err));
    return false;
  }
  acc.uid = reg_resp.user_id();

  UserLoginReq login;
  login.set_phone(acc.phone);
  login.set_login_type(LOGIN_BY_PASSWORD);
  login.set_password("pass123");
  UserLoginResp login_resp;
  if (!sync_call(*acc.client, REQ_TYPE_LOGIN, login, &login_resp, &err) || !login_resp.success()) {
    printf("      登录失败: %s\n", qPrintable(err));
    return false;
  }
  acc.token = QString::fromStdString(login_resp.token());
  acc.client->set_token(acc.token);
  return true;
}

// 收集某端收到的推送
struct PushLog {
  std::vector<std::pair<int, QByteArray>> frames;
  void attach(GatewayClient* c) {
    QObject::connect(c, &GatewayClient::pushReceived, [this](int type, const QByteArray& body) {
      frames.emplace_back(type, body);
    });
  }
  // 在已收到的帧里找一条 NEW_MESSAGE，返回其 MessageInfo（找不到返回 false）
  bool find_message(const std::string& content, MessageInfo* out) {
    for (const auto& f : frames) {
      if (f.first != PUSH_TYPE_NEW_MESSAGE) continue;
      MessageInfo msg;
      if (!msg.ParseFromArray(f.second.constData(), f.second.size())) continue;
      if (msg.content() == content) {
        if (out) *out = msg;
        return true;
      }
    }
    return false;
  }
  // 按 file_id 找一条推送（图片/文件/语音消息 content 为空，用 file_id 匹配）
  bool find_by_file_id(const std::string& file_id, MessageInfo* out) {
    for (const auto& f : frames) {
      if (f.first != PUSH_TYPE_NEW_MESSAGE) continue;
      MessageInfo msg;
      if (!msg.ParseFromArray(f.second.constData(), f.second.size())) continue;
      if (msg.file_id() == file_id) {
        if (out) *out = msg;
        return true;
      }
    }
    return false;
  }
  void clear() { frames.clear(); }
};

// 上传/下载的同步封装（协议层是回调风格）
bool upload_sync(GatewayClient& c, const QString& name, const QByteArray& data, QString* file_id,
                 QString* err) {
  QEventLoop loop;
  bool ok = false;
  QString fid;
  c.upload_file(name, data, [&](bool o, const QString& e, const QString& f) {
    ok = o;
    fid = f;
    if (err) *err = e;
    loop.quit();
  });
  loop.exec();
  if (file_id) *file_id = fid;
  return ok;
}

bool download_sync(GatewayClient& c, const QString& file_id, QByteArray* out, QString* err) {
  QEventLoop loop;
  bool ok = false;
  QByteArray data;
  c.download_file(file_id, [&](bool o, const QString& e, const QByteArray& d, const QString&) {
    ok = o;
    data = d;
    if (err) *err = e;
    loop.quit();
  });
  loop.exec();
  if (out) *out = data;
  return ok;
}

// 造一段 16kHz/单声道/16bit PCM WAV（与客户端 WavRecorder 产物同规格）
QByteArray make_test_wav(int ms) {
  const int rate = 16000;
  const int samples = rate * ms / 1000;
  QByteArray pcm;
  pcm.reserve(samples * 2);
  for (int i = 0; i < samples; ++i) {
    const qint16 v = static_cast<qint16>(3000 * std::sin(2 * 3.14159265 * 440 * i / rate));
    pcm.append(static_cast<char>(v & 0xff));
    pcm.append(static_cast<char>((v >> 8) & 0xff));
  }
  QByteArray wav("RIFF", 4);
  auto u32 = [&wav](quint32 v) {
    wav.append(static_cast<char>(v & 0xff));
    wav.append(static_cast<char>((v >> 8) & 0xff));
    wav.append(static_cast<char>((v >> 16) & 0xff));
    wav.append(static_cast<char>((v >> 24) & 0xff));
  };
  auto u16 = [&wav](quint16 v) {
    wav.append(static_cast<char>(v & 0xff));
    wav.append(static_cast<char>((v >> 8) & 0xff));
  };
  u32(36 + pcm.size());
  wav.append("WAVE", 4);
  wav.append("fmt ", 4);
  u32(16);
  u16(1);
  u16(1);
  u32(rate);
  u32(rate * 2);
  u16(2);
  u16(16);
  wav.append("data", 4);
  u32(pcm.size());
  wav.append(pcm);
  return wav;
}

// 仅登录（账号已存在时用；密码统一 pass123）
bool login_only(Account& acc) {
  QString err;
  UserLoginReq login;
  login.set_phone(acc.phone);
  login.set_login_type(LOGIN_BY_PASSWORD);
  login.set_password("pass123");
  UserLoginResp resp;
  if (!sync_call(*acc.client, REQ_TYPE_LOGIN, login, &resp, &err) || !resp.success()) {
    printf("      登录失败: %s\n", qPrintable(err));
    return false;
  }
  acc.token = QString::fromStdString(resp.token());
  acc.uid = resp.user_info().user_id();
  acc.client->set_token(acc.token);
  return true;
}

// 找出与 peer_uid 的单聊会话 id（遍历我的会话列表，比对成员）
std::string find_session_with(GatewayClient& c, const std::string& my_uid,
                              const std::string& peer_uid) {
  QString err;
  GetChatSessionListReq req;
  req.set_user_id(my_uid);
  GetChatSessionListResp resp;
  if (!sync_call(c, REQ_TYPE_GET_SESSION_LIST, req, &resp, &err) || !resp.success()) return "";
  for (const auto& s : resp.session_list()) {
    GetSessionMemberReq mreq;
    mreq.set_chat_session_id(s.chat_session_id());
    GetSessionMemberResp mresp;
    if (!sync_call(c, REQ_TYPE_GET_SESSION_MEMBER, mreq, &mresp, &err)) continue;
    for (const auto& m : mresp.members()) {
      if (m.user_id() == peer_uid) return s.chat_session_id();
    }
  }
  return "";
}

// 辅助模式：以 sender 身份给 peer 发一条文本，用于 GUI 侧验证推送与未读计数
//   dual_client_push <host> <http> <ws> --send <sender_phone> <peer_phone> <text>
int run_send_mode(const QString& host, quint16 http_port, quint16 ws_port,
                  const std::string& sender_phone, const std::string& peer_phone,
                  const std::string& text) {
  GatewayClient client(host, http_port, host, ws_port);
  Account sender{sender_phone, "", QString(), &client};
  if (!login_only(sender)) return 1;

  QString err;
  SearchUserReq search;
  search.set_user_id(sender.uid);
  search.set_keyword(peer_phone);
  SearchUserResp search_resp;
  if (!sync_call(client, REQ_TYPE_SEARCH_USER, search, &search_resp, &err) ||
      search_resp.result_size() == 0) {
    printf("找不到对端 %s\n", peer_phone.c_str());
    return 1;
  }
  const std::string peer_uid = search_resp.result(0).user_id();
  const std::string session_id = find_session_with(client, sender.uid, peer_uid);
  if (session_id.empty()) {
    printf("找不到与 %s 的会话\n", peer_phone.c_str());
    return 1;
  }

  MsgTransmitReq req;
  req.set_chat_session_id(session_id);
  req.mutable_content()->set_type(MESSAGE_TYPE_TEXT);
  req.mutable_content()->set_content(text);
  MsgTransmitResp resp;
  const bool ok = sync_call(client, REQ_TYPE_TRANSMIT_MESSAGE, req, &resp, &err) && resp.success();
  printf("send: session=%s ok=%d\n", session_id.c_str(), ok ? 1 : 0);
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);

  const QString host = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("127.0.0.1");
  const int http_port = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 9000;
  const int ws_port = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 9001;

  // 辅助模式：--send <sender_phone> <peer_phone> <text>
  if (argc > 4 && std::string(argv[4]) == "--send") {
    if (argc < 8) {
      printf("用法: dual_client_push <host> <http> <ws> --send <发送者手机号> <对端手机号> <文本>\n");
      return 2;
    }
    return run_send_mode(host, static_cast<quint16>(http_port),
                         static_cast<quint16>(ws_port), argv[5], argv[6], argv[7]);
  }

  GatewayClient client_a(host, static_cast<quint16>(http_port), host,
                         static_cast<quint16>(ws_port));
  GatewayClient client_b(host, static_cast<quint16>(http_port), host,
                         static_cast<quint16>(ws_port));

  const qint64 stamp = QDateTime::currentMSecsSinceEpoch() % 1000000000;
  Account a{"19" + std::to_string(stamp), "", QString(), &client_a};
  Account b{"18" + std::to_string(stamp), "", QString(), &client_b};

  printf("== 双客户端实时推送验证 (A=%s B=%s) ==\n", a.phone.c_str(), b.phone.c_str());

  // ---- ① 注册 + 登录 ----
  check(signup(a, "端A"), "A 注册并登录");
  check(signup(b, "端B"), "B 注册并登录");
  if (a.uid.empty() || b.uid.empty()) return 1;

  QString err;

  // ---- ② A 搜索 B 并发送好友申请 ----
  SearchUserReq search;
  search.set_user_id(a.uid);
  search.set_keyword(b.phone);
  SearchUserResp search_resp;
  check(sync_call(client_a, REQ_TYPE_SEARCH_USER, search, &search_resp, &err) &&
            search_resp.result_size() == 1 && search_resp.result(0).user_id() == b.uid,
        "A 按手机号搜到 B");

  SendFriendApplyReq apply;
  apply.set_user_id(a.uid);
  apply.set_peer_id(b.uid);
  apply.set_apply_note("来自端A");
  SendFriendApplyResp apply_resp;
  check(sync_call(client_a, REQ_TYPE_SEND_FRIEND_APPLY, apply, &apply_resp, &err) &&
            apply_resp.success(),
        "A 发送好友申请");

  // ---- ③ B 处理申请（同意 → 自动建单聊会话）----
  std::string session_id;
  {
    GetPendingEventsReq pending;
    GetPendingEventsResp pending_resp;
    bool ok = wait_for(
        [&]() {
          return sync_call(client_b, REQ_TYPE_GET_PENDING_EVENTS, pending, &pending_resp, &err) &&
                 pending_resp.friend_apply_events_size() > 0;
        },
        8000);
    check(ok, "B 收到好友申请事件");
    if (!ok) return 1;

    ProcessFriendApplyReq proc;
    proc.set_user_id(b.uid);
    proc.set_apply_id(pending_resp.friend_apply_events(0).apply_id());
    proc.set_status(APPLY_STATUS_AGREE);
    ProcessFriendApplyResp proc_resp;
    check(sync_call(client_b, REQ_TYPE_PROCESS_FRIEND_APPLY, proc, &proc_resp, &err) &&
              proc_resp.success(),
          "B 同意申请");
    session_id = proc_resp.session_info().chat_session_id();
    check(!session_id.empty(), "同意后返回单聊会话 id");
    if (session_id.empty()) return 1;
  }

  // ---- ④ 双端 WS 上线 ----
  PushLog log_a, log_b;
  log_a.attach(&client_a);
  log_b.attach(&client_b);
  client_a.connect_push();
  client_b.connect_push();
  check(wait_for([&]() { return client_a.push_connected(); }, 10000), "A WS 鉴权连接");
  check(wait_for([&]() { return client_b.push_connected(); }, 10000), "B WS 鉴权连接");

  // ---- ⑤ A 发消息 → B 实时收到推送 ----
  const std::string text1 = "hello-from-A-" + std::to_string(stamp);
  {
    MsgTransmitReq req;
    req.set_chat_session_id(session_id);
    req.mutable_content()->set_type(MESSAGE_TYPE_TEXT);
    req.mutable_content()->set_content(text1);
    MsgTransmitResp resp;
    check(sync_call(client_a, REQ_TYPE_TRANSMIT_MESSAGE, req, &resp, &err) && resp.success(),
          "A 发送文本消息");
  }
  check(wait_for([&]() {
          MessageInfo m;
          return log_b.find_message(text1, &m);
        }, 10000),
        "B 实时收到 NEW_MESSAGE 推送");

  {
    MessageInfo got;
    if (log_b.find_message(text1, &got)) {
      check(got.sender_id() == a.uid, "推送消息 sender_id == A");
      check(got.chat_session_id() == session_id, "推送消息 session 一致");
      check(got.type() == MESSAGE_TYPE_TEXT, "推送消息类型为文本");
      check(!got.message_id().empty() && got.create_time() > 0, "推送消息带 message_id/时间戳");
    } else {
      check(false, "推送消息字段校验（未收到）");
    }
  }

  // 发送者自己也应收到 MQ 广播回显（客户端据此上屏，无需本地插入）
  check(wait_for([&]() {
          MessageInfo m;
          return log_a.find_message(text1, &m);
        }, 10000),
        "A 收到自己消息的广播回显");

  // ---- ⑥ 离线补历史：B 断开 → A 再发 → B 重连拉历史 ----
  client_b.disconnect_push();
  check(wait_for([&]() { return !client_b.push_connected(); }, 5000), "B 断开 WS（模拟离线）");
  log_b.clear();

  const std::string text2 = "offline-msg-" + std::to_string(stamp);
  {
    MsgTransmitReq req;
    req.set_chat_session_id(session_id);
    req.mutable_content()->set_type(MESSAGE_TYPE_TEXT);
    req.mutable_content()->set_content(text2);
    MsgTransmitResp resp;
    check(sync_call(client_a, REQ_TYPE_TRANSMIT_MESSAGE, req, &resp, &err) && resp.success(),
          "B 离线时 A 发送消息");
  }
  // 离线期间不应推给 B（连接已断，无法收帧）
  check(!log_b.find_message(text2, nullptr), "离线期间 B 未收到该消息");

  client_b.connect_push();
  check(wait_for([&]() { return client_b.push_connected(); }, 10000), "B 重连 WS");

  {
    GetHistoryReq req;
    req.set_chat_session_id(session_id);
    req.set_cursor_timestamp(0);
    req.set_limit(50);
    GetHistoryResp resp;
    bool ok = sync_call(client_b, REQ_TYPE_GET_HISTORY, req, &resp, &err) && resp.success();
    bool found = false;
    for (const auto& m : resp.messages()) {
      if (m.content() == text2) found = true;
    }
    check(ok && found, "B 重连后拉历史补齐离线消息");
    check(resp.messages_size() >= 2, "历史含两条消息");
  }

  // ---- ⑦ 文件/图片消息链路：上传 → 发送 → 推送 → 对端下载字节一致 ----
  log_b.clear();
  {
    QByteArray payload;
    payload.reserve(4096);
    for (int i = 0; i < 4096; ++i) payload.append(static_cast<char>(i % 251));  // 含 0 字节

    QString file_id;
    check(upload_sync(client_a, QStringLiteral("m9_payload.bin"), payload, &file_id, &err) &&
              !file_id.isEmpty(),
          "A 上传文件拿到 file_id");

    MsgTransmitReq req;
    req.set_chat_session_id(session_id);
    req.mutable_content()->set_type(MESSAGE_TYPE_FILE);
    req.mutable_content()->set_file_id(file_id.toStdString());
    req.mutable_content()->set_file_name("m9_payload.bin");
    req.mutable_content()->set_file_size(payload.size());
    MsgTransmitResp resp;
    check(sync_call(client_a, REQ_TYPE_TRANSMIT_MESSAGE, req, &resp, &err) && resp.success(),
          "A 发送文件消息");

    MessageInfo got;
    check(wait_for([&]() { return log_b.find_by_file_id(file_id.toStdString(), &got); }, 10000),
          "B 实时收到文件消息推送");
    check(got.type() == MESSAGE_TYPE_FILE && got.file_name() == "m9_payload.bin" &&
              got.file_size() == payload.size(),
          "文件消息元信息一致（type/文件名/大小）");

    QByteArray back;
    check(download_sync(client_b, file_id, &back, &err), "B 下载文件");
    check(back == payload, "B 下载内容与上传字节完全一致");
  }

  // ---- ⑧ 语音消息链路：WAV 上传 → 发送 → 服务端 ASR → 推送带 asr_text ----
  log_b.clear();
  {
    const QByteArray wav = make_test_wav(1200);  // 1.2s 440Hz
    QString file_id;
    check(upload_sync(client_a, QStringLiteral("voice_m9.wav"), wav, &file_id, &err) &&
              !file_id.isEmpty(),
          "A 上传语音 WAV");

    MsgTransmitReq req;
    req.set_chat_session_id(session_id);
    req.mutable_content()->set_type(MESSAGE_TYPE_VOICE);
    req.mutable_content()->set_file_id(file_id.toStdString());
    req.mutable_content()->set_file_name("voice_m9.wav");
    req.mutable_content()->set_file_size(wav.size());
    MsgTransmitResp resp;
    check(sync_call(client_a, REQ_TYPE_TRANSMIT_MESSAGE, req, &resp, &err) && resp.success(),
          "A 发送语音消息");

    MessageInfo got;
    check(wait_for([&]() { return log_b.find_by_file_id(file_id.toStdString(), &got); }, 10000),
          "B 实时收到语音消息推送");
    check(got.type() == MESSAGE_TYPE_VOICE, "语音消息类型正确");
    check(!got.asr_text().empty(), "语音消息带 ASR 转写文本（开发模式占位）");
    if (!got.asr_text().empty()) printf("      asr_text = %s\n", got.asr_text().c_str());

    QByteArray back;
    check(download_sync(client_b, file_id, &back, &err) && back == wav,
          "B 下载语音内容与上传一致");
  }

  // ---- ⑨ 会话历史应包含 4 条消息（2 文本 + 1 文件 + 1 语音）----
  {
    GetHistoryReq req;
    req.set_chat_session_id(session_id);
    req.set_cursor_timestamp(0);
    req.set_limit(50);
    GetHistoryResp resp;
    bool ok = sync_call(client_b, REQ_TYPE_GET_HISTORY, req, &resp, &err) && resp.success();
    check(ok && resp.messages_size() == 4,
          QString("历史消息数 = 4（实际 %1）").arg(resp.messages_size()).toStdString());
  }

  printf("\n=== 双客户端推送验证: %d 项失败 ===\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
