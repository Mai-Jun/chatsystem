#include "ui/main_window.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "chat_window.hpp"
#include "avatar.h"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "protocol/gateway_client.hpp"
#include "user.pb.h"

namespace im {

namespace {

// ---------------- 搜索用户并加好友 ----------------
class SearchDialog : public QDialog {
 public:
  SearchDialog(GatewayClient* client, const QString& my_id, QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("搜索用户"));
    resize(420, 380);
    auto* v = new QVBoxLayout(this);
    auto* row = new QHBoxLayout();
    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(QStringLiteral("手机号（精确）或昵称（模糊）"));
    auto* btn = new QPushButton(QStringLiteral("搜索"), this);
    row->addWidget(edit_, 1);
    row->addWidget(btn);
    v->addLayout(row);
    list_ = new QListWidget(this);
    v->addWidget(list_, 1);
    status_ = new QLabel(this);
    v->addWidget(status_);

    connect(btn, &QPushButton::clicked, this, [this, client, my_id]() {
      const QString kw = edit_->text().trimmed();
      if (kw.isEmpty()) return;
      SearchUserReq req;
      req.set_user_id(my_id.toStdString());
      req.set_keyword(kw.toStdString());
      client->call_p<SearchUserResp>(
          REQ_TYPE_SEARCH_USER, req, [this](bool ok, const QString& err,
                                            const SearchUserResp& resp) {
            list_->clear();
            if (!ok) {
              status_->setText(err);
              return;
            }
            for (const auto& u : resp.result()) {
              auto* item = new QListWidgetItem(
                  QString("%1  (%2)").arg(QString::fromStdString(u.nickname()),
                                         QString::fromStdString(u.phone())),
                  list_);
              item->setData(Qt::UserRole, QString::fromStdString(u.user_id()));
              item->setData(Qt::UserRole + 1, QString::fromStdString(u.nickname()));
            }
            status_->setText(QString("共 %1 条结果，双击发送好友申请").arg(resp.result_size()));
          });
    });
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this, client, my_id](QListWidgetItem* item) {
              SendFriendApplyReq req;
              req.set_user_id(my_id.toStdString());
              req.set_peer_id(item->data(Qt::UserRole).toString().toStdString());
              req.set_apply_note("我是 " + my_id.toStdString());
              client->call_p<SendFriendApplyResp>(
                  REQ_TYPE_SEND_FRIEND_APPLY, req,
                  [this](bool ok, const QString& err, const SendFriendApplyResp&) {
                    status_->setText(ok ? QStringLiteral("申请已发送，等待对方处理")
                                        : QStringLiteral("发送失败: %1").arg(err));
                  });
            });
  }

 private:
  QLineEdit* edit_ = nullptr;
  QListWidget* list_ = nullptr;
  QLabel* status_ = nullptr;
};

