#include "Core/Application.h"
#include "Tests/EngineTests.h"
#include <string_view>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand)
{
    const std::wstring_view arguments(commandLine);
    if (arguments == L"--engine-tests") return nereides::RunEngineTests();
    nereides::Application application;
    const bool sceneTest = arguments == L"--scene-smoke-test";
    return application.Run(instance, showCommand, arguments == L"--smoke-test" || sceneTest, sceneTest);
}
