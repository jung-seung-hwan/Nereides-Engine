#include "Graphics/D3D11Renderer.h"
#include "Renderer/Renderer.h"
#include "Graphics/TriangleShaders.h"
#include "Core/Log.h"
#include "RHI/RHI.h"
#include "RHI/D3D11/D3D11Device.h"
#include <algorithm>
#include <cstddef>
#include <fstream>

#include <d3dcompiler.h>
#include <d3d11sdklayers.h>

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

// 백버퍼에 쓰는 clear 색 (UNORM 뷰, 감마 변환 없음). 화면에서 (6, 26, 46)이 됨.
constexpr float kTriangleBackground[] = {0.025f, 0.10f, 0.18f, 1.0f};

// 화면에 무엇인가 그려졌는지 검사한다. 하늘·조명 때문에 "배경색이 아닌 픽셀 수"는 의미가 없어서,
// (1) 모서리 픽셀과 뚜렷이 다른 픽셀이 있고 (2) 밝은 픽셀이 있는지를 본다. rowPitch 는 바이트 단위.
bool LooksRendered(const unsigned char* rgba, std::size_t rowPitch, std::uint32_t width,
                   std::uint32_t height)
{
    if (width == 0 || height == 0)
        return false;

    const auto sum = [](const unsigned char* p) { return unsigned(p[0]) + p[1] + p[2]; };
    const unsigned corner = sum(rgba);
    unsigned different = 0, bright = 0;

    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const unsigned value = sum(rgba + y * rowPitch + x * 4);
            if (value > 150)
                ++bright;
            if (value + 30 < corner || value > corner + 30)
                ++different;
        }
    return bright > 16 && different > 16;
}
} // namespace


D3D11Renderer::D3D11Renderer() = default;


D3D11Renderer::~D3D11Renderer()
{
    Shutdown();
}


bool D3D11Renderer::Fail(const char* message)
{
    Log::Write(LogLevel::Error, message);
    return false;
}


ID3D11Device* D3D11Renderer::Device() const
{
    return m_d3d ? m_d3d->GetDevice() : nullptr;
}


ID3D11DeviceContext* D3D11Renderer::Context() const
{
    return m_d3d ? m_d3d->GetContext() : nullptr;
}


void D3D11Renderer::SetVSync(bool enabled)
{
    if (m_rhi)
        m_rhi->SetVSync(enabled);
}


RenderSettings& D3D11Renderer::Settings()
{
    return m_renderer->GetSettings();
}


// 창에 출력할 환경 준비: RHI 장치(스왑체인 포함), 장면 렌더러, 기본 삼각형
bool D3D11Renderer::Initialize(HWND window, std::uint32_t width, std::uint32_t height)
{
    if (!window || width == 0 || height == 0 || m_rhi)
        return false;

    // WIC로 PNG·JPG를 읽는 텍스처 로더가 이 스레드의 COM을 요구함. 이미 다른 모드로 초기화됐으면(RPC_E_CHANGED_MODE) 그대로 씀.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_comInitialized = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
        return Fail("COM 초기화 실패");

    RHI::DeviceDesc desc;
    desc.windowHandle = window;
    desc.width = static_cast<int>(width);
    desc.height = static_cast<int>(height);
    desc.enableDebugLayer = true; // Debug 빌드에서만 실제로 켜짐
    m_rhi = RHI::CreateDevice(RHI::Backend::D3D11, desc);
    if (!m_rhi)
        return Fail("RHI 장치 생성 실패");

    m_d3d = static_cast<D3D11Device*>(m_rhi.get());
    m_rhi->SetVSync(true); // 이전 렌더러의 Present(1, 0)와 같음

#if defined(_DEBUG)
    // 이전 렌더러와 같이 디버그 레이어의 오류·손상 메시지에서 멈춤. 조용히 지나가는 오류를 막기 위함.
    ComPtr<ID3D11InfoQueue> queue;
    if (SUCCEEDED(m_d3d->GetDevice()->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf()))))
    {
        queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
    }
#endif

    m_width = width;
    m_height = height;

    m_renderer = std::make_unique<Renderer>();
    if (!m_renderer->Initialize(m_rhi.get()))
        return Fail("장면 렌더러 초기화 실패");

    if (!CreateTriangle())
        return false;

    OutputDebugStringW(L"[Nereides] DX11 renderer initialized.\n");
    return true;
}


