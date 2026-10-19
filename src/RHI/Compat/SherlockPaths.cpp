#include "RHI/Compat/SherlockPaths.h"
#include <windows.h>
#include <filesystem>

namespace
{
std::filesystem::path ExeDir()
{
    wchar_t path[MAX_PATH * 2] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(std::wstring(path, length)).parent_path();
}

const std::wstring& ShaderDir()
{
    static const std::wstring dir = (ExeDir() / L"Shaders").wstring() + L"\\";
    return dir;
}

#if defined(_DEBUG)
// 소스 트리의 Shaders\ . 실행 파일(bin\x64\<구성>\) 위쪽으로 올라가며 프로젝트 파일이 있는 폴더를 찾음.
// 못 찾으면 빈 문자열 (배포 환경).
const std::wstring& SourceShaderDir()
{
    static const std::wstring dir = [] {
        std::error_code error;
        for (auto folder = ExeDir(); folder.has_parent_path(); folder = folder.parent_path())
        {
            if (std::filesystem::exists(folder / L"NereidesSandbox.vcxproj", error) &&
                std::filesystem::exists(folder / L"Shaders", error))
                return (folder / L"Shaders").wstring() + L"\\";
            if (folder == folder.parent_path())
                break;
        }
        return std::wstring();
    }();
    return dir;
}
#endif
} // namespace

namespace Paths
{
std::wstring GetShaderPath(const wchar_t* fileName)
{
    return ShaderDir() + fileName;
}

std::wstring GetShaderBinaryPath(const wchar_t* fileName)
{
    return ShaderDir() + fileName;
}

std::wstring GetShaderSourcePath(const wchar_t* fileName)
{
#if defined(_DEBUG)
    // Debug: 소스 트리의 원본을 우선함. 핫리로드가 저장소의 파일 수정을 바로 봄.
    if (!SourceShaderDir().empty())
        return SourceShaderDir() + fileName;
#endif
    return ShaderDir() + fileName;
}
} // namespace Paths
