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

// 登录/注册窗口（微信风格，M9 第三轮）
// - 账号密码登录（主入口，第一个标签页）
// - 验证码登录：手机号 + 短信验证码（开发模式固定码 666666）
// - 注册：手机号 + 验证码 + 昵称 + 密码，注册成功自动登录
// - 服务器地址可配置并持久化（QSettings）
class LoginWindow : public QDialog {
  Q_OBJECT
 public:
  explicit LoginWindow(GatewayClient* client, QWidget* parent = nullptr);

 signals:
  void loginSucceeded(QString token, im::UserInfo user);

 private:
  QWidget* build_password_tab();
  QWidget* build_sms_tab();
  QWidget* build_register_tab();
  void do_password_login();
  void do_sms_login();
  void do_register();
  // 发送验证码（带 60s 倒计时），错误信息打到 status
  void send_code(const QString& phone, QPushButton* btn, QLabel* status);
  void after_login(const QString& token, const im::UserInfo& user);
  void set_status(QLabel* label, const QString& text);

  GatewayClient* client_;
  QTabWidget* tabs_ = nullptr;

  // 账号密码（主入口）
  QLineEdit* pwd_phone_ = nullptr;
  QLineEdit* pwd_password_ = nullptr;
  QPushButton* pwd_btn_ = nullptr;
  QLabel* pwd_status_ = nullptr;

  // 验证码登录
  QLineEdit* sms_phone_ = nullptr;
  QLineEdit* sms_code_ = nullptr;
  QPushButton* sms_send_btn_ = nullptr;
  QPushButton* sms_btn_ = nullptr;
  QLabel* sms_status_ = nullptr;

  // 注册
  QLineEdit* reg_phone_ = nullptr;
  QLineEdit* reg_code_ = nullptr;
  QLineEdit* reg_nickname_ = nullptr;
  QLineEdit* reg_password_ = nullptr;
  QPushButton* reg_send_btn_ = nullptr;
  QPushButton* reg_btn_ = nullptr;
  QLabel* reg_status_ = nullptr;
};

}  // namespace im
