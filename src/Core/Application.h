#pragma once
#include "Core/Input.h"
#include "Core/Time.h"
#include "Platform/Win32Window.h"
#include "Graphics/D3D11Renderer.h"
namespace nereides
{
class Application final
{
public:
    int Run(HINSTANCE instance, int showCommand, bool smokeTest);
private:
    // Reverse destruction releases GPU resources before the window.
    Input m_input;
    Time m_time;
    Win32Window m_window;
    D3D11Renderer m_renderer;
};
}
