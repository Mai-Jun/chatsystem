// 本地 Mock 网关（UI 开发/自检用，不连服务器、不依赖后端）
//
// 用途：在没有可用后端（或离线开发）时，用预置数据把 Qt 客户端全界面跑起来——
//   登录页三标签 → 主窗口（会话/好友/待处理）→ 聊天窗（四类消息/头像/未读/分页）。
//   HTTP 9000 应答协议请求（ClientRequest/ServerResponse），WS 9001 鉴权后
//   可把 TRANSMIT 的消息按广播语义回推（与真实网关行为一致，客户端按 message_id 去重）。
//
// 用法（本机 MSYS2 构建后）：
//   ./mock_gateway [--http-port 9000] [--ws-port 9001] [--push-demo <session_id>]
//   --push-demo   鉴权完成后 4 秒推一条消息到指定会话（演示未读角标/实时推送）
//
// 预置数据：
//   - 登录任意手机号/密码即成功（token=mock-token）
//   - 单聊会话（对方「晓彤」，带头像）含 9 条历史：文本/图片/文件/语音四类
//   - 群聊「开发小组」3 人，含群消息（演示发送者昵称）
//   - 1 条待处理好友申请（演示角标）
//   - 图片/头像/语音按 file_id 动态生成（PNG / 1.5s 440Hz WAV），下载链路全通
//
// 仅监听 127.0.0.1；正式联调仍走真实网关（47.112.192.119:9000/9001）。
#include <QBuffer>
#include <QGuiApplication>
#include <QHostAddress>
#include <QImage>
#include <QPainter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include <cstdio>
#include <cmath>
#include <map>
#include <vector>

#include "base.pb.h"
#include "file.pb.h"
#include "friend.pb.h"
#include "gateway.pb.h"
#include "message_storage.pb.h"
#include "message_transmit.pb.h"
#include "user.pb.h"

using namespace im;  // NOLINT

namespace {

const char* kMyId = "u-me";
const char* kMyName = "麦麦";
const char* kPeerId = "u-peer";
const char* kPeerName = "晓彤";
const char* kOtherId = "u-other";
const char* kOtherName = "阿杰";
const char* kSingleSid = "sess-single-01";
const char* kGroupSid = "sess-group-01";

int64_t now_s() { return QDateTime::currentSecsSinceEpoch(); }

// ---------------- 预置数据 ----------------
UserInfo make_user(const char* id, const char* name, const char* avatar = "") {
  UserInfo u;
  u.set_user_id(id);
  u.set_nickname(name);
  u.set_avatar_file_id(avatar);
  u.set_phone("19353589846");
  return u;
}

// 圆形首字头像 PNG
QByteArray make_avatar_png(const QString& text, const QColor& bg) {
  QImage img(120, 120, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(bg);
  p.setPen(Qt::NoPen);
  p.drawEllipse(4, 4, 112, 112);
  p.setPen(Qt::white);
  QFont f = p.font();
  f.setPixelSize(56);
  f.setBold(true);
  p.setFont(f);
  p.drawText(img.rect(), Qt::AlignCenter, text);
  p.end();
  QByteArray out;
  QBuffer buf(&out);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "PNG");
  return out;
}

// 640x400 示例图片（图片消息内容）
QByteArray make_demo_png() {
  QImage img(640, 400, QImage::Format_ARGB32_Premultiplied);
  QPainter p(&img);
  QLinearGradient grad(0, 0, 640, 400);
  grad.setColorAt(0, QColor(0x07, 0xC1, 0x60));
  grad.setColorAt(1, QColor(0x0B, 0x8A, 0x4A));
  p.fillRect(img.rect(), grad);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(QColor(255, 255, 255, 230));
  p.setPen(Qt::NoPen);
  p.drawEllipse(QPointF(140, 200), 56, 56);
  p.setPen(QColor(0x07, 0xC1, 0x60));
  QFont lf = p.font();
  lf.setPixelSize(44);
  lf.setBold(true);
  p.setFont(lf);
  p.drawText(QRectF(84, 172, 112, 56), Qt::AlignCenter, "IM");
  p.setPen(Qt::white);
  QFont tf = p.font();
  tf.setPixelSize(30);
  p.setFont(tf);
  p.drawText(QRectF(230, 150, 360, 100), Qt::AlignVCenter | Qt::AlignLeft,
             QStringLiteral("图片消息示例\n（Mock 网关生成）"));
  p.end();
  QByteArray out;
  QBuffer buf(&out);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "PNG");
  return out;
}

