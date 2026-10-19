#include "RHI/Compat/SherlockLog.h"
#include "Core/Log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace
{
void Forward(nereides::LogLevel level, const char* fmt, va_list args)
{
    char buffer[2048];
    va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(buffer, sizeof(buffer), fmt, copy);
    va_end(copy);
    if (length < 0)
    {
        nereides::Log::Write(level, fmt);
        return;
    }
    if (static_cast<size_t>(length) < sizeof(buffer))
    {
        nereides::Log::Write(level, std::string_view(buffer, static_cast<size_t>(length)));
        return;
    }
    std::string text(static_cast<size_t>(length) + 1, '\0');
    std::vsnprintf(text.data(), text.size(), fmt, args);
    text.resize(static_cast<size_t>(length));
    nereides::Log::Write(level, text);
}
} // namespace

namespace Log
{
// Nereides Log에는 Debug 레벨이 없어 Info로 보낸다.
void Debug(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Forward(nereides::LogLevel::Info, fmt, args);
    va_end(args);
}

void Info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Forward(nereides::LogLevel::Info, fmt, args);
    va_end(args);
}

void Warn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Forward(nereides::LogLevel::Warning, fmt, args);
    va_end(args);
}

void Error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Forward(nereides::LogLevel::Error, fmt, args);
    va_end(args);
}

std::string ToUtf8(const wchar_t* wide)
{
    if (!wide || !*wide)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::string HrToString(HRESULT hr)
{
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(hr));
    std::string result = code;
    if (length > 0 && message)
    {
        std::wstring text(message, length);
        while (!text.empty() &&
               (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
            text.pop_back();
        result += " (" + ToUtf8(text.c_str()) + ")";
    }
    if (message)
        LocalFree(message);
    return result;
}
} // namespace Log
