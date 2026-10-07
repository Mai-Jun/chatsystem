#include "ui/chat_window.hpp"

#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include "friend.pb.h"
#include "media/wav_recorder.hpp"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "protocol/gateway_client.hpp"
#include "ui/avatar.h"

namespace im {

namespace {

constexpr int kPageSize = 50;
constexpr int kMinVoiceMs = 500;     // 短于该时长视为误触，不上传
constexpr int kMaxVoiceMs = 60000;   // 上限 60s，到点自动停止
constexpr int kImageMax = 220;       // 图片气泡最长边
constexpr int kTextMaxWidth = 320;   // 文本气泡换行宽度
constexpr int kBubblePadX = 24;      // 气泡左右内边距（与 QSS 一致的估算值）
constexpr int kBubblePadY = 16;

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

// 文本气泡的固定尺寸：按换行排版算出紧贴内容的宽高（短消息不撑满整行）
QSize text_bubble_size(const QString& text) {
  QFontMetrics fm = QLabel().fontMetrics();
  const QRect r = fm.boundingRect(QRect(0, 0, kTextMaxWidth, 10000),
                                  Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
  return QSize(r.width() + kBubblePadX, r.height() + kBubblePadY);
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
  resize(640, 700);

  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);

  // 顶栏：标题 + 历史分页
  auto* top = new QHBoxLayout();
  top->setContentsMargins(12, 10, 12, 10);
  more_btn_ = new QPushButton(QStringLiteral("加载更多"), this);
  more_btn_->setEnabled(false);
  status_ = new QLabel(QStringLiteral("历史加载中..."), this);
  status_->setObjectName(QStringLiteral("hint"));
  top->addWidget(more_btn_);
  top->addWidget(status_, 1);
  v->addLayout(top);

  auto* line = new QFrame(this);
  line->setFrameShape(QFrame::HLine);
  line->setObjectName(QStringLiteral("card"));
  v->addWidget(line);

  // 消息列表
  view_ = new QListWidget(this);
  view_->setObjectName(QStringLiteral("msgList"));
  view_->setSelectionMode(QAbstractItemView::NoSelection);
  view_->setFocusPolicy(Qt::NoFocus);
  view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  view_->setUniformItemSizes(false);
  v->addWidget(view_, 1);

  // 底部输入区
  auto* input_panel = new QWidget(this);
  input_panel->setStyleSheet(QStringLiteral("background:#FFFFFF;"));
  auto* iv = new QVBoxLayout(input_panel);
  iv->setContentsMargins(12, 8, 12, 10);
  iv->setSpacing(6);
  auto* btn_row = new QHBoxLayout();
  image_btn_ = new QPushButton(QStringLiteral("图片"), input_panel);
  file_btn_ = new QPushButton(QStringLiteral("文件"), input_panel);
  voice_btn_ = new QPushButton(QStringLiteral("录音"), input_panel);
  for (auto* b : {image_btn_, file_btn_, voice_btn_}) b->setObjectName(QStringLiteral("toolBtn"));
  btn_row->addWidget(image_btn_);
  btn_row->addWidget(file_btn_);
  btn_row->addWidget(voice_btn_);
  btn_row->addStretch(1);
  iv->addLayout(btn_row);

  auto* send_row = new QHBoxLayout();
  send_row->setSpacing(8);
  input_ = new QLineEdit(input_panel);
  input_->setPlaceholderText(QStringLiteral("输入消息，回车发送"));
  send_btn_ = new QPushButton(QStringLiteral("发送"), input_panel);
  send_btn_->setObjectName(QStringLiteral("primary"));
  send_btn_->setFixedWidth(84);
  send_row->addWidget(input_, 1);
  send_row->addWidget(send_btn_);
  iv->addLayout(send_row);
  v->addWidget(input_panel);

  connect(send_btn_, &QPushButton::clicked, this, &ChatWindow::send_text);
  connect(input_, &QLineEdit::returnPressed, this, &ChatWindow::send_text);
  connect(image_btn_, &QPushButton::clicked, this,
          [this]() { send_attachment(MESSAGE_TYPE_IMAGE); });
  connect(file_btn_, &QPushButton::clicked, this,
          [this]() { send_attachment(MESSAGE_TYPE_FILE); });
  connect(voice_btn_, &QPushButton::clicked, this, &ChatWindow::toggle_record);
  connect(more_btn_, &QPushButton::clicked, this, [this]() { load_history(oldest_ts_); });

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
        member_names_.clear();
        for (const auto& m : resp.members()) {
          member_names_[QString::fromStdString(m.user_id())] =
              QString::fromStdString(m.nickname().empty() ? m.user_id() : m.nickname());
        }
        group_chat_ = resp.members_size() > 2;
        // 群聊时行内要显示发送者昵称：整表重建（仅此一次；insert_row 会重填 rows_）
        auto* bar = view_->verticalScrollBar();
        const int old_value = bar->value();
        const int old_max = bar->maximum();
        rows_.clear();
        for (int i = view_->count() - 1; i >= 0; --i) delete view_->item(i);  // 连同 item widget
        for (const auto& m : messages_) insert_row(m, view_->count());
        bar->setValue(old_value + (bar->maximum() - old_max));
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
  // 否则与分页边界同秒的消息会被永久跳过；重复项由 message_id 去重
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
        auto* bar = view_->verticalScrollBar();
        const int old_value = bar->value();
        const int old_max = bar->maximum();

        // 先过滤重复（rows_ 里已有的 message_id），再插行、再合并 messages_
        QVector<im::MessageInfo> page;
        for (const auto& m : resp.messages()) {
          if (!rows_.contains(QString::fromStdString(m.message_id()))) page.append(m);
        }
        int index = 0;
        for (const auto& m : page) {
          if (prepend) {
            insert_row(m, index++);  // 服务端升序返回整页，依次插在表头即升序衔接
          } else {
            insert_row(m, view_->count());
          }
        }
        if (prepend) {
          QVector<im::MessageInfo> merged;
          merged.reserve(messages_.size() + page.size());
          for (const auto& m : page) merged.append(m);
          for (const auto& m : messages_) merged.append(m);
          messages_ = merged;
        } else {
          for (const auto& m : page) messages_.append(m);
        }

        if (resp.messages_size() < kPageSize) no_more_history_ = true;
        if (!messages_.isEmpty()) oldest_ts_ = messages_.first().create_time();
        more_btn_->setEnabled(!no_more_history_ && !messages_.isEmpty());
        set_status(prepend && page.isEmpty() ? QStringLiteral("没有更多历史了")
                                            : QStringLiteral("%1 条消息").arg(messages_.size()));
        if (prepend) {
          bar->setValue(old_value + (bar->maximum() - old_max));  // 视口停在原消息
        } else {
          view_->scrollToBottom();
        }
      });
}

