#pragma once
#include <cmath>
#include <cstdint>

namespace nereides
{
struct ClockState
{
    // 해당 시계의 누적 시간
    double elapsed = 0;

    // 해당 시계가 이번 갱신에서 진행한 시간
    double delta = 0;
};

class Time final
{
public:
    // Application이 경과 시간 측정 → Time::Advance() 에 전달
    void Advance(double dt) noexcept
    {
        if (!std::isfinite(dt) || dt < 0)
            dt = 0;

        ++frame;

        /*
        시계           진행량                정지 조건
        real            dt              아래 플래그로는 정지하지 않음 
        combat        dt × m_scale     paused 또는 `combatStopped
        escape          dt              paused 또는 `combatStopped
        presentation    dt              paused와 pausePresentation이 모두 참 
        */
        AdvanceClock(real, dt);

        // combat은 m_scale을 이용해서 전투 중 슬로우 모션 처리 진행 가능
        AdvanceClock(combat, paused || combatStopped ? 0 : dt * m_scale);
        AdvanceClock(escape, paused || combatStopped ? 0 : dt);
        AdvanceClock(presentation, paused && pausePresentation ? 0 : dt);
    }

    // 전투 배속 설정
    bool SetScale(double value) noexcept
    {
        if (!std::isfinite(value) || value < 0)
            return false;

        m_scale = value;
        return true;
    }

    void Reset() noexcept
    {
        *this = Time{};
    }

    /*
    real	        전달받은 시간을 배속·정지 플래그 없이 누적
    combat	        이동·공격 등 전투 진행에 사용할 시간
    escape	        전투 배속의 영향을 받지 않는 탈출용 시간
    presentation	연출 진행에 사용할 시간
    */
    ClockState real, combat, escape, presentation;

    std::uint64_t frame = 0;
    bool paused = false, combatStopped = false;
    bool pausePresentation = true; // Provisional policy, configurable by content.

private:
    // 전달받은 원본 시계 수정
    static void AdvanceClock(ClockState& clock, double dt) noexcept
    {
        clock.delta = dt;
        clock.elapsed += dt;
    }

    double m_scale = 1;
};
} // namespace nereides
