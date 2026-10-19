#include "RHI/Compat/RHICommon.h"
#include "RHI/D3D11/D3D11CommandList.h"
#include "RHI/D3D11/D3D11Device.h"
#include "RHI/D3D11/D3D11Convert.h"
#include "RHI/Compat/SherlockLog.h"

void D3D11CommandList::ErrorOnce(bool& flag, const char* message)
{
    if (flag)
        return;
    flag = true;
    Log::Error("%s", message);
}

void D3D11CommandList::Init(D3D11Device* device)
{
    m_device = device;
    m_inPass = false;
    for (TextureHandle& h : m_vsSrv)
        h = TextureHandle{};
    for (TextureHandle& h : m_psSrv)
        h = TextureHandle{};
}

// ------------------------------------------------------------------ 렌더 패스

void D3D11CommandList::BeginRenderPass(const RenderPassDesc& desc)
{
    static bool warnedNested = false;
    ID3D11DeviceContext* context = m_device ? m_device->GetContext() : nullptr;
    if (context == nullptr)
        return;
    if (m_inPass)
    {
        ErrorOnce(warnedNested,
                  "BeginRenderPass : 이전 패스가 끝나지 않음. EndRenderPass 를 먼저 부를 것.");
        EndRenderPass();
    }
    m_currentPass = desc;
    m_inPass = true;
    ++m_stats.renderPasses;

    // (1) 타깃이 될 텍스처가 SRV 슬롯에 남아 있으면 먼저 풂.
    for (uint32_t i = 0; i < desc.colorCount; ++i)
        UnbindShaderResourcesOf(desc.colors[i].texture);
    if (desc.depth.texture.IsValid())
        UnbindShaderResourcesOf(desc.depth.texture);

    // (2) 뷰를 모음. 뷰포트 기본 크기는 첫 attachment 에서 가져옴.
    ID3D11RenderTargetView* rtvs[kMaxRenderTargets] = {};
    uint32_t width = 0, height = 0;
    for (uint32_t i = 0; i < desc.colorCount; ++i)
    {
        D3D11Texture* texture = m_device->GetTexture(desc.colors[i].texture);
        if (texture == nullptr)
        {
            Log::Error("BeginRenderPass : 컬러 attachment %u 의 텍스처 핸들이 유효하지 않음 (%s).",
                       i, desc.debugName ? desc.debugName : "");
            continue;
        }
        rtvs[i] = texture->GetRTV(m_device->GetDevice(), desc.colors[i].srgbView);
        if (width == 0)
        {
            width = texture->desc.width;
            height = texture->desc.height;
        }
    }
    ID3D11DepthStencilView* dsv = nullptr;
    if (desc.depth.texture.IsValid())
    {
        D3D11Texture* texture = m_device->GetTexture(desc.depth.texture);
        if (texture == nullptr)
        {
            Log::Error("BeginRenderPass : 깊이 attachment 의 텍스처 핸들이 유효하지 않음 (%s).",
                       desc.debugName ? desc.debugName : "");
        }
        else
        {
            dsv = texture->GetDSV(m_device->GetDevice());
            if (width == 0)
            {
                width = texture->desc.width;
                height = texture->desc.height;
            }
        }
    }

    // (3) 바인딩. flip 모델에서는 Present 후 백버퍼 바인딩이 풀리므로 패스마다 다시 바인딩함.
    context->OMSetRenderTargets(desc.colorCount, desc.colorCount ? rtvs : nullptr, dsv);

    // (4) Load op. Clear 만 실제로 동작하고, Load/DontCare 는 D3D11에서 동작이 같음.
    for (uint32_t i = 0; i < desc.colorCount; ++i)
    {
        if (rtvs[i] && desc.colors[i].load == LoadOp::Clear)
        {
            context->ClearRenderTargetView(rtvs[i], desc.colors[i].clearColor);
        }
    }
    if (dsv && desc.depth.load == LoadOp::Clear)
    {
        context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                       desc.depth.clearDepth, desc.depth.clearStencil);
    }

    // (5) 뷰포트. PSO 밖의 동적 상태임.
    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = desc.viewport.x;
    viewport.TopLeftY = desc.viewport.y;
    viewport.Width = desc.viewport.width > 0.0f ? desc.viewport.width : static_cast<float>(width);
    viewport.Height =
        desc.viewport.height > 0.0f ? desc.viewport.height : static_cast<float>(height);
    viewport.MinDepth = desc.viewport.minDepth;
    viewport.MaxDepth = desc.viewport.maxDepth;
    context->RSSetViewports(1, &viewport);
}

