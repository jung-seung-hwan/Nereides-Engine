#include "RHI/Compat/RHICommon.h"
#include "RHI/D3D11/D3D11Device.h"
#include "RHI/D3D11/D3D11Convert.h"
#include "RHI/Compat/SherlockLog.h"
#include <cstring>
#include <format> // C++20: 디버그 이름 조립
#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace
{
// 같은 오류를 매 프레임 찍지 않도록 한 번만 남기는 로그.
void ErrorOnce(bool& flag, const char* message)
{
    if (flag)
        return;
    flag = true;
    Log::Error("%s", message);
}
}

// ------------------------------------------------------------------ 수명

bool D3D11Device::Init(const RHI::DeviceDesc& desc)
{
    m_mainWindow = static_cast<HWND>(desc.windowHandle);
    m_screenWidth = desc.width;
    m_screenHeight = desc.height;
    m_debugLayer = desc.enableDebugLayer;

    if (!InitDirect3D())
        return false;
    m_commandList.Init(this);
    // 10단계
    if (!CreateTimestampQueries())
        return false;
    if (!CreateBackBufferTexture())
        return false;
    if (!CreateDepthTexture())
        return false;

    return true;
}

void D3D11Device::ReleaseDevice()
{
    if (m_imguiInitialized)
    {
        ShutdownImGui();
    }
    if (m_context)
    {
        m_context->ClearState();
        m_context->Flush();
    }

    // 장치가 만든 객체를 먼저 해제함. 장치보다 오래 살아 있으면 Live Object 경고가 발생함.
    m_pipelines.Clear();
    for (TimestampSet& set : m_timestampSets) // 10단계: 쿼리도 장치 객체임
    {
        set.disjoint.Reset();
        for (ComPtr<ID3D11Query>& query : set.timestamps)
            query.Reset();
        set.active = false;
    }
    m_resourceSets.Clear();
    m_bindingLayouts.Clear();
    m_shaders.Clear();
    m_samplers.Clear();
    m_textures.Clear(); // 백버퍼·깊이 버퍼 포함
    m_buffers.Clear();
    m_backBuffer = TextureHandle{};
    m_depthBuffer = TextureHandle{};

    m_swapChain.Reset();
    m_context.Reset();

#if defined(_DEBUG)
    // 장치만 남은 시점에 살아 있는 객체를 보고하게 함. 깨끗하게 정리됐다면
    // "Live ID3D11Device ..." 한 줄만 나옴 (자식 객체 보고가 없어야 함).
    if (m_device)
    {
        ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(m_device.As(&debug)))
        {
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
        }
        DumpDebugLayerMessages();
    }
#endif
    m_device.Reset();
}

void D3D11Device::DumpDebugLayerMessages()
{
#if defined(_DEBUG)
    if (!m_device)
        return;

    ComPtr<ID3D11InfoQueue> queue;
    if (FAILED(m_device.As(&queue)))
        return; // Debug Layer가 없으면(SDK 미설치) 조용히 넘어감.

    const UINT64 count = queue->GetNumStoredMessages();
    std::vector<char> buffer;
    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T length = 0;
        if (FAILED(queue->GetMessage(i, nullptr, &length)) || length == 0)
            continue;
        buffer.resize(length);
        D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(buffer.data());
        if (FAILED(queue->GetMessage(i, message, &length)))
            continue;

        switch (message->Severity)
        {
        case D3D11_MESSAGE_SEVERITY_CORRUPTION:
        case D3D11_MESSAGE_SEVERITY_ERROR:
            Log::Error("D3D11: %.*s", static_cast<int>(message->DescriptionByteLength),
                       message->pDescription);
            break;
        case D3D11_MESSAGE_SEVERITY_WARNING:
            Log::Warn("D3D11: %.*s", static_cast<int>(message->DescriptionByteLength),
                      message->pDescription);
            break;
        default:
            Log::Info("D3D11: %.*s", static_cast<int>(message->DescriptionByteLength),
                      message->pDescription);
            break;
        }
    }
    queue->ClearStoredMessages();
#endif
}

void D3D11Device::SetDebugName(ID3D11DeviceChild* object, const std::string& name, uint32_t index)
{
#if defined(_DEBUG)
    // Desc.debugName 을 Debug Layer 와 RenderDoc 이 읽는 이름으로 설정함 (WKPDID_D3DDebugObjectName).
    if (object == nullptr || name.empty())
        return;
    // C++20 (2026-09-28): 문자열 조립은 std::format. "이름[3]" 처럼 조각을 + 로 이어 붙이던 것을 서식 하나로 씀.
    const std::string full = index > 0 ? std::format("{}[{}]", name, index) : name;
    object->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(full.size()), full.c_str());
#else
    (void)object;
    (void)name;
    (void)index;
#endif
}

// ------------------------------------------------------------------ ImGui 어댑터

