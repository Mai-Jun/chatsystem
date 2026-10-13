#include "ui/main_window.hpp"

#include <QAction>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>

#include "avatar.h"
#include "chat_window.hpp"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "protocol/gateway_client.hpp"
#include "user.pb.h"

namespace im {

namespace {

// 列表行数据角色：session_id/uid、搜索用名字
const int kRowIdRole = Qt::UserRole;
const int kSearchNameRole = Qt::UserRole + 40;

// 微信式列表时间：今天 HH:mm、昨天、本周星期、更早 M/d
QString format_list_time(qint64 seconds) {
  const QDateTime dt = QDateTime::fromSecsSinceEpoch(seconds);
  const QDate today = QDate::currentDate();
  const int days = static_cast<int>(dt.date().daysTo(today));
  if (days <= 0) return dt.toString(QStringLiteral("HH:mm"));
  if (days == 1) return QStringLiteral("昨天");
  if (days < 7) {
    static const char* kWeek[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    return QString::fromUtf8(kWeek[dt.date().dayOfWeek() - 1]);
  }
  return dt.toString(QStringLiteral("M/d"));
}

// 会话列表预览文本（微信式：[图片]/[文件] xxx/[语音] 文本）
QString preview_text_of(const im::MessageInfo& m) {
  switch (m.type()) {
    case MESSAGE_TYPE_TEXT:
      return QString::fromStdString(m.content()).simplified();
    case MESSAGE_TYPE_IMAGE:
      return QStringLiteral("[图片]");
    case MESSAGE_TYPE_FILE:
      return QStringLiteral("[文件] ") + QString::fromStdString(m.file_name());
    case MESSAGE_TYPE_VOICE: {
      QString t = QString::fromStdString(m.asr_text()).simplified();
      if (t.isEmpty()) t = QStringLiteral("语音");
      return QStringLiteral("[语音] ") + t;
    }
    default:
      return QStringLiteral("[消息]");
  }
}

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

// ============================================================
// MainWindow（微信风格主窗：侧边栏 + 列表面板 + 独立聊天窗）
// ============================================================

MainWindow::MainWindow(GatewayClient* client, im::UserInfo me, QWidget* parent)
    : QMainWindow(parent), client_(client), me_(std::move(me)) {
  setWindowTitle(QString("IM - %1").arg(QString::fromStdString(me_.nickname())));
  build_ui();
  connect_push();
  refresh_sessions();
  refresh_friends();
  refresh_pending_count();
}

MainWindow::~MainWindow() = default;

void MainWindow::build_ui() {
  const QString my_name =
      QString::fromStdString(me_.nickname().empty() ? me_.user_id() : me_.nickname());
  setFixedWidth(356);  // 56 侧栏 + 300 列表
  resize(356, 720);

  auto* central = new QWidget(this);
  auto* root = new QHBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ---------- 左侧导航栏（深色，微信式） ----------
  side_bar_ = new QWidget(central);
  side_bar_->setObjectName(QStringLiteral("sideBar"));
  side_bar_->setAttribute(Qt::WA_StyledBackground, true);
  side_bar_->setFixedWidth(56);
  auto* sv = new QVBoxLayout(side_bar_);
  sv->setContentsMargins(6, 16, 6, 10);
  sv->setSpacing(2);

  my_avatar_ = new QLabel(side_bar_);
  my_avatar_->setPixmap(avatar_pixmap(my_name, 40));
  my_avatar_->setFixedSize(40, 40);
  my_avatar_->setToolTip(my_name);
  sv->addWidget(my_avatar_, 0, Qt::AlignHCenter);
  sv->addSpacing(16);

  chat_nav_ = new QPushButton(QStringLiteral("聊天"), side_bar_);
  chat_nav_->setObjectName(QStringLiteral("sideNav"));
  chat_nav_->setCheckable(true);
  chat_nav_->setChecked(true);
  chat_nav_->setFixedSize(44, 44);
  chat_nav_->setCursor(Qt::PointingHandCursor);
  contact_nav_ = new QPushButton(QStringLiteral("通讯录"), side_bar_);
  contact_nav_->setObjectName(QStringLiteral("sideNav"));
  contact_nav_->setCheckable(true);
  contact_nav_->setFixedSize(44, 44);
  contact_nav_->setCursor(Qt::PointingHandCursor);
  auto* nav_group = new QButtonGroup(this);
  nav_group->addButton(chat_nav_, 0);
  nav_group->addButton(contact_nav_, 1);
  nav_group->setExclusive(true);
  sv->addWidget(chat_nav_, 0, Qt::AlignHCenter);
  sv->addWidget(contact_nav_, 0, Qt::AlignHCenter);

  sv->addStretch(1);
  logout_btn_ = new QPushButton(QStringLiteral("退出"), side_bar_);
  logout_btn_->setObjectName(QStringLiteral("sideNav"));
  logout_btn_->setFixedSize(44, 30);
  logout_btn_->setCursor(Qt::PointingHandCursor);
  sv->addWidget(logout_btn_, 0, Qt::AlignHCenter);

  // 通讯录红点（有未处理事件时显示，绝对定位在按钮右上角）
  pending_dot_ = new QLabel(side_bar_);
  pending_dot_->setObjectName(QStringLiteral("redDot"));
  pending_dot_->setFixedSize(9, 9);
  pending_dot_->hide();

  // ---------- 中间列表面板 ----------
  auto* list_panel = new QWidget(central);
  list_panel->setObjectName(QStringLiteral("listPanel"));
  list_panel->setAttribute(Qt::WA_StyledBackground, true);
  list_panel->setFixedWidth(300);
  auto* lv = new QVBoxLayout(list_panel);
  lv->setContentsMargins(10, 12, 10, 0);
  lv->setSpacing(8);

  auto* search_row = new QHBoxLayout();
  search_row->setSpacing(6);
  search_ = new QLineEdit(list_panel);
  search_->setObjectName(QStringLiteral("searchBox"));
  search_->setPlaceholderText(QStringLiteral("搜索"));
  plus_btn_ = new QPushButton(QStringLiteral("+"), list_panel);
  plus_btn_->setObjectName(QStringLiteral("plusBtn"));
  plus_btn_->setFixedSize(30, 30);
  plus_btn_->setCursor(Qt::PointingHandCursor);
  search_row->addWidget(search_, 1);
  search_row->addWidget(plus_btn_);
  lv->addLayout(search_row);

  pages_ = new QStackedWidget(list_panel);
  // 会话页
  session_list_ = new QListWidget(pages_);
  session_list_->setObjectName(QStringLiteral("listView"));
  session_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  session_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  session_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  pages_->addWidget(session_list_);
  // 通讯录页（入口行 + 好友列表）
  auto* contact_page = new QWidget(pages_);
  auto* cv = new QVBoxLayout(contact_page);
  cv->setContentsMargins(0, 0, 0, 0);
  cv->setSpacing(0);
  pending_entry_ = new QPushButton(QStringLiteral("新的朋友"), contact_page);
  pending_entry_->setObjectName(QStringLiteral("contactEntry"));
  pending_entry_->setCursor(Qt::PointingHandCursor);
  auto* group_entry = new QPushButton(QStringLiteral("创建群聊"), contact_page);
  group_entry->setObjectName(QStringLiteral("contactEntry"));
  group_entry->setCursor(Qt::PointingHandCursor);
  auto* search_entry = new QPushButton(QStringLiteral("搜索/加好友"), contact_page);
  search_entry->setObjectName(QStringLiteral("contactEntry"));
  search_entry->setCursor(Qt::PointingHandCursor);
  cv->addWidget(pending_entry_);
  cv->addWidget(group_entry);
  cv->addWidget(search_entry);
  friend_list_ = new QListWidget(contact_page);
  friend_list_->setObjectName(QStringLiteral("listView"));
  friend_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  friend_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  cv->addWidget(friend_list_, 1);
  pages_->addWidget(contact_page);
  lv->addWidget(pages_, 1);

  root->addWidget(side_bar_);
  root->addWidget(list_panel);
  setCentralWidget(central);

  statusBar()->addWidget(status_ = new QLabel(QStringLiteral("推送连接中..."), this));

  // ---------- 导航切换 / 搜索过滤 ----------
  connect(nav_group, &QButtonGroup::idClicked, this, [this](int id) {
    pages_->setCurrentIndex(id);
    apply_filter();
  });
  connect(search_, &QLineEdit::textChanged, this, [this]() { apply_filter(); });

  // ---------- 「+」菜单（微信式）----------
  auto* menu = new QMenu(plus_btn_);
  auto* act_add = menu->addAction(QStringLiteral("搜索/加好友…"));
  auto* act_group = menu->addAction(QStringLiteral("创建群聊…"));
  auto* act_pending = menu->addAction(QStringLiteral("待处理事件"));
  menu->addSeparator();
  auto* act_refresh = menu->addAction(QStringLiteral("刷新数据"));
  connect(plus_btn_, &QPushButton::clicked, this, [this, menu]() {
    menu->exec(plus_btn_->mapToGlobal(QPoint(0, plus_btn_->height() + 2)));
  });

  // ---------- 列表交互 ----------
  connect(session_list_, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem* item) { open_session(item); });
  connect(friend_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    // 双击好友 → 打开与该好友的单聊会话（按对端 user_id 匹配）
    const QString uid = item->data(kRowIdRole).toString();
    for (auto it = session_peers_.constBegin(); it != session_peers_.constEnd(); ++it) {
      if (it.value() != uid) continue;
      for (int i = 0; i < session_list_->count(); ++i) {
        auto* row = session_list_->item(i);
        if (row->data(kRowIdRole).toString() == it.key()) {
          open_session(row);
          return;
        }
      }
    }
    QMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("请先通过“搜索/加好友”添加对方为好友"));
  });

