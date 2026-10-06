#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace nereides
{
struct Layer2D
{
    std::string texture, label;
    DirectX::XMFLOAT2 from{0, 0}, to{0, 0}, size{1, 1};
    DirectX::XMFLOAT4 color{1, 1, 1, 1};
    float startScale = 1, endScale = 1;
};
struct Clip2D
{
    double duration = 1, fadeOut = .3, fadeIn = .3;
    std::vector<Layer2D> layers;
    static Clip2D Load(const std::filesystem::path& path);
};
enum class TransitionState
{
    Idle,
    FadeOut,
    Playing,
    WaitingForScene,
    FadeIn,
    Complete,
    Failed
};
struct TransitionNotice
{
    std::uint64_t token;
    TransitionState state;
};
// Presentation service. Content owns area selection, HP, scene preparation, and combat policy.
class Transition final
{
public:
    std::uint64_t Begin(Clip2D clip);
    void Advance(double presentationDelta);
    bool Skip(std::uint64_t token);
    bool SceneReady(std::uint64_t token);
    bool Fail(std::uint64_t token, std::string reason);
    void Reset();
    bool BlocksCombat() const
    {
        return m_state != TransitionState::Idle && m_state != TransitionState::Complete;
    }
    TransitionState State() const
    {
        return m_state;
    }
    std::uint64_t Token() const
    {
        return m_token;
    }
    float Fade() const;
    double ClipPosition() const
    {
        return m_state == TransitionState::Playing ? m_time : 0;
    }
    const Clip2D& Clip() const
    {
        return m_clip;
    }
    const std::string& Error() const
    {
        return m_error;
    }
    std::vector<TransitionNotice> TakeNotices()
    {
        auto result = std::move(m_notices);
        m_notices.clear();
        return result;
    }

private:
    void SetState(TransitionState state);
    Clip2D m_clip;
    TransitionState m_state = TransitionState::Idle;
    std::uint64_t m_token = 0;
    double m_time = 0;
    std::string m_error;
    std::vector<TransitionNotice> m_notices;
};
} // namespace nereides
