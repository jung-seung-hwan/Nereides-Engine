#pragma once
#include <cstdint>
#include <DirectXMath.h>

// 조명 데이터. C++ 구조체와 HLSL(Shaders/Common.hlsli의 LightData)의 메모리 배치가 바이트 단위로 같아야 함.
//
// HLSL 상수버퍼 패킹 규칙:
//   - 16바이트 레지스터 단위로 채우며, 벡터 하나가 레지스터 경계를 넘을 수 없음.
//   - 따라서 float3 다음에는 4바이트 스칼라를 두어 정확히 16바이트를 채움.
//   - 구조체 배열의 각 원소는 16바이트 경계에서 시작함.
// 아래 필드 순서는 그 규칙에 맞춰 직접 배치한 것임. 필드를 하나 옮기면 HLSL 쪽도 같이 옮겨야 함.

enum class LightType : uint32_t
{
	Directional = 0,
	Point = 1,
	Spot = 2
};

constexpr uint32_t MAX_LIGHTS = 8;

struct alignas(16) LightData
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 10.0f, -10.0f);
	float intensity = 1.0f;

	// 빛이 진행하는 방향. 셰이더의 L(표면에서 광원을 향하는 벡터)은 이와 반대 방향을 사용함
	DirectX::XMFLOAT3 direction = DirectX::XMFLOAT3(0.0f, -0.4472136f, 0.8944272f);
	float range = 50.0f;

	DirectX::XMFLOAT3 color = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	uint32_t type = static_cast<uint32_t>(LightType::Directional);

	// constant, linear, quadratic attenuation
	DirectX::XMFLOAT3 attenuation = DirectX::XMFLOAT3(1.0f, 0.045f, 0.0075f);
	float innerConeCos = 0.9238795f; // cos(22.5 degrees)

	float outerConeCos = 0.8660254f; // cos(30 degrees)
	DirectX::XMFLOAT3 padding = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
};

static_assert(sizeof(LightData) == 80, "LightData는 HLSL과 같이 80바이트(16바이트 레지스터 5개)여야 한다.");
