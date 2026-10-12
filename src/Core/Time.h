#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <stdexcept>

namespace nereides
{
// 프레임 시간을 고정 간격으로 소비. 과도한 지연은 버리고 소수 잔여 시간만 유지한다.
class FixedStep final
{
public:
    static constexpr double DefaultHz = 50.0;
    static constexpr unsigned MaxStepsPerFrame = 8; // 초기 보호값. 실측 후 조정.

    explicit FixedStep(double hz = DefaultHz) : m_step(1.0 / hz)
    {
        if (!std::isfinite(hz) || hz <= 0 || !std::isfinite(m_step))
            throw std::invalid_argument("Invalid fixed update frequency");
    }

    unsigned Accumulate(double dt)
    {
        if (!std::isfinite(dt) || dt <= 0)
            return 0;
        const double available = m_remainder + dt;
        const double whole = std::floor(available / m_step + 1e-9);
        const unsigned count = static_cast<unsigned>(std::min(whole, double(MaxStepsPerFrame)));
        m_remainder = std::max(0.0, std::fmod(available, m_step));
        if (m_remainder < m_step * 1e-9 || m_step - m_remainder < m_step * 1e-9)
            m_remainder = 0;
        m_dropped += std::max(0.0, whole - count) * m_step;
        return count;
    }

    void Reset()
    {
        m_remainder = 0;
    }
    double Step() const
    {
        return m_step;
    }
    double DroppedSeconds() const
    {
        return m_dropped;
    }

private:
    double m_step, m_remainder = 0, m_dropped = 0;
};

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
    // 단일 단계 검사·미리보기용 호환 함수. 일반 실행은 프레임과 tick을 분리한다.
    void Advance(double dt) noexcept
    {
        AdvanceFrame(dt);
        AdvanceTick(dt);
    }

    // 실제 시간과 연출은 렌더 프레임마다 진행한다.
    void AdvanceFrame(double dt) noexcept
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

        combat.delta = escape.delta = 0;
        AdvanceClock(presentation, paused && pausePresentation ? 0 : dt);
    }

    // 고정 갱신 한 번. 탈출 시계는 전투 배속의 영향을 받지 않는다.
    void AdvanceTick(double dt) noexcept
    {
        if (!std::isfinite(dt) || dt < 0)
            dt = 0;
        if (!paused && !combatStopped && dt > 0)
            ++tick;
        // combat은 m_scale을 이용해서 전투 중 슬로우 모션 처리 진행 가능
        AdvanceClock(combat, paused || combatStopped ? 0 : dt * m_scale);
        AdvanceClock(escape, paused || combatStopped ? 0 : dt);
    }

    // 프레임 콜백에는 마지막 tick의 delta를 재사용하지 않도록 0으로 제공한다.
    // 누적 전투 시간은 최신 tick 결과를 유지한다. 원본 시계는 변경하지 않는다.
    Time FrameView() const noexcept
    {
        Time result = *this;
        result.combat.delta = result.escape.delta = 0;
        return result;
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
    std::uint64_t tick = 0;
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
