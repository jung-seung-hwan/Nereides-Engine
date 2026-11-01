#pragma once
#include "Core/Input.h"
#include "Core/Time.h"
#include "Platform/Win32Window.h"
#include "Graphics/D3D11Renderer.h"
#include "Scene/Scene.h"
#include "Collision/Collision.h"
#include "Editor/Editor.h"
#include "Core/Events.h"

namespace nereides
{
struct RunOptions
{
    bool automatic = false, legacyTriangle = false, editor = true, preview = false, resize = false,
         benchmark = false;
};

// final로 선언해 Application은 상속하지 못하게 막음
class Application final
{
public:
    ~Application()
    {
        // 이벤트·충돌·렌더 서비스가 살아 있을 때 실행 객체의 정리 콜백부터 호출한다.
        m_scene.EndPlay();
        // Also detach callbacks on early initialization/render failure before Editor is destroyed.
        m_window.SetMessageHandler({});
        m_window.SetCloseGuard({});
        m_renderer.SetOverlay({});
    }

    int Run(HINSTANCE instance, int showCommand, const RunOptions& options);

private:
    // Reverse destruction releases GPU resources before the window.
    // 생성된 멤버는 선언의 역순으로 해제된다
    Input m_input;
    Time m_time;
    Win32Window m_window;
    D3D11Renderer m_renderer;
    Editor m_editor;
    Scene m_scene;
    ObjectId m_player = 0;
    CollisionWorld m_collision;
    Events m_events;
    void ResolveContacts(const std::vector<Contact>& contacts);
};
} // namespace nereides
