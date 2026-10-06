#include "Core/Application.h"
#include "Tests/EngineTests.h"
#include "Core/Log.h"
#include <string_view>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand)
{
    // Resolve project/data relative paths consistently for VS, Explorer, and packaged runs.
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (length > 0 && length < 32768)
    {
        auto directory = std::filesystem::path(executable).parent_path();
        for (auto candidate = directory; !candidate.empty(); candidate = candidate.parent_path())
        {
            if (std::filesystem::exists(candidate / L"NereidesEngine.sln") ||
                std::filesystem::exists(candidate / L"data/presentation/preview.json"))
            {
                std::filesystem::current_path(candidate);
                break;
            }
            if (candidate == candidate.parent_path())
                break;
        }
    }
    const std::wstring_view arguments(commandLine);
    if (arguments == L"--engine-tests")
        return nereides::RunEngineTests();
    if (arguments.starts_with(L"--model-test "))
    {
        auto path = arguments.substr(13);
        if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
            path = path.substr(1, path.size() - 2);
        return nereides::RunModelTest(std::filesystem::path(path));
    }
    nereides::Application application;
    nereides::RunOptions options;
    if (!arguments.empty())
    {
        options.automatic = true;
        options.editor = false;
        if (arguments == L"--smoke-test")
            options.legacyTriangle = true;
        else if (arguments == L"--scene-smoke-test")
        {
        }
        else if (arguments == L"--editor-smoke-test")
            options.editor = true;
        else if (arguments == L"--presentation-smoke-test")
        {
            options.editor = true;
            options.preview = true;
        }
        else if (arguments == L"--resize-smoke-test")
            options.resize = true;
        else if (arguments == L"--benchmark")
            options.benchmark = true;
        else
        {
            nereides::Log::Write(nereides::LogLevel::Error, "Unknown command line option");
            return 2;
        }
    }
    try
    {
        return application.Run(instance, showCommand, options);
    }
    catch (const std::exception& error)
    {
        nereides::Log::Write(nereides::LogLevel::Error, error.what());
        return 1;
    }
}
