#include <QApplication>
#include <QEventLoop>
#include <QSettings>

#include "gateway.pb.h"
#include "protocol/gateway_client.hpp"
#include "ui/login_window.hpp"
#include "ui/main_window.hpp"
#include "ui/theme.h"
#include "user.pb.h"

// IM 桌面客户端入口（M9）
//   服务器地址存 QSettings（默认 47.112.192.119:9000，双击启动无需命令行参数）；
//   有历史 token 则先尝试自动登录（GET_USER_INFO 校验）；
//   退出登录 → 回到登录/注册页（同进程切换窗口，不退出程序）。
using namespace im;  // NOLINT

namespace {

void open_main_window(GatewayClient* client, const im::UserInfo& user);

// 登录页：登录成功 → 存 token → 开主窗口
void show_login_window(GatewayClient* client) {
  auto* login = new LoginWindow(client);
  // 登录成功 → 打开主窗口（登录窗 accept 后自动销毁）
  QObject::connect(login, &LoginWindow::loginSucceeded, login,
                   [client](const QString& token, const im::UserInfo& user) {
                     QSettings().setValue("account/token", token);
                     open_main_window(client, user);
                   });
  login->setAttribute(Qt::WA_DeleteOnClose);
  login->show();
}

// 主窗口：退出登录 → 关闭销毁主窗口并回到登录页
void open_main_window(GatewayClient* client, const im::UserInfo& user) {
  auto* w = new MainWindow(client, user);
  w->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(w, &MainWindow::logoutRequested, w,
                   [client]() { show_login_window(client); });
  w->showMaximized();
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setOrganizationName(QStringLiteral("im-system"));
  QApplication::setApplicationName(QStringLiteral("im-client"));
  app.setStyleSheet(QString::fromUtf8(kAppStyleSheet));  // 微信风格全局主题

  QSettings settings;
  const QString host = settings.value("server/host", QStringLiteral("47.112.192.119")).toString();
  const int port = settings.value("server/port", 9000).toInt();

  GatewayClient client(host, static_cast<quint16>(port), host, 9001);

  // 自动登录：本地存有 token 时先校验
  const QString saved_token = settings.value("account/token").toString();
  if (!saved_token.isEmpty()) {
    client.set_token(saved_token);
    QEventLoop loop;
    bool token_ok = false;
    UserInfo me;
    client.call_p<GetUserInfoResp>(
        REQ_TYPE_GET_USER_INFO, GetUserInfoReq{},
        [&](bool ok, const QString&, const GetUserInfoResp& resp) {
          token_ok = ok;
          if (ok) me = resp.user_info();
          loop.quit();
        });
    loop.exec();
    if (token_ok) {
      open_main_window(&client, me);
      return app.exec();
    }
    client.clear_token();
  }

  show_login_window(&client);
  return app.exec();
}
