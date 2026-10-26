#include "RHI/Compat/RHICommon.h"
#include "RHI/D3D11/PipelineState.h"
#include "RHI/D3D11/D3D11Convert.h"
#include "RHI/D3D11/D3D11Device.h"
#include "RHI/Compat/SherlockLog.h"

bool PipelineState::Create(D3D11Device& device, const PipelineStateDesc& desc)
{
    m_desc = desc;
    ID3D11Device* d3d = device.GetDevice();
    if (d3d == nullptr)
    {
        Log::Error("PipelineState::Create : Device가 없음.");
        return false;
    }

    // 셰이더. 핸들로 풀에서 실제 객체를 찾음.
    const D3D11Shader* vs = device.GetShader(desc.vs);
    if (vs == nullptr || vs->stage != ShaderStage::Vertex || !vs->vs)
    {
        Log::Error("PipelineState::Create : 정점 셰이더 핸들이 유효하지 않음.");
        return false;
    }
    m_vs = vs->vs;

    // 6단계: 픽셀 셰이더는 선택 사항임. 핸들이 비어 있으면 깊이 전용 파이프라인(그림자 맵)임.
    // D3D11은 PSSetShader(nullptr)로 래스터라이저 이후 단계를 끊고, D3D12는 PS 바이트코드를 비워 두면 됨.
    m_ps.Reset();
    if (desc.ps.IsValid())
    {
        const D3D11Shader* ps = device.GetShader(desc.ps);
        if (ps == nullptr || ps->stage != ShaderStage::Pixel || !ps->ps)
        {
            Log::Error("PipelineState::Create : 픽셀 셰이더 핸들이 유효하지 않음.");
            return false;
        }
        m_ps = ps->ps;
    }

    // 입력 레이아웃. D3D11은 정점 셰이더 바이트코드로 시그니처를 검증하므로
    // 셰이더의 바이트코드 사본이 필요함. 그래서 D3D11Shader가 바이트코드를 보관함.
    if (desc.vertexLayout.attributeCount > kMaxVertexAttributes)
    {
        Log::Error("PipelineState::Create : 정점 속성 개수가 잘못됨 (%u).",
                   desc.vertexLayout.attributeCount);
        return false;
    }

    // 2026-10-08: 속성이 0개면 입력 레이아웃 없음 (정점 버퍼 없이 SV_VertexID 로 그리는 PSO). Bind 가 IASetInputLayout(nullptr) 을 함.
    HRESULT hr = S_OK;
    if (desc.vertexLayout.attributeCount > 0)
    {
        D3D11_INPUT_ELEMENT_DESC elements[kMaxVertexAttributes] = {};
        for (uint32_t i = 0; i < desc.vertexLayout.attributeCount; ++i)
        {
            const VertexAttribute& a = desc.vertexLayout.attributes[i];
            elements[i].SemanticName = D3D11Convert::ToSemanticName(a.semantic);
            elements[i].SemanticIndex = a.semanticIndex;
            elements[i].Format = D3D11Convert::ToDXGI(a.format);
            elements[i].InputSlot = a.inputSlot;
            elements[i].AlignedByteOffset = a.offset;
            elements[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            elements[i].InstanceDataStepRate = 0;
        }

        hr = d3d->CreateInputLayout(elements, desc.vertexLayout.attributeCount, vs->bytecode.data(),
                                    vs->bytecode.size(), m_inputLayout.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            Log::Error("PipelineState::Create : CreateInputLayout 실패. %s",
                       Log::HrToString(hr).c_str());
            return false;
        }
    }

    // 래스터라이저 / 깊이스텐실 / 블렌드. PSO마다 따로 만듦.
    // 같은 Desc끼리 객체를 공유해 D3D11 객체 수를 줄이는 하위 캐시는 PSO 수가 늘어나면 추가함.
    const D3D11_RASTERIZER_DESC rd = D3D11Convert::ToD3D11(desc.rasterizer);
    hr = d3d->CreateRasterizerState(&rd, m_rasterizerState.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("PipelineState::Create : CreateRasterizerState 실패. %s",
                   Log::HrToString(hr).c_str());
        return false;
    }

    const D3D11_DEPTH_STENCIL_DESC dd = D3D11Convert::ToD3D11(desc.depthStencil);
    hr = d3d->CreateDepthStencilState(&dd, m_depthStencilState.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("PipelineState::Create : CreateDepthStencilState 실패. %s",
                   Log::HrToString(hr).c_str());
        return false;
    }

    const D3D11_BLEND_DESC bd = D3D11Convert::ToD3D11(desc.blend);
    hr = d3d->CreateBlendState(&bd, m_blendState.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("PipelineState::Create : CreateBlendState 실패. %s",
                   Log::HrToString(hr).c_str());
        return false;
    }

    m_topology = D3D11Convert::ToD3D11(desc.topology);
    return true;
}

void PipelineState::Bind(ID3D11DeviceContext* context) const
{
    if (context == nullptr)
        return;

    // D3D12의 SetPipelineState 한 번에 해당하는 일곱 번의 호출.
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetPrimitiveTopology(m_topology);
    context->VSSetShader(m_vs.Get(), nullptr, 0);
    context->PSSetShader(m_ps.Get(), nullptr, 0);
    context->RSSetState(m_rasterizerState.Get());
    // 스텐실 참조값과 블렌드 팩터는 동적 상태임. 여기서는 기본값을 넘김.
    context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
}