// 창 크기 변경 처리. 크기 0(최소화)은 실패로 돌려 호출자가 건너뛰게 함.
bool D3D11Renderer::Resize(std::uint32_t width, std::uint32_t height)
{
    if (!m_rhi || width == 0 || height == 0)
        return false;

    if (width == m_width && height == m_height)
        return true;

    m_rhi->Resize(static_cast<int>(width), static_cast<int>(height));
    if (!m_rhi->GetBackBuffer().IsValid() || !m_rhi->GetDepthBuffer().IsValid())
        return Fail("Resize 후 백버퍼를 만들지 못함");

    m_width = width;
    m_height = height;
    return true;
}


bool D3D11Renderer::CreateTriangle()
{
    const Vertex vertices[] = {{{0.0f, 0.6f, 0.5f}, {1.0f, 0.0f, 0.0f}},
                               {{0.6f, -0.6f, 0.5f}, {0.0f, 1.0f, 0.0f}},
                               {{-0.6f, -0.6f, 0.5f}, {0.0f, 0.0f, 1.0f}}};
    const BufferDesc bufferDesc{.size = sizeof(vertices),
                                .usage = BufferUsage::Default,
                                .bindFlags = BufferBind_Vertex,
                                .stride = sizeof(Vertex),
                                .debugName = "TriangleVertices"};
    m_triangleVertices = m_rhi->CreateBuffer(bufferDesc, vertices);
    if (!m_triangleVertices.IsValid())
        return Fail("삼각형 정점 버퍼 생성 실패");

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    ComPtr<ID3DBlob> vertexCode, pixelCode;
    const auto compile = [&](const char* entry, const char* profile, ComPtr<ID3DBlob>& code) {
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompile(kTriangleShaderSource, sizeof(kTriangleShaderSource) - 1,
                                          "TriangleShaders", nullptr, nullptr, entry, profile,
                                          flags, 0, code.GetAddressOf(), errors.GetAddressOf());
        if (errors)
            OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        return SUCCEEDED(result);
    };
    if (!compile("VSMain", "vs_5_0", vertexCode) || !compile("PSMain", "ps_5_0", pixelCode))
        return Fail("삼각형 셰이더 컴파일 실패");

    const auto bytes = [](ComPtr<ID3DBlob>& blob) {
        return std::span<const std::uint8_t>(
            static_cast<const std::uint8_t*>(blob->GetBufferPointer()), blob->GetBufferSize());
    };
    m_triangleVs = m_rhi->CreateShader(
        ShaderDesc{.stage = ShaderStage::Vertex, .bytecode = bytes(vertexCode), .debugName = "TriangleVS"});
    m_trianglePs = m_rhi->CreateShader(
        ShaderDesc{.stage = ShaderStage::Pixel, .bytecode = bytes(pixelCode), .debugName = "TrianglePS"});
    if (!m_triangleVs.IsValid() || !m_trianglePs.IsValid())
        return Fail("삼각형 셰이더 생성 실패");

    return true;
}


// 에디터 Scene 영역용 색상·깊이 텍스처와 View 준비
// 크기가 같으면 기존 자원 재사용
bool D3D11Renderer::PrepareSceneView(std::uint32_t width, std::uint32_t height)
{
    if (!m_renderer)
        return false;

    width = std::clamp(width, 1u, 8192u);
    height = std::clamp(height, 1u, 8192u);
    m_renderer->SetSceneTarget(width, height);
    if (!m_renderer->IsOffscreen())
    {
        m_sceneView = nullptr;
        return false;
    }

    // ImGui 가 읽는 SRV. 텍스처가 다시 만들어지면 이전 SRV 는 사라지므로 매번 받아 둠.
    m_sceneView = reinterpret_cast<ID3D11ShaderResourceView*>(
        static_cast<std::uintptr_t>(m_d3d->GetImGuiTextureId(m_renderer->GetSceneTexture())));
    return m_sceneView != nullptr;
}


