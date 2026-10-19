#include "RHI/Compat/RHICommon.h"
#include "Renderer/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Renderer/ShaderConstants.h"
#include "Renderer/VertexTypes.h"
#include "Renderer/TextureLoader.h"
#include "Renderer/EmbeddedTextures.h"
#include "RHI/Compat/SherlockPaths.h"
#include "RHI/Compat/SherlockLog.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	constexpr const wchar_t* kVertexShaderFile = L"BasicVertexShader.hlsl";
	constexpr const wchar_t* kPixelShaderFile = L"BasicPixelShader.hlsl";
	constexpr const wchar_t* kShadowShaderFile = L"ShadowVertexShader.hlsl";
	// Nereides: 스키닝 변형. 같은 소스를 #include 하는 얇은 파일 — LoadOrCompile 의 .cso 이름이 파일 이름을 따르기 때문.
	constexpr const wchar_t* kSkinnedVertexShaderFile = L"SkinnedVertexShader.hlsl";
	constexpr const wchar_t* kShadowSkinnedShaderFile = L"ShadowSkinnedVertexShader.hlsl";
	constexpr const wchar_t* kSkyVertexShaderFile = L"SkyboxVertexShader.hlsl";   // 2026-10-08
	constexpr const wchar_t* kSkyPixelShaderFile = L"SkyboxPixelShader.hlsl";
	constexpr const wchar_t* kCommonShaderFile = L"Common.hlsli";
	constexpr uint64_t kHotReloadPollIntervalMs = 500;   // 벽시계 밀리초

	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}

	// 텍스처 이름 → 파일 경로. 절대 경로는 그대로, 상대 경로는 실행 폴더 기준과 assets/textures 기준을 차례로 찾음.
	std::wstring ResolveTexturePath(const std::string& name)
	{
		const std::filesystem::path given(ToWide(name));
		if (given.is_absolute()) return given.wstring();
		std::error_code error;
		for (const std::filesystem::path& base : { std::filesystem::path(), std::filesystem::path(L"assets") / L"textures" })
		{
			const std::filesystem::path candidate = base / given;
			if (std::filesystem::exists(candidate, error)) return candidate.wstring();
		}
		return given.wstring();
	}

	// 가시성: 포인터가 가리키는 객체가 캐시 항목이 기록한 객체와 같은가
	template <class T>
	bool SameOwner(const std::weak_ptr<const T>& cached, const std::shared_ptr<const T>& current)
	{
		return !cached.owner_before(current) && !current.owner_before(cached);
	}
}

Renderer::Renderer()
{
	auto defaultMaterial = std::make_shared<Material>();
	defaultMaterial->name = "Default";
	m_defaultMaterial = std::move(defaultMaterial);
	XMStoreFloat4x4(&m_lightViewProj, XMMatrixIdentity());
}

Renderer::~Renderer()
{
	Shutdown();
}

bool Renderer::Initialize(RHI::Device* device)
{
	if (device == nullptr)
	{
		Log::Error("Renderer::Initialize : Device가 없음.");
		return false;
	}
	m_device = device;

	// ---- 상수버퍼. 전부 Dynamic(매 프레임/드로우마다 Map WRITE_DISCARD) ----
	// C++20 (2026-09-28): Desc 는 지정 초기화(designated initializer)로 채움. "빈 Desc 를 만들고 필드를 하나씩 대입"하던
	// 코드가 D3D12 의 desc 채우기와 같은 한 덩어리 리터럴이 됨. 나열하지 않은 필드(stride)는 헤더의 기본값을 받음.
	auto makeCB = [this](uint32_t size, const char* name)
	{
		const BufferDesc desc{ .size = size, .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_Constant, .debugName = name };
		return m_device->CreateBuffer(desc);
	};
	m_perFrameCB = makeCB(sizeof(PerFrameConstants), "CB_PerFrame");
	m_perObjectCB = makeCB(sizeof(PerObjectConstants), "CB_PerObject");
	m_boneCB = makeCB(sizeof(BoneConstants), "CB_Bones");
	m_lightCB = makeCB(sizeof(LightConstants), "CB_Lights");
	if (!m_perFrameCB.IsValid() || !m_perObjectCB.IsValid() || !m_boneCB.IsValid() || !m_lightCB.IsValid())
	{
		Log::Error("Renderer::Initialize : 상수버퍼 생성 실패.");
		return false;
	}

	if (!CreateSamplers() || !CreateBuiltinTextures() || !CreateShadowResources())
	{
		return false;
	}

	// ---- 바인딩 레이아웃. "이 파이프라인은 b0·b1·b2·b3·t0·t1·t8·t9·s0·s4·s5 를 쓴다"는 선언 ----
	BindingLayoutDesc frameLayout;
	frameLayout.debugName = "Layout_Frame";
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotPerFrame);
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotLights);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotShadowMap);
	frameLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotShadow);
	// 2026-10-08: 환경 큐브맵. 재질 셋이 아니라 프레임 셋에 둠 — 씬마다 하나이고, 스카이박스와 (이후의) 반사·환경광이 같은 큐브를 읽음.
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotEnvironment);
	frameLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotEnvironment);
	// 2026-10-08: IBL. 확산 조도 큐브, 반사 프리필터 큐브, BRDF LUT. 모두 환경 샘플러(s5)로 읽음.
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotIrradiance);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotPrefiltered);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotBrdfLut);
	m_frameLayout = m_device->CreateBindingLayout(frameLayout);

	BindingLayoutDesc objectLayout;
	objectLayout.debugName = "Layout_Object";
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotPerObject);
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotBones);
	m_objectLayout = m_device->CreateBindingLayout(objectLayout);

	// 재질 셋: 상수(b3, uvScale 때문에 VS도), 알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4 (2026-10-08 PBR), 샘플러 s0.
	BindingLayoutDesc materialLayout;
	materialLayout.debugName = "Layout_Material";
	materialLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotMaterial);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotAlbedo);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotNormal);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotMetallicRoughness);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotOcclusion);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotEmissive);
	materialLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotMaterial);
	m_materialLayout = m_device->CreateBindingLayout(materialLayout);

	if (!m_frameLayout.IsValid() || !m_objectLayout.IsValid() || !m_materialLayout.IsValid())
	{
		Log::Error("Renderer::Initialize : BindingLayout 생성 실패.");
		return false;
	}

	// ---- 프레임/오브젝트 ResourceSet. 재질 셋은 재질마다 GetOrCreateGpuMaterial이 만듦 ----
	// 프레임 셋은 씬의 환경(스카이박스·IBL)이 바뀔 때 다시 만들어야 하므로 UpdateFrameSet 이 만듦. 처음에는 빈 환경(검은 큐브·흰 LUT)을 꽂음.
	ResolveEnvironment(nereides::RenderEnvironment{});
	UpdateFrameSet();

	const ResourceSetDesc objectSet{
		.layout = m_objectLayout,
		.bindings = { { .buffer = m_perObjectCB }, { .buffer = m_boneCB } },
		.debugName = "Set_Object",
	};
	m_objectSet = m_device->CreateResourceSet(objectSet);

	if (!m_frameSet.IsValid() || !m_objectSet.IsValid())
	{
		Log::Error("Renderer::Initialize : ResourceSet 생성 실패.");
		return false;
	}

	// ---- 셰이더 (.cso 우선, 필요 시 컴파일) + 레이아웃 대조 ----
	if (!LoadShaders(false))
	{
		return false;
	}

	// ---- PSO의 고정 부분. 나머지(깊이 테스트 켬, 불투명, 트라이앵글 리스트)는 Desc 기본값을 씀 ----
	// PipelineStateDesc 를 D3D12_GRAPHICS_PIPELINE_STATE_DESC 처럼 한 리터럴로 적음. 지정 초기화는 선언 순서를 따라야 하므로
	// 필드 순서는 PipelineTypes.h 와 같음: vs, ps, bindingLayouts, bindingLayoutCount, vertexLayout, rasterizer, ..., rtvCount, dsvFormat.
	m_baseDesc = PipelineStateDesc
	{
		.vs = m_vs,
		.ps = m_ps,
		.bindingLayouts = { m_frameLayout, m_objectLayout, m_materialLayout },
		.bindingLayoutCount = 3,
	};

	// 그림자 패스: VS 만(ps 는 빈 핸들), 렌더 타깃 없음, 깊이 D32_FLOAT. 래스터라이저 깊이 바이어스로 자기 그림자(acne)를 줄임.
	// D32_FLOAT 에서 DepthBias 단위는 2^(지수−23) — z ≈ 0.5 면 한 단위가 6e-8 임.
	m_shadowDesc = PipelineStateDesc{
		.vs = m_shadowVs,
		.bindingLayouts = { m_frameLayout, m_objectLayout },
		.bindingLayoutCount = 2,
		.rasterizer = { .depthBias = 2000, .depthBiasClamp = 0.01f, .slopeScaledDepthBias = 2.0f },
		.rtvCount = 0,
		.dsvFormat = Format::D32_FLOAT,
	};

	// 2026-10-08: 스카이박스. 정점 입력이 없으므로 vertexLayout 은 기본값(속성 0개) 그대로 둠 — 입력 레이아웃 없이 SV_VertexID 만 씀.
	// 깊이: 테스트는 켜고(LessEqual — VS 가 만든 깊이 1.0 이 클리어 값 1.0 과 같아도 통과) 쓰기는 끔. 컬링: 전체 화면 삼각형 하나라 끔.
	m_skyDesc = PipelineStateDesc{
		.vs = m_skyVs,
		.ps = m_skyPs,
		.bindingLayouts = { m_frameLayout },
		.bindingLayoutCount = 1,
		.rasterizer = { .cull = CullMode::None },
		.depthStencil = { .depthEnable = true, .depthWrite = false, .depthFunc = CompareFunc::LessEqual },
	};

	// 첫 프레임 전에 기본 PSO를 만들어 두어 생성 실패를 초기화 단계에서 잡음.
	if (!GetPipelineFor(*m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangent).IsValid() ||
		!GetShadowPipelineFor(*m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangent).IsValid() ||
		!GetPipelineFor(*m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangentSkinned).IsValid() ||
		!GetShadowPipelineFor(*m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangentSkinned).IsValid() ||
		!GetSkyPipeline().IsValid())
	{
		Log::Error("Renderer::Initialize : 기본 PSO 생성 실패.");
		return false;
	}

	return true;
}

