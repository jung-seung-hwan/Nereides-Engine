#pragma once

namespace nereides
{
// Embedded for this first draw: no runtime shader-file path or copying is required.
inline constexpr char kTriangleShaderSource[] = R"hlsl(
struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
)hlsl";
}