// ---------------- 待处理事件（好友申请 + 群聊事件） ----------------
class PendingDialog : public QDialog {
 public:
  PendingDialog(GatewayClient* client, const QString& my_id, QWidget* parent,
                std::function<void()> on_changed)
      : QDialog(parent), client_(client), my_id_(my_id), on_changed_(std::move(on_changed)) {
    setWindowTitle(QStringLiteral("待处理事件"));
    resize(460, 420);
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(QStringLiteral("好友申请（双击同意，按 D 拒绝）"), this));
    list_ = new QListWidget(this);
    v->addWidget(list_, 2);
    v->addWidget(new QLabel(QStringLiteral("群聊事件（已被拉入以下群聊）"), this));
    group_list_ = new QListWidget(this);
    group_list_->setSelectionMode(QAbstractItemView::NoSelection);
    v->addWidget(group_list_, 1);
    status_ = new QLabel(this);
    v->addWidget(status_);
    auto* refresh_btn = new QPushButton(QStringLiteral("刷新"), this);
    v->addWidget(refresh_btn);
    connect(refresh_btn, &QPushButton::clicked, this, [this]() { load(); });
    load();
  }

 private:
  void load() {
    GetPendingEventsReq req;
    client_->call_p<GetPendingEventsResp>(
        REQ_TYPE_GET_PENDING_EVENTS, req,
        [this](bool ok, const QString& err, const GetPendingEventsResp& resp) {
          list_->clear();
          group_list_->clear();
          if (!ok) {
            status_->setText(err);
            return;
          }
          for (const auto& e : resp.friend_apply_events()) {
            auto* item = new QListWidgetItem(
                QString("%1  请求加你为好友   [双击同意 / 选中后按 D 拒绝]")
                    .arg(QString::fromStdString(e.apply_note().empty() ? e.user_id()
                                                                       : e.apply_note())),
                list_);
            item->setData(Qt::UserRole, QString::fromStdString(e.apply_id()));
          }
          for (const auto& e : resp.group_events()) {
            new QListWidgetItem(
                QString("你已被拉入群聊 %1").arg(QString::fromStdString(e.group_session_id())),
                group_list_);
          }
          status_->setText(QString("好友申请 %1 条").arg(resp.friend_apply_events_size()));
        });
  }

  void process(QListWidgetItem* item, im::ApplyStatus status) {
    ProcessFriendApplyReq req;
    req.set_user_id(my_id_.toStdString());
    req.set_apply_id(item->data(Qt::UserRole).toString().toStdString());
    req.set_status(status);
    client_->call_p<ProcessFriendApplyResp>(
        REQ_TYPE_PROCESS_FRIEND_APPLY, req,
        [this, status](bool ok, const QString& err, const ProcessFriendApplyResp&) {
          if (!ok) {
            status_->setText(err);
            return;
          }
          status_->setText(status == APPLY_STATUS_AGREE ? QStringLiteral("已同意")
                                                        : QStringLiteral("已拒绝"));
          load();
          if (on_changed_) on_changed_();
        });
  }

  GatewayClient* client_;
  QString my_id_;
  std::function<void()> on_changed_;
  QListWidget* list_ = nullptr;
  QListWidget* group_list_ = nullptr;
  QLabel* status_ = nullptr;

  // 对象内 lambda 转发键盘事件（D = 拒绝）
 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_D && list_->currentItem() != nullptr) {
      process(list_->currentItem(), APPLY_STATUS_REJECT);
      return;
    }
    QDialog::keyPressEvent(event);
  }

 public:
  // 双击同意（构造函数内 connect 用到，声明在此便于调用）
  void accept_current() {
    if (list_->currentItem() != nullptr) process(list_->currentItem(), APPLY_STATUS_AGREE);
  }
  void bind_double_click() {
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { accept_current(); });
  }
};

// ---------------- 创建群聊 ----------------
class GroupDialog : public QDialog {
 public:
  GroupDialog(GatewayClient* client, const QString& my_id, QWidget* parent,
              std::function<void()> on_created)
      : QDialog(parent), client_(client), my_id_(my_id), on_created_(std::move(on_created)) {
    setWindowTitle(QStringLiteral("创建群聊"));
    resize(380, 420);
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(QStringLiteral("群名称"), this));
    name_ = new QLineEdit(this);
    v->addWidget(name_);
    v->addWidget(new QLabel(QStringLiteral("选择成员（可多选）"), this));
    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::MultiSelection);
    v->addWidget(list_, 1);
    status_ = new QLabel(this);
    v->addWidget(status_);
    auto* create_btn = new QPushButton(QStringLiteral("创建"), this);
    v->addWidget(create_btn);
    connect(create_btn, &QPushButton::clicked, this, [this]() {
      const QString name = name_->text().trimmed();
      if (name.isEmpty()) {
        status_->setText(QStringLiteral("请输入群名称"));
        return;
      }
      CreateGroupSessionReq req;
      req.set_user_id(my_id_.toStdString());
      req.set_group_name(name.toStdString());
      for (auto* item : list_->selectedItems()) {
        req.add_member_ids(item->data(Qt::UserRole).toString().toStdString());
      }
      client_->call_p<CreateGroupSessionResp>(
          REQ_TYPE_CREATE_GROUP_SESSION, req,
          [this](bool ok, const QString& err, const CreateGroupSessionResp&) {
            if (!ok) {
              status_->setText(err);
              return;
            }
            if (on_created_) on_created_();
            accept();
          });
    });
    load_friends();
  }

 private:
  void load_friends() {
    GetFriendListReq req;
    client_->call_p<GetFriendListResp>(
        REQ_TYPE_GET_FRIEND_LIST, req,
        [this](bool ok, const QString& err, const GetFriendListResp& resp) {
          if (!ok) {
            status_->setText(err);
            return;
          }
          for (const auto& u : resp.friend_list()) {
            auto* item = new QListWidgetItem(QString::fromStdString(u.nickname()), list_);
            item->setData(Qt::UserRole, QString::fromStdString(u.user_id()));
          }
        });
  }

  GatewayClient* client_;
  QString my_id_;
  std::function<void()> on_created_;
  QLineEdit* name_ = nullptr;
  QListWidget* list_ = nullptr;
  QLabel* status_ = nullptr;
};

}  // namespace