/*
 → BeginFrame
 → 그림자·메인 패스 (Renderer::Render)
 → UI 패스: 에디터 UI 출력
 → EndFrame (Present)
*/
bool D3D11Renderer::RenderScene(const RenderFrame& frame, bool verifyFrame,
                                std::uint64_t* pixelHash)
{
    if (!m_rhi || !m_renderer || !m_rhi->GetBackBuffer().IsValid() ||
        !m_rhi->GetDepthBuffer().IsValid())
        return false;

    const bool offscreen = m_renderer->IsOffscreen();
    const bool wantPixels = pixelHash || !m_capturePath.empty();
    bool ok = true;

    m_rhi->BeginFrame();
    m_renderer->Render(frame);
    if (verifyFrame && offscreen && !VerifySceneView())
        ok = false;

    m_renderer->BeginUIPass();
    if (m_overlay)
        m_overlay();
    m_renderer->EndUIPass();

    if (wantPixels || (verifyFrame && !offscreen))
        m_rhi->RequestBackBufferReadback();
    m_rhi->EndFrame();

    std::vector<std::uint8_t> rgba;
    std::uint32_t width = 0, height = 0;
    const bool haveReadback = m_rhi->TakeReadbackResult(rgba, width, height);
    if (verifyFrame && !offscreen)
    {
        if (!haveReadback || !LooksRendered(rgba.data(), std::size_t(width) * 4, width, height))
            ok = Fail("Scene pixel verification") && ok;
    }

    if (wantPixels)
    {
        if (!haveReadback)
            return Fail("Back buffer readback");
        std::uint64_t hash = 0;
        if (!AnalyzeBackBuffer(rgba, width, height, &hash))
            ok = false;
        if (pixelHash)
            *pixelHash = hash;
    }

    return ok;
}


// 씬 뷰 텍스처를 읽어 무엇인가 그려졌는지 확인 (에디터 모드의 검증)
bool D3D11Renderer::VerifySceneView()
{
    if (!m_sceneView)
        return false;

    ComPtr<ID3D11Resource> source;
    m_sceneView->GetResource(&source);
    ComPtr<ID3D11Texture2D> texture, readback;
    if (FAILED(source.As(&texture)))
        return false;

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    auto* device = Device();
    auto* context = Context();
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &readback)))
        return Fail("Scene probe");

    context->CopyResource(readback.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return Fail("Scene probe map");

    const bool visible = LooksRendered(static_cast<const unsigned char*>(mapped.pData),
                                       mapped.RowPitch, desc.Width, desc.Height);
    context->Unmap(readback.Get(), 0);
    return visible || Fail("Scene view verification");
}


