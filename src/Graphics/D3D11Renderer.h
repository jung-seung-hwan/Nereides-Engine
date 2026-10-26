#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <cstdint>
#include "Graphics/RenderFrame.h"
#include <functional>
#include <filesystem>
#include <memory>
#include <vector>
#include "RHI/Handle.h"

// Sherlock 에서 가져온 클래스 (전역 네임스페이스). 정의는 cpp 에서만 include 함.
class Renderer;
struct RenderSettings;
class D3D11Device;
namespace RHI
{
class Device;
}

namespace nereides
{
// 창 하나에 그리는 렌더러. 장치·스왑체인은 RHI::Device 가 소유하고, 장면은 Sherlock 에서 가져온 Renderer 가 그림.
// 이 클래스는 Application/Editor 가 쓰던 호출 방식(Initialize/Resize/RenderScene/SetOverlay 등)을 그대로 유지하는 얇은 앞면임.
//
// 프레임 순서: BeginFrame → Renderer::Render (그림자·메인 패스) → UI 패스(overlay) → EndFrame(Present).
// 에디터의 ImGui 는 Device()/Context() 접근자로 직접 초기화함.
class D3D11Renderer final
{
public:
    D3D11Renderer();
    ~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    bool Initialize(HWND window, std::uint32_t width, std::uint32_t height);
    bool Resize(std::uint32_t width, std::uint32_t height);
    bool PrepareSceneView(std::uint32_t width, std::uint32_t height);

    // 에디터 Scene 영역의 색상 텍스처. PrepareSceneView 가 만든 뒤에만 유효함.
    ID3D11ShaderResourceView* SceneView() const
    {
        return m_sceneView;
    }

    // --smoke-test 의 기본 삼각형. RHI 로 그림.
    bool Render(bool verifyFrame = false);
    bool RenderScene(const RenderFrame& frame, bool verifyFrame = false,
                     std::uint64_t* pixelHash = nullptr);
    void Shutdown() noexcept;

    ID3D11Device* Device() const;
    ID3D11DeviceContext* Context() const;

    void SetOverlay(std::function<void()> draw)
    {
        m_overlay = std::move(draw);
    }

    void RequestCapture(std::filesystem::path path)
    {
        m_capturePath = std::move(path);
    }

    // 수직 동기화. 기본은 켜짐(Present(1, 0)). 성능 측정에서 끌 수 있음.
    void SetVSync(bool enabled);

    // 렌더 설정(그림자 영역·디버그 뷰 등). Initialize 이후에만 부를 수 있음.
    RenderSettings& Settings();

private:
    bool CreateTriangle();

    bool VerifyTriangleFrame(const std::vector<std::uint8_t>& rgba, std::uint32_t width,
                             std::uint32_t height);
    bool VerifySceneView();
    bool AnalyzeBackBuffer(const std::vector<std::uint8_t>& rgba, std::uint32_t width,
                           std::uint32_t height, std::uint64_t* hash);
    bool Fail(const char* message);

    std::unique_ptr<RHI::Device> m_rhi;
    D3D11Device* m_d3d = nullptr; // m_rhi 의 구체 타입. Device()/Context() 접근자와 SRV 얻기에 씀
    std::unique_ptr<Renderer> m_renderer;

    // 기본 삼각형
    ShaderHandle m_triangleVs;
    ShaderHandle m_trianglePs;
    BufferHandle m_triangleVertices;

    ID3D11ShaderResourceView* m_sceneView = nullptr;
    std::function<void()> m_overlay;
    std::filesystem::path m_capturePath;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    bool m_comInitialized = false; // 텍스처 로더(DirectXTex의 WIC 경로)가 COM을 요구함
};
} // namespace nereides