void D3D11CommandList::EndRenderPass()
{
    ID3D11DeviceContext* context = m_device ? m_device->GetContext() : nullptr;
    if (!m_inPass || context == nullptr)
        return;
    // Store op 는 D3D11에서 할 일이 없음. 타깃을 풀어 다음 패스가 SRV 로 읽어도 경고가 나지 않게 함.
    context->OMSetRenderTargets(0, nullptr, nullptr);
    m_inPass = false;
}

void D3D11CommandList::UnbindShaderResourcesOf(TextureHandle texture)
{
    ID3D11DeviceContext* context = m_device->GetContext();
    ID3D11ShaderResourceView* nullSrv = nullptr;
    for (uint32_t slot = 0; slot < kMaxSrvRegister; ++slot)
    {
        if (m_vsSrv[slot] == texture)
        {
            context->VSSetShaderResources(slot, 1, &nullSrv);
            m_vsSrv[slot] = TextureHandle{};
            ++m_stats.srvUnbinds;
        }
        if (m_psSrv[slot] == texture)
        {
            context->PSSetShaderResources(slot, 1, &nullSrv);
            m_psSrv[slot] = TextureHandle{};
            ++m_stats.srvUnbinds;
        }
    }
}

// ------------------------------------------------------------------ 상태·바인딩

void D3D11CommandList::SetPipelineState(PipelineHandle handle)
{
    static bool warned = false;
    const PipelineState* state = m_device ? m_device->GetPipeline(handle) : nullptr;
    if (state == nullptr)
    {
        ErrorOnce(warned, "SetPipelineState : 유효하지 않은 PipelineHandle.");
        return;
    }
    state->Bind(m_device->GetContext());
}

void D3D11CommandList::SetResourceSet(ResourceSetHandle handle)
{
    static bool warned = false;
    const D3D11ResourceSet* set = m_device ? m_device->GetResourceSet(handle) : nullptr;
    const D3D11BindingLayout* layout = set ? m_device->GetBindingLayout(set->desc.layout) : nullptr;
    if (set == nullptr || layout == nullptr)
    {
        ErrorOnce(warned, "SetResourceSet : 유효하지 않은 ResourceSetHandle 또는 레이아웃.");
        return;
    }
    ID3D11DeviceContext* context = m_device->GetContext();
    const uint32_t frameIndex = m_device->GetFrameIndex();

    // D3D12라면 SetGraphicsRootDescriptorTable 한 번이면 됨. D3D11은 슬롯마다 스테이지별 호출로 풀어냄.
    for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
    {
        const BindingSlot& slot = layout->desc.slots[i];
        const ResourceBinding& binding = set->desc.bindings[i];
        switch (slot.type)
        {
        case BindingType::ConstantBuffer: {
            const D3D11Buffer* buffer = m_device->GetBuffer(binding.buffer);
            ID3D11Buffer* d3dBuffer = buffer ? buffer->Get(frameIndex) : nullptr;
            if (slot.stageMask & ShaderStageMask_Vertex)
                context->VSSetConstantBuffers(slot.reg, 1, &d3dBuffer);
            if (slot.stageMask & ShaderStageMask_Pixel)
                context->PSSetConstantBuffers(slot.reg, 1, &d3dBuffer);
            break;
        }
        case BindingType::ShaderResource: {
            D3D11Texture* texture = m_device->GetTexture(binding.texture);
            ID3D11ShaderResourceView* srv =
                texture ? texture->GetSRV(m_device->GetDevice()) : nullptr;
            if (slot.stageMask & ShaderStageMask_Vertex)
            {
                context->VSSetShaderResources(slot.reg, 1, &srv);
                if (slot.reg < kMaxSrvRegister)
                    m_vsSrv[slot.reg] = binding.texture;
            }
            if (slot.stageMask & ShaderStageMask_Pixel)
            {
                context->PSSetShaderResources(slot.reg, 1, &srv);
                if (slot.reg < kMaxSrvRegister)
                    m_psSrv[slot.reg] = binding.texture;
            }
            break;
        }
        case BindingType::Sampler: {
            const D3D11Sampler* sampler = m_device->GetSampler(binding.sampler);
            ID3D11SamplerState* state = sampler ? sampler->sampler.Get() : nullptr;
            if (slot.stageMask & ShaderStageMask_Vertex)
                context->VSSetSamplers(slot.reg, 1, &state);
            if (slot.stageMask & ShaderStageMask_Pixel)
                context->PSSetSamplers(slot.reg, 1, &state);
            break;
        }
        }
    }
}

