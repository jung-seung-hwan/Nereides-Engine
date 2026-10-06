#include "Core/Log.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
namespace nereides
{
void Log::Write(LogLevel level, std::string_view message)
{
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    const char* label = level == LogLevel::Error ? "error" : level == LogLevel::Warning ? "warning" : "info";
    const std::string line = "[" + std::string(label) + "] " + std::string(message) + "\n";
    OutputDebugStringA(line.c_str());
    std::error_code error;
    std::filesystem::create_directories("logs", error);
    std::ofstream output("logs/engine.log", std::ios::app);
    output << line;
}
}
