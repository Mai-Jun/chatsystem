#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QWebSocket>

#include <functional>

#include "gateway.pb.h"

class QNetworkAccessManager;
class QTimer;

namespace im {

// ============================================================
// 客户端协议层（M9）
//   - call(): HTTP POST /gateway，ClientRequest 外壳，异步回调（主线程）
//   - connect_push(): WebSocket 连接后首帧发带 token 的 ClientRequest 鉴权，
//     收到确认帧后进入推送状态，ServerPush 通过 pushReceived 信号上抛
//     （首帧恒为鉴权确认：连接未绑定前服务端不会推送任何业务帧）
//   - 断线自动重连（3 秒重试），保活走协议层 ping（服务端对已绑定
//     连接忽略一切业务帧）
// 所有回调/信号都在主线程（QNetworkAccessManager/QWebSocket 事件驱动），
// UI 层可直接使用。
// ============================================================

class GatewayClient : public QObject {
  Q_OBJECT
 public:
  GatewayClient(const QString& http_host, quint16 http_port, const QString& ws_host,
                quint16 ws_port, QObject* parent = nullptr);

  void set_token(const QString& token) { token_ = token; }
  const QString& token() const { return token_; }
  void clear_token() { token_.clear(); }

  using RespCallback = std::function<void(const im::ServerResponse&)>;

  // 发起一次网关请求（body 为具体 Req 的序列化结果）
  void call(im::RequestType type, const std::string& body, RespCallback on_done);

  // 便捷封装：请求 + 响应 body 解析一步完成
  template <class RespT>
  void call_p(im::RequestType type, const google::protobuf::MessageLite& req,
              std::function<void(bool ok, const QString& errmsg, const RespT&)> on_done) {
    call(type, req.SerializeAsString(),
         [on_done](const im::ServerResponse& resp) {
           RespT parsed;
           if (resp.success() && !parsed.ParseFromString(resp.body())) {
             on_done(false, QStringLiteral("响应解析失败"), parsed);
             return;
           }
           on_done(resp.success(), QString::fromStdString(resp.errmsg()), parsed);
         });
  }

  void connect_push();
  void disconnect_push();
  bool push_connected() const { return ws_ready_; }

  // ---- 文件上传/下载（头像/图片/文件/语音共用，经网关转文件子服务）----
  // 上传：成功回调服务端生成的 file_id；失败 errmsg 非空
  using UploadCallback =
      std::function<void(bool ok, const QString& errmsg, const QString& file_id)>;
  // 下载：成功回调文件内容与服务端记录的文件名
  using DownloadCallback = std::function<void(bool ok, const QString& errmsg,
                                              const QByteArray& content, const QString& file_name)>;
  void upload_file(const QString& file_name, const QByteArray& content, UploadCallback on_done);
  void download_file(const QString& file_id, DownloadCallback on_done);

 signals:
  void pushConnected(bool ok, const QString& errmsg);
  void pushReceived(int type, const QByteArray& body);  // body 为对应 Info 的序列化结果

 private:
  void schedule_reconnect();
  void on_ws_connected();
  void on_ws_disconnected();
  void on_ws_message(const QByteArray& frame);

  QNetworkAccessManager* nam_;
  QWebSocket ws_;
  QString http_host_;
  quint16 http_port_ = 9000;
  QString ws_host_;
  quint16 ws_port_ = 9001;
  QString token_;

  QString pending_auth_id_;  // 非空表示正在等待鉴权确认帧
  bool ws_ready_ = false;
  bool user_closed_ = false;  // 主动断开时不重连
  QTimer* reconnect_timer_ = nullptr;
  QTimer* ping_timer_ = nullptr;
};

}  // namespace im
