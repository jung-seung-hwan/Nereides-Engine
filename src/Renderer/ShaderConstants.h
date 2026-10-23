#pragma once
#include <cstdint>
#include <cstddef>
#include <DirectXMath.h>
#include "Renderer/Light.h"

// 상수버퍼 레이아웃. Shaders/Common.hlsli 의 cbuffer 네 개와 바이트 단위로 같아야 함.
//
//   갱신 빈도별로 나눔
//   b0 PerFrame  — 프레임에 한 번. 뷰·투영·광원 ViewProj·카메라 위치·시간·그림자·환경광(IBL)·출력(노출·톤매핑) 파라미터. VS와 PS 모두.
//   b1 PerObject — 드로우마다. 월드 행렬과 노멀 변환용 역전치.          VS만.
//   b2 Lights    — 프레임에 한 번(조명이 바뀔 때만이어도 됨).           PS만.
//   b3 Material  — 재질마다(4단계). 기본색·금속성·거칠기·발광·UV 배율·노멀 맵 세기·AO 세기. VS(uvScale)와 PS.
//   b4 Bones     — Nereides 추가. 스키닝 메시의 뼈 행렬 128개. VS만.
//   b5 PostParams — 후처리(톤 패스·블러·블룸) 파라미터. PS만. 후처리 셋에서만 바인딩
// 예전에는 b0 하나(world·view·proj·wvp, 320B)를 드로우마다 통째로 올렸음.
//
// 패킹 규칙 (HLSL "Packing Rules for Constant Variables"):
//   (1) 16바이트 레지스터 단위. 벡터는 레지스터 경계를 넘지 못함.
//       → C++의 XMFLOAT3 뒤에는 반드시 4바이트 스칼라(또는 명시적 pad)를 둬야 함.
//   (2) matrix(float4x4) = 64B. 구조체 배열의 원소는 16B 정렬.
//   (3) HLSL uint ↔ C++ uint32_t.
//   (4) alignas(16)과 "sizeof % 16 == 0"은 필요조건일 뿐임. float3 두 개를 나란히
//       두면 크기는 맞아도 오프셋이 어긋남. 그래서 아래 offsetof 단언이 실제 검사임.
//
//   행렬 관례:
//   HLSL은 기본적으로 column_major 로 읽고, 셰이더는 mul(v, M) 행벡터 곱을 씀.
//   DirectXMath 행렬(행우선 메모리)을 그대로 올리면 HLSL이 전치된 행렬로 읽으므로
//   C++에서 XMMatrixTranspose 로 전치해서 올림. #pragma pack_matrix(row_major)는 섞어 쓰지 않음.
//   worldInvTranspose = (W⁻¹)ᵀ 인데 전치해서 올려야 하므로 실제로 올리는 값은 W⁻¹ 임.
//
// 레지스터 배정 (6단계). D3D11의 t#/s# 는 스테이지마다 번호 공간이 하나뿐이므로 셋끼리 겹치지 않게 나눔.
// D3D12 에서는 이 구간이 그대로 디스크립터 테이블 범위가 됨.
//   재질 셋:   t0..t7   s0..s3   (알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4, 재질 샘플러 s0)
//   프레임 셋: t8..t15  s4..s7   (그림자 맵 t8, 비교 샘플러 s4, 스카이박스 t9, 환경 샘플러 s5,
//                                 IBL 확산 조도 t10, IBL 반사 프리필터 t11, BRDF LUT t12 — 2026-10-08)

constexpr uint32_t kCBSlotPerFrame = 0;
constexpr uint32_t kCBSlotPerObject = 1;
constexpr uint32_t kCBSlotLights = 2;
constexpr uint32_t kCBSlotMaterial = 3;
constexpr uint32_t kCBSlotBones = 4; // Nereides: 스키닝 뼈 행렬 (VS)
constexpr uint32_t kMaxBones = 128;
constexpr uint32_t kSRVSlotAlbedo = 0;       // t0 (재질)
constexpr uint32_t kSRVSlotNormal = 1;       // t1 (재질)
constexpr uint32_t kSRVSlotMetallicRoughness = 2; // t2 (재질, G = 거칠기, B = 금속성)
constexpr uint32_t kSRVSlotOcclusion = 3;    // t3 (재질, R = AO)
constexpr uint32_t kSRVSlotEmissive = 4;     // t4 (재질, sRGB)
constexpr uint32_t kSRVSlotShadowMap = 8;    // t8 (프레임)
constexpr uint32_t kSRVSlotEnvironment = 9;  // t9 (프레임, 스카이박스 큐브맵)
constexpr uint32_t kSRVSlotIrradiance = 10;  // t10 (프레임, IBL 확산 조도 큐브)
constexpr uint32_t kSRVSlotPrefiltered = 11; // t11 (프레임, IBL 반사 프리필터 큐브. 밉 = 거칠기)
constexpr uint32_t kSRVSlotBrdfLut = 12;     // t12 (프레임, split-sum BRDF LUT 2D)
constexpr uint32_t kSRVSlotSceneHdr = 13;    // t13 (후처리 셋, 후처리 패스의 입력 텍스처. 톤 패스는 HDR 씬, 블러·블룸은 앞 단계의 결과)
constexpr uint32_t kCBSlotPostParams = 5;    // b5 (후처리 셋, 패스마다 올림). Nereides: b4 는 뼈 팔레트가 쓰므로 Sherlock 의 b4 에서 옮김
constexpr uint32_t kSamplerSlotPost = 6;     // s6 (후처리 셋, 선형 Clamp. 블룸 축소·확대와 썸네일이 씀. 후처리 패스는 프레임 셋을 바인딩하지 않으므로 s4~s7 과 겹치지 않음)
constexpr uint32_t kSamplerSlotMaterial = 0; // s0 (재질)
constexpr uint32_t kSamplerSlotShadow = 4;   // s4 (프레임, 비교 샘플러)
constexpr uint32_t kSamplerSlotEnvironment = 5; // s5 (프레임, 큐브맵용 선형 Clamp)