bool Renderer::CreateSamplers()
{
	// SamplerPreset 순서와 같아야 함.
	struct PresetDesc { SamplerFilter filter; SamplerAddress address; float maxLod; const char* name; };
	const PresetDesc presets[kSamplerPresetCount] =
	{
		{ SamplerFilter::Linear,      SamplerAddress::Wrap,  1000.0f, "Sampler_LinearWrap" },
		{ SamplerFilter::Linear,      SamplerAddress::Clamp, 1000.0f, "Sampler_LinearClamp" },
		{ SamplerFilter::Anisotropic, SamplerAddress::Wrap,  1000.0f, "Sampler_AnisotropicWrap" },
		{ SamplerFilter::Point,       SamplerAddress::Wrap,  1000.0f, "Sampler_PointWrap" },
		{ SamplerFilter::Point,       SamplerAddress::Wrap,  0.0f,    "Sampler_PointNoMip" },   // maxLod 0 = 밉 0만
	};
	for (uint32_t i = 0; i < kSamplerPresetCount; ++i)
	{
		const SamplerDesc desc{
			.filter = presets[i].filter,
			.addressU = presets[i].address,
			.addressV = presets[i].address,
			.addressW = presets[i].address,
			.maxAnisotropy = 16,
			.maxLod = presets[i].maxLod,
			.debugName = presets[i].name,
		};
		m_samplers[i] = m_device->CreateSampler(desc);
		if (!m_samplers[i].IsValid())
		{
			Log::Error("Renderer : 샘플러 생성 실패 (%s).", presets[i].name);
			return false;
		}
	}
	return true;
}

bool Renderer::CreateBuiltinTextures()
{
	m_whiteTexture = GetOrLoadTexture("builtin:white", false);
	m_flatNormalTexture = GetOrLoadTexture("builtin:flatnormal", false);
	m_blackCube = GetOrLoadCubemap("builtin:black");   // 2026-10-08: 환경이 없을 때 t9~t11 의 기본값
	return m_whiteTexture.IsValid() && m_flatNormalTexture.IsValid() && m_blackCube.IsValid();
}

void Renderer::ResolveEnvironment(const nereides::RenderEnvironment& environment)
{
	EnvironmentBinding binding;
	binding.skybox = binding.irradiance = binding.specular = m_blackCube;
	binding.brdfLut = m_whiteTexture;

	// 실패한 이름은 GetOrLoadCubemap 이 검은 큐브를 돌려주므로 "검은 큐브가 아님" = 로드 성공.
	auto load = [this](const std::string& name, uint32_t& mips) -> TextureHandle
	{
		if (name.empty()) return TextureHandle{};
		const TextureHandle handle = GetOrLoadCubemap(name, &mips);
		return handle != m_blackCube ? handle : TextureHandle{};
	};
	uint32_t skyMips = 1, irradianceMips = 1, specularMips = 1;
	const TextureHandle sky = load(environment.skybox, skyMips);
	const TextureHandle irradiance = load(environment.irradiance, irradianceMips);
	const TextureHandle specular = load(environment.specular, specularMips);
	if (sky.IsValid()) binding.skybox = sky;

	// 반사: 프리필터 큐브 → 없으면 스카이박스의 밉 체인으로 근사(박스 필터라 GGX 로 거른 것보다 거칠기 모양이 덜 맞음).
	if (specular.IsValid()) { binding.specular = specular; binding.specularMips = specularMips; }
	else if (sky.IsValid()) { binding.specular = sky; binding.specularMips = skyMips; }
	// 확산: 조도 큐브 → 없으면 스카이박스(또는 반사 큐브)의 끝에서 두 번째 밉. 면당 2×2 라 위·아래·옆의 밝기 차이 정도만 남음.
	if (irradiance.IsValid()) { binding.irradiance = irradiance; binding.irradianceLod = 0.0f; }
	else if (binding.specular != m_blackCube)
	{
		binding.irradiance = binding.specular;
		binding.irradianceLod = static_cast<float>(binding.specularMips >= 2 ? binding.specularMips - 2 : 0);
	}
	binding.hasEnvironment = irradiance.IsValid() || binding.specular != m_blackCube;

	// BRDF LUT 는 2D 텍스처 캐시를 씀 (선형, 부동소수점 DDS 면 HDR 경로). 실패하면 흰색이 돌아오므로 그것으로 판단함.
	if (!environment.brdfLut.empty())
	{
		const TextureHandle lut = GetOrLoadTexture(environment.brdfLut, false);
		if (lut != m_whiteTexture) { binding.brdfLut = lut; binding.hasBrdfLut = true; }
	}

	binding.iblIntensity = environment.iblIntensity;
	binding.exposure = environment.exposure;
	binding.toneMapping = environment.toneMapping;
	m_environment = binding;
}

bool Renderer::UpdateFrameSet()
{
	if (m_frameSet.IsValid() && m_environment.SameTextures(m_boundEnvironment)) return true;

	// ResourceSet 은 값 객체라 슬롯 하나만 바꾸는 기능이 없음 — 재질 셋과 같이 통째로 다시 만듦 (D3D12 디스크립터 테이블도 다시 씀).
	// 이전 셋은 D3D12 에서 지연 해제되므로, 이미 기록된 드로우가 그 테이블을 읽어도 안전함.
	m_device->DestroyResourceSet(m_frameSet);
	// bindings[i] 는 Initialize 의 frameLayout.Add(...) i 번째 슬롯과 짝임: b0, b2, t8, s4, t9, s5, t10, t11, t12.
	// 환경 샘플러는 재질 프리셋의 LinearClamp 를 함께 씀 (큐브 샘플은 면 경계를 하드웨어가 이어 주고, LUT 는 가장자리에서 반복되면 안 됨).
	const ResourceSetDesc frameSet{
		.layout = m_frameLayout,
		.bindings = {
			{ .buffer = m_perFrameCB }, { .buffer = m_lightCB }, { .texture = m_shadowMap }, { .sampler = m_shadowSampler },
			{ .texture = m_environment.skybox }, { .sampler = GetSampler(SamplerPreset::LinearClamp) },
			{ .texture = m_environment.irradiance }, { .texture = m_environment.specular }, { .texture = m_environment.brdfLut },
		},
		.debugName = "Set_Frame",
	};
	m_frameSet = m_device->CreateResourceSet(frameSet);
	m_boundEnvironment = m_environment;
	if (!m_frameSet.IsValid())
	{
		Log::Error("Renderer : 프레임 ResourceSet 생성 실패.");
		return false;
	}
	return true;
}

bool Renderer::CreateShadowResources()
{
	// 깊이 전용 텍스처지만 셰이더에서도 읽음 → Device 가 TYPELESS 리소스 + DSV(D32) + SRV(R32) 로 만듦.
	const TextureDesc desc{
		.width = kShadowMapSize,
		.height = kShadowMapSize,
		.format = Format::D32_FLOAT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil | TextureBind_ShaderResource,
		.debugName = "ShadowMap",
	};
	m_shadowMap = m_device->CreateTexture(desc);
	m_shadowMapState = ResourceState::Common;

	// 비교 샘플러: SampleCmp 가 "비교값(받는 쪽 깊이) ≤ 저장된 깊이" 면 1(빛 받음), 아니면 0 을 돌려주고 이웃과 보간함(하드웨어 PCF).
	// 맵 밖은 Border 1 로 두어 빛을 받는 것으로 처리함.
	const SamplerDesc sampler{
		.filter = SamplerFilter::Comparison,
		.addressU = SamplerAddress::Border,
		.addressV = SamplerAddress::Border,
		.addressW = SamplerAddress::Border,
		.maxAnisotropy = 1,
		.compareFunc = CompareFunc::LessEqual,
		.borderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.debugName = "Sampler_ShadowCompare",
	};
	m_shadowSampler = m_device->CreateSampler(sampler);

	if (!m_shadowMap.IsValid() || !m_shadowSampler.IsValid())
	{
		Log::Error("Renderer : 그림자 맵 리소스 생성 실패.");
		return false;
	}
	Log::Info("그림자 맵: %ux%u D32_FLOAT (DSV + SRV), PCF 3x3", kShadowMapSize, kShadowMapSize);
	return true;
}

SamplerHandle Renderer::GetSampler(SamplerPreset preset) const
{
	const uint32_t index = static_cast<uint32_t>(preset);
	return index < kSamplerPresetCount ? m_samplers[index] : m_samplers[0];
}