  // ---------- 入口动作 ----------
  auto open_search = [this]() {
    auto* d = new SearchDialog(client_, QString::fromStdString(me_.user_id()), this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &QDialog::finished, this, [this]() { refresh_sessions(); refresh_friends(); });
    d->show();
  };
  auto open_group = [this]() {
    auto* d = new GroupDialog(client_, QString::fromStdString(me_.user_id()), this,
                              [this]() { refresh_sessions(); });
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->show();
  };
  auto open_pending = [this]() {
    auto* d = new PendingDialog(client_, QString::fromStdString(me_.user_id()), this, [this]() {
      refresh_sessions();
      refresh_friends();
      refresh_pending_count();
    });
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->bind_double_click();
    connect(d, &QDialog::finished, this, [this]() { refresh_pending_count(); });
    d->show();
  };
  connect(act_add, &QAction::triggered, this, open_search);
  connect(act_group, &QAction::triggered, this, open_group);
  connect(act_pending, &QAction::triggered, this, open_pending);
  connect(act_refresh, &QAction::triggered, this, [this]() {
    refresh_sessions();
    refresh_friends();
    refresh_pending_count();
  });
  connect(pending_entry_, &QPushButton::clicked, this, open_pending);
  connect(group_entry, &QPushButton::clicked, this, open_group);
  connect(search_entry, &QPushButton::clicked, this, open_search);

