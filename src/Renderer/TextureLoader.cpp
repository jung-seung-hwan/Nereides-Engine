#include "RHI/Compat/RHICommon.h"
#include "Renderer/TextureLoader.h"
#include "RHI/Compat/SherlockLog.h"
#include <DirectXTex.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <filesystem>

namespace
{
	// DirectXTex ScratchImage(밉 체인) → 우리 TextureImage. 픽셀을 한 버퍼로 복사함.
	// 2026-10-08: dimension = TextureCube 면 면 6장을 서브리소스 인덱스 순(면 0 의 밉 전부, 면 1 의 밉 전부, ...)으로 옮김.
	// Texture2D 면 배열의 첫 장만 씀 — 큐브 DDS 를 재질 알베도로 지정해도 예전처럼 +X 면 하나짜리 2D 가 됨.
	bool FromScratchImage(const DirectX::ScratchImage& scratch, Format format, TextureDimension dimension, TextureImage& out)
	{
		const DirectX::TexMetadata& meta = scratch.GetMetadata();
		out.desc = TextureDesc{   // C++20 지정 초기화. 나열하지 않은 sampleCount / debugName 은 기본값
			.dimension = dimension,
			.width = static_cast<uint32_t>(meta.width),
			.height = static_cast<uint32_t>(meta.height),
			.format = format,
			.mipLevels = static_cast<uint32_t>(meta.mipLevels),
			.bindFlags = TextureBind_ShaderResource,
		};
		const size_t faces = GetArraySize(out.desc);
		if (meta.arraySize < faces) return false;

		out.pixels.clear();
		out.subresources.clear();

		size_t total = 0;
		for (size_t face = 0; face < faces; ++face)
		{
			for (size_t mip = 0; mip < meta.mipLevels; ++mip)
			{
				const DirectX::Image* image = scratch.GetImage(mip, face, 0);
				if (image == nullptr) return false;
				total += image->slicePitch;
			}
		}
		out.pixels.resize(total);

		size_t offset = 0;
		for (size_t face = 0; face < faces; ++face)
		{
			for (size_t mip = 0; mip < meta.mipLevels; ++mip)
			{
				const DirectX::Image* image = scratch.GetImage(mip, face, 0);
				std::memcpy(out.pixels.data() + offset, image->pixels, image->slicePitch);
				TextureSubresource sub;
				sub.data = out.pixels.data() + offset;
				sub.rowPitch = static_cast<uint32_t>(image->rowPitch);
				out.subresources.push_back(sub);
				offset += image->slicePitch;
			}
		}
		return true;
	}

