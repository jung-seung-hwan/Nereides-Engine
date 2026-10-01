#include "Graphics/D3D11Renderer.h"

#include <d3d11sdklayers.h>
#include <cstdio>

namespace nereides
{
using Microsoft::WRL::ComPtr;

D3D11Renderer::~D3D11Renderer()
{
    Shutdown();
}

bool D3D11Renderer::Check(HRESULT result, const wchar_t* operation)
{
    if (SUCCEEDED(result)) return true;
    wchar_t message[512]{};
    swprintf_s(message, L"[Nereides] %s failed: HRESULT 0x%08X\n",
        operation, static_cast<unsigned>(result));
    OutputDebugStringW(message);
    if (m_device && (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET))
    {
        wchar_t reason[128]{};
        swprintf_s(reason, L"[Nereides] Device removed reason: 0x%08X\n",
            static_cast<unsigned>(m_device->GetDeviceRemovedReason()));
        OutputDebugStringW(reason);
    }
    MessageBoxW(nullptr, message, L"Nereides DX11 error", MB_OK | MB_ICONERROR);
    return false;
}

bool D3D11Renderer::Initialize(HWND window, std::uint32_t width, std::uint32_t height)
{
    if (!window || width == 0 || height == 0 || m_device) return false;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    if (!Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 1, D3D11_SDK_VERSION, m_device.GetAddressOf(), nullptr,
        m_context.GetAddressOf()), L"D3D11CreateDevice")) return false;

#if defined(_DEBUG)
    ComPtr<ID3D11InfoQueue> queue;
    if (!Check(m_device.As(&queue), L"Query ID3D11InfoQueue")) return false;
    if (!Check(queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE),
        L"SetBreakOnSeverity(CORRUPTION)")) return false;
    if (!Check(queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE),
        L"SetBreakOnSeverity(ERROR)")) return false;
    OutputDebugStringW(L"[Nereides] DX11 debug layer enabled.\n");
#endif

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (!Check(m_device.As(&dxgiDevice), L"Query IDXGIDevice") ||
        !Check(dxgiDevice->GetAdapter(adapter.GetAddressOf()), L"GetAdapter") ||
        !Check(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())), L"GetParent(factory)"))
        return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!Check(factory->CreateSwapChainForHwnd(m_device.Get(), window, &desc, nullptr,
        nullptr, m_swapChain.GetAddressOf()), L"CreateSwapChainForHwnd")) return false;
    if (!Check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
        L"MakeWindowAssociation")) return false;
    m_width = width;
    m_height = height;
    if (!CreateBackBuffer()) return false;
    OutputDebugStringW(L"[Nereides] DX11 renderer initialized.\n");
    return true;
}

bool D3D11Renderer::CreateBackBuffer()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    if (!Check(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())),
        L"GetBuffer")) return false;
    if (!Check(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr,
        m_renderTarget.GetAddressOf()), L"CreateRenderTargetView")) return false;
#if defined(_DEBUG)
    constexpr char name[] = "Nereides.BackBufferRTV";
    if (!Check(m_renderTarget->SetPrivateData(WKPDID_D3DDebugObjectName,
        sizeof(name) - 1, name), L"SetPrivateData(RTV name)")) return false;
#endif
    return true;
}

bool D3D11Renderer::Resize(std::uint32_t width, std::uint32_t height)
{
    if (!m_swapChain || width == 0 || height == 0) return false;
    if (width == m_width && height == m_height) return true;
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTarget.Reset();
    if (!Check(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0),
        L"ResizeBuffers")) return false;
    m_width = width;
    m_height = height;
    return CreateBackBuffer();
}

bool D3D11Renderer::Render()
{
    if (!m_renderTarget) return false;
    ID3D11RenderTargetView* target = m_renderTarget.Get();
    m_context->OMSetRenderTargets(1, &target, nullptr);
    constexpr float color[] = {0.025f, 0.10f, 0.18f, 1.0f};
    m_context->ClearRenderTargetView(target, color);
    return Check(m_swapChain->Present(1, 0), L"Present");
}

void D3D11Renderer::Shutdown() noexcept
{
    if (m_context)
    {
        m_context->ClearState();
        m_context->Flush();
    }
    m_renderTarget.Reset();
    m_swapChain.Reset();
    m_context.Reset();
#if defined(_DEBUG)
    if (m_device)
    {
        ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(m_device.As(&debug)))
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
    }
#endif
    m_device.Reset();
}
}
