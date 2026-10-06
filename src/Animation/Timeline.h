#pragma once
#include "Scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <stdexcept>
namespace nereides
{
struct TimelineEvent
{
    double time = 0;
    std::string name;
};
// One action/cutscene instance. Low FPS crosses all markers, not only the current frame's marker.
class Timeline final
{
public:
    void Start(double duration, std::vector<TimelineEvent> events)
    {
        if (!std::isfinite(duration) || duration < 0)
            throw std::invalid_argument("Invalid duration");
        for (const auto& event : events)
            if (!std::isfinite(event.time) || event.time < 0 || event.time > duration)
                throw std::invalid_argument("Invalid event time");
        std::stable_sort(events.begin(), events.end(),
                         [](const auto& a, const auto& b) { return a.time < b.time; });
        m_events = std::move(events);
        m_duration = duration;
        m_time = 0;
        m_next = 0;
        m_active = true;
        ++m_generation;
    }
    std::vector<TimelineEvent> Advance(double dt)
    {
        std::vector<TimelineEvent> result;
        if (!m_active || !std::isfinite(dt) || dt < 0)
            return result;
        m_time = std::min(m_time + dt, m_duration);
        while (m_next < m_events.size() && m_events[m_next].time <= m_time)
            result.push_back(m_events[m_next++]);
        if (m_time >= m_duration)
            m_active = false;
        return result;
    }
    void Cancel()
    {
        m_active = false;
        m_events.clear();
        ++m_generation;
    }
    bool Active() const
    {
        return m_active;
    }
    double Position() const
    {
        return m_time;
    }
    std::uint64_t Generation() const
    {
        return m_generation;
    }

private:
    std::vector<TimelineEvent> m_events;
    double m_time = 0, m_duration = 0;
    std::size_t m_next = 0;
    std::uint64_t m_generation = 0;
    bool m_active = false;
};
struct TransformKey
{
    double time = 0;
    Transform value;
};
// Rigid floating-sword trajectory, evaluated on the content's attack clock.
Transform EvaluateTrajectory(const std::vector<TransformKey>& keys, double time);
} // namespace nereides
