#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>
#include "RHI/Handle.h"
#include "RHI/PipelineTypes.h"
#include "RHI/RenderPassTypes.h"
#include "Renderer/VertexTypes.h" // VertexFormat
#include "Renderer/Material.h"
#include "Renderer/Light.h"
#include "Graphics/RenderFrame.h"

namespace RHI
{
class Device;
class CommandList;
} // namespace RHI

// ImGui에서 바꾸는 렌더 설정. Material의 옵션과 합쳐져 PipelineStateDesc가 됨.
struct RenderSettings
{
    bool wireframe = false;
    bool cullBack = true;
    bool srgbOutput = true; // 5단계: 백버퍼 sRGB 뷰. 끄면 감마 보정 없이 출력함 (비교용)
    int samplerOverride = -1; // -1 = 재질 설정대로. 그 외 SamplerPreset 값으로 전부 덮어씀

    // 6단계: 그림자 (방향광 0) 와 노멀 매핑
    bool shadows = true;
    float shadowBias = 0.0015f;    // 수신자 깊이에서 빼는 값 (광원 클립 z 단위, 0..1)
    float shadowStrength = 1.0f;   // 0 = 그림자 없음, 1 = 완전히 어둡게
    float shadowOrthoSize = 60.0f; // 광원 직교 투영의 한 변 (월드 단위). 씬 전체를 덮어야 함
    float shadowDistance = 40.0f;  // 광원 눈의 위치 = 그림자 중심 − 방향 × 거리
    // Nereides: 그림자 영역의 중심. Sherlock 은 (0, 2, 0) 으로 고정했음.
    DirectX::XMFLOAT3 shadowCenter = DirectX::XMFLOAT3(0.0f, 2.0f, 0.0f);
    bool normalMapping = true;

    // 11단계: 디버그 뷰 (0 조명, 1 알베도, 2 월드 노멀, 3 깊이, 4 그림자 계수, 5 UV) 와 깊이 뷰의 범위(월드 단위)
    int debugView = 0;
    float debugDepthRange = 60.0f;
};

// Nereides 포팅: Sherlock 의 Renderer 를 가져오되 입력을 Scene/Camera 대신 nereides::RenderFrame 으로 바꿈.
// 메시는 nereides::MeshData, 재질은 Material 의 shared_ptr 로 받고, GPU 캐시는 그 포인터를 키로 쓰되 weak_ptr 로 수명을 확인함.
// 카메라 미리보기(11-C)와 Scene::InvalidateScene 은 가져오지 않았음. 스키닝(뼈 팔레트 b4)이 추가됨.
//
// 프레임을 그림. 프레임 데이터는 갖지 않고 입력으로 받음
//
// 소유하는 것: 셰이더 핸들, 상수버퍼 핸들, BindingLayout 세 개(프레임/오브젝트/재질)와
// ResourceSet, PSO Desc의 고정 부분, Mesh → GPU 버퍼 캐시, Material → GPU 재질 캐시,
// 텍스처 캐시(이름 → 핸들), 샘플러 프리셋, 그림자 맵. 전부 핸들이므로 이 헤더와 .cpp에는 D3D 타입이 없음.
//
// 6단계: 모든 GPU 명령은 Device::GetCommandList() 를 거침. 프레임은 렌더 패스 세 개로 이루어짐.
//   ShadowPass (깊이 전용, 2048², 광원 ViewProj) → MainPass (백버퍼 sRGB 뷰 + 깊이) → UIPass (백버퍼 UNORM 뷰)
// 2026-10-08: MainPass 는 불투명 드로우 뒤에 스카이박스(전체 화면 삼각형, 깊이 1.0)를 그림. 스카이박스는 프레임 셋 t9,
// IBL(확산 조도·반사 프리필터·BRDF LUT)은 t10~t12 에 꽂힘. 재질은 PBR(metallic-roughness)이고 재질 셋이 t0~t4 를 씀.
// 그림자 맵은 "DSV 로 쓰고 → SRV 로 읽는" 첫 리소스라 양쪽에 Barrier 호출이 있음 (D3D11 에서는 상태 추적만 함).
class Renderer
{
public:
    struct Stats
    {
        uint32_t draws = 0;
        uint32_t shadowDraws = 0;
        uint32_t triangles = 0; // 9단계: 메인 패스 삼각형 수
        uint32_t pipelineSwitches = 0;
        uint32_t resourceSetSwitches = 0;
        uint32_t meshSwitches = 0;
        uint32_t renderPasses = 0;
        uint32_t barriers = 0;
        uint32_t shaderReloads = 0; // 누적
    };

