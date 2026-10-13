#pragma once
#include "Scene/Scene.h"
#include <functional>
#include <map>
#include <typeindex>
#include <type_traits>
#include <variant>

namespace nereides
{
// 명시적으로 등록한 설정값만 편집·저장한다. 포인터와 실행 상태는 등록하지 않는다.
using PropertyValue = std::variant<float, int, bool, DirectX::XMFLOAT3>;

struct PropertyInfo
{
    std::string name, label;
    PropertyValue defaultValue;
    double minimum = -1e9, maximum = 1e9;
    std::function<PropertyValue(const Component&)> read;
    std::function<void(Component&, const PropertyValue&)> write;

    void Validate(const PropertyValue& value) const;
    void Set(Component& component, const PropertyValue& value) const;
};

// 멤버 포인터를 사용하므로 메모리 오프셋이나 강제 포인터 변환이 필요 없다.
template <class T, class V>
PropertyInfo Property(std::string name, std::string label, V T::* member, double minimum = -1e9,
                      double maximum = 1e9)
{
    PropertyInfo result{std::move(name), std::move(label), T{}.*member, minimum, maximum};
    result.read = [member](const Component& component) -> PropertyValue {
        return dynamic_cast<const T&>(component).*member;
    };
    result.write = [member](Component& component, const PropertyValue& value) {
        dynamic_cast<T&>(component).*member = std::get<V>(value);
    };
    return result;
}

struct ComponentType
{
    std::string name, label;
    std::type_index cppType;
    std::function<std::unique_ptr<Component>()> create;
    std::vector<PropertyInfo> properties;
};

template <class T>
ComponentType DescribeComponent(std::string name, std::string label,
                                std::vector<PropertyInfo> properties)
{
    return {std::move(name), std::move(label), typeid(T), [] { return std::make_unique<T>(); },
            std::move(properties)};
}

class ComponentRegistry
{
public:
    // 시작 시 등록한다. 영구 저장 키(name)는 표시 이름(label)과 분리한다.
    void Register(ComponentType type);
    const ComponentType* Find(const std::string& name) const;
    const ComponentType* Find(const Component& component) const;
    const std::map<std::string, ComponentType>& Types() const
    {
        return m_types;
    }
    Component& Add(Object& object, const std::string& name) const;

private:
    std::map<std::string, ComponentType> m_types;
};

// 기본 등록을 한 번 구성한다. 새 콘텐츠 타입도 편집·로드 전에 여기에 등록한다.
ComponentRegistry& ComponentTypes();
} // namespace nereides