void D3D11CommandList::SetVertexBuffer(BufferHandle handle, uint32_t slot, uint32_t offset)
{
    static bool warned = false;
    const D3D11Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
    if (buffer == nullptr)
    {
        ErrorOnce(warned, "SetVertexBuffer : 유효하지 않은 BufferHandle.");
        return;
    }
    ID3D11Buffer* d3dBuffer = buffer->Get(m_device->GetFrameIndex());
    const UINT stride = buffer->desc.stride;
    m_device->GetContext()->IASetVertexBuffers(slot, 1, &d3dBuffer, &stride, &offset);
}

void D3D11CommandList::SetIndexBuffer(BufferHandle handle, Format indexFormat)
{
    static bool warned = false;
    const D3D11Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
    if (buffer == nullptr)
    {
        ErrorOnce(warned, "SetIndexBuffer : 유효하지 않은 BufferHandle.");
        return;
    }
    m_device->GetContext()->IASetIndexBuffer(buffer->Get(m_device->GetFrameIndex()),
                                             D3D11Convert::ToDXGI(indexFormat), 0);
}

// ------------------------------------------------------------------ 드로우

void D3D11CommandList::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
{
    static bool warnedOutside = false;
    if (!m_inPass)
    {
        ErrorOnce(warnedOutside,
                  "DrawIndexed : 렌더 패스 밖에서 드로우. BeginRenderPass 가 필요하다.");
        return;
    }
    m_device->GetContext()->DrawIndexed(indexCount, startIndex, baseVertex);
}

void D3D11CommandList::Draw(uint32_t vertexCount, uint32_t startVertex)
{
    static bool warnedOutside = false;
    if (!m_inPass)
    {
        ErrorOnce(warnedOutside, "Draw : 렌더 패스 밖에서 드로우. BeginRenderPass 가 필요하다.");
        return;
    }
    m_device->GetContext()->Draw(vertexCount, startVertex);
}

// ------------------------------------------------------------------ 전이·복사

void D3D11CommandList::Barrier(TextureHandle handle, ResourceState before, ResourceState after)
{
    ++m_stats.barriers;
    D3D11Texture* texture = m_device ? m_device->GetTexture(handle) : nullptr;
    if (texture == nullptr)
    {
        static bool warned = false;
        ErrorOnce(warned, "Barrier : 유효하지 않은 TextureHandle.");
        return;
    }
#if defined(_DEBUG)
    // D3D11에서는 할 일이 없음. 대신 호출 코드가 적어 놓은 before 가 실제 추적 상태와
    // 맞는지 확인함. 어긋나면 D3D12에서는 Debug Layer 에러가 나는 경우임.
    if (texture->state != before)
    {
        Log::Warn("Barrier : '%s' 의 현재 상태는 %s 인데 before=%s 로 호출됨 (after=%s).",
                  texture->name.c_str(), ToString(texture->state), ToString(before),
                  ToString(after));
    }
#else
    (void)before;
#endif
    texture->state = after;
}

void D3D11CommandList::CopyBuffer(BufferHandle dst, BufferHandle src)
{
    const D3D11Buffer* d = m_device ? m_device->GetBuffer(dst) : nullptr;
    const D3D11Buffer* s = m_device ? m_device->GetBuffer(src) : nullptr;
    if (d == nullptr || s == nullptr)
    {
        Log::Error("CopyBuffer : 유효하지 않은 BufferHandle.");
        return;
    }
    if (d->desc.size != s->desc.size)
    {
        Log::Error("CopyBuffer : 크기가 다름 (%s %u, %s %u).", d->name.c_str(), d->desc.size,
                   s->name.c_str(), s->desc.size);
        return;
    }
    const uint32_t frameIndex = m_device->GetFrameIndex();
    m_device->GetContext()->CopyResource(d->Get(frameIndex), s->Get(frameIndex));
}

void D3D11CommandList::WriteTimestamp(uint32_t slot)
{
    if (m_device)
        m_device->WriteTimestamp(slot);
}
