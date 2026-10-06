#pragma once
#include <string>

#include "flags.hpp"
#include "logger.hpp"

// ============================================================
// 短信发送抽象
// - 开发模式（未配置阿里云 AccessKey）：验证码只打进日志，直接成功
// - 正式模式：AliyunSmsSender 真实下发（M9/M10 接入，接口已留好）
// ============================================================
namespace im {

class SmsSender {
 public:
  // 返回 true 表示已发送（或开发模式下模拟发送）
  bool send(const std::string& phone, const std::string& code) const {
    if (FLAGS_aliyun_sms_access_key_id.empty() || FLAGS_aliyun_sms_access_key_secret.empty()) {
      // 开发模式：固定码由 redis 校验逻辑兜底，这里只记录
      LOG_INFO("【模拟短信】phone={} 验证码={}", phone, code);
      return true;
    }
    // TODO(M9): 阿里云短信 REST 下发（签名+模板参数），密钥经 conf/*.local.flags 注入
    LOG_ERROR("阿里云短信尚未接入，发送失败 phone={}", phone);
    return false;
  }
};

}  // namespace im
