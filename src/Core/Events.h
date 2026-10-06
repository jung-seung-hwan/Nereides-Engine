#pragma once
#include "Scene/Scene.h"
#include <functional>
#include <map>
#include <stdexcept>
namespace nereides
{
struct Event
{
    std::string name;
    ObjectId source=0,target=0;
    std::uint64_t operation=0,epoch=0;
    double time=0,value=0;
};
class Events final
{
public:
    using Subscription=std::uint64_t;
    Subscription Subscribe(ObjectId owner,std::string name,std::function<void(const Event&)> callback)
    {const auto id=++m_next;m_subscribers.emplace(id,Subscriber{owner,std::move(name),std::move(callback)});return id;}
    void Unsubscribe(Subscription id){m_subscribers.erase(id);}
    std::uint64_t Epoch() const {return m_epoch;}
    bool Publish(Event event)
    {if(event.epoch!=m_epoch)return false;m_pending.push_back(std::move(event));return true;}
    void Reset(){++m_epoch;m_pending.clear();m_subscribers.clear();}
    void Dispatch(const Scene& scene)
    {
        if(m_dispatching)throw std::logic_error("Recursive event dispatch");
        m_dispatching=true;
        auto batch=std::move(m_pending);m_pending.clear();
        try
        {
            for(const auto& event:batch)
            {
                if(event.epoch!=m_epoch)continue;
                std::vector<Subscription> recipients;
                for(const auto& [id,subscriber]:m_subscribers)if(subscriber.name==event.name)recipients.push_back(id);
                for(auto id:recipients)
                {
                    if(event.epoch!=m_epoch)break;
                    const auto found=m_subscribers.find(id);if(found==m_subscribers.end())continue;
                    if(found->second.owner && !scene.Find(found->second.owner)){m_subscribers.erase(found);continue;}
                    auto callback=found->second.callback;callback(event); // Safe if callback unsubscribes or resets.
                }
            }
        }
        catch(...){m_dispatching=false;throw;}
        m_dispatching=false;
    }
private:
    struct Subscriber{ObjectId owner;std::string name;std::function<void(const Event&)> callback;};
    std::map<Subscription,Subscriber> m_subscribers;
    std::vector<Event> m_pending;
    std::uint64_t m_epoch=1,m_next=0;
    bool m_dispatching=false;
};
}
