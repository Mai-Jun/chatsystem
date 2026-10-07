#pragma once
#include <QIcon>
#include <QMainWindow>
#include <QMap>
#include <QSet>

#include "base.pb.h"
#include "friend.pb.h"
#include "gateway.pb.h"

class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QTabWidget;
class QCloseEvent;

namespace im {

class GatewayClient;
class ChatWindow;

// 主窗口：会话列表 / 好友列表 / 待处理事件 / 建群 / 实时推送
// - 未读计数：推送到达时会话窗未聚焦则累加，聚焦/打开时清零
// - 头像：列表项按 avatar_file_id 异步下载并缓存（同一文件只下一次）
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow(GatewayClient* client, im::UserInfo me, QWidget* parent = nullptr);
  ~MainWindow() override;

 signals:
  void logoutRequested();  // 退出登录：主流程据此切回登录/注册页（进程不退出）

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void build_toolbar();
  void connect_push();
  void refresh_sessions();
  void refresh_friends();
  void refresh_pending_count();
  void open_session(QListWidgetItem* item);
  ChatWindow* chat_window_for(const QString& session_id, const QString& display_name);
  void on_push(int type, const QByteArray& body);
  QString display_name_for(const im::ChatSessionInfo& s);

  void on_session_seen(const QString& session_id);
  void update_session_item(const QString& session_id);
  void ensure_avatar(const QString& file_id);
  void apply_avatars();

  GatewayClient* client_;
  im::UserInfo me_;

  QTabWidget* tabs_ = nullptr;
  QListWidget* session_list_ = nullptr;
  QListWidget* friend_list_ = nullptr;
  QLabel* status_ = nullptr;         // 底部状态：WS 推送连接状态
  QPushButton* pending_btn_ = nullptr;

  QMap<QString, im::ChatSessionInfo> sessions_;             // session_id -> info
  QMap<QString, QString> session_names_;                    // session_id -> 显示名
  QMap<QString, QString> session_peers_;                    // session_id -> 对端 user_id(单聊)
  QMap<QString, QString> session_avatars_;                  // session_id -> 头像 file_id
  QMap<QString, QListWidgetItem*> session_items_;           // session_id -> 列表项
  QMap<QString, int> unread_;                               // session_id -> 未读数
  QMap<QString, im::UserInfo> friends_;                     // user_id -> info
  QMap<QString, ChatWindow*> chat_windows_;                 // session_id -> 打开的聊天窗

  QMap<QString, QIcon> avatar_icons_;                       // file_id -> 已下载头像
  QSet<QString> avatar_pending_;                            // 下载中的头像 file_id
};

}  // namespace im
