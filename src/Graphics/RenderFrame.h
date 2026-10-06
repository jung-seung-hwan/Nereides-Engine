#pragma once
#include "Scene/Scene.h"
#include <array>
#include <memory>
#include <vector>
namespace nereides
{
struct MeshVertex
{
    DirectX::XMFLOAT3 position{}, color{1,1,1}, normal{0,1,0};
    DirectX::XMFLOAT2 uv{};
    std::array<unsigned,4> bones{};
    std::array<float,4> weights{};
};
struct MeshData
{
    bool builtinCube = false;
    std::vector<MeshVertex> vertices;
    std::vector<unsigned> indices;
};
struct DrawItem
{
    std::shared_ptr<const MeshData> mesh; // Keeps CPU data alive through this frame.
    DirectX::XMFLOAT4X4 world{};
    DirectX::XMFLOAT4 tint{1,1,1,1};
    std::vector<DirectX::XMFLOAT4X4> bones;
};
struct RenderFrame
{
    DirectX::XMFLOAT4X4 view{}, projection{};
    std::vector<DrawItem> items;
    double time = 0;
};
class MeshComponent final : public Component
{
public:
    explicit MeshComponent(std::shared_ptr<const MeshData> value) : mesh(std::move(value)) {}
    std::shared_ptr<const MeshData> mesh;
    DirectX::XMFLOAT4 tint{1,1,1,1};
};
RenderFrame CollectRenderFrame(const Scene&, ObjectId cameraObject, const Camera&, float aspect, double time);
std::shared_ptr<const MeshData> MakeCube();
}