  // ---------- 退出登录 ----------
  connect(logout_btn_, &QPushButton::clicked, this, [this]() {
    UserLogoutReq req;
    client_->call_p<UserLogoutResp>(REQ_TYPE_LOGOUT, req,
                                    [](bool, const QString&, const UserLogoutResp&) {});
    client_->disconnect_push();  // 断开旧账号推送通道，重登后按新 token 重连
    client_->clear_token();
    QSettings().remove(QStringLiteral("account/token"));  // 下次启动不再自动登录
    for (auto* w : chat_windows_) w->deleteLater();  // 聊天窗持有旧账号会话数据，直接销毁
    chat_windows_.clear();
    emit logoutRequested();  // 主流程切回登录页，随后关闭并销毁主窗口
    close();
  });

  // 红点初始定位（等布局落定）
  QTimer::singleShot(0, this, [this]() { position_pending_dot(); });
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  position_pending_dot();
}

void MainWindow::position_pending_dot() {
  if (pending_dot_ == nullptr || contact_nav_ == nullptr || side_bar_ == nullptr) return;
  const QPoint p = contact_nav_->mapTo(side_bar_, QPoint(contact_nav_->width() - 12, 3));
  pending_dot_->move(p);
  pending_dot_->raise();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  for (auto* w : chat_windows_) w->close();
  event->accept();
}

// ---------------- 会话行（微信式） ----------------

