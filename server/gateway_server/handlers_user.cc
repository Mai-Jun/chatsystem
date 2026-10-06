#include "dispatch.hpp"
#include "user.pb.h"

// 用户子服务分发（本 TU 仅含 user.pb.h + dispatch.hpp）
namespace im {
namespace gateway {

bool dispatch_user(const Ctx& ctx, Result* out) {
  switch (ctx.type) {
    case REQ_TYPE_SEND_SMS_CODE:
      forward<SendSmsCodeReq, SendSmsCodeResp, UserService_Stub>(
          ctx, &UserService_Stub::SendSmsCode, out);
      return true;
    case REQ_TYPE_REGISTER:
      forward<UserRegisterReq, UserRegisterResp, UserService_Stub>(
          ctx, &UserService_Stub::UserRegister, out);
      return true;
    case REQ_TYPE_LOGIN:
      forward<UserLoginReq, UserLoginResp, UserService_Stub>(
          ctx, &UserService_Stub::UserLogin, out);
      return true;
    case REQ_TYPE_LOGOUT: {
      // token 来自外壳（不是 body）
      UserLogoutReq req;
      if (ctx.token != nullptr) req.set_token(*ctx.token);
      UserService_Stub stub(ctx.channel);
      brpc::Controller cntl;
      UserLogoutResp resp;
      stub.UserLogout(&cntl, &req, &resp, nullptr);
      if (cntl.Failed()) {
        out->success = false;
        out->errmsg = cntl.ErrorText();
      } else {
        out->success = resp.success();
        out->errmsg = resp.errmsg();
        out->body = resp.SerializeAsString();
      }
      return true;
    }
    case REQ_TYPE_GET_USER_INFO:
      forward<GetUserInfoReq, GetUserInfoResp, UserService_Stub>(
          ctx, &UserService_Stub::GetUserInfo, out);
      return true;
    case REQ_TYPE_SET_USER_INFO:
      forward<SetUserInfoReq, SetUserInfoResp, UserService_Stub>(
          ctx, &UserService_Stub::SetUserInfo, out);
      return true;
    case REQ_TYPE_SET_USER_AVATAR:
      forward<SetUserAvatarReq, SetUserAvatarResp, UserService_Stub>(
          ctx, &UserService_Stub::SetUserAvatar, out);
      return true;
    default:
      return false;
  }
}

}  // namespace gateway
}  // namespace im
