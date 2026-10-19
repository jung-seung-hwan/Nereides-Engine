#include "Common.hlsli"

VSOutput TransformVertex(float3 position, float4 col, float3 normal, float2 uv, float4 tangent)
{
    VSOutput vsOutput = (VSOutput)0;

    // 행벡터 곱 mul(v, M). C++가 행렬을 전치해서 올렸으므로 이 순서가 맞음.
    const float4 localPosition = float4(position, 1.0f);
    const float4 worldPosition = mul(localPosition, world);

    vsOutput.position = mul(worldPosition, viewProj);
    vsOutput.worldPosition = worldPosition.xyz;
    vsOutput.col = col;
    vsOutput.uv = uv * uvScale;
    // 노멀은 (W⁻¹)ᵀ 로 변환함. 그래야 비균등 스케일에서도 표면과 수직을 유지함.
    vsOutput.normal = normalize(mul(float4(normal, 0.0f), worldInvTranspose).xyz);
    // 탄젠트는 표면 "위"의 방향이므로 월드 행렬로 변환함 (방향 벡터, w = 0). 손잡이(w)는 그대로 둠.
    vsOutput.tangent = float4(normalize(mul(float4(tangent.xyz, 0.0f), world).xyz), tangent.w);
    // 6단계: 같은 월드 위치를 광원 시점에서 본 클립 좌표. PS 에서 그림자 맵 UV 로 바꿈.
    vsOutput.shadowPosition = mul(worldPosition, lightViewProj);

    return vsOutput;
}

VSOutput VS_Main(VSInput vsInput)
{
    return TransformVertex(vsInput.position, vsInput.col, vsInput.normal, vsInput.uv, vsInput.tangent);
}

// Nereides 추가: 스키닝. 뼈 행렬로 모델 공간 위치·노멀·탄젠트를 먼저 변형한 뒤 월드 행렬을 적용함.
VSOutput VS_Skinned(VSInputSkinned vsInput)
{
    float3 position = vsInput.position;
    float3 normal = vsInput.normal;
    float3 tangent = vsInput.tangent.xyz;

    if (dot(vsInput.weights, float4(1.0f, 1.0f, 1.0f, 1.0f)) > 0.0f)
    {
        const matrix skin = bones[vsInput.boneIds.x] * vsInput.weights.x + bones[vsInput.boneIds.y] * vsInput.weights.y +
                            bones[vsInput.boneIds.z] * vsInput.weights.z + bones[vsInput.boneIds.w] * vsInput.weights.w;
        position = mul(float4(position, 1.0f), skin).xyz;
        normal = mul(float4(normal, 0.0f), skin).xyz;
        tangent = mul(float4(tangent, 0.0f), skin).xyz;
    }
    return TransformVertex(position, vsInput.col, normal, vsInput.uv, float4(tangent, vsInput.tangent.w));
}
