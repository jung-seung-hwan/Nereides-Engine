#pragma once
#include "RHI/D3D11/D3D11Common.h"
#include "RHI/CommandList.h"
#include "RHI/BindingTypes.h"

class D3D11Device;

// RHI::CommandList 의 D3D11 구현. ImmediateContext 의 얇은 래퍼라 "기록"이 곧 실행임.
// 6단계의 CommandList 클래스에서 이름만 바꾸고 인터페이스를 상속하게 했음. 메서드 본문은 같음.
class D3D11CommandList final : public RHI::CommandList
{
public:
    void Init(D3D11Device* device); // D3D11Device::Init 이 호출함

    void BeginRenderPass(const RenderPassDesc& desc) override;
    void EndRenderPass() override;
    bool IsInRenderPass() const override
    {
        return m_inPass;
    }

    void SetPipelineState(PipelineHandle handle) override;
    void SetResourceSet(ResourceSetHandle handle) override;
    void SetVertexBuffer(BufferHandle handle, uint32_t slot = 0, uint32_t offset = 0) override;
    void SetIndexBuffer(BufferHandle handle, Format indexFormat = Format::R32_UINT) override;

    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, int32_t baseVertex = 0) override;
    void Draw(uint32_t vertexCount, uint32_t startVertex = 0) override;

    void Barrier(TextureHandle texture, ResourceState before, ResourceState after) override;
    void CopyBuffer(BufferHandle dst, BufferHandle src) override;

    void WriteTimestamp(uint32_t slot) override; // 10단계: Device 의 쿼리 세트에 위임
    const Stats& GetStats() const override
    {
        return m_stats;
    }
    void ResetStats() override
    {
        m_stats = Stats{};
    }

private:
    void UnbindShaderResourcesOf(TextureHandle texture);
    static void ErrorOnce(bool& flag, const char* message);

private:
    D3D11Device* m_device = nullptr;
    bool m_inPass = false;
    RenderPassDesc m_currentPass;
    Stats m_stats;

    // 현재 SRV 슬롯마다 어떤 텍스처가 바인딩돼 있는지 기록함. 렌더 패스가 그 텍스처를 타깃으로 잡기 전에
    // 풀어 주기 위함임. D3D11은 같은 리소스가 SRV와 RTV/DSV에 동시에 바인딩되면 경고한 뒤
    // 강제로 SRV를 NULL 로 만듦 — 우리가 먼저 풀어 두면 경고가 나지 않고 추적도 정확해짐.
    TextureHandle m_vsSrv[kMaxSrvRegister];
    TextureHandle m_psSrv[kMaxSrvRegister];
};
