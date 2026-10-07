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
//   服务器地址存 QSettings；有历史 token 则先尝试自动登录（GET_USER_INFO 校验）
using namespace im;  // NOLINT

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
  QWidget* first = nullptr;
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
      auto* w = new MainWindow(&client, me);
      w->setAttribute(Qt::WA_DeleteOnClose);
      w->showMaximized();
      first = w;
    } else {
      client.clear_token();
    }
  }

  if (first == nullptr) {
    auto* login = new LoginWindow(&client);
    // 登录成功 → 打开主窗口（登录窗 accept 后自动销毁）
    QObject::connect(login, &LoginWindow::loginSucceeded, login,
                     [&client](const QString& token, const im::UserInfo& user) {
                       QSettings().setValue("account/token", token);
                       auto* w = new MainWindow(&client, user);
                       w->setAttribute(Qt::WA_DeleteOnClose);
                       w->showMaximized();
                     });
    login->setAttribute(Qt::WA_DeleteOnClose);
    login->show();
  } else {
    settings.setValue("account/token", saved_token);
  }

  return app.exec();
}