bool D3D11Device::InitImGui()
{
    if (!ImGui_ImplDX11_Init(m_device.Get(), m_context.Get()))
    {
        Log::Error("ImGui DX11 백엔드 초기화 실패");
        return false;
    }
    m_imguiInitialized = true;
    return true;
}

void D3D11Device::NewFrameImGui()
{
    if (m_imguiInitialized)
        ImGui_ImplDX11_NewFrame();
}

void D3D11Device::RenderImGui()
{
    // 이 백엔드는 자기가 건드린 D3D11 상태를 백업하고 복원함.
    // 그래도 다음 프레임의 첫 드로우 전에 PSO를 다시 바인딩하는 규칙은 유지함.
    if (m_imguiInitialized)
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void D3D11Device::ShutdownImGui()
{
    if (!m_imguiInitialized)
        return;
    ImGui_ImplDX11_Shutdown();
    m_imguiInitialized = false;
}

uint64_t D3D11Device::GetImGuiTextureId(TextureHandle handle)
{
    D3D11Texture* texture = m_textures.Get(handle);
    ID3D11ShaderResourceView* srv = texture ? texture->GetSRV(m_device.Get()) : nullptr;
    // imgui_impl_dx11 의 ImTextureID 는 SRV 포인터임. RHI 헤더에 imgui 를 끌어오지 않으려고 정수에 담아 넘김.
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(srv));
}

// ------------------------------------------------------------------ 프레임

uint32_t D3D11Device::BeginFrame()
{
    m_frameIndex = static_cast<uint32_t>(m_frameCounter % kFrameCount);
    m_commandList.ResetStats();

    // 10단계: 다시 쓸 세트(kTimestampSets 프레임 전에 쓴 것)의 결과를 먼저 읽고, 이번 프레임의 세트로 다시 시작함.
    if (m_timestampSets[0].disjoint)
    {
        TimestampSet& set = m_timestampSets[m_frameCounter % kTimestampSets];
        if (set.active)
            ResolveTimestamps(set);
        for (bool& w : set.written)
            w = false;
        set.frameNumber = m_frameCounter;
        set.active = true;
        m_context->Begin(set.disjoint.Get());
    }
    return m_frameIndex;
}

void D3D11Device::EndFrame()
{
    if (m_commandList.IsInRenderPass())
    {
        static bool warned = false;
        ErrorOnce(warned, "EndFrame : 렌더 패스가 열린 채 Present. EndRenderPass 를 빠뜨렸다.");
        m_commandList.EndRenderPass();
    }
    if (m_readbackRequested)
        ReadBackBuffer(); // 11단계: Present 전에 읽음 (flip 모델은 Present 후 백버퍼 내용을 버림)
    if (m_swapChain)
    {
        // SyncInterval 1 = 수직 동기화(모니터 주사율로 제한), 0 = 제한 없음.
        // flip 모델의 창 모드에서 프레임을 제한 없이 내려면 ALLOW_TEARING 플래그가 있어야 컴포지터에 막히지 않음.
        const UINT flags = (!m_vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        m_swapChain->Present(m_vsync ? 1 : 0, flags);
    }
    if (m_timestampSets[0].disjoint)
    {
        m_context->End(m_timestampSets[m_frameCounter % kTimestampSets].disjoint.Get());
    }
    // 이 프레임에 쌓인 Debug Layer 메시지를 콘솔로 출력함. 비어 있으면 비용이 거의 없음.
    DumpDebugLayerMessages();
    ++m_frameCounter;
}

void D3D11Device::Resize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    if (!m_device || !m_context || !m_swapChain)
        return;

    m_screenWidth = width;
    m_screenHeight = height;

    // 순서가 중요함.
    // (1) 렌더 타깃 바인딩 해제 → (2) 백버퍼·깊이 텍스처 파괴(스왑체인 버퍼 참조가 전부
    // 사라져야 ResizeBuffers가 DXGI_ERROR_INVALID_CALL 없이 성공함) → (3) ResizeBuffers
    // → (4) 새 핸들로 재생성. 이전 핸들은 세대가 올라가므로 어디서 쓰든 nullptr가 됨.
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    DestroyTexture(m_backBuffer);
    DestroyTexture(m_depthBuffer);
    m_backBuffer = TextureHandle{};
    m_depthBuffer = TextureHandle{};

    const HRESULT hr =
        m_swapChain->ResizeBuffers(0, // 기존 BufferCount 유지
                                   m_screenWidth, m_screenHeight,
                                   DXGI_FORMAT_UNKNOWN, // 기존 포맷 유지
                                   m_swapChainFlags); // 생성 때와 같은 플래그(ALLOW_TEARING)여야 함
    if (FAILED(hr))
    {
        Log::Error("ResizeBuffers() 실패. %s", Log::HrToString(hr).c_str());
        return;
    }

    if (!CreateBackBufferTexture())
    {
        Log::Error("Resize 후 백버퍼 재생성 실패.");
        return;
    }
    if (!CreateDepthTexture())
    {
        Log::Error("Resize 후 깊이 버퍼 재생성 실패.");
        return;
    }
}

