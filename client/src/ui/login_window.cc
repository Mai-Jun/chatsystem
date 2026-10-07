#include "ui/login_window.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "protocol/gateway_client.hpp"
#include "user.pb.h"

namespace im {

LoginWindow::LoginWindow(GatewayClient* client, QWidget* parent)
    : QDialog(parent), client_(client) {
  setWindowTitle(QStringLiteral("IM 即时通讯 - 登录"));
  setFixedSize(380, 300);

  auto* layout = new QVBoxLayout(this);
  tabs_ = new QTabWidget(this);
  layout->addWidget(tabs_);

  auto* login_tab = new QWidget(this);
  build_login_tab(login_tab);
  tabs_->addTab(login_tab, QStringLiteral("登录"));

  auto* reg_tab = new QWidget(this);
  build_register_tab(reg_tab);
  tabs_->addTab(reg_tab, QStringLiteral("注册"));

  // 服务器地址（持久化；默认为线上网关，本机调试可改 127.0.0.1）
  QSettings settings("im-system", "im-client");
  const QString host =
      settings.value("server/host", QStringLiteral("47.112.192.119")).toString();
  const int port = settings.value("server/port", 9000).toInt();
  auto* addr_row = new QHBoxLayout();
  auto* addr_edit = new QLineEdit(host, this);
  auto* port_edit = new QLineEdit(QString::number(port), this);
  port_edit->setFixedWidth(64);
  addr_row->addWidget(new QLabel(QStringLiteral("服务器"), this));
  addr_row->addWidget(addr_edit, 1);
  addr_row->addWidget(new QLabel(QStringLiteral(":"), this));
  addr_row->addWidget(port_edit);
  layout->addLayout(addr_row);
  connect(addr_edit, &QLineEdit::editingFinished, this, [this, addr_edit, port_edit, &settings]() {
    settings.setValue("server/host", addr_edit->text());
    settings.setValue("server/port", port_edit->text().toInt());
  });
}

void LoginWindow::build_login_tab(QWidget* tab) {
  auto* v = new QVBoxLayout(tab);
  auto* form = new QFormLayout();  // 不指定父：交给 v 接管（同时给两个布局设父会丢控件）
  login_phone_ = new QLineEdit(tab);
  login_phone_->setPlaceholderText(QStringLiteral("手机号"));
  login_password_ = new QLineEdit(tab);
  login_password_->setEchoMode(QLineEdit::Password);
  login_password_->setPlaceholderText(QStringLiteral("密码"));
  form->addRow(QStringLiteral("手机号"), login_phone_);
  form->addRow(QStringLiteral("密码"), login_password_);

  login_btn_ = new QPushButton(QStringLiteral("登录"), tab);
  login_btn_->setDefault(true);  // 密码框回车即登录
  login_status_ = new QLabel(tab);
  login_status_->setWordWrap(true);
  v->addLayout(form);
  v->addWidget(login_btn_);
  v->addWidget(login_status_);
  v->addStretch(1);
  connect(login_btn_, &QPushButton::clicked, this, &LoginWindow::do_login);
}

void LoginWindow::build_register_tab(QWidget* tab) {
  auto* v = new QVBoxLayout(tab);
  auto* form = new QFormLayout();  // 同上：单一布局持有
  reg_phone_ = new QLineEdit(tab);
  reg_code_ = new QLineEdit(tab);
  reg_code_->setPlaceholderText(QStringLiteral("开发模式固定码 666666"));
  reg_nickname_ = new QLineEdit(tab);
  reg_password_ = new QLineEdit(tab);
  reg_password_->setEchoMode(QLineEdit::Password);
  form->addRow(QStringLiteral("手机号"), reg_phone_);
  form->addRow(QStringLiteral("验证码"), reg_code_);
  form->addRow(QStringLiteral("昵称"), reg_nickname_);
  form->addRow(QStringLiteral("密码"), reg_password_);

  send_code_btn_ = new QPushButton(QStringLiteral("发送验证码"), tab);
  reg_btn_ = new QPushButton(QStringLiteral("注册并登录"), tab);
  reg_status_ = new QLabel(tab);
  reg_status_->setWordWrap(true);
  v->addLayout(form);
  v->addWidget(send_code_btn_);
  v->addWidget(reg_btn_);
  v->addWidget(reg_status_);
  v->addStretch(1);
  connect(send_code_btn_, &QPushButton::clicked, this, &LoginWindow::do_send_code);
  connect(reg_btn_, &QPushButton::clicked, this, &LoginWindow::do_register);
}

void LoginWindow::do_login() {
  const QString phone = login_phone_->text().trimmed();
  const QString password = login_password_->text();
  if (phone.isEmpty() || password.isEmpty()) {
    login_status_->setText(QStringLiteral("请输入手机号和密码"));
    return;
  }
  login_btn_->setEnabled(false);
  login_status_->setText(QStringLiteral("登录中..."));

  UserLoginReq req;
  req.set_phone(phone.toStdString());
  req.set_login_type(LOGIN_BY_PASSWORD);
  req.set_password(password.toStdString());
  client_->call_p<UserLoginResp>(
      REQ_TYPE_LOGIN, req, [this](bool ok, const QString& errmsg, const UserLoginResp& resp) {
        login_btn_->setEnabled(true);
        if (!ok) {
          login_status_->setText(errmsg);
          return;
        }
        after_login(QString::fromStdString(resp.token()), resp.user_info());
      });
}

void LoginWindow::do_send_code() {
  const QString phone = reg_phone_->text().trimmed();
  if (phone.isEmpty()) {
    reg_status_->setText(QStringLiteral("请先输入手机号"));
    return;
  }
  send_code_btn_->setEnabled(false);
  SendSmsCodeReq req;
  req.set_phone(phone.toStdString());
  client_->call_p<SendSmsCodeResp>(
      REQ_TYPE_SEND_SMS_CODE, req,
      [this](bool ok, const QString& errmsg, const SendSmsCodeResp&) {
        send_code_btn_->setEnabled(true);
        if (!ok) {
          reg_status_->setText(errmsg);
          return;
        }
        reg_status_->setText(
            QStringLiteral("验证码已发送（开发模式请输入 666666）"));
        // 60s 限发：期间禁用按钮
        send_code_btn_->setEnabled(false);
        QTimer::singleShot(60000, this, [this]() { send_code_btn_->setEnabled(true); });
      });
}

void LoginWindow::do_register() {
  const QString phone = reg_phone_->text().trimmed();
  const QString code = reg_code_->text().trimmed();
  const QString nickname = reg_nickname_->text().trimmed();
  const QString password = reg_password_->text();
  if (phone.isEmpty() || code.isEmpty() || nickname.isEmpty() || password.isEmpty()) {
    reg_status_->setText(QStringLiteral("请填写完整注册信息"));
    return;
  }
  reg_btn_->setEnabled(false);
  reg_status_->setText(QStringLiteral("注册中..."));

  UserRegisterReq req;
  req.set_phone(phone.toStdString());
  req.set_sms_code(code.toStdString());
  req.set_nickname(nickname.toStdString());
  req.set_password(password.toStdString());
  client_->call_p<UserRegisterResp>(
      REQ_TYPE_REGISTER, req,
      [this](bool ok, const QString& errmsg, const UserRegisterResp& resp) {
        if (!ok) {
          reg_btn_->setEnabled(true);
          reg_status_->setText(errmsg);
          return;
        }
        // 注册成功即登录（复用注册页的密码）
        UserLoginReq login;
        login.set_phone(reg_phone_->text().trimmed().toStdString());
        login.set_login_type(LOGIN_BY_PASSWORD);
        login.set_password(reg_password_->text().toStdString());
        client_->call_p<UserLoginResp>(
            REQ_TYPE_LOGIN, login,
            [this](bool ok2, const QString& err2, const UserLoginResp& lresp) {
              reg_btn_->setEnabled(true);
              if (!ok2) {
                reg_status_->setText(err2);
                return;
              }
              after_login(QString::fromStdString(lresp.token()), lresp.user_info());
            });
      });
}

void LoginWindow::after_login(const QString& token, const im::UserInfo& user) {
  client_->set_token(token);
  QSettings settings("im-system", "im-client");
  settings.setValue("account/phone", QString::fromStdString(user.phone()));
  emit loginSucceeded(token, user);
  accept();  // 关闭登录窗
}

}  // namespace im
