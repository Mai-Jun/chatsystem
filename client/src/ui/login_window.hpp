#pragma once
#include <QDialog>
#include <QVector>

#include "base.pb.h"
#include "gateway.pb.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QStackedWidget;

namespace im {

class GatewayClient;

// 登录/注册窗口（微信风格）
// - 自绘等分标签头（账号密码 / 验证码登录 / 注册）+ QStackedWidget 页
//   （不用 QTabWidget：其 setExpanding 在带 QSS 环境下不可靠，且下划线样式难精确控制）
// - 账号密码登录（主入口）；验证码登录走 LOGIN_BY_SMS（开发模式固定码 666666）
// - 注册成功自动登录；服务器地址可配置并持久化（QSettings）
class LoginWindow : public QDialog {
  Q_OBJECT
 public:
  explicit LoginWindow(GatewayClient* client, QWidget* parent = nullptr);

 signals:
  void loginSucceeded(QString token, im::UserInfo user);

 private:
  QWidget* build_tab_header();   // 顶部三标签（等分，绿下划线）
  QWidget* build_password_tab();
  QWidget* build_sms_tab();
  QWidget* build_register_tab();
  void set_tab(int index);       // 切换页 + 标签选中态
  void do_password_login();
  void do_sms_login();
  void do_register();
  // 发送验证码（带 60s 倒计时），错误信息打到 status
  void send_code(const QString& phone, QPushButton* btn, QLabel* status);
  void after_login(const QString& token, const im::UserInfo& user);
  void set_status(QLabel* label, const QString& text);

  GatewayClient* client_;
  QStackedWidget* stack_ = nullptr;
  QVector<QPushButton*> tab_btns_;

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
