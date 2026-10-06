#pragma once
#include "Graphics/RenderFrame.h"
#include <filesystem>
#include <map>

namespace nereides
{
struct Pose
{
    DirectX::XMFLOAT3 position{0, 0, 0}, scale{1, 1, 1};
    DirectX::XMFLOAT4 rotation{0, 0, 0, 1};
    DirectX::XMMATRIX Matrix() const;
};
struct ModelNode
{
    std::string name;
    int parent = -1;
    Pose bind;
};
template <class T> struct Key
{
    double time = 0;
    T value{};
};
struct NodeTrack
{
    unsigned node = 0;
    std::vector<Key<DirectX::XMFLOAT3>> positions, scales;
    std::vector<Key<DirectX::XMFLOAT4>> rotations;
};
struct AnimationClip
{
    std::string name;
    double duration = 0;
    std::vector<NodeTrack> tracks;
};
struct BoneBinding
{
    unsigned node = 0;
    DirectX::XMFLOAT4X4 inverseBind{};
};
struct MaterialData
{
    std::string name, baseColorTexture, normalTexture;
    DirectX::XMFLOAT4 baseColor{1, 1, 1, 1};
};
struct ModelPart
{
    std::shared_ptr<const MeshData> mesh;
    unsigned node = 0, material = 0;
    std::vector<BoneBinding> bones;
};
struct ModelData
{
    std::string path;
    std::vector<ModelNode> nodes;
    std::vector<ModelPart> parts;
    std::vector<MaterialData> materials;
    std::vector<AnimationClip> clips;
};
class ModelCache final
{
public:
    std::shared_ptr<const ModelData> Load(const std::filesystem::path& path);
    void Prune();

private:
    std::map<std::filesystem::path, std::weak_ptr<const ModelData>> m_cache;
};
class AnimationPlayer final
{
public:
    explicit AnimationPlayer(std::shared_ptr<const ModelData> model);
    bool Play(std::size_t clip, bool loop, double blendSeconds = 0);
    void Advance(double dt);
    const std::vector<Pose>& LocalPose() const
    {
        return m_pose;
    }
    std::vector<DirectX::XMFLOAT4X4> GlobalPose() const;
    double Position() const
    {
        return m_time;
    }
    bool Finished() const
    {
        return m_finished;
    }

private:
    std::shared_ptr<const ModelData> m_model;
    std::vector<Pose> m_pose, m_from;
    std::size_t m_clip = 0;
    double m_time = 0, m_blendTime = 0, m_blendDuration = 0;
    bool m_playing = false, m_loop = false, m_finished = false;
    void Evaluate();
};
class ModelComponent final : public Component
{
public:
    UpdatePhase Phase() const override
    {
        return UpdatePhase::Animation;
    }
    explicit ModelComponent(std::shared_ptr<const ModelData> value)
        : model(std::move(value)), player(model)
    {
    }
    void Update(Object&, Scene&, const FrameContext& frame) override
    {
        player.Advance(frame.time.combat.delta);
    }
    std::shared_ptr<const ModelData> model;
    AnimationPlayer player;
};
void AppendModelDraws(const ModelComponent& component, DirectX::FXMMATRIX world,
                      RenderFrame& frame);
} // namespace nereides
