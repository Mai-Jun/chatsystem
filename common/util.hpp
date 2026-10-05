#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ============================================================
// 通用工具：uuid / 时间戳 / base64 / 字符串 / 文件
// ============================================================
namespace im {

// 生成 uuid（小写，36 字符带连字符）
std::string uuid();

// 秒级 / 毫秒级时间戳
int64_t now_seconds();
int64_t now_milliseconds();

// base64（etcd v3 HTTP 接口的 key/value 都是 base64）
std::string base64_encode(const std::string& raw);
std::string base64_decode(const std::string& encoded);

// 字符串
std::vector<std::string> split_string(const std::string& s, const std::string& delim);
bool has_prefix(const std::string& s, const std::string& prefix);

// 文件（二进制）
bool read_file(const std::string& path, std::string* out);
bool write_file(const std::string& path, const std::string& content);
bool is_regular_file(const std::string& path);

}  // namespace im