struct alignas(16) PerFrameConstants
{
	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4 proj;
	DirectX::XMFLOAT4X4 viewProj;
	DirectX::XMFLOAT4X4 lightViewProj;   // 6단계: 방향광 0의 그림자 행렬 (월드 → 광원 클립)
	DirectX::XMFLOAT3 cameraPosition;
	float time;
	DirectX::XMFLOAT4 shadowParams;      // x = 1/맵 크기, y = 깊이 바이어스, z = 세기(0..1), w = 사용 여부
	DirectX::XMFLOAT4 debugParams;       // 11단계: x = 디버그 뷰(0 조명, 1 알베도, 2 노멀, 3 깊이, 4 그림자, 5 UV, 6 금속성/거칠기, 7 AO), y = 깊이 범위
	// 2026-10-08: 환경광(IBL). x = 세기, y = 반사 큐브(t11)의 밉 수, z = 확산 조도 큐브(t10)를 읽을 밉,
	// w = 환경 큐브가 있음(1) / 없음(0, ambientColor 로 대체)
	DirectX::XMFLOAT4 environmentParams;
	// 2026-10-08: 출력. x = 노출(최종 색에 곱함), y = ACES 톤매핑(1) / 끔(0), z = BRDF LUT(t12) 있음(1) / 해석적 근사(0), w = 미사용
	// 2026/10/20 HDR: x, y 는 톤 패스(TonemapPixelShader)가 읽음. 톤 패스의 Layout_Post 가 이 b0 를 그대로 다시 씀.
	DirectX::XMFLOAT4 outputParams;
};

// 2026/10/23: 후처리 패스 파라미터 (b5). 패스마다 올림 — 업로드 링을 쓰므로 프레임 안에서 여러 번 올려도 됨 (b0 가 미리보기에서 그렇게 함).
// 톤 패스도 매 프레임 올리므로 효과가 전부 꺼져 있어도 D3D12 의 "이번 프레임에 갱신되지 않은 Dynamic CB" 경고가 나지 않음.
struct alignas(16) PostParamsConstants
{
	// x = 모드 (PS_Post: 1 블러, 2 블룸 밝기 추출+축소, 3 축소, 4 확대), y,z = 블러 방향 (텍셀 단위, 가로 (1,0) / 세로 (0,1)), w = 미사용
	DirectX::XMFLOAT4 pass;
	// x = 시그마(픽셀), y = 반경(픽셀, 정수로 읽음), zw = 미사용
	DirectX::XMFLOAT4 blur;
	// x = 임계값, y = knee, z = 확대 결과에 곱할 배율 (축적 확대는 1, 마지막 합성은 intensity), w = 미사용
	DirectX::XMFLOAT4 bloom;
	// xy = 입력 텍스처 크기, zw = 출력 타깃 크기 (픽셀). 톤 패스는 Load 좌표를 xy/zw 비로 늘려 읽음 (썸네일 = 축소 출력)
	DirectX::XMFLOAT4 view;
	// x = GreyScale 양 (0..1), y = 1 이면 색 효과를 건너뜀 (단계별 썸네일의 "효과 전" 그림), zw = 미사용
	DirectX::XMFLOAT4 grade;
};

struct alignas(16) PerObjectConstants
{
	DirectX::XMFLOAT4X4 world;
	DirectX::XMFLOAT4X4 worldInvTranspose;
};

struct alignas(16) LightConstants
{
	LightData lights[MAX_LIGHTS];
	DirectX::XMFLOAT3 ambientColor;
	uint32_t lightCount;
};

