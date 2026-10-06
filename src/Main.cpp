#include "Core/Application.h"
#include "Tests/EngineTests.h"
#include "Core/Log.h"
#include <string_view>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand)
{
    const std::wstring_view arguments(commandLine);
    if (arguments == L"--engine-tests") return nereides::RunEngineTests();
    if (arguments.starts_with(L"--model-test "))
    {
        auto path=arguments.substr(13);
        if(path.size()>=2 && path.front()==L'"' && path.back()==L'"') path=path.substr(1,path.size()-2);
        return nereides::RunModelTest(std::filesystem::path(path));
    }
    nereides::Application application;
    const bool editorTest = arguments == L"--editor-smoke-test";
    const bool sceneTest = arguments == L"--scene-smoke-test" || editorTest;
    try {return application.Run(instance, showCommand, arguments == L"--smoke-test" || sceneTest, sceneTest,editorTest);}
    catch(const std::exception& error){nereides::Log::Write(nereides::LogLevel::Error,error.what());return 1;}
}
