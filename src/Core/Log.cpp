#include "Core/Log.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <deque>
#include <cstdio>
namespace nereides
{
namespace
{
std::mutex logMutex;
std::deque<LogEntry> recent;
std::uint64_t nextId = 1;
} // namespace
const char* Log::Label(LogLevel level)
{
    return level == LogLevel::Error ? "Error" : level == LogLevel::Warning ? "Warning" : "Info";
}
std::string Log::Format(const LogEntry& entry)
{
    return entry.time + " [" + Label(entry.level) + "] " + entry.message;
}
std::uint64_t Log::Write(LogLevel level, std::string_view message)
{
    const std::lock_guard lock(logMutex);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char timestamp[40]{};
    std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u.%03u", now.wYear,
                  now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    LogEntry entry{nextId++, level, timestamp, std::string(message)};
    const auto id = entry.id;
    const auto line = Format(entry) + "\n";
    OutputDebugStringA(line.c_str());
    recent.push_back(std::move(entry));
    if (recent.size() > 1000)
        recent.pop_front();
    std::error_code error;
    std::filesystem::create_directories("logs", error);
    std::ofstream output("logs/engine.log", std::ios::app);
    output << line;
    return id;
}
std::vector<std::string> Log::Recent()
{
    const std::lock_guard lock(logMutex);
    std::vector<std::string> result;
    for (const auto& entry : recent)
        result.push_back(Format(entry));
    return result;
}
std::vector<LogEntry> Log::Entries()
{
    const std::lock_guard lock(logMutex);
    return {recent.begin(), recent.end()};
}
} // namespace nereides
