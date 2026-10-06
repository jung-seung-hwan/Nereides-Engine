#pragma once
#include "Scene/Scene.h"
#include <map>
#include <optional>
#include <set>
namespace nereides
{
enum class Shape { Sphere, Box };
class Collider final : public Component
{
public:
    Shape shape=Shape::Sphere;
    DirectX::XMFLOAT3 offset{}, halfExtents{.5f,.5f,.5f};
    float radius=.5f;
    unsigned layer=1, mask=~0u;
    bool trigger=false;
    std::uint64_t attack=0; // Assigned by content; zero means not an attack.
};
struct CollisionShape
{
    ObjectId object=0;
    Shape shape=Shape::Sphere;
    DirectX::XMFLOAT3 center{}, halfExtents{};
    float radius=0;
    unsigned layer=1,mask=~0u;
    bool trigger=false;
    std::uint64_t attack=0;
};
enum class ContactKind { Hit, Enter, Stay, Exit };
struct Contact
{
    ObjectId first=0,second=0;
    std::uint64_t firstAttack=0,secondAttack=0;
    ContactKind kind=ContactKind::Hit;
    double fraction=0, time=0; // Fraction maps onto any clock's matching interval.
    DirectX::XMFLOAT3 position{};
};
struct RayHit { ObjectId object=0; float distance=0; DirectX::XMFLOAT3 position{}; };
// Optional content-owned ledger. Record only after accepting a candidate as a valid hit.
class HitLedger final
{
public:
    bool Record(std::uint64_t attack,ObjectId target) {return attack!=0 && m_hits.emplace(attack,target).second;}
    void End(std::uint64_t attack) {std::erase_if(m_hits,[=](const auto& hit){return hit.first==attack;});}
    void Reset() {m_hits.clear();}
private:
    std::set<std::pair<std::uint64_t,ObjectId>> m_hits;
};
class CollisionWorld final
{
public:
    std::vector<CollisionShape> Collect(const Scene& scene) const;
    std::vector<Contact> Step(const Scene& scene,double intervalStart,double intervalEnd);
    std::optional<RayHit> Raycast(const Scene& scene,DirectX::XMFLOAT3 origin,DirectX::XMFLOAT3 direction,float maxDistance,unsigned mask=~0u) const;
    void Reset() {m_previous.clear();m_overlaps.clear();}
private:
    std::map<ObjectId,CollisionShape> m_previous;
    std::set<std::pair<ObjectId,ObjectId>> m_overlaps;
};
}
