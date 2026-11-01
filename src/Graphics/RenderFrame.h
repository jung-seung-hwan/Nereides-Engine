#pragma once
#include "Renderer/Light.h"
#include "Renderer/Material.h"
#include "Renderer/PostProcessSettings.h"
#include "Scene/Scene.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace nereides
{
// 정점 한 개의 정보
struct MeshVertex
{
    DirectX::XMFLOAT3 position{}, color{1, 1, 1}, normal{0, 1, 0};
    DirectX::XMFLOAT2 uv{};
    DirectX::XMFLOAT4 tangent{1, 0, 0, 1}; // xyz = u가 늘어나는 방향, w = 손잡이(±1)
    std::array<unsigned, 4> bones{};
    std::array<float, 4> weights{};
};

// 정점들과 연결 순서로 표현한 기본 모양
struct MeshData
{
    bool builtinCube = false;
    // 0보다 크면 MakeSphere로 만든 구. 씬 파일에 이 값으로 다시 만들 수 있게 저장함.
    float sphereRadius = 0;
    unsigned sphereSlices = 0, sphereStacks = 0;
    std::vector<MeshVertex> vertices;
    std::vector<unsigned> indices;
};

// 그릴 항목 하나의 모양·변환·재질
struct DrawItem
{
    std::shared_ptr<const MeshData> mesh; // Keeps CPU data alive through this frame.
    DirectX::XMFLOAT4X4 world{};
    // 재질은 포인터로 GPU 재질 캐시의 키가 됨. 같은 재질을 쓰는 항목은 같은 객체를 가리켜야 함.
    // nullptr이면 렌더러의 기본 재질을 씀.
    std::shared_ptr<const Material> material;
    std::vector<DirectX::XMFLOAT4X4> bones;
};

// 환경: 스카이박스·IBL 큐브 이름과 출력(노출·톤매핑). 이름 규칙은 Renderer/Material.h를 따름.
// 비어 있는 이름은 "없음". IBL 맵이 없고 스카이박스만 있으면 스카이박스를 밉으로 흐려 대신 씀.
struct RenderEnvironment
{
    std::string skybox = "builtin:sky";
    std::string irradiance, specular, brdfLut;
    float iblIntensity = 1.f;
    float exposure = 1.f;
    bool toneMapping = false;
    DirectX::XMFLOAT3 ambientColor{.12f, .12f, .12f}; // 환경 큐브가 없을 때의 환경광
    PostProcessSettings post; // 2026/10/23: 후처리 (블러·블룸·GreyScale). 기본은 전부 꺼짐
};

// 카메라 정보와 전체 그리기 목록
// 카메라 위치는 view의 역행렬에서 렌더러가 구함 (에디터가 view를 덮어써도 일관됨).
struct RenderFrame
{
    DirectX::XMFLOAT4X4 view{}, projection{};
    std::vector<DrawItem> items;
    // 조명. 첫 방향광이 그림자를 만듦. 최대 MAX_LIGHTS개까지 쓰임. 기본은 방향광 하나.
    std::vector<LightData> lights{LightData{}};
    RenderEnvironment environment;
    // 선형 색. 백버퍼의 sRGB 뷰가 인코딩하므로 화면에서는 (6, 26, 46)이 됨.
    std::array<float, 4> clearColor{0.001935f, 0.010023f, 0.027140f, 1.f};
    double time = 0;
};

// 객체에 어떤 메시를 사용할지 지정
class MeshComponent final : public Component
{
public:
    explicit MeshComponent(std::shared_ptr<const MeshData> value) : mesh(std::move(value)) {}

    std::shared_ptr<const MeshData> mesh;
    DirectX::XMFLOAT4 tint{1, 1, 1, 1};
    float metallic = 0, roughness = 0.5f; // glTF 금속성/거칠기. 기본값은 렌더러 기본 재질과 같음

    // tint를 baseColor로, metallic·roughness를 PBR 값으로 쓰는 재질. tint가 바뀔 때만 새로 만들어 GPU 재질 캐시가 재사용되게 함.
    std::shared_ptr<const Material> RenderMaterial() const;

private:
    mutable std::shared_ptr<const Material> m_material;
    mutable DirectX::XMFLOAT4 m_materialTint{};
    mutable float m_materialMetallic = 0, m_materialRoughness = 0;
};

// cameraObject가 0이면 FindActiveCamera로 고른다. 켜진 카메라가 없으면 기본 시점을 쓴다.
RenderFrame CollectRenderFrame(const Scene&, float aspect, double time, ObjectId cameraObject = 0);

std::shared_ptr<const MeshData> MakeCube();
// 반지름 radius인 구. 경도 slices, 위도 stacks 분할. 바깥에서 볼 때 시계방향이 앞면.
std::shared_ptr<const MeshData> MakeSphere(float radius, unsigned slices, unsigned stacks);
} // namespace nereides
