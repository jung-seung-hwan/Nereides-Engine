// SherlockEngine 공용 셰이더 헤더. (Nereides 포팅: 스키닝용 b4·VSInputSkinned 추가)
// 상수버퍼 레이아웃은 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
// C++ 쪽 static_assert와 Debug 빌드의 D3DReflect 대조로 이를 검사함.
//
// register(b#, space0) 표기는 SM 5.0 fxc가 error X3721로 거부함 (5.1 이상만 지원). 원문의
// "fxc가 무시한다"는 설명은 틀렸음. space는 8단계에서 SM 5.1/6.0으로 올릴 때 넣음.
//
// 레지스터 배정 (6단계, ShaderConstants.h 와 동일):
//   재질 셋:   t0..t7  s0..s3   — 알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4, 재질 샘플러 s0
//   프레임 셋: t8..t15 s4..s7   — 그림자 맵 t8, 비교 샘플러 s4, 스카이박스 t9, 환경 샘플러 s5,
//                                IBL 확산 조도 t10, IBL 반사 프리필터 t11, BRDF LUT t12 (2026-10-08)
#ifndef SHERLOCK_COMMON_HLSLI
#define SHERLOCK_COMMON_HLSLI

static const uint MAX_LIGHTS = 8;
static const uint LIGHT_DIRECTIONAL = 0;
static const uint LIGHT_POINT = 1;
static const uint LIGHT_SPOT = 2;
static const float EPSILON = 0.0001f;
static const float PI = 3.14159265f;

// 80바이트 = 16바이트 레지스터 5개. float3 뒤에 스칼라를 두어 경계를 맞춤.
struct LightData
{
    float3 position;
    float intensity;

    float3 direction;
    float range;

    float3 color;
    uint type;

    float3 attenuation;
    float innerConeCos;

    float outerConeCos;
    float3 padding;
};

// b0: 프레임에 한 번. VS와 PS 모두 읽음. (336바이트)
cbuffer PerFrame : register(b0)
{
    matrix view;
    matrix proj;
    matrix viewProj;
    matrix lightViewProj;     // 6단계: 방향광 0의 그림자 행렬 (월드 → 광원 클립)
    float3 cameraPosition;
    float time;
    float4 shadowParams;      // x = 1/맵 크기, y = 깊이 바이어스, z = 세기(0..1), w = 사용 여부
    float4 debugParams;       // 11단계: x = 디버그 뷰 (0 조명, 1 알베도, 2 노멀, 3 깊이, 4 그림자, 5 UV, 6 금속성/거칠기, 7 AO), y = 깊이 범위
    float4 environmentParams; // 2026-10-08: x = IBL 세기, y = 반사 큐브 밉 수, z = 확산 조도를 읽을 밉, w = 환경 있음
    float4 outputParams;      // 2026-10-08: x = 노출, y = 톤매핑, z = BRDF LUT 있음
}

// b5: 후처리 패스 파라미터 (Sherlock 은 b4 — Nereides 는 b4 를 뼈 팔레트가 씀). 후처리 셋(톤 패스·블러·블룸)만 바인딩함. (80바이트, ShaderConstants.h 의 PostParamsConstants)
cbuffer PostParams : register(b5)
{
    float4 postPass;          // x = 모드 (1 블러, 2 블룸 밝기 추출+축소, 3 축소, 4 확대), y,z = 블러 방향 (텍셀 단위)
    float4 postBlur;          // x = 시그마(픽셀), y = 반경(픽셀)
    float4 postBloom;         // x = 임계값, y = knee, z = 확대 결과 배율
    float4 postView;          // xy = 입력 크기, zw = 출력 크기 (픽셀)
    float4 postGrade;         // x = GreyScale 양, y = 1 이면 색 효과를 건너뜀
}

// b1: 드로우마다. VS만. (128바이트)
cbuffer PerObject : register(b1)
{
    matrix world;
    matrix worldInvTranspose;
}

// b4: 스키닝 뼈 행렬. 스키닝 VS(VS_Skinned, VS_ShadowSkinned)만 읽음. (8192바이트)
// Nereides 추가: 뼈 행렬 = inverseBind * 뼈의 모델 공간 자세. 월드 행렬과 같이 전치해서 올림.
static const uint MAX_BONES = 128;
cbuffer Bones : register(b4)
{
    matrix bones[MAX_BONES];
}

// b2: 조명. PS만. (656바이트)
cbuffer Lights : register(b2)
{
    LightData lights[MAX_LIGHTS];
    float3 ambientColor;
    uint lightCount;
}