MainWindow::MainWindow(GatewayClient* client, im::UserInfo me, QWidget* parent)
    : QMainWindow(parent), client_(client), me_(std::move(me)) {
  setWindowTitle(QString("IM - %1").arg(QString::fromStdString(me_.nickname())));
  resize(760, 520);

  auto* central = new QWidget(this);
  auto* v = new QVBoxLayout(central);
  auto* btn_row = new QHBoxLayout();
  auto* search_btn = new QPushButton(QStringLiteral("搜索/加好友"), central);
  auto* group_btn = new QPushButton(QStringLiteral("创建群聊"), central);
  pending_btn_ = new QPushButton(QStringLiteral("待处理事件(0)"), central);
  auto* refresh_btn = new QPushButton(QStringLiteral("刷新"), central);
  auto* logout_btn = new QPushButton(QStringLiteral("退出登录"), central);
  btn_row->addWidget(search_btn);
  btn_row->addWidget(group_btn);
  btn_row->addWidget(pending_btn_);
  btn_row->addWidget(refresh_btn);
  btn_row->addStretch(1);
  btn_row->addWidget(logout_btn);
  v->addLayout(btn_row);

  tabs_ = new QTabWidget(central);
  session_list_ = new QListWidget(tabs_);
  friend_list_ = new QListWidget(tabs_);
  tabs_->addTab(session_list_, QStringLiteral("会话"));
  tabs_->addTab(friend_list_, QStringLiteral("好友"));
  v->addWidget(tabs_, 1);
  setCentralWidget(central);

  statusBar()->addWidget(status_ = new QLabel(QStringLiteral("推送连接中..."), this));

  connect(session_list_, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem* item) { open_session(item); });
  connect(friend_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    // 双击好友 → 打开与该好友的单聊会话（按对端 user_id 匹配）
    const QString uid = item->data(Qt::UserRole).toString();
    for (auto it = session_peers_.constBegin(); it != session_peers_.constEnd(); ++it) {
      if (it.value() != uid) continue;
      for (int i = 0; i < session_list_->count(); ++i) {
        auto* row = session_list_->item(i);
        if (row->data(Qt::UserRole).toString() == it.key()) {
          open_session(row);
          return;
        }
      }
    }
    QMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("请先通过“搜索/加好友”添加对方为好友"));
  });

  connect(search_btn, &QPushButton::clicked, this, [this]() {
    auto* d = new SearchDialog(client_, QString::fromStdString(me_.user_id()), this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &QDialog::finished, this, [this]() { refresh_sessions(); refresh_friends(); });
    d->show();
  });
  connect(group_btn, &QPushButton::clicked, this, [this]() {
    auto* d = new GroupDialog(client_, QString::fromStdString(me_.user_id()), this,
                              [this]() { refresh_sessions(); });
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->show();
  });
  connect(pending_btn_, &QPushButton::clicked, this, [this]() {
    auto* d =
        new PendingDialog(client_, QString::fromStdString(me_.user_id()), this,
                          [this]() { refresh_sessions(); refresh_pending_count(); });
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->bind_double_click();
    connect(d, &QDialog::finished, this, [this]() { refresh_pending_count(); });
    d->show();
  });
  connect(refresh_btn, &QPushButton::clicked, this, [this]() {
    refresh_sessions();
    refresh_friends();
    refresh_pending_count();
  });
  connect(logout_btn, &QPushButton::clicked, this, [this]() {
    UserLogoutReq req;
    client_->call_p<UserLogoutResp>(REQ_TYPE_LOGOUT, req,
                                    [this](bool, const QString&, const UserLogoutResp&) {});
    client_->clear_token();
    qApp->quit();  // 简化：退出程序（重新打开进入登录页）
  });

  connect_push();
  refresh_sessions();
  refresh_friends();
  refresh_pending_count();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
  for (auto* w : chat_windows_) w->close();
  event->accept();
}

