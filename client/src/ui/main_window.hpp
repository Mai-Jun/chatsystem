#pragma once
#include <QIcon>
#include <QMainWindow>
#include <QMap>
#include <QPixmap>
#include <QSet>

#include "base.pb.h"
#include "friend.pb.h"
#include "gateway.pb.h"

class QListWidget;
class QListWidgetItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QCloseEvent;
class QResizeEvent;
class QTimer;

namespace im {

class GatewayClient;
class ChatWindow;

// 主窗口（微信风格，M10 UI 轮）
// - 左侧深色导航栏（头像 / 聊天 / 通讯录 / 退出），通讯录有未处理事件时显示红点
// - 中间列表面板：搜索框（本地过滤）+ 「会话 / 通讯录」两页
// - 会话行为自绘控件：头像 + 昵称 + 末条消息预览 + 时间 + 未读数红色角标
//     · 预览来自 GetHistory(limit=1)，推送到达即时更新
//     · 会话按最近消息时间倒序（微信行为）
// - 未读计数：推送到达且会话窗未聚焦则累加，聚焦/打开清零
// - 头像：avatar_file_id 异步下载缓存，未设置时用首字圆角方头像
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow(GatewayClient* client, im::UserInfo me, QWidget* parent = nullptr);
  ~MainWindow() override;

 signals:
  void logoutRequested();  // 退出登录：主流程据此切回登录/注册页（进程不退出）

 protected:
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void build_ui();
  void connect_push();
  void refresh_sessions();
  void refresh_friends();
  void refresh_pending_count();
  void fetch_session_preview(const QString& session_id);      // 拉最后一条消息做列表预览
  void update_preview(const QString& session_id, const im::MessageInfo& msg);
  void rebuild_session_list();                                // 清空重建 + 按预览时间排序
  void rebuild_session_row(const QString& session_id);
  void rebuild_friend_row(const QString& user_id);
  QWidget* build_session_row(const QString& session_id);
  QWidget* build_friend_row(const QString& user_id);
  void apply_filter();
  void position_pending_dot();
  void open_session(QListWidgetItem* item);
  ChatWindow* chat_window_for(const QString& session_id, const QString& display_name);
  void on_push(int type, const QByteArray& body);
  QString display_name_for(const im::ChatSessionInfo& s);
  void refresh_session_member(const QString& session_id);  // 单聊：异步刷新对方昵称/头像

  void on_session_seen(const QString& session_id);
  void update_session_item(const QString& session_id);  // 重建该会话行（未读/预览变化）
  void ensure_avatar(const QString& file_id);
  void on_avatar_ready(const QString& file_id);         // 头像下载完成：刷新引用它的行

  GatewayClient* client_;
  im::UserInfo me_;

  // 侧边栏
  QWidget* side_bar_ = nullptr;
  QLabel* my_avatar_ = nullptr;
  QPushButton* chat_nav_ = nullptr;
  QPushButton* contact_nav_ = nullptr;
  QPushButton* logout_btn_ = nullptr;
  QLabel* pending_dot_ = nullptr;   // 通讯录红点（有未处理事件时显示）

  // 列表面板
  QLineEdit* search_ = nullptr;
  QPushButton* plus_btn_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QListWidget* session_list_ = nullptr;
  QListWidget* friend_list_ = nullptr;
  QPushButton* pending_entry_ = nullptr;  // 通讯录页「新的朋友 (N)」
  QTimer* reorder_timer_ = nullptr;       // 预览就绪后重排防抖

  QLabel* status_ = nullptr;         // 底部状态：WS 推送连接状态

  QMap<QString, im::ChatSessionInfo> sessions_;             // session_id -> info
  QMap<QString, QString> session_names_;                    // session_id -> 显示名
  QMap<QString, QString> session_peers_;                    // session_id -> 对端 user_id(单聊)
  QMap<QString, QString> session_avatars_;                  // session_id -> 头像 file_id
  QMap<QString, QListWidgetItem*> session_items_;           // session_id -> 列表项
  QMap<QString, QString> session_preview_;                  // session_id -> 末条消息预览
  QMap<QString, qint64> session_preview_time_;              // session_id -> 末条消息时间
  QMap<QString, int> unread_;                               // session_id -> 未读数
  QMap<QString, im::UserInfo> friends_;                     // user_id -> info
  QMap<QString, QListWidgetItem*> friend_items_;            // user_id -> 列表项
  QMap<QString, ChatWindow*> chat_windows_;                 // session_id -> 打开的聊天窗

  QMap<QString, QPixmap> avatar_pixmaps_;                   // file_id -> 已下载头像(40px)
  QSet<QString> avatar_pending_;                            // 下载中的头像 file_id
};

}  // namespace im