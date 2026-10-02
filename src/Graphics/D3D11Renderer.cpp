#include "Graphics/D3D11Renderer.h"
#include "Graphics/TriangleShaders.h"

#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <cstdio>
#include <cstddef>

namespace nereides
{
using Microsoft::WRL::ComPtr;

namespace
{
struct Vertex
{
    float position[3];
    float color[3];
};
static_assert(sizeof(Vertex) == 6 * sizeof(float));
}

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
    if (!CreateTriangle()) return false;
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

bool D3D11Renderer::CreateTriangle()
{
    const Vertex vertices[] = {
        {{ 0.0f,  0.6f, 0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.6f, -0.6f, 0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.6f, -0.6f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = vertices;
    if (!Check(m_device->CreateBuffer(&bufferDesc, &initialData, m_vertexBuffer.GetAddressOf()),
        L"CreateBuffer(triangle)")) return false;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> pixelCode;
    const auto compile = [&](const char* entry, const char* profile, ComPtr<ID3DBlob>& code)
    {
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompile(kTriangleShaderSource, sizeof(kTriangleShaderSource) - 1,
            "TriangleShaders", nullptr, nullptr, entry, profile, flags, 0,
            code.GetAddressOf(), errors.GetAddressOf());
        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        return Check(result, L"D3DCompile(triangle)");
    };
    if (!compile("VSMain", "vs_5_0", vertexCode) || !compile("PSMain", "ps_5_0", pixelCode))
        return false;
    if (!Check(m_device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
        nullptr, m_vertexShader.GetAddressOf()), L"CreateVertexShader")) return false;
    if (!Check(m_device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(),
        nullptr, m_pixelShader.GetAddressOf()), L"CreatePixelShader")) return false;

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, position)),
            D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)),
            D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    return Check(m_device->CreateInputLayout(elements, 2, vertexCode->GetBufferPointer(),
        vertexCode->GetBufferSize(), m_inputLayout.GetAddressOf()), L"CreateInputLayout");
}

bool D3D11Renderer::Render(bool verifyFrame)
{
    if (!m_renderTarget) return false;
    ID3D11RenderTargetView* target = m_renderTarget.Get();
    m_context->OMSetRenderTargets(1, &target, nullptr);
    constexpr float color[] = {0.025f, 0.10f, 0.18f, 1.0f};
    m_context->ClearRenderTargetView(target, color);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    ID3D11Buffer* vertexBuffer = m_vertexBuffer.Get();
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_context->Draw(3, 0);
    if (verifyFrame && !VerifyTriangleFrame()) return false;
    return Check(m_swapChain->Present(1, 0), L"Present");
}

bool D3D11Renderer::VerifyTriangleFrame()
{
    // Smoke test reads pixels before Present: issuing Draw alone does not prove visible output.
    ComPtr<ID3D11Texture2D> backBuffer;
    if (!Check(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())), L"GetBuffer(verify)"))
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> readback;
    if (!Check(m_device->CreateTexture2D(&desc, nullptr, readback.GetAddressOf()), L"CreateTexture2D(verify)"))
        return false;
    m_context->CopyResource(readback.Get(), backBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!Check(m_context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped), L"Map(verify)"))
        return false;
    const auto* pixels = static_cast<const unsigned char*>(mapped.pData);
    const auto* center = pixels + (m_height / 2) * mapped.RowPitch + (m_width / 2) * 4;
    const bool backgroundMatches = pixels[0] >= 4 && pixels[0] <= 8 &&
        pixels[1] >= 24 && pixels[1] <= 28 && pixels[2] >= 44 && pixels[2] <= 48;
    const bool triangleVisible = center[0] > 25 && center[1] > 25 && center[2] > 25 &&
        static_cast<unsigned>(center[0]) + center[1] + center[2] > 200;
    m_context->Unmap(readback.Get(), 0);
    if (!backgroundMatches || !triangleVisible)
        return Check(E_FAIL, L"Triangle pixel verification");
    OutputDebugStringW(L"[Nereides] Triangle/background pixel verification passed.\n");
    return true;
}

void D3D11Renderer::Shutdown() noexcept
{
    if (m_context)
    {
        m_context->ClearState();
        m_context->Flush();
    }
    m_renderTarget.Reset();
    m_inputLayout.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();
    m_vertexBuffer.Reset();
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
