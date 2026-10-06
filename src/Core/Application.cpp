#include "Core/Application.h"
#include "Core/Log.h"
#include "Sandbox/MoveComponent.h"
#include <chrono>
#include <string>
namespace nereides
{
int Application::Run(HINSTANCE instance, int showCommand, bool smokeTest, bool sceneTest)
{
    m_window.SetInput(&m_input);
    if (!m_window.Create(instance, smokeTest ? SW_HIDE : showCommand)) return 1;
    if (!m_renderer.Initialize(m_window.Handle(), m_window.Width(), m_window.Height())) return 1;
    Log::Write(LogLevel::Info, "Application initialized");
    auto& player = m_scene.Create("Example player");
    m_player = player.Id();
    player.Add<MoveComponent>();
    auto cube = MakeCube();
    player.transform.position = {0,1,0};
    player.Add<MeshComponent>(cube).tint = {.85f,.65f,.3f,1};
    player.Add<Collider>().mask=2;
    auto& target=m_scene.Create("Contact example");target.transform.position={2,1,0};
    target.Add<MeshComponent>(cube).tint={.65f,.25f,.3f,1};
    auto& collider=target.Add<Collider>();collider.layer=2;collider.mask=1;collider.trigger=true;
    auto& ground = m_scene.Create("Temporary deck");
    ground.transform.scale = {8,.25f,6};
    ground.transform.position = {0,-.4f,1};
    ground.Add<MeshComponent>(cube).tint = {.12f,.35f,.4f,1};
    auto& camera = m_scene.Create("Example camera");
    m_camera = camera.Id(); camera.transform.position = {0,1,-6};
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
        const auto contacts=m_collision.Step(m_scene,m_time.combat.elapsed-m_time.combat.delta,m_time.combat.elapsed);
        for(const auto& contact:contacts) if(contact.kind==ContactKind::Enter)
            Log::Write(LogLevel::Info,"Trigger entered at "+std::to_string(contact.time));
        if (!m_renderer.Resize(m_window.Width(), m_window.Height())) return 1;
        if (smokeTest && !sceneTest)
        {
            if (!m_renderer.Render(!frameVerified)) return 1;
        }
        else
        {
            const auto frame = CollectRenderFrame(m_scene,m_camera,m_cameraData,
                float(m_window.Width())/float(m_window.Height()),m_time.combat.elapsed);
            if (!m_renderer.RenderScene(frame,sceneTest && !frameVerified)) return 1;
        }
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
