#pragma once
#include <httplib.h>

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>

#include "flags.hpp"
#include "logger.hpp"
#include "util.hpp"

// ============================================================
// 百度云短语音识别 REST API 客户端（直连 HTTP，无 SDK 依赖）
//   ① API Key / Secret Key 换 access_token（缓存至过期前，约 30 天）
//   ② 原始音频 POST 到 https://vop.baidu.com/server_api，解析 result
// 文档: https://ai.baidu.com/ai-doc/SPEECH/Vlbnde6et
// 约束: 单段音频 ≤60s / ≤10MB；需要 httplib 的 OpenSSL 支持
//       （根 CMakeLists 已全局定义 CPPHTTPLIB_OPENSSL_SUPPORT）。
// ============================================================
namespace im {

// 密钥是否已配置（未配置时上层走开发模式）
inline bool baidu_keys_configured() {
  return !FLAGS_baidu_api_key.empty() && !FLAGS_baidu_secret_key.empty();
}

// 解析 WAV 头的采样率（非 WAV 或解析失败返回 0）
inline int wav_sample_rate(const std::string& wav) {
  if (wav.size() < 44 || wav.compare(0, 4, "RIFF") != 0 || wav.compare(8, 4, "WAVE") != 0) {
    return 0;
  }
  // fmt 块 SampleRate 位于偏移 24（小端 4 字节）
  return static_cast<unsigned char>(wav[24]) | (static_cast<unsigned char>(wav[25]) << 8) |
         (static_cast<unsigned char>(wav[26]) << 16) | (static_cast<unsigned char>(wav[27]) << 24);
}

// 极简 JSON 取值（百度响应结构固定，无需引入 JSON 库）
namespace json_mini {

// 定位 "key": 之后的位置（精确匹配键名；找不到返回 nullptr）
inline const char* find_key(const std::string& j, const std::string& key) {
  const std::string pat = "\"" + key + "\"";
  size_t pos = 0;
  while ((pos = j.find(pat, pos)) != std::string::npos) {
    size_t p = pos + pat.size();
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\r' || j[p] == '\n')) ++p;
    if (p < j.size() && j[p] == ':') return j.c_str() + p + 1;
    pos += pat.size();
  }
  return nullptr;
}

inline void skip_ws(const std::string& j, size_t* i) {
  while (*i < j.size() && (j[*i] == ' ' || j[*i] == '\t' || j[*i] == '\r' || j[*i] == '\n')) ++(*i);
}

// 从位置 i 解析 JSON 字符串（含转义与 \uXXXX→UTF-8），成功时 next 指向结束引号后
inline bool string_at(const std::string& j, size_t i, std::string* out, size_t* next) {
  skip_ws(j, &i);
  if (i >= j.size() || j[i] != '"') return false;
  ++i;
  out->clear();
  while (i < j.size()) {
    char c = j[i++];
    if (c == '"') {
      *next = i;
      return true;
    }
    if (c == '\\' && i < j.size()) {
      char e = j[i++];
      switch (e) {
        case 'n': out->push_back('\n'); break;
        case 't': out->push_back('\t'); break;
        case 'r': out->push_back('\r'); break;
        case 'b': out->push_back('\b'); break;
        case 'f': out->push_back('\f'); break;
        case '"':
        case '\\':
        case '/': out->push_back(e); break;
        case 'u': {
          if (i + 4 > j.size()) return false;
          int cp = std::strtol(j.substr(i, 4).c_str(), nullptr, 16);
          i += 4;
          if (cp < 0x80) {
            out->push_back(static_cast<char>(cp));
          } else if (cp < 0x800) {
            out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
          } else {
            out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
          }
          break;
        }
        default: out->push_back(e); break;
      }
    } else {
      out->push_back(c);
    }
  }
  return false;  // 未闭合
}

inline bool json_string(const std::string& j, const std::string& key, std::string* out) {
  const char* p = find_key(j, key);
  if (p == nullptr) return false;
  size_t next = 0;
  return string_at(j, p - j.c_str(), out, &next);
}

inline bool json_int(const std::string& j, const std::string& key, long long* out) {
  const char* p = find_key(j, key);
  if (p == nullptr) return false;
  char* end = nullptr;
  long long v = std::strtoll(p, &end, 10);
  if (end == p) return false;
  *out = v;
  return true;
}

// 取字符串数组的第一个元素（百度 result 字段）
inline bool json_array_first(const std::string& j, const std::string& key, std::string* out) {
  const char* p = find_key(j, key);
  if (p == nullptr) return false;
  size_t i = p - j.c_str();
  skip_ws(j, &i);
  if (i >= j.size() || j[i] != '[') return false;
  ++i;
  skip_ws(j, &i);
  if (i < j.size() && j[i] == ']') return false;  // 空数组
  size_t next = 0;
  return string_at(j, i, out, &next);
}

}  // namespace json_mini

class BaiduAsrClient {
 public:
  // 识别一段音频。format 如 "wav"/"pcm"/"amr"/"m4a"；成功时 text 填转写文本。
  bool recognize(const std::string& audio, const std::string& format, std::string* text,
                 std::string* errmsg) {
    if (audio.empty()) {
      *errmsg = "音频内容为空";
      return false;
    }
    if (audio.size() > 10 * 1024 * 1024) {
      *errmsg = "音频超过短语音 10MB 上限";
      return false;
    }
    if (!baidu_keys_configured()) {
      *errmsg = "未配置百度密钥";
      return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (token_.empty() || now_seconds() >= token_expire_at_ - 60) {
      if (!fetch_token_locked(errmsg)) return false;
    }

    // 采样率：wav 从头里读真实值，其余按 16k（百度默认）
    int rate = 16000;
    if (format == "wav") {
      int r = wav_sample_rate(audio);
      if (r == 8000 || r == 16000) rate = r;
    }
    const std::string content_type =
        "audio/" + format + ";rate=" + std::to_string(rate);

    httplib::Client cli("https://vop.baidu.com");
    cli.set_connection_timeout(5);
    cli.set_read_timeout(20);
    const std::string path = "/server_api?dev_pid=" + std::to_string(FLAGS_baidu_dev_pid) +
                             "&cuid=" + uuid() + "&token=" + token_;
    auto res = cli.Post(path, audio, content_type.c_str());
    if (!res) {
      *errmsg = "连接百度识别服务失败: " + std::string(httplib::to_string(res.error()));
      return false;
    }
    if (res->status != 200) {
      *errmsg = "百度识别 HTTP " + std::to_string(res->status) + ": " + res->body.substr(0, 200);
      return false;
    }

    long long err_no = -1;
    json_mini::json_int(res->body, "err_no", &err_no);
    if (err_no != 0) {
      std::string emsg;
      json_mini::json_string(res->body, "err_msg", &emsg);
      // token 失效/过期：清缓存，下次调用重新获取
      if (err_no == 110 || err_no == 111) token_.clear();
      *errmsg = "百度识别错误 " + std::to_string(err_no) + ": " + emsg;
      return false;
    }
    if (!json_mini::json_array_first(res->body, "result", text)) {
      *errmsg = "百度响应缺少 result: " + res->body.substr(0, 200);
      return false;
    }
    return true;
  }

 private:
  // 获取并缓存 access_token（调用方需已持锁）
  bool fetch_token_locked(std::string* errmsg) {
    httplib::Client cli("https://aip.baidubce.com");
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);
    const std::string path =
        "/oauth/2.0/token?grant_type=client_credentials&client_id=" + FLAGS_baidu_api_key +
        "&client_secret=" + FLAGS_baidu_secret_key;
    auto res = cli.Post(path, "", "application/x-www-form-urlencoded");
    if (!res) {
      *errmsg = "获取 access_token 失败: " + std::string(httplib::to_string(res.error()));
      return false;
    }
    if (res->status != 200) {
      *errmsg = "获取 access_token HTTP " + std::to_string(res->status) + ": " +
                res->body.substr(0, 200);
      return false;
    }
    std::string tk;
    if (!json_mini::json_string(res->body, "access_token", &tk)) {
      *errmsg = "access_token 解析失败: " + res->body.substr(0, 200);
      return false;
    }
    long long expires = 2592000;  // 默认 30 天，以响应为准
    json_mini::json_int(res->body, "expires_in", &expires);
    token_ = tk;
    token_expire_at_ = now_seconds() + expires;
    LOG_INFO("百度 access_token 已刷新, 有效期 {} 秒", expires);
    return true;
  }

  std::mutex mutex_;
  std::string token_;
  int64_t token_expire_at_ = 0;  // 秒级时间戳
};

}  // namespace im
