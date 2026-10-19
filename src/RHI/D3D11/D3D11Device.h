#pragma once
#include "RHI/D3D11/D3D11Common.h"
#include "RHI/D3D11/D3D11Resources.h"
#include "RHI/ResourcePool.h"
#include "RHI/D3D11/PipelineStateCache.h"
#include "RHI/D3D11/D3D11CommandList.h"
#include "RHI/Device.h"
#include "RHI/GraphicsConfig.h"

// RHI::Device 의 D3D11 구현. 장치·스왑체인과 리소스 풀을 관리함.
//
// 폴더 밖의 코드는 RHI::Device 인터페이스만 봄. ID3D11* 타입을 돌려주는 Get* 접근자는
// RHI/D3D11/ 안의 코드(PipelineState, D3D11CommandList)에서만 호출함.
// 3~6단계의 Device 클래스에서 이름만 바꾸고 인터페이스를 상속하게 했음. 메서드 본문은 같음.
class D3D11Device final : public RHI::Device
{
public:
    ~D3D11Device() override
    {
        ReleaseDevice();
    }

    bool Init(const RHI::DeviceDesc& desc); // RHI::CreateDevice 가 호출함
    void ReleaseDevice();                   // 두 번 호출해도 안전함.

    // ---- RHI::Device ----
    RHI::Backend GetBackend() const override
    {
        return RHI::Backend::D3D11;
    }
    const char* GetBackendName() const override
    {
        return "D3D11";
    }
    bool IsInitialized() const override
    {
        return m_device != nullptr;
    }

    uint32_t BeginFrame() override;
    void EndFrame() override;
    uint32_t GetFrameIndex() const override
    {
        return m_frameIndex;
    }
    RHI::CommandList& GetCommandList() override
    {
        return m_commandList;
    }
    bool GetTimestampResults(std::span<uint64_t> ticks, uint64_t& frequency,
                             uint64_t& frameNumber) override;
    void RequestBackBufferReadback() override
    {
        m_readbackRequested = true;
    }
    bool TakeReadbackResult(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) override;
    void WriteTimestamp(uint32_t slot); // D3D11CommandList 가 이쪽으로 위임함

    void Resize(int width, int height) override;
    void SetVSync(bool enabled) override
    {
        m_vsync = enabled;
    }
    bool IsVSync() const override
    {
        return m_vsync;
    }
    bool IsTearingSupported() const override
    {
        return m_tearingSupported;
    }

    BufferHandle CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) override;
    void DestroyBuffer(BufferHandle handle) override;
    void UpdateBuffer(BufferHandle handle, const void* data, uint32_t size) override;

    TextureHandle CreateTexture(const TextureDesc& desc,
                                std::span<const TextureSubresource> subresources = {}) override;
    void DestroyTexture(TextureHandle handle) override;
    TextureHandle GetBackBuffer() const override
    {
        return m_backBuffer;
    }
    TextureHandle GetDepthBuffer() const override
    {
        return m_depthBuffer;
    }

    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle handle) override;

    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    void DestroyShader(ShaderHandle handle) override;

    PipelineHandle CreatePipeline(const PipelineStateDesc& desc) override;
    size_t GetPipelineCount() const override
    {
        return m_pipelines.Count();
    }
    void InvalidatePipelines() override;

    BindingLayoutHandle CreateBindingLayout(const BindingLayoutDesc& desc) override;
    void DestroyBindingLayout(BindingLayoutHandle handle) override;
    ResourceSetHandle CreateResourceSet(const ResourceSetDesc& desc) override;
    void DestroyResourceSet(ResourceSetHandle handle) override;

    // ImGui 어댑터: imgui_impl_dx11 을 감쌈.
    bool InitImGui() override;
    void NewFrameImGui() override;
    void RenderImGui() override;
    void ShutdownImGui() override;
    uint64_t GetImGuiTextureId(TextureHandle texture) override;

    // Debug Layer가 InfoQueue에 쌓아 둔 메시지를 Log로 옮김(Debug 빌드에서만).
    void DumpDebugLayerMessages();

    // ---- RHI/D3D11/ 안에서만 쓰는 접근자 ----
    ID3D11Device* GetDevice() const
    {
        return m_device.Get();
    }
    ID3D11DeviceContext* GetContext() const
    {
        return m_context.Get();
    }
    const D3D11Shader* GetShader(ShaderHandle handle) const
    {
        return m_shaders.Get(handle);
    }
    const D3D11Buffer* GetBuffer(BufferHandle handle) const
    {
        return m_buffers.Get(handle);
    }
    D3D11Texture* GetTexture(TextureHandle handle)
    {
        return m_textures.Get(handle);
    }
    const D3D11Sampler* GetSampler(SamplerHandle handle) const
    {
        return m_samplers.Get(handle);
    }
    const D3D11BindingLayout* GetBindingLayout(BindingLayoutHandle handle) const
    {
        return m_bindingLayouts.Get(handle);
    }
    const D3D11ResourceSet* GetResourceSet(ResourceSetHandle handle) const
    {
        return m_resourceSets.Get(handle);
    }
    const PipelineState* GetPipeline(PipelineHandle handle) const
    {
        return m_pipelines.Get(handle);
    }