TextureHandle Renderer::GetOrLoadTexture(const std::string& name, bool srgb)
{
	// 빈 이름 = 텍스처 없음. 캐시 항목을 따로 만들지 않고 기본 흰색을 그대로 씀.
	if (name.empty() && m_whiteTexture.IsValid())
	{
		return m_whiteTexture;
	}
	const std::string key = name + (srgb ? "|srgb" : "|linear");
	auto found = m_textureCache.find(key);
	if (found != m_textureCache.end())
	{
		return found->second;
	}

	TextureImage image;
	bool ok = false;
	std::string debugName = name;
	if (name.empty() || name == "builtin:white")
	{
		const uint8_t white[4] = { 255, 255, 255, 255 };
		ok = TextureLoader::CreateSolid(1, white, false, image);   // 흰색은 sRGB든 선형이든 1.0
		debugName = "builtin:white";
	}
	else if (name == "builtin:flatnormal")
	{
		// 탄젠트 공간 (0, 0, 1) = 평평한 노멀. 항상 선형.
		const uint8_t flat[4] = { 128, 128, 255, 255 };
		ok = TextureLoader::CreateSolid(1, flat, false, image);
	}
	else if (name == "builtin:gray128")
	{
		// 감마 검증용. sRGB 128 → 선형 0.216 → sRGB 뷰에 쓰면 다시 128.
		const uint8_t gray[4] = { 128, 128, 128, 255 };
		ok = TextureLoader::CreateSolid(4, gray, srgb, image);
	}
	else if (name == "builtin:checker")
	{
		const uint8_t a[4] = { 230, 230, 230, 255 };
		const uint8_t b[4] = { 40, 40, 48, 255 };
		ok = TextureLoader::CreateChecker(512, 16, a, b, srgb, true, image);
	}
	else if (const auto embedded = nereides::EmbeddedTextures::Find(name))
	{
		// 모델 파일 안에 든 압축 이미지 ("<모델 파일명>/<번호>")
		ok = TextureLoader::LoadFromMemory(embedded->data(), embedded->size(), srgb, true, image);
	}
	else
	{
		ok = TextureLoader::LoadFromFile(ResolveTexturePath(name), srgb, true, image);
	}

	TextureHandle handle;
	if (ok)
	{
		image.desc.debugName = debugName.c_str();
		handle = m_device->CreateTexture(image.desc, image.subresources);   // C++20: vector → std::span (포인터+개수 대신)
	}
	if (!handle.IsValid())
	{
		Log::Warn("텍스처 '%s' 를 쓸 수 없어 흰색으로 대체.", name.c_str());
		handle = m_whiteTexture;
	}
	m_textureCache.emplace(key, handle);
	return handle;
}

TextureHandle Renderer::GetOrLoadCubemap(const std::string& name, uint32_t* mipLevels)
{
	auto found = m_cubeCache.find(name);
	if (found != m_cubeCache.end())
	{
		if (mipLevels != nullptr) *mipLevels = found->second.mipLevels;
		return found->second.handle;
	}

	// 8비트 큐브맵은 "색" 텍스처이므로 sRGB 로 읽음. 부동소수점(HDR) DDS 는 TextureLoader 가 R16G16B16A16_FLOAT 로 읽고 라벨을 무시함.
	TextureImage image;
	bool ok = false;
	if (name == "builtin:black")
	{
		ok = TextureLoader::CreateCube(1, [](const float[3], uint8_t rgba[4]) { rgba[0] = rgba[1] = rgba[2] = 0; rgba[3] = 255; }, false, false, image);
	}
	else if (name == "builtin:sky")
	{
		// 절차적 하늘: 방향의 높이(y)만 봄. 지평선(y = 0)의 옅은 하늘색에서 천정(y = 1)의 짙은 파랑으로, 지평선 아래는 금방 지면색이 됨.
		// 값은 sRGB 바이트 — 감마 공간에서 섞어도 눈에 띄는 차이는 없고, 밉 생성만 선형에서 함(TextureLoader 의 TEX_FILTER_SRGB).
		ok = TextureLoader::CreateCube(128, [](const float d[3], uint8_t rgba[4])
		{
			static constexpr float zenith[3] = { 52.0f, 105.0f, 185.0f };
			static constexpr float horizon[3] = { 185.0f, 208.0f, 230.0f };
			static constexpr float ground[3] = { 92.0f, 88.0f, 82.0f };
			const float y = d[1];
			const float* target = y >= 0.0f ? zenith : ground;
			const float t = y >= 0.0f ? powf(y, 0.6f) : (std::min)(1.0f, -y * 4.0f);
			for (int i = 0; i < 3; ++i) rgba[i] = static_cast<uint8_t>(horizon[i] + (target[i] - horizon[i]) * t + 0.5f);
			rgba[3] = 255;
		}, true, true, image);
	}
	else if (name == "builtin:skyfaces")
	{
		// 방향 검증용: 주축(절댓값이 가장 큰 성분)으로 면을 고르고 면마다 색을 다르게 칠함 — +X 빨강, −X 청록, +Y 초록, −Y 자홍, +Z 파랑, −Z 노랑
		// (마주 보는 면은 보색). 면을 4×4 칸으로 나누는 격자선이 면 경계에서 끊김 없이 이어지면 TextureLoader 의 텍셀 → 방향 변환이 맞는 것.
		ok = TextureLoader::CreateCube(64, [](const float d[3], uint8_t rgba[4])
		{
			static constexpr uint8_t colors[6][3] = { { 220, 50, 50 }, { 50, 200, 200 }, { 60, 200, 60 }, { 200, 60, 200 }, { 60, 90, 230 }, { 230, 210, 50 } };
			const float ax = fabsf(d[0]), ay = fabsf(d[1]), az = fabsf(d[2]);
			int face;
			float u, v;   // 면 위의 나머지 두 성분 ÷ 주축 → [−1, 1]
			if (ax >= ay && ax >= az) { face = d[0] > 0.0f ? 0 : 1; u = d[1] / ax; v = d[2] / ax; }
			else if (ay >= az)        { face = d[1] > 0.0f ? 2 : 3; u = d[0] / ay; v = d[2] / ay; }
			else                      { face = d[2] > 0.0f ? 4 : 5; u = d[0] / az; v = d[1] / az; }
			const bool line = fabsf(u * 2.0f - roundf(u * 2.0f)) < 0.04f || fabsf(v * 2.0f - roundf(v * 2.0f)) < 0.04f;
			for (int i = 0; i < 3; ++i) rgba[i] = line ? static_cast<uint8_t>(colors[face][i] / 3) : colors[face][i];
			rgba[3] = 255;
		}, true, true, image);
	}
	else
	{
		// 2D 텍스처와 같은 경로 규칙. 파일이면 큐브 DDS, 폴더면 면 이미지 6장.
		ok = TextureLoader::LoadCubeFromFile(ResolveTexturePath(name), true, true, image);
	}

	CubeEntry entry;
	if (ok)
	{
		image.desc.debugName = name.c_str();
		entry.handle = m_device->CreateTexture(image.desc, image.subresources);
		entry.mipLevels = image.desc.mipLevels;
	}
	if (!entry.handle.IsValid())
	{
		Log::Warn("큐브맵 '%s' 를 쓸 수 없어 검은 큐브로 대체.", name.c_str());
		entry.handle = m_blackCube;   // 검은 큐브 자신을 만들다 실패했다면 빈 핸들 → Initialize 가 실패함
		entry.mipLevels = 1;
	}
	m_cubeCache.emplace(name, entry);
	if (mipLevels != nullptr) *mipLevels = entry.mipLevels;
	return entry.handle;
}

