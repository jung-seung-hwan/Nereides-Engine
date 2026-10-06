#pragma once
#include "Core/Input.h"
#include "Core/Time.h"
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace nereides
{
using ObjectId = std::uint64_t;
struct FrameContext { const Input& input; const Time& time; };
struct Transform
{
    DirectX::XMFLOAT3 position{0,0,0}, rotation{0,0,0}, scale{1,1,1};
    DirectX::XMMATRIX Matrix() const noexcept;
};
class Object;
class Scene;
class Component
{
public:
    virtual ~Component() = default;
    virtual void Update(Object&, Scene&, const FrameContext&) {}
    bool enabled = true;
private:
    friend class Object;
    friend class Scene;
    bool m_removed = false;
};
class Object final
{
public:
    Object(ObjectId id, std::string name) : m_id(id), m_name(std::move(name)) {}
    ObjectId Id() const noexcept { return m_id; }
    ObjectId Parent() const noexcept { return m_parent; }
    const std::string& Name() const noexcept { return m_name; }
    void SetName(std::string name) { m_name = std::move(name); }
    template<class T, class... Args> T& Add(Args&&... args)
    {
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *value; m_components.push_back(std::move(value)); return result;
    }
    template<class T> T* Get() const noexcept
    {
        for (const auto& component : m_components)
            if (!component->m_removed)
                if (auto* value = dynamic_cast<T*>(component.get())) return value;
        return nullptr;
    }
    template<class T> void Remove() noexcept
    {
        if (auto* value = Get<T>()) { value->enabled = false; value->m_removed = true; }
    }
    Transform transform;
    bool enabled = true;
private:
    friend class Scene;
    ObjectId m_id, m_parent = 0;
    std::string m_name;
    bool m_destroyed = false;
    std::vector<std::unique_ptr<Component>> m_components;
};
class Scene final
{
public:
    Object& Create(std::string name);
    Object* Find(ObjectId id) const noexcept;
    std::vector<ObjectId> Objects() const;
    void Destroy(ObjectId id); // Immediately invisible; memory released at Flush.
    void Clear();
    void Flush();
    void Update(const FrameContext& context);
    bool SetParent(ObjectId child, ObjectId parent);
    DirectX::XMMATRIX World(ObjectId id) const;
    bool Active(ObjectId id) const;
private:
    bool m_updating = false;
    std::vector<std::unique_ptr<Object>> m_objects;
};
struct Camera
{
    float verticalFov = DirectX::XM_PIDIV4;
    float nearPlane = 0.1f, farPlane = 500.f;
    DirectX::XMMATRIX Projection(float aspect) const;
    DirectX::XMMATRIX View(const Scene& scene, ObjectId object) const;
};
}
