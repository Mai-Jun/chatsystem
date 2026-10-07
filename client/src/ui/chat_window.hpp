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
class QTextBrowser;
class QKeyEvent;
class QMediaPlayer;
class QAudioOutput;
class QTimer;
class QUrl;

namespace im {

class GatewayClient;
class WavRecorder;

// 聊天窗口（M9 第二版）：文本 + 图片 + 文件 + 语音
// - 历史分页：首屏最新一页，「加载更多」按最旧时间戳向前翻页（升序渲染）
// - 图片：下载后内嵌显示（QTextDocument 图片资源，避免 base64 撑爆 HTML）
// - 文件：消息内「另存」锚点，点击选目录落盘
// - 语音：QtMultimedia 录音 → WAV 上传 → 服务端 ASR，气泡显示转写文本，可回放
// - 自己的消息以 WS 广播回显为准（message_id 去重）；推送未连通时用
//   MsgTransmitResp 的返回值本地补显，避免发送后“无反应”
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
  void render_all(bool keep_position);
  void append_message(const im::MessageInfo& msg);
  QString bubble_html(const im::MessageInfo& msg) const;
  QString sender_name(const im::MessageInfo& msg) const;
  void on_anchor(const QUrl& url);
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

  GatewayClient* client_;
  QString session_id_;
  QString display_name_;
  QString my_id_;

  QTextBrowser* view_ = nullptr;
  QLineEdit* input_ = nullptr;
  QPushButton* send_btn_ = nullptr;
  QPushButton* image_btn_ = nullptr;
  QPushButton* file_btn_ = nullptr;
  QPushButton* voice_btn_ = nullptr;
  QPushButton* more_btn_ = nullptr;
  QLabel* status_ = nullptr;

  QVector<im::MessageInfo> messages_;      // 升序
  QSet<QString> shown_ids_;                // message_id 去重
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
};

}  // namespace im