QWidget* MainWindow::build_session_row(const QString& session_id) {
  const QString name = session_names_.value(session_id, session_id);
  auto* w = new QWidget(session_list_);
  w->setFixedHeight(66);
  auto* h = new QHBoxLayout(w);
  h->setContentsMargins(8, 5, 8, 5);
  h->setSpacing(10);

  auto* avatar = new QLabel(w);
  avatar->setFixedSize(40, 40);
  const QString fid = session_avatars_.value(session_id);
  avatar->setPixmap(avatar_pixmaps_.contains(fid) ? avatar_pixmaps_.value(fid)
                                                  : avatar_pixmap(name, 40));
  h->addWidget(avatar, 0, Qt::AlignVCenter);

  auto* col = new QVBoxLayout();
  col->setSpacing(3);
  auto* name_lbl = new QLabel(w);
  name_lbl->setObjectName(QStringLiteral("nameLabel"));
  name_lbl->setFixedWidth(150);
  name_lbl->setText(QFontMetrics(name_lbl->font()).elidedText(name, Qt::ElideRight, 150));
  col->addWidget(name_lbl);
  auto* preview_lbl = new QLabel(w);
  preview_lbl->setObjectName(QStringLiteral("preview"));
  preview_lbl->setFixedWidth(150);
  const QString preview = session_preview_.value(session_id);
  preview_lbl->setText(QFontMetrics(preview_lbl->font()).elidedText(preview, Qt::ElideRight, 150));
  col->addWidget(preview_lbl);
  h->addLayout(col, 1);

  auto* right = new QVBoxLayout();
  right->setSpacing(4);
  auto* time_lbl = new QLabel(w);
  time_lbl->setObjectName(QStringLiteral("timeLabel"));
  const qint64 t = session_preview_time_.value(session_id, 0);
  time_lbl->setText(t > 0 ? format_list_time(t) : QString());
  right->addWidget(time_lbl, 0, Qt::AlignRight);
  const int unread = unread_.value(session_id, 0);
  auto* badge = new QLabel(w);
  badge->setObjectName(QStringLiteral("badge"));
  badge->setFixedHeight(18);
  badge->setMinimumWidth(18);
  badge->setAlignment(Qt::AlignCenter);
  badge->setText(unread > 99 ? QStringLiteral("99+") : QString::number(unread));
  badge->setVisible(unread > 0);
  right->addWidget(badge, 0, Qt::AlignRight);
  h->addLayout(right);
  return w;
}

void MainWindow::rebuild_session_row(const QString& session_id) {
  auto* item = session_items_.value(session_id);
  if (item == nullptr) return;
  QWidget* w = build_session_row(session_id);
  item->setSizeHint(w->sizeHint());
  session_list_->removeItemWidget(item);
  session_list_->setItemWidget(item, w);
}

void MainWindow::rebuild_session_list() {
  session_list_->clear();
  session_items_.clear();
  // 按最近消息时间倒序（微信行为）；无预览时间的按原相对顺序排后面
  QStringList sids = sessions_.keys();
  std::stable_sort(sids.begin(), sids.end(), [this](const QString& a, const QString& b) {
    return session_preview_time_.value(a, 0) > session_preview_time_.value(b, 0);
  });
  for (const QString& sid : sids) {
    auto* item = new QListWidgetItem(session_list_);
    item->setData(kRowIdRole, sid);
    item->setData(kSearchNameRole, session_names_.value(sid, sid));
    session_items_[sid] = item;
    rebuild_session_row(sid);
  }
  apply_filter();
}

QWidget* MainWindow::build_friend_row(const QString& user_id) {
  const im::UserInfo u = friends_.value(user_id);
  const QString name = u.nickname().empty() ? user_id : QString::fromStdString(u.nickname());
  auto* w = new QWidget(friend_list_);
  w->setFixedHeight(58);
  auto* h = new QHBoxLayout(w);
  h->setContentsMargins(8, 5, 8, 5);
  h->setSpacing(10);

  auto* avatar = new QLabel(w);
  avatar->setFixedSize(40, 40);
  const QString fid = QString::fromStdString(u.avatar_file_id());
  avatar->setPixmap(avatar_pixmaps_.contains(fid) ? avatar_pixmaps_.value(fid)
                                                  : avatar_pixmap(name, 40));
  h->addWidget(avatar, 0, Qt::AlignVCenter);

  auto* col = new QVBoxLayout();
  col->setSpacing(3);
  auto* name_lbl = new QLabel(w);
  name_lbl->setObjectName(QStringLiteral("nameLabel"));
  name_lbl->setFixedWidth(210);
  name_lbl->setText(QFontMetrics(name_lbl->font()).elidedText(name, Qt::ElideRight, 210));
  col->addWidget(name_lbl);
  QString desc = QString::fromStdString(u.description()).trimmed();
  if (desc.isEmpty()) desc = QString::fromStdString(u.phone());
  auto* desc_lbl = new QLabel(w);
  desc_lbl->setObjectName(QStringLiteral("preview"));
  desc_lbl->setFixedWidth(210);
  desc_lbl->setText(QFontMetrics(desc_lbl->font()).elidedText(desc, Qt::ElideRight, 210));
  col->addWidget(desc_lbl);
  h->addLayout(col, 1);
  return w;
}

