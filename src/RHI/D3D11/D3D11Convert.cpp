#include "RHI/Compat/RHICommon.h"
#include "RHI/D3D11/D3D11Convert.h"
#include "RHI/Compat/SherlockLog.h"

namespace D3D11Convert
{
DXGI_FORMAT ToDXGI(Format format)
{
    switch (format)
    {
    case Format::Unknown:
        return DXGI_FORMAT_UNKNOWN;
    case Format::R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::D24_UNORM_S8_UINT:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32_FLOAT:
        return DXGI_FORMAT_D32_FLOAT;
    case Format::R32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case Format::R32G32_FLOAT:
        return DXGI_FORMAT_R32G32_FLOAT;
    case Format::R32G32B32_FLOAT:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case Format::R32G32B32A32_FLOAT:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::R16G16B16A16_FLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::R32G32B32A32_UINT:
        return DXGI_FORMAT_R32G32B32A32_UINT;
    case Format::R16_UINT:
        return DXGI_FORMAT_R16_UINT;
    case Format::R32_UINT:
        return DXGI_FORMAT_R32_UINT;
    default:
        Log::Error("D3D11Convert : 알 수 없는 Format %d", static_cast<int>(format));
        return DXGI_FORMAT_UNKNOWN;
    }
}

D3D11_FILL_MODE ToD3D11(FillMode fill)
{
    switch (fill)
    {
    case FillMode::Solid:
        return D3D11_FILL_SOLID;
    case FillMode::Wireframe:
        return D3D11_FILL_WIREFRAME;
    default:
        Log::Error("D3D11Convert : 알 수 없는 FillMode %d", static_cast<int>(fill));
        return D3D11_FILL_SOLID;
    }
}

D3D11_CULL_MODE ToD3D11(CullMode cull)
{
    switch (cull)
    {
    case CullMode::None:
        return D3D11_CULL_NONE;
    case CullMode::Front:
        return D3D11_CULL_FRONT;
    case CullMode::Back:
        return D3D11_CULL_BACK;
    default:
        Log::Error("D3D11Convert : 알 수 없는 CullMode %d", static_cast<int>(cull));
        return D3D11_CULL_BACK;
    }
}

D3D11_COMPARISON_FUNC ToD3D11(CompareFunc func)
{
    switch (func)
    {
    case CompareFunc::Never:
        return D3D11_COMPARISON_NEVER;
    case CompareFunc::Less:
        return D3D11_COMPARISON_LESS;
    case CompareFunc::Equal:
        return D3D11_COMPARISON_EQUAL;
    case CompareFunc::LessEqual:
        return D3D11_COMPARISON_LESS_EQUAL;
    case CompareFunc::Greater:
        return D3D11_COMPARISON_GREATER;
    case CompareFunc::NotEqual:
        return D3D11_COMPARISON_NOT_EQUAL;
    case CompareFunc::GreaterEqual:
        return D3D11_COMPARISON_GREATER_EQUAL;
    case CompareFunc::Always:
        return D3D11_COMPARISON_ALWAYS;
    default:
        Log::Error("D3D11Convert : 알 수 없는 CompareFunc %d", static_cast<int>(func));
        return D3D11_COMPARISON_LESS;
    }
}

D3D11_PRIMITIVE_TOPOLOGY ToD3D11(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::TriangleList:
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip:
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::LineList:
        return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::PointList:
        return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    default:
        Log::Error("D3D11Convert : 알 수 없는 PrimitiveTopology %d", static_cast<int>(topology));
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

D3D11_BLEND ToD3D11(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero:
        return D3D11_BLEND_ZERO;
    case BlendFactor::One:
        return D3D11_BLEND_ONE;
    case BlendFactor::SrcAlpha:
        return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:
        return D3D11_BLEND_INV_SRC_ALPHA;
    case BlendFactor::SrcColor:
        return D3D11_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor:
        return D3D11_BLEND_INV_SRC_COLOR;
    case BlendFactor::DstAlpha:
        return D3D11_BLEND_DEST_ALPHA;
    case BlendFactor::InvDstAlpha:
        return D3D11_BLEND_INV_DEST_ALPHA;
    default:
        Log::Error("D3D11Convert : 알 수 없는 BlendFactor %d", static_cast<int>(factor));
        return D3D11_BLEND_ONE;
    }
}

D3D11_BLEND_OP ToD3D11(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add:
        return D3D11_BLEND_OP_ADD;
    case BlendOp::Subtract:
        return D3D11_BLEND_OP_SUBTRACT;
    case BlendOp::RevSubtract:
        return D3D11_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:
        return D3D11_BLEND_OP_MIN;
    case BlendOp::Max:
        return D3D11_BLEND_OP_MAX;
    default:
        Log::Error("D3D11Convert : 알 수 없는 BlendOp %d", static_cast<int>(op));
        return D3D11_BLEND_OP_ADD;
    }
}

