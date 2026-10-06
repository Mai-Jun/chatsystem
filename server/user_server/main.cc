#include <brpc/server.h>
#include <gflags/gflags.h>
#include <openssl/evp.h>

#include <memory>
#include <random>
#include <string>

#include "channel_manager.hpp"
#include "file.pb.h"
#include "flags.hpp"
#include "logger.hpp"
#include "odb/database.hpp"
#include "odb/entities.hpp"
#include "entities-odb.hxx"
#include "redis_client.hpp"
#include "sms_sender.hpp"
#include "user.pb.h"
#include "util.hpp"

// ============================================================
// 用户子服务：短信验证码 / 注册 / 登录(双方式) / 信息 / 头像
// - 验证码: Redis sms:code:{phone} TTL 300s，60s 限发
// - 登录态: Redis auth:token:{token} = user_id，TTL 7 天
// - 密码:   sha256(password + 盐)
// - 头像:   内部经 etcd 发现调用文件子服务 PutSingle
// ============================================================
using namespace im;  // NOLINT

namespace {

const char* kPasswordSalt = "im-system::v1::salt";
const int kTokenTtlSeconds = 7 * 24 * 3600;

std::string sha256_hex(const std::string& in) {
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  EVP_Digest(in.data(), in.size(), md, &len, EVP_sha256(), nullptr);
  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (unsigned int i = 0; i < len; ++i) {
    out.push_back(hex[md[i] >> 4]);
    out.push_back(hex[md[i] & 0xF]);
  }
  return out;
}

std::string random_code6() {
  static std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(100000, 999999);
  return std::to_string(dist(rng));
}

void fill_user_info(const User& u, UserInfo* info) {
  info->set_user_id(u.id());
  info->set_nickname(u.nickname());
  info->set_description(u.description());
  info->set_phone(u.phone());
  info->set_avatar_file_id(u.avatar_file_id());
}

class UserServiceImpl : public UserService {
 public:
  UserServiceImpl(odb::mysql::database* db, RedisClient* redis, ChannelManager* channels)
      : db_(db), redis_(redis), channels_(channels) {}

