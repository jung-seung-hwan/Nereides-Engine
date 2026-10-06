#include "Core/Application.h"
#include "Core/Log.h"
#include "Sandbox/MoveComponent.h"
#include <chrono>
#include <string>
#include <algorithm>
#include <numeric>
#include <psapi.h>
namespace nereides
{
int Application::Run(HINSTANCE instance, int showCommand, const RunOptions& options)
{
    const bool smokeTest = options.automatic,
               sceneTest = options.automatic && !options.legacyTriangle;
    const bool editorTest = options.automatic && options.editor, presentationTest = options.preview;
    m_window.SetInput(&m_input);
    if (!m_window.Create(instance, smokeTest ? SW_HIDE : showCommand))
        return 1;
    if (!m_renderer.Initialize(m_window.Handle(), m_window.Width(), m_window.Height()))
        return 1;
    Log::Write(LogLevel::Info, "Application initialized");
    auto& player = m_scene.Create("Example player");
    m_player = player.Id();
    player.Add<MoveComponent>();
    auto cube = MakeCube();
    player.transform.position = {0, 1, 0};
    player.Add<MeshComponent>(cube).tint = {.85f, .65f, .3f, 1};
    player.Add<Collider>().mask = 2;
    auto& target = m_scene.Create("Contact example");
    target.transform.position = {2, 1, 0};
    target.Add<MeshComponent>(cube).tint = {.65f, .25f, .3f, 1};
    auto& collider = target.Add<Collider>();
    collider.layer = 2;
    collider.mask = 1;
    collider.trigger = true;
    auto& ground = m_scene.Create("Temporary deck");
    ground.transform.scale = {8, .25f, 6};
    ground.transform.position = {0, -.4f, 1};
    ground.Add<MeshComponent>(cube).tint = {.12f, .35f, .4f, 1};
    auto& camera = m_scene.Create("Example camera");
    m_camera = camera.Id();
    camera.transform.position = {0, 1, -6};
    auto& parameters = target.Add<Parameters>();
    parameters.values["Example weakpoint duration"] = {5, 0.1, 60};
    const bool editorEnabled = options.editor;
    if (editorEnabled)
    {
        if (!m_editor.Initialize(m_window.Handle(), m_renderer))
            return 1;
        if (!smokeTest)
            m_window.SetCloseGuard([this] { return m_editor.RequestClose(); });
        m_window.SetMessageHandler(
            [this](HWND w, UINT m, WPARAM p, LPARAM l) { m_editor.Message(w, m, p, l); });
        m_renderer.SetOverlay([this] { m_editor.Render(); });
        if (presentationTest)
            m_editor.StartPreview();
    }
    auto previous = std::chrono::steady_clock::now();
    int exitCode = 0;
    bool frameVerified = false;
    std::vector<double> frameMilliseconds;
    while (true)
    {
        const auto frameStart = std::chrono::steady_clock::now();
        m_input.BeginFrame();
        if (!m_window.ProcessMessages(exitCode))
            break;
        const auto now = std::chrono::steady_clock::now();
        const double delta =
            presentationTest ? 1. / 60 : std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (m_window.IsMinimized() || m_window.Width() == 0 || m_window.Height() == 0)
        {
            WaitMessage();
            previous = std::chrono::steady_clock::now();
            continue;
        }
        if (editorEnabled &&
            m_editor.Begin(m_scene, m_camera, m_player, m_cameraData, m_time, m_input, m_collision))
        {
            m_collision.Reset();
            m_input.DiscardHeld();
        }
        if (m_input.Pressed(VK_ESCAPE) && (!editorEnabled || m_editor.Playing()))
        {
            m_time.paused = !m_time.paused;
            m_input.DiscardHeld();
            Log::Write(LogLevel::Info, m_time.paused ? "Paused" : "Resumed");
        }
        const bool menuPause = m_time.paused;
        const bool presentationBlocked = editorEnabled && m_editor.BlocksCombat();
        if (presentationBlocked != m_time.combatStopped || presentationBlocked)
            m_input.DiscardHeld();
        m_time.combatStopped = presentationBlocked;
        m_time.paused = menuPause || (!smokeTest && !m_window.IsFocused());
        m_time.Advance(delta);
        m_time.paused = menuPause;
        if (editorEnabled)
            m_editor.AdvancePresentation(m_time.presentation.delta);
        if (!editorEnabled || m_editor.Playing())
            m_scene.Update({m_input, m_time});
        std::vector<Contact> contacts;
        if (m_time.combat.delta > 0)
            contacts = m_collision.Step(m_scene, m_time.combat.elapsed - m_time.combat.delta,
                                        m_time.combat.elapsed);
        else
            m_collision.Reset();
        for (const auto& contact : contacts)
            if (contact.kind == ContactKind::Enter)
                Log::Write(LogLevel::Info, "Trigger entered at " + std::to_string(contact.time));
        if (editorEnabled)
            m_editor.DrawDebug(m_scene, m_camera, m_cameraData, m_collision);
        if (editorTest && m_time.frame == 3)
            m_renderer.RequestCapture("captures/editor.bmp");
        if (presentationTest && m_time.frame == 30)
            m_renderer.RequestCapture("captures/presentation.bmp");
        if (!m_renderer.Resize(m_window.Width(), m_window.Height()))
            return 1;
        if (smokeTest && !sceneTest)
        {
            if (!m_renderer.Render(!frameVerified))
                return 1;
        }
        else
        {
            auto frame = CollectRenderFrame(m_scene, m_camera, m_cameraData,
                                            float(m_window.Width()) / float(m_window.Height()),
                                            m_time.combat.elapsed);
            if (editorEnabled)
                m_editor.ApplyView(frame);
            const bool verifyPixels =
                sceneTest && !presentationTest &&
                (!frameVerified || (options.resize && (m_time.frame == 4 || m_time.frame == 7)));
            if (!m_renderer.RenderScene(frame, verifyPixels))
                return 1;
        }
        frameVerified = true;
        if (options.resize && (m_time.frame == 2 || m_time.frame == 5))
        {
            RECT bounds{0, 0, m_time.frame == 2 ? 900 : 1280, m_time.frame == 2 ? 600 : 720};
            if (!AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0) ||
                !SetWindowPos(m_window.Handle(), nullptr, 0, 0, bounds.right - bounds.left,
                              bounds.bottom - bounds.top,
                              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
                return 1;
        }
        if (options.resize && m_time.frame == 4 &&
            (m_window.Width() != 900 || m_window.Height() != 600))
            return 1;
        if (options.resize && m_time.frame == 7 &&
            (m_window.Width() != 1280 || m_window.Height() != 720))
            return 1;
        if (options.benchmark && m_time.frame > 30)
            frameMilliseconds.push_back(std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - frameStart)
                                            .count());
        if (!smokeTest)
        {
            const auto title = L"Nereides | combat " + std::to_wstring(m_time.combat.elapsed) +
                               L" | WASD / Esc pause";
            SetWindowTextW(m_window.Handle(), title.c_str());
        }
        if (smokeTest && (options.benchmark ? m_time.frame >= 210
                                            : m_time.real.elapsed >= .5 &&
                                                  (!options.resize || m_time.frame >= 8)))
            PostMessageW(m_window.Handle(), WM_CLOSE, 0, 0);
    }
    if (options.benchmark && !frameMilliseconds.empty())
    {
        const double average =
            std::accumulate(frameMilliseconds.begin(), frameMilliseconds.end(), 0.) /
            frameMilliseconds.size();
        std::sort(frameMilliseconds.begin(), frameMilliseconds.end());
        PROCESS_MEMORY_COUNTERS memory{};
        memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
        Log::Write(
            LogLevel::Info,
            "Benchmark sandbox 1280x720 (wall frame incl Present; not GPU time), samples=" +
                std::to_string(frameMilliseconds.size()) + " mean_ms=" + std::to_string(average) +
                " p95_ms=" +
                std::to_string(frameMilliseconds[frameMilliseconds.size() * 95 / 100]) +
                " working_set_MiB=" + std::to_string(double(memory.WorkingSetSize) / 1048576));
    }
    m_renderer.SetOverlay({});
    m_window.SetMessageHandler({});
    Log::Write(LogLevel::Info, "Application stopped");
    return exitCode;
}
} // namespace nereides