// 4단계: 재질. 5단계: uvScale·unlit. 6단계: normalStrength.
// 2026-10-08: PBR. specularColor·shininess 자리(16~31)에 emissive·metallic 이, 끝의 패딩 자리에 roughness·occlusionStrength 가 들어감. 크기는 64B 그대로.
struct alignas(16) MaterialConstants
{
	DirectX::XMFLOAT4 baseColor;       // 정점 색·텍스처에 곱해짐
	DirectX::XMFLOAT3 emissive;        // 발광 계수 (선형)
	float metallic;
	DirectX::XMFLOAT2 uvScale;         // 타일링
	float unlit;                       // 1 = 조명 없이 알베도 출력
	float normalStrength;              // 노멀 맵 xy 배율. 0 = 노멀 맵 무시
	float alphaCutoff;                 // 9단계: 알파 컷아웃. 0 = 끔
	float roughness;
	float occlusionStrength;
	float padding;
};

// Nereides: 스키닝. b4. 뼈 행렬 = inverseBind * 뼈 자세 (모델 공간). 월드 행렬과 같은 방식으로 전치해서 올림.
struct alignas(16) BoneConstants
{
    DirectX::XMFLOAT4X4 bones[kMaxBones];
};

// C++20 (2026-09-28): consteval 검사 함수. cbuffer 는 16바이트 레지스터 단위로 올라가고 D3D11 은 65,536바이트가 상한임.
// consteval 이라 런타임에는 존재하지 않는 함수임 — static_assert 안에서만 호출됨. 아래의 정확한 크기 단언은
// 그대로 두고(HLSL 과 바이트 단위로 대조하는 것이 실제 검사), 이 함수는 새 cbuffer 를 추가할 때 먼저 걸리는 일반 규칙임.
consteval bool IsValidConstantBufferSize(size_t bytes)
{
	return bytes > 0 && bytes % 16 == 0 && bytes <= 65536;
}
static_assert(IsValidConstantBufferSize(sizeof(PerFrameConstants)), "PerFrameConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(PerObjectConstants)), "PerObjectConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(LightConstants)), "LightConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(MaterialConstants)), "MaterialConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(BoneConstants)), "BoneConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(PostParamsConstants)), "PostParamsConstants 는 16의 배수여야 함");
static_assert(sizeof(PostParamsConstants) == 80, "PostParamsConstants: 16 * 5");

static_assert(sizeof(PerFrameConstants) == 336, "PerFrameConstants: 64*4 + 16 * 5");
static_assert(offsetof(PerFrameConstants, environmentParams) == 304, "PerFrameConstants.environmentParams");
static_assert(offsetof(PerFrameConstants, outputParams) == 320, "PerFrameConstants.outputParams");
static_assert(offsetof(PerFrameConstants, debugParams) == 288, "PerFrameConstants.debugParams");
static_assert(offsetof(PerFrameConstants, lightViewProj) == 192, "PerFrameConstants.lightViewProj");
static_assert(offsetof(PerFrameConstants, cameraPosition) == 256, "PerFrameConstants.cameraPosition");
static_assert(offsetof(PerFrameConstants, time) == 268, "PerFrameConstants.time");
static_assert(offsetof(PerFrameConstants, shadowParams) == 272, "PerFrameConstants.shadowParams");

static_assert(sizeof(PerObjectConstants) == 128, "PerObjectConstants: 64*2");
static_assert(offsetof(PerObjectConstants, worldInvTranspose) == 64, "PerObjectConstants.worldInvTranspose");

static_assert(sizeof(LightConstants) == 656, "LightConstants: 80*8 + 16");
static_assert(offsetof(LightConstants, ambientColor) == 640, "LightConstants.ambientColor");
static_assert(offsetof(LightConstants, lightCount) == 652, "LightConstants.lightCount");

static_assert(sizeof(BoneConstants) == 64 * kMaxBones, "BoneConstants: 64 * 128");

static_assert(sizeof(MaterialConstants) == 64, "MaterialConstants: 16 + 16 + 16 + 16");
static_assert(offsetof(MaterialConstants, emissive) == 16, "MaterialConstants.emissive");
static_assert(offsetof(MaterialConstants, metallic) == 28, "MaterialConstants.metallic");
static_assert(offsetof(MaterialConstants, uvScale) == 32, "MaterialConstants.uvScale");
static_assert(offsetof(MaterialConstants, unlit) == 40, "MaterialConstants.unlit");
static_assert(offsetof(MaterialConstants, normalStrength) == 44, "MaterialConstants.normalStrength");
static_assert(offsetof(MaterialConstants, alphaCutoff) == 48, "MaterialConstants.alphaCutoff");
static_assert(offsetof(MaterialConstants, roughness) == 52, "MaterialConstants.roughness");
static_assert(offsetof(MaterialConstants, occlusionStrength) == 56, "MaterialConstants.occlusionStrength");
