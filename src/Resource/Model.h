#pragma once
#include "Graphics/RenderFrame.h"
#include <filesystem>
#include <map>

namespace nereides
{
// Pose	: 노드 하나의 위치·회전·크기
struct Pose
{
    DirectX::XMFLOAT3 position{0, 0, 0}, scale{1, 1, 1};
    DirectX::XMFLOAT4 rotation{0, 0, 0, 1}; // 쿼터니언 XYZW

    DirectX::XMMATRIX Matrix() const; // 크기 행렬 * 회전 행렬 * 이동 행렬
};

// ModelNode : 모델 내부의 노드와 부모 관계, 기본 자세
struct ModelNode
{
    std::string name;
    int parent = -1;
    Pose bind;
};

// Key<T> : 특정 시간에 어떤 값을 가져야 하는지
template <class T>
struct Key
{
    double time = 0;
    T value{};
};

// NodeTrack : 노드 하나의 위치·회전·크기 변화 기록
struct NodeTrack
{
    unsigned node = 0;
    std::vector<Key<DirectX::XMFLOAT3>> positions, scales;
    std::vector<Key<DirectX::XMFLOAT4>> rotations;
};

// AnimationClip : 애니메이션 한 개의 길이와 노드별 키프레임 트랙을 보관
// 현재 시간에 맞춰 여러 노드의 자세를 함께 계산
struct AnimationClip
{
    std::string name;
    double duration = 0;
    std::vector<NodeTrack> tracks;
};

// BoneBinding : 메시를 변형할 뼈와 보정 행렬
struct BoneBinding
{
    unsigned node = 0;
    DirectX::XMFLOAT4X4 inverseBind{};
};

// MaterialData : 재질 이름, 색상, 텍스처 경로
struct MaterialData
{
    std::string name, baseColorTexture, normalTexture;
    DirectX::XMFLOAT4 baseColor{1, 1, 1, 1};
};

// ModelPart : 모델에서 그릴 메시 한 부분
struct ModelPart
{
    std::shared_ptr<const MeshData> mesh;
    unsigned node = 0, material = 0;
    std::vector<BoneBinding> bones;
};

// ModelData : 모델 원본
struct ModelData
{
    std::string path;
    std::vector<ModelNode> nodes;
    std::vector<ModelPart> parts;
    std::vector<MaterialData> materials;
    std::vector<AnimationClip> clips;
};

// ModelCache : 모델 로딩과 원본 재사용
class ModelCache final
{
public:
    std::shared_ptr<const ModelData> Load(const std::filesystem::path& path);
    void Prune();

private:
    std::map<std::filesystem::path, std::weak_ptr<const ModelData>> m_cache;
};

// AnimationPlayer : 현재 재생 시간과 자세 계산
// 현재 재생 중인 동작과 시간·자세는 각 객체가 별도로 계산됨
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

// ModelComponent : 씬 객체에 모델과 플레이어를 연결
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