  // ---------------- 短信验证码 ----------------
  void SendSmsCode(google::protobuf::RpcController* cntl, const SendSmsCodeReq* req,
                   SendSmsCodeResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    if (req->phone().empty()) {
      resp->set_success(false);
      resp->set_errmsg("手机号为空");
      return;
    }
    if (redis_->exists("sms:limit:" + req->phone())) {
      resp->set_success(false);
      resp->set_errmsg("发送过于频繁，请稍后再试");
      return;
    }
    std::string code = random_code6();
    if (!redis_->setex("sms:code:" + req->phone(), code, 300) ||
        !redis_->setex("sms:limit:" + req->phone(), "1", 60)) {
      resp->set_success(false);
      resp->set_errmsg("验证码存储失败");
      return;
    }
    SmsSender sender;
    if (!sender.send(req->phone(), code)) {
      resp->set_success(false);
      resp->set_errmsg("短信发送失败");
      return;
    }
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 注册 ----------------
  void UserRegister(google::protobuf::RpcController* cntl, const UserRegisterReq* req,
                    UserRegisterResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    if (req->phone().empty() || req->nickname().empty() || req->password().empty()) {
      resp->set_success(false);
      resp->set_errmsg("手机号/昵称/密码不能为空");
      return;
    }
    auto code = redis_->get("sms:code:" + req->phone());
    if (!code || *code != req->sms_code()) {
      resp->set_success(false);
      resp->set_errmsg("验证码错误或已过期");
      return;
    }
    {
      odb::transaction t(db_->begin());
      if (db_->query_one<User>(odb::query<User>::phone == req->phone())) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("手机号已注册");
        return;
      }
      t.commit();
    }
    std::string uid = uuid();
    try {
      odb::transaction t(db_->begin());
      db_->persist(User(uid, req->nickname(), "", req->phone(),
                        sha256_hex(req->password() + kPasswordSalt), req->avatar_file_id()));
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("注册入库失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("注册失败");
      return;
    }
    redis_->del("sms:code:" + req->phone());
    LOG_INFO("注册成功: {} {}", uid, req->phone());
    resp->set_success(true);
    resp->set_errmsg("ok");
    resp->set_user_id(uid);
  }

  // ---------------- 登录 ----------------
  void UserLogin(google::protobuf::RpcController* cntl, const UserLoginReq* req,
                 UserLoginResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    std::unique_ptr<User> user;
    try {
      odb::transaction t(db_->begin());
      user.reset(db_->query_one<User>(odb::query<User>::phone == req->phone()));
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("登录查询失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("登录失败");
      return;
    }
    if (!user) {
      resp->set_success(false);
      resp->set_errmsg("账号不存在");
      return;
    }
    if (req->login_type() == LOGIN_BY_PASSWORD) {
      if (user->password_hash() != sha256_hex(req->password() + kPasswordSalt)) {
        resp->set_success(false);
        resp->set_errmsg("密码错误");
        return;
      }
    } else if (req->login_type() == LOGIN_BY_SMS) {
      auto code = redis_->get("sms:code:" + req->phone());
      if (!code || *code != req->sms_code()) {
        resp->set_success(false);
        resp->set_errmsg("验证码错误或已过期");
        return;
      }
      redis_->del("sms:code:" + req->phone());
    } else {
      resp->set_success(false);
      resp->set_errmsg("未知登录方式");
      return;
    }

    std::string token = uuid();
    if (!redis_->setex("auth:token:" + token, user->id(), kTokenTtlSeconds)) {
      resp->set_success(false);
      resp->set_errmsg("会话写入失败");
      return;
    }
    LOG_INFO("登录成功: {} type={}", user->id(),
             req->login_type() == LOGIN_BY_PASSWORD ? "password" : "sms");
    resp->set_success(true);
    resp->set_errmsg("ok");
    resp->set_token(token);
    fill_user_info(*user, resp->mutable_user_info());
  }

  // ---------------- 登出 ----------------
  void UserLogout(google::protobuf::RpcController* cntl, const UserLogoutReq* req,
                  UserLogoutResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    redis_->del("auth:token:" + req->token());
    resp->set_success(true);
    resp->set_errmsg("ok");
  }

  // ---------------- 获取用户信息 ----------------
  void GetUserInfo(google::protobuf::RpcController* cntl, const GetUserInfoReq* req,
                   GetUserInfoResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    try {
      odb::transaction t(db_->begin());
      auto u = db_->query_one<User>(odb::query<User>::id == req->user_id());
      t.commit();
      if (!u) {
        resp->set_success(false);
        resp->set_errmsg("用户不存在");
        return;
      }
      resp->set_success(true);
      resp->set_errmsg("ok");
      fill_user_info(*u, resp->mutable_user_info());
    } catch (const std::exception& e) {
      LOG_ERROR("用户查询失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("用户查询失败");
    }
  }

  // ---------------- 修改信息 ----------------
  void SetUserInfo(google::protobuf::RpcController* cntl, const SetUserInfoReq* req,
                   SetUserInfoResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    try {
      odb::transaction t(db_->begin());
      std::unique_ptr<User> u(db_->query_one<User>(odb::query<User>::id == req->user_id()));
      if (!u) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("用户不存在");
        return;
      }
      if (!req->nickname().empty()) u->nickname(req->nickname());
      if (!req->description().empty()) u->description(req->description());
      if (!req->phone().empty() && req->phone() != u->phone()) {
        if (db_->query_one<User>(odb::query<User>::phone == req->phone())) {
          t.commit();
          resp->set_success(false);
          resp->set_errmsg("新手机号已被占用");
          return;
        }
        // 换绑手机暂不开放（需短信二次验证，M9 加固）
        resp->set_success(false);
        resp->set_errmsg("换绑手机暂未开放");
        return;
      }
      u->touch();
      db_->update(*u);
      t.commit();
      resp->set_success(true);
      resp->set_errmsg("ok");
    } catch (const std::exception& e) {
      LOG_ERROR("信息修改失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("信息修改失败");
    }
  }

  // ---------------- 修改头像（经文件子服务上传） ----------------
  void SetUserAvatar(google::protobuf::RpcController* cntl, const SetUserAvatarReq* req,
                     SetUserAvatarResp* resp, google::protobuf::Closure* done) override {
    brpc::ClosureGuard guard(done);
    (void)cntl;
    auto* channel = channels_->channel("file_server");
    if (channel == nullptr) {
      resp->set_success(false);
      resp->set_errmsg("文件子服务不可用");
      return;
    }
    // 服务间调用：头像上传
    FileService_Stub file_stub(channel);
    brpc::Controller file_cntl;
    PutSingleReq preq;
    auto* d = preq.mutable_data();
    d->set_file_name(req->user_id() + "_avatar");
    d->set_file_content(req->avatar().file_content());
    PutSingleResp presp;
    file_stub.PutSingle(&file_cntl, &preq, &presp, nullptr);
    if (file_cntl.Failed() || !presp.success()) {
      LOG_ERROR("头像上传失败: {}", file_cntl.ErrorText());
      resp->set_success(false);
      resp->set_errmsg("头像上传失败: " + file_cntl.ErrorText());
      return;
    }
    try {
      odb::transaction t(db_->begin());
      std::unique_ptr<User> u(db_->query_one<User>(odb::query<User>::id == req->user_id()));
      if (!u) {
        t.commit();
        resp->set_success(false);
        resp->set_errmsg("用户不存在");
        return;
      }
      u->avatar_file_id(presp.file_id());
      u->touch();
      db_->update(*u);
      t.commit();
    } catch (const std::exception& e) {
      LOG_ERROR("头像更新失败: {}", e.what());
      resp->set_success(false);
      resp->set_errmsg("头像更新失败");
      return;
    }
    LOG_INFO("头像更新成功: {} -> {}", req->user_id(), presp.file_id());
    resp->set_success(true);
    resp->set_errmsg("ok");
    resp->set_avatar_file_id(presp.file_id());
  }

 private:
  odb::mysql::database* db_;
  RedisClient* redis_;
  ChannelManager* channels_;
};

}  // namespace

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  init_logger(FLAGS_log_dir, FLAGS_service_name, FLAGS_log_level);
  LOG_INFO("{} 启动, 端口 {}", FLAGS_service_name, FLAGS_listen_port);

  auto db = create_db();
  RedisClient redis;
  ChannelManager channels(FLAGS_etcd_endpoints);
  channels.discover("file_server");  // 头像上传需要文件子服务

  ServiceRegistry registry(FLAGS_etcd_endpoints, FLAGS_service_name, FLAGS_instance_id,
                           FLAGS_register_host, FLAGS_listen_port, FLAGS_etcd_lease_ttl,
                           FLAGS_etcd_keepalive_interval);
  if (!registry.start()) {
    LOG_ERROR("注册失败，退出");
    return 1;
  }

  brpc::Server server;
  UserServiceImpl impl(db.get(), &redis, &channels);
  if (server.AddService(&impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
    LOG_ERROR("AddService 失败");
    return 1;
  }
  brpc::ServerOptions options;
  if (server.Start(FLAGS_listen_port, &options) != 0) {
    LOG_ERROR("brpc 启动失败: 端口 {}", FLAGS_listen_port);
    return 1;
  }
  LOG_INFO("{} 就绪: {}", FLAGS_service_name, FLAGS_register_host);
  server.RunUntilAskedToQuit();

  registry.stop();
  LOG_INFO("{} 退出", FLAGS_service_name);
  return 0;
}