    static constexpr uint32_t kShadowMapSize = 2048;

    Renderer();
    ~Renderer();

    bool Initialize(RHI::Device* device);
    void Shutdown();

    // 그림자 패스 + 메인 패스. 프레임의 UI 패스에서는 아래 두 함수 사이에 ImGui 를 그림.
    void Render(const nereides::RenderFrame& frame);
    void BeginUIPass(); // 백버퍼 UNORM 뷰, 깊이 없음, Load (씬 위에 그림)
    void EndUIPass();   // 패스 종료 + 백버퍼 RenderTarget → Present 전이

    // 셰이더를 소스에서 다시 컴파일하고 PSO 캐시를 비움. 실패하면 이전 셰이더를 유지함.
    // Debug 빌드에서는 소스 파일 수정 시각을 벽시계 기준으로 감시해 자동으로 호출함.
    bool ReloadShaders();

    RenderSettings& GetSettings()
    {
        return m_settings;
    }
    const Stats& GetStats() const
    {
        return m_stats;
    }
    size_t GetPipelineCount() const;
    size_t GetGpuMeshCount() const
    {
        return m_gpuMeshes.size();
    }
    size_t GetGpuMaterialCount() const
    {
        return m_gpuMaterials.size();
    }
    size_t GetTextureCount() const
    {
        return m_textureCache.size() + m_cubeCache.size();
    }
    TextureHandle GetShadowMap() const
    {
        return m_shadowMap;
    } // 디버그 표시용

    // 11단계: 씬 뷰를 오프스크린 텍스처에 그림 (에디터용). 0×0 이면 백버퍼에 직접 그림 (기존 동작).
    // 크기가 바뀌면 텍스처를 다시 만듦. 색 텍스처는 UNORM(TYPELESS) — sRGB 뷰로 그리고 ImGui 는 UNORM SRV 로 읽음.
    void SetSceneTarget(uint32_t width, uint32_t height);
    bool IsOffscreen() const
    {
        return m_sceneColor.IsValid();
    }
    TextureHandle GetSceneTexture() const
    {
        return m_sceneColor;
    }
    uint32_t GetSceneWidth() const
    {
        return m_sceneWidth;
    }
    uint32_t GetSceneHeight() const
    {
        return m_sceneHeight;
    }

    // 메시/재질의 내용이 바뀌면 캐시 항목을 버림. 파괴된 것은 매 프레임 자동으로 버려지므로 내용을 바꿀 때만 부름.
    void InvalidateMesh(const nereides::MeshData* mesh);
    void InvalidateMaterial(const Material* material);

private:
    struct GpuMesh
    {
        std::weak_ptr<const nereides::MeshData>
            source; // 캐시 키(포인터)가 재사용되어도 다른 객체임을 알아봄
        BufferHandle vertexBuffer;
        BufferHandle indexBuffer;
        uint32_t indexCount = 0;
        Format indexFormat = Format::R32_UINT; // 9단계 (D10): 정점 65,536개 미만이면 R16_UINT
        VertexFormat vertexFormat = VertexFormat::PositionColorNormalTexcoordTangent;
    };

