#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>
#include <vector>

// ============================================================
// 日志封装：每个进程初始化一次，控制台 + 滚动文件双写
// 用法: im::init_logger(FLAGS_log_dir, FLAGS_service_name, FLAGS_log_level);
//       LOG_INFO("xxx: {}", val); LOG_ERROR(...); LOG_DEBUG(...);
// ============================================================
namespace im {

inline void init_logger(const std::string& log_dir, const std::string& log_name,
                        const std::string& level) {
  std::vector<spdlog::sink_ptr> sinks;
  sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      log_dir + "/" + log_name + ".log", 10 * 1024 * 1024, 5));

  auto logger = std::make_shared<spdlog::logger>(log_name, sinks.begin(), sinks.end());
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l][%t] %v");

  spdlog::level::level_enum lv = spdlog::level::info;
  if (level == "debug") lv = spdlog::level::debug;
  else if (level == "warn") lv = spdlog::level::warn;
  else if (level == "error") lv = spdlog::level::err;
  logger->set_level(lv);
  logger->flush_on(spdlog::level::warn);

  spdlog::set_default_logger(logger);
  spdlog::set_level(lv);
  spdlog::flush_every(std::chrono::seconds(1));
}

}  // namespace im

#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
