#pragma once
namespace nereides
{
inline constexpr char kMeshShader[] = R"hlsl(
cbuffer ObjectData : register(b0)
{
    row_major float4x4 worldViewProjection;
    float4 tint;
};
cbuffer SkinData : register(b1) { row_major float4x4 bones[128]; };
struct VertexInput { float3 position:POSITION; float3 color:COLOR; uint4 boneIds:BLENDINDICES; float4 weights:BLENDWEIGHT; };
struct PixelInput { float4 position:SV_POSITION; float3 color:COLOR; };
PixelInput VSMain(VertexInput v)
{
    float4 position=float4(v.position,1);
    if(dot(v.weights,float4(1,1,1,1))>0)
        position=mul(position,bones[v.boneIds.x])*v.weights.x+mul(position,bones[v.boneIds.y])*v.weights.y+
            mul(position,bones[v.boneIds.z])*v.weights.z+mul(position,bones[v.boneIds.w])*v.weights.w;
    PixelInput o; o.position=mul(position,worldViewProjection); o.color=v.color*tint.rgb; return o;
}
float4 PSMain(PixelInput p):SV_TARGET { return float4(p.color,1); }
)hlsl";
}
