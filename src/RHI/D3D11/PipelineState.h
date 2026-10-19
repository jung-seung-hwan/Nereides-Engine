#pragma once
#include "RHI/D3D11/D3D11Common.h"
#include "RHI/PipelineTypes.h"

class D3D11Device;

// D3D11 위의 PSO 에뮬레이션.
//
// D3D12의 PSO는 셰이더·입력 레이아웃·래스터라이저·깊이스텐실·블렌드·토폴로지를
// 하나의 불변 객체로 묶음. D3D11에는 그런 객체가 없으므로, 여기서는 Desc 하나로
// D3D11 상태 객체 네 개(입력 레이아웃·래스터라이저·깊이스텐실·블렌드)를 만들고 셰이더(VS·PS)와 함께 보관해 Bind() 한 번에 전부 설정함.
//
// 이후 모든 드로우는 PSO를 통해서만 파이프라인 상태를 바꿈. Renderer 코드에
// RSSetState / VSSetShader 같은 개별 호출이 남지 않는 것이 1단계 완료 기준임.
//
// PSO에 들어가지 않는 "동적 상태": 뷰포트, 시저, 스텐실 참조값, 블렌드 팩터,
// 정점/인덱스 버퍼, 상수 버퍼·SRV 바인딩. D3D12에서도 이것들은 PSO 밖에 있음.
class PipelineState
{
public:
    bool Create(D3D11Device& device, const PipelineStateDesc& desc);
    void Bind(ID3D11DeviceContext* context) const;

    const PipelineStateDesc& GetDesc() const
    {
        return m_desc;
    }

private:
    PipelineStateDesc m_desc;

    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    ComPtr<ID3D11BlendState> m_blendState;
    D3D11_PRIMITIVE_TOPOLOGY m_topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};
