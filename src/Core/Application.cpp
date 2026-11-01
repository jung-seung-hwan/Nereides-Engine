#include "Core/Application.h"
#include "Core/Log.h"
#include "Sandbox/MoveComponent.h"
#include <chrono>
#include <string>
#include <algorithm>
#include <numeric>
#include <cstdlib>
#include <string_view>
#include <psapi.h>

namespace nereides
{
// 콘텐츠의 피해·파괴 우선순위는 이 결과 처리 지점에 연결한다.
// 계산이 끝난 충돌 목록을 받아 게임 규칙을 적용, 결과는 반환하지 않고 장면이나 이벤트에 직접 반영 (현재는 트리거 진입 로그만 출력)
void Application::ResolveContacts(const std::vector<Contact>& contacts)
{
    for (const auto& contact : contacts)
        if (contact.kind == ContactKind::Enter)
            Log::Write(LogLevel::Info, "tick=" + std::to_string(m_time.tick) +
                                           " Trigger entered at " + std::to_string(contact.time));
}


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

    // 성능 측정에서 NEREIDES_VSYNC=0 이면 수직 동기화를 꺼 프레임 비용을 그대로 잼
    // NEREIDES_VSYNC=1이면 수직 동기화를 사용해서 화면 주사율과 게임 fps를 동기화
    if (options.benchmark)
    {
        char vsync[8] = {};
        size_t length = 0;
        if (getenv_s(&length, vsync, "NEREIDES_VSYNC") == 0 && std::string_view(vsync) == "0")
            m_renderer.SetVSync(false);
    }

    Log::Write(LogLevel::Info, "Application initialized");

    // 에디터 예제 등록 (객체에 컴포넌트를 추가하고, 장면에 배치)
    auto& player = m_scene.Create("Example player");
    m_player = player.Id();
    player.Add<MoveComponent>();
    auto cube = MakeCube();
    player.transform.position = {0, 1, 0};
    player.Add<MeshComponent>(cube).tint = {.85f, .65f, .3f, 1};
    player.Add<Collider>().mask = 2;

    // 충돌 트리거 테스트용
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
    camera.transform.position = {0, 1, -6};
    camera.Add<CameraComponent>();
    auto& parameters = target.Add<Parameters>();
    parameters.values["Example weakpoint duration"] = {5, 0.1, 60};