    // Material = 상수버퍼 + ResourceSet(b3, t0, t1, s0). PSO는 Desc가 캐시 키이므로 여기 저장하지
    // 않고 매 프레임 GetPipelineFor로 얻음(핫리로드로 캐시가 비워져도 안전함).
    // 셋에 들어간 텍스처·샘플러가 재질 편집으로 바뀌면 셋을 다시 만듦.
    struct GpuMaterial
    {
        std::weak_ptr<const Material> source;
        BufferHandle constants;
        ResourceSetHandle set;
        TextureHandle boundAlbedo;
        TextureHandle boundNormal;
        TextureHandle boundMetallicRoughness; // 2026-10-08 PBR
        TextureHandle boundOcclusion;
        TextureHandle boundEmissive;
        SamplerHandle boundSampler;
        uint64_t uploadedFrame = UINT64_MAX; // 이번 프레임에 상수를 올렸는지
    };

    struct DrawItem
    {
        PipelineHandle pipeline; // 메인 패스 PSO (정렬 키)
        const Material* material;
        const nereides::MeshData* mesh;
        const nereides::DrawItem* source; // 월드 행렬과 뼈 팔레트. 프레임이 끝날 때까지 유효
        VertexFormat vertexFormat;
    };

    bool LoadShaders(bool fromSourceOnly);
    // 셰이더가 실제로 쓰는 슬롯이 BindingLayout 에 선언됐는지 대조함. 2026-10-08: 스카이박스 VS/PS 도 대조함.
    bool ValidateBindings(const std::vector<uint8_t>& vsCode, const std::vector<uint8_t>& psCode,
                          const std::vector<uint8_t>& shadowVsCode,
                          const std::vector<uint8_t>& skyVsCode,
                          const std::vector<uint8_t>& skyPsCode,
                          const std::vector<uint8_t>& skinnedVsCode,
                          const std::vector<uint8_t>& shadowSkinnedVsCode) const;
    void CheckHotReload();
    bool CreateSamplers();
    bool CreateBuiltinTextures();
    bool CreateShadowResources();

    void PruneCaches(); // 파괴된 메시·재질의 GPU 자원을 버림
    void BuildDrawList(const nereides::RenderFrame& frame);
    void UploadPerFrameConstants(
        const nereides::RenderFrame&
            frame); // b0: view/proj 는 프레임의 것, 나머지는 프레임 공통 값
    void RenderShadowPass(RHI::CommandList& cmd);
    void RenderMainPass(RHI::CommandList& cmd, const nereides::RenderFrame& frame);
    void DrawItems(RHI::CommandList& cmd,
                   const nereides::RenderFrame& frame); // 메인 패스 드로우 루프 (끝에 스카이박스)
    void DrawSkybox(
        RHI::CommandList& cmd); // 2026-10-08: 전체 화면 삼각형 하나 (Draw(3), 정점 버퍼 없음)
    // 2026-10-08: 이번 프레임의 환경 바인딩. RenderFrame::environment 의 이름들을 ResolveEnvironment 가 핸들과 셰이더 파라미터로 바꿈.
    struct EnvironmentBinding
    {
        TextureHandle skybox;     // t9  (없으면 검은 큐브)
        TextureHandle irradiance; // t10 (IBL 확산 조도. 없으면 스카이박스 → 반사 큐브 → 검은 큐브 순으로 대체)
        TextureHandle specular;   // t11 (IBL 반사 프리필터. 없으면 스카이박스 → 검은 큐브)
        TextureHandle brdfLut;    // t12 (없으면 흰색 2D, 셰이더는 해석적 근사를 씀)
        uint32_t specularMips = 1; // t11 의 밉 수 — 거칠기 → 밉 매핑의 끝
        float irradianceLod =
            0.0f; // t10 을 읽을 밉. 진짜 조도 큐브면 0, 스카이박스로 대체하면 끝에서 두 번째 밉(면당 2×2)
        bool hasEnvironment = false; // 거짓이면 셰이더가 ambientColor 를 씀
        bool hasBrdfLut = false;
        float iblIntensity = 1.0f;
        float exposure = 1.0f;
        bool toneMapping = false;

