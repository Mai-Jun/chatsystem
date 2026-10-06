#pragma once
#include <QDialog>

#include <QMap>
#include <QSet>

#include "base.pb.h"

class QLineEdit;
class QPushButton;
class QTextBrowser;
class QKeyEvent;

namespace im {

class GatewayClient;

// 聊天窗口（M9 第一版：文本消息）
// - 打开时拉取历史（升序展示）
// - 发送：TRANSMIT_MESSAGE（转发→持久化→MQ 广播）
// - 接收：主窗口转发 NEW_MESSAGE 推送（按 session 过滤、按 message_id 去重，
//   自己的消息经广播回来也会到达，因此发送成功后不重复上屏）
class ChatWindow : public QDialog {
  Q_OBJECT
 public:
  ChatWindow(GatewayClient* client, const QString& session_id, const QString& display_name,
             const QString& my_id, QWidget* parent = nullptr);

  // 主窗口收到 NEW_MESSAGE 推送时转发进来（body = MessageInfo 序列化）
  void on_message_push(const QByteArray& body);

 protected:
  void keyPressEvent(QKeyEvent* event) override;

 private:
  void load_history();
  void load_members();
  void append_message(const im::MessageInfo& msg);
  void send_text();

  GatewayClient* client_;
  QString session_id_;
  QString my_id_;

  QTextBrowser* view_ = nullptr;
  QLineEdit* input_ = nullptr;
  QPushButton* send_btn_ = nullptr;

  QMap<QString, QString> member_names_;   // user_id -> nickname
  QSet<QString> shown_ids_;               // 已上屏的 message_id（去重）
};

}  // namespace im
