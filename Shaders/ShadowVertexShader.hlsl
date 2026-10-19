#include "Common.hlsli"

// 6단계: 그림자 맵 패스. 깊이만 쓰므로 픽셀 셰이더가 없고 위치만 출력함.
// 입력 레이아웃은 메인 패스와 같은 VERTEX(64B)를 씀. VS 가 POSITION 만 읽어도
// VS 시그니처의 모든 원소가 레이아웃에 있기만 하면 D3D11 입력 레이아웃 검증을 통과함.
struct ShadowVSInput
{
    float3 position : POSITION;
};

// Nereides 추가: 스키닝 메시의 그림자. 위치와 뼈 속성만 읽음.
struct ShadowSkinnedVSInput
{
    float3 position : POSITION;
    uint4 boneIds : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

float4 VS_Shadow(ShadowVSInput input) : SV_POSITION
{
    const float4 worldPosition = mul(float4(input.position, 1.0f), world);
    return mul(worldPosition, lightViewProj);
}

float4 VS_ShadowSkinned(ShadowSkinnedVSInput input) : SV_POSITION
{
    float4 position = float4(input.position, 1.0f);
    if (dot(input.weights, float4(1.0f, 1.0f, 1.0f, 1.0f)) > 0.0f)
    {
        const matrix skin = bones[input.boneIds.x] * input.weights.x + bones[input.boneIds.y] * input.weights.y +
                            bones[input.boneIds.z] * input.weights.z + bones[input.boneIds.w] * input.weights.w;
        position = mul(position, skin);
    }
    const float4 worldPosition = mul(position, world);
    return mul(worldPosition, lightViewProj);
}
