#include "RHI/Compat/RHICommon.h"
#include "Renderer/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Renderer/ShaderConstants.h"
#include "RHI/Compat/SherlockLog.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

// 2026/10/23: HDR 구간 후처리 (블러 → 블룸). Renderer 의 멤버 함수를 파일만 나눠 구현함 — Renderer.cpp 가 너무 길어지지 않게.
//
// 순서는 고정임 (사용자가 바꿀 수 없음): 선형 HDR 값에서 해야 1 을 넘는 밝기가 살아 있으므로 톤매핑 앞에 둠.
// 효과 하나를 추가할 때: PostProcessSettings 에 필드 → 여기 ApplyHdrEffects 에 호출 하나 → (새 모드가 필요하면) PostPixelShader.hlsl 에 모드 하나.
// 결과는 항상 hdr 텍스처에 "제자리"로 남으므로 톤 패스는 효과가 몇 개 켜졌는지 모름.

namespace
{
	constexpr float kReferenceHeight = 720.0f;   // PostProcessSettings 의 블러 반경이 기준으로 삼는 출력 높이
	constexpr int kMaxBlurRadius = 64;           // PostPixelShader.hlsl 의 kMaxBlurRadius 와 같아야 함
}

PipelineHandle Renderer::GetPostPipeline(bool additive)
{
	// 블러·블룸은 FP16 스크래치/HDR 텍스처에 그림 (깊이 없음 — dsvFormat 은 m_tonemapDesc 에서 물려받은 Unknown).
	PipelineStateDesc desc = additive ? m_postAddDesc : m_postDesc;
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	desc.rtvCount = 1;
	return m_device->CreatePipeline(desc);
}

bool Renderer::EnsurePostScratch(PostScratch& scratch, uint32_t width, uint32_t height, bool needBlur, bool needBloom)
{
	if (m_device == nullptr || width == 0 || height == 0) return false;
	if (scratch.width != width || scratch.height != height) DestroyPostScratch(scratch);   // hdr 크기가 바뀌었으면 전부 다시

	// 후처리 텍스처는 전부 FP16 — 1 을 넘는 밝기를 잘라내지 않도록 hdr 와 같은 포맷.
	auto create = [this](uint32_t w, uint32_t h, const char* name, TextureHandle& texture, ResourceSetHandle& set)
	{
		const TextureDesc desc{
			.width = w,
			.height = h,
			.format = Format::R16G16B16A16_FLOAT,
			.mipLevels = 1,
			.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
			.debugName = name,
		};
		texture = m_device->CreateTexture(desc);
		if (texture.IsValid()) set = CreatePostSet(texture, name);
		return texture.IsValid() && set.IsValid();
	};

	if (needBlur && !scratch.temp.IsValid())
	{
		if (!create(width, height, "PostTemp", scratch.temp, scratch.tempSet))
		{
			Log::Error("후처리 스크래치 텍스처 생성 실패 (%ux%u).", width, height);
			DestroyPostScratch(scratch);
			return false;
		}
		scratch.tempState = ResourceState::Common;
		Log::Info("후처리 스크래치(블러) %ux%u (R16G16B16A16_FLOAT)", width, height);
	}
	if (needBloom && scratch.bloomCount == 0)
	{
		// 레벨 i = hdr 의 1/2^(i+1). 변이 4 미만이 되면 거기서 멈춤 (너무 작은 레벨은 의미가 없음).
		uint32_t w = width, h = height;
		for (uint32_t i = 0; i < PostScratch::kBloomLevels; ++i)
		{
			w = (std::max)(1u, (w + 1) / 2);
			h = (std::max)(1u, (h + 1) / 2);
			PostScratch::BloomLevel& level = scratch.bloom[i];
			if (!create(w, h, "PostBloom", level.texture, level.set))
			{
				Log::Error("후처리 스크래치 텍스처 생성 실패 (블룸 레벨 %u, %ux%u).", i, w, h);
				DestroyPostScratch(scratch);
				return false;
			}
			level.state = ResourceState::Common;
			level.width = w;
			level.height = h;
			scratch.bloomCount = i + 1;
			if (w < 4 || h < 4) break;
		}
		Log::Info("후처리 스크래치(블룸) 레벨 %u개, 가장 큰 쪽 %ux%u", scratch.bloomCount, scratch.bloom[0].width, scratch.bloom[0].height);
	}
	scratch.width = width;
	scratch.height = height;
	return true;
}

void Renderer::DestroyPostScratch(PostScratch& scratch)
{
	if (m_device != nullptr)
	{
		// D3D12 는 지연 해제라 이미 기록된 명령이 읽어도 안전함.
		if (scratch.tempSet.IsValid()) m_device->DestroyResourceSet(scratch.tempSet);
		if (scratch.temp.IsValid()) m_device->DestroyTexture(scratch.temp);
		for (PostScratch::BloomLevel& level : scratch.bloom)
		{
			if (level.set.IsValid()) m_device->DestroyResourceSet(level.set);
			if (level.texture.IsValid()) m_device->DestroyTexture(level.texture);
		}
	}
	scratch = PostScratch{};
}