const im::MessageInfo* ChatWindow::find_message(const QString& message_id) const {
  for (const auto& m : messages_) {
    if (QString::fromStdString(m.message_id()) == message_id) return &m;
  }
  return nullptr;
}

void ChatWindow::on_message_push(const QByteArray& body) {
  im::MessageInfo msg;
  if (!msg.ParseFromArray(body.constData(), body.size())) return;
  if (QString::fromStdString(msg.chat_session_id()) != session_id_) return;
  on_push_message(msg);
  if (isActiveWindow()) emit seen(session_id_);
}

void ChatWindow::on_push_message(const im::MessageInfo& msg) {
  const QString mid = QString::fromStdString(msg.message_id());
  if (rows_.contains(mid)) return;  // 广播回来的自己的消息/重复推送
  messages_.append(msg);
  insert_row(msg, view_->count());
  view_->scrollToBottom();
}

QString ChatWindow::sender_display_name(const im::MessageInfo& msg) const {
  const QString uid = QString::fromStdString(msg.sender_id());
  if (uid == my_id_) return QStringLiteral("我");
  return member_names_.value(uid, uid);
}

QPixmap make_chat_avatar(const QString& name) { return avatar_pixmap(name, 36); }

QWidget* ChatWindow::build_row(const im::MessageInfo& msg) {
  const bool mine = QString::fromStdString(msg.sender_id()) == my_id_;
  auto* w = new QWidget(this);
  auto* v = new QVBoxLayout(w);
  v->setContentsMargins(12, 5, 12, 5);
  v->setSpacing(3);

  auto* time_lbl = new QLabel(format_time(msg.create_time()), w);
  time_lbl->setObjectName(QStringLiteral("msgTime"));
  time_lbl->setAlignment(Qt::AlignCenter);
  v->addWidget(time_lbl);

  auto* line = new QHBoxLayout();
  line->setSpacing(8);

  const QString sender_name = sender_display_name(msg);
  auto* avatar = new QLabel(w);
  avatar->setPixmap(make_chat_avatar(sender_name));
  avatar->setFixedSize(36, 36);

  QWidget* bubble = build_bubble(msg, mine);

  if (mine) {
    line->addStretch(1);
    line->addWidget(bubble);
    line->addWidget(avatar, 0, Qt::AlignTop);
  } else {
    line->addWidget(avatar, 0, Qt::AlignTop);
    auto* col = new QVBoxLayout();
    col->setSpacing(1);
    if (group_chat_) {
      auto* name_lbl = new QLabel(sender_name, w);
      name_lbl->setObjectName(QStringLiteral("msgSender"));
      col->addWidget(name_lbl);
    }
    col->addWidget(bubble);
    line->addLayout(col);
    line->addStretch(1);
  }
  v->addLayout(line);
  return w;
}

