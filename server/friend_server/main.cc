#include <brpc/server.h>
#include <gflags/gflags.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "entities-odb.hxx"
#include "flags.hpp"
#include "friend.pb.h"
#include "logger.hpp"
#include "odb/database.hpp"
#include "odb/entities.hpp"
#include "etcd_client.hpp"
#include "util.hpp"

// ============================================================
// 好友子服务：
//   搜索用户 / 好友申请 / 处理申请(同意即建单聊会话) / 删好友 /
//   好友列表 / 待处理事件(好友申请 + 群聊事件) / 建群 / 会话成员 / 会话列表
// 说明：本服务与用户子服务共享 MySQL（微服务共库策略，见 docs/PLAN.md），
//       搜索用户直接查 user 表，避免为查询类接口再造一层 RPC。
// ============================================================
using namespace im;  // NOLINT

namespace {

const int kApplyPending = 0;
const int kApplyAgree = 1;
const int kApplyReject = 2;
const int kSessionSingle = 0;
const int kSessionGroup = 1;

void fill_user_info(const User& u, UserInfo* info) {
  info->set_user_id(u.id());
  info->set_nickname(u.nickname());
  info->set_description(u.description());
  info->set_phone(u.phone());
  info->set_avatar_file_id(u.avatar_file_id());
}

class FriendServiceImpl : public FriendService {
 public:
  explicit FriendServiceImpl(odb::mysql::database* db) : db_(db) {}

  // ---------------- 搜索用户（手机号精确 / 昵称模糊） ----------------
  void SearchUser(google::protobuf::RpcController* cntl, const SearchUserReq* req,
                  SearchUserResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    if (req->keyword().empty()) {
      resp->set_success(false);
      resp->set_errmsg("关键字为空");
      return;
    }
    try {
      odb::transaction t(db_->begin());
      auto rs = db_->query<User>(odb::query<User>::phone == req->keyword() ||
                                 odb::query<User>::nickname.like("%" + req->keyword() + "%"));
      for (auto it = rs.begin(); it != rs.end(); ++it) {
        if (it->id() == req->user_id()) continue;  // 排除自己
        fill_user_info(*it, resp->add_result());
      }
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("搜索用户失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("搜索失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 发送好友申请 ----------------
  void SendFriendApply(google::protobuf::RpcController* cntl, const SendFriendApplyReq* req,
                       SendFriendApplyResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    if (req->user_id() == req->peer_id()) {
      resp->set_success(false);
      resp->set_errmsg("不能添加自己为好友");
      return;
    }
    std::string apply_id = uuid();
    try {
      odb::transaction t(db_->begin());
      // 对方存在？
      if (!db_->query_one<User>(odb::query<User>::id == req->peer_id())) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("用户不存在");
        return;
      }
      // 已是好友？
      if (db_->query_one<FriendRelation>(odb::query<FriendRelation>::user_id == req->user_id() &&
                                         odb::query<FriendRelation>::peer_id == req->peer_id())) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("你们已经是好友");
        return;
      }
      // 已有待处理申请？
      if (db_->query_one<FriendApply>(odb::query<FriendApply>::user_id == req->user_id() &&
                                      odb::query<FriendApply>::peer_id == req->peer_id() &&
                                      odb::query<FriendApply>::status == kApplyPending)) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("已发送过申请，请等待对方处理");
        return;
      }
      db_->persist(FriendApply(apply_id, req->user_id(), req->peer_id(), kApplyPending,
                               req->apply_note()));
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("发送好友申请失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("发送申请失败");
      return;
    }
    LOG_INFO("好友申请: {} -> {}", req->user_id(), req->peer_id());
    resp->set_success(true);
    resp->set_errmsg("ok");
    resp->set_apply_id(apply_id);
  }

  // ---------------- 处理好友申请（同意则建单聊会话） ----------------
  void ProcessFriendApply(google::protobuf::RpcController* cntl, const ProcessFriendApplyReq* req,
                          ProcessFriendApplyResp* resp,
                          google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    if (req->status() != kApplyAgree && req->status() != kApplyReject) {
      resp->set_success(false);
      resp->set_errmsg("非法处理状态");
      return;
    }
    try {
      odb::transaction t(db_->begin());
      std::unique_ptr<FriendApply> apply(
          db_->query_one<FriendApply>(odb::query<FriendApply>::id == req->apply_id()));
      if (!apply) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("申请不存在");
        return;
      }
      if (apply->peer_id() != req->user_id()) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("无权处理该申请");
        return;
      }
      if (apply->status() != kApplyPending) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("申请已处理过");
        return;
      }
      apply->status(req->status());
      apply->touch();
      db_->update(*apply);

