// M4 模拟客户端：走网关跑通 注册→登录→查信息→改信息→文件上传下载 全链路
// 仅在服务器上运行（读取验证码需要访问 redis 容器）
#include <httplib.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "gateway.pb.h"
#include "logger.hpp"
#include "user.pb.h"
#include "file.pb.h"
#include "util.hpp"

using namespace im;  // NOLINT

namespace {

const char* kGateway = "http://127.0.0.1:9000";

ServerResponse send(const ClientRequest& creq) {
  httplib::Client cli("127.0.0.1", 9000);
  auto res = cli.Post("/gateway", creq.SerializeAsString(), "application/x-protobuf");
  ServerResponse sresp;
  if (!res || !sresp.ParseFromString(res->body)) {
    sresp.set_success(false);
    sresp.set_errmsg("网关无响应");
  }
  return sresp;
}

ClientRequest make_req(RequestType t, const std::string& token,
                       const google::protobuf::MessageLite& body) {
  ClientRequest r;
  r.set_request_id(uuid());
  r.set_type(t);
  r.set_token(token);
  r.set_body(body.SerializeAsString());
  return r;
}

std::string read_code_from_redis(const std::string& phone) {
  std::string cmd = "docker exec im-redis redis-cli get 'sms:code:" + phone + "'";
  FILE* f = popen(cmd.c_str(), "r");
  if (f == nullptr) return "";
  char buf[64] = {0};
  if (fgets(buf, sizeof(buf), f) == nullptr) {
    pclose(f);
    return "";
  }
  pclose(f);
  std::string code(buf);
  while (!code.empty() && (code.back() == '\n' || code.back() == '\r')) code.pop_back();
  return code;
}

}  // namespace

int main() {
  init_logger("/tmp", "gateway_sim", "info");

  // 唯一手机号（时间戳后 8 位），避免重复注册
  std::string phone = "199" + std::to_string(now_milliseconds()).substr(5);

  // 1. 发送验证码（走网关）
  SendSmsCodeReq sms_req;
  sms_req.set_phone(phone);
  ClientRequest c1 = make_req(REQ_TYPE_SEND_SMS_CODE, "", sms_req);
  auto r1 = send(c1);
  if (!r1.success()) {
    printf("[FAIL] 发送验证码: %s\n", r1.errmsg().c_str());
    return 1;
  }
  printf("[PASS] 发送验证码\n");

  // 2. 取验证码并注册
  std::string code = read_code_from_redis(phone);
  if (code.empty()) {
    printf("[FAIL] 读取验证码失败\n");
    return 1;
  }
  UserRegisterReq reg_req;
  reg_req.set_phone(phone);
  reg_req.set_sms_code(code);
  reg_req.set_nickname("网关模拟用户");
  reg_req.set_password("pass123");
  auto r2 = send(make_req(REQ_TYPE_REGISTER, "", reg_req));
  UserRegisterResp reg_resp;
  if (!r2.success() || !reg_resp.ParseFromString(r2.body())) {
    printf("[FAIL] 注册: %s\n", r2.errmsg().c_str());
    return 1;
  }
  printf("[PASS] 注册 user_id=%s\n", reg_resp.user_id().c_str());

  // 3. 登录（密码）
  UserLoginReq login_req;
  login_req.set_phone(phone);
  login_req.set_login_type(LOGIN_BY_PASSWORD);
  login_req.set_password("pass123");
  auto r3 = send(make_req(REQ_TYPE_LOGIN, "", login_req));
  UserLoginResp login_resp;
  if (!r3.success() || !login_resp.ParseFromString(r3.body()) || login_resp.token().empty()) {
    printf("[FAIL] 登录: %s\n", r3.errmsg().c_str());
    return 1;
  }
  printf("[PASS] 登录 token=%s\n", login_resp.token().c_str());

  // 4. 查信息（token 鉴权，user_id 由网关注入）
  GetUserInfoReq info_req;  // user_id 留空，由网关注入
  auto r4 = send(make_req(REQ_TYPE_GET_USER_INFO, login_resp.token(), info_req));
  GetUserInfoResp info_resp;
  if (!r4.success() || !info_resp.ParseFromString(r4.body()) ||
      info_resp.user_info().user_id() != reg_resp.user_id()) {
    printf("[FAIL] 查信息: %s\n", r4.errmsg().c_str());
    return 1;
  }
  printf("[PASS] 查信息 nickname=%s\n", info_resp.user_info().nickname().c_str());

  // 5. 改信息
  SetUserInfoReq set_req;
  set_req.set_nickname("网关改");
  auto r5 = send(make_req(REQ_TYPE_SET_USER_INFO, login_resp.token(), set_req));
  if (!r5.success()) {
    printf("[FAIL] 改信息: %s\n", r5.errmsg().c_str());
    return 1;
  }
  printf("[PASS] 改信息\n");

  // 6. 文件上传（走网关）
  PutSingleReq put_req;
  put_req.mutable_data()->set_file_name("sim.txt");
  put_req.mutable_data()->set_file_content("gateway-sim-content");
  auto r6 = send(make_req(REQ_TYPE_PUT_SINGLE_FILE, login_resp.token(), put_req));
  PutSingleResp put_resp;
  if (!r6.success() || !put_resp.ParseFromString(r6.body())) {
    printf("[FAIL] 文件上传: %s\n", r6.errmsg().c_str());
    return 1;
  }
  printf("[PASS] 文件上传 file_id=%s\n", put_resp.file_id().c_str());

  // 7. 文件下载
  GetSingleReq get_req;
  get_req.set_file_id(put_resp.file_id());
  auto r7 = send(make_req(REQ_TYPE_GET_SINGLE_FILE, login_resp.token(), get_req));
  GetSingleResp get_resp;
  if (!r7.success() || !get_resp.ParseFromString(r7.body()) ||
      get_resp.data().file_content() != "gateway-sim-content") {
    printf("[FAIL] 文件下载: %s\n", r7.errmsg().c_str());
    return 1;
  }
  printf("[PASS] 文件下载\n");

  // 8. 未登录访问被拒
  GetUserInfoReq anon_req;
  auto r8 = send(make_req(REQ_TYPE_GET_USER_INFO, "fake-token", anon_req));
  if (r8.success() || r8.errmsg().find("未登录") == std::string::npos) {
    printf("[FAIL] 鉴权拦截: %s\n", r8.errmsg().c_str());
    return 1;
  }
  printf("[PASS] 鉴权拦截（伪造 token 被拒）\n");

  printf("\n=== 模拟客户端全链路 8/8 通过 ===\n");
  return 0;
}
