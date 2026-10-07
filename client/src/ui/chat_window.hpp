#pragma once
#include <QDialog>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

#include "base.pb.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QKeyEvent;
class QMediaPlayer;
class QAudioOutput;
class QTimer;

namespace im {

class GatewayClient;
class WavRecorder;

// 聊天窗口（微信风格，M9 第三轮）
// - 消息行用真实控件渲染：对方白气泡居左、自己绿气泡居右，首字圆形头像
// - 图片消息异步下载后就地替换为缩略图（≤220px），文件/语音用按钮交互（另存/播放）
// - 历史分页：首屏最新 50 条，「加载更多」按最旧时间戳+1 向前翻（message_id 去重）
// - 自己的消息以 WS 广播回显为准；推送未连通时用 MsgTransmitResp 本地补显
class ChatWindow : public QDialog {
  Q_OBJECT
 public:
  ChatWindow(GatewayClient* client, const QString& session_id, const QString& display_name,
             const QString& my_id, QWidget* parent = nullptr);

  // 主窗口收到 NEW_MESSAGE 推送时转发进来（body = MessageInfo 序列化）
  void on_message_push(const QByteArray& body);

 signals:
  void seen(const QString& session_id);  // 该会话被用户看到（清未读）

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  bool event(QEvent* e) override;

 private:
  void load_members();
  void load_history(int64_t cursor);
  void insert_row(const im::MessageInfo& msg, int index);
  void rebuild_row(const QString& message_id);
  QWidget* build_row(const im::MessageInfo& msg);
  QWidget* build_bubble(const im::MessageInfo& msg, bool mine);
  QString sender_display_name(const im::MessageInfo& msg) const;
  void on_push_message(const im::MessageInfo& msg);
  void send_text();
  void send_attachment(im::MessageType type);
  void toggle_record();
  void tick_record();
  void ensure_image(const QString& file_id);
  void save_attachment(const QString& file_id, const QString& fallback_name);
  void play_voice(const QString& file_id);
  void set_status(const QString& text);
  void set_busy(bool busy);
  void transmit(im::MessageType type, const QString& content, const QString& file_id,
                const QString& file_name, int64_t file_size);
  const im::MessageInfo* find_message(const QString& message_id) const;

  GatewayClient* client_;
  QString session_id_;
  QString display_name_;
  QString my_id_;

  QListWidget* view_ = nullptr;        // 消息列表（每条消息一个 item widget）
  QLineEdit* input_ = nullptr;
  QPushButton* send_btn_ = nullptr;
  QPushButton* image_btn_ = nullptr;
  QPushButton* file_btn_ = nullptr;
  QPushButton* voice_btn_ = nullptr;
  QPushButton* more_btn_ = nullptr;
  QLabel* status_ = nullptr;

  QVector<im::MessageInfo> messages_;      // 升序
  QMap<QString, QListWidgetItem*> rows_;   // message_id -> 列表项
  QMap<QString, QString> member_names_;    // user_id -> nickname
  QMap<QString, QString> file_names_;      // file_id -> file_name（另存用）
  QMap<QString, QImage> images_;           // file_id -> 已下载图片
  QSet<QString> image_pending_;

  WavRecorder* recorder_ = nullptr;
  QMediaPlayer* player_ = nullptr;
  QAudioOutput* audio_out_ = nullptr;
  QString playing_file_id_;
  QTimer* record_tick_ = nullptr;
  qint64 record_started_ms_ = 0;

  int64_t oldest_ts_ = 0;
  bool no_more_history_ = false;
  bool loading_history_ = false;
  bool group_chat_ = false;  // 会话成员数 > 2 时显示发送者昵称
};

}  // namespace im
