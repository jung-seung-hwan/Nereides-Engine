#include "Reflection/ComponentRegistry.h"
#include "Sandbox/MoveComponent.h"
#include "Collision/Collision.h"
#include "Data/SceneIO.h"
#include "Renderer/RenderComponents.h"
#include <cmath>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <climits>

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
            else if constexpr (std::is_same_v<V, std::string>)
            {
                if (std::find(choices.begin(), choices.end(), v) == choices.end())
                    throw std::runtime_error("Unknown enum key: " + name);
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
    if (type.name.empty() || type.label.empty() || (!type.create && !type.createFromFile) ||
        m_types.contains(type.name))
        throw std::runtime_error("Invalid/duplicate component registration: " + type.name);
    // 전용 직렬화 키는 해당 실제 타입만 사용할 수 있다.
    if (((type.name == "cube" || type.name == "sphere") && type.cppType != typeid(MeshComponent)) ||
        (type.name == "model" && type.cppType != typeid(ModelComponent)) ||
        (type.name == "collider" && type.cppType != typeid(Collider)) ||
        (type.name == "parameters" && type.cppType != typeid(Parameters)))
        throw std::runtime_error("Reserved component type: " + type.name);
    for (const auto& [name, existing] : m_types)
        if (existing.cppType == type.cppType)
            throw std::runtime_error("C++ component type already registered: " + name);
    std::set<std::string> fields{"type", "enabled"};
    const auto sample = type.create ? type.create() : nullptr;
    if ((type.create && (!sample || std::type_index(typeid(*sample)) != type.cppType)) ||
        (!sample && !type.properties.empty()))
        throw std::runtime_error("Component factory type mismatch");
    for (const auto& property : type.properties)
    {
        if (property.name.empty() || property.label.empty() || !property.read || !property.write ||
            !fields.insert(property.name).second || !std::isfinite(property.minimum) ||
            !std::isfinite(property.maximum) || property.minimum > property.maximum)
            throw std::runtime_error("Invalid property registration: " + property.name);
        property.Validate(property.defaultValue);
        property.Validate(property.read(*sample));
        std::set<std::string> choices;
        for (const auto& key : property.choices)
            if (key.empty() || !choices.insert(key).second)
                throw std::runtime_error("Duplicate/empty enum key: " + property.name);
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

Component& ComponentRegistry::Add(Object& object, const std::string& name, ModelCache* cache,
                                  const std::filesystem::path& source) const
{
    const auto* type = Find(name);
    if (!type)
        throw std::runtime_error("Unknown component type: " + name);
    for (const auto* component : object.Components())
        if (std::type_index(typeid(*component)) == type->cppType)
            throw std::runtime_error("Duplicate component: " + name);
    std::unique_ptr<Component> component;
    if (type->createFromFile)
    {
        if (!cache || source.empty())
            throw std::runtime_error("Component requires a model file: " + name);
        component = type->createFromFile(*cache, source);
    }
    else
        component = type->create();
    if (!component || std::type_index(typeid(*component)) != type->cppType)
        throw std::runtime_error("Component factory type mismatch: " + name);
    return object.AddComponent(std::move(component));
}

ComponentRegistry& ComponentTypes()
{
    static ComponentRegistry registry = [] {
        ComponentRegistry result;
        result.Register(DescribeComponent<MoveComponent>(
            "move", "Move Component",
            {Property("speed", "Move speed", &MoveComponent::speed, 0, 1000)}));
        result.Register(DescribeComponent<Collider>(
            "collider", "Collider",
            {EnumProperty<Collider, Shape>("shape", "Shape", &Collider::shape,
                                           {{"sphere", Shape::Sphere}, {"box", Shape::Box}}),
             Property("offset", "Offset", &Collider::offset),
             Property("extents", "Half extents (box)", &Collider::halfExtents, 0, 1e9),
             Property("radius", "Radius (sphere)", &Collider::radius, 0, 10000),
             Property("layer", "Layer bits", &Collider::layer, 0, double(UINT_MAX)),
             Property("mask", "Mask bits", &Collider::mask, 0, double(UINT_MAX)),
             Property("trigger", "Trigger", &Collider::trigger)}));
        result.Register({"cube",
                         "Mesh Component (Cube)",
                         typeid(MeshComponent),
                         [] { return std::make_unique<MeshComponent>(MakeCube()); },
                         {}});
        result.Register(DescribeComponent<LightComponent>(
            "light", "Light",
            {EnumProperty<LightComponent, LightType>(
                 "lightType", "Type",
                 &LightComponent::type,
                 {{"directional", LightType::Directional},
                  {"point", LightType::Point},
                  {"spot", LightType::Spot}}),
             Property("color", "Color", &LightComponent::color, 0, 1000),
             Property("intensity", "Intensity", &LightComponent::intensity, 0, 10000),
             Property("range", "Range", &LightComponent::range, 0, 100000),
             Property("attenuation", "Attenuation (const, linear, quad)",
                      &LightComponent::attenuation, 0, 1000),
             Property("innerConeCos", "Spot inner cone cos", &LightComponent::innerConeCos, -1, 1),
             Property("outerConeCos", "Spot outer cone cos", &LightComponent::outerConeCos, -1, 1)}));
        result.Register(DescribeComponent<EnvironmentComponent>(
            "environment", "Environment",
            {EnumProperty<EnvironmentComponent, EnvironmentPreset>(
                 "preset", "Sky / IBL",
                 &EnvironmentComponent::preset,
                 {{"builtinSky", EnvironmentPreset::BuiltinSky},
                  {"cubemapSample", EnvironmentPreset::CubemapSample},
                  {"none", EnvironmentPreset::None}}),
             Property("iblIntensity", "IBL intensity", &EnvironmentComponent::iblIntensity, 0, 100),
             Property("exposure", "Exposure", &EnvironmentComponent::exposure, 0, 100),
             Property("toneMapping", "Tone mapping", &EnvironmentComponent::toneMapping),
             Property("ambientColor", "Ambient color", &EnvironmentComponent::ambientColor, 0, 100),
             Property("clearColor", "Clear color (linear)", &EnvironmentComponent::clearColor, 0,
                      1)}));
        result.Register(DescribeComponent<Parameters>("parameters", "Parameters", {}));
        ComponentType model{"model", "Model Component (file...)", typeid(ModelComponent), {}, {}};
        model.createFromFile = [](ModelCache& cache, const std::filesystem::path& path) {
            return std::make_unique<ModelComponent>(cache.Load(path));
        };
        result.Register(std::move(model));
        return result;
    }();
    return registry;
}
} // namespace nereides