        bool SameTextures(const EnvironmentBinding& other) const
        {
            return skybox == other.skybox && irradiance == other.irradiance &&
                   specular == other.specular && brdfLut == other.brdfLut;
        }
    };
    void ResolveEnvironment(
        const nereides::RenderEnvironment& environment); // → m_environment
    // 프레임 셋(b0, b2, t8, s4, t9, s5, t10, t11, t12)을 m_environment 의 텍스처로 만듦.
    // 꽂힌 텍스처가 바뀔 때만 다시 만듦 (ResourceSet 은 값 객체).
    bool UpdateFrameSet();
    // 월드 행렬(b1)과, 스키닝 항목이면 뼈 팔레트(b4)를 올림.
    void UploadObjectConstants(const nereides::DrawItem& item);
    DirectX::XMMATRIX ComputeLightViewProj(const LightData& light) const;

    const GpuMesh* GetOrCreateGpuMesh(const std::shared_ptr<const nereides::MeshData>& mesh);
    const GpuMaterial* GetOrCreateGpuMaterial(const Material& material);
    PipelineHandle GetPipelineFor(
        const Material& material,
        VertexFormat vertexFormat); // vertexFormat 에 맞는 VS(정적/스키닝)를 고름
    PipelineHandle GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat);
    PipelineHandle
    GetSkyPipeline(); // 2026-10-08: 정점 속성 0개, 깊이 LessEqual + 쓰기 끔, 컬링 없음
    // 재질의 텍스처 이름 → 핸들. "builtin:*"은 절차적으로 만들고, 그 밖에는 파일 경로로 읽음 (절대 경로, 또는 실행 폴더·assets/textures 기준).
    // 실패하면 흰색 텍스처를 돌려줌.
    TextureHandle GetOrLoadTexture(const std::string& name, bool srgb);
    // 2026-10-08: 스카이박스 이름 → 큐브 텍스처 핸들. "builtin:*" 은 절차적으로 만들고, 그 밖에는 파일(큐브 DDS) 또는 폴더(면 이미지 6장)
    // 경로. 실패하면 1×1 검은 큐브를 돌려줌. 항상 sRGB 로 읽음.
    // mipLevels 가 있으면 그 큐브의 밉 수를 돌려줌 (반사 프리필터의 거칠기 → 밉 매핑에 씀).
    TextureHandle GetOrLoadCubemap(const std::string& name, uint32_t* mipLevels = nullptr);
    SamplerHandle GetSampler(SamplerPreset preset) const;