void MainWindow::connect_push() {
  connect(client_, &GatewayClient::pushConnected, this,
          [this](bool ok, const QString& errmsg) {
            status_->setText(ok ? QStringLiteral("推送: 已连接")
                                : QStringLiteral("推送: %1").arg(errmsg));
          });
  connect(client_, &GatewayClient::pushReceived, this, &MainWindow::on_push);
  client_->connect_push();
}

void MainWindow::on_push(int type, const QByteArray& body) {
  if (type == PUSH_TYPE_NEW_MESSAGE) {
    im::MessageInfo msg;
    if (!msg.ParseFromArray(body.constData(), body.size())) return;
    const QString sid = QString::fromStdString(msg.chat_session_id());
    const bool from_me = QString::fromStdString(msg.sender_id()) ==
                         QString::fromStdString(me_.user_id());
    ChatWindow* w = chat_windows_.value(sid);
    // 先判聚焦再转发：聊天窗收到消息会 emit seen 清零未读，顺序反了会立刻 +1
    const bool focused = w != nullptr && w->isActiveWindow();
    if (w != nullptr) w->on_message_push(body);
    if (!from_me && !focused) {
      unread_[sid] = unread_.value(sid, 0) + 1;
      update_session_item(sid);
    }
    refresh_sessions();
  } else if (type == PUSH_TYPE_FRIEND_APPLY || type == PUSH_TYPE_GROUP_EVENT) {
    refresh_pending_count();
    if (type == PUSH_TYPE_FRIEND_APPLY) refresh_sessions();
  }
}

QString MainWindow::display_name_for(const im::ChatSessionInfo& s) {
  const QString sid = QString::fromStdString(s.chat_session_id());
  const QString cached = session_names_.value(sid);
  if (!cached.isEmpty()) return cached;
  if (s.type() == SESSION_TYPE_GROUP) {
    const QString name = QString::fromStdString(s.session_name());
    session_names_[sid] = name;
    return name;
  }
  // 单聊：会话名为空，取对方昵称 + 头像（GetSessionMember）
  GetSessionMemberReq req;
  req.set_chat_session_id(s.chat_session_id());
  client_->call_p<GetSessionMemberResp>(
      REQ_TYPE_GET_SESSION_MEMBER, req,
      [this, sid](bool ok, const QString&, const GetSessionMemberResp& resp) {
        if (!ok) return;
        for (const auto& m : resp.members()) {
          if (QString::fromStdString(m.user_id()) == QString::fromStdString(me_.user_id())) {
            continue;
          }
          session_peers_[sid] = QString::fromStdString(m.user_id());
          session_names_[sid] = QString::fromStdString(
              m.nickname().empty() ? m.user_id() : m.nickname());
          session_avatars_[sid] = QString::fromStdString(m.avatar_file_id());
          ensure_avatar(session_avatars_[sid]);
          update_session_item(sid);
          apply_avatars();
          break;
        }
      });
  return sid;  // 解析完成前先以 session_id 展示
}

void MainWindow::refresh_sessions() {
  GetChatSessionListReq req;
  req.set_user_id(me_.user_id());
  client_->call_p<GetChatSessionListResp>(
      REQ_TYPE_GET_SESSION_LIST, req,
      [this](bool ok, const QString& err, const GetChatSessionListResp& resp) {
        if (!ok) {
          statusBar()->showMessage(err, 3000);
          return;
        }
        sessions_.clear();
        session_items_.clear();
        session_list_->clear();
        for (const auto& s : resp.session_list()) {
          const QString sid = QString::fromStdString(s.chat_session_id());
          sessions_[sid] = s;
          display_name_for(s);  // 异步补全显示名/头像，先建项
          auto* item = new QListWidgetItem(session_list_);
          const QString name = session_names_.value(
              sid, s.type() == SESSION_TYPE_GROUP
                        ? QString::fromStdString(s.session_name())
                        : sid);
          item->setText(name);
          item->setIcon(avatar_pixmap(name, 28));  // 默认首字头像，下载到真头像后覆盖
          item->setData(Qt::UserRole, sid);
          session_items_[sid] = item;
          update_session_item(sid);
          apply_avatars();
        }
      });
}

