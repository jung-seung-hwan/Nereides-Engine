#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "RHI/ResourceDesc.h"

// 텍스처 파일 → CPU 이미지(밉 체인 포함). API 중립.
//
// 디코딩과 밉맵 생성은 DirectXTex(vcpkg)가 맡음. WIC(PNG·JPG·BMP)와 DDS를 읽음.
// 결과는 TextureDesc + 밉 레벨별 TextureSubresource 배열이므로 Device::CreateTexture에
// 그대로 넘김. D3D 헤더는 쓰지 않음 — DirectXTex의 D3D11 헬퍼(CreateTexture 등)도
// 일부러 쓰지 않음. 그것을 쓰면 로더가 D3D11 폴더에 들어가야 함.
//
// sRGB: 색 텍스처(알베도)는 srgb=true 로 읽어 *_UNORM_SRGB 포맷으로 만듦. 샘플할 때
// 하드웨어가 선형으로 풀고, 조명은 선형에서 계산되며, 백버퍼 sRGB 뷰가 다시 인코딩함.
// 노멀맵·마스크 같은 "숫자" 텍스처는 srgb=false.
class TextureImage
{
public:
	TextureDesc desc;                            // dimension, format, width, height, mipLevels
	std::vector<uint8_t> pixels;                 // 모든 서브리소스(면 × 밉)를 이어 붙인 버퍼
	std::vector<TextureSubresource> subresources;   // 서브리소스마다 pixels 안의 포인터와 rowPitch. 순서는 ResourceDesc.h (면 0 의 밉 전부, 면 1 ...)

	bool IsValid() const { return desc.width > 0 && !subresources.empty(); }
};

namespace TextureLoader
{
	// 파일에서 읽음. generateMips가 true면 1×1까지 밉 체인을 만듦.
	bool LoadFromFile(const std::wstring& path, bool srgb, bool generateMips, TextureImage& out);

	// 9단계: 메모리의 파일 내용(PNG/JPG/DDS 바이트)에서 읽음. 모델에 내장된 이미지용.
	bool LoadFromMemory(const uint8_t* data, size_t size, bool srgb, bool generateMips, TextureImage& out);

	// 11-B단계: 에디터 썸네일. 긴 변이 maxSize 를 넘으면 DirectXTex Resize 로 줄임. 밉 없음.
	// 포맷은 UNORM 라벨 — ImGui 는 백버퍼의 UNORM 뷰에 그리므로 sRGB 라벨을 붙이면 두 번 인코딩되어 색이 바램.
	// 블록 압축 DDS 는 먼저 압축을 풂. 로그는 실패할 때만 남김 (폴더 하나에 수십 장이 있을 수 있음).
	bool LoadThumbnail(const std::wstring& path, uint32_t maxSize, TextureImage& out);

	// 절차적 텍스처. 파일 없이 검증할 때 씀.
	// 체커: cellsPerSide×cellsPerSide 격자, 두 색 교대.
	bool CreateChecker(uint32_t size, uint32_t cellsPerSide, const uint8_t colorA[4], const uint8_t colorB[4],
		bool srgb, bool generateMips, TextureImage& out);
	// 단색 (예: 1×1 흰색 = "텍스처 없음"의 기본값, 128 회색 = 감마 검증).
	bool CreateSolid(uint32_t size, const uint8_t rgba[4], bool srgb, TextureImage& out);

	// ---- 2026-10-08: 큐브맵 (desc.dimension = TextureCube, 면 6장) ----
	// path 가 폴더면 그 안의 면 이미지 6장(px/nx/py/ny/pz/nz, posx/negx/..., right/left/top/bottom/front/back 중 하나의 규칙,
	// 확장자 .png/.jpg/.jpeg/.bmp)을 읽음. 파일이면 큐브 DDS(면 6장 + 큐브 플래그). 블록 압축 DDS 는 압축을 풀어 R8G8B8A8 로 만듦.
	// 부동소수점(HDR) DDS 는 R16G16B16A16_FLOAT 로 읽음 — 2D 도 같음(FinishImage). 면 이미지 폴더는 8비트만 받음.
	bool LoadCubeFromFile(const std::wstring& path, bool srgb, bool generateMips, TextureImage& out);
	// 절차적 큐브. 면마다 size×size 텍셀을 돌며 텍셀 중심이 가리키는 정규화된 방향(월드 축 기준)을 shade 에 넘기고 RGBA 를 받음.
	// 내장 하늘("builtin:sky"), 면 판별용 큐브, 1×1 검은 기본 큐브를 모두 이것으로 만듦.
	bool CreateCube(uint32_t size, const std::function<void(const float direction[3], uint8_t rgba[4])>& shade,
		bool srgb, bool generateMips, TextureImage& out);
}
