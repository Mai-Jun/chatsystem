#include "protocol/gateway_client.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>
#include <QUrl>

namespace im {

GatewayClient::GatewayClient(const QString& http_host, quint16 http_port, const QString& ws_host,
                             quint16 ws_port, QObject* parent)
    : QObject(parent),
      nam_(new QNetworkAccessManager(this)),
      http_host_(http_host),
      http_port_(http_port),
      ws_host_(ws_host),
      ws_port_(ws_port) {
  connect(&ws_, &QWebSocket::connected, this, &GatewayClient::on_ws_connected);
  connect(&ws_, &QWebSocket::disconnected, this, &GatewayClient::on_ws_disconnected);
  connect(&ws_, &QWebSocket::binaryMessageReceived, this,
          [this](const QByteArray& f) { on_ws_message(f); });

  reconnect_timer_ = new QTimer(this);
  reconnect_timer_->setSingleShot(true);
  reconnect_timer_->setInterval(3000);
  connect(reconnect_timer_, &QTimer::timeout, this, &GatewayClient::connect_push);

  // 保活：协议层 ping（服务端自动回 pong；业务帧会被网关忽略）
  ping_timer_ = new QTimer(this);
  ping_timer_->setInterval(30000);
  connect(ping_timer_, &QTimer::timeout, this, [this]() {
    if (ws_ready_) ws_.ping();
  });
  ping_timer_->start();
}

void GatewayClient::call(im::RequestType type, const std::string& body, RespCallback on_done) {
  im::ClientRequest creq;
  creq.set_request_id(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
  creq.set_type(type);
  creq.set_token(token_.toStdString());
  creq.set_body(body);

  QNetworkRequest request(QUrl(QString("http://%1:%2/gateway").arg(http_host_).arg(http_port_)));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-protobuf");
  const std::string payload = creq.SerializeAsString();
  QNetworkReply* reply = nam_->post(
      request, QByteArray(payload.data(), static_cast<int>(payload.size())));
  connect(reply, &QNetworkReply::finished, this, [this, reply, cb = std::move(on_done)]() {
    reply->deleteLater();
    im::ServerResponse resp;
    if (reply->error() != QNetworkReply::NoError) {
      resp.set_success(false);
      resp.set_errmsg(QStringLiteral("网络错误: %1").arg(reply->errorString()).toStdString());
      cb(resp);
      return;
    }
    const QByteArray payload = reply->readAll();
    if (!resp.ParseFromArray(payload.constData(), payload.size())) {
      resp.set_success(false);
      resp.set_errmsg("响应解析失败");
    }
    cb(resp);
  });
}

void GatewayClient::connect_push() {
  if (token_.isEmpty()) return;  // 未登录不连推送
  user_closed_ = false;
  if (ws_ready_) return;
  pending_auth_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QUrl url(QString("ws://%1:%2").arg(ws_host_).arg(ws_port_));
  ws_.open(url);
}

void GatewayClient::disconnect_push() {
  user_closed_ = true;
  ws_ready_ = false;
  ws_.close();
}

void GatewayClient::on_ws_connected() {
  im::ClientRequest auth;
  auth.set_request_id(pending_auth_id_.toStdString());
  auth.set_type(im::REQ_TYPE_UNKNOWN);  // 鉴权帧只看 token
  auth.set_token(token_.toStdString());
  const std::string payload = auth.SerializeAsString();
  ws_.sendBinaryMessage(QByteArray(payload.data(), static_cast<int>(payload.size())));
}

void GatewayClient::on_ws_disconnected() {
  const bool was_ready = ws_ready_;
  ws_ready_ = false;
  if (was_ready) emit pushConnected(false, QStringLiteral("推送连接断开"));
  if (!user_closed_ && !token_.isEmpty()) schedule_reconnect();
}

void GatewayClient::on_ws_message(const QByteArray& frame) {
  // 首帧恒为鉴权确认（ServerResponse）；此后全部为业务推送（ServerPush）
  if (!pending_auth_id_.isEmpty() && !ws_ready_) {
    im::ServerResponse resp;
    const bool ok = resp.ParseFromArray(frame.constData(), frame.size()) && resp.success();
    pending_auth_id_.clear();
    ws_ready_ = ok;
    if (ok) {
      emit pushConnected(true, QStringLiteral("ok"));
    } else {
      emit pushConnected(false,
                         QString::fromStdString(resp.errmsg().empty() ? "鉴权失败" : resp.errmsg()));
      schedule_reconnect();
    }
    return;
  }
  im::ServerPush push;
  if (!push.ParseFromArray(frame.constData(), frame.size())) return;
  const std::string& body = push.body();
  emit pushReceived(static_cast<int>(push.type()),
                    QByteArray(body.data(), static_cast<int>(body.size())));
}

void GatewayClient::schedule_reconnect() {
  if (!reconnect_timer_->isActive()) reconnect_timer_->start();
}

}  // namespace im
