#pragma once
#include <string_view>
#include <string>
#include <vector>
namespace nereides
{
enum class LogLevel { Info, Warning, Error };
class Log final
{
public:
    static void Write(LogLevel level, std::string_view message);
    static std::vector<std::string> Recent();
};
}