// ------------------------------------------------------------------ 버퍼

BufferHandle D3D11Device::CreateBuffer(const BufferDesc& descIn, const void* initialData)
{
    if (!m_device)
    {
        Log::Error("CreateBuffer : 장치가 없음.");
        return BufferHandle{};
    }
    if (descIn.size == 0)
    {
        Log::Error("CreateBuffer : 크기가 0 (%s).", descIn.debugName ? descIn.debugName : "");
        return BufferHandle{};
    }

    D3D11Buffer buffer;
    buffer.desc = descIn;
    buffer.desc.debugName = nullptr; // 포인터 수명을 믿을 수 없으므로 문자열로 복사함.
    buffer.name = descIn.debugName ? descIn.debugName : "";

    // D3D11 상수 버퍼 크기는 16바이트 단위여야 함. desc.size에도 올림한 값을 보관해
    // UpdateBuffer의 크기 검사가 실제 버퍼 크기와 맞게 함.
    if (buffer.desc.bindFlags & BufferBind_Constant)
    {
        buffer.desc.size = (buffer.desc.size + 15) & ~15u;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = buffer.desc.size;
    if (buffer.desc.bindFlags & BufferBind_Vertex)
        bd.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
    if (buffer.desc.bindFlags & BufferBind_Index)
        bd.BindFlags |= D3D11_BIND_INDEX_BUFFER;
    if (buffer.desc.bindFlags & BufferBind_Constant)
        bd.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
    if (buffer.desc.bindFlags & BufferBind_ShaderResource)
    {
        bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = buffer.desc.stride;
    }

    switch (buffer.desc.usage)
    {
    case BufferUsage::Default:
        bd.Usage = D3D11_USAGE_DEFAULT;
        break;
    case BufferUsage::Dynamic:
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        break;
    case BufferUsage::Staging:
        bd.Usage = D3D11_USAGE_STAGING;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        bd.BindFlags = 0; // Staging은 파이프라인에 바인딩할 수 없음
        break;
    }

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = initialData;

    // Dynamic 버퍼는 프레임마다 다른 복제본을 씀. 초기 데이터는 모든 복제본에 넣음.
    const uint32_t copies = (buffer.desc.usage == BufferUsage::Dynamic) ? kFrameCount : 1;
    for (uint32_t i = 0; i < copies; ++i)
    {
        const HRESULT hr = m_device->CreateBuffer(&bd, initialData ? &init : nullptr,
                                                  buffer.buffers[i].GetAddressOf());
        if (FAILED(hr))
        {
            Log::Error("CreateBuffer 실패 (%s, %u바이트). %s", buffer.name.c_str(),
                       buffer.desc.size, Log::HrToString(hr).c_str());
            return BufferHandle{};
        }
        SetDebugName(buffer.buffers[i].Get(), buffer.name, i);
    }

    return m_buffers.Add(std::move(buffer));
}

void D3D11Device::DestroyBuffer(BufferHandle handle)
{
    m_buffers.Remove(handle);
}

void D3D11Device::UpdateBuffer(BufferHandle handle, const void* data, uint32_t size)
{
    static bool warnedInvalid = false;
    D3D11Buffer* buffer = m_buffers.Get(handle);
    if (buffer == nullptr || data == nullptr)
    {
        ErrorOnce(warnedInvalid, "UpdateBuffer : 유효하지 않은 핸들 또는 데이터.");
        return;
    }
    if (size > buffer->desc.size)
    {
        Log::Error("UpdateBuffer : 크기 초과 (%s, %u > %u).", buffer->name.c_str(), size,
                   buffer->desc.size);
        return;
    }

    ID3D11Buffer* target = buffer->Get(m_frameIndex);
    switch (buffer->desc.usage)
    {
    case BufferUsage::Dynamic: {
        // WRITE_DISCARD: 드라이버가 새 메모리를 주고 이전 내용은 버림. GPU가 아직
        // 읽고 있어도 기다리지 않음(이름 바꾸기, renaming). 한 프레임에 같은
        // 버퍼를 여러 번 Map해도 각 드로우는 자기 데이터를 봄. 대신 매번
        // 전체를 다시 써야 함 — 이전 내용을 읽을 수 없음.
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT hr = m_context->Map(target, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
        {
            Log::Error("UpdateBuffer : Map 실패 (%s). %s", buffer->name.c_str(),
                       Log::HrToString(hr).c_str());
            return;
        }
        std::memcpy(mapped.pData, data, size);
        m_context->Unmap(target, 0);
        break;
    }
    case BufferUsage::Default:
        // UpdateSubresource는 pDstBox가 없으면 리소스 전체를 덮어씀. 부분 갱신은
        // 상수버퍼에서 허용되지 않으므로 전체 크기만 받음.
        if (size != buffer->desc.size)
        {
            Log::Error("UpdateBuffer : Default 버퍼는 전체 크기로만 갱신할 수 있음 (%s, %u != %u).",
                       buffer->name.c_str(), size, buffer->desc.size);
            return;
        }
        m_context->UpdateSubresource(target, 0, nullptr, data, 0, 0);
        break;
    case BufferUsage::Staging:
        Log::Error("UpdateBuffer : Staging 버퍼는 이 경로로 갱신하지 않음 (%s).",
                   buffer->name.c_str());
        break;
    }
}

// ------------------------------------------------------------------ 텍스처

TextureHandle D3D11Device::CreateTexture(const TextureDesc& descIn,
                                         std::span<const TextureSubresource> subresources)
{
    if (!m_device)
    {
        Log::Error("CreateTexture : 장치가 없음.");
        return TextureHandle{};
    }

    D3D11Texture texture;
    texture.desc = descIn;
    texture.desc.debugName = nullptr;
    texture.name = descIn.debugName ? descIn.debugName : "";

    // 깊이 텍스처를 셰이더에서도 읽으려면(그림자 맵) 리소스를 TYPELESS로 만듦. 포맷은 뷰가 정함.
    const DXGI_FORMAT format = D3D11Convert::ToDXGI(descIn.format);
    const bool depthAndSrv = (descIn.bindFlags & TextureBind_DepthStencil) &&
                             (descIn.bindFlags & TextureBind_ShaderResource);
    // 11단계: 렌더 타깃 + SRV 인 UNORM 색상 텍스처(에디터 씬 뷰)도 TYPELESS로 만듦 — sRGB RTV 로 그리고 UNORM SRV 로 읽음.
    const bool colorRtAndSrv = (descIn.bindFlags & TextureBind_RenderTarget) &&
                               (descIn.bindFlags & TextureBind_ShaderResource) &&
                               format == DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.typeless = (depthAndSrv && D3D11DepthFormat::IsDepth(format)) || colorRtAndSrv;

    // 2026-10-08: 큐브는 정사각형 + ShaderResource 전용 (ResourceDesc.h). RTV/DSV 는 2D 뷰만 만들 줄 알기 때문.
    const bool isCube = descIn.dimension == TextureDimension::TextureCube;
    const uint32_t arraySize = GetArraySize(descIn);
    if (isCube && (descIn.width != descIn.height ||
                   (descIn.bindFlags & (TextureBind_RenderTarget | TextureBind_DepthStencil))))
    {
        Log::Error("CreateTexture : 큐브 텍스처는 정사각형 ShaderResource 전용임 (%s, %ux%u).",
                   texture.name.c_str(), descIn.width, descIn.height);
        return TextureHandle{};
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = descIn.width;
    td.Height = descIn.height;
    td.MipLevels = descIn.mipLevels;
    td.ArraySize = arraySize;
    td.Format = texture.typeless
                    ? (D3D11DepthFormat::IsDepth(format) ? D3D11DepthFormat::Typeless(format)
                                                         : DXGI_FORMAT_R8G8B8A8_TYPELESS)
                    : format;
    td.SampleDesc.Count = descIn.sampleCount;
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    if (descIn.bindFlags & TextureBind_RenderTarget)
        td.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (descIn.bindFlags & TextureBind_DepthStencil)
        td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    if (descIn.bindFlags & TextureBind_ShaderResource)
        td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (isCube)
        td.MiscFlags |=
            D3D11_RESOURCE_MISC_TEXTURECUBE; // 이 플래그가 있어야 TEXTURECUBE SRV 를 만들 수 있음

    // 초기 데이터: 서브리소스(면 × 밉)마다 D3D11_SUBRESOURCE_DATA 하나. 순서는 면 0 의 밉 0..N−1, 면 1 의 ... (서브리소스 인덱스 순).
    // 개수가 모자라면 거부함 (모자란 밉에는 쓰레기 값이 들어가 원거리에서 이상한 색이 나옴).
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    if (!subresources.empty()) // span: (nullptr, 0) 검사 두 개가 empty() 하나로
    {
        const uint32_t count = descIn.mipLevels * arraySize;
        if (subresources.size() < count)
        {
            Log::Error(
                "CreateTexture : 초기 데이터가 서브리소스 수(면 %u × 밉 %u)보다 적음 (%s, %zu < %u).",
                arraySize, descIn.mipLevels, texture.name.c_str(), subresources.size(), count);
            return TextureHandle{};
        }
        init.resize(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            init[i].pSysMem = subresources[i].data;
            init[i].SysMemPitch = subresources[i].rowPitch;
            init[i].SysMemSlicePitch = 0;
        }
    }

    const HRESULT hr = m_device->CreateTexture2D(&td, init.empty() ? nullptr : init.data(),
                                                 texture.texture.GetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("CreateTexture2D 실패 (%s, %ux%u, 밉 %u). %s", texture.name.c_str(),
                   descIn.width, descIn.height, descIn.mipLevels, Log::HrToString(hr).c_str());
        return TextureHandle{};
    }
    SetDebugName(texture.texture.Get(), texture.name, 0);

    return m_textures.Add(std::move(texture));
}

void D3D11Device::DestroyTexture(TextureHandle handle)
{
    m_textures.Remove(handle);
}

bool D3D11Device::CreateBackBufferTexture()
{
    // 스왑체인이 이미 만든 텍스처를 감쌈. CreateTexture로는 만들 수 없는 유일한 텍스처임.
    // 포맷은 UNORM임. flip 모델에서는 백버퍼를 *_SRGB로 만들 수 없으므로 뷰를 sRGB로 만듦.
    ComPtr<ID3D11Texture2D> backBuffer;
    const HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr))
    {
        Log::Error("SwapChain BackBuffer 가져오기 실패. %s", Log::HrToString(hr).c_str());
        return false;
    }

    D3D11Texture texture;
    // C++20 지정 초기화: 나열하지 않은 필드(debugName)는 헤더의 기본값을 받음.
    // 지정 초기화는 리스트 초기화라 축소 변환(int → uint32_t)이 금지됨 — 그래서 static_cast 가 필요함. 대입문에서는 조용히 넘어가던 변환임.
    texture.desc = TextureDesc{
        .width = static_cast<uint32_t>(m_screenWidth),
        .height = static_cast<uint32_t>(m_screenHeight),
        .format = Format::R8G8B8A8_UNORM,
        .mipLevels = 1,
        .bindFlags = TextureBind_RenderTarget,
        .sampleCount = 1,
    };
    texture.name = "BackBuffer";
    texture.texture = backBuffer;
    texture.state = ResourceState::Present; // D3D12 관례: 스왑체인 버퍼는 PRESENT 상태로 시작
    SetDebugName(texture.texture.Get(), texture.name, 0);

    m_backBuffer = m_textures.Add(std::move(texture));
    return m_backBuffer.IsValid();
}

bool D3D11Device::CreateDepthTexture()
{
    // 깊이·스텐실 텍스처. 크기와 샘플 수는 백버퍼와 일치해야 함.
    const TextureDesc desc{
        .width = static_cast<uint32_t>(m_screenWidth),
        .height = static_cast<uint32_t>(m_screenHeight),
        .format = Format::D24_UNORM_S8_UINT,
        .mipLevels = 1,
        .bindFlags = TextureBind_DepthStencil,
        .sampleCount = 1, // 스왑체인과 동일 (MSAA 미사용)
        .debugName = "DepthBuffer",
    };

    m_depthBuffer = CreateTexture(desc);
    if (D3D11Texture* t = m_textures.Get(m_depthBuffer))
        t->state = ResourceState::DepthWrite;
    return m_depthBuffer.IsValid();
}

// ------------------------------------------------------------------ 샘플러

SamplerHandle D3D11Device::CreateSampler(const SamplerDesc& descIn)
{
    if (!m_device)
    {
        Log::Error("CreateSampler : 장치가 없음.");
        return SamplerHandle{};
    }

    D3D11Sampler sampler;
    sampler.desc = descIn;
    sampler.desc.debugName = nullptr;
    sampler.name = descIn.debugName ? descIn.debugName : "";

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11Convert::ToD3D11(descIn.filter);
    sd.AddressU = D3D11Convert::ToD3D11(descIn.addressU);
    sd.AddressV = D3D11Convert::ToD3D11(descIn.addressV);
    sd.AddressW = D3D11Convert::ToD3D11(descIn.addressW);
    sd.MipLODBias = 0.0f;
    sd.MaxAnisotropy = descIn.maxAnisotropy;
    sd.ComparisonFunc = (descIn.filter == SamplerFilter::Comparison)
                            ? D3D11Convert::ToD3D11(descIn.compareFunc)
                            : D3D11_COMPARISON_NEVER;
    std::memcpy(sd.BorderColor, descIn.borderColor, sizeof(sd.BorderColor));
    sd.MinLOD = descIn.minLod;
    sd.MaxLOD = descIn.maxLod;

    const HRESULT hr = m_device->CreateSamplerState(&sd, sampler.sampler.GetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("CreateSamplerState 실패 (%s). %s", sampler.name.c_str(),
                   Log::HrToString(hr).c_str());
        return SamplerHandle{};
    }
    SetDebugName(sampler.sampler.Get(), sampler.name, 0);
    return m_samplers.Add(std::move(sampler));
}

void D3D11Device::DestroySampler(SamplerHandle handle)
{
    m_samplers.Remove(handle);
}

// ------------------------------------------------------------------ 셰이더 / PSO

ShaderHandle D3D11Device::CreateShader(const ShaderDesc& desc)
{
    if (!m_device || desc.bytecode.empty())
    {
        Log::Error("CreateShader : 장치가 없거나 바이트코드가 비어 있음.");
        return ShaderHandle{};
    }

    D3D11Shader shader;
    shader.stage = desc.stage;
    shader.name = desc.debugName ? desc.debugName : "";

    HRESULT hr = S_OK;
    switch (desc.stage)
    {
    case ShaderStage::Vertex:
        hr = m_device->CreateVertexShader(desc.bytecode.data(), desc.bytecode.size(), nullptr,
                                          shader.vs.GetAddressOf());
        SetDebugName(shader.vs.Get(), shader.name, 0);
        break;
    case ShaderStage::Pixel:
        hr = m_device->CreatePixelShader(desc.bytecode.data(), desc.bytecode.size(), nullptr,
                                         shader.ps.GetAddressOf());
        SetDebugName(shader.ps.Get(), shader.name, 0);
        break;
    default:
        Log::Error("CreateShader : 알 수 없는 ShaderStage %d", static_cast<int>(desc.stage));
        return ShaderHandle{};
    }
    if (FAILED(hr))
    {
        Log::Error("CreateShader : 셰이더 객체 생성 실패 (%s). %s", shader.name.c_str(),
                   Log::HrToString(hr).c_str());
        return ShaderHandle{};
    }

    // 입력 레이아웃 검증(VS)과 리플렉션에 쓸 바이트코드 사본. span 은 소유하지 않으므로 여기서 복사함.
    shader.bytecode.assign(desc.bytecode.begin(), desc.bytecode.end());

    return m_shaders.Add(std::move(shader));
}

void D3D11Device::DestroyShader(ShaderHandle handle)
{
    m_shaders.Remove(handle);
}

PipelineHandle D3D11Device::CreatePipeline(const PipelineStateDesc& desc)
{
    return m_pipelines.GetOrCreate(*this, desc);
}

void D3D11Device::InvalidatePipelines()
{
    m_pipelines.Clear();
}

// ------------------------------------------------------------------ 바인딩 레이아웃 / 리소스 셋

BindingLayoutHandle D3D11Device::CreateBindingLayout(const BindingLayoutDesc& descIn)
{
    const char* name = descIn.debugName ? descIn.debugName : "";
    if (descIn.slotCount > kMaxBindingSlots)
    {
        Log::Error("CreateBindingLayout : 슬롯이 너무 많음 (%s, %u > %u).", name, descIn.slotCount,
                   kMaxBindingSlots);
        return BindingLayoutHandle{};
    }

    for (uint32_t i = 0; i < descIn.slotCount; ++i)
    {
        const BindingSlot& slot = descIn.slots[i];
        if (slot.stageMask == 0)
        {
            Log::Error("CreateBindingLayout : 슬롯 %u 의 stageMask가 0 (%s).", i, name);
            return BindingLayoutHandle{};
        }
        uint8_t limit = 0;
        switch (slot.type)
        {
        case BindingType::ConstantBuffer:
            limit = kMaxCbvRegister;
            break;
        case BindingType::ShaderResource:
            limit = kMaxSrvRegister;
            break;
        case BindingType::Sampler:
            limit = kMaxSamplerRegister;
            break;
        }
        if (slot.reg >= limit)
        {
            Log::Error("CreateBindingLayout : 슬롯 %u 레지스터 %u 가 상한 %u 이상 (%s).", i,
                       slot.reg, limit, name);
            return BindingLayoutHandle{};
        }
        // 같은 (타입, 레지스터)가 같은 스테이지에 두 번 나오면 하나가 다른 하나를 덮어씀.
        for (uint32_t j = 0; j < i; ++j)
        {
            const BindingSlot& other = descIn.slots[j];
            if (other.type == slot.type && other.reg == slot.reg &&
                (other.stageMask & slot.stageMask) != 0)
            {
                Log::Error(
                    "CreateBindingLayout : 슬롯 %u 와 %u 가 같은 레지스터를 같은 스테이지에 선언 (%s).",
                    j, i, name);
                return BindingLayoutHandle{};
            }
        }
    }

    D3D11BindingLayout layout;
    layout.desc = descIn;
    layout.desc.debugName = nullptr;
    layout.name = name;
    return m_bindingLayouts.Add(std::move(layout));
}

void D3D11Device::DestroyBindingLayout(BindingLayoutHandle handle)
{
    m_bindingLayouts.Remove(handle);
}

ResourceSetHandle D3D11Device::CreateResourceSet(const ResourceSetDesc& descIn)
{
    const char* name = descIn.debugName ? descIn.debugName : "";
    const D3D11BindingLayout* layout = m_bindingLayouts.Get(descIn.layout);
    if (layout == nullptr)
    {
        Log::Error("CreateResourceSet : 유효하지 않은 BindingLayoutHandle (%s).", name);
        return ResourceSetHandle{};
    }

    // 슬롯 타입에 맞는 종류의 핸들이 꽂혀 있는지 지금 검사함. 드로우 시점에 매번 검사하지 않기 위함임.
    for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
    {
        const BindingSlot& slot = layout->desc.slots[i];
        const ResourceBinding& binding = descIn.bindings[i];
        switch (slot.type)
        {
        case BindingType::ConstantBuffer: {
            const D3D11Buffer* buffer = m_buffers.Get(binding.buffer);
            if (buffer == nullptr || !(buffer->desc.bindFlags & BufferBind_Constant))
            {
                Log::Error("CreateResourceSet : 슬롯 %u (b%u) 에 상수버퍼가 아닌 핸들 (%s).", i,
                           slot.reg, name);
                return ResourceSetHandle{};
            }
            break;
        }
        case BindingType::ShaderResource: {
            const D3D11Texture* texture = m_textures.Get(binding.texture);
            if (texture == nullptr || !(texture->desc.bindFlags & TextureBind_ShaderResource))
            {
                Log::Error(
                    "CreateResourceSet : 슬롯 %u (t%u) 에 SRV 가능한 텍스처가 아닌 핸들 (%s).", i,
                    slot.reg, name);
                return ResourceSetHandle{};
            }
            break;
        }
        case BindingType::Sampler: {
            if (m_samplers.Get(binding.sampler) == nullptr)
            {
                Log::Error("CreateResourceSet : 슬롯 %u (s%u) 에 유효하지 않은 샘플러 핸들 (%s).",
                           i, slot.reg, name);
                return ResourceSetHandle{};
            }
            break;
        }
        }
    }

    D3D11ResourceSet set;
    set.desc = descIn;
    set.desc.debugName = nullptr;
    set.name = name;
    return m_resourceSets.Add(std::move(set));
}

void D3D11Device::DestroyResourceSet(ResourceSetHandle handle)
{
    m_resourceSets.Remove(handle);
}

// ------------------------------------------------------------------ 내부

bool D3D11Device::InitDirect3D()
{
    UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    if (m_debugLayer)
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0 // 7절 #10: 11_0 전용. 9_3 폴백은 의도한 것이 아니었음 (11단계 정리)
    };
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    // 5단계: 장치와 스왑체인을 따로 만듦. D3D11CreateDeviceAndSwapChain은 옛 DXGI_SWAP_CHAIN_DESC만
    // 받으므로 flip 모델의 ALLOW_TEARING 확인(IDXGIFactory5)이나 CreateSwapChainForHwnd를 쓸 수 없음.
    HRESULT hr =
        D3D11CreateDevice(nullptr, // 기본 그래픽 어댑터
                          D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevels,
                          ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, m_device.GetAddressOf(),
                          &featureLevel, m_context.GetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("D3D11CreateDevice() 실패. %s", Log::HrToString(hr).c_str());
        return false;
    }
    if (featureLevel != D3D_FEATURE_LEVEL_11_0)
    {
        Log::Error("D3D Feature Level 11_0 지원 안 함.");
        return false;
    }

    // 장치를 만든 어댑터의 팩토리를 얻음. 다른 팩토리로 스왑체인을 만들면 실패함.
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(m_device.As(&dxgiDevice)) ||
        FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()))))
    {
        Log::Error("IDXGIFactory2 얻기 실패.");
        return false;
    }

    // 창 모드에서 무제한 프레임(VSync 끔)을 내려면 tearing 지원이 필요함.
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5)))
    {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                    &allowTearing, sizeof(allowTearing))))
        {
            m_tearingSupported = (allowTearing != FALSE);
        }
    }
    m_swapChainFlags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    // flip 모델 (DXGI_SWAP_EFFECT_FLIP_DISCARD). 요구 사항: 버퍼 2개 이상, MSAA 없음,
    // Present 후 백버퍼 재바인딩(BeginRenderPass가 담당), 백버퍼 포맷은 UNORM(sRGB는 뷰로 처리).
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = m_screenWidth;
    sd.Height = m_screenHeight;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = m_swapChainFlags;

    hr = factory->CreateSwapChainForHwnd(m_device.Get(), m_mainWindow, &sd, nullptr, nullptr,
                                         m_swapChain.GetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("CreateSwapChainForHwnd() 실패. %s", Log::HrToString(hr).c_str());
        return false;
    }
    // DXGI가 Alt+Enter 전체화면 전환을 하지 않게 함 (창 모드 flip만으로도 전체화면급 성능이 나옴).
    factory->MakeWindowAssociation(m_mainWindow, DXGI_MWA_NO_ALT_ENTER);

    Log::Info("스왑체인: FLIP_DISCARD, 버퍼 2, tearing %s", m_tearingSupported ? "지원" : "미지원");

    // 4X MSAA 지원 여부 확인
    hr =
        m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &m_numQualityLevels);
    if (FAILED(hr) || m_numQualityLevels <= 0)
    {
        Log::Warn("4X MSAA 지원 안 됨.");
    }
    else
    {
        Log::Info("4X MSAA 지원됨. QualityLevels: %u", m_numQualityLevels);
    }
    return true;
}

