#pragma once
#include "Data/SceneIO.h"
#include "Collision/Collision.h"
#include <Windows.h>
#include <d3d11.h>
namespace nereides
{
class Editor final
{
public:
    ~Editor();
    bool Initialize(HWND window,ID3D11Device* device,ID3D11DeviceContext* context);
    void Message(HWND,UINT,WPARAM,LPARAM);
    // Returns true when live scene is replaced; callers must reset transient contacts/input.
    bool Begin(Scene&,ObjectId& camera,ObjectId& player,Camera&,Time&,Input&,CollisionWorld&);
    void Render();
    void DrawDebug(const Scene&,ObjectId,const Camera&,const CollisionWorld&);
private:
    bool m_initialized=false,m_debug=true,m_dirty=false;
    ObjectId m_selected=0;
    int m_operation=0;
    ModelCache m_models;
    std::string m_status="Ready";
    char m_scenePath[512]="data/scenes/sandbox.json";
    char m_modelPath[512]="assets/black_sword/model/black_sword_all_clips.fbx";
};
}
