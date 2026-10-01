#include "Platform/Win32Window.h"
#include "Graphics/D3D11Renderer.h"

#include <chrono>
#include <string_view>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand)
{
    const bool smokeTest = std::wstring_view(commandLine) == L"--smoke-test";
    nereides::Win32Window window;
    if (!window.Create(instance, smokeTest ? SW_HIDE : showCommand)) return 1;
    nereides::D3D11Renderer renderer;
    if (!renderer.Initialize(window.Handle(), window.Width(), window.Height())) return 1;

    const auto start = std::chrono::steady_clock::now();
    int exitCode = 0;
    while (window.ProcessMessages(exitCode))
    {
        if (window.IsMinimized() || window.Width() == 0 || window.Height() == 0)
        {
            WaitMessage();
            continue;
        }
        if (!renderer.Resize(window.Width(), window.Height()) || !renderer.Render())
            return 1;
        if (smokeTest && std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(500))
            PostMessageW(window.Handle(), WM_CLOSE, 0, 0);
    }
    return exitCode;
}