void Renderer::RequestPostCapture()
{
	m_postCaptureRequested = true;
	// 썸네일 텍스처는 여기(UI 를 만드는 중)에서 만들고 바꿈. Render 에서 바꾸면 이 프레임의 UI 가 이미 집어 든 ImGui 텍스처 ID(D3D11 은 SRV 포인터)가
	// UI 패스가 그리기 전에 해제됨. 씬 뷰 텍스처(SetSceneTarget)를 UI 단계에서 바꾸는 것과 같은 규칙임.
	if (IsOffscreen()) EnsureStageThumbs(m_sceneWidth, m_sceneHeight);
}

void Renderer::EnsureStageThumbs(uint32_t hdrWidth, uint32_t hdrHeight)
{
	if (m_device == nullptr || hdrWidth == 0 || hdrHeight == 0) return;
	// 폭 384 (HDR 가 더 좁으면 그 폭), 높이는 같은 종횡비. 종횡비가 바뀌면(씬 뷰 리사이즈) 다시 만듦.
	const uint32_t width = (std::min)(384u, hdrWidth);
	const uint32_t height = (std::max)(1u, static_cast<uint32_t>(static_cast<uint64_t>(width) * hdrHeight / hdrWidth));
	if (width == m_thumbWidth && height == m_thumbHeight && m_stageThumbs[0].texture.IsValid()) return;

	DestroyStageThumbs();
	static const char* const kNames[kPostStageCount] = { "Scene", "Blur", "Bloom", "Final" };
	for (uint32_t i = 0; i < kPostStageCount; ++i)
	{
		const TextureDesc desc{
			.width = width,
			.height = height,
			.format = Format::R8G8B8A8_UNORM,   // 씬 뷰와 같은 TYPELESS 규칙 (sRGB RTV + UNORM SRV 로 ImGui 가 읽음)
			.mipLevels = 1,
			.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
			.debugName = "PostStageThumb",
		};
		m_stageThumbs[i].texture = m_device->CreateTexture(desc);
		m_stageThumbs[i].name = kNames[i];
		m_stageThumbStates[i] = ResourceState::Common;
		if (!m_stageThumbs[i].texture.IsValid())
		{
			Log::Error("후처리 단계 썸네일 생성 실패 (%ux%u).", width, height);
			DestroyStageThumbs();
			return;
		}
	}
	m_thumbWidth = width;
	m_thumbHeight = height;
	Log::Info("후처리 단계 썸네일 %ux%u x %u", width, height, kPostStageCount);
}

void Renderer::DestroyStageThumbs()
{
	for (uint32_t i = 0; i < kPostStageCount; ++i)
	{
		if (m_device != nullptr && m_stageThumbs[i].texture.IsValid()) m_device->DestroyTexture(m_stageThumbs[i].texture);   // D3D12 는 지연 해제 (ImGui 가 읽을 수 있음)
		m_stageThumbs[i] = PostStageThumb{};
		m_stageThumbStates[i] = ResourceState::Common;
	}
	m_thumbWidth = m_thumbHeight = 0;
}

void Renderer::CapturePostStage(RHI::CommandList& cmd, uint32_t stage, const char* name, ResourceSetHandle hdrSet,
	uint32_t hdrWidth, uint32_t hdrHeight, bool skipGrade)
{
	PostStageThumb& thumb = m_stageThumbs[stage];
	if (!thumb.texture.IsValid() || m_thumbWidth == 0) return;
	// 지금까지의 HDR 결과를 노출·ACES 로 톤매핑해 작은 텍스처에 그림 (톤 패스의 셰이더가 입력/출력 크기 비로 읽음 = 최근접 축소).
	// 단계 0~2 는 색 효과를 건너뜀 — "이 효과가 한 일"만 보이게. 마지막 Final 만 색 효과까지 포함.
	RenderTonemapPass(cmd, hdrSet, thumb.texture, m_stageThumbStates[stage], ResourceState::ShaderResource, name,
		hdrWidth, hdrHeight, m_thumbWidth, m_thumbHeight, skipGrade);
	m_stageThumbStates[stage] = ResourceState::ShaderResource;
	thumb.active = true;
}

