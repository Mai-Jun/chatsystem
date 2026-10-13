#include "ui/login_window.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "protocol/gateway_client.hpp"
#include "user.pb.h"

namespace im {

namespace {

// 微信式品牌 Logo：绿色圆角方块 + 白色聊天气泡
QPixmap make_logo(int size) {
  QPixmap pm(size, size);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  QPainterPath rounded;
  rounded.addRoundedRect(QRectF(0, 0, size, size), size * 0.24, size * 0.24);
  p.fillPath(rounded, QColor("#07C160"));
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::white);
  p.drawEllipse(QRectF(size * 0.20, size * 0.22, size * 0.47, size * 0.47));
  QPainterPath tail;
  tail.moveTo(size * 0.58, size * 0.62);
  tail.lineTo(size * 0.77, size * 0.79);
  tail.lineTo(size * 0.60, size * 0.72);
  tail.closeSubpath();
  p.drawPath(tail);
  p.drawEllipse(QRectF(size * 0.40, size * 0.42, size * 0.15, size * 0.15));
  return pm;
}

}  // namespace

LoginWindow::LoginWindow(GatewayClient* client, QWidget* parent)
    : QDialog(parent), client_(client) {
  setWindowTitle(QStringLiteral("IM 即时通讯 - 登录"));
  setObjectName(QStringLiteral("loginDialog"));
  setFixedSize(420, 560);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(26, 26, 26, 12);
  layout->setSpacing(12);

  // ---- 品牌区：Logo + 标题 + 副标题 ----
  auto* logo = new QLabel(this);
  logo->setPixmap(make_logo(64));
  logo->setFixedSize(64, 64);
  layout->addWidget(logo, 0, Qt::AlignHCenter);

  auto* title = new QLabel(QStringLiteral("IM 即时通讯"), this);
  title->setObjectName(QStringLiteral("title"));
  title->setAlignment(Qt::AlignCenter);
  auto* subtitle = new QLabel(QStringLiteral("账号密码登录，也可使用手机号验证码登录"), this);
  subtitle->setObjectName(QStringLiteral("subtitle"));
  subtitle->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);
  layout->addWidget(subtitle);

  // ---- 三标签头（等分，绿下划线）+ 页栈 ----
  layout->addWidget(build_tab_header());

  stack_ = new QStackedWidget(this);
  stack_->addWidget(build_password_tab());
  stack_->addWidget(build_sms_tab());
  stack_->addWidget(build_register_tab());
  layout->addWidget(stack_, 1);
  set_tab(0);

  // ---- 服务器地址（持久化；默认为线上网关，本机调试可改 127.0.0.1）----
  QSettings settings("im-system", "im-client");
  const QString host =
      settings.value("server/host", QStringLiteral("47.112.192.119")).toString();
  const int port = settings.value("server/port", 9000).toInt();
  auto* line = new QFrame(this);
  line->setFrameShape(QFrame::HLine);
  line->setStyleSheet(QStringLiteral("color:#F0F0F0;"));
  layout->addWidget(line);

  auto* addr_row = new QHBoxLayout();
  addr_row->setSpacing(4);
  auto* addr_label = new QLabel(QStringLiteral("服务器"), this);
  addr_label->setObjectName(QStringLiteral("hint"));
  auto* addr_edit = new QLineEdit(host, this);
  addr_edit->setObjectName(QStringLiteral("addrEdit"));
  auto* port_edit = new QLineEdit(QString::number(port), this);
  port_edit->setObjectName(QStringLiteral("addrEdit"));
  port_edit->setFixedWidth(52);
  addr_row->addWidget(addr_label);
  addr_row->addWidget(addr_edit, 1);
  addr_row->addWidget(new QLabel(QStringLiteral(":"), this));
  addr_row->addWidget(port_edit);
  layout->addLayout(addr_row);
  connect(addr_edit, &QLineEdit::editingFinished, this, [this, addr_edit, port_edit, &settings]() {
    settings.setValue("server/host", addr_edit->text());
    settings.setValue("server/port", port_edit->text().toInt());
  });

  // 已预填手机号时焦点直接给密码框（省一次点击）
  if (!pwd_phone_->text().isEmpty()) {
    pwd_password_->setFocus();
  } else {
    pwd_phone_->setFocus();
  }
}

// 顶部三标签：等分宽度的扁平按钮，选中态由 QSS :checked 画绿下划线
QWidget* LoginWindow::build_tab_header() {
  auto* header = new QWidget(this);
  auto* h = new QHBoxLayout(header);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(0);
  const QStringList names = {QStringLiteral("账号密码"), QStringLiteral("验证码登录"),
                             QStringLiteral("注册")};
  for (int i = 0; i < names.size(); ++i) {
    auto* btn = new QPushButton(names[i], header);
    btn->setObjectName(QStringLiteral("loginTab"));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QPushButton::clicked, this, [this, i]() { set_tab(i); });
    h->addWidget(btn, 1);  // stretch=1：三等分
    tab_btns_.append(btn);
  }
  return header;
}

