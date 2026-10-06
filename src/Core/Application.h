#pragma once
#include "Core/Input.h"
#include "Core/Time.h"
#include "Platform/Win32Window.h"
#include "Graphics/D3D11Renderer.h"
#include "Scene/Scene.h"
#include "Collision/Collision.h"
#include "Editor/Editor.h"
namespace nereides
{
class Application final
{
public:
    int Run(HINSTANCE instance, int showCommand, bool smokeTest, bool sceneTest = false, bool editorTest = false);
private:
    // Reverse destruction releases GPU resources before the window.
    Input m_input;
    Time m_time;
    Win32Window m_window;
    D3D11Renderer m_renderer;
    Editor m_editor;
    Scene m_scene;
    ObjectId m_player = 0;
    ObjectId m_camera = 0;
    Camera m_cameraData;
    CollisionWorld m_collision;
};
}
