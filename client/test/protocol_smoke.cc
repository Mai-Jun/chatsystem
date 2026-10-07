// M9 协议冒烟测试（无 GUI）：验证客户端协议层对真实网关的全链路
//   发码 → 注册(开发模式固定码 666666) → 登录 → 用户信息 → 搜索 →
//   会话列表 → WebSocket 连接鉴权 → 收到推送连接确认
// 在服务器上运行：./build/client/client_smoke [http_host] [http_port] [ws_port]
// 退出码 0 = 全部通过
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>

#include <cstdio>
#include <string>

#include "protocol/gateway_client.hpp"
#include "friend.pb.h"
#include "user.pb.h"

using namespace im;  // NOLINT

namespace {

int g_failures = 0;

void check(bool cond, const char* name) {
  printf("[PASS] %s\n", name);
  if (!cond) {
    printf("      ^^^ FAILED\n");
    ++g_failures;
  }
}

// 同步等待一次请求完成（协议层是异步回调风格，测试里用事件循环收敛）
template <class RespT>
bool sync_call(GatewayClient& c, RequestType type, const google::protobuf::MessageLite& req,
               RespT* resp, QString* errmsg) {
  QEventLoop loop;
  bool ok = false;
  c.call_p<RespT>(type, req,
                  [&](bool o, const QString& e, const RespT& r) {
                    ok = o;
                    if (resp) *resp = r;
                    if (errmsg) *errmsg = e;
                    loop.quit();
                  });
  loop.exec();
  return ok;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);

  const QString host = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("127.0.0.1");
  const int http_port = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 9000;
  const int ws_port = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 9001;

  GatewayClient client(host, static_cast<quint16>(http_port), host,
                       static_cast<quint16>(ws_port));

  // 用时间戳造不重复手机号
  const std::string phone = "196" + std::to_string(QDateTime::currentMSecsSinceEpoch()).substr(6);
  const std::string password = "pass123";
  QString err;

  // ---- 注册（开发模式固定码）----
  SendSmsCodeReq sms;
  sms.set_phone(phone);
  SendSmsCodeResp sms_resp;
  check(sync_call(client, REQ_TYPE_SEND_SMS_CODE, sms, &sms_resp, &err), "发送验证码");
  if (!sms_resp.success()) {
    printf("  ! %s\n", qPrintable(QString::fromStdString(sms_resp.errmsg())));
    return 1;
  }

  UserRegisterReq reg;
  reg.set_phone(phone);
  reg.set_sms_code("666666");  // 开发模式固定码旁路
  reg.set_nickname("冒烟用户");
  reg.set_password(password);
  UserRegisterResp reg_resp;
  const bool reg_ok = sync_call(client, REQ_TYPE_REGISTER, reg, &reg_resp, &err);
  check(reg_ok && reg_resp.success(), "注册（固定码 666666）");
  if (!reg_ok) {
    printf("  ! %s\n", qPrintable(err));
    return 1;
  }
  const std::string uid = reg_resp.user_id();

  // ---- 登录 ----
  UserLoginReq login;
  login.set_phone(phone);
  login.set_login_type(LOGIN_BY_PASSWORD);
  login.set_password(password);
  UserLoginResp login_resp;
  const bool login_ok = sync_call(client, REQ_TYPE_LOGIN, login, &login_resp, &err);
  check(login_ok && login_resp.success() && !login_resp.token().empty(), "密码登录");
  if (!login_ok) {
    printf("  ! %s\n", qPrintable(err));
    return 1;
  }
  client.set_token(QString::fromStdString(login_resp.token()));

  // ---- 短信验证码登录（开发模式固定码；第二种登录方式）----
  // 注：不再单独发码——同一手机号 60s 内限发（Redis sms:limit），复用注册步骤已发送的码
  UserLoginReq sms_login;
  sms_login.set_phone(phone);
  sms_login.set_login_type(LOGIN_BY_SMS);
  sms_login.set_sms_code("666666");
  UserLoginResp sms_login_resp;
  check(sync_call(client, REQ_TYPE_LOGIN, sms_login, &sms_login_resp, &err) &&
            sms_login_resp.success() && !sms_login_resp.token().empty(),
        "短信验证码登录（固定码 666666）");

  // ---- 用户信息（token 鉴权链路）----
  GetUserInfoReq info_req;
  GetUserInfoResp info_resp;
  const bool info_ok = sync_call(client, REQ_TYPE_GET_USER_INFO, info_req, &info_resp, &err);
  check(info_ok && info_resp.success() && info_resp.user_info().user_id() == uid,
        "获取用户信息（token 鉴权）");

  // ---- 搜索 ----
  SearchUserReq search;
  search.set_user_id(uid);
  search.set_keyword(phone);
  SearchUserResp search_resp;
  const bool search_ok = sync_call(client, REQ_TYPE_SEARCH_USER, search, &search_resp, &err);
  check(search_ok && search_resp.success(), "搜索用户");

  // ---- 会话列表 ----
  GetChatSessionListReq sessions_req;
  GetChatSessionListResp sessions_resp;
  check(sync_call(client, REQ_TYPE_GET_SESSION_LIST, sessions_req, &sessions_resp, &err),
        "会话列表");

  // ---- WebSocket 推送连接（首帧鉴权）----
  bool ws_ok = false;
  QString ws_err;
  QEventLoop ws_loop;
  QTimer::singleShot(8000, &ws_loop, [&ws_loop]() { ws_loop.quit(); });  // 超时兜底
  QObject::connect(&client, &GatewayClient::pushConnected, &ws_loop,
                   [&](bool ok, const QString& e) {
                     ws_ok = ok;
                     ws_err = e;
                     ws_loop.quit();
                   });
  client.connect_push();
  ws_loop.exec();
  check(ws_ok, QString("WebSocket 鉴权连接 (%1)").arg(ws_err).toLocal8Bit().constData());

  printf("\n=== 客户端协议冒烟: %d 项失败 ===\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