void LoginWindow::set_tab(int index) {
  if (stack_ == nullptr || index < 0 || index >= stack_->count()) return;
  stack_->setCurrentIndex(index);
  for (int i = 0; i < tab_btns_.size(); ++i) tab_btns_[i]->setChecked(i == index);
}

QWidget* LoginWindow::build_password_tab() {
  auto* tab = new QWidget(this);
  auto* v = new QVBoxLayout(tab);
  v->setContentsMargins(4, 22, 4, 8);
  v->setSpacing(14);

  pwd_phone_ = new QLineEdit(tab);
  pwd_phone_->setPlaceholderText(QStringLiteral("手机号"));
  // 预填上次登录的手机号（退出登录回到本页时省一次输入）
  pwd_phone_->setText(QSettings("im-system", "im-client").value("account/phone").toString());
  pwd_password_ = new QLineEdit(tab);
  pwd_password_->setEchoMode(QLineEdit::Password);
  pwd_password_->setPlaceholderText(QStringLiteral("密码"));
  v->addWidget(pwd_phone_);
  v->addWidget(pwd_password_);

  pwd_btn_ = new QPushButton(QStringLiteral("登 录"), tab);
  pwd_btn_->setObjectName(QStringLiteral("primary"));
  pwd_btn_->setDefault(true);
  v->addWidget(pwd_btn_);

  pwd_status_ = new QLabel(tab);
  pwd_status_->setObjectName(QStringLiteral("loginStatus"));
  pwd_status_->setWordWrap(true);
  v->addWidget(pwd_status_);
  v->addStretch(1);

  connect(pwd_btn_, &QPushButton::clicked, this, &LoginWindow::do_password_login);
  return tab;
}

QWidget* LoginWindow::build_sms_tab() {
  auto* tab = new QWidget(this);
  auto* v = new QVBoxLayout(tab);
  v->setContentsMargins(4, 22, 4, 8);
  v->setSpacing(14);

  sms_phone_ = new QLineEdit(tab);
  sms_phone_->setPlaceholderText(QStringLiteral("手机号"));
  // 与密码登录共用上次手机号
  sms_phone_->setText(QSettings("im-system", "im-client").value("account/phone").toString());
  v->addWidget(sms_phone_);

  auto* code_row = new QHBoxLayout();
  code_row->setSpacing(8);
  sms_code_ = new QLineEdit(tab);
  sms_code_->setPlaceholderText(QStringLiteral("短信验证码"));
  sms_send_btn_ = new QPushButton(QStringLiteral("获取验证码"), tab);
  sms_send_btn_->setFixedWidth(104);
  code_row->addWidget(sms_code_, 1);
  code_row->addWidget(sms_send_btn_);
  v->addLayout(code_row);

  sms_btn_ = new QPushButton(QStringLiteral("登 录"), tab);
  sms_btn_->setObjectName(QStringLiteral("primary"));
  v->addWidget(sms_btn_);

  sms_status_ = new QLabel(tab);
  sms_status_->setObjectName(QStringLiteral("loginStatus"));
  sms_status_->setWordWrap(true);
  v->addWidget(sms_status_);
  v->addStretch(1);

  connect(sms_send_btn_, &QPushButton::clicked, this, [this]() {
    send_code(sms_phone_->text().trimmed(), sms_send_btn_, sms_status_);
  });
  connect(sms_btn_, &QPushButton::clicked, this, &LoginWindow::do_sms_login);
  return tab;
}

QWidget* LoginWindow::build_register_tab() {
  auto* tab = new QWidget(this);
  auto* v = new QVBoxLayout(tab);
  v->setContentsMargins(4, 16, 4, 8);
  v->setSpacing(10);

  auto* phone_row = new QHBoxLayout();
  phone_row->setSpacing(8);
  reg_phone_ = new QLineEdit(tab);
  reg_phone_->setPlaceholderText(QStringLiteral("手机号"));
  reg_send_btn_ = new QPushButton(QStringLiteral("获取验证码"), tab);
  reg_send_btn_->setFixedWidth(104);
  phone_row->addWidget(reg_phone_, 1);
  phone_row->addWidget(reg_send_btn_);
  v->addLayout(phone_row);

  reg_code_ = new QLineEdit(tab);
  reg_code_->setPlaceholderText(QStringLiteral("短信验证码（开发模式固定码 666666）"));
  v->addWidget(reg_code_);

  reg_nickname_ = new QLineEdit(tab);
  reg_nickname_->setPlaceholderText(QStringLiteral("昵称"));
  v->addWidget(reg_nickname_);

  reg_password_ = new QLineEdit(tab);
  reg_password_->setEchoMode(QLineEdit::Password);
  reg_password_->setPlaceholderText(QStringLiteral("设置密码"));
  v->addWidget(reg_password_);

  reg_btn_ = new QPushButton(QStringLiteral("注册并登录"), tab);
  reg_btn_->setObjectName(QStringLiteral("primary"));
  v->addWidget(reg_btn_);

  reg_status_ = new QLabel(tab);
  reg_status_->setObjectName(QStringLiteral("loginStatus"));
  reg_status_->setWordWrap(true);
  v->addWidget(reg_status_);
  v->addStretch(1);

  connect(reg_send_btn_, &QPushButton::clicked, this, [this]() {
    send_code(reg_phone_->text().trimmed(), reg_send_btn_, reg_status_);
  });
  connect(reg_btn_, &QPushButton::clicked, this, &LoginWindow::do_register);
  return tab;
}

