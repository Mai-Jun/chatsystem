#include "util.hpp"

#include <uuid/uuid.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <mutex>
#include <cctype>
#include <cstring>
#include <cstdint>

namespace im {

std::string uuid() {
  uuid_t uid;
  uuid_generate_random(uid);
  char buf[64] = {0};
  uuid_unparse_lower(uid, buf);
  return std::string(buf);
}

int64_t now_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t now_milliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string now_datetime_str() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32] = {0};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return std::string(buf);
}

// ---- base64 ----
namespace {
const char kB64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64_encode(const std::string& raw) {
  std::string out;
  out.reserve((raw.size() + 2) / 3 * 4);
  int val = 0, valb = -6;
  for (unsigned char c : raw) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(kB64Table[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(kB64Table[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

std::string base64_decode(const std::string& encoded) {
  std::string out;
  out.reserve(encoded.size() / 4 * 3);
  int val = 0, valb = -8;
  for (unsigned char c : encoded) {
    if (c == '=') break;
    if (std::isspace(c)) continue;
    const char* pos = std::strchr(kB64Table, c);
    if (pos == nullptr) continue;
    val = (val << 6) + static_cast<int>(pos - kB64Table);
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

std::vector<std::string> split_string(const std::string& s, const std::string& delim) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < s.size()) {
    size_t pos = s.find(delim, start);
    if (pos == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    if (pos > start) out.push_back(s.substr(start, pos - start));
    start = pos + delim.size();
  }
  return out;
}

bool has_prefix(const std::string& s, const std::string& prefix) {
  return s.rfind(prefix, 0) == 0;
}

bool read_file(const std::string& path, std::string* out) {
  if (out == nullptr) return false;
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

bool write_file(const std::string& path, const std::string& content) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) return false;
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
  return f.good();
}

bool is_regular_file(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}

}  // namespace im
