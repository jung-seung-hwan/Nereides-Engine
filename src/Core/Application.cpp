#include "Core/Application.h"
#include "Core/Log.h"
#include "Sandbox/MoveComponent.h"
#include <chrono>
#include <string>
namespace nereides
{
int Application::Run(HINSTANCE instance, int showCommand, bool smokeTest)
{
    m_window.SetInput(&m_input);
    if (!m_window.Create(instance, smokeTest ? SW_HIDE : showCommand)) return 1;
    if (!m_renderer.Initialize(m_window.Handle(), m_window.Width(), m_window.Height())) return 1;
    Log::Write(LogLevel::Info, "Application initialized");
    auto& player = m_scene.Create("Example player");
    m_player = player.Id();
    player.Add<MoveComponent>();
    auto previous = std::chrono::steady_clock::now();
    int exitCode = 0;
    bool frameVerified = false;
    while (true)
    {
        m_input.BeginFrame();
        if (!m_window.ProcessMessages(exitCode)) break;
        const auto now = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (m_window.IsMinimized() || m_window.Width() == 0 || m_window.Height() == 0)
        {
            WaitMessage(); previous = std::chrono::steady_clock::now(); continue;
        }
        if (m_input.Pressed(VK_ESCAPE))
        {
            m_time.paused = !m_time.paused; m_input.DiscardHeld();
            Log::Write(LogLevel::Info, m_time.paused ? "Paused" : "Resumed");
        }
        const bool menuPause = m_time.paused;
        m_time.paused = menuPause || (!smokeTest && !m_window.IsFocused());
        m_time.Advance(delta); m_time.paused = menuPause;
        m_scene.Update({m_input, m_time});
        if (!m_renderer.Resize(m_window.Width(), m_window.Height()) ||
            !m_renderer.Render(smokeTest && !frameVerified)) return 1;
        frameVerified = true;
        if (!smokeTest)
        {
            const auto title = L"Nereides | combat " + std::to_wstring(m_time.combat.elapsed) +
                L" | x " + std::to_wstring(m_scene.Find(m_player)->transform.position.x) + L" | WASD / Esc pause";
            SetWindowTextW(m_window.Handle(), title.c_str());
        }
        if (smokeTest && m_time.real.elapsed >= 0.5) PostMessageW(m_window.Handle(), WM_CLOSE, 0, 0);
    }
    Log::Write(LogLevel::Info, "Application stopped"); return exitCode;
}
}
