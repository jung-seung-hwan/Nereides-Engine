#include "Reflection/ComponentRegistry.h"
#include "Sandbox/MoveComponent.h"
#include <cmath>
#include <set>
#include <stdexcept>

namespace nereides
{
void PropertyInfo::Validate(const PropertyValue& value) const
{
    if (value.index() != defaultValue.index())
        throw std::runtime_error("Property type mismatch: " + name);
    auto number = [&](double v) {
        if (!std::isfinite(v) || v < minimum || v > maximum)
            throw std::runtime_error("Property out of range: " + name);
    };
    std::visit(
        [&](const auto& v) {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, DirectX::XMFLOAT3>)
            {
                number(v.x);
                number(v.y);
                number(v.z);
            }
            else if constexpr (!std::is_same_v<V, bool>)
                number(v);
        },
        value);
}

void PropertyInfo::Set(Component& component, const PropertyValue& value) const
{
    Validate(value);
    write(component, value);
}

void ComponentRegistry::Register(ComponentType type)
{
    if (type.name.empty() || type.label.empty() || !type.create || m_types.contains(type.name))
        throw std::runtime_error("Invalid/duplicate component registration: " + type.name);
    // 기존 리소스·동적 맵 전용 직렬화 키와 충돌하지 않게 한다.
    if (type.name == "cube" || type.name == "model" || type.name == "collider" ||
        type.name == "parameters")
        throw std::runtime_error("Reserved component type: " + type.name);
    for (const auto& [name, existing] : m_types)
        if (existing.cppType == type.cppType)
            throw std::runtime_error("C++ component type already registered: " + name);
    std::set<std::string> fields{"type", "enabled"};
    const auto sample = type.create();
    if (!sample || std::type_index(typeid(*sample)) != type.cppType)
        throw std::runtime_error("Component factory type mismatch");
    for (const auto& property : type.properties)
    {
        if (property.name.empty() || property.label.empty() || !property.read || !property.write ||
            !fields.insert(property.name).second || !std::isfinite(property.minimum) ||
            !std::isfinite(property.maximum) || property.minimum > property.maximum)
            throw std::runtime_error("Invalid property registration: " + property.name);
        property.Validate(property.defaultValue);
        property.Validate(property.read(*sample));
    }
    const auto name = type.name;
    m_types.emplace(name, std::move(type));
}

const ComponentType* ComponentRegistry::Find(const std::string& name) const
{
    const auto found = m_types.find(name);
    return found == m_types.end() ? nullptr : &found->second;
}

const ComponentType* ComponentRegistry::Find(const Component& component) const
{
    for (const auto& [name, type] : m_types)
        if (type.cppType == std::type_index(typeid(component)))
            return &type;
    return nullptr;
}

Component& ComponentRegistry::Add(Object& object, const std::string& name) const
{
    const auto* type = Find(name);
    if (!type)
        throw std::runtime_error("Unknown component type: " + name);
    for (const auto* component : object.Components())
        if (std::type_index(typeid(*component)) == type->cppType)
            throw std::runtime_error("Duplicate component: " + name);
    return object.AddComponent(type->create());
}

ComponentRegistry& ComponentTypes()
{
    static ComponentRegistry registry = [] {
        ComponentRegistry result;
        result.Register(DescribeComponent<MoveComponent>(
            "move", "Move Component",
            {Property("speed", "Move speed", &MoveComponent::speed, 0, 1000)}));
        return result;
    }();
    return registry;
}
} // namespace nereides
