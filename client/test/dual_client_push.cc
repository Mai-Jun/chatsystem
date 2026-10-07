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
  void clear() { frames.clear(); }
};

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);

  const QString host = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("127.0.0.1");
  const int http_port = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 9000;
  const int ws_port = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 9001;

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

  printf("\n=== 双客户端推送验证: %d 项失败 ===\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
