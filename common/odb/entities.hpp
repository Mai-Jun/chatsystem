#pragma once
#include <string>
#include <cstdint>

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <odb/core.hxx>
#include <odb/boost/date-time/odb/boost-date-time.hxx>

// ============================================================
// ODB 实体定义 —— 与 sql/im_system.sql 的 7 张表一一对应
// 表已由 sql 脚本创建（ odb 不负责建表，--generate-query 模式）
// 时间字段统一使用 boost::posix_time::ptime 映射 DATETIME
// ============================================================

namespace im {

// 用户表
#pragma db object table("user")
class User {
 public:
  User() = default;
  User(std::string id, std::string nickname, std::string description, std::string phone,
       std::string password_hash, std::string avatar_file_id)
      : id_(std::move(id)),
        nickname_(std::move(nickname)),
        description_(std::move(description)),
        phone_(std::move(phone)),
        password_hash_(std::move(password_hash)),
        avatar_file_id_(std::move(avatar_file_id)) {
    create_time_ = boost::posix_time::microsec_clock::universal_time();
    update_time_ = create_time_;
  }

  const std::string& id() const { return id_; }
  const std::string& nickname() const { return nickname_; }
  void nickname(const std::string& v) { nickname_ = v; }
  const std::string& description() const { return description_; }
  void description(const std::string& v) { description_ = v; }
  const std::string& phone() const { return phone_; }
  const std::string& password_hash() const { return password_hash_; }
  void password_hash(const std::string& v) { password_hash_ = v; }
  const std::string& avatar_file_id() const { return avatar_file_id_; }
  void avatar_file_id(const std::string& v) { avatar_file_id_ = v; }
  const boost::posix_time::ptime& create_time() const { return create_time_; }
  const boost::posix_time::ptime& update_time() const { return update_time_; }
  void touch() { update_time_ = boost::posix_time::microsec_clock::universal_time(); }

 private:
  friend class odb::access;

#pragma db id
  std::string id_;
#pragma db column("nickname")
  std::string nickname_;
#pragma db column("description")
  std::string description_;
#pragma db column("phone") unique
  std::string phone_;
#pragma db column("password_hash")
  std::string password_hash_;
#pragma db column("avatar_file_id")
  std::string avatar_file_id_;
#pragma db type("DATETIME") column("create_time")
  boost::posix_time::ptime create_time_;
#pragma db type("DATETIME") column("update_time")
  boost::posix_time::ptime update_time_;
};

// 好友申请表
#pragma db object table("friend_apply")
class FriendApply {
 public:
  FriendApply() = default;
  FriendApply(std::string id, std::string user_id, std::string peer_id, int status,
              std::string apply_note)
      : id_(std::move(id)),
        user_id_(std::move(user_id)),
        peer_id_(std::move(peer_id)),
        status_(status),
        apply_note_(std::move(apply_note)) {
    create_time_ = boost::posix_time::microsec_clock::universal_time();
    update_time_ = create_time_;
  }

  const std::string& id() const { return id_; }
  const std::string& user_id() const { return user_id_; }
  const std::string& peer_id() const { return peer_id_; }
  int status() const { return status_; }
  void status(int v) { status_ = v; }
  const std::string& apply_note() const { return apply_note_; }
  const boost::posix_time::ptime& create_time() const { return create_time_; }
  const boost::posix_time::ptime& update_time() const { return update_time_; }
  void touch() { update_time_ = boost::posix_time::microsec_clock::universal_time(); }

 private:
  friend class odb::access;

#pragma db id
  std::string id_;
#pragma db column("user_id")
  std::string user_id_;
#pragma db column("peer_id")
  std::string peer_id_;
#pragma db column("status")
  int status_;
#pragma db column("apply_note")
  std::string apply_note_;
#pragma db type("DATETIME") column("create_time")
  boost::posix_time::ptime create_time_;
#pragma db type("DATETIME") column("update_time")
  boost::posix_time::ptime update_time_;
};

// 好友关系表（互为好友写两行）
#pragma db object table("friend_relation")
class FriendRelation {
 public:
  FriendRelation() = default;
  FriendRelation(std::string id, std::string user_id, std::string peer_id)
      : id_(std::move(id)), user_id_(std::move(user_id)), peer_id_(std::move(peer_id)) {
    create_time_ = boost::posix_time::microsec_clock::universal_time();
  }

  const std::string& id() const { return id_; }
  const std::string& user_id() const { return user_id_; }
  const std::string& peer_id() const { return peer_id_; }
  const boost::posix_time::ptime& create_time() const { return create_time_; }

