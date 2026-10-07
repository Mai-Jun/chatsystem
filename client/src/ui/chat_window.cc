#include "ui/chat_window.hpp"

#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QStringLiteral>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include "friend.pb.h"
#include "media/wav_recorder.hpp"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "protocol/gateway_client.hpp"

namespace im {

namespace {

constexpr int kPageSize = 50;
constexpr int kMinVoiceMs = 500;   // 短于该时长视为误触，不上传
constexpr int kMaxVoiceMs = 60000;  // 上限 60s，到点自动停止
constexpr int kImageWidth = 220;

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

QString human_size(int64_t bytes) {
  if (bytes < 1024) return QString("%1 B").arg(bytes);
  if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
  return QString("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
}

QString format_ms(int ms) {
  return QString("%1:%2").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

ChatWindow::ChatWindow(GatewayClient* client, const QString& session_id,
                       const QString& display_name, const QString& my_id, QWidget* parent)
    : QDialog(parent),
      client_(client),
      session_id_(session_id),
      display_name_(display_name),
      my_id_(my_id) {
  setWindowTitle(QString("会话 - %1").arg(display_name));
  resize(560, 620);

  auto* v = new QVBoxLayout(this);

  // 顶栏：历史分页
  auto* top = new QHBoxLayout();
  more_btn_ = new QPushButton(QStringLiteral("加载更多"), this);
  more_btn_->setEnabled(false);
  status_ = new QLabel(QStringLiteral("历史加载中..."), this);
  top->addWidget(more_btn_);
  top->addWidget(status_, 1);
  v->addLayout(top);

  view_ = new QTextBrowser(this);
  view_->setOpenLinks(false);
  view_->setOpenExternalLinks(false);
  v->addWidget(view_, 1);

  auto* row = new QHBoxLayout();
  image_btn_ = new QPushButton(QStringLiteral("图片"), this);
  file_btn_ = new QPushButton(QStringLiteral("文件"), this);
  voice_btn_ = new QPushButton(QStringLiteral("录音"), this);
  input_ = new QLineEdit(this);
  input_->setPlaceholderText(QStringLiteral("输入消息，回车发送"));
  send_btn_ = new QPushButton(QStringLiteral("发送"), this);
  row->addWidget(image_btn_);
  row->addWidget(file_btn_);
  row->addWidget(voice_btn_);
  row->addWidget(input_, 1);
  row->addWidget(send_btn_);
  v->addLayout(row);

  connect(send_btn_, &QPushButton::clicked, this, &ChatWindow::send_text);
  connect(input_, &QLineEdit::returnPressed, this, &ChatWindow::send_text);
  connect(image_btn_, &QPushButton::clicked, this, [this]() { send_attachment(MESSAGE_TYPE_IMAGE); });
  connect(file_btn_, &QPushButton::clicked, this, [this]() { send_attachment(MESSAGE_TYPE_FILE); });
  connect(voice_btn_, &QPushButton::clicked, this, &ChatWindow::toggle_record);
  connect(more_btn_, &QPushButton::clicked, this, [this]() { load_history(oldest_ts_); });
  connect(view_, &QTextBrowser::anchorClicked, this, &ChatWindow::on_anchor);

  recorder_ = new WavRecorder(this);
  record_tick_ = new QTimer(this);
  record_tick_->setInterval(200);
  connect(record_tick_, &QTimer::timeout, this, &ChatWindow::tick_record);

  load_members();
  load_history(0);
}

bool ChatWindow::event(QEvent* e) {
  if (e->type() == QEvent::WindowActivate) emit seen(session_id_);
  return QDialog::event(e);
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
        render_all(true);
      });
}

void ChatWindow::load_history(int64_t cursor) {
  if (loading_history_) return;
  loading_history_ = true;
  more_btn_->setEnabled(false);

  const bool prepend = cursor != 0;  // 0 = 首屏最新一页；否则向前翻页
  GetHistoryReq req;
  req.set_chat_session_id(session_id_.toStdString());
  // 服务端语义为 create_time < cursor（开区间），游标用「最旧时间戳 + 1」，
  // 否则与分页边界同秒的消息会被永久跳过；重复项由 shown_ids_ 去重
  req.set_cursor_timestamp(prepend ? cursor + 1 : 0);
  req.set_limit(kPageSize);
  client_->call_p<GetHistoryResp>(
      REQ_TYPE_GET_HISTORY, req,
      [this, prepend](bool ok, const QString& err, const GetHistoryResp& resp) {
        loading_history_ = false;
        if (!ok) {
          set_status(QStringLiteral("历史加载失败: %1").arg(err));
          more_btn_->setEnabled(true);
          return;
        }
        QVector<im::MessageInfo> page;
        for (const auto& m : resp.messages()) {
          if (shown_ids_.contains(QString::fromStdString(m.message_id()))) continue;
          page.append(m);
        }
        if (prepend) {
          // 服务端升序返回：整页插到最前，保持全局升序
          for (int i = page.size() - 1; i >= 0; --i) messages_.prepend(page[i]);
        } else {
          for (const auto& m : page) messages_.append(m);
        }
        for (const auto& m : page) shown_ids_.insert(QString::fromStdString(m.message_id()));
        if (resp.messages_size() < kPageSize) no_more_history_ = true;
        if (!messages_.isEmpty()) oldest_ts_ = messages_.first().create_time();
        render_all(prepend);
        more_btn_->setEnabled(!no_more_history_ && !messages_.isEmpty());
        set_status(prepend && page.isEmpty() ? QStringLiteral("没有更多历史了")
                                            : QStringLiteral("%1 条消息").arg(messages_.size()));
      });
}

void ChatWindow::on_message_push(const QByteArray& body) {
  im::MessageInfo msg;
  if (!msg.ParseFromArray(body.constData(), body.size())) return;
  if (QString::fromStdString(msg.chat_session_id()) != session_id_) return;
  append_message(msg);
  if (isActiveWindow()) emit seen(session_id_);
}

void ChatWindow::append_message(const im::MessageInfo& msg) {
  const QString mid = QString::fromStdString(msg.message_id());
  if (shown_ids_.contains(mid)) return;  // 广播回来的自己的消息/重复推送
  shown_ids_.insert(mid);
  messages_.append(msg);
  if (!msg.file_id().empty()) file_names_[QString::fromStdString(msg.file_id())] =
                                  QString::fromStdString(msg.file_name());
  render_all(false);
}

QString ChatWindow::sender_name(const im::MessageInfo& msg) const {
  const QString uid = QString::fromStdString(msg.sender_id());
  if (uid == my_id_) return QStringLiteral("我");
  return member_names_.value(uid, uid);
}

QString ChatWindow::bubble_html(const im::MessageInfo& msg) const {
  const bool mine = QString::fromStdString(msg.sender_id()) == my_id_;
  const QString color = mine ? QStringLiteral("#1a73e8") : QStringLiteral("#333");
  const QString fid = QString::fromStdString(msg.file_id());
  const QString fname = escape_html(QString::fromStdString(msg.file_name()));

  QString content;
  switch (msg.type()) {
    case MESSAGE_TYPE_TEXT:
      content = escape_html(QString::fromStdString(msg.content()));
      break;
    case MESSAGE_TYPE_IMAGE: {
      const QString img = images_.contains(fid)
                              ? QString("<img src=\"imimg:%1\" width=\"%2\">")
                                    .arg(fid)
                                    .arg(kImageWidth)
                              : QStringLiteral("<i>[图片下载中...]</i>");
      content = QString("%1 %2 <a href=\"imgsave:%3\">[保存]</a>").arg(img, fname, fid);
      break;
    }
    case MESSAGE_TYPE_FILE:
      content = QStringLiteral("[文件] %1 (%2) <a href=\"imsave:%3\">[另存]</a>")
                    .arg(fname, human_size(msg.file_size()), fid);
      break;
    case MESSAGE_TYPE_VOICE: {
      const QString asr = escape_html(QString::fromStdString(msg.asr_text()));
      const QString label = asr.isEmpty() ? QStringLiteral("(转写中/无文本)") : asr;
      content = QStringLiteral("[语音] %1 <a href=\"implay:%2\">[%3]</a>")
                    .arg(label, fid, playing_file_id_ == fid ? QStringLiteral("播放中")
                                                             : QStringLiteral("播放"));
      break;
    }
    default:
      content = QStringLiteral("[未知类型]");
      break;
  }

  return QStringLiteral(
             "<div style='margin:6px 0'>"
             "<span style='color:#999;font-size:small'>[%1] %2</span><br>"
             "<span style='color:%3'>%4</span></div>")
      .arg(format_time(msg.create_time()), escape_html(sender_name(msg)), color, content);
}

void ChatWindow::render_all(bool keep_position) {
  auto* bar = view_->verticalScrollBar();
  const int old_value = bar->value();
  const int old_max = bar->maximum();

  // 图片消息：未下载的先触发下载（回调里再重绘）
  for (const auto& m : messages_) {
    if (m.type() == MESSAGE_TYPE_IMAGE && !m.file_id().empty()) {
      ensure_image(QString::fromStdString(m.file_id()));
    }
  }

  // 已下载图片注册为文档资源，HTML 里以 imimg:<file_id> 引用
  for (auto it = images_.constBegin(); it != images_.constEnd(); ++it) {
    view_->document()->addResource(QTextDocument::ImageResource,
                                   QUrl(QStringLiteral("imimg:") + it.key()), QVariant(*it));
  }

  QString html;
  for (const auto& m : messages_) html += bubble_html(m);
  view_->setHtml(html);

  if (keep_position) {
    // 预置历史/补下载：内容变高后把视口按增量下移，视觉上停在原消息
    bar->setValue(old_value + (bar->maximum() - old_max));
  } else {
    bar->setValue(bar->maximum());  // 新消息：滚到底
  }
}

void ChatWindow::on_anchor(const QUrl& url) {
  const QString scheme = url.scheme();
  const QString fid = url.path();
  if (fid.isEmpty()) return;
  if (scheme == QLatin1String("imsave") || scheme == QLatin1String("imgsave")) {
    save_attachment(fid, file_names_.value(fid, QStringLiteral("download.bin")));
  } else if (scheme == QLatin1String("implay")) {
    play_voice(fid);
  }
}

void ChatWindow::ensure_image(const QString& file_id) {
  if (file_id.isEmpty() || images_.contains(file_id) || image_pending_.contains(file_id)) return;
  image_pending_.insert(file_id);
  client_->download_file(
      file_id, [this, file_id](bool ok, const QString&, const QByteArray& data, const QString&) {
        image_pending_.remove(file_id);
        if (!ok || data.isEmpty()) return;
        QImage img;
        if (!img.loadFromData(data)) return;
        images_[file_id] = img;
        render_all(true);
      });
}

void ChatWindow::save_attachment(const QString& file_id, const QString& fallback_name) {
  set_status(QStringLiteral("下载中..."));
  client_->download_file(
      file_id, [this, fallback_name](bool ok, const QString& err, const QByteArray& data,
                                     const QString& server_name) {
        if (!ok) {
          set_status(QStringLiteral("下载失败: %1").arg(err));
          return;
        }
        QString name = server_name.isEmpty() ? fallback_name : server_name;
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存文件"),
                                                         QDir::homePath() + "/" + name);
        if (path.isEmpty()) {
          set_status(QStringLiteral("已取消"));
          return;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
          set_status(QStringLiteral("写入失败: %1").arg(path));
          return;
        }
        set_status(QStringLiteral("已保存: %1 (%2)").arg(path, human_size(data.size())));
      });
}

void ChatWindow::play_voice(const QString& file_id) {
  if (playing_file_id_ == file_id && player_ != nullptr) {  // 再次点击 = 停止
    player_->stop();
    playing_file_id_.clear();
    render_all(true);
    return;
  }
  const QString cached = QDir::tempPath() + "/im_voice_" + file_id + ".wav";
  auto start_play = [this, file_id, cached]() {
    if (player_ == nullptr) {
      player_ = new QMediaPlayer(this);
      audio_out_ = new QAudioOutput(this);
      player_->setAudioOutput(audio_out_);
      connect(player_, &QMediaPlayer::mediaStatusChanged, this,
              [this](QMediaPlayer::MediaStatus st) {
                if (st == QMediaPlayer::EndOfMedia || st == QMediaPlayer::InvalidMedia) {
                  playing_file_id_.clear();
                  render_all(true);
                }
              });
    }
    playing_file_id_ = file_id;
    player_->setSource(QUrl::fromLocalFile(cached));
    player_->play();
    render_all(true);
    set_status(QStringLiteral("播放语音"));
  };

  if (QFile::exists(cached)) {
    start_play();
    return;
  }
  set_status(QStringLiteral("语音下载中..."));
  client_->download_file(
      file_id,
      [this, cached, start_play](bool ok, const QString& err, const QByteArray& data,
                                 const QString&) {
        if (!ok) {
          set_status(QStringLiteral("语音下载失败: %1").arg(err));
          return;
        }
        QFile f(cached);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
          set_status(QStringLiteral("语音缓存写入失败"));
          return;
        }
        f.close();
        start_play();
      });
}

