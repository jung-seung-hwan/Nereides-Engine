#pragma once
#include "RHI/D3D11/D3D11Common.h"
#include "RHI/PipelineTypes.h"
#include "RHI/ResourceDesc.h" // SamplerFilter, SamplerAddress

// API 중립 열거형/Desc → D3D11 타입 변환. 이 폴더 안에서만 씀.
// 대응하는 값이 없으면 Log::Error를 남기고 안전한 기본값을 돌려줌.
namespace D3D11Convert
{
DXGI_FORMAT ToDXGI(Format format);
D3D11_FILL_MODE ToD3D11(FillMode fill);
D3D11_CULL_MODE ToD3D11(CullMode cull);
D3D11_COMPARISON_FUNC ToD3D11(CompareFunc func);
D3D11_PRIMITIVE_TOPOLOGY ToD3D11(PrimitiveTopology topology);
D3D11_BLEND ToD3D11(BlendFactor factor);
D3D11_BLEND_OP ToD3D11(BlendOp op);
const char* ToSemanticName(VertexSemantic semantic);
D3D11_FILTER ToD3D11(SamplerFilter filter);
D3D11_TEXTURE_ADDRESS_MODE ToD3D11(SamplerAddress address);

D3D11_RASTERIZER_DESC ToD3D11(const RasterizerDesc& desc);
D3D11_DEPTH_STENCIL_DESC ToD3D11(const DepthStencilDesc& desc);
D3D11_BLEND_DESC ToD3D11(const BlendDesc& desc);
}
