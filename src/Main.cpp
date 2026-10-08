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
    if (arguments == L"--editor-workflow-test")
        return nereides::RunEditorWorkflowTest();
    if (arguments.starts_with(L"--model-test "))
    {
        auto path = arguments.substr(13);
        if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
            path = path.substr(1, path.size() - 2);
        return nereides::RunModelTest(std::filesystem::path(path));
    }

    // 실행관리자
    nereides::Application application;

    // 실행 모드 설정
    /*
    일반 에디터	        editor = true	                    에디터를 열고 사용자가 종료할 때까지 실행
    장면 출력 검사	    automatic = true, editor = false	에디터 없이 장면을 검사하고 자동 종료
    창 크기 변경 검사  	resize = true	                    검사 중 창 크기를 바꿔 출력 확인
    성능 측정	        benchmark = true	                정해진 구간의 프레임 시간 등을 측정
    */
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
        else if (arguments == L"--editor-resize-smoke-test")
        {
            options.resize = true;
            options.editor = true;
        }
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