void ChatWindow::set_status(const QString& text) { status_->setText(text); }

void ChatWindow::set_busy(bool busy) {
  image_btn_->setEnabled(!busy);
  file_btn_->setEnabled(!busy);
  voice_btn_->setEnabled(!busy);
  send_btn_->setEnabled(!busy);
}

void ChatWindow::send_text() {
  const QString text = input_->text().trimmed();
  if (text.isEmpty()) return;
  set_busy(true);
  set_status(QStringLiteral("发送中..."));
  transmit(MESSAGE_TYPE_TEXT, text, QString(), QString(), 0);
  input_->clear();
}

void ChatWindow::send_attachment(im::MessageType type) {
  const bool image = type == MESSAGE_TYPE_IMAGE;
  const QString path = QFileDialog::getOpenFileName(
      this, image ? QStringLiteral("选择图片") : QStringLiteral("选择文件"), QDir::homePath(),
      image ? QStringLiteral("图片 (*.png *.jpg *.jpeg *.gif *.bmp *.webp)")
            : QStringLiteral("所有文件 (*.*)"));
  if (path.isEmpty()) return;

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    set_status(QStringLiteral("无法读取: %1").arg(path));
    return;
  }
  const QByteArray data = f.readAll();
  f.close();
  if (data.isEmpty()) {
    set_status(QStringLiteral("文件为空"));
    return;
  }

  const QString name = QFileInfo(path).fileName();
  set_busy(true);
  set_status(QStringLiteral("上传中 %1 (%2)...").arg(name, human_size(data.size())));
  client_->upload_file(
      name, data,
      [this, type, name, size = static_cast<int64_t>(data.size())](
          bool ok, const QString& err, const QString& file_id) {
        set_busy(false);
        if (!ok) {
          set_status(QStringLiteral("上传失败: %1").arg(err));
          return;
        }
        set_status(QStringLiteral("已上传，发送中..."));
        transmit(type, QString(), file_id, name, size);
      });
}

