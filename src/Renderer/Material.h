#pragma once
#include <string>
#include <DirectXMath.h>

// 재질(CPU 데이터). GPU 쪽(상수버퍼 핸들, 텍스처·샘플러 핸들, ResourceSet, PSO)은
// Renderer의 캐시가 가짐. Mesh ↔ GpuMesh 와 같은 방식으로 경계를 나눈 것임 (rendering-analysis D3).
//
// 텍스처는 핸들이 아니라 "이름"으로 참조함. Scene은 Device를 모르므로 파일을 열 수 없고,
// 이름 → TextureHandle 변환은 Renderer의 텍스처 캐시가 맡음.
//   - 파일 경로: Assets/Textures/ 기준 상대 경로 ("uv_checker.png")
//   - 에셋 경로: "asset:Models\Sponza\lion.jpg" — Assets/ 기준 상대 경로 (11-B단계, 콘텐츠 브라우저 드롭)
//   - 내장:      "builtin:white", "builtin:checker", "builtin:gray128", "builtin:flatnormal"
//   - 빈 문자열: 텍스처 없음 (알베도·금속성/거칠기·AO·발광은 1×1 흰색, 노멀 맵은 평평한 노멀로 대체)

enum class SamplerPreset : uint8_t
{
	LinearWrap,       // 3선형 + 반복. 기본값
	LinearClamp,
	AnisotropicWrap,  // 비등방 16x. 바닥처럼 비스듬히 보는 면에 필요
	PointWrap,        // 최근접(픽셀 아트). 밉맵은 씀
	PointNoMip,       // 최근접 + 밉맵 끔. "밉맵이 없으면 어떻게 되나"의 대조군
};

struct Material
{
	std::string name;

	DirectX::XMFLOAT4 baseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);   // 정점 색·텍스처에 곱해짐

	// 2026-10-08: PBR (glTF metallic-roughness). Blinn-Phong 의 specularColor / shininess 를 대체함.
	// 직접광은 Cook-Torrance(GGX), 환경광은 IBL(split-sum) 이 이 두 값을 읽음 (BasicPixelShader.hlsl).
	float metallic = 0.0f;                 // 0 = 유전체(F0 = 0.04), 1 = 금속(F0 = baseColor, 확산 없음)
	float roughness = 0.5f;                // 지각적 거칠기. GGX 의 α = roughness². IBL 반사 큐브의 밉도 이 값으로 고름
	DirectX::XMFLOAT3 emissive = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);   // 스스로 내는 빛(선형, 1 을 넘어도 됨). 조명·그림자와 무관하게 더해짐

	// 5단계
	std::string albedoTexture;             // 위 규칙을 따르는 이름. 비면 흰색
	bool albedoSrgb = true;                // 색 텍스처는 sRGB. 데이터 텍스처(노멀·마스크)는 false
	SamplerPreset sampler = SamplerPreset::LinearWrap;
	DirectX::XMFLOAT2 uvScale = DirectX::XMFLOAT2(1.0f, 1.0f);
	bool unlit = false;                    // 조명 없이 알베도만 출력 (감마 검증용)

	// 6단계: 노멀 맵. 항상 선형(sRGB 아님). 탄젠트 공간, +X = u 증가, +Y = v 증가(아래), +Z = 표면 밖.
	std::string normalTexture;             // 비면 평평한 노멀 (128,128,255)
	float normalStrength = 1.0f;           // xy 배율. 0 이면 노멀 맵을 무시한 것과 같음

	// 2026-10-08: PBR 텍스처. 계수(metallic·roughness·emissive)에 곱해짐 — glTF 규칙. 비면 흰색이라 계수가 그대로 쓰임.
	std::string metallicRoughnessTexture;  // 선형. G = roughness, B = metallic (glTF 채널 배치)
	std::string occlusionTexture;          // 선형. R = 앰비언트 오클루전. 환경광(IBL)에만 곱함
	float occlusionStrength = 1.0f;        // 0 = AO 무시, 1 = 텍스처 값 그대로
	std::string emissiveTexture;           // sRGB 색 텍스처

	// 9단계: 알파 컷아웃 (glTF alphaMode MASK). 0 = 끔. 알베도 알파가 이 값보다 작은 픽셀은 버림(clip).
	float alphaCutoff = 0.0f;

	// 파이프라인 상태에 영향을 주는 옵션. 값이 다르면 다른 PSO를 사용함.
	bool wireframe = false;
	bool doubleSided = false;   // 뒷면 컬링 끔
};
