#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace http
{
namespace utils
{

enum class LogLevel
{
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

// 线程安全的文件日志工具类（单例）
// 日志格式：[2026-06-09 10:30:45.123] [WARN ] [file.cpp:123] [func] message
class LogUtil
{
public:
    static LogUtil& instance();

    void setLogFile(const std::string& filepath);
    void setLogLevel(LogLevel level);

    void log(LogLevel level, const char* file, int line, const char* func,
             const std::string& message);

private:
    LogUtil() = default;
    ~LogUtil();

    std::string levelToString(LogLevel level) const;
    std::string getTimestamp() const;

    std::ofstream file_;
    std::mutex    mutex_;
    LogLevel      level_ = LogLevel::INFO;
};

} // namespace utils
} // namespace http

// 便捷宏，自动捕获 __FILE__ / __LINE__ / __FUNCTION__
#define LOG_UTIL_DEBUG(msg)                                                     \
    do {                                                                       \
        std::ostringstream _log_oss_;                                          \
        _log_oss_ << msg;                                                      \
        ::http::utils::LogUtil::instance().log(                                \
            ::http::utils::LogLevel::DEBUG, __FILE__, __LINE__,                \
            __FUNCTION__, _log_oss_.str());                                    \
    } while (0)

#define LOG_UTIL_INFO(msg)                                                     \
    do {                                                                       \
        std::ostringstream _log_oss_;                                          \
        _log_oss_ << msg;                                                      \
        ::http::utils::LogUtil::instance().log(                                \
            ::http::utils::LogLevel::INFO, __FILE__, __LINE__,                 \
            __FUNCTION__, _log_oss_.str());                                    \
    } while (0)

#define LOG_UTIL_WARN(msg)                                                     \
    do {                                                                       \
        std::ostringstream _log_oss_;                                          \
        _log_oss_ << msg;                                                      \
        ::http::utils::LogUtil::instance().log(                                \
            ::http::utils::LogLevel::WARN, __FILE__, __LINE__,                 \
            __FUNCTION__, _log_oss_.str());                                    \
    } while (0)

#define LOG_UTIL_ERROR(msg)                                                    \
    do {                                                                       \
        std::ostringstream _log_oss_;                                          \
        _log_oss_ << msg;                                                      \
        ::http::utils::LogUtil::instance().log(                                \
            ::http::utils::LogLevel::ERROR, __FILE__, __LINE__,                \
            __FUNCTION__, _log_oss_.str());                                    \
    } while (0)