// 백버퍼 픽셀의 해시를 구하고, 요청이 있으면 BMP 로 저장
bool D3D11Renderer::AnalyzeBackBuffer(const std::vector<std::uint8_t>& rgba, std::uint32_t width,
                                      std::uint32_t height, std::uint64_t* hash)
{
    std::uint64_t value = 14695981039346656037ull;
    unsigned visible = 0;
    std::ofstream capture;
    if (!m_capturePath.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(m_capturePath.parent_path(), error);
        capture.open(m_capturePath, std::ios::binary);
        BITMAPFILEHEADER file{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + width * height * 4;
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(width);
        info.biHeight = -static_cast<LONG>(height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        capture.write(reinterpret_cast<const char*>(&file), sizeof(file));
        capture.write(reinterpret_cast<const char*>(&info), sizeof(info));
    }

    for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
    {
        const auto* pixel = rgba.data() + i * 4;
        if (pixel[0] != 6 || pixel[1] != 26 || pixel[2] != 46)
            ++visible;
        for (unsigned c = 0; c < 4; ++c)
        {
            value ^= pixel[c];
            value *= 1099511628211ull;
        }

        if (capture.is_open())
        {
            const unsigned char bgra[] = {pixel[2], pixel[1], pixel[0], pixel[3]};
            capture.write(reinterpret_cast<const char*>(bgra), 4);
        }
    }

    if (!m_capturePath.empty())
    {
        capture.flush();
        const bool ok = bool(capture);
        m_capturePath.clear();
        if (!ok)
            return false;
    }

    if (hash)
        *hash = value;
    return visible > 16;
}


// --smoke-test: 삼각형 한 개만 RHI 로 그림
bool D3D11Renderer::Render(bool verifyFrame)
{
    if (!m_rhi || !m_rhi->GetBackBuffer().IsValid())
        return false;

    RHI::CommandList& cmd = m_rhi->GetCommandList();
    const TextureHandle backBuffer = m_rhi->GetBackBuffer();

    PipelineStateDesc pipeline;
    pipeline.vs = m_triangleVs;
    pipeline.ps = m_trianglePs;
    pipeline.vertexLayout.attributes[0] = {.semantic = VertexSemantic::Position,
                                           .format = Format::R32G32B32_FLOAT,
                                           .offset = static_cast<std::uint16_t>(offsetof(Vertex, position))};
    pipeline.vertexLayout.attributes[1] = {.semantic = VertexSemantic::Color,
                                           .format = Format::R32G32B32_FLOAT,
                                           .offset = static_cast<std::uint16_t>(offsetof(Vertex, color))};
    pipeline.vertexLayout.attributeCount = 2;
    pipeline.vertexLayout.stride = sizeof(Vertex);
    pipeline.rasterizer.cull = CullMode::None;
    pipeline.depthStencil.depthEnable = false;
    pipeline.dsvFormat = Format::Unknown;
    const PipelineHandle handle = m_rhi->CreatePipeline(pipeline);
    if (!handle.IsValid())
        return Fail("삼각형 파이프라인 생성 실패");

    m_rhi->BeginFrame();
    cmd.Barrier(backBuffer, ResourceState::Present, ResourceState::RenderTarget);
    RenderPassDesc pass;
    pass.colors[0].texture = backBuffer;
    pass.colors[0].load = LoadOp::Clear;
    pass.colors[0].srgbView = false;
    std::copy(std::begin(kTriangleBackground), std::end(kTriangleBackground),
              pass.colors[0].clearColor);
    pass.colorCount = 1;
    pass.debugName = "TrianglePass";
    cmd.BeginRenderPass(pass);
    cmd.SetPipelineState(handle);
    cmd.SetVertexBuffer(m_triangleVertices);
    cmd.Draw(3, 0);
    cmd.EndRenderPass();
    cmd.Barrier(backBuffer, ResourceState::RenderTarget, ResourceState::Present);

    if (verifyFrame)
        m_rhi->RequestBackBufferReadback();
    m_rhi->EndFrame();

    if (!verifyFrame)
        return true;

    std::vector<std::uint8_t> rgba;
    std::uint32_t width = 0, height = 0;
    return m_rhi->TakeReadbackResult(rgba, width, height) &&
           VerifyTriangleFrame(rgba, width, height);
}


bool D3D11Renderer::VerifyTriangleFrame(const std::vector<std::uint8_t>& rgba,
                                        std::uint32_t width, std::uint32_t height)
{
    // Smoke test reads pixels before Present: issuing Draw alone does not prove visible output.
    if (rgba.size() < std::size_t(width) * height * 4 || width == 0 || height == 0)
        return Fail("Triangle pixel verification: no readback");

    const auto* pixels = rgba.data();
    const auto* center = pixels + (std::size_t(height / 2) * width + width / 2) * 4;
    const bool backgroundMatches = pixels[0] >= 4 && pixels[0] <= 8 && pixels[1] >= 24 &&
                                   pixels[1] <= 28 && pixels[2] >= 44 && pixels[2] <= 48;
    const bool triangleVisible = center[0] > 25 && center[1] > 25 && center[2] > 25 &&
                                 static_cast<unsigned>(center[0]) + center[1] + center[2] > 200;
    if (!backgroundMatches || !triangleVisible)
        return Fail("Triangle pixel verification");

    OutputDebugStringW(L"[Nereides] Triangle/background pixel verification passed.\n");
    return true;
}


void D3D11Renderer::Shutdown() noexcept
{
    if (m_renderer)
        m_renderer->Shutdown();
    m_renderer.reset();

    if (m_rhi)
    {
        m_rhi->DestroyBuffer(m_triangleVertices);
        m_rhi->DestroyShader(m_triangleVs);
        m_rhi->DestroyShader(m_trianglePs);
    }
    m_triangleVertices = BufferHandle{};
    m_triangleVs = m_trianglePs = ShaderHandle{};
    m_sceneView = nullptr;

    // 장치 소멸자가 객체를 정리하고 Debug 빌드에서는 살아 있는 객체를 보고함.
    m_d3d = nullptr;
    m_rhi.reset();

    if (m_comInitialized)
        CoUninitialize();
    m_comInitialized = false;
}
} // namespace nereides