bool Renderer::LoadShaders(bool fromSourceOnly)
{
	std::vector<uint8_t> vsCode, psCode, shadowCode, skyVsCode, skyPsCode, skinnedVsCode, shadowSkinnedCode;
	std::wstring vsSource, psSource, shadowSource, skyVsSource, skyPsSource, skinnedVsSource, shadowSkinnedSource;

	if (fromSourceOnly)
	{
		vsSource = Paths::GetShaderSourcePath(kVertexShaderFile);
		psSource = Paths::GetShaderSourcePath(kPixelShaderFile);
		shadowSource = Paths::GetShaderSourcePath(kShadowShaderFile);
		skyVsSource = Paths::GetShaderSourcePath(kSkyVertexShaderFile);
		skyPsSource = Paths::GetShaderSourcePath(kSkyPixelShaderFile);
		skinnedVsSource = Paths::GetShaderSourcePath(kSkinnedVertexShaderFile);
		shadowSkinnedSource = Paths::GetShaderSourcePath(kShadowSkinnedShaderFile);
		if (!ShaderCompiler::CompileFromFile(vsSource, "VS_Main", ShaderStage::Vertex, vsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(psSource, "PS_Main", ShaderStage::Pixel, psCode)) return false;
		if (!ShaderCompiler::CompileFromFile(shadowSource, "VS_Shadow", ShaderStage::Vertex, shadowCode)) return false;
		if (!ShaderCompiler::CompileFromFile(skyVsSource, "VS_Sky", ShaderStage::Vertex, skyVsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(skyPsSource, "PS_Sky", ShaderStage::Pixel, skyPsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(skinnedVsSource, "VS_Skinned", ShaderStage::Vertex, skinnedVsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(shadowSkinnedSource, "VS_ShadowSkinned", ShaderStage::Vertex, shadowSkinnedCode)) return false;
	}
	else
	{
		if (!ShaderCompiler::LoadOrCompile(kVertexShaderFile, "VS_Main", ShaderStage::Vertex, vsCode, &vsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kPixelShaderFile, "PS_Main", ShaderStage::Pixel, psCode, &psSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kShadowShaderFile, "VS_Shadow", ShaderStage::Vertex, shadowCode, &shadowSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kSkyVertexShaderFile, "VS_Sky", ShaderStage::Vertex, skyVsCode, &skyVsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kSkyPixelShaderFile, "PS_Sky", ShaderStage::Pixel, skyPsCode, &skyPsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kSkinnedVertexShaderFile, "VS_Skinned", ShaderStage::Vertex, skinnedVsCode, &skinnedVsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kShadowSkinnedShaderFile, "VS_ShadowSkinned", ShaderStage::Vertex, shadowSkinnedCode, &shadowSkinnedSource)) return false;
	}

#if defined(_DEBUG)
	// HLSL cbuffer 크기와 C++ 구조체 크기를 리플렉션으로 대조함. 어긋나면 실패.
	bool layoutOk = true;
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "PerObject", sizeof(PerObjectConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "Material", sizeof(MaterialConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "Lights", sizeof(LightConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "Material", sizeof(MaterialConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(shadowCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(shadowCode, "PerObject", sizeof(PerObjectConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(skyVsCode, "PerFrame", sizeof(PerFrameConstants));   // 2026-10-08: view, proj
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(skinnedVsCode, "Bones", sizeof(BoneConstants));   // Nereides: 스키닝
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(skinnedVsCode, "PerObject", sizeof(PerObjectConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(shadowSkinnedCode, "Bones", sizeof(BoneConstants));
	if (!layoutOk)
	{
		Log::Error("Renderer : 상수버퍼 레이아웃 불일치.");
		return false;
	}
#endif

	// 셰이더가 실제로 쓰는 슬롯이 선언한 레이아웃 안에 있는지 검사함. Release에서는 경고만 함.
	if (!ValidateBindings(vsCode, psCode, shadowCode, skyVsCode, skyPsCode, skinnedVsCode, shadowSkinnedCode))
	{
#if defined(_DEBUG)
		return false;
#endif
	}

	auto makeShader = [this](ShaderStage stage, std::span<const uint8_t> code, const char* name)
	{
		// ShaderDesc::bytecode 는 std::span — vector 의 data()/size() 를 따로 옮기지 않음.
		const ShaderDesc desc{ .stage = stage, .bytecode = code, .debugName = name };
		return m_device->CreateShader(desc);
	};
	const ShaderHandle newVs = makeShader(ShaderStage::Vertex, vsCode, "BasicVS");
	const ShaderHandle newPs = makeShader(ShaderStage::Pixel, psCode, "BasicPS");
	const ShaderHandle newShadowVs = makeShader(ShaderStage::Vertex, shadowCode, "ShadowVS");
	const ShaderHandle newSkyVs = makeShader(ShaderStage::Vertex, skyVsCode, "SkyboxVS");
	const ShaderHandle newSkyPs = makeShader(ShaderStage::Pixel, skyPsCode, "SkyboxPS");
	const ShaderHandle newSkinnedVs = makeShader(ShaderStage::Vertex, skinnedVsCode, "SkinnedVS");
	const ShaderHandle newShadowSkinnedVs = makeShader(ShaderStage::Vertex, shadowSkinnedCode, "ShadowSkinnedVS");

	if (!newVs.IsValid() || !newPs.IsValid() || !newShadowVs.IsValid() || !newSkyVs.IsValid() || !newSkyPs.IsValid()
		|| !newSkinnedVs.IsValid() || !newShadowSkinnedVs.IsValid())
	{
		Log::Error("Renderer : 셰이더 생성 실패.");
		m_device->DestroyShader(newSkinnedVs);
		m_device->DestroyShader(newShadowSkinnedVs);
		m_device->DestroyShader(newVs);
		m_device->DestroyShader(newPs);
		m_device->DestroyShader(newShadowVs);
		m_device->DestroyShader(newSkyVs);
		m_device->DestroyShader(newSkyPs);
		return false;
	}

	// 이전 셰이더는 PSO가 참조로 붙들고 있으므로 풀에서 지워도 캐시가 비워질 때까지 살아 있음.
	m_device->DestroyShader(m_vs);
	m_device->DestroyShader(m_ps);
	m_device->DestroyShader(m_shadowVs);
	m_device->DestroyShader(m_skyVs);
	m_device->DestroyShader(m_skyPs);
	m_device->DestroyShader(m_vsSkinned);
	m_device->DestroyShader(m_shadowVsSkinned);
	m_vsSkinned = newSkinnedVs;
	m_shadowVsSkinned = newShadowSkinnedVs;
	m_vs = newVs;
	m_ps = newPs;
	m_shadowVs = newShadowVs;
	m_skyVs = newSkyVs;
	m_skyPs = newSkyPs;

	// 핫리로드 감시 목록: 소스 트리의 원본 여섯 개(VS·PS·ShadowVS·SkyboxVS·SkyboxPS·Common.hlsli). Release에서도 목록은 만들지만 CheckHotReload가 동작하지 않음.
	m_watched.clear();
	for (const wchar_t* file : { kVertexShaderFile, kPixelShaderFile, kShadowShaderFile, kSkyVertexShaderFile, kSkyPixelShaderFile, kSkinnedVertexShaderFile, kShadowSkinnedShaderFile, kCommonShaderFile })
	{
		WatchedFile watched;
		watched.path = Paths::GetShaderSourcePath(file);
		watched.writeTime = ShaderCompiler::GetLastWriteTime(watched.path);
		m_watched.push_back(watched);
	}

	Log::Info("셰이더 로드: VS %s, PS %s, ShadowVS %s, SkyboxVS %s, SkyboxPS %s", Log::ToUtf8(vsSource.c_str()).c_str(), Log::ToUtf8(psSource.c_str()).c_str(),
		Log::ToUtf8(shadowSource.c_str()).c_str(), Log::ToUtf8(skyVsSource.c_str()).c_str(), Log::ToUtf8(skyPsSource.c_str()).c_str());
	return true;
}

bool Renderer::ValidateBindings(const std::vector<uint8_t>& vsCode, const std::vector<uint8_t>& psCode, const std::vector<uint8_t>& shadowVsCode,
	const std::vector<uint8_t>& skyVsCode, const std::vector<uint8_t>& skyPsCode, const std::vector<uint8_t>& skinnedVsCode,
	const std::vector<uint8_t>& shadowSkinnedVsCode) const
{
	// 선언: 세 레이아웃의 슬롯 합집합. Renderer가 만든 Desc 내용을 그대로 다시 적음.
	struct Declared { BindingType type; uint8_t reg; uint8_t stageMask; };
	const Declared declared[] =
	{
		{ BindingType::ConstantBuffer, kCBSlotPerFrame,     ShaderStageMask_Vertex | ShaderStageMask_Pixel },
		{ BindingType::ConstantBuffer, kCBSlotLights,       ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotShadowMap,   ShaderStageMask_Pixel },
		{ BindingType::Sampler,        kSamplerSlotShadow,  ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotEnvironment, ShaderStageMask_Pixel },        // 2026-10-08
		{ BindingType::Sampler,        kSamplerSlotEnvironment, ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotIrradiance,  ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotPrefiltered, ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotBrdfLut,     ShaderStageMask_Pixel },
		{ BindingType::ConstantBuffer, kCBSlotPerObject,    ShaderStageMask_Vertex },
		{ BindingType::ConstantBuffer, kCBSlotBones,        ShaderStageMask_Vertex },   // Nereides: 스키닝
		{ BindingType::ConstantBuffer, kCBSlotMaterial,     ShaderStageMask_Vertex | ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotAlbedo,      ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotNormal,      ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotMetallicRoughness, ShaderStageMask_Pixel },   // 2026-10-08 PBR
		{ BindingType::ShaderResource, kSRVSlotOcclusion,   ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotEmissive,    ShaderStageMask_Pixel },
		{ BindingType::Sampler,        kSamplerSlotMaterial, ShaderStageMask_Pixel },
	};

	bool ok = true;
	auto check = [&](const std::vector<uint8_t>& code, uint8_t stage, const char* stageName)
	{
		std::vector<ShaderCompiler::ReflectedBinding> used;
		if (!ShaderCompiler::ReflectBindings(code, used)) return;   // 리플렉션이 불가능하면 검사 생략
		for (const auto& u : used)
		{
			bool found = false;
			for (const Declared& d : declared)
			{
				if (d.type == u.type && d.reg == u.reg && (d.stageMask & stage)) { found = true; break; }
			}
			const char* typeName = u.type == BindingType::ConstantBuffer ? "b" : (u.type == BindingType::ShaderResource ? "t" : "s");
			if (!found)
			{
				Log::Error("바인딩 대조: %s가 %s%u ('%s')를 쓰지만 BindingLayout에 그 스테이지로 선언되지 않음.",
					stageName, typeName, u.reg, u.name.c_str());
				ok = false;
			}
			else
			{
				Log::Info("바인딩 대조: %s %s%u ('%s') OK", stageName, typeName, u.reg, u.name.c_str());
			}
		}
	};
	check(vsCode, ShaderStageMask_Vertex, "VS");
	check(psCode, ShaderStageMask_Pixel, "PS");
	check(shadowVsCode, ShaderStageMask_Vertex, "ShadowVS");
	// 스카이박스 PSO 는 프레임 레이아웃만 씀. 위 목록은 세 레이아웃의 합집합이라 재질 슬롯을 써도 통과하지만, 셰이더가 실제로 쓰는 것은 b0·t9·s5 뿐임.
	check(skyVsCode, ShaderStageMask_Vertex, "SkyboxVS");
	check(skyPsCode, ShaderStageMask_Pixel, "SkyboxPS");
	check(skinnedVsCode, ShaderStageMask_Vertex, "SkinnedVS");
	check(shadowSkinnedVsCode, ShaderStageMask_Vertex, "ShadowSkinnedVS");
	return ok;
}

bool Renderer::ReloadShaders()
{
	if (m_device == nullptr) return false;

	Log::Info("셰이더 핫리로드 시작…");
	if (!LoadShaders(true))
	{
		Log::Warn("셰이더 핫리로드 실패. 이전 셰이더를 유지한다.");
		// 실패해도 수정 시각은 갱신해 같은 파일로 계속 재시도하지 않게 함. 파일을 다시 저장하면 재시도함.
		for (WatchedFile& w : m_watched) w.writeTime = ShaderCompiler::GetLastWriteTime(w.path);
		return false;
	}

	// 새 셰이더 핸들로 PSO를 다시 만들어야 함. 캐시를 비우면 이전 세대 핸들은 무효가 되고,
	// 다음 프레임의 GetPipelineFor가 새 Desc(새 vs/ps 핸들)로 새 PSO를 만듦.
	m_baseDesc.vs = m_vs;
	m_baseDesc.ps = m_ps;
	m_shadowDesc.vs = m_shadowVs;
	m_skyDesc.vs = m_skyVs;
	m_skyDesc.ps = m_skyPs;
	m_device->InvalidatePipelines();
	++m_stats.shaderReloads;
	Log::Info("셰이더 핫리로드 완료 (%u회째). PSO 캐시 재생성.", m_stats.shaderReloads);
	return true;
}

void Renderer::CheckHotReload()
{
#if defined(_DEBUG)
	// 벽시계로 폴링함: 게임 시간(정지·배속)이나 프레임 시간과 무관하게 일정 간격으로 파일을 확인함.
	const uint64_t now = GetTickCount64();
	if (now < m_nextWatchTick) return;
	m_nextWatchTick = now + kHotReloadPollIntervalMs;

	bool changed = false;
	for (const WatchedFile& w : m_watched)
	{
		const uint64_t writeTime = ShaderCompiler::GetLastWriteTime(w.path);
		if (writeTime != 0 && writeTime != w.writeTime) { changed = true; break; }
	}
	if (changed)
	{
		ReloadShaders();
	}
#endif
}

void Renderer::Shutdown()
{
	if (m_device == nullptr) return;

	for (auto& entry : m_gpuMeshes)
	{
		m_device->DestroyBuffer(entry.second.vertexBuffer);
		m_device->DestroyBuffer(entry.second.indexBuffer);
	}
	m_gpuMeshes.clear();
	for (auto& entry : m_gpuMaterials)
	{
		m_device->DestroyResourceSet(entry.second.set);
		m_device->DestroyBuffer(entry.second.constants);
	}
	m_gpuMaterials.clear();
	for (auto& entry : m_textureCache)
	{
		if (entry.second != m_whiteTexture) m_device->DestroyTexture(entry.second);
	}
	m_textureCache.clear();
	m_device->DestroyTexture(m_whiteTexture);
	m_whiteTexture = TextureHandle{};
	m_flatNormalTexture = TextureHandle{};
	// 2026-10-08: 큐브맵. 로드 실패 항목은 검은 큐브 핸들을 공유하므로 따로 한 번만 지움.
	for (auto& entry : m_cubeCache)
	{
		if (entry.second.handle != m_blackCube) m_device->DestroyTexture(entry.second.handle);
	}
	m_cubeCache.clear();
	m_device->DestroyTexture(m_blackCube);
	m_blackCube = TextureHandle{};
	m_environment = m_boundEnvironment = EnvironmentBinding{};
	SetSceneTarget(0, 0);   // 11단계
	m_device->DestroyTexture(m_shadowMap);
	m_device->DestroySampler(m_shadowSampler);
	m_shadowMap = TextureHandle{};
	m_shadowSampler = SamplerHandle{};
	for (SamplerHandle& sampler : m_samplers)
	{
		m_device->DestroySampler(sampler);
		sampler = SamplerHandle{};
	}

	m_device->DestroyResourceSet(m_frameSet);
	m_device->DestroyResourceSet(m_objectSet);
	m_device->DestroyBindingLayout(m_frameLayout);
	m_device->DestroyBindingLayout(m_objectLayout);
	m_device->DestroyBindingLayout(m_materialLayout);
	m_device->DestroyBuffer(m_perFrameCB);
	m_device->DestroyBuffer(m_perObjectCB);
	m_device->DestroyBuffer(m_boneCB);
	m_device->DestroyBuffer(m_lightCB);
	m_device->DestroyShader(m_vsSkinned);
	m_device->DestroyShader(m_shadowVsSkinned);
	m_device->DestroyShader(m_vs);
	m_device->DestroyShader(m_ps);
	m_device->DestroyShader(m_shadowVs);
	m_device->DestroyShader(m_skyVs);
	m_device->DestroyShader(m_skyPs);
	// PSO는 캐시가 소유함. 셰이더 핸들을 지워도 PSO는 자기 참조로 셰이더 객체를 붙들고 있음.

	m_perFrameCB = m_perObjectCB = m_boneCB = m_lightCB = BufferHandle{};
	m_frameSet = m_objectSet = ResourceSetHandle{};
	m_frameLayout = m_objectLayout = m_materialLayout = BindingLayoutHandle{};
	m_vs = m_vsSkinned = m_ps = m_shadowVs = m_shadowVsSkinned = m_skyVs = m_skyPs = ShaderHandle{};
	m_device = nullptr;
}

size_t Renderer::GetPipelineCount() const
{
	return m_device ? m_device->GetPipelineCount() : 0;
}

void Renderer::InvalidateMesh(const nereides::MeshData* mesh)
{
	auto found = m_gpuMeshes.find(mesh);
	if (found == m_gpuMeshes.end()) return;
	m_device->DestroyBuffer(found->second.vertexBuffer);
	m_device->DestroyBuffer(found->second.indexBuffer);
	m_gpuMeshes.erase(found);
}

void Renderer::InvalidateMaterial(const Material* material)
{
	auto found = m_gpuMaterials.find(material);
	if (found == m_gpuMaterials.end()) return;
	m_device->DestroyResourceSet(found->second.set);
	m_device->DestroyBuffer(found->second.constants);
	m_gpuMaterials.erase(found);
}

void Renderer::PruneCaches()
{
	// 프레임이 지나면 MeshData / Material 이 파괴될 수 있음 (모델 교체, 컴포넌트 삭제, tint 변경으로 재질 교체).
	// 파괴된 객체의 GPU 자원을 버림. 텍스처는 여러 재질이 공유하므로 여기서 버리지 않음.
	for (auto it = m_gpuMeshes.begin(); it != m_gpuMeshes.end();)
	{
		if (!it->second.source.expired()) { ++it; continue; }
		m_device->DestroyBuffer(it->second.vertexBuffer);
		m_device->DestroyBuffer(it->second.indexBuffer);
		it = m_gpuMeshes.erase(it);
	}
	for (auto it = m_gpuMaterials.begin(); it != m_gpuMaterials.end();)
	{
		if (!it->second.source.expired()) { ++it; continue; }
		m_device->DestroyResourceSet(it->second.set);
		m_device->DestroyBuffer(it->second.constants);
		it = m_gpuMaterials.erase(it);
	}
}

// ------------------------------------------------------------------ 프레임

XMMATRIX Renderer::ComputeLightViewProj(const LightData& light) const
{
	// 방향광은 위치가 없음. 씬 중심에서 빛의 반대 방향으로 distance 만큼 물러난 곳에 눈을 두고,
	// 직교 투영으로 orthoSize × orthoSize 영역을 봄. 씬 경계를 알게 되면 그 AABB 에 맞추는 것이 다음 단계임.
	XMVECTOR direction = XMLoadFloat3(&light.direction);
	if (XMVectorGetX(XMVector3LengthSq(direction)) < 0.000001f)
	{
		direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	}
	direction = XMVector3Normalize(direction);

	const XMVECTOR center = XMVectorSet(m_settings.shadowCenter.x, m_settings.shadowCenter.y, m_settings.shadowCenter.z, 1.0f);
	const XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(direction, m_settings.shadowDistance));
	// 빛이 거의 수직이면 up = +Y 가 시선과 평행해져 LookAt 이 깨짐. 그때는 +Z 를 up 으로 씀.
	const XMVECTOR up = (fabsf(XMVectorGetY(direction)) > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const XMMATRIX view = XMMatrixLookAtLH(eye, center, up);
	const float size = m_settings.shadowOrthoSize;
	const XMMATRIX proj = XMMatrixOrthographicLH(size, size, 0.1f, m_settings.shadowDistance * 2.0f + size);
	return view * proj;
}

void Renderer::Render(const nereides::RenderFrame& frame)
{
	if (m_device == nullptr) return;

	CheckHotReload();
	PruneCaches();
	++m_frameNumber;
	const uint32_t reloads = m_stats.shaderReloads;
	const uint32_t lastPasses = m_stats.renderPasses;   // 2026-10-15: 패스·배리어 수는 UI 패스까지 센 지난 프레임 값 (EndUIPass 가 채움)
	const uint32_t lastBarriers = m_stats.barriers;
	m_stats = Stats{};
	m_stats.shaderReloads = reloads;
	m_stats.renderPasses = lastPasses;
	m_stats.barriers = lastBarriers;

	RHI::CommandList& cmd = m_device->GetCommandList();

	// ---- 광원 0 (방향광) 의 그림자 행렬 ----
	const std::vector<LightData>& sceneLights = frame.lights;
	m_shadowEnabledThisFrame = m_settings.shadows && !sceneLights.empty()
		&& sceneLights[0].type == static_cast<uint32_t>(LightType::Directional);
	const XMMATRIX lightViewProj = m_shadowEnabledThisFrame ? ComputeLightViewProj(sceneLights[0]) : XMMatrixIdentity();
	XMStoreFloat4x4(&m_lightViewProj, lightViewProj);

	// ---- 2026-10-08: 환경 (스카이박스 t9, IBL t10~t12, 노출·톤매핑). b0 의 environmentParams·outputParams 가 이 값을 씀 ----
	ResolveEnvironment(frame.environment);

	// ---- b0 PerFrame ----
	UploadPerFrameConstants(frame);

	// ---- b2 Lights ----
	LightConstants lights = {};
	lights.lightCount = static_cast<uint32_t>(sceneLights.size() < MAX_LIGHTS ? sceneLights.size() : MAX_LIGHTS);
	if (sceneLights.size() > MAX_LIGHTS && !m_warnedLightLimit)
	{
		Log::Warn("Renderer : 조명 %zu개 중 처음 %u개만 사용함.", sceneLights.size(), MAX_LIGHTS);
		m_warnedLightLimit = true;
	}
	lights.ambientColor = frame.environment.ambientColor;
	for (uint32_t i = 0; i < lights.lightCount; ++i)
	{
		lights.lights[i] = sceneLights[i];

		// 셰이더는 정규화된 방향을 기대함. 0벡터면 아래를 향하게 함.
		const XMVECTOR direction = XMLoadFloat3(&lights.lights[i].direction);
		const float lengthSquared = XMVectorGetX(XMVector3LengthSq(direction));
		if (lengthSquared > 0.000001f)
		{
			XMStoreFloat3(&lights.lights[i].direction, XMVector3Normalize(direction));
		}
		else
		{
			lights.lights[i].direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
		}
	}
	m_device->UpdateBuffer(m_lightCB, &lights, sizeof(lights));

	BuildDrawList(frame);

	// 2026-10-08: 환경 텍스처를 프레임 셋 t9~t12 에 꽂음. 없는 것도 슬롯을 비우지 않고 검은 큐브·흰 LUT 를 꽂음
	// (D3D12 디스크립터 테이블에는 유효한 뷰가 있어야 함. 셰이더는 environmentParams·outputParams 의 플래그로 쓸지 정함).
	UpdateFrameSet();

	// 프레임 셋(b0, b2, t8, s4, t9, s5, t10~t12)과 오브젝트 셋(b1, b4)을 바인딩함. 그림자 패스는 t8(그림자 맵)을 DSV 로 쓰므로
	// BeginRenderPass 가 t8 SRV 바인딩을 해제함. 메인 패스에서 셋을 다시 바인딩해 t8 을 복구함.
	cmd.SetResourceSet(m_frameSet);
	cmd.SetResourceSet(m_objectSet);

	if (m_shadowEnabledThisFrame)
	{
		RenderShadowPass(cmd);
	}
	RenderMainPass(cmd, frame);
}

void Renderer::BuildDrawList(const nereides::RenderFrame& frame)
{
	m_drawList.clear();
	for (const nereides::DrawItem& source : frame.items)
	{
		if (!source.mesh) continue;
		const GpuMesh* gpuMesh = GetOrCreateGpuMesh(source.mesh);   // 여기서 만들어 두면 이후 패스는 캐시 조회만 함
		if (gpuMesh == nullptr) continue;

		const Material* material = source.material ? source.material.get() : m_defaultMaterial.get();
		// 재질 GPU 자원은 아래 DrawItems 에서 처음 쓸 때 만듦. weak_ptr 는 그때 기록되므로 소유권을 지금 넘겨 둠.
		DrawItem item;
		item.pipeline = GetPipelineFor(*material, gpuMesh->vertexFormat);
		item.material = material;
		item.mesh = source.mesh.get();
		item.source = &source;
		item.vertexFormat = gpuMesh->vertexFormat;
		if (!item.pipeline.IsValid()) continue;
		m_drawList.push_back(item);

		// 이 재질을 처음 보는 경우 소유권(shared_ptr)과 함께 캐시에 올림 (기본 재질도 같은 경로).
		const std::shared_ptr<const Material>& owner = source.material ? source.material : m_defaultMaterial;
		auto found = m_gpuMaterials.find(material);
		if (found == m_gpuMaterials.end() || !SameOwner(found->second.source, owner))
		{
			if (found != m_gpuMaterials.end())
			{
				m_device->DestroyResourceSet(found->second.set);
				m_device->DestroyBuffer(found->second.constants);
				m_gpuMaterials.erase(found);
			}
			GpuMaterial created;
			created.source = owner;
			m_gpuMaterials.emplace(material, created);
		}
	}

	// ---- 정렬: PSO → Material → Mesh. 같은 상태가 연속되면 바인딩을 건너뛸 수 있음 ----
	std::stable_sort(m_drawList.begin(), m_drawList.end(), [](const DrawItem& a, const DrawItem& b)
	{
		if (a.pipeline.index != b.pipeline.index) return a.pipeline.index < b.pipeline.index;
		if (a.material != b.material) return a.material < b.material;
		return a.mesh < b.mesh;
	});
}

void Renderer::UploadObjectConstants(const nereides::DrawItem& item)
{
	// b1 PerObject. 드로우마다 Map(WRITE_DISCARD). 드라이버가 매번 새 메모리를 주므로
	// 앞 드로우가 아직 실행되지 않았어도 그 데이터를 덮어쓰지 않음. 바인딩은 프레임 초에 한 번만 했음.
	const XMMATRIX world = XMLoadFloat4x4(&item.world);
	PerObjectConstants perObject = {};
	XMStoreFloat4x4(&perObject.world, XMMatrixTranspose(world));
	// 노멀 변환 행렬은 (W⁻¹)ᵀ. 전치해서 올려야 하므로 ((W⁻¹)ᵀ)ᵀ = W⁻¹ 를 그대로 저장함.
	XMStoreFloat4x4(&perObject.worldInvTranspose, XMMatrixInverse(nullptr, world));
	m_device->UpdateBuffer(m_perObjectCB, &perObject, sizeof(perObject));

	if (!item.bones.empty())
	{
		// b4 뼈 팔레트. 월드 행렬처럼 전치해서 올림. 로더가 127개 + 대체 1개로 제한하지만 상수버퍼 크기를 넘지 않게 한 번 더 막음.
		BoneConstants bones = {};
		const size_t count = (std::min)(item.bones.size(), static_cast<size_t>(kMaxBones));
		for (size_t i = 0; i < count; ++i)
		{
			XMStoreFloat4x4(&bones.bones[i], XMMatrixTranspose(XMLoadFloat4x4(&item.bones[i])));
		}
		m_device->UpdateBuffer(m_boneCB, &bones, sizeof(bones));
	}
}

void Renderer::RenderShadowPass(RHI::CommandList& cmd)
{
	// 그림자 맵: ShaderResource(또는 Common) → DepthWrite. D3D11 에서는 상태 추적만 하고, D3D12 에서는 실제 배리어를 기록함.
	cmd.Barrier(m_shadowMap, m_shadowMapState, ResourceState::DepthWrite);
	m_shadowMapState = ResourceState::DepthWrite;

	// RenderPassDesc 도 지정 초기화 — Vulkan 의 VkRenderingInfo 나 D3D12 의 BeginRenderPass 인자와 같은 모양이 됨.
	const RenderPassDesc pass{
		.colorCount = 0,
		.depth = { .texture = m_shadowMap, .load = LoadOp::Clear, .clearDepth = 1.0f },
		.debugName = "ShadowPass",
	};
	cmd.BeginRenderPass(pass);   // 뷰포트는 2048² (attachment 크기)

	PipelineHandle lastPipeline;
	const nereides::MeshData* lastMesh = nullptr;
	for (const DrawItem& item : m_drawList)
	{
		if (item.material->unlit) continue;   // 감마 검증 카드처럼 조명을 받지 않는 물체는 그림자도 만들지 않음

		const PipelineHandle pipeline = GetShadowPipelineFor(*item.material, item.vertexFormat);
		if (!pipeline.IsValid()) continue;
		if (pipeline != lastPipeline)
		{
			cmd.SetPipelineState(pipeline);
			lastPipeline = pipeline;
			++m_stats.pipelineSwitches;
		}
		const auto gpuMeshEntry = m_gpuMeshes.find(item.mesh);
		if (gpuMeshEntry == m_gpuMeshes.end()) continue;
		const GpuMesh* gpuMesh = &gpuMeshEntry->second;
		if (item.mesh != lastMesh)
		{
			cmd.SetVertexBuffer(gpuMesh->vertexBuffer);
			cmd.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat);
			lastMesh = item.mesh;
			++m_stats.meshSwitches;
		}
		UploadObjectConstants(*item.source);
		cmd.DrawIndexed(gpuMesh->indexCount, 0, 0);
		++m_stats.shadowDraws;
	}

	cmd.EndRenderPass();

	// DepthWrite → ShaderResource. 메인 패스가 t8 로 읽음.
	cmd.Barrier(m_shadowMap, ResourceState::DepthWrite, ResourceState::ShaderResource);
	m_shadowMapState = ResourceState::ShaderResource;
}

void Renderer::RenderMainPass(RHI::CommandList& cmd, const nereides::RenderFrame& frame)
{
	// 11단계: 에디터가 씬 뷰 텍스처를 요청했으면 거기에 그리고, 아니면 백버퍼에 직접 그림.
	TextureHandle target;
	TextureHandle depth;
	if (IsOffscreen())
	{
		target = m_sceneColor;
		depth = m_sceneDepth;
		cmd.Barrier(m_sceneColor, m_sceneColorState, ResourceState::RenderTarget);
		m_sceneColorState = ResourceState::RenderTarget;
	}
	else
	{
		target = m_device->GetBackBuffer();
		depth = m_device->GetDepthBuffer();
		// 백버퍼: Present → RenderTarget. (D3D12 스왑체인 관례. D3D11 에서는 상태 추적만 함.)
		cmd.Barrier(target, ResourceState::Present, ResourceState::RenderTarget);
	}

	const RenderPassDesc pass{
		.colors = { {
			.texture = target,
			.load = LoadOp::Clear,
			.srgbView = m_settings.srgbOutput,   // 클리어 색은 선형 값. sRGB 뷰가 인코딩함
			.clearColor = { frame.clearColor[0], frame.clearColor[1], frame.clearColor[2], frame.clearColor[3] },
		} },
		.colorCount = 1,
		.depth = { .texture = depth, .load = LoadOp::Clear },
		.debugName = "MainPass",
	};
	cmd.BeginRenderPass(pass);
	DrawItems(cmd, frame);
	cmd.EndRenderPass();

	if (IsOffscreen())
	{
		// ImGui 가 이 텍스처를 샘플링함. D3D12 에서는 실제 배리어를 기록하고, D3D11 에서는 상태만 추적함.
		cmd.Barrier(m_sceneColor, ResourceState::RenderTarget, ResourceState::ShaderResource);
		m_sceneColorState = ResourceState::ShaderResource;
	}
}

void Renderer::DrawItems(RHI::CommandList& cmd, const nereides::RenderFrame& frame)
{
	// 그림자 패스에서 t8 바인딩이 해제됐으므로 프레임 셋을 다시 바인딩함 (이제 그림자 맵은 SRV 상태). 패스마다 새로 바인딩함.
	cmd.SetResourceSet(m_frameSet);
	cmd.SetResourceSet(m_objectSet);

	// ---- 드로우. "직전" 상태 기억은 이 패스 안에서만 유효함 ----
	PipelineHandle lastPipeline;
	const Material* lastMaterial = nullptr;
	const nereides::MeshData* lastMesh = nullptr;

	for (const DrawItem& item : m_drawList)
	{
		if (item.pipeline != lastPipeline)
		{
			cmd.SetPipelineState(item.pipeline);
			lastPipeline = item.pipeline;
			++m_stats.pipelineSwitches;
		}

		if (item.material != lastMaterial)
		{
			const GpuMaterial* gpuMaterial = GetOrCreateGpuMaterial(*item.material);
			if (gpuMaterial == nullptr) continue;
			cmd.SetResourceSet(gpuMaterial->set);
			lastMaterial = item.material;
			++m_stats.resourceSetSwitches;
		}

		const auto gpuMeshEntry = m_gpuMeshes.find(item.mesh);
		if (gpuMeshEntry == m_gpuMeshes.end()) continue;
		const GpuMesh* gpuMesh = &gpuMeshEntry->second;
		if (item.mesh != lastMesh)
		{
			cmd.SetVertexBuffer(gpuMesh->vertexBuffer);
			cmd.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat);
			lastMesh = item.mesh;
			++m_stats.meshSwitches;
		}

		UploadObjectConstants(*item.source);
		cmd.DrawIndexed(gpuMesh->indexCount, 0, 0);
		m_stats.triangles += gpuMesh->indexCount / 3;
		++m_stats.draws;
	}

	// 2026-10-08: 불투명 물체를 다 그린 뒤 하늘. 깊이 버퍼가 채워져 있으므로 가려진 픽셀은 깊이 테스트에서 떨어짐.
	if (!frame.environment.skybox.empty())
	{
		DrawSkybox(cmd);
	}
}

void Renderer::DrawSkybox(RHI::CommandList& cmd)
{
	const PipelineHandle pipeline = GetSkyPipeline();
	if (!pipeline.IsValid()) return;
	cmd.SetPipelineState(pipeline);
	++m_stats.pipelineSwitches;
	// 프레임 셋(b0 view/proj, t9 큐브, s5)은 DrawItems 앞에서 이미 바인딩됨. 정점 버퍼는 쓰지 않음 — VS 가 SV_VertexID 0, 1, 2 로 삼각형을 만듦.
	cmd.Draw(3, 0);
	++m_stats.draws;
	m_stats.triangles += 1;
}

void Renderer::UploadPerFrameConstants(const nereides::RenderFrame& frame)
{
	// HLSL은 column_major로 읽고 셰이더는 mul(v, M)을 쓰므로 전치해서 올림 (ShaderConstants.h).
	const XMMATRIX view = XMLoadFloat4x4(&frame.view);
	const XMMATRIX proj = XMLoadFloat4x4(&frame.projection);
	PerFrameConstants perFrame = {};
	XMStoreFloat4x4(&perFrame.view, XMMatrixTranspose(view));
	XMStoreFloat4x4(&perFrame.proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&perFrame.viewProj, XMMatrixTranspose(view * proj));
	XMStoreFloat4x4(&perFrame.lightViewProj, XMMatrixTranspose(XMLoadFloat4x4(&m_lightViewProj)));
	// 카메라 위치 = 뷰 행렬의 역행렬의 이동 성분. 에디터가 view 를 덮어써도 항상 화면과 맞음.
	XMStoreFloat3(&perFrame.cameraPosition, XMMatrixInverse(nullptr, view).r[3]);
	perFrame.time = static_cast<float>(frame.time);
	perFrame.shadowParams = XMFLOAT4(1.0f / kShadowMapSize, m_settings.shadowBias, m_settings.shadowStrength, m_shadowEnabledThisFrame ? 1.0f : 0.0f);
	perFrame.debugParams = XMFLOAT4(static_cast<float>(m_settings.debugView), m_settings.debugDepthRange, 0.0f, 0.0f);
	// 2026-10-08: ResolveEnvironment 가 이번 프레임에 정한 환경 (Render 맨 앞에서 불림)
	perFrame.environmentParams = XMFLOAT4(m_environment.iblIntensity, static_cast<float>(m_environment.specularMips),
		m_environment.irradianceLod, m_environment.hasEnvironment ? 1.0f : 0.0f);
	perFrame.outputParams = XMFLOAT4(m_environment.exposure, m_environment.toneMapping ? 1.0f : 0.0f, m_environment.hasBrdfLut ? 1.0f : 0.0f, 0.0f);
	m_device->UpdateBuffer(m_perFrameCB, &perFrame, sizeof(perFrame));
}

void Renderer::BeginUIPass()
{
	if (m_device == nullptr) return;
	// ImGui 는 sRGB 로 인코딩된 색을 그대로 출력하므로 UNORM 뷰에 그림. 씬 위에 덧그리므로 Load, 깊이 없음(depth 기본값 = 빈 핸들).
	RenderPassDesc pass{
		.colors = { { .texture = m_device->GetBackBuffer(), .load = LoadOp::Load, .srgbView = false } },
		.colorCount = 1,
		.debugName = "UIPass",
	};
	if (IsOffscreen())
	{
		// 씬이 백버퍼에 그려지지 않았으므로 여기서 전이하고 지움 (에디터 배경).
		m_device->GetCommandList().Barrier(m_device->GetBackBuffer(), ResourceState::Present, ResourceState::RenderTarget);
		pass.colors[0].load = LoadOp::Clear;
		pass.colors[0].clearColor[0] = 0.13f; pass.colors[0].clearColor[1] = 0.13f; pass.colors[0].clearColor[2] = 0.15f; pass.colors[0].clearColor[3] = 1.0f;
	}
	m_device->GetCommandList().BeginRenderPass(pass);
}

void Renderer::EndUIPass()
{
	if (m_device == nullptr) return;
	RHI::CommandList& cmd = m_device->GetCommandList();
	cmd.EndRenderPass();
	// RenderTarget → Present. Present 직전의 마지막 전이.
	cmd.Barrier(m_device->GetBackBuffer(), ResourceState::RenderTarget, ResourceState::Present);
	// 2026-10-15: UI 패스와 마지막 Present 전이까지 센 프레임 전체 값. 패널은 UI 패스 안에서 그려 이 값이 한 프레임 늦게 보임.
	m_stats.renderPasses = cmd.GetStats().renderPasses;
	m_stats.barriers = cmd.GetStats().barriers;
}

// ------------------------------------------------------------------ GPU 캐시

const Renderer::GpuMesh* Renderer::GetOrCreateGpuMesh(const std::shared_ptr<const nereides::MeshData>& mesh)
{
	auto found = m_gpuMeshes.find(mesh.get());
	if (found != m_gpuMeshes.end())
	{
		if (SameOwner(found->second.source, mesh)) return &found->second;
		// 같은 주소에 다른 객체가 만들어짐: 이전 항목을 버리고 다시 올림.
		m_device->DestroyBuffer(found->second.vertexBuffer);
		m_device->DestroyBuffer(found->second.indexBuffer);
		m_gpuMeshes.erase(found);
	}
	if (mesh->vertices.empty() || mesh->indices.empty())
	{
		return nullptr;
	}
	const size_t vertexCount = mesh->vertices.size();
	for (const unsigned index : mesh->indices)
	{
		if (index >= vertexCount)
		{
			Log::Error("GpuMesh : 인덱스 %u 가 정점 수 %zu 를 넘음. 그리지 않음.", index, vertexCount);
			return nullptr;
		}
	}

	// 가중치가 하나라도 있으면 스키닝 정점 형식. 가중치 없는 정점은 셰이더가 합이 0인 것으로 알아보고 그대로 둠.
	bool skinned = false;
	for (const nereides::MeshVertex& v : mesh->vertices)
	{
		if (v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3] > 0.0f) { skinned = true; break; }
	}

	// 정점/인덱스 버퍼는 바뀌지 않으므로 Default 버퍼에 초기 데이터를 넣어 만듦.
	GpuMesh gpuMesh;
	gpuMesh.source = mesh;
	gpuMesh.vertexFormat = skinned ? VertexFormat::PositionColorNormalTexcoordTangentSkinned : VertexFormat::PositionColorNormalTexcoordTangent;

	std::vector<VERTEX_SKINNED> packed(vertexCount);   // 스키닝이 아니어도 VERTEX 부분을 채워 같은 코드로 복사함
	for (size_t i = 0; i < vertexCount; ++i)
	{
		const nereides::MeshVertex& v = mesh->vertices[i];
		VERTEX_SKINNED& out = packed[i];
		out.base = { v.position.x, v.position.y, v.position.z, v.color.x, v.color.y, v.color.z, 1.0f,
			v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y, v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w };
		for (int b = 0; b < 4; ++b) { out.boneIds[b] = v.bones[b]; out.weights[b] = v.weights[b]; }
	}
	std::vector<VERTEX> staticVertices;
	if (!skinned)
	{
		staticVertices.resize(vertexCount);
		for (size_t i = 0; i < vertexCount; ++i) staticVertices[i] = packed[i].base;
	}

	const uint32_t stride = GetVertexStride(gpuMesh.vertexFormat);
	const BufferDesc vbDesc{
		.size = static_cast<uint32_t>(stride * vertexCount),
		.usage = BufferUsage::Default,
		.bindFlags = BufferBind_Vertex,
		.stride = stride,
		.debugName = "VB",
	};
	gpuMesh.vertexBuffer = m_device->CreateBuffer(vbDesc, skinned ? static_cast<const void*>(packed.data()) : static_cast<const void*>(staticVertices.data()));

	// 9단계 (D10): 정점이 65,536개 미만이면 16비트 인덱스. 버퍼 크기와 IA 대역폭이 절반으로 줄어듦. CPU 쪽은 32비트 그대로 둠.
	// size / stride 는 아래 분기에서 채우므로 여기서는 공통 필드만 지정함.
	BufferDesc ibDesc{ .usage = BufferUsage::Default, .bindFlags = BufferBind_Index, .debugName = "IB" };
	if (vertexCount <= 65535)
	{
		std::vector<uint16_t> indices16(mesh->indices.size());
		for (size_t i = 0; i < indices16.size(); ++i) indices16[i] = static_cast<uint16_t>(mesh->indices[i]);
		ibDesc.size = static_cast<uint32_t>(sizeof(uint16_t) * indices16.size());
		ibDesc.stride = sizeof(uint16_t);
		gpuMesh.indexFormat = Format::R16_UINT;
		gpuMesh.indexBuffer = m_device->CreateBuffer(ibDesc, indices16.data());
	}
	else
	{
		ibDesc.size = static_cast<uint32_t>(sizeof(uint32_t) * mesh->indices.size());
		ibDesc.stride = sizeof(uint32_t);
		gpuMesh.indexFormat = Format::R32_UINT;
		gpuMesh.indexBuffer = m_device->CreateBuffer(ibDesc, mesh->indices.data());
	}

	gpuMesh.indexCount = static_cast<uint32_t>(mesh->indices.size());

	if (!gpuMesh.vertexBuffer.IsValid() || !gpuMesh.indexBuffer.IsValid())
	{
		Log::Error("GpuMesh 생성 실패 (정점 %zu, 인덱스 %zu).", vertexCount, mesh->indices.size());
		m_device->DestroyBuffer(gpuMesh.vertexBuffer);
		m_device->DestroyBuffer(gpuMesh.indexBuffer);
		return nullptr;
	}

	return &m_gpuMeshes.emplace(mesh.get(), gpuMesh).first->second;
}

const Renderer::GpuMaterial* Renderer::GetOrCreateGpuMaterial(const Material& material)
{
	GpuMaterial* gpuMaterial = nullptr;
	auto found = m_gpuMaterials.find(&material);
	if (found != m_gpuMaterials.end() && found->second.constants.IsValid())
	{
		gpuMaterial = &found->second;
	}
	else
	{
		// BuildDrawList 가 소유권(weak_ptr)을 기록해 둔 항목이면 그것을 이어받음.
		GpuMaterial created;
		if (found != m_gpuMaterials.end()) created.source = found->second.source;

		const BufferDesc cbDesc{ .size = sizeof(MaterialConstants), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_Constant, .debugName = "CB_Material" };
		created.constants = m_device->CreateBuffer(cbDesc);
		if (!created.constants.IsValid())
		{
			Log::Error("GpuMaterial 상수버퍼 생성 실패 (%s).", material.name.c_str());
			return nullptr;
		}
		gpuMaterial = &(m_gpuMaterials[&material] = created);
	}

	// 재질이 가리키는 텍스처·샘플러가 바뀌었으면(ImGui 편집, 첫 생성) ResourceSet을 다시 만듦.
	// ResourceSet은 값 객체라 부분 갱신 기능이 없음 — D3D12 디스크립터 테이블도 다시 써야 함.
	const TextureHandle albedo = GetOrLoadTexture(material.albedoTexture, material.albedoSrgb);
	const TextureHandle normal = material.normalTexture.empty() ? m_flatNormalTexture : GetOrLoadTexture(material.normalTexture, false);
	// 2026-10-08 PBR: 빈 이름은 흰색 — 금속성/거칠기·AO·발광 모두 계수가 그대로 쓰임. 발광만 색이라 sRGB.
	const TextureHandle metallicRoughness = GetOrLoadTexture(material.metallicRoughnessTexture, false);
	const TextureHandle occlusion = GetOrLoadTexture(material.occlusionTexture, false);
	const TextureHandle emissive = GetOrLoadTexture(material.emissiveTexture, true);
	const SamplerPreset preset = (m_settings.samplerOverride >= 0 && m_settings.samplerOverride < static_cast<int>(kSamplerPresetCount))
		? static_cast<SamplerPreset>(m_settings.samplerOverride) : material.sampler;
	const SamplerHandle sampler = GetSampler(preset);
	if (!gpuMaterial->set.IsValid() || gpuMaterial->boundAlbedo != albedo || gpuMaterial->boundNormal != normal || gpuMaterial->boundSampler != sampler
		|| gpuMaterial->boundMetallicRoughness != metallicRoughness || gpuMaterial->boundOcclusion != occlusion || gpuMaterial->boundEmissive != emissive)
	{
		m_device->DestroyResourceSet(gpuMaterial->set);
		// 슬롯 순서는 Initialize 의 materialLayout.Add(...) 순서: b3 상수, t0 알베도, t1 노멀, t2 금속성/거칠기, t3 AO, t4 발광, s0 샘플러.
		const ResourceSetDesc setDesc{
			.layout = m_materialLayout,
			.bindings = {
				{ .buffer = gpuMaterial->constants }, { .texture = albedo }, { .texture = normal },
				{ .texture = metallicRoughness }, { .texture = occlusion }, { .texture = emissive }, { .sampler = sampler },
			},
			.debugName = "Set_Material",
		};
		gpuMaterial->set = m_device->CreateResourceSet(setDesc);
		gpuMaterial->boundAlbedo = albedo;
		gpuMaterial->boundNormal = normal;
		gpuMaterial->boundMetallicRoughness = metallicRoughness;
		gpuMaterial->boundOcclusion = occlusion;
		gpuMaterial->boundEmissive = emissive;
		gpuMaterial->boundSampler = sampler;
		if (!gpuMaterial->set.IsValid())
		{
			Log::Error("GpuMaterial ResourceSet 생성 실패 (%s).", material.name.c_str());
			return nullptr;
		}
	}

	// 재질 상수는 프레임에 한 번 올림. 편집(ImGui)이 바로 반영되도록 매 프레임 올리되,
	// 같은 재질을 여러 오브젝트가 쓰면 그중 첫 드로우에서만 올림.
	if (gpuMaterial->uploadedFrame != m_frameNumber)
	{
		MaterialConstants constants = {};
		constants.baseColor = material.baseColor;
		constants.emissive = material.emissive;
		constants.metallic = material.metallic;
		constants.roughness = material.roughness;
		constants.occlusionStrength = material.occlusionStrength;
		constants.uvScale = material.uvScale;
		constants.unlit = material.unlit ? 1.0f : 0.0f;
		constants.normalStrength = m_settings.normalMapping ? material.normalStrength : 0.0f;
		constants.alphaCutoff = material.alphaCutoff;
		m_device->UpdateBuffer(gpuMaterial->constants, &constants, sizeof(constants));
		gpuMaterial->uploadedFrame = m_frameNumber;
	}
	return gpuMaterial;
}

PipelineHandle Renderer::GetPipelineFor(const Material& material, VertexFormat vertexFormat)
{
	PipelineStateDesc desc = m_baseDesc;
	if (vertexFormat == VertexFormat::PositionColorNormalTexcoordTangentSkinned) desc.vs = m_vsSkinned;
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.fill = (m_settings.wireframe || material.wireframe) ? FillMode::Wireframe : FillMode::Solid;
	desc.rasterizer.cull = (m_settings.cullBack && !material.doubleSided) ? CullMode::Back : CullMode::None;
	// 8단계: D3D12 PSO 는 렌더 타깃 뷰의 포맷을 알아야 함. 메인 패스가 백버퍼의 sRGB 뷰에 그리면 PSO 도 sRGB 포맷이어야 함.
	// D3D11 은 이 항목을 쓰지 않지만 1단계부터 Desc 에 있었고, 여기서 처음 실제 값이 들어감.
	desc.rtvFormats[0] = m_settings.srgbOutput ? Format::R8G8B8A8_UNORM_SRGB : Format::R8G8B8A8_UNORM;
	desc.rtvCount = 1;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	return m_device->CreatePipeline(desc);
}

PipelineHandle Renderer::GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat)
{
	// 그림자는 항상 솔리드로 그림 (와이어프레임 재질도 실체는 있음). 양면 재질은 컬링 없이 그림.
	PipelineStateDesc desc = m_shadowDesc;
	if (vertexFormat == VertexFormat::PositionColorNormalTexcoordTangentSkinned) desc.vs = m_shadowVsSkinned;
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.cull = material.doubleSided ? CullMode::None : CullMode::Back;
	return m_device->CreatePipeline(desc);
}

PipelineHandle Renderer::GetSkyPipeline()
{
	// 메인 패스와 같은 타깃에 그리므로 RTV/DSV 포맷도 메인 PSO 와 같게 맞춤 (D3D12 PSO 는 이 값이 다르면 그릴 수 없음).
	// 와이어프레임 설정은 따르지 않음 — 화면을 덮는 삼각형 하나의 모서리만 보여 의미가 없음.
	PipelineStateDesc desc = m_skyDesc;
	desc.rtvFormats[0] = m_settings.srgbOutput ? Format::R8G8B8A8_UNORM_SRGB : Format::R8G8B8A8_UNORM;
	desc.rtvCount = 1;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	return m_device->CreatePipeline(desc);
}

// ------------------------------------------------------------------ 11단계: 오프스크린 씬 뷰

void Renderer::SetSceneTarget(uint32_t width, uint32_t height)
{
	if (m_device == nullptr) return;
	if (width == m_sceneWidth && height == m_sceneHeight) return;

	// 이전 텍스처는 Destroy 함 — D3D12 는 지연 해제(실행 중인 프레임의 ImGui 드로우가 아직 읽을 수 있음), D3D11 은 참조 카운트로 처리.
	if (m_sceneColor.IsValid()) m_device->DestroyTexture(m_sceneColor);
	if (m_sceneDepth.IsValid()) m_device->DestroyTexture(m_sceneDepth);
	m_sceneColor = TextureHandle{};
	m_sceneDepth = TextureHandle{};
	m_sceneWidth = width;
	m_sceneHeight = height;
	m_sceneColorState = ResourceState::Common;
	if (width == 0 || height == 0) return;

	const TextureDesc color{
		.width = width,
		.height = height,
		.format = Format::R8G8B8A8_UNORM,   // TYPELESS 리소스: sRGB RTV + UNORM SRV
		.mipLevels = 1,
		.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
		.debugName = "SceneView_Color",
	};
	m_sceneColor = m_device->CreateTexture(color);

	const TextureDesc depth{
		.width = width,
		.height = height,
		.format = Format::D24_UNORM_S8_UINT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil,
		.debugName = "SceneView_Depth",
	};
	m_sceneDepth = m_device->CreateTexture(depth);

	if (!m_sceneColor.IsValid() || !m_sceneDepth.IsValid())
	{
		Log::Error("씬 뷰 텍스처 생성 실패 (%ux%u).", width, height);
		SetSceneTarget(0, 0);
		return;
	}
	Log::Info("씬 뷰 텍스처 %ux%u", width, height);
}