void MainWindow::update_session_item(const QString& session_id) {
  auto* item = session_items_.value(session_id);
  if (item == nullptr) return;
  const QString name = session_names_.value(
      session_id, sessions_.value(session_id).type() == SESSION_TYPE_GROUP
                        ? QString::fromStdString(sessions_.value(session_id).session_name())
                        : session_id);
  const int unread = unread_.value(session_id, 0);
  item->setText(unread > 0 ? QString("%1  (%2)").arg(name).arg(unread) : name);
}

void MainWindow::on_session_seen(const QString& session_id) {
  if (unread_.value(session_id, 0) == 0) return;
  unread_[session_id] = 0;
  update_session_item(session_id);
}

void MainWindow::ensure_avatar(const QString& file_id) {
  if (file_id.isEmpty() || avatar_icons_.contains(file_id) || avatar_pending_.contains(file_id)) {
    return;
  }
  avatar_pending_.insert(file_id);
  client_->download_file(
      file_id, [this, file_id](bool ok, const QString&, const QByteArray& data, const QString&) {
        avatar_pending_.remove(file_id);
        if (!ok || data.isEmpty()) return;
        QPixmap pm;
        if (!pm.loadFromData(data)) return;
        avatar_icons_[file_id] = QIcon(
            pm.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        apply_avatars();
      });
}

void MainWindow::apply_avatars() {
  for (auto it = session_avatars_.constBegin(); it != session_avatars_.constEnd(); ++it) {
    auto* item = session_items_.value(it.key());
    if (item == nullptr) continue;
    const auto icon = avatar_icons_.constFind(it.value());
    if (icon != avatar_icons_.constEnd()) item->setIcon(*icon);
  }
  for (int i = 0; i < friend_list_->count(); ++i) {
    auto* item = friend_list_->item(i);
    const QString fid = item->data(Qt::UserRole + 1).toString();
    const auto icon = avatar_icons_.constFind(fid);
    if (icon != avatar_icons_.constEnd()) item->setIcon(*icon);
  }
}

void MainWindow::refresh_friends() {
  GetFriendListReq req;
  client_->call_p<GetFriendListResp>(
      REQ_TYPE_GET_FRIEND_LIST, req,
      [this](bool ok, const QString&, const GetFriendListResp& resp) {
        if (!ok) return;
        friends_.clear();
        friend_list_->clear();
        for (const auto& u : resp.friend_list()) {
          friends_[QString::fromStdString(u.user_id())] = u;
          auto* item =
              new QListWidgetItem(QString::fromStdString(u.nickname()), friend_list_);
          item->setIcon(avatar_pixmap(QString::fromStdString(u.nickname()), 28));
          item->setData(Qt::UserRole, QString::fromStdString(u.user_id()));
          item->setData(Qt::UserRole + 1, QString::fromStdString(u.avatar_file_id()));
          ensure_avatar(QString::fromStdString(u.avatar_file_id()));
        }
        apply_avatars();
      });
}

void MainWindow::refresh_pending_count() {
  GetPendingEventsReq req;
  client_->call_p<GetPendingEventsResp>(
      REQ_TYPE_GET_PENDING_EVENTS, req,
      [this](bool ok, const QString&, const GetPendingEventsResp& resp) {
        if (!ok) return;
        const int n = resp.friend_apply_events_size() + resp.group_events_size();
        pending_btn_->setText(QString("待处理事件(%1)").arg(n));
      });
}

ChatWindow* MainWindow::chat_window_for(const QString& session_id, const QString& display_name) {
  if (auto* w = chat_windows_.value(session_id)) return w;
  auto* w = new ChatWindow(client_, session_id, display_name,
                           QString::fromStdString(me_.user_id()));
  connect(w, &ChatWindow::seen, this, &MainWindow::on_session_seen);
  connect(w, &QObject::destroyed, this,
          [this, session_id]() { chat_windows_.remove(session_id); });
  chat_windows_[session_id] = w;
  return w;
}

void MainWindow::open_session(QListWidgetItem* item) {
  const QString sid = item->data(Qt::UserRole).toString();
  if (sid.isEmpty()) return;
  ChatWindow* w = chat_window_for(sid, session_names_.value(sid, item->text()));
  w->show();
  w->raise();
  w->activateWindow();
  on_session_seen(sid);  // 打开即已读
}

}  // namespace im