	bool FinishImage(DirectX::ScratchImage& base, bool srgb, bool generateMips, TextureImage& out, TextureDimension dimension = TextureDimension::Texture2D)
	{
		// 픽셀 바이트는 파일에 든 그대로(sRGB 인코딩된 값)임. *_SRGB 포맷은 "이 바이트를 샘플할 때
		// 선형으로 풀어라"는 라벨일 뿐 값을 바꾸지 않음. 그래서 여기서는 색공간 변환 없이
		// R8G8B8A8_UNORM으로 통일만 하고, 라벨(desc.format)만 sRGB로 붙임.
		//
		// 함정: DirectXTex::Convert(UNORM → UNORM_SRGB)는 입력을 선형으로 보고 sRGB로 "인코딩"함
		// (TEX_FILTER_SRGB_OUT 암시). 그 결과 128 회색이 188이 되어 화면에서 188로 읽혔음. 반대로 DDS가
		// 이미 *_SRGB 포맷이면 Convert가 "디코딩"하므로, 먼저 OverrideFormat으로 라벨을 떼어 냄.
		if (DirectX::IsSRGB(base.GetMetadata().format))
		{
			base.OverrideFormat(DirectX::MakeLinear(base.GetMetadata().format));
		}

		// 2026-10-08: HDR. 원본이 부동소수점(R32G32B32A32_FLOAT 등)이거나 BC6H 면 1.0 을 넘는 값을 잃지 않도록
		// R16G16B16A16_FLOAT 로 맞춤 (float32 의 절반 크기, 최대 65504 — HDR 환경맵의 태양 ≈ 수백). 부동소수점 데이터는
		// 이미 선형이므로 sRGB 라벨을 붙이지 않고, 밉도 선형 그대로 평균 냄. srgb 인자는 무시됨.
		const DXGI_FORMAT sourceFormat = base.GetMetadata().format;
		const bool hdr = DirectX::FormatDataType(sourceFormat) == DirectX::FORMAT_TYPE_FLOAT
			|| sourceFormat == DXGI_FORMAT_BC6H_UF16 || sourceFormat == DXGI_FORMAT_BC6H_SF16;
		if (hdr) srgb = false;
		const DXGI_FORMAT target = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
		DirectX::ScratchImage converted;
		const DirectX::ScratchImage* current = &base;
		// 2026-10-08: 블록 압축(BC1~7) DDS 는 Convert 가 다루지 못하므로 먼저 압축을 풂 (내려받은 큐브 DDS 는 대개 BC 압축임).
		// 압축을 풀면 바로 target(R8G8B8A8_UNORM, BC6H 는 R16G16B16A16_FLOAT)이 되므로 아래 Convert 는 건너뜀. 바이트는 그대로라 sRGB 라벨 규칙도 같음.
		DirectX::ScratchImage decompressed;
		if (DirectX::IsCompressed(base.GetMetadata().format))
		{
			const HRESULT hr = DirectX::Decompress(base.GetImages(), base.GetImageCount(), base.GetMetadata(), target, decompressed);
			if (FAILED(hr))
			{
				Log::Error("TextureLoader : 블록 압축 해제 실패. %s", Log::HrToString(hr).c_str());
				return false;
			}
			current = &decompressed;
		}
		if (current->GetMetadata().format != target)
		{
			const HRESULT hr = DirectX::Convert(current->GetImages(), current->GetImageCount(), current->GetMetadata(),
				target, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted);
			if (FAILED(hr))
			{
				Log::Error("TextureLoader : 포맷 변환 실패. %s", Log::HrToString(hr).c_str());
				return false;
			}
			current = &converted;
		}

		DirectX::ScratchImage mipped;
		if (generateMips && current->GetMetadata().mipLevels == 1 && (current->GetMetadata().width > 1 || current->GetMetadata().height > 1))
		{
			// 박스 필터로 1×1까지. sRGB 데이터면 TEX_FILTER_SRGB(= SRGB_IN | SRGB_OUT): 풀어서 선형에서
			// 평균 내고 다시 인코딩함. 감마 공간에서 그대로 평균 내면 밉이 어두워짐
			// (흰 230 + 검 40 의 절반은 선형에서 ≈ 170, 감마에서 135).
			const DirectX::TEX_FILTER_FLAGS filter = srgb ? DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_DEFAULT;
			const HRESULT hr = DirectX::GenerateMipMaps(current->GetImages(), current->GetImageCount(), current->GetMetadata(),
				filter, 0, mipped);
			if (FAILED(hr))
			{
				Log::Error("TextureLoader : 밉맵 생성 실패. %s", Log::HrToString(hr).c_str());
				return false;
			}
			current = &mipped;
		}

		// 2026-10-08: 큐브를 요청했는데 원본이 큐브가 아니면(2D DDS·면 6장이 아닌 배열) 실패. 반대 방향(큐브 → 2D)은 첫 면만 씀.
		if (dimension == TextureDimension::TextureCube && !current->GetMetadata().IsCubemap())
		{
			Log::Error("TextureLoader : 큐브맵이 아님 (DDS 의 큐브 플래그가 없거나 면이 6장이 아님).");
			return false;
		}
		const Format format = hdr ? Format::R16G16B16A16_FLOAT : (srgb ? Format::R8G8B8A8_UNORM_SRGB : Format::R8G8B8A8_UNORM);
		return FromScratchImage(*current, format, dimension, out);
	}
}

namespace
{
	// 로그용 색공간 이름. HDR 은 FinishImage 가 srgb 인자를 무시하므로 결과 포맷으로 판단함.
	const char* ColorSpaceName(const TextureImage& image)
	{
		switch (image.desc.format)
		{
		case Format::R16G16B16A16_FLOAT:  return "HDR half";
		case Format::R8G8B8A8_UNORM_SRGB: return "sRGB";
		default:                          return "linear";
		}
	}

