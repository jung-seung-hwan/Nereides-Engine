#pragma once
#include <string_view>
namespace nereides
{
enum class LogLevel { Info, Warning, Error };
class Log final { public: static void Write(LogLevel level, std::string_view message); };
}