void ChatWindow::transmit(im::MessageType type, const QString& content, const QString& file_id,
                          const QString& file_name, int64_t file_size) {
  MsgTransmitReq req;
  req.set_chat_session_id(session_id_.toStdString());
  req.mutable_content()->set_type(type);
  req.mutable_content()->set_content(content.toStdString());
  req.mutable_content()->set_file_id(file_id.toStdString());
  req.mutable_content()->set_file_name(file_name.toStdString());
  req.mutable_content()->set_file_size(file_size);
  client_->call_p<MsgTransmitResp>(
      REQ_TYPE_TRANSMIT_MESSAGE, req,
      [this, type, content, file_id, file_name, file_size](bool ok, const QString& err,
                                                           const MsgTransmitResp& resp) {
        set_busy(false);
        if (!ok) {
          set_status(QStringLiteral("发送失败: %1").arg(err));
          return;
        }
        set_status(QStringLiteral("已发送"));
        // 推送在线时由 WS 广播回显（去重）；离线兜底本地补显，避免“发出去没反应”
        if (!client_->push_connected()) {
          im::MessageInfo msg;
          msg.set_message_id(resp.new_message_id());
          msg.set_chat_session_id(session_id_.toStdString());
          msg.set_sender_id(my_id_.toStdString());
          msg.set_type(type);
          msg.set_content(content.toStdString());
          msg.set_file_id(file_id.toStdString());
          msg.set_file_name(file_name.toStdString());
          msg.set_file_size(file_size);
          msg.set_create_time(resp.create_time());
          append_message(msg);
        }
      });
}