QWidget* ChatWindow::build_bubble(const im::MessageInfo& msg, bool mine) {
  const QString fid = QString::fromStdString(msg.file_id());
  const QString fname = QString::fromStdString(msg.file_name());

  switch (msg.type()) {
    case MESSAGE_TYPE_TEXT: {
      const QString text = QString::fromStdString(msg.content());
      auto* lbl = new QLabel(text, this);
      lbl->setObjectName(mine ? QStringLiteral("bubbleSelf") : QStringLiteral("bubblePeer"));
      lbl->setWordWrap(true);
      lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
      const QSize s = text_bubble_size(text);
      lbl->setFixedSize(s);
      return lbl;
    }
    case MESSAGE_TYPE_IMAGE: {
      // 图片直接展示（微信样式，无气泡底），下方给一个「保存」链接按钮
      auto* col = new QWidget(this);
      auto* cv = new QVBoxLayout(col);
      cv->setContentsMargins(0, 0, 0, 0);
      cv->setSpacing(0);
      if (images_.contains(fid)) {
        QImage img = images_.value(fid);
        QImage scaled = img.scaled(kImageMax, kImageMax, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
        auto* img_lbl = new QLabel(col);
        img_lbl->setPixmap(QPixmap::fromImage(scaled));
        img_lbl->setFixedSize(scaled.size());
        cv->addWidget(img_lbl);
      } else {
        auto* placeholder = new QLabel(QStringLiteral("[ 图片加载中 ]"), col);
        placeholder->setObjectName(mine ? QStringLiteral("bubbleSelf")
                                        : QStringLiteral("bubblePeer"));
        placeholder->setFixedSize(180, 90);
        placeholder->setAlignment(Qt::AlignCenter);
        cv->addWidget(placeholder);
        ensure_image(fid);
      }
      auto* save_btn = new QPushButton(QStringLiteral("保存"), col);
      save_btn->setObjectName(QStringLiteral("linkBtn"));
      cv->addWidget(save_btn, 0, Qt::AlignLeft);
      connect(save_btn, &QPushButton::clicked, this,
              [this, fid, fname]() { save_attachment(fid, fname); });
      return col;
    }
    case MESSAGE_TYPE_FILE: {
      auto* frame = new QFrame(this);
      frame->setObjectName(mine ? QStringLiteral("bubbleSelf") : QStringLiteral("bubblePeer"));
      auto* h = new QHBoxLayout(frame);
      h->setContentsMargins(11, 8, 11, 8);
      h->setSpacing(8);
      auto* icon = new QLabel(QStringLiteral("📄"), frame);
      auto* col = new QVBoxLayout();
      col->setSpacing(1);
      auto* name_lbl = new QLabel(fname.isEmpty() ? QStringLiteral("文件") : fname, frame);
      name_lbl->setObjectName(QStringLiteral("fileName"));
      auto* size_lbl = new QLabel(human_size(msg.file_size()), frame);
      size_lbl->setObjectName(QStringLiteral("fileSize"));
      col->addWidget(name_lbl);
      col->addWidget(size_lbl);
      auto* save_btn = new QPushButton(QStringLiteral("另存"), frame);
      save_btn->setObjectName(QStringLiteral("linkBtn"));
      h->addWidget(icon);
      h->addLayout(col, 1);
      h->addWidget(save_btn);
      connect(save_btn, &QPushButton::clicked, this,
              [this, fid, fname]() { save_attachment(fid, fname); });
      frame->setMinimumWidth(240);
      return frame;
    }
    case MESSAGE_TYPE_VOICE: {
      const QString asr = QString::fromStdString(msg.asr_text());
      auto* frame = new QFrame(this);
      frame->setObjectName(mine ? QStringLiteral("bubbleSelf") : QStringLiteral("bubblePeer"));
      auto* h = new QHBoxLayout(frame);
      h->setContentsMargins(11, 8, 11, 8);
      h->setSpacing(8);
      auto* play_btn = new QPushButton(
          playing_file_id_ == fid ? QStringLiteral("⏸ 停止") : QStringLiteral("▶ 播放"), frame);
      play_btn->setObjectName(QStringLiteral("linkBtn"));
      auto* asr_lbl = new QLabel(asr.isEmpty() ? QStringLiteral("(转写中/无文本)") : asr, frame);
      asr_lbl->setWordWrap(true);
      asr_lbl->setMaximumWidth(240);
      h->addWidget(play_btn);
      h->addWidget(asr_lbl, 1);
      connect(play_btn, &QPushButton::clicked, this, [this, fid]() { play_voice(fid); });
      return frame;
    }
    default: {
      auto* lbl = new QLabel(QStringLiteral("[未知类型]"), this);
      lbl->setObjectName(mine ? QStringLiteral("bubbleSelf") : QStringLiteral("bubblePeer"));
      lbl->setFixedSize(90, 38);
      lbl->setAlignment(Qt::AlignCenter);
      return lbl;
    }
  }
}

void ChatWindow::insert_row(const im::MessageInfo& msg, int index) {
  const QString mid = QString::fromStdString(msg.message_id());
  auto* item = new QListWidgetItem;
  QWidget* w = build_row(msg);
  item->setSizeHint(w->sizeHint());
  view_->insertItem(index, item);
  view_->setItemWidget(item, w);
  rows_[mid] = item;
  if (!msg.file_id().empty()) {
    file_names_[QString::fromStdString(msg.file_id())] = QString::fromStdString(msg.file_name());
  }
  if (msg.type() == MESSAGE_TYPE_IMAGE && !msg.file_id().empty()) {
    ensure_image(QString::fromStdString(msg.file_id()));
  }
}

void ChatWindow::rebuild_row(const QString& message_id) {
  auto it = rows_.constFind(message_id);
  if (it == rows_.constEnd()) return;
  QListWidgetItem* item = it.value();
  const im::MessageInfo* msg = find_message(message_id);
  if (msg == nullptr) return;
  // takeItemWidget 会被 setItemWidget 内部清理，这里直接换新
  QWidget* w = build_row(*msg);
  view_->removeItemWidget(item);
  view_->setItemWidget(item, w);
  item->setSizeHint(w->sizeHint());
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
        // 就地刷新含该图的消息行
        for (const auto& m : messages_) {
          if (m.type() == MESSAGE_TYPE_IMAGE && QString::fromStdString(m.file_id()) == file_id) {
            rebuild_row(QString::fromStdString(m.message_id()));
          }
        }
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
    for (const auto& m : messages_) {
      if (m.type() == MESSAGE_TYPE_VOICE && QString::fromStdString(m.file_id()) == file_id) {
        rebuild_row(QString::fromStdString(m.message_id()));
      }
    }
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
                  const QString fid = playing_file_id_;
                  playing_file_id_.clear();
                  for (const auto& m : messages_) {
                    if (m.type() == MESSAGE_TYPE_VOICE &&
                        QString::fromStdString(m.file_id()) == fid) {
                      rebuild_row(QString::fromStdString(m.message_id()));
                    }
                  }
                }
              });
    }
    playing_file_id_ = file_id;
    player_->setSource(QUrl::fromLocalFile(cached));
    player_->play();
    for (const auto& m : messages_) {
      if (m.type() == MESSAGE_TYPE_VOICE && QString::fromStdString(m.file_id()) == file_id) {
        rebuild_row(QString::fromStdString(m.message_id()));
      }
    }
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
          on_push_message(msg);
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
