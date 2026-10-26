#pragma once
#include <string>
#include <winerror.h>

// Sherlock "Core/Log.h" 호환 어댑터. RHI 소스의 Log::Error/Warn/Info 호출을 그대로 유지하기 위한 것이다.
// Nereides의 nereides::Log(src/Core/Log.h)와 API가 달라 그쪽은 수정하지 않고, 여기서 Write로 전달한다.
// 전역 namespace Log는 nereides::Log와 이름이 같지만, nereides 안에서는 안쪽 이름이 우선하므로 충돌하지 않는다.
namespace Log
{
void Debug(const char* fmt, ...);
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);

// "0x80070002 (설명)" 형태. Sherlock과 같은 형식.
std::string HrToString(HRESULT hr);

// 넓은 문자열 → UTF-8.
std::string ToUtf8(const wchar_t* wide);
} // namespace Log