void ChatWindow::toggle_record() {
  if (recorder_->recording()) {
    int duration = 0;
    const QByteArray wav = recorder_->stop(&duration);
    record_tick_->stop();
    voice_btn_->setText(QStringLiteral("录音"));
    if (wav.isEmpty()) {
      set_status(QStringLiteral("录音失败"));
      return;
    }
    if (duration < kMinVoiceMs) {
      set_status(QStringLiteral("录音太短（%1）").arg(format_ms(duration)));
      return;
    }
    set_busy(true);
    set_status(QStringLiteral("语音上传中 (%1)...").arg(format_ms(duration)));
    client_->upload_file(
        QStringLiteral("voice_%1.wav").arg(QDateTime::currentSecsSinceEpoch()), wav,
        [this, size = static_cast<int64_t>(wav.size())](bool ok, const QString& err,
                                                        const QString& file_id) {
          set_busy(false);
          if (!ok) {
            set_status(QStringLiteral("语音上传失败: %1").arg(err));
            return;
          }
          set_status(QStringLiteral("语音已上传，发送中..."));
          transmit(MESSAGE_TYPE_VOICE, QString(), file_id, QStringLiteral("voice.wav"), size);
        });
    return;
  }

  QString err;
  if (!recorder_->start(&err)) {
    set_status(err);
    return;
  }
  record_started_ms_ = QDateTime::currentMSecsSinceEpoch();
  record_tick_->start();
  voice_btn_->setText(QStringLiteral("停止 0:00"));
  set_status(QStringLiteral("录音中，再次点击结束"));
}

void ChatWindow::tick_record() {
  if (!recorder_->recording()) return;
  const int elapsed = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - record_started_ms_);
  voice_btn_->setText(QStringLiteral("停止 %1").arg(format_ms(elapsed)));
  if (elapsed >= kMaxVoiceMs) toggle_record();  // 到上限自动收尾
}

void ChatWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    close();
    return;
  }
  QDialog::keyPressEvent(event);
}

}  // namespace im
