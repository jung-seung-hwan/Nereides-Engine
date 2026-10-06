#pragma once
#include <cmath>
#include <cstdint>
namespace nereides
{
struct ClockState { double elapsed = 0; double delta = 0; };
class Time final
{
public:
    void Advance(double dt) noexcept
    {
        if (!std::isfinite(dt) || dt < 0) dt = 0;
        ++frame;
        AdvanceClock(real, dt);
        AdvanceClock(combat, paused || combatStopped ? 0 : dt * m_scale);
        AdvanceClock(escape, paused || combatStopped ? 0 : dt);
        AdvanceClock(presentation, paused && pausePresentation ? 0 : dt);
    }
    bool SetScale(double value) noexcept
    { if (!std::isfinite(value) || value < 0) return false; m_scale = value; return true; }
    void Reset() noexcept { *this = Time{}; }
    ClockState real, combat, escape, presentation;
    std::uint64_t frame = 0;
    bool paused = false, combatStopped = false;
    bool pausePresentation = true; // Provisional policy, configurable by content.
private:
    static void AdvanceClock(ClockState& clock, double dt) noexcept { clock.delta = dt; clock.elapsed += dt; }
    double m_scale = 1;
};
}
