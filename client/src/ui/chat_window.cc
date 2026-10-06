#include "ui/chat_window.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QStringLiteral>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "friend.pb.h"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "protocol/gateway_client.hpp"

namespace im {

namespace {

QString escape_html(const QString& s) {
  QString r = s;
  r.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
  r.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
  r.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
  return r;
}

QString format_time(int64_t seconds) {
  return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("MM-dd hh:mm"));
}

}  // namespace

ChatWindow::ChatWindow(GatewayClient* client, const QString& session_id,
                       const QString& display_name, const QString& my_id, QWidget* parent)
    : QDialog(parent),
      client_(client),
      session_id_(session_id),
      my_id_(my_id) {
  setWindowTitle(QString("会话 - %1").arg(display_name));
  resize(520, 560);

  auto* v = new QVBoxLayout(this);
  view_ = new QTextBrowser(this);
  view_->setOpenExternalLinks(false);
  v->addWidget(view_, 1);

  auto* row = new QHBoxLayout();
  input_ = new QLineEdit(this);
  input_->setPlaceholderText(QStringLiteral("输入消息，回车发送（语音/图片/文件后续版本）"));
  send_btn_ = new QPushButton(QStringLiteral("发送"), this);
  row->addWidget(input_, 1);
  row->addWidget(send_btn_);
  v->addLayout(row);

  connect(send_btn_, &QPushButton::clicked, this, &ChatWindow::send_text);
  connect(input_, &QLineEdit::returnPressed, this, &ChatWindow::send_text);

  load_members();
  load_history();
}

void ChatWindow::load_members() {
  GetSessionMemberReq req;
  req.set_chat_session_id(session_id_.toStdString());
  client_->call_p<GetSessionMemberResp>(
      REQ_TYPE_GET_SESSION_MEMBER, req,
      [this](bool ok, const QString&, const GetSessionMemberResp& resp) {
        if (!ok) return;
        for (const auto& m : resp.members()) {
          member_names_[QString::fromStdString(m.user_id())] =
              QString::fromStdString(m.nickname().empty() ? m.user_id() : m.nickname());
        }
      });
}

void ChatWindow::load_history() {
  GetHistoryReq req;
  req.set_chat_session_id(session_id_.toStdString());
  req.set_cursor_timestamp(0);  // 最新一页
  req.set_limit(50);
  client_->call_p<GetHistoryResp>(
      REQ_TYPE_GET_HISTORY, req,
      [this](bool ok, const QString& err, const GetHistoryResp& resp) {
        if (!ok) {
          view_->append(QStringLiteral("<span style='color:#c00'>历史加载失败: %1</span>")
                            .arg(escape_html(err)));
          return;
        }
        // 升序返回，直接顺序上屏
        for (const auto& m : resp.messages()) append_message(m);
      });
}

void ChatWindow::on_message_push(const QByteArray& body) {
  im::MessageInfo msg;
  if (!msg.ParseFromArray(body.constData(), body.size())) return;
  if (QString::fromStdString(msg.chat_session_id()) != session_id_) return;
  append_message(msg);
}

void ChatWindow::append_message(const im::MessageInfo& msg) {
  const QString mid = QString::fromStdString(msg.message_id());
  if (shown_ids_.contains(mid)) return;  // 广播回来的自己的消息/重复推送
  shown_ids_.insert(mid);

  const bool mine = QString::fromStdString(msg.sender_id()) == my_id_;
  QString sender = member_names_.value(QString::fromStdString(msg.sender_id()),
                                       QString::fromStdString(msg.sender_id()));
  if (mine) sender = QStringLiteral("我");

  QString content;
  switch (msg.type()) {
    case MESSAGE_TYPE_TEXT:
      content = escape_html(QString::fromStdString(msg.content()));
      break;
    case MESSAGE_TYPE_IMAGE:
      content = QStringLiteral("[图片] %1").arg(
          escape_html(QString::fromStdString(msg.file_name())));
      break;
    case MESSAGE_TYPE_FILE:
      content = QStringLiteral("[文件] %1").arg(
          escape_html(QString::fromStdString(msg.file_name())));
      break;
    case MESSAGE_TYPE_VOICE:
      content = QStringLiteral("[语音] %1")
                    .arg(escape_html(QString::fromStdString(msg.asr_text())));
      break;
    default:
      content = QStringLiteral("[未知类型]");
      break;
  }

  const QString color = mine ? QStringLiteral("#1a73e8") : QStringLiteral("#444");
  view_->append(QStringLiteral("<span style='color:#999;font-size:small'>[%1] %2</span><br>"
                               "<span style='color:%3'>%4</span>")
                    .arg(format_time(msg.create_time()), escape_html(sender), color, content));
}

void ChatWindow::send_text() {
  const QString text = input_->text().trimmed();
  if (text.isEmpty()) return;
  send_btn_->setEnabled(false);
  MsgTransmitReq req;
  req.set_chat_session_id(session_id_.toStdString());
  req.mutable_content()->set_type(MESSAGE_TYPE_TEXT);
  req.mutable_content()->set_content(text.toStdString());
  client_->call_p<MsgTransmitResp>(
      REQ_TYPE_TRANSMIT_MESSAGE, req,
      [this](bool ok, const QString& err, const MsgTransmitResp&) {
        send_btn_->setEnabled(true);
        if (!ok) {
          view_->append(QStringLiteral("<span style='color:#c00'>发送失败: %1</span>")
                            .arg(escape_html(err)));
          return;
        }
        input_->clear();
        // 消息上屏由广播推送驱动（网关会推给在线的自己）
      });
}

void ChatWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    close();
    return;
  }
  QDialog::keyPressEvent(event);
}

}  // namespace im
