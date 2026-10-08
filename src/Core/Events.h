#pragma once
#include "Scene/Scene.h"
#include <functional>
#include <map>
#include <stdexcept>

namespace nereides
{
// 이벤트 이름·관련 객체·실행 번호·시각·수치 등 전달할 알림 데이터
struct Event
{
    std::string name;
    ObjectId source = 0, target = 0;
    std::uint64_t operation = 0, epoch = 0;
    double time = 0, value = 0;
};

// 발생한 알림을 보관했다가, 그 알림을 받겠다고 등록한 함수들에게 전달
class Events final
{
public:
    using Subscription = std::uint64_t;

    // 이벤트를 수신할 콜백을 등록하고, 등록 ID 반환
    Subscription Subscribe(ObjectId owner, std::string name,
                           std::function<void(const Event&)> callback)
    {
        const auto id = ++m_next;
        m_subscribers.emplace(id, Subscriber{owner, std::move(name), std::move(callback)});
        return id;
    }

    // 등록 ID를 이용해 이벤트 수신을 취소
    void Unsubscribe(Subscription id)
    {
        m_subscribers.erase(id);
    }

    // Reset 전후의 실행 구간을 구분하는 현재 번호 반환
    std::uint64_t Epoch() const
    {
        return m_epoch;
    }

    // 이벤트 발생 시 알림 예약
    bool Publish(Event event)
    {
        if (event.epoch != m_epoch)
            return false;

        m_pending.push_back(std::move(event));
        return true;
    }

    // 실행 구간 번호를 변경하고 대기 이벤트와 모든 구독 제거
    void Reset()
    {
        ++m_epoch;
        m_pending.clear();
        m_subscribers.clear();
    }

    // 등록된 함수 호출
    void Dispatch(const Scene& scene)
    {
        if (m_dispatching)
            throw std::logic_error("Recursive event dispatch");

        m_dispatching = true;
        auto batch = std::move(m_pending);
        m_pending.clear();

        try
        {
            for (const auto& event : batch)
            {
                if (event.epoch != m_epoch)
                    continue;

                std::vector<Subscription> recipients;

                for (const auto& [id, subscriber] : m_subscribers)
                    if (subscriber.name == event.name)
                        recipients.push_back(id);
                for (auto id : recipients)
                {
                    if (event.epoch != m_epoch)
                        break;
                    const auto found = m_subscribers.find(id);
                    if (found == m_subscribers.end())
                        continue;
                    if (found->second.owner && !scene.Find(found->second.owner))
                    {
                        m_subscribers.erase(found);
                        continue;
                    }

                    auto callback = found->second.callback;
                    callback(event); // Safe if callback unsubscribes or resets.
                }
            }
        }
        catch (...)
        {
            m_dispatching = false;
            throw;
        }

        m_dispatching = false;
    }

private:
    struct Subscriber
    {
        ObjectId owner;
        std::string name;
        std::function<void(const Event&)> callback;
    };

    std::map<Subscription, Subscriber> m_subscribers;
    std::vector<Event> m_pending;
    std::uint64_t m_epoch = 1, m_next = 0;
    bool m_dispatching = false;
};
} // namespace nereides