void MainWindow::rebuild_friend_row(const QString& user_id) {
  auto* item = friend_items_.value(user_id);
  if (item == nullptr) return;
  QWidget* w = build_friend_row(user_id);
  item->setSizeHint(w->sizeHint());
  friend_list_->removeItemWidget(item);
  friend_list_->setItemWidget(item, w);
}

void MainWindow::apply_filter() {
  const QString kw = search_->text().trimmed();
  QListWidget* active = pages_->currentIndex() == 0 ? session_list_ : friend_list_;
  for (int i = 0; i < active->count(); ++i) {
    QListWidgetItem* item = active->item(i);
    const QString name = item->data(kSearchNameRole).toString();
    item->setHidden(!kw.isEmpty() && !name.contains(kw, Qt::CaseInsensitive));
  }
}

// ---------------- 会话预览 ----------------

void MainWindow::fetch_session_preview(const QString& session_id) {
  GetHistoryReq req;
  req.set_chat_session_id(session_id.toStdString());
  req.set_cursor_timestamp(0);
  req.set_limit(1);  // 只取最新一条做列表预览
  client_->call_p<GetHistoryResp>(
      REQ_TYPE_GET_HISTORY, req,
      [this, session_id](bool ok, const QString&, const GetHistoryResp& resp) {
        if (!ok || resp.messages_size() == 0) return;
        update_preview(session_id, resp.messages(resp.messages_size() - 1));
        // 预览时间就绪后重排（微信行为：最近有消息的会话在最上）。
        // 多个会话的预览回调会集中到达，用防抖聚合成一次重建。
        if (reorder_timer_ == nullptr) {
          reorder_timer_ = new QTimer(this);
          reorder_timer_->setSingleShot(true);
          reorder_timer_->setInterval(80);
          connect(reorder_timer_, &QTimer::timeout, this, [this]() { rebuild_session_list(); });
        }
        reorder_timer_->start();
      });
}

void MainWindow::update_preview(const QString& session_id, const im::MessageInfo& msg) {
  session_preview_[session_id] = preview_text_of(msg);
  session_preview_time_[session_id] = msg.create_time();
}

void MainWindow::connect_push() {
  connect(client_, &GatewayClient::pushConnected, this,
          [this](bool ok, const QString& errmsg) {
            status_->setText(ok ? QStringLiteral("推送: 已连接")
                                : QStringLiteral("推送: %1").arg(errmsg));
            // 断线重连后补漏：已打开的聊天窗各补拉一次最新消息
            // （sync_latest 幂等防重入；首次连接时通常无窗口，天然无副作用）
            if (ok) {
              for (auto* w : chat_windows_) w->sync_latest();
            }
          });
  connect(client_, &GatewayClient::pushReceived, this, &MainWindow::on_push);
  client_->connect_push();
}

void MainWindow::on_push(int type, const QByteArray& body) {
  if (type == PUSH_TYPE_NEW_MESSAGE) {
    im::MessageInfo msg;
    if (!msg.ParseFromArray(body.constData(), body.size())) return;
    const QString sid = QString::fromStdString(msg.chat_session_id());
    update_preview(sid, msg);  // 微信行为：列表预览与时间即时更新
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
    refresh_sessions();  // 会话排序/新会话补拉（预览已有则不再请求）
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
  refresh_session_member(sid);
  return sid;  // 解析完成前先以 session_id 展示
}

// 单聊会话：异步拉取对方昵称/头像并刷新列表项（打开会话时也会调用，
// 对方换头像/改昵称后本端打开即刷新）
void MainWindow::refresh_session_member(const QString& sid) {
  GetSessionMemberReq req;
  req.set_chat_session_id(sid.toStdString());
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
          rebuild_session_row(sid);
          break;
        }
      });
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
        for (const auto& s : resp.session_list()) {
          const QString sid = QString::fromStdString(s.chat_session_id());
          sessions_[sid] = s;
          display_name_for(s);  // 异步补全显示名/头像
        }
        rebuild_session_list();  // 重建行（含排序/角标/预览）
        // 预览缺失的会话各拉一次最后一条消息
        for (const auto& s : resp.session_list()) {
          const QString sid = QString::fromStdString(s.chat_session_id());
          if (!session_preview_time_.contains(sid)) fetch_session_preview(sid);
        }
      });
}