 private:
  friend class odb::access;

#pragma db id
  std::string id_;
#pragma db column("user_id")
  std::string user_id_;
#pragma db column("peer_id")
  std::string peer_id_;
#pragma db type("DATETIME") column("create_time")
  boost::posix_time::ptime create_time_;
};

// 会话表
#pragma db object table("chat_session")
class ChatSession {
 public:
  ChatSession() = default;
  ChatSession(std::string id, std::string name, int type, std::string creator_id)
      : id_(std::move(id)), name_(std::move(name)), type_(type), creator_id_(std::move(creator_id)) {
    create_time_ = boost::posix_time::microsec_clock::universal_time();
  }

  const std::string& id() const { return id_; }
  const std::string& name() const { return name_; }
  int type() const { return type_; }
  const std::string& creator_id() const { return creator_id_; }
  const boost::posix_time::ptime& create_time() const { return create_time_; }

 private:
  friend class odb::access;

#pragma db id
  std::string id_;
#pragma db column("name")
  std::string name_;
#pragma db column("type")
  int type_;
#pragma db column("creator_id")
  std::string creator_id_;
#pragma db type("DATETIME") column("create_time")
  boost::posix_time::ptime create_time_;
};

// 会话成员表
#pragma db object table("chat_session_member")
class ChatSessionMember {
 public:
  ChatSessionMember() = default;
  ChatSessionMember(std::string id, std::string session_id, std::string user_id)
      : id_(std::move(id)), session_id_(std::move(session_id)), user_id_(std::move(user_id)) {
    create_time_ = boost::posix_time::microsec_clock::universal_time();
  }

  const std::string& id() const { return id_; }
  const std::string& session_id() const { return session_id_; }
  const std::string& user_id() const { return user_id_; }
  const boost::posix_time::ptime& create_time() const { return create_time_; }

 private:
  friend class odb::access;

#pragma db id
  std::string id_;
#pragma db column("session_id")
  std::string session_id_;
#pragma db column("user_id")
  std::string user_id_;
#pragma db type("DATETIME") column("create_time")
  boost::posix_time::ptime create_time_;
};

// 消息表（MySQL 权威存储）
#pragma db object table("message")
class Message {
 public:
  Message() = default;
  Message(std::string id, std::string session_id, std::string sender_id, int type,
          std::string content, std::string file_id, std::string file_name, int64_t file_size,
          std::string asr_text)
      : id_(std::move(id)),
        session_id_(std::move(session_id)),
        sender_id_(std::move(sender_id)),
        type_(type),
        content_(std::move(content)),
        file_id_(std::move(file_id)),
        file_name_(std::move(file_name)),
        file_size_(file_size),
        asr_text_(std::move(asr_text)) {
    create_time_ = boost::posix_time::microsec_clock::universal_time();
  }

  const std::string& id() const { return id_; }
  const std::string& session_id() const { return session_id_; }
  const std::string& sender_id() const { return sender_id_; }
  int type() const { return type_; }
  const std::string& content() const { return content_; }
  void content(const std::string& v) { content_ = v; }
  const std::string& file_id() const { return file_id_; }
  const std::string& file_name() const { return file_name_; }
  int64_t file_size() const { return file_size_; }
  const std::string& asr_text() const { return asr_text_; }
  void asr_text(const std::string& v) { asr_text_ = v; }
  const boost::posix_time::ptime& create_time() const { return create_time_; }

 private:
  friend class odb::access;

#pragma db id
  std::string id_;
#pragma db column("session_id")
  std::string session_id_;
#pragma db column("sender_id")
  std::string sender_id_;
#pragma db column("type")
  int type_;
#pragma db column("content") type("TEXT")
  std::string content_;
#pragma db column("file_id")
  std::string file_id_;
#pragma db column("file_name")
  std::string file_name_;
#pragma db column("file_size")
  int64_t file_size_;
#pragma db column("asr_text") type("TEXT")
  std::string asr_text_;
#pragma db type("DATETIME") column("create_time")
  boost::posix_time::ptime create_time_;
};

// 文件元数据表（实体存本地磁盘）
#pragma db object table("file")
class FileMeta {
 public:
  FileMeta() = default;
  FileMeta(std::string id, std::string file_name, int64_t file_size, std::string file_path)
      : id_(std::move(id)),
        file_name_(std::move(file_name)),
        file_size_(file_size),
        file_path_(std::move(file_path)) {
    create_time_ = boost::posix_time::microsec_clock::universal_time();
  }

  const std::string& id() const { return id_; }
  const std::string& file_name() const { return file_name_; }
  int64_t file_size() const { return file_size_; }
  const std::string& file_path() const { return file_path_; }
  const boost::posix_time::ptime& create_time() const { return create_time_; }

 private:
  friend class odb::access;

#pragma db id
  std::string id_;
#pragma db column("file_name")
  std::string file_name_;
#pragma db column("file_size")
  int64_t file_size_;
#pragma db column("file_path")
  std::string file_path_;
#pragma db type("DATETIME") column("create_time")
  boost::posix_time::ptime create_time_;
};

}  // namespace im
