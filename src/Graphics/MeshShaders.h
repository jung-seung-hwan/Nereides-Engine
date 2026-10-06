#pragma once
namespace nereides
{
inline constexpr char kMeshShader[] = R"hlsl(
cbuffer ObjectData : register(b0)
{
    row_major float4x4 worldViewProjection;
    float4 tint;
};
struct VertexInput { float3 position:POSITION; float3 color:COLOR; };
struct PixelInput { float4 position:SV_POSITION; float3 color:COLOR; };
PixelInput VSMain(VertexInput v)
{
    PixelInput o; o.position=mul(float4(v.position,1),worldViewProjection); o.color=v.color*tint.rgb; return o;
}
float4 PSMain(PixelInput p):SV_TARGET { return float4(p.color,1); }
)hlsl";
}
