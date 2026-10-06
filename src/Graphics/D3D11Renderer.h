#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>
#include "Graphics/RenderFrame.h"
#include <map>
#include <functional>
#include <filesystem>

namespace nereides
{
class D3D11Renderer final
{
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    bool Initialize(HWND window, std::uint32_t width, std::uint32_t height);
    bool Resize(std::uint32_t width, std::uint32_t height);
    bool Render(bool verifyFrame = false);
    bool RenderScene(const RenderFrame& frame, bool verifyFrame = false,
                     std::uint64_t* pixelHash = nullptr);
    void Shutdown() noexcept;
    ID3D11Device* Device() const
    {
        return m_device.Get();
    }
    ID3D11DeviceContext* Context() const
    {
        return m_context.Get();
    }
    void SetOverlay(std::function<void()> draw)
    {
        m_overlay = std::move(draw);
    }
    void RequestCapture(std::filesystem::path path)
    {
        m_capturePath = std::move(path);
    }

private:
    bool CreateBackBuffer();
    bool CreateTriangle();
    bool CreateMeshPipeline();
    struct GpuMesh
    {
        std::weak_ptr<const MeshData> source;
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertices, indices;
        UINT count = 0;
    };
    GpuMesh* Upload(const std::shared_ptr<const MeshData>& mesh);
    bool VerifyTriangleFrame();
    bool ReadbackHash(std::uint64_t& hash);
    bool Check(HRESULT result, const wchar_t* operation);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depth;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_meshVs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_meshPs;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_meshLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_objectConstants;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_skinConstants;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_meshRasterizer;
    std::map<const MeshData*, GpuMesh> m_meshes;
    std::function<void()> m_overlay;
    std::filesystem::path m_capturePath;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
};
} // namespace nereides