const char* ToSemanticName(VertexSemantic semantic)
{
    switch (semantic)
    {
    case VertexSemantic::Position:
        return "POSITION";
    case VertexSemantic::Normal:
        return "NORMAL";
    case VertexSemantic::Color:
        return "COLOR";
    case VertexSemantic::TexCoord:
        return "TEXCOORD";
    case VertexSemantic::Tangent:
        return "TANGENT";
    case VertexSemantic::BlendIndices:
        return "BLENDINDICES";
    case VertexSemantic::BlendWeight:
        return "BLENDWEIGHT";
    default:
        Log::Error("D3D11Convert : 알 수 없는 VertexSemantic %d", static_cast<int>(semantic));
        return "POSITION";
    }
}

D3D11_FILTER ToD3D11(SamplerFilter filter)
{
    switch (filter)
    {
    case SamplerFilter::Point:
        return D3D11_FILTER_MIN_MAG_MIP_POINT;
    case SamplerFilter::Linear:
        return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    case SamplerFilter::Anisotropic:
        return D3D11_FILTER_ANISOTROPIC;
    case SamplerFilter::Comparison:
        return D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    default:
        Log::Error("D3D11Convert : 알 수 없는 SamplerFilter %d", static_cast<int>(filter));
        return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    }
}

D3D11_TEXTURE_ADDRESS_MODE ToD3D11(SamplerAddress address)
{
    switch (address)
    {
    case SamplerAddress::Wrap:
        return D3D11_TEXTURE_ADDRESS_WRAP;
    case SamplerAddress::Clamp:
        return D3D11_TEXTURE_ADDRESS_CLAMP;
    case SamplerAddress::Mirror:
        return D3D11_TEXTURE_ADDRESS_MIRROR;
    case SamplerAddress::Border:
        return D3D11_TEXTURE_ADDRESS_BORDER;
    default:
        Log::Error("D3D11Convert : 알 수 없는 SamplerAddress %d", static_cast<int>(address));
        return D3D11_TEXTURE_ADDRESS_WRAP;
    }
}

D3D11_RASTERIZER_DESC ToD3D11(const RasterizerDesc& desc)
{
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = ToD3D11(desc.fill);
    rd.CullMode = ToD3D11(desc.cull);
    rd.FrontCounterClockwise = desc.frontCounterClockwise;
    rd.DepthBias = desc.depthBias;
    rd.DepthBiasClamp = desc.depthBiasClamp;
    rd.SlopeScaledDepthBias = desc.slopeScaledDepthBias;
    rd.DepthClipEnable = desc.depthClipEnable;
    rd.ScissorEnable = desc.scissorEnable;
    rd.MultisampleEnable = desc.multisampleEnable;
    rd.AntialiasedLineEnable = desc.antialiasedLineEnable;
    return rd;
}

D3D11_DEPTH_STENCIL_DESC ToD3D11(const DepthStencilDesc& desc)
{
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = desc.depthEnable;
    dd.DepthWriteMask = desc.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = ToD3D11(desc.depthFunc);
    dd.StencilEnable = desc.stencilEnable;
    dd.StencilReadMask = desc.stencilReadMask;
    dd.StencilWriteMask = desc.stencilWriteMask;

    // 스텐실 연산은 아직 Desc에 없음. D3D11 기본값(KEEP / ALWAYS)으로 채움.
    const D3D11_DEPTH_STENCILOP_DESC defaultOp = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                                                  D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    dd.FrontFace = defaultOp;
    dd.BackFace = defaultOp;
    return dd;
}

D3D11_BLEND_DESC ToD3D11(const BlendDesc& desc)
{
    D3D11_BLEND_DESC bd = {};
    bd.AlphaToCoverageEnable = desc.alphaToCoverage;
    bd.IndependentBlendEnable = desc.independentBlend;
    for (uint32_t i = 0; i < kMaxRenderTargets; ++i)
    {
        const RenderTargetBlendDesc& src = desc.rt[i];
        D3D11_RENDER_TARGET_BLEND_DESC& dst = bd.RenderTarget[i];
        dst.BlendEnable = src.blendEnable;
        dst.SrcBlend = ToD3D11(src.srcColor);
        dst.DestBlend = ToD3D11(src.dstColor);
        dst.BlendOp = ToD3D11(src.colorOp);
        dst.SrcBlendAlpha = ToD3D11(src.srcAlpha);
        dst.DestBlendAlpha = ToD3D11(src.dstAlpha);
        dst.BlendOpAlpha = ToD3D11(src.alphaOp);
        dst.RenderTargetWriteMask = src.writeMask;
    }
    return bd;
}
}