// ------------------------------------------------------------------ 10단계: 타임스탬프 쿼리

bool D3D11Device::CreateTimestampQueries()
{
    D3D11_QUERY_DESC disjointDesc = {};
    disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    D3D11_QUERY_DESC timestampDesc = {};
    timestampDesc.Query = D3D11_QUERY_TIMESTAMP;
    for (uint32_t s = 0; s < kTimestampSets; ++s)
    {
        TimestampSet& set = m_timestampSets[s];
        HRESULT hr = m_device->CreateQuery(&disjointDesc, set.disjoint.GetAddressOf());
        if (FAILED(hr))
        {
            Log::Error("CreateQuery(DISJOINT) 실패. %s", Log::HrToString(hr).c_str());
            return false;
        }
        for (uint32_t i = 0; i < kMaxTimestamps; ++i)
        {
            hr = m_device->CreateQuery(&timestampDesc, set.timestamps[i].GetAddressOf());
            if (FAILED(hr))
            {
                Log::Error("CreateQuery(TIMESTAMP) 실패. %s", Log::HrToString(hr).c_str());
                return false;
            }
        }
    }
    return true;
}

void D3D11Device::WriteTimestamp(uint32_t slot)
{
    if (slot >= kMaxTimestamps || !m_context)
        return;
    TimestampSet& set = m_timestampSets[m_frameCounter % kTimestampSets];
    if (!set.active)
        return;
    // TIMESTAMP 쿼리는 End 만 호출함 (Begin 없음). GPU 가 명령 스트림의 이 지점을 지날 때의 시각을 기록함.
    m_context->End(set.timestamps[slot].Get());
    set.written[slot] = true;
}

