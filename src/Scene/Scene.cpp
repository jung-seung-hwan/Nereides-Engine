#include "Scene/Scene.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace nereides
{
using namespace DirectX;
XMMATRIX Transform::Matrix() const noexcept
{
    return XMMatrixScaling(scale.x, scale.y, scale.z) *
           XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z) *
           XMMatrixTranslation(position.x, position.y, position.z);
}
Object& Scene::Create(std::string name)
{
    static std::atomic<ObjectId> next{1}; // Never reused across Scene/Clear/restart.
    auto object = std::make_unique<Object>(next.fetch_add(1), std::move(name));
    auto& result = *object;
    m_objects.push_back(std::move(object));
    return result;
}
Object* Scene::Find(ObjectId id) const noexcept
{
    for (const auto& object : m_objects)
        if (object->Id() == id && !object->m_destroyed)
            return object.get();
    return nullptr;
}
std::vector<ObjectId> Scene::Objects() const
{
    std::vector<ObjectId> ids;
    for (const auto& object : m_objects)
        if (!object->m_destroyed)
            ids.push_back(object->Id());
    return ids;
}
void Scene::Destroy(ObjectId id)
{
    auto* target = Find(id);
    if (!target)
        return;
    target->m_destroyed = true;
    bool changed;
    do
    {
        changed = false;
        for (const auto& object : m_objects)
            if (!object->m_destroyed && object->Parent() && !Find(object->Parent()))
            {
                object->m_destroyed = true;
                changed = true;
            }
    } while (changed);
}
void Scene::Clear()
{
    for (const auto& object : m_objects)
        object->m_destroyed = true;
    Flush();
}
void Scene::Flush()
{
    if (m_updating)
        return;
    for (const auto& object : m_objects)
        std::erase_if(object->m_components,
                      [](const auto& component) { return component->m_removed; });
    std::erase_if(m_objects, [](const auto& object) { return object->m_destroyed; });
}
bool Scene::Active(ObjectId id) const
{
    for (auto* object = Find(id); object; object = Find(object->Parent()))
    {
        if (!object->enabled)
            return false;
        if (!object->Parent())
            return true;
    }
    return false;
}
void Scene::Update(const FrameContext& context)
{
    if (m_updating)
        throw std::logic_error("Recursive Scene::Update");
    m_updating = true;
    try
    {
        // Snapshot at the frame boundary: new objects/components begin next frame.
        std::vector<std::pair<ObjectId, std::vector<Component*>>> batch;
        for (auto id : Objects())
        {
            std::vector<Component*> components;
            for (const auto& component : Find(id)->m_components)
                components.push_back(component.get());
            batch.emplace_back(id, std::move(components));
        }
        for (auto phase : {UpdatePhase::Logic, UpdatePhase::Animation, UpdatePhase::Late})
            for (const auto& [id, components] : batch)
            {
                auto* object = Find(id);
                if (!object || !Active(id))
                    continue;
                for (auto* component : components)
                {
                    if (!Active(id))
                        break;
                    if (component->enabled && !component->m_removed && component->Phase() == phase)
                        component->Update(*object, *this, context);
                }
            }
    }
    catch (...)
    {
        m_updating = false;
        Flush();
        throw;
    }
    m_updating = false;
    Flush();
}
bool Scene::SetParent(ObjectId child, ObjectId parent)
{
    auto* object = Find(child);
    if (!object || (parent && !Find(parent)))
        return false;
    for (auto* ancestor = Find(parent); ancestor; ancestor = Find(ancestor->Parent()))
        if (ancestor->Id() == child)
            return false;
    object->m_parent = parent;
    return true; // Preserve local transform.
}
XMMATRIX Scene::World(ObjectId id) const
{
    XMMATRIX result = XMMatrixIdentity();
    for (auto* object = Find(id); object; object = Find(object->Parent()))
        result *= object->transform.Matrix();
    return result;
}
XMMATRIX Camera::Projection(float aspect) const
{
    if (!std::isfinite(aspect) || aspect <= 0 || !std::isfinite(verticalFov) || verticalFov <= 0 ||
        verticalFov >= XM_PI || !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        nearPlane <= 0 || farPlane <= nearPlane)
        throw std::invalid_argument("Invalid camera projection");
    return XMMatrixPerspectiveFovLH(verticalFov, aspect, nearPlane, farPlane);
}
XMMATRIX Camera::View(const Scene& scene, ObjectId object) const
{
    if (!scene.Find(object))
        throw std::invalid_argument("Camera object is missing");
    const auto world = scene.World(object);
    const float determinant = XMVectorGetX(XMMatrixDeterminant(world));
    if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001f)
        throw std::invalid_argument("Camera transform is singular");
    return XMMatrixInverse(nullptr, world);
}
} // namespace nereides