private:
    bool InitDirect3D();
    bool CreateBackBufferTexture(); // 스왑체인의 버퍼 0을 Texture로 등록
    bool CreateDepthTexture();
    static void SetDebugName(ID3D11DeviceChild* object, const std::string& name, uint32_t index);

public:
    UINT m_numQualityLevels = 0;

private:
    HWND m_mainWindow = nullptr;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    bool m_vsync = false;
    bool m_tearingSupported = false;
    bool m_debugLayer = false;
    bool m_imguiInitialized = false;
    UINT m_swapChainFlags = 0; // ResizeBuffers에 같은 값을 넘겨야 함

    uint64_t m_frameCounter = 0;
    uint32_t m_frameIndex = 0;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain1> m_swapChain;

    // 풀과 캐시는 m_device보다 뒤에 선언함. 멤버는 선언의 역순으로 파괴되므로
    // 이것들이 먼저 사라지고 나서 장치가 해제됨. 순서가 반대면 Debug Layer가
    // 종료 시 Live Object를 보고함. ReleaseDevice도 같은 순서로 비움.
    ResourcePool<D3D11Buffer, BufferHandle> m_buffers;
    ResourcePool<D3D11Texture, TextureHandle> m_textures;
    ResourcePool<D3D11Sampler, SamplerHandle> m_samplers;
    ResourcePool<D3D11Shader, ShaderHandle> m_shaders;
    ResourcePool<D3D11BindingLayout, BindingLayoutHandle> m_bindingLayouts;
    ResourcePool<D3D11ResourceSet, ResourceSetHandle> m_resourceSets;
    PipelineStateCache m_pipelines;
    D3D11CommandList
        m_commandList; // 소유권 없음(D3D11Device* 만 보관). 모든 바인딩·드로우가 이것을 거침

    TextureHandle m_backBuffer;
    TextureHandle m_depthBuffer;

    // 10단계: 타임스탬프 쿼리. 프레임마다 DISJOINT 쿼리 하나 + TIMESTAMP 쿼리 kMaxTimestamps 개.
    // 세트 6개를 돌려 쓰고, 다시 쓰기 직전(6프레임 뒤)에 읽음. DXGI 가 최대 3프레임을 큐에 둘 수 있어
    // 2~3프레임 전 세트는 아직 GPU 에서 처리 중일 수 있고, 결과를 읽지 않은 쿼리에 End 를 호출하면 Debug Layer 가 경고함.
    static constexpr uint32_t kTimestampSets = 6;
    struct TimestampSet
    {
        ComPtr<ID3D11Query> disjoint;
        ComPtr<ID3D11Query> timestamps[kMaxTimestamps];
        bool written[kMaxTimestamps] = {};
        uint64_t frameNumber = 0;
        bool active = false;
    };
    TimestampSet m_timestampSets[kTimestampSets];
    uint64_t m_lastTicks[kMaxTimestamps] = {};
    uint64_t m_lastFrequency = 0;
    uint64_t m_lastFrameNumber = 0;
    bool m_hasTimestampResults = false;
    bool CreateTimestampQueries();
    void ReadBackBuffer(); // 11단계: EndFrame 이 Present 직전에 호출함
    bool m_readbackRequested = false;
    bool m_readbackReady = false;
    std::vector<uint8_t> m_readbackPixels;
    uint32_t m_readbackWidth = 0;
    uint32_t m_readbackHeight = 0;
    void ResolveTimestamps(TimestampSet& set);
};
