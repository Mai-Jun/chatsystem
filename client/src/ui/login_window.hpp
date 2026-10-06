#pragma once
#include <QDialog>

#include "base.pb.h"
#include "gateway.pb.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QTabWidget;

namespace im {

class GatewayClient;

// 登录/注册窗口（M9 第一版）
// - 登录：手机号 + 密码
// - 注册：手机号 + 短信验证码（开发模式固定码 666666）+ 昵称 + 密码，注册成功自动登录
// - 服务器地址可配置并持久化（QSettings）
class LoginWindow : public QDialog {
  Q_OBJECT
 public:
  explicit LoginWindow(GatewayClient* client, QWidget* parent = nullptr);

 signals:
  void loginSucceeded(QString token, im::UserInfo user);

 private:
  void build_login_tab(QWidget* tab);
  void build_register_tab(QWidget* tab);
  void do_login();
  void do_send_code();
  void do_register();
  void after_login(const QString& token, const im::UserInfo& user);

  GatewayClient* client_;
  QTabWidget* tabs_ = nullptr;

  // 登录页
  QLineEdit* login_phone_ = nullptr;
  QLineEdit* login_password_ = nullptr;
  QPushButton* login_btn_ = nullptr;
  QLabel* login_status_ = nullptr;

  // 注册页
  QLineEdit* reg_phone_ = nullptr;
  QLineEdit* reg_code_ = nullptr;
  QLineEdit* reg_nickname_ = nullptr;
  QLineEdit* reg_password_ = nullptr;
  QPushButton* send_code_btn_ = nullptr;
  QPushButton* reg_btn_ = nullptr;
  QLabel* reg_status_ = nullptr;
};

}  // namespace im