      if (req->status() == kApplyAgree) {
        // 双向好友关系
        db_->persist(FriendRelation(uuid(), apply->user_id(), apply->peer_id()));
        db_->persist(FriendRelation(uuid(), apply->peer_id(), apply->user_id()));
        // 单聊会话（同两人已有会话则复用）
        std::string session_id;
        auto members_a = db_->query<ChatSessionMember>(
            odb::query<ChatSessionMember>::user_id == apply->user_id());
        for (auto it = members_a.begin(); it != members_a.end(); ++it) {
          auto peer_member = db_->query_one<ChatSessionMember>(
              odb::query<ChatSessionMember>::session_id == it->session_id() &&
              odb::query<ChatSessionMember>::user_id == apply->peer_id());
          if (!peer_member) continue;
          auto sess = db_->query_one<ChatSession>(
              odb::query<ChatSession>::id == it->session_id() &&
              odb::query<ChatSession>::type == kSessionSingle);
          if (sess) {
            session_id = it->session_id();
            break;
          }
        }
        if (session_id.empty()) {
          session_id = uuid();
          db_->persist(ChatSession(session_id, "", kSessionSingle, ""));
          db_->persist(ChatSessionMember(uuid(), session_id, apply->user_id()));
          db_->persist(ChatSessionMember(uuid(), session_id, apply->peer_id()));
        }
        resp->mutable_session_info()->set_chat_session_id(session_id);
        resp->mutable_session_info()->set_type(SESSION_TYPE_SINGLE);
        LOG_INFO("好友申请通过: {} <-> {}, session={}", apply->user_id(), apply->peer_id(),
                 session_id);
      } else {
        LOG_INFO("好友申请拒绝: {}", req->apply_id());
      }
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("处理好友申请失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("处理失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 删除好友 ----------------
  void DelFriend(google::protobuf::RpcController* cntl, const DelFriendReq* req,
                 DelFriendResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    try {
      odb::transaction t(db_->begin());
      db_->erase_query<FriendRelation>(odb::query<FriendRelation>::user_id == req->user_id() &&
                                       odb::query<FriendRelation>::peer_id == req->peer_id());
      db_->erase_query<FriendRelation>(odb::query<FriendRelation>::user_id == req->peer_id() &&
                                       odb::query<FriendRelation>::peer_id == req->user_id());
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("删除好友失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("删除失败");
      return;
    }
    LOG_INFO("删除好友: {} x {}", req->user_id(), req->peer_id());
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 好友列表 ----------------
  void GetFriendList(google::protobuf::RpcController* cntl, const GetFriendListReq* req,
                     GetFriendListResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    try {
      odb::transaction t(db_->begin());
      auto rs = db_->query<FriendRelation>(odb::query<FriendRelation>::user_id == req->user_id());
      for (auto it = rs.begin(); it != rs.end(); ++it) {
        std::unique_ptr<User> u(db_->query_one<User>(odb::query<User>::id == it->peer_id()));
        if (u) fill_user_info(*u, resp->add_friend_list());
      }
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("好友列表失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("查询失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 待处理事件 ----------------
  // 语义：好友申请返回待处理项；群聊事件返回未读项并标记已读（消费语义）
  void GetPendingEvents(google::protobuf::RpcController* cntl, const GetPendingEventsReq* req,
                        GetPendingEventsResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    try {
      odb::transaction t(db_->begin());
      // 好友申请（我是被申请人且待处理）
      auto applies = db_->query<FriendApply>(odb::query<FriendApply>::peer_id == req->user_id() &&
                                             odb::query<FriendApply>::status == kApplyPending);
      for (auto it = applies.begin(); it != applies.end(); ++it) {
        auto* e = resp->add_friend_apply_events();
        e->set_apply_id(it->id());
        e->set_user_id(it->user_id());
        e->set_peer_id(it->peer_id());
        e->set_status(APPLY_STATUS_PENDING);
        e->set_apply_note(it->apply_note());
        e->set_create_time(it->create_time());
        // 附申请人昵称，便于客户端直接展示
        std::unique_ptr<User> u(db_->query_one<User>(odb::query<User>::id == it->user_id()));
        if (u) e->set_apply_note(it->apply_note().empty() ? u->nickname() : it->apply_note());
      }
      // 群聊事件（未读）
      auto events = db_->query<GroupEvent>(odb::query<GroupEvent>::user_id == req->user_id() &&
                                           odb::query<GroupEvent>::status == 0);
      for (auto it = events.begin(); it != events.end(); ++it) {
        auto* e = resp->add_group_events();
        e->set_event_id(it->id());
        e->set_group_session_id(it->session_id());
        e->set_inviter_id(it->inviter_id());
        e->set_create_time(it->create_time());
        GroupEvent upd = *it;
        upd.status(1);
        db_->update(upd);
      }
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("待处理事件失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("查询失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 创建群聊会话 ----------------
  void CreateGroupSession(google::protobuf::RpcController* cntl, const CreateGroupSessionReq* req,
                          CreateGroupSessionResp* resp,
                          google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    if (req->group_name().empty()) {
      resp->set_success(false);
      resp->set_errmsg("群名称不能为空");
      return;
    }
    std::string session_id = uuid();
    try {
      odb::transaction t(db_->begin());
      db_->persist(ChatSession(session_id, req->group_name(), kSessionGroup, req->user_id()));
      db_->persist(ChatSessionMember(uuid(), session_id, req->user_id()));  // 群主
      for (const auto& mid : req->member_ids()) {
        if (mid == req->user_id()) continue;
        if (!db_->query_one<User>(odb::query<User>::id == mid)) continue;  // 跳过不存在用户
        db_->persist(ChatSessionMember(uuid(), session_id, mid));
        db_->persist(GroupEvent(uuid(), session_id, mid, req->user_id()));
      }
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("建群失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("建群失败");
      return;
    }
    LOG_INFO("建群成功: {} name={} creator={} members={}", session_id, req->group_name(),
             req->user_id(), req->member_ids_size());
    resp->set_success(true);
    resp->set_errmsg("ok");
    resp->mutable_session_info()->set_chat_session_id(session_id);
    resp->mutable_session_info()->set_type(SESSION_TYPE_GROUP);
    resp->mutable_session_info()->set_session_name(req->group_name());
    resp->mutable_session_info()->set_creator_id(req->user_id());
  }

  // ---------------- 会话成员 ----------------
  void GetSessionMember(google::protobuf::RpcController* cntl, const GetSessionMemberReq* req,
                        GetSessionMemberResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    try {
      odb::transaction t(db_->begin());
      auto rs = db_->query<ChatSessionMember>(
          odb::query<ChatSessionMember>::session_id == req->chat_session_id());
      for (auto it = rs.begin(); it != rs.end(); ++it) {
        std::unique_ptr<User> u(db_->query_one<User>(odb::query<User>::id == it->user_id()));
        auto* m = resp->add_members();
        m->set_user_id(it->user_id());
        if (u) {
          m->set_nickname(u->nickname());
          m->set_avatar_file_id(u->avatar_file_id());
        }
      }
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("会话成员失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("查询失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 我的会话列表 ----------------
  void GetChatSessionList(google::protobuf::RpcController* cntl, const GetChatSessionListReq* req,
                          GetChatSessionListResp* resp,
                          google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    try {
      odb::transaction t(db_->begin());
      auto members = db_->query<ChatSessionMember>(
          odb::query<ChatSessionMember>::user_id == req->user_id());
      for (auto it = members.begin(); it != members.end(); ++it) {
        std::unique_ptr<ChatSession> s(
            db_->query_one<ChatSession>(odb::query<ChatSession>::id == it->session_id()));
        if (!s) continue;
        auto* info = resp->add_session_list();
        info->set_chat_session_id(s->id());
        info->set_type(s->type() == kSessionGroup ? SESSION_TYPE_GROUP : SESSION_TYPE_SINGLE);
        info->set_session_name(s->name());
        info->set_creator_id(s->creator_id());
      }
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("会话列表失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("查询失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

 private:
  odb::mysql::database* db_;
};

}  // namespace

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  init_logger(FLAGS_log_dir, FLAGS_service_name, FLAGS_log_level);
  LOG_INFO("{} 启动, 端口 {}", FLAGS_service_name, FLAGS_listen_port);

  auto db = create_db();

  ServiceRegistry registry(FLAGS_etcd_endpoints, FLAGS_service_name, FLAGS_instance_id,
                           FLAGS_register_host, FLAGS_listen_port, FLAGS_etcd_lease_ttl,
                           FLAGS_etcd_keepalive_interval);
  if (!registry.start()) {
    LOG_ERROR("注册失败，退出");
    return 1;
  }

  brpc::Server server;
  FriendServiceImpl impl(db.get());
  if (server.AddService(&impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
    LOG_ERROR("AddService 失败");
    return 1;
  }
  brpc::ServerOptions options;
  if (server.Start(FLAGS_listen_port, &options) != 0) {
    LOG_ERROR("brpc 启动失败: 端口 {}", FLAGS_listen_port);
    return 1;
  }
  LOG_INFO("{} 就绪: {}:{}", FLAGS_service_name, FLAGS_register_host, FLAGS_listen_port);
  server.RunUntilAskedToQuit();

  registry.stop();
  LOG_INFO("{} 退出", FLAGS_service_name);
  return 0;
}