void LoginWindow::send_code(const QString& phone, QPushButton* btn, QLabel* status) {
  if (phone.isEmpty()) {
    set_status(status, QStringLiteral("请先输入手机号"));
    return;
  }
  btn->setEnabled(false);
  SendSmsCodeReq req;
  req.set_phone(phone.toStdString());
  client_->call_p<SendSmsCodeResp>(
      REQ_TYPE_SEND_SMS_CODE, req,
      [this, btn, status](bool ok, const QString& errmsg, const SendSmsCodeResp&) {
        if (!ok) {
          btn->setEnabled(true);
          set_status(status, errmsg);
          return;
        }
        set_status(status, QStringLiteral("验证码已发送（开发模式请输入 666666）"));
        // 60s 限发倒计时
        btn->setProperty("left", 60);
        btn->setText(QStringLiteral("重发(60s)"));
        auto* timer = new QTimer(btn);
        timer->setInterval(1000);
        connect(timer, &QTimer::timeout, btn, [btn, timer]() {
          int left = btn->property("left").toInt() - 1;
          btn->setProperty("left", left);
          if (left <= 0) {
            btn->setText(QStringLiteral("获取验证码"));
            btn->setEnabled(true);
            timer->stop();
            timer->deleteLater();
          } else {
            btn->setText(QStringLiteral("重发(%1s)").arg(left));
          }
        });
        timer->start();
      });
}

void LoginWindow::set_status(QLabel* label, const QString& text) { label->setText(text); }

void LoginWindow::do_password_login() {
  const QString phone = pwd_phone_->text().trimmed();
  const QString password = pwd_password_->text();
  if (phone.isEmpty() || password.isEmpty()) {
    set_status(pwd_status_, QStringLiteral("请输入手机号和密码"));
    return;
  }
  pwd_btn_->setEnabled(false);
  set_status(pwd_status_, QStringLiteral("登录中..."));

  UserLoginReq req;
  req.set_phone(phone.toStdString());
  req.set_login_type(LOGIN_BY_PASSWORD);
  req.set_password(password.toStdString());
  client_->call_p<UserLoginResp>(
      REQ_TYPE_LOGIN, req, [this](bool ok, const QString& errmsg, const UserLoginResp& resp) {
        pwd_btn_->setEnabled(true);
        if (!ok) {
          set_status(pwd_status_, errmsg);
          return;
        }
        after_login(QString::fromStdString(resp.token()), resp.user_info());
      });
}

void LoginWindow::do_sms_login() {
  const QString phone = sms_phone_->text().trimmed();
  const QString code = sms_code_->text().trimmed();
  if (phone.isEmpty() || code.isEmpty()) {
    set_status(sms_status_, QStringLiteral("请输入手机号和验证码"));
    return;
  }
  sms_btn_->setEnabled(false);
  set_status(sms_status_, QStringLiteral("登录中..."));

  UserLoginReq req;
  req.set_phone(phone.toStdString());
  req.set_login_type(LOGIN_BY_SMS);
  req.set_sms_code(code.toStdString());
  client_->call_p<UserLoginResp>(
      REQ_TYPE_LOGIN, req, [this](bool ok, const QString& errmsg, const UserLoginResp& resp) {
        sms_btn_->setEnabled(true);
        if (!ok) {
          set_status(sms_status_, errmsg);
          return;
        }
        after_login(QString::fromStdString(resp.token()), resp.user_info());
      });
}

void LoginWindow::do_register() {
  const QString phone = reg_phone_->text().trimmed();
  const QString code = reg_code_->text().trimmed();
  const QString nickname = reg_nickname_->text().trimmed();
  const QString password = reg_password_->text();
  if (phone.isEmpty() || code.isEmpty() || nickname.isEmpty() || password.isEmpty()) {
    set_status(reg_status_, QStringLiteral("请填写完整注册信息"));
    return;
  }
  reg_btn_->setEnabled(false);
  set_status(reg_status_, QStringLiteral("注册中..."));

  UserRegisterReq req;
  req.set_phone(phone.toStdString());
  req.set_sms_code(code.toStdString());
  req.set_nickname(nickname.toStdString());
  req.set_password(password.toStdString());
  client_->call_p<UserRegisterResp>(
      REQ_TYPE_REGISTER, req, [this](bool ok, const QString& errmsg, const UserRegisterResp&) {
        if (!ok) {
          reg_btn_->setEnabled(true);
          set_status(reg_status_, errmsg);
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
                set_status(reg_status_, err2);
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