void Renderer::ApplyHdrEffects(RHI::CommandList& cmd, PostScratch& scratch, TextureHandle hdr, ResourceState& hdrState, ResourceSetHandle hdrSet,
	uint32_t width, uint32_t height, bool capture)
{
	const PostProcessSettings& post = m_postSettings;
	bool blur = post.blurEnabled && post.blurRadius > 0.0f;
	bool bloom = post.bloomEnabled && post.bloomIntensity > 0.0f;
	if ((blur || bloom) && !EnsurePostScratch(scratch, width, height, blur, bloom))
	{
		blur = bloom = false;   // 못 만들면 효과만 건너뜀 (씬은 그대로 나감)
	}
	if (capture) CapturePostStage(cmd, 0, "Scene", hdrSet, width, height, true);

	const float w = static_cast<float>(width);
	const float h = static_cast<float>(height);

	// ---- 가우시안 블러: 가로 (hdr → temp) 후 세로 (temp → hdr). 반경은 720p 기준이라 출력 높이에 비례해 스케일함 ----
	if (blur)
	{
		const float effectiveRadius = (std::min)(post.blurRadius * h / kReferenceHeight, static_cast<float>(kMaxBlurRadius));
		PostParamsConstants params = {};
		params.blur = XMFLOAT4((std::max)(effectiveRadius / 2.5f, 0.5f), std::ceil((std::max)(effectiveRadius, 1.0f)), 0.0f, 0.0f);   // 시그마, 반경 (±2.5σ 에서 자름)
		params.view = XMFLOAT4(w, h, w, h);

		params.pass = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);   // 모드 1, 방향 (1, 0)
		UploadPostParams(params);
		RenderPostPass(cmd, PostPassDesc{
			.pipeline = GetPostPipeline(false),
			.input = hdrSet,
			.output = scratch.temp,
			.outputBefore = scratch.tempState,
			.outputAfter = ResourceState::ShaderResource,
			.load = LoadOp::DontCare,
			.debugName = "BlurH",
		});
		scratch.tempState = ResourceState::ShaderResource;

		params.pass = XMFLOAT4(1.0f, 0.0f, 1.0f, 0.0f);   // 방향 (0, 1). 결과를 hdr 에 되돌려 씀
		UploadPostParams(params);
		RenderPostPass(cmd, PostPassDesc{
			.pipeline = GetPostPipeline(false),
			.input = scratch.tempSet,
			.output = hdr,
			.outputBefore = hdrState,
			.outputAfter = ResourceState::ShaderResource,
			.load = LoadOp::DontCare,
			.debugName = "BlurV",
		});
		hdrState = ResourceState::ShaderResource;
		if (capture) CapturePostStage(cmd, 1, "Blur", hdrSet, width, height, true);
	}

	// ---- 블룸: 밝기 추출 + 축소 (hdr → 레벨 0) → 축소 체인 → 확대하며 가산 (레벨 n-1 → … → 0) → hdr 에 intensity 만큼 가산 ----
	// 마지막 가산이 hdr 를 렌더 타깃으로 쓰고(Load) 입력은 레벨 0 이라, 같은 텍스처를 읽고 쓰는 일이 없음.
	if (bloom && scratch.bloomCount > 0)
	{
		PostScratch::BloomLevel* levels = scratch.bloom;
		const uint32_t count = scratch.bloomCount;

		PostParamsConstants params = {};
		params.bloom = XMFLOAT4(post.bloomThreshold, post.bloomThreshold * post.bloomKnee, 1.0f, 0.0f);

		auto run = [&](float mode, ResourceSetHandle input, float srcW, float srcH, PostScratch::BloomLevel* outLevel, const char* name, bool additive, float scale)
		{
			params.pass = XMFLOAT4(mode, 0.0f, 0.0f, 0.0f);
			params.view = XMFLOAT4(srcW, srcH, static_cast<float>(outLevel->width), static_cast<float>(outLevel->height));
			params.bloom.z = scale;
			UploadPostParams(params);
			RenderPostPass(cmd, PostPassDesc{
				.pipeline = GetPostPipeline(additive),
				.input = input,
				.output = outLevel->texture,
				.outputBefore = outLevel->state,
				.outputAfter = ResourceState::ShaderResource,
				.load = additive ? LoadOp::Load : LoadOp::DontCare,
				.debugName = name,
			});
			outLevel->state = ResourceState::ShaderResource;
		};

		run(2.0f, hdrSet, w, h, &levels[0], "BloomPrefilter", false, 1.0f);
		for (uint32_t i = 1; i < count; ++i)
			run(3.0f, levels[i - 1].set, static_cast<float>(levels[i - 1].width), static_cast<float>(levels[i - 1].height), &levels[i], "BloomDown", false, 1.0f);
		for (uint32_t i = count - 1; i-- > 0;)
			run(4.0f, levels[i + 1].set, static_cast<float>(levels[i + 1].width), static_cast<float>(levels[i + 1].height), &levels[i], "BloomUp", true, 1.0f);

		// 합성: 레벨 0 → hdr (가산, intensity)
		params.pass = XMFLOAT4(4.0f, 0.0f, 0.0f, 0.0f);
		params.view = XMFLOAT4(static_cast<float>(levels[0].width), static_cast<float>(levels[0].height), w, h);
		params.bloom.z = post.bloomIntensity;
		UploadPostParams(params);
		RenderPostPass(cmd, PostPassDesc{
			.pipeline = GetPostPipeline(true),
			.input = levels[0].set,
			.output = hdr,
			.outputBefore = hdrState,
			.outputAfter = ResourceState::ShaderResource,
			.load = LoadOp::Load,
			.debugName = "BloomComposite",
		});
		hdrState = ResourceState::ShaderResource;
		if (capture) CapturePostStage(cmd, 2, "Bloom", hdrSet, width, height, true);
	}
	if (capture) CapturePostStage(cmd, 3, "Final", hdrSet, width, height, false);
}
