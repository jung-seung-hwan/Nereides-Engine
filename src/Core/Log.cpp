#include "Core/Log.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
namespace nereides
{
namespace {std::mutex logMutex;std::vector<std::string> recent;}
void Log::Write(LogLevel level, std::string_view message)
{
    const std::lock_guard lock(logMutex);
    const char* label = level == LogLevel::Error ? "error" : level == LogLevel::Warning ? "warning" : "info";
    const std::string line = "[" + std::string(label) + "] " + std::string(message) + "\n";
    OutputDebugStringA(line.c_str());
    recent.push_back(line);if(recent.size()>100)recent.erase(recent.begin());
    std::error_code error;
    std::filesystem::create_directories("logs", error);
    std::ofstream output("logs/engine.log", std::ios::app);
    output << line;
}
std::vector<std::string> Log::Recent(){const std::lock_guard lock(logMutex);return recent;}
}
