#pragma once
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>
namespace nereides
{
enum class LogLevel
{
    Info,
    Warning,
    Error
};
struct LogEntry
{
    std::uint64_t id = 0;
    LogLevel level = LogLevel::Info;
    std::string time, message;
};
class Log final
{
public:
    static std::uint64_t Write(LogLevel level, std::string_view message);
    static std::vector<std::string> Recent();
    static std::vector<LogEntry> Entries();
    static const char* Label(LogLevel level);
    static std::string Format(const LogEntry& entry);
};
} // namespace nereides
