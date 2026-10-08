#pragma once
#include "Scene/Scene.h"
#include <array>
#include <memory>
#include <vector>

namespace nereides
{
// 정점 한 개의 정보
struct MeshVertex
{
    DirectX::XMFLOAT3 position{}, color{1, 1, 1}, normal{0, 1, 0};
    DirectX::XMFLOAT2 uv{};
    std::array<unsigned, 4> bones{};
    std::array<float, 4> weights{};
};

// 정점들과 연결 순서로 표현한 기본 모양
struct MeshData
{
    bool builtinCube = false;
    std::vector<MeshVertex> vertices;
    std::vector<unsigned> indices;
};

// 그릴 항목 하나의 모양·변환·색상
struct DrawItem
{
    std::shared_ptr<const MeshData> mesh; // Keeps CPU data alive through this frame.
    DirectX::XMFLOAT4X4 world{};
    DirectX::XMFLOAT4 tint{1, 1, 1, 1};
    std::vector<DirectX::XMFLOAT4X4> bones;
};

// 카메라 정보와 전체 그리기 목록
struct RenderFrame
{
    DirectX::XMFLOAT4X4 view{}, projection{};
    std::vector<DrawItem> items;
    double time = 0;
};

// 객체에 어떤 메시를 사용할지 지정
class MeshComponent final : public Component
{
public:
    explicit MeshComponent(std::shared_ptr<const MeshData> value) : mesh(std::move(value)) {}

    std::shared_ptr<const MeshData> mesh;
    DirectX::XMFLOAT4 tint{1, 1, 1, 1};
};

RenderFrame CollectRenderFrame(const Scene&, ObjectId cameraObject, const Camera&, float aspect,
                               double time);

std::shared_ptr<const MeshData> MakeCube();
} // namespace nereides
