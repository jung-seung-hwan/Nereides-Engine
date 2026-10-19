#pragma once
#include <cstddef>
#include "RHI/PipelineTypes.h"

// 정점 구조체와 그 레이아웃 기술.
//
// 레이아웃은 offsetof로 만듦. 예전에는 MeshRenderer가 D3D11_INPUT_ELEMENT_DESC에
// 오프셋 0/12/28을 손으로 적었음. 구조체를 고치면 그 숫자도 같이 고쳐야 했고,
// 잊으면 컴파일은 되는데 화면이 깨졌음.
//
// 4단계(D9): 정점 포맷을 열거형으로 두고 Mesh가 자기 포맷을 알려줌. Renderer는
// GetVertexLayout(mesh.GetVertexFormat())으로 PSO Desc를 채움.
// 5단계: UV 추가. 6단계: 탄젠트 추가 (Position / Color / Normal / TexCoord / Tangent, 64바이트).
//
// C++20 (2026-09-28): 레이아웃 생성 함수를 consteval 로 바꿈. 이 함수는 런타임 입력이 없고
// offsetof / sizeof 만 쓰므로 컴파일 시점에 계산되어야 마땅하고, consteval 은 그것을 강제함
// (런타임 호출은 컴파일 에러). 결과는 아래 kVertexLayoutPCNTT 상수에 담기고, 그 아래 static_assert 가
// "레이아웃의 stride 가 정점 크기와 같다", "속성 5개" 를 컴파일 시점에 검사함 — 이전에는 런타임에
// 화면이 깨져야 알 수 있던 종류의 실수임.

struct VERTEX
{
	float x, y, z; 			//좌표(Position)
	float r, g, b, a;		//색상(Diffuse Color)
	float nx, ny, nz;		//노멀(Normal)
	float u, v;				//텍스처 좌표(TexCoord). (0,0) = 왼쪽 위 (D3D 관례)
	float tx, ty, tz, tw;	//탄젠트(Tangent) = u 가 증가하는 방향. w = 손잡이(±1): B = cross(N, T) * w
};
using VertexPCNTT = VERTEX;

// Nereides 추가: 스키닝 정점. VERTEX 뒤에 뼈 인덱스 4개와 가중치 4개가 붙음 (96바이트).
struct VERTEX_SKINNED
{
	VERTEX base;
	uint32_t boneIds[4];   // BLENDINDICES: 뼈 팔레트(b4) 안의 인덱스
	float weights[4];      // BLENDWEIGHT: 합이 0이면 스키닝하지 않음
};
using VertexPCNTTSkinned = VERTEX_SKINNED;

enum class VertexFormat : uint8_t
{
	PositionColorNormalTexcoordTangent,   // VERTEX (VertexPCNTT)
	PositionColorNormalTexcoordTangentSkinned,   // VERTEX_SKINNED (Nereides)
};

// Position / Color / Normal / TexCoord / Tangent 레이아웃. 컴파일 시점 전용(consteval).
consteval VertexLayoutDesc MakeVertexLayoutPCNTT()
{
	VertexLayoutDesc layout;
	// C++20 지정 초기화. VertexAttribute 필드 순서: semantic, semanticIndex, format, offset, inputSlot.
	layout.attributes[0] = { .semantic = VertexSemantic::Position, .format = Format::R32G32B32_FLOAT,    .offset = static_cast<uint16_t>(offsetof(VERTEX, x)) };
	layout.attributes[1] = { .semantic = VertexSemantic::Color,    .format = Format::R32G32B32A32_FLOAT, .offset = static_cast<uint16_t>(offsetof(VERTEX, r)) };
	layout.attributes[2] = { .semantic = VertexSemantic::Normal,   .format = Format::R32G32B32_FLOAT,    .offset = static_cast<uint16_t>(offsetof(VERTEX, nx)) };
	layout.attributes[3] = { .semantic = VertexSemantic::TexCoord, .format = Format::R32G32_FLOAT,       .offset = static_cast<uint16_t>(offsetof(VERTEX, u)) };
	layout.attributes[4] = { .semantic = VertexSemantic::Tangent,  .format = Format::R32G32B32A32_FLOAT, .offset = static_cast<uint16_t>(offsetof(VERTEX, tx)) };
	layout.attributeCount = 5;
	layout.stride = sizeof(VERTEX);
	return layout;
}

inline constexpr VertexLayoutDesc kVertexLayoutPCNTT = MakeVertexLayoutPCNTT();

static_assert(kVertexLayoutPCNTT.stride == sizeof(VERTEX), "VertexPCNTT 레이아웃의 stride 가 정점 구조체 크기와 다름");
static_assert(kVertexLayoutPCNTT.attributeCount == 5, "VertexPCNTT 는 속성 5개(Position/Color/Normal/TexCoord/Tangent)");
static_assert(kVertexLayoutPCNTT.attributes[4].offset + sizeof(float) * 4 == sizeof(VERTEX), "마지막 속성(Tangent) 뒤에 빈 공간이 없어야 함");

consteval VertexLayoutDesc MakeVertexLayoutPCNTTSkinned()
{
	VertexLayoutDesc layout = MakeVertexLayoutPCNTT();
	layout.attributes[5] = { .semantic = VertexSemantic::BlendIndices, .format = Format::R32G32B32A32_UINT,  .offset = static_cast<uint16_t>(offsetof(VERTEX_SKINNED, boneIds)) };
	layout.attributes[6] = { .semantic = VertexSemantic::BlendWeight,  .format = Format::R32G32B32A32_FLOAT, .offset = static_cast<uint16_t>(offsetof(VERTEX_SKINNED, weights)) };
	layout.attributeCount = 7;
	layout.stride = sizeof(VERTEX_SKINNED);
	return layout;
}

inline constexpr VertexLayoutDesc kVertexLayoutPCNTTSkinned = MakeVertexLayoutPCNTTSkinned();

static_assert(kVertexLayoutPCNTTSkinned.stride == sizeof(VERTEX_SKINNED), "스키닝 정점 레이아웃의 stride 가 정점 구조체 크기와 다름");
static_assert(kVertexLayoutPCNTTSkinned.attributeCount == 7, "스키닝 정점은 속성 7개");
static_assert(kVertexLayoutPCNTTSkinned.attributes[6].offset + sizeof(float) * 4 == sizeof(VERTEX_SKINNED), "마지막 속성(BlendWeight) 뒤에 빈 공간이 없어야 함");

// 이전 이름을 유지함 — 호출처는 그대로임. 이제 상수를 돌려줄 뿐임.
constexpr const VertexLayoutDesc& GetVertexLayoutPCNTT()
{
	return kVertexLayoutPCNTT;
}

constexpr const VertexLayoutDesc& GetVertexLayout(VertexFormat format)
{
	switch (format)
	{
	case VertexFormat::PositionColorNormalTexcoordTangentSkinned:
		return kVertexLayoutPCNTTSkinned;
	case VertexFormat::PositionColorNormalTexcoordTangent:
	default:
		return kVertexLayoutPCNTT;
	}
}

constexpr uint32_t GetVertexStride(VertexFormat format)
{
	return GetVertexLayout(format).stride;
}