	// 파일 → ScratchImage. 확장자가 .dds 면 DDS, 아니면 WIC. LoadFromFile 과 LoadThumbnail 이 함께 씀.
	bool LoadScratchFromFile(const std::wstring& path, DirectX::ScratchImage& image)
	{
		DirectX::TexMetadata meta = {};
		HRESULT hr;

		const size_t dot = path.find_last_of(L'.');
		std::wstring ext = (dot == std::wstring::npos) ? L"" : path.substr(dot);
		std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

		if (ext == L".dds")
		{
			hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &meta, image);
		}
		else
		{
			// WIC: PNG, JPG, BMP, TIFF, GIF. 알파 없는 파일도 RGBA로 확장함.
			// IGNORE_SRGB: 파일의 색공간 메타데이터를 보고 포맷을 *_SRGB로 바꾸지 않게 함. 라벨은 우리가 붙임.
			hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB, &meta, image);
		}
		if (FAILED(hr))
		{
			Log::Error("TextureLoader : 파일 로드 실패 %s. %s", Log::ToUtf8(path.c_str()).c_str(), Log::HrToString(hr).c_str());
			return false;
		}
		return true;
	}
}

bool TextureLoader::LoadFromFile(const std::wstring& path, bool srgb, bool generateMips, TextureImage& out)
{
	DirectX::ScratchImage image;
	if (!LoadScratchFromFile(path, image)) return false;

	if (!FinishImage(image, srgb, generateMips, out))
	{
		return false;
	}
	Log::Info("텍스처 로드: %s (%ux%u, 밉 %u, %s)", Log::ToUtf8(path.c_str()).c_str(),
		out.desc.width, out.desc.height, out.desc.mipLevels, ColorSpaceName(out));
	return true;
}

bool TextureLoader::CreateChecker(uint32_t size, uint32_t cellsPerSide, const uint8_t colorA[4], const uint8_t colorB[4],
	bool srgb, bool generateMips, TextureImage& out)
{
	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1)))
	{
		return false;
	}
	const DirectX::Image* img = image.GetImage(0, 0, 0);
	const uint32_t cell = size / (cellsPerSide > 0 ? cellsPerSide : 1);
	for (uint32_t y = 0; y < size; ++y)
	{
		uint8_t* row = img->pixels + y * img->rowPitch;
		for (uint32_t x = 0; x < size; ++x)
		{
			const bool even = (((x / cell) + (y / cell)) % 2) == 0;
			const uint8_t* c = even ? colorA : colorB;
			std::memcpy(row + x * 4, c, 4);
		}
	}
	return FinishImage(image, srgb, generateMips, out);
}

bool TextureLoader::CreateSolid(uint32_t size, const uint8_t rgba[4], bool srgb, TextureImage& out)
{
	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1)))
	{
		return false;
	}
	const DirectX::Image* img = image.GetImage(0, 0, 0);
	for (uint32_t y = 0; y < size; ++y)
	{
		uint8_t* row = img->pixels + y * img->rowPitch;
		for (uint32_t x = 0; x < size; ++x)
		{
			std::memcpy(row + x * 4, rgba, 4);
		}
	}
	return FinishImage(image, srgb, false, out);
}

bool TextureLoader::LoadThumbnail(const std::wstring& path, uint32_t maxSize, TextureImage& out)
{
	DirectX::ScratchImage image;
	if (!LoadScratchFromFile(path, image)) return false;

	// 블록 압축(BC1~7)은 Resize/Convert 가 다루지 못하므로 먼저 압축을 풂.
	DirectX::ScratchImage decompressed;
	const DirectX::ScratchImage* current = &image;
	if (DirectX::IsCompressed(image.GetMetadata().format))
	{
		const HRESULT hr = DirectX::Decompress(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, decompressed);
		if (FAILED(hr))
		{
			Log::Warn("TextureLoader : 썸네일용 압축 해제 실패 %s. %s", Log::ToUtf8(path.c_str()).c_str(), Log::HrToString(hr).c_str());
			return false;
		}
		current = &decompressed;
	}

	// 밉 0 만 씀. 긴 변을 maxSize 에 맞춤 (비율 유지).
	const DirectX::Image* base = current->GetImage(0, 0, 0);
	if (base == nullptr) return false;
	DirectX::ScratchImage resized;
	const size_t longest = (std::max)(base->width, base->height);
	if (maxSize > 0 && longest > maxSize)
	{
		const size_t w = (std::max<size_t>)(1, base->width * maxSize / longest);
		const size_t h = (std::max<size_t>)(1, base->height * maxSize / longest);
		const HRESULT hr = DirectX::Resize(*base, w, h, DirectX::TEX_FILTER_DEFAULT, resized);
		if (FAILED(hr))
		{
			Log::Warn("TextureLoader : 썸네일 축소 실패 %s. %s", Log::ToUtf8(path.c_str()).c_str(), Log::HrToString(hr).c_str());
			return false;
		}
		base = resized.GetImage(0, 0, 0);
	}

	// 밉 0 하나짜리 ScratchImage 로 옮겨 FinishImage 의 포맷 통일을 거침. srgb=false: UNORM 라벨 (헤더 주석).
	DirectX::ScratchImage single;
	if (FAILED(single.InitializeFromImage(*base))) return false;
	return FinishImage(single, false, false, out);
}