void D3D11Device::ResolveTimestamps(TimestampSet& set)
{
    // DONOTFLUSH: 준비되지 않았으면 S_FALSE 로 돌아오고 이 세트는 버림 (kTimestampSets 프레임 전에 쓴 세트라 거의 항상 준비됨).
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
    const HRESULT hr = m_context->GetData(set.disjoint.Get(), &disjoint, sizeof(disjoint),
                                          D3D11_ASYNC_GETDATA_DONOTFLUSH);
    set.active = false;
    // 타임스탬프는 disjoint 결과와 무관하게 전부 읽어 둠 — 읽지 않은 쿼리에 다시 End 를 호출하면 경고가 발생함.
    uint64_t ticks[kMaxTimestamps] = {};
    bool complete = hr == S_OK;
    for (uint32_t i = 0; i < kMaxTimestamps; ++i)
    {
        if (!set.written[i])
            continue;
        uint64_t value = 0;
        if (m_context->GetData(set.timestamps[i].Get(), &value, sizeof(value),
                               D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            complete = false;
        ticks[i] = value;
    }
    if (!complete || disjoint.Disjoint || disjoint.Frequency == 0)
        return;
    std::memcpy(m_lastTicks, ticks, sizeof(ticks));
    m_lastFrequency = disjoint.Frequency;
    m_lastFrameNumber = set.frameNumber;
    m_hasTimestampResults = true;
}

bool D3D11Device::GetTimestampResults(std::span<uint64_t> ticks, uint64_t& frequency,
                                      uint64_t& frameNumber)
{
    if (!m_hasTimestampResults)
        return false;
    const size_t n = ticks.size() < kMaxTimestamps
                         ? ticks.size()
                         : kMaxTimestamps; // 호출자 배열 크기는 span 이 알고 있음
    for (size_t i = 0; i < n; ++i)
        ticks[i] = m_lastTicks[i];
    frequency = m_lastFrequency;
    frameNumber = m_lastFrameNumber;
    return true;
}

// ------------------------------------------------------------------ 11단계: 백버퍼 리드백 (스크린샷)

void D3D11Device::ReadBackBuffer()
{
    m_readbackRequested = false;
    D3D11Texture* texture = m_textures.Get(m_backBuffer);
    if (texture == nullptr || !texture->texture)
        return;

    D3D11_TEXTURE2D_DESC desc = {};
    texture->texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("리드백 스테이징 텍스처 생성 실패. %s", Log::HrToString(hr).c_str());
        return;
    }

    m_context->CopyResource(staging.Get(), texture->texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped); // 여기서 GPU 를 기다림
    if (FAILED(hr))
    {
        Log::Error("리드백 Map 실패. %s", Log::HrToString(hr).c_str());
        return;
    }
    m_readbackWidth = desc.Width;
    m_readbackHeight = desc.Height;
    m_readbackPixels.resize(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (uint32_t y = 0; y < desc.Height; ++y)
    {
        std::memcpy(m_readbackPixels.data() + static_cast<size_t>(y) * desc.Width * 4,
                    static_cast<const uint8_t*>(mapped.pData) +
                        static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(desc.Width) * 4);
    }
    m_context->Unmap(staging.Get(), 0);
    m_readbackReady = true;
}

bool D3D11Device::TakeReadbackResult(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height)
{
    if (!m_readbackReady)
        return false;
    rgba.swap(m_readbackPixels);
    width = m_readbackWidth;
    height = m_readbackHeight;
    m_readbackPixels.clear();
    m_readbackReady = false;
    return true;
}