// b3: 재질. VS(uvScale)와 PS 모두. (64바이트)
cbuffer Material : register(b3)
{
    float4 baseColor;
    float3 emissive;          // 2026-10-08 PBR: 발광 계수 (선형)
    float metallic;           // 0 = 유전체, 1 = 금속
    float2 uvScale;
    float unlit;
    float normalStrength;     // 6단계: 노멀 맵 xy 배율. 0 = 무시
    float alphaCutoff;        // 9단계: 알파 컷아웃. 0 = 끔
    float roughness;          // 지각적 거칠기. α = roughness²
    float occlusionStrength;  // AO 텍스처를 얼마나 따를지
    float materialPadding;
}

// 재질 셋 (Layout_Material)
Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicRoughnessTexture : register(t2);   // 2026-10-08: G = 거칠기, B = 금속성 (glTF)
Texture2D occlusionTexture : register(t3);           // R = AO
Texture2D emissiveTexture : register(t4);            // sRGB
SamplerState albedoSampler : register(s0);

// 프레임 셋 (Layout_Frame). 그림자 맵은 R32_FLOAT 로 읽는 깊이 텍스처.
Texture2D shadowMap : register(t8);
SamplerComparisonState shadowSampler : register(s4);
// 2026-10-08: 씬의 스카이박스 큐브맵 (없으면 1×1 검은 큐브). 방향 벡터로 샘플함 — 길이는 상관없음.
// 프레임 셋에 둔 이유: 스카이박스뿐 아니라 이후 반사·환경광(IBL)도 모든 재질이 같은 큐브를 읽기 때문.
TextureCube environmentMap : register(t9);
SamplerState environmentSampler : register(s5);
// 2026-10-08: IBL. 셋 다 같은 선형 Clamp 샘플러(s5)로 읽음.
//   t10 확산 조도: 표면 노멀 방향의 코사인 가중 평균 복사휘도. albedo 를 곱하면 곧 확산 반사광 (π 는 이미 들어 있음)
//   t11 반사 프리필터: 반사 방향으로 읽음. 밉 0 = 거칠기 0(거울), 마지막 밉 = 거칠기 1
//   t12 BRDF LUT: u = N·V, v = 1 − roughness. RG = F0 에 곱할 값과 더할 값 (Karis 의 split-sum)
// 씬에 IBL 맵이 없고 스카이박스만 있으면 Renderer 가 스카이박스를 t10·t11 에 대신 꽂음(밉으로 근사).
TextureCube irradianceMap : register(t10);
TextureCube prefilteredMap : register(t11);
Texture2D brdfLut : register(t12);

struct VSInput
{
    float3 position : POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;   // xyz = u 증가 방향, w = 손잡이. B = cross(N, T) * w
};

// Nereides 추가: 스키닝 정점. 정적 VSInput 뒤에 뼈 인덱스·가중치가 붙음. 가중치 합이 0이면 스키닝하지 않음.
struct VSInputSkinned
{
    float3 position : POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
    uint4 boneIds : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;   // 광원 클립 공간
    float4 tangent : TANGENT0;
};

// 2026-10-08: 스카이박스 VS → PS (SkyboxVertexShader / SkyboxPixelShader).
struct SkyVSOutput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;   // 월드 공간 방향 (정규화 전). 큐브 샘플은 방향만 보므로 길이는 상관없음
};

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return (lengthSquared > EPSILON * EPSILON) ? value * rsqrt(lengthSquared) : fallback;
}

// 2026-10-08: 최종 출력. 노출을 곱하고, 켜져 있으면 ACES 근사(Narkowicz 2016)로 [0, ∞) → [0, 1] 로 누름.
// 꺼져 있으면 1 을 넘는 값은 출력 타깃에서 잘림. 결과는 선형 — sRGB RTV 가 인코딩함.
// 2026/10/20 HDR: 이 함수는 TonemapPixelShader 만 부름 (메인·스카이 PS 는 선형 HDR 값을 FP16 타깃에 그대로 씀).
// 그래서 "sRGB RTV" 는 톤 패스의 출력 뷰임.
float3 ApplyExposureToneMap(float3 color)
{
    color *= outputParams.x;
    if (outputParams.y > 0.5f)
    {
        color = saturate((color * (2.51f * color + 0.03f)) / (color * (2.43f * color + 0.59f) + 0.14f));
    }
    return color;
}

#endif // SHERLOCK_COMMON_HLSLI
