#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>

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
    void Shutdown() noexcept;

private:
    bool CreateBackBuffer();
    bool CreateTriangle();
    bool VerifyTriangleFrame();
    bool Check(HRESULT result, const wchar_t* operation);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
};
}
