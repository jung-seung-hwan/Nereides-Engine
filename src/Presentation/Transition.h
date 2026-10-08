#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nereides
{
// 2D 연출 레이어. 텍스처, 위치, 크기, 색상, 스케일 변화 등을 정의
struct Layer2D
{
    std::string texture, label;
    DirectX::XMFLOAT2 from{0, 0}, to{0, 0}, size{1, 1};
    DirectX::XMFLOAT4 color{1, 1, 1, 1};
    float startScale = 1, endScale = 1;
};

// 2D 연출 클립. 여러 레이어를 시간에 따라 재생하며, 페이드 인/아웃을 지원
struct Clip2D
{
    double duration = 1, fadeOut = .3, fadeIn = .3;
    std::vector<Layer2D> layers;
    static Clip2D Load(const std::filesystem::path& path);
};

// Transition 상태. 연출 진행 단계와 전투 차단 여부를 나타냄
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