bool TextureLoader::LoadFromMemory(const uint8_t* data, size_t size, bool srgb, bool generateMips, TextureImage& out)
{
	if (data == nullptr || size == 0) return false;
	DirectX::ScratchImage image;
	DirectX::TexMetadata meta = {};

	// DDS 매직("DDS ") 이면 DDS, 아니면 WIC (PNG/JPG). 모델 파일이 확장자를 알려 주지 않을 수 있어 내용으로 판단함.
	HRESULT hr;
	if (size >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ')
	{
		hr = DirectX::LoadFromDDSMemory(data, size, DirectX::DDS_FLAGS_NONE, &meta, image);
	}
	else
	{
		hr = DirectX::LoadFromWICMemory(data, size, DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB, &meta, image);
	}
	if (FAILED(hr))
	{
		Log::Error("TextureLoader : 메모리 이미지 디코딩 실패 (%zu 바이트). %s", size, Log::HrToString(hr).c_str());
		return false;
	}
	return FinishImage(image, srgb, generateMips, out);
}

// ------------------------------------------------------------------ 2026-10-08: 큐브맵

namespace
{
	// 면 이미지 파일 이름 후보. 바깥 순서 = D3D 면 순서 +X, −X, +Y, −Y, +Z, −Z. 흔한 세 가지 이름 규칙을 받음.
	// right/left/top/bottom/front/back 은 LearnOpenGL 등에서 쓰는 이름 — front = +Z 라서 왼손 좌표계(+Z 앞)인 이 엔진과 그대로 맞음.
	const wchar_t* const kFaceNames[6][3] =
	{
		{ L"px", L"posx", L"right" },
		{ L"nx", L"negx", L"left" },
		{ L"py", L"posy", L"top" },
		{ L"ny", L"negy", L"bottom" },
		{ L"pz", L"posz", L"front" },
		{ L"nz", L"negz", L"back" },
	};
	const wchar_t* const kFaceExtensions[] = { L".png", L".jpg", L".jpeg", L".bmp" };

	std::wstring FindFaceFile(const std::wstring& folder, int face)
	{
		for (const wchar_t* name : kFaceNames[face])
		{
			for (const wchar_t* extension : kFaceExtensions)
			{
				const std::filesystem::path candidate = std::filesystem::path(folder) / (std::wstring(name) + extension);
				std::error_code ec;
				if (std::filesystem::is_regular_file(candidate, ec)) return candidate.wstring();
			}
		}
		return L"";
	}

	// 면 face 의 텍셀 좌표 (s, t) ∈ [−1, 1] (s = 오른쪽, t = 아래쪽) → 그 텍셀이 가리키는 방향 (정규화 전).
	// D3D 큐브맵 규칙 표(면마다 "주축 ma, 가로 sc, 세로 tc")를 거꾸로 푼 것. 예: +X 면은 sc = −z, tc = −y 이므로 z = −s, y = −t.
	void CubeTexelDirection(int face, float s, float t, float direction[3])
	{
		switch (face)
		{
		case 0:  direction[0] =  1.0f; direction[1] = -t;    direction[2] = -s;    break;   // +X
		case 1:  direction[0] = -1.0f; direction[1] = -t;    direction[2] =  s;    break;   // −X
		case 2:  direction[0] =  s;    direction[1] =  1.0f; direction[2] =  t;    break;   // +Y
		case 3:  direction[0] =  s;    direction[1] = -1.0f; direction[2] = -t;    break;   // −Y
		case 4:  direction[0] =  s;    direction[1] = -t;    direction[2] =  1.0f; break;   // +Z
		default: direction[0] = -s;    direction[1] = -t;    direction[2] = -1.0f; break;   // −Z
		}
	}
}

bool TextureLoader::LoadCubeFromFile(const std::wstring& path, bool srgb, bool generateMips, TextureImage& out)
{
	DirectX::ScratchImage cube;
	std::error_code ec;
	if (std::filesystem::is_directory(path, ec))
	{
		// 면 이미지 6장 → 큐브 ScratchImage 하나. 각 면을 FinishImage(2D, 밉 없음)에 한 번 통과시켜 R8G8B8A8_UNORM 바이트로 맞춘 뒤 행 단위로 복사함.
		// 라벨(sRGB)은 면마다 붙이지 않고 마지막 FinishImage 에서 큐브 전체에 붙임 — 밉 생성도 거기서 sRGB 규칙으로 함.
		uint32_t size = 0;
		for (int face = 0; face < 6; ++face)
		{
			const std::wstring file = FindFaceFile(path, face);
			if (file.empty())
			{
				Log::Error("TextureLoader : 큐브 면 파일이 없음 — '%s' (또는 %s / %s) .png/.jpg/.bmp, 폴더 %s",
					Log::ToUtf8(kFaceNames[face][0]).c_str(), Log::ToUtf8(kFaceNames[face][1]).c_str(), Log::ToUtf8(kFaceNames[face][2]).c_str(),
					Log::ToUtf8(path.c_str()).c_str());
				return false;
			}
			DirectX::ScratchImage loaded;
			TextureImage faceImage;
			if (!LoadScratchFromFile(file, loaded) || !FinishImage(loaded, false, false, faceImage)) return false;
			if (faceImage.desc.format != Format::R8G8B8A8_UNORM)
			{
				// 면 이미지 폴더는 8비트 이미지(PNG·JPG·BMP)만 받음. HDR 큐브는 큐브 DDS 로 줘야 함.
				Log::Error("TextureLoader : 큐브 면 폴더에는 8비트 이미지만 쓸 수 있음 (%s). HDR 은 큐브 DDS 로.", Log::ToUtf8(file.c_str()).c_str());
				return false;
			}
			if (face == 0)
			{
				size = faceImage.desc.width;
				if (faceImage.desc.width != faceImage.desc.height || FAILED(cube.InitializeCube(DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1)))
				{
					Log::Error("TextureLoader : 큐브 면은 정사각형이어야 함 (%ux%u, %s).", faceImage.desc.width, faceImage.desc.height, Log::ToUtf8(file.c_str()).c_str());
					return false;
				}
			}
			else if (faceImage.desc.width != size || faceImage.desc.height != size)
			{
				Log::Error("TextureLoader : 큐브 면 크기가 서로 다름 (%u² 와 %ux%u, %s).", size, faceImage.desc.width, faceImage.desc.height, Log::ToUtf8(file.c_str()).c_str());
				return false;
			}
			const DirectX::Image* dst = cube.GetImage(0, face, 0);
			for (uint32_t y = 0; y < size; ++y)
			{
				std::memcpy(dst->pixels + y * dst->rowPitch,
					static_cast<const uint8_t*>(faceImage.subresources[0].data) + static_cast<size_t>(y) * faceImage.subresources[0].rowPitch,
					static_cast<size_t>(size) * 4);
			}
		}
	}
	else if (!LoadScratchFromFile(path, cube))   // 큐브 DDS (면 6장 + 큐브 플래그). 아니면 FinishImage 가 거부함.
	{
		return false;
	}

	if (!FinishImage(cube, srgb, generateMips, out, TextureDimension::TextureCube))
	{
		Log::Error("TextureLoader : 큐브맵 로드 실패 %s", Log::ToUtf8(path.c_str()).c_str());
		return false;
	}
	Log::Info("큐브맵 로드: %s (%ux%u × 6면, 밉 %u, %s)", Log::ToUtf8(path.c_str()).c_str(),
		out.desc.width, out.desc.height, out.desc.mipLevels, ColorSpaceName(out));
	return true;
}

bool TextureLoader::CreateCube(uint32_t size, const std::function<void(const float direction[3], uint8_t rgba[4])>& shade,
	bool srgb, bool generateMips, TextureImage& out)
{
	DirectX::ScratchImage cube;
	if (size == 0 || FAILED(cube.InitializeCube(DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1)))
	{
		return false;
	}
	for (int face = 0; face < 6; ++face)
	{
		const DirectX::Image* img = cube.GetImage(0, face, 0);
		for (uint32_t y = 0; y < size; ++y)
		{
			uint8_t* row = img->pixels + y * img->rowPitch;
			for (uint32_t x = 0; x < size; ++x)
			{
				// 텍셀 중심. 면 가장자리에서 이웃 면과 같은 방향을 두 번 쓰지 않음.
				const float s = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
				const float t = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
				float direction[3];
				CubeTexelDirection(face, s, t, direction);
				const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
				for (float& component : direction) component /= length;
				shade(direction, row + x * 4);
			}
		}
	}
	return FinishImage(cube, srgb, generateMips, out, TextureDimension::TextureCube);
}