private:
    RHI::Device* m_device = nullptr;

    ShaderHandle m_vs;
    ShaderHandle m_vsSkinned; // Nereides: 스키닝 VS (뼈 팔레트 b4)
    ShaderHandle m_ps;
    ShaderHandle m_shadowVs; // 6단계: 깊이 전용 VS
    ShaderHandle m_shadowVsSkinned;
    ShaderHandle m_skyVs; // 2026-10-08: 스카이박스 (SV_VertexID 전체 화면 삼각형)
    ShaderHandle m_skyPs;
    BufferHandle m_perFrameCB;
    BufferHandle m_perObjectCB;
    BufferHandle m_boneCB; // Nereides: 뼈 행렬 128개
    BufferHandle m_lightCB;

    // 갱신 빈도별 레이아웃. PSO Desc에는 이 세 개가 순서대로 들어감.
    BindingLayoutHandle
        m_frameLayout; // b0 (VS|PS), b2 (PS), t8 그림자 맵, s4 비교 샘플러, t9 스카이박스, s5, t10~t12 IBL (모두 PS)
    BindingLayoutHandle m_objectLayout; // b1 (VS), b4 뼈 팔레트 (VS)
    BindingLayoutHandle
        m_materialLayout; // b3 (VS|PS), t0 알베도, t1 노멀, t2 금속성/거칠기, t3 AO, t4 발광, s0 (모두 PS)
    ResourceSetHandle m_frameSet;
    ResourceSetHandle m_objectSet;

    PipelineStateDesc m_baseDesc;   // 메인 패스: 셰이더·레이아웃 등 설정과 무관한 부분
    PipelineStateDesc m_shadowDesc; // 그림자 패스: VS만, RTV 0개, DSV D32_FLOAT, 깊이 바이어스
    PipelineStateDesc m_skyDesc;    // 2026-10-08: 스카이박스. 프레임 레이아웃 하나만 씀
    RenderSettings m_settings;
    Stats m_stats;
    uint64_t m_frameNumber = 0;

    std::shared_ptr<const Material> m_defaultMaterial; // DrawItem.material == nullptr 일 때

    // 5단계: 샘플러 프리셋과 텍스처 캐시
    static constexpr uint32_t kSamplerPresetCount = 5;
    SamplerHandle m_samplers[kSamplerPresetCount];
    TextureHandle m_whiteTexture; // "텍스처 없음"의 기본값
    TextureHandle m_flatNormalTexture;
    std::unordered_map<std::string, TextureHandle>
        m_textureCache; // 키 = 이름 + "|srgb" / "|linear"

    // 2026-10-08: 큐브맵. 2D 캐시와 나눈 이유: 실패 시 대체물이 흰 2D 가 아니라 검은 큐브여야 함 (t9 는 TextureCube 슬롯).
    TextureHandle m_blackCube; // 환경이 없을 때 t9~t11 에 꽂는 1×1 큐브
    EnvironmentBinding m_environment; // 이번 프레임 (ResolveEnvironment)
    EnvironmentBinding
        m_boundEnvironment; // 지금 m_frameSet 에 꽂힌 것. 비우면 다음 UpdateFrameSet 이 셋을 다시 만듦
    struct CubeEntry
    {
        TextureHandle handle;
        uint32_t mipLevels = 1;
    };
    std::unordered_map<std::string, CubeEntry>
        m_cubeCache; // 키 = 큐브맵 이름 (스카이박스·IBL 공용)

    // 6단계: 그림자 맵 (D32_FLOAT, DSV + SRV) 과 비교 샘플러. 상태는 Barrier 로 전이함.
    TextureHandle m_shadowMap;
    SamplerHandle m_shadowSampler;
    ResourceState m_shadowMapState = ResourceState::Common;

    // 11단계: 오프스크린 씬 타깃
    TextureHandle m_sceneColor;
    TextureHandle m_sceneDepth;
    uint32_t m_sceneWidth = 0;
    uint32_t m_sceneHeight = 0;
    ResourceState m_sceneColorState = ResourceState::Common;
    bool m_shadowEnabledThisFrame = false;
    bool m_warnedLightLimit = false; // 조명이 MAX_LIGHTS 를 넘을 때 로그는 한 번만
    DirectX::XMFLOAT4X4 m_lightViewProj;

    // 메시·재질은 shared_ptr 로 공유됨. 포인터를 키로 쓰고, 항목의 weak_ptr 로 같은 객체인지 확인함.
    std::unordered_map<const nereides::MeshData*, GpuMesh> m_gpuMeshes;
    std::unordered_map<const Material*, GpuMaterial> m_gpuMaterials;
    std::vector<DrawItem> m_drawList;

    // 핫리로드: 감시하는 소스 파일과 마지막 수정 시각
    struct WatchedFile
    {
        std::wstring path;
        uint64_t writeTime = 0;
    };
    std::vector<WatchedFile> m_watched;
    uint64_t m_nextWatchTick = 0; // 벽시계(GetTickCount64, 밀리초). 프레임 시간과 무관하게 폴링함
};