// 16k/单声道/16bit PCM WAV：1.5 秒 440Hz 正弦（语音消息内容，可播放）
QByteArray make_voice_wav(int ms = 1500, double freq = 440.0) {
  const int rate = 16000;
  const int n = rate * ms / 1000;
  QByteArray pcm(n * 2, 0);
  for (int i = 0; i < n; ++i) {
    double env = 1.0;
    if (i < 800) env = i / 800.0;                 // 淡入，避免爆音
    if (n - i < 800) env = (n - i) / 800.0;       // 淡出
    int16_t s = static_cast<int16_t>(std::sin(2 * M_PI * freq * i / rate) * 0.35 * env * 32767);
    pcm[2 * i] = static_cast<char>(s & 0xFF);
    pcm[2 * i + 1] = static_cast<char>((s >> 8) & 0xFF);
  }
  auto le32 = [](QByteArray& b, quint32 v) {
    b.append(static_cast<char>(v & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>((v >> 16) & 0xFF));
    b.append(static_cast<char>((v >> 24) & 0xFF));
  };
  auto le16 = [](QByteArray& b, quint16 v) {
    b.append(static_cast<char>(v & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
  };
  QByteArray wav;
  wav.append("RIFF");
  le32(wav, 36 + pcm.size());
  wav.append("WAVE");
  wav.append("fmt ");
  le32(wav, 16);
  le16(wav, 1);       // PCM
  le16(wav, 1);       // mono
  le32(wav, rate);
  le32(wav, rate * 2);  // byte rate
  le16(wav, 2);       // block align
  le16(wav, 16);      // bits
  wav.append("data");
  le32(wav, pcm.size());
  wav.append(pcm);
  return wav;
}

// ---------------- Mock 状态 ----------------
class MockState {
 public:
  MockState() { build_history(); }

  MessageInfo add_text(const char* sid, const char* sender, const QString& text, int64_t t) {
    MessageInfo m;
    m.set_message_id(QString("mock-msg-%1").arg(next_id_++).toStdString());
    m.set_chat_session_id(sid);
    m.set_sender_id(sender);
    m.set_type(MESSAGE_TYPE_TEXT);
    m.set_content(text.toStdString());
    m.set_create_time(t);
    return m;
  }

  void build_history() {
    const int64_t base = now_s() - 600;
    std::vector<MessageInfo>& single = hist_[kSingleSid];
    single.push_back(add_text(kSingleSid, kPeerId, QStringLiteral("嗨，在吗？"), base + 1));
    single.push_back(add_text(kSingleSid, kMyId, QStringLiteral("在的，怎么了？"), base + 30));
    single.push_back(
        add_text(kSingleSid, kPeerId, QStringLiteral("我把上次聚会的照片发你看下"), base + 62));
    {
      MessageInfo m;
      m.set_message_id("mock-msg-img");
      m.set_chat_session_id(kSingleSid);
      m.set_sender_id(kPeerId);
      m.set_type(MESSAGE_TYPE_IMAGE);
      m.set_file_id("img-01");
      m.set_file_name("party.png");
      m.set_file_size(0);
      m.set_create_time(base + 90);
      single.push_back(m);
      files_["img-01"] = {QStringLiteral("party.png"), make_demo_png()};
    }
    single.push_back(add_text(kSingleSid, kMyId, QStringLiteral("拍得真好！"), base + 120));
    {
      MessageInfo m;
      m.set_message_id("mock-msg-file");
      m.set_chat_session_id(kSingleSid);
      m.set_sender_id(kMyId);
      m.set_type(MESSAGE_TYPE_FILE);
      m.set_file_id("file-01");
      m.set_file_name(QStringLiteral("项目周报-第41周.pdf").toStdString());
      m.set_file_size(238 * 1024);
      m.set_create_time(base + 150);
      single.push_back(m);
      files_["file-01"] = {QStringLiteral("项目周报-第41周.pdf"),
                           QByteArray("%PDF-1.4 mock demo content", 25)};
    }
    single.push_back(
        add_text(kSingleSid, kPeerId, QStringLiteral("周报我收到了，晚点细看"), base + 200));
    {
      MessageInfo m;
      m.set_message_id("mock-msg-voice");
      m.set_chat_session_id(kSingleSid);
      m.set_sender_id(kPeerId);
      m.set_type(MESSAGE_TYPE_VOICE);
      m.set_file_id("voice-01");
      m.set_file_name("voice.wav");
      m.set_file_size(0);
      m.set_asr_text(QStringLiteral("（开发模式）语音消息").toStdString());
      m.set_create_time(base + 240);
      single.push_back(m);
      files_["voice-01"] = {QStringLiteral("voice.wav"), make_voice_wav()};
    }
    single.push_back(add_text(kSingleSid, kMyId, QStringLiteral("行，有问题随时说"), base + 270));

    std::vector<MessageInfo>& group = hist_[kGroupSid];
    group.push_back(add_text(kGroupSid, kMyId, QStringLiteral("周会纪要发群里了，大家看下"), base + 5));
    group.push_back(add_text(kGroupSid, kOtherId, QStringLiteral("收到，我明天评审前给反馈"), base + 40));
    group.push_back(add_text(kGroupSid, kPeerId, QStringLiteral("+1，辛苦了"), base + 80));

    // 头像文件（会话/好友列表异步下载）
    files_["avatar-peer-01"] = {"avatar_peer.png", make_avatar_png(QStringLiteral("晓"), QColor(0x57, 0x6B, 0x95))};
    files_["avatar-other-01"] = {"avatar_other.png", make_avatar_png(QStringLiteral("杰"), QColor(0xD9, 0x7A, 0x3D))};
  }

  ServerResponse handle(const ClientRequest& req) {
    ServerResponse resp;
    resp.set_request_id(req.request_id());
    resp.set_success(true);
    switch (req.type()) {
      case REQ_TYPE_SEND_SMS_CODE: {
        SendSmsCodeResp r;
        r.set_success(true);
        resp.set_body(r.SerializeAsString());
        break;
      }
      case REQ_TYPE_REGISTER: {
        // 与真实服务端一致：注册返回属于该手机号的 uid，后续 GET_USER_INFO/登录
        // 都回同一 uid（冒烟测试会断言这条链路）
        UserRegisterReq r;
        (void)r.ParseFromString(req.body());
        SelfUid() = "u-" + r.phone();
        UserRegisterResp out;
        out.set_success(true);
        out.set_user_id(SelfUid());
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_LOGIN: {
        UserLoginReq r;
        (void)r.ParseFromString(req.body());
        if (SelfUid().empty()) SelfUid() = "u-" + r.phone();
        UserLoginResp out;
        out.set_success(true);
        out.set_token("mock-token");
        UserInfo me = make_user(SelfUid().c_str(), SelfNick(), "");
        me.set_phone(r.phone());
        *out.mutable_user_info() = me;
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_LOGOUT: {
        UserLogoutResp out;
        out.set_success(true);
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_GET_USER_INFO: {
        GetUserInfoResp out;
        out.set_success(true);
        *out.mutable_user_info() =
            make_user(SelfUid().empty() ? kMyId : SelfUid().c_str(), SelfNick(), "");
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_GET_SESSION_LIST: {
        GetChatSessionListResp r;
        r.set_success(true);
        auto* s1 = r.add_session_list();
        s1->set_chat_session_id(kSingleSid);
        s1->set_type(SESSION_TYPE_SINGLE);
        auto* s2 = r.add_session_list();
        s2->set_chat_session_id(kGroupSid);
        s2->set_type(SESSION_TYPE_GROUP);
        s2->set_session_name(QStringLiteral("开发小组").toStdString());
        s2->set_creator_id(kMyId);
        resp.set_body(r.SerializeAsString());
        break;
      }
      case REQ_TYPE_GET_FRIEND_LIST: {
        GetFriendListResp r;
        r.set_success(true);
        *r.add_friend_list() = make_user(kPeerId, kPeerName, "avatar-peer-01");
        *r.add_friend_list() = make_user(kOtherId, kOtherName, "avatar-other-01");
        resp.set_body(r.SerializeAsString());
        break;
      }
      case REQ_TYPE_GET_PENDING_EVENTS: {
        GetPendingEventsResp r;
        r.set_success(true);
        auto* e = r.add_friend_apply_events();
        e->set_apply_id("apply-01");
        e->set_user_id("u-stranger");
        e->set_peer_id(kMyId);
        e->set_apply_note(QStringLiteral("你好，我是隔壁组的同事").toStdString());
        e->set_status(APPLY_STATUS_PENDING);
        e->set_create_time(now_s() - 3600);
        resp.set_body(r.SerializeAsString());
        break;
      }
      case REQ_TYPE_GET_SESSION_MEMBER: {
        GetSessionMemberReq r;
        (void)r.ParseFromString(req.body());
        GetSessionMemberResp out;
        out.set_success(true);
        auto* m1 = out.add_members();
        m1->set_user_id(SelfUid().empty() ? kMyId : SelfUid());
        m1->set_nickname(kMyName);
        if (r.chat_session_id() == kSingleSid) {
          auto* m2 = out.add_members();
          m2->set_user_id(kPeerId);
          m2->set_nickname(kPeerName);
          m2->set_avatar_file_id("avatar-peer-01");
        } else {
          auto* m2 = out.add_members();
          m2->set_user_id(kPeerId);
          m2->set_nickname(kPeerName);
          m2->set_avatar_file_id("avatar-peer-01");
          auto* m3 = out.add_members();
          m3->set_user_id(kOtherId);
          m3->set_nickname(kOtherName);
          m3->set_avatar_file_id("avatar-other-01");
        }
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_GET_HISTORY: {
        GetHistoryReq r;
        (void)r.ParseFromString(req.body());
        int limit = r.limit() > 0 ? r.limit() : 50;
        const auto& all =
            hist_.count(r.chat_session_id()) ? hist_[r.chat_session_id()] : std::vector<MessageInfo>();
        std::vector<MessageInfo> page;
        for (auto it = all.rbegin(); it != all.rend(); ++it) {  // 从新到旧
          if (r.cursor_timestamp() > 0 && it->create_time() >= r.cursor_timestamp()) continue;
          page.push_back(*it);
          if (static_cast<int>(page.size()) >= limit) break;
        }
        std::reverse(page.begin(), page.end());  // 升序返回
        GetHistoryResp out;
        out.set_success(true);
        for (const auto& m : page) {
          MessageInfo mm = m;
          if (mm.sender_id() == kMyId) mm.set_sender_id(SelfUid().empty() ? kMyId : SelfUid());
          *out.add_messages() = mm;
        }
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_SEARCH_HISTORY: {
        SearchHistoryReq r;
        (void)r.ParseFromString(req.body());
        SearchHistoryResp out;
        out.set_success(true);
        const auto& all =
            hist_.count(r.chat_session_id()) ? hist_[r.chat_session_id()] : std::vector<MessageInfo>();
        const QString kw = QString::fromStdString(r.keyword());
        for (const auto& m : all) {
          if (QString::fromStdString(m.content()).contains(kw)) *out.add_messages() = m;
        }
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_TRANSMIT_MESSAGE: {
        MsgTransmitReq r;
        if (!r.ParseFromString(req.body())) break;
        MessageInfo m;
        m.set_message_id(QString("mock-msg-%1").arg(next_id_++).toStdString());
        m.set_chat_session_id(r.chat_session_id());
        m.set_sender_id(SelfUid().empty() ? kMyId : SelfUid());  // 真实网关按 token 注入
        m.set_type(r.content().type());
        m.set_content(r.content().content());
        m.set_file_id(r.content().file_id());
        m.set_file_name(r.content().file_name());
        m.set_file_size(r.content().file_size());
        m.set_create_time(now_s());
        hist_[r.chat_session_id()].push_back(m);
        pending_push_.push_back(m);

        MsgTransmitResp out;
        out.set_success(true);
        out.set_new_message_id(m.message_id());
        out.set_create_time(m.create_time());
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_PUT_SINGLE_FILE: {
        PutSingleReq r;
        (void)r.ParseFromString(req.body());
        const QString fid = QString("mock-file-%1").arg(next_id_++);
        files_[fid.toStdString()] = {QString::fromStdString(r.data().file_name()),
                                     QByteArray(r.data().file_content().data(),
                                                static_cast<int>(r.data().file_content().size()))};
        PutSingleResp out;
        out.set_success(true);
        out.set_file_id(fid.toStdString());
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_GET_SINGLE_FILE: {
        GetSingleReq r;
        (void)r.ParseFromString(req.body());
        GetSingleResp out;
        auto it = files_.find(r.file_id());
        if (it == files_.end()) {
          out.set_success(false);
          out.set_errmsg("文件不存在（mock）");
        } else {
          out.set_success(true);
          out.mutable_data()->set_file_id(r.file_id());
          out.mutable_data()->set_file_name(it->second.first.toStdString());
          const QByteArray& bytes = it->second.second;
          out.mutable_data()->set_file_content(bytes.constData(),
                                               static_cast<size_t>(bytes.size()));
          out.mutable_data()->set_file_size(bytes.size());
        }
        resp.set_body(out.SerializeAsString());
        break;
      }
      case REQ_TYPE_SEARCH_USER: {
        SearchUserReq r;
        (void)r.ParseFromString(req.body());
        SearchUserResp out;
        out.set_success(true);
        // 关键字匹配预置好友的手机号/昵称，返回结果（真实服务端语义）
        auto add_if_match = [&](const char* id, const char* name, const char* phone,
                                const char* avatar) {
          if (QString::fromStdString(r.keyword()) == phone ||
              QString::fromStdString(name).contains(QString::fromStdString(r.keyword()))) {
            UserInfo u = make_user(id, name, avatar);
            u.set_phone(phone);
            *out.add_result() = u;
          }
        };
        add_if_match(kPeerId, kPeerName, "18353589846", "avatar-peer-01");
        add_if_match(kOtherId, kOtherName, "18553589846", "avatar-other-01");
        resp.set_body(out.SerializeAsString());
        break;
      }
      default: {
        resp.set_success(true);  // 其余类型（加好友等）统一成功，避免 UI 弹错
        break;
      }
    }
    return resp;
  }

  std::vector<MessageInfo> take_pending_push() {
    auto out = pending_push_;
    pending_push_.clear();
    return out;
  }

  std::map<std::string, std::vector<MessageInfo>> hist_;

 private:
  // 本"登录会话"的动态身份（注册/首次登录时按手机号生成，冒烟断言要求
  // GET_USER_INFO.user_id == 注册返回的 uid）
  std::string& SelfUid() { return self_uid_; }
  const char* SelfNick() const { return kMyName; }

  std::map<std::string, std::pair<QString, QByteArray>> files_;
  std::vector<MessageInfo> pending_push_;
  std::string self_uid_;
  int next_id_ = 1;
};

// ---------------- 极简 HTTP 服务（POST /gateway） ----------------
class HttpMock : public QTcpServer {
 public:
  HttpMock(MockState* state, QObject* parent = nullptr) : QTcpServer(parent), state_(state) {
    connect(this, &QTcpServer::newConnection, this, [this]() {
      while (auto* sock = nextPendingConnection()) {
        buffers_[sock] = QByteArray();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { on_read(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
          buffers_.remove(sock);
          sock->deleteLater();
        });
      }
    });
  }

 private:
  // Keep-alive HTTP handling: QNetworkAccessManager pools connections and
  // sends multiple POSTs over one socket; closing per response makes the next
  // request race onto a dying connection ("Connection closed"). So parse and
  // answer request-by-request on the same socket, never closing from our side.
  void on_read(QTcpSocket* sock) {
    QByteArray& buf = buffers_[sock];
    buf.append(sock->readAll());
    while (true) {
      const int hdr_end = buf.indexOf("\r\n\r\n");
      if (hdr_end < 0) return;
      int content_length = 0;
      for (const QByteArray& line : buf.left(hdr_end).split('\n')) {
        if (line.toLower().startsWith("content-length:")) {
          content_length = line.mid(15).trimmed().toInt();
        }
      }
      const int body_start = hdr_end + 4;
      if (buf.size() < body_start + content_length) return;  // 等 body 到齐

      ClientRequest creq;
      ServerResponse resp;
      if (!creq.ParseFromArray(buf.constData() + body_start, content_length)) {
        resp.set_success(false);
        resp.set_errmsg("mock: 请求解析失败");
        qWarning("mock: parse failed len=%d", content_length);
      } else {
        qInfo("mock: req type=%d len=%d", static_cast<int>(creq.type()), content_length);
        resp = state_->handle(creq);
      }
      const std::string ser = resp.SerializeAsString();
      const QByteArray payload(ser.data(), static_cast<int>(ser.size()));
      QByteArray head;
      head += "HTTP/1.1 200 OK\r\n";
      head += "Content-Type: application/x-protobuf\r\n";
      head += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n";
      head += "Connection: keep-alive\r\n\r\n";
      sock->write(head + payload);
      sock->flush();
      buf.remove(0, body_start + content_length);
    }
  }

  MockState* state_;
  QHash<QTcpSocket*, QByteArray> buffers_;
};

// ---------------- WS 推送服务（鉴权 + 广播回推） ----------------
class WsMock : public QObject {
 public:
  WsMock(MockState* state, const QString& demo_sid, QObject* parent = nullptr)
      : QObject(parent), state_(state), server_("mock-ws", QWebSocketServer::NonSecureMode, this) {
    connect(&server_, &QWebSocketServer::newConnection, this, [this]() {
      while (auto* ws = server_.nextPendingConnection()) {
        clients_.append({ws, false});
        connect(ws, &QWebSocket::binaryMessageReceived, this,
                [this, ws](const QByteArray& f) { on_frame(ws, f); });
        connect(ws, &QWebSocket::disconnected, this, [this, ws]() {
          for (int i = 0; i < clients_.size(); ++i) {
            if (clients_[i].first == ws) {
              clients_.removeAt(i);
              break;
            }
          }
          ws->deleteLater();
        });
      }
    });
    if (!demo_sid.isEmpty()) {
      demo_sid_ = demo_sid;
      demo_timer_.setInterval(4000);
      demo_timer_.setSingleShot(true);
      connect(&demo_timer_, &QTimer::timeout, this, [this]() { push_demo(demo_sid_); });
    }
  }

  bool listen(quint16 port) { return server_.listen(QHostAddress::LocalHost, port); }

  // 把 mock 状态里挂起的消息按"广播"语义推给所有已鉴权连接（含发送者自己，客户端去重显示）
  void drain_pending() {
    const auto msgs = state_->take_pending_push();
    for (const auto& m : msgs) {
      ServerPush push;
      push.set_type(PUSH_TYPE_NEW_MESSAGE);
      push.set_body(m.SerializeAsString());
      const QByteArray frame(push.SerializeAsString().data(), push.SerializeAsString().size());
      for (auto& c : clients_) {
        if (c.second) c.first->sendBinaryMessage(frame);
      }
    }
  }

 private:
  void on_frame(QWebSocket* ws, const QByteArray& f) {
    ClientRequest creq;
    if (creq.ParseFromArray(f.constData(), f.size()) && !creq.token().empty()) {
      ServerResponse ok;
      ok.set_request_id(creq.request_id());
      ok.set_success(true);
      const QByteArray payload(ok.SerializeAsString().data(), ok.SerializeAsString().size());
      ws->sendBinaryMessage(payload);
      for (auto& c : clients_) {
        if (c.first == ws) c.second = true;
      }
      // 首次鉴权后 4 秒推一条演示消息（演示未读角标/实时推送）
      if (!demo_sid_.isEmpty() && !demo_started_) {
        demo_started_ = true;
        demo_timer_.start();
      }
      return;
    }
    // 已鉴权后的主动上行消息（客户端只发 ping，不处理）
  }

  void push_demo(const QString& sid) {
    const QByteArray sid_utf8 = sid.toUtf8();
    MessageInfo m = state_->add_text(sid_utf8.constData(), kPeerId,
                                     QStringLiteral("刚到家，明天见～"), now_s());
    state_->hist_[sid_utf8.constData()].push_back(m);
    ServerPush push;
    push.set_type(PUSH_TYPE_NEW_MESSAGE);
    push.set_body(m.SerializeAsString());
    const QByteArray frame(push.SerializeAsString().data(), push.SerializeAsString().size());
    for (auto& c : clients_) {
      if (c.second) c.first->sendBinaryMessage(frame);
    }
    qInfo("mock: pushed demo message to %s", qUtf8Printable(sid));
  }

  MockState* state_;
  QWebSocketServer server_;
  QList<QPair<QWebSocket*, bool>> clients_;  // bool = 已鉴权
  QTimer demo_timer_;
  QString demo_sid_;
  bool demo_started_ = false;
};

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);  // QImage/QPainter 生成演示素材需要 Gui 运行时

  quint16 http_port = 9000, ws_port = 9001;
  QString demo_sid;
  for (int i = 1; i < argc; ++i) {
    const QString a = argv[i];
    if (a == "--http-port" && i + 1 < argc) http_port = QString(argv[++i]).toUShort();
    if (a == "--ws-port" && i + 1 < argc) ws_port = QString(argv[++i]).toUShort();
    if (a == "--push-demo" && i + 1 < argc) demo_sid = argv[++i];
  }

  MockState state;
  HttpMock http(&state);
  if (!http.listen(QHostAddress::LocalHost, http_port)) {
    qCritical("mock: HTTP 端口 %u 监听失败（已被占用？）", http_port);
    return 1;
  }
  WsMock ws(&state, demo_sid);
  if (!ws.listen(ws_port)) {
    qCritical("mock: WS 端口 %u 监听失败（已被占用？）", ws_port);
    return 1;
  }

  // TRANSMIT 的广播回推：100ms 轮询把挂起消息推给 WS（真实系统走 RabbitMQ 广播）
  QTimer drain;
  QObject::connect(&drain, &QTimer::timeout, &ws, [&ws]() { ws.drain_pending(); });
  drain.start(100);

  qInfo("mock gateway ready: http=127.0.0.1:%u ws=127.0.0.1:%u%s", http_port, ws_port,
        demo_sid.isEmpty() ? "" : " (push-demo on)");
  return app.exec();
}
