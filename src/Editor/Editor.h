#pragma once
#include "Data/SceneIO.h"
#include "Collision/Collision.h"
#include "Presentation/Transition.h"
#include "Graphics/TextureCache.h"
#include "Graphics/D3D11Renderer.h"
#include "Editor/EditHistory.h"
#include <Windows.h>
#include <d3d11.h>
struct ImDrawList;
namespace nereides
{
class Editor final
{
public:
    ~Editor();
    bool Initialize(HWND window, D3D11Renderer& renderer);
    void Message(HWND, UINT, WPARAM, LPARAM);
    // Returns true when live scene is replaced; callers must reset transient contacts/input.
    bool Begin(Scene&, ObjectId& camera, ObjectId& player, Camera&, Time&, Input&, CollisionWorld&);
    void Render();
    void DrawDebug(const Scene&, ObjectId, const Camera&, const CollisionWorld&);
    void AdvancePresentation(double dt);
    bool BlocksCombat() const
    {
        return m_transition.BlocksCombat();
    }
    void StartPreview();
    void ApplyView(RenderFrame&) const;
    bool RequestClose();
    bool Playing() const
    {
        return m_playing;
    }

private:
    friend struct EditorTestAccess;
    bool m_initialized = false, m_debug = true, m_dirty = false;
    bool m_historyReady = false, m_playing = false, m_closeRequested = false, m_allowClose = false,
         m_local = false, m_snap = false, m_grid = true;
    bool m_hasScenePath = false;
    HWND m_window = nullptr;
    D3D11Renderer* m_renderer = nullptr;
    EditHistory m_history;
    std::string m_playSnapshot;
    std::size_t m_playSelection = 0;
    DirectX::XMFLOAT3 m_focus{0, 1, 0};
    float m_yaw = -.65f, m_pitch = .4f, m_distance = 12, m_snapStep = .5f;
    DirectX::XMFLOAT2 m_viewOrigin{}, m_viewSize{640, 480};
    DirectX::XMFLOAT4X4 m_view{}, m_projection{};
    ImDrawList* m_sceneDrawList = nullptr;
    std::vector<std::filesystem::path> m_assets;
    char m_assetFilter[128]{};
    void RefreshAssets();
    void Focus(const Scene&, ObjectId);
    void Import(Scene&, const std::filesystem::path&, const DirectX::XMFLOAT3* position = nullptr);
    ObjectId m_selected = 0;
    int m_operation = 0;
    ModelCache m_models;
    TextureCache m_textures;
    Transition m_transition;
    std::string m_status = "Ready";
    char m_scenePath[512] = "data/scenes/sandbox.json";
    char m_modelPath[512] = "assets/black_sword/model/black_sword_all_clips.fbx";
    char m_clipPath[512] = "data/presentation/preview.json";
};
} // namespace nereides
