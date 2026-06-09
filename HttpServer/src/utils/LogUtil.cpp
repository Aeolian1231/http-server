#include "../../include/utils/LogUtil.h"

#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <iostream>

namespace http
{
namespace utils
{

LogUtil& LogUtil::instance()
{
    static LogUtil inst;
    return inst;
}

LogUtil::~LogUtil()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open())
    {
        log(LogLevel::INFO, "LogUtil.cpp", 0, "~LogUtil",
            "Log file closed, server shutting down");
        file_.close();
    }
}

void LogUtil::setLogFile(const std::string& filepath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open())
    {
        file_.close();
    }
    file_.open(filepath, std::ios::out | std::ios::app);
    if (!file_.is_open())
    {
        std::cerr << "[LogUtil] Failed to open log file: " << filepath
                  << " (" << std::strerror(errno) << ")" << std::endl;
        return;
    }
    // 写入启动分隔行
    file_ << "\n"
          << "========== Server Start " << getTimestamp()
          << " ==========" << std::endl;
}

void LogUtil::setLogLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

void LogUtil::log(LogLevel level, const char* file, int line, const char* func,
                  const std::string& message)
{
    if (level < level_)
        return;

    // 从完整路径中提取文件名
    const char* basename = file;
    for (const char* p = file; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
            basename = p + 1;
    }

    std::ostringstream oss;
    oss << "[" << getTimestamp() << "] "
        << "[" << levelToString(level) << "] "
        << "[" << basename << ":" << line << "] "
        << "[" << func << "] "
        << message;

    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open())
    {
        file_ << oss.str() << std::endl;
        file_.flush();
    }
}

std::string LogUtil::levelToString(LogLevel level) const
{
    switch (level)
    {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "UNKN ";
}

std::string LogUtil::getTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_r(&time_t, &tm);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long long>(ms.count()));
    return buf;
}

} // namespace utils
} // namespace http