    // 에디터 초기화
    const bool editorEnabled = options.editor;
    if (editorEnabled)
    {
        // ImGui 생성 후 창과 렌더러의 GPU에 연결
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

    // 장면 실행
    if (!editorEnabled)
        m_scene.BeginPlay();

    // 경과 시간 측정
    auto previous = std::chrono::steady_clock::now();       // 직전 프레임 시각 (delta time 계산용)
    int exitCode = 0;                                       // WM_QUIT 저장용
    bool frameVerified = false;                             // 첫프레임 픽셀 검증 여부
    std::vector<double> frameMilliseconds;                  // 벤치마크용 프레임 시간 기록
    FixedStep fixed;                                        // 프레임별로 tick을 몇 번 계산할지
    bool wasSimulating = false;                             // 상태 변경 체크
    bool missingCameraReported = false;                     // 카메라 없음 경고 중복 방지

    // 프로그램 실행 루프
    while (true)
    {
        // 반복 시간 측정용
        const auto frameStart = std::chrono::steady_clock::now();
        m_input.BeginFrame();
        // os 메세지 처리 - 키 입력 -> m_input, 크기 변경 -> 창 크기. WM_QUIT -> 루프 break
        if (!m_window.ProcessMessages(exitCode))
            break;
        const auto now = std::chrono::steady_clock::now();

        // 이번 프레임의 시간 간격 계산 (초 단위)
        const double delta =
            presentationTest ? 1. / 60 : std::chrono::duration<double>(now - previous).count();
        previous = now;

        // 창 최소화 처리
        if (m_window.IsMinimized() || m_window.Width() == 0 || m_window.Height() == 0)
        {
            fixed.Reset();
            wasSimulating = false;
            m_input.DiscardHeld();

            // 최소화 시 메시지 올 때까지 스레드 대기
            WaitMessage();

            // 대기 시간 제외하기 위해 기준 시간 초기화
            previous = std::chrono::steady_clock::now();
            continue;
        }

        // 에디터 활성화 시, 에디터 프레임 처리
        const bool wasPlaying = editorEnabled && m_editor.Playing();
        bool sceneReplaced = false;
        if (editorEnabled &&
            m_editor.Begin(m_scene, m_player, m_time, m_input, m_collision))
        {
            sceneReplaced = true;
            m_events.Reset();
            m_collision.Reset();
            m_input.DiscardHeld();
        }

        // 게임 일시정지 처리 (ESC 키)
        if (m_input.Pressed(VK_ESCAPE) && (!editorEnabled || m_editor.Playing()))
        {
            m_time.paused = !m_time.paused;
            m_input.DiscardHeld();
            Log::Write(LogLevel::Info, m_time.paused ? "Paused" : "Resumed");
        }

        // 정지 판정
        // ESC로(메뉴) 일시정지
        const bool menuPause = m_time.paused;

        // 연출 시에 전투 정지
        const bool presentationBlocked = editorEnabled && m_editor.BlocksCombat();
        if (presentationBlocked != m_time.combatStopped || presentationBlocked)
            m_input.DiscardHeld();

        m_time.combatStopped = presentationBlocked;
        m_time.paused = menuPause || (!smokeTest && !m_window.IsFocused());

        // 고정 tick을 돌릴지를 경정 (에디터 없이 실행하거나 에디터 play 시 and 정지 x and 연출 진행 x)
        // 고정 tick을 사용하는 이유 - 이동, 충돌, 전투 판정 같은 계산을 일정하지 않은 기존 프레임 간격이 아닌 고정 프레임으로 계산해서 재현성과 안정성 보장
        const bool simulating =
            (!editorEnabled || m_editor.Playing()) && !m_time.paused && !presentationBlocked;
        const bool restartStep = sceneReplaced || simulating != wasSimulating ||
                                 (editorEnabled && wasPlaying != m_editor.Playing());
        if (restartStep)
        {
            fixed.Reset();
            m_input.DiscardHeld();
        }
        wasSimulating = simulating;

        m_time.AdvanceFrame(delta);
        m_time.paused = menuPause;
        if (editorEnabled)
            m_editor.AdvancePresentation(m_time.presentation.delta);

        // 고정 갱신: 입력 → 행동/애니메이션 → 충돌 → 결과 → 이벤트 → 실제 삭제.
        // 재개 프레임의 delta는 정지 중 시간이 섞일 수 있으므로 다음 프레임부터 누적.
        const unsigned steps = simulating ? fixed.Accumulate(restartStep ? 0 : delta) : 0;
        for (unsigned step = 0; step < steps; ++step)
        {
            if (m_time.paused || m_time.combatStopped || (editorEnabled && m_editor.BlocksCombat()))
            {
                fixed.Reset();
                m_input.DiscardHeld();
                break;
            }
            const Input tickInput = m_input.ConsumeTick();
            m_time.AdvanceTick(fixed.Step());
            m_scene.FixedUpdate({tickInput, m_time}, false);

            std::vector<Contact> contacts;
            if (m_time.combat.delta > 0)
                contacts = m_collision.Step(m_scene, m_time.combat.elapsed - m_time.combat.delta,
                                            m_time.combat.elapsed);
            else
                m_collision.Reset();
            ResolveContacts(contacts);
            m_events.Dispatch(m_scene);
            m_scene.Flush();
        }
        if (!simulating)
        {
            fixed.Reset();
            m_input.ClearPendingTick();
            m_collision.Reset();
        }

        // Play 중 프레임 갱신은 tick 수와 무관하게 한 번 실행한다.
        // 일시정지·전환 중에도 호출해 UI/연출 갱신을 허용한다. Edit 모드에서는 실행하지 않는다.
        if (!editorEnabled || m_editor.Playing())
        {
            const Time frameTime = m_time.FrameView();
            m_scene.Update({m_input, frameTime});
        }

        // 디버그 표시
        if (editorEnabled)
            m_editor.DrawDebug(m_scene, m_collision);
        if (editorTest && m_time.frame == 3)
            m_renderer.RequestCapture("captures/editor.bmp");
        if (presentationTest && m_time.frame == 30)
            m_renderer.RequestCapture("captures/presentation.bmp");
        if (!m_renderer.Resize(m_window.Width(), m_window.Height()))
            return 1;

        // 장면 렌더링 정보 전달
        if (smokeTest && !sceneTest)
        {
            if (!m_renderer.Render(!frameVerified))
                return 1;
        }
        else
        {
            // 에디터 없이 실행할 때 켜진 카메라가 없으면 기본 시점으로 그리고 한 번만 알린다.
            const bool hasCamera = FindActiveCamera(m_scene) != 0;
            if (!editorEnabled && !hasCamera && !missingCameraReported)
                Log::Write(LogLevel::Warning, "No active CameraComponent; using default view");
            missingCameraReported = !hasCamera;
            auto frame = CollectRenderFrame(m_scene,
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