void MainWindow::update_session_item(const QString& session_id) {
  rebuild_session_row(session_id);
}

void MainWindow::on_session_seen(const QString& session_id) {
  if (unread_.value(session_id, 0) == 0) return;
  unread_[session_id] = 0;
  update_session_item(session_id);
}

void MainWindow::ensure_avatar(const QString& file_id) {
  if (file_id.isEmpty() || avatar_pixmaps_.contains(file_id) || avatar_pending_.contains(file_id)) {
    return;
  }
  avatar_pending_.insert(file_id);
  client_->download_file(
      file_id, [this, file_id](bool ok, const QString&, const QByteArray& data, const QString&) {
        avatar_pending_.remove(file_id);
        if (!ok || data.isEmpty()) return;
        QPixmap pm;
        if (!pm.loadFromData(data)) return;
        avatar_pixmaps_[file_id] =
            pm.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        on_avatar_ready(file_id);
      });
}

void MainWindow::on_avatar_ready(const QString& file_id) {
  // 刷新引用该头像的所有行
  for (auto it = session_avatars_.constBegin(); it != session_avatars_.constEnd(); ++it) {
    if (it.value() == file_id) rebuild_session_row(it.key());
  }
  for (auto it = friends_.constBegin(); it != friends_.constEnd(); ++it) {
    if (QString::fromStdString(it.value().avatar_file_id()) == file_id) {
      rebuild_friend_row(it.key());
    }
  }
}

void MainWindow::refresh_friends() {
  GetFriendListReq req;
  client_->call_p<GetFriendListResp>(
      REQ_TYPE_GET_FRIEND_LIST, req,
      [this](bool ok, const QString&, const GetFriendListResp& resp) {
        if (!ok) return;
        friends_.clear();
        friend_items_.clear();
        friend_list_->clear();
        for (const auto& u : resp.friend_list()) {
          const QString uid = QString::fromStdString(u.user_id());
          friends_[uid] = u;
          auto* item = new QListWidgetItem(friend_list_);
          item->setData(kRowIdRole, uid);
          item->setData(kSearchNameRole, QString::fromStdString(u.nickname()));
          friend_items_[uid] = item;
          rebuild_friend_row(uid);
          ensure_avatar(QString::fromStdString(u.avatar_file_id()));
        }
        apply_filter();
      });
}

void MainWindow::refresh_pending_count() {
  GetPendingEventsReq req;
  client_->call_p<GetPendingEventsResp>(
      REQ_TYPE_GET_PENDING_EVENTS, req,
      [this](bool ok, const QString&, const GetPendingEventsResp& resp) {
        if (!ok) return;
        const int n = resp.friend_apply_events_size() + resp.group_events_size();
        pending_entry_->setText(n > 0 ? QString("新的朋友 (%1)").arg(n)
                                      : QStringLiteral("新的朋友"));
        pending_dot_->setVisible(n > 0);
        position_pending_dot();
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
  const QString sid = item->data(kRowIdRole).toString();
  if (sid.isEmpty()) return;
  const bool existed = chat_windows_.contains(sid);
  ChatWindow* w = chat_window_for(sid, session_names_.value(sid, sid));
  w->show();
  w->raise();
  w->activateWindow();
  if (existed) w->sync_latest();  // 重开旧窗口：补拉断连/关闭期间漏掉的消息
  // 单聊：打开即刷新对方昵称/头像（群聊名称静态，跳过；sessions_ 查不到也跳过）
  if (sessions_.contains(sid) && sessions_.value(sid).type() != SESSION_TYPE_GROUP) {
    refresh_session_member(sid);
  }
  on_session_seen(sid);  // 打开即已读
}

}  // namespace im