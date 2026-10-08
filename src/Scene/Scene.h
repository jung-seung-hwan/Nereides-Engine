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

// 컴포넌트 업데이트 시 필요한 입력 및 시간 정보
// 원본 참조 + 값 수정 불가
struct FrameContext
{
    const Input& input;
    const Time& time;
};

// 객체의 위치, 회전, 크기 정보를 담는 구조체
// 부모 기준의 로컬 위치·회전·크기. 회전은 라디안 단위
// 부모 변환까지 합친 월드 행렬은 Scene::World()에서 계산
struct Transform
{
    DirectX::XMFLOAT3 position{0, 0, 0}, rotation{0, 0, 0}, scale{1, 1, 1};
    DirectX::XMMATRIX Matrix() const noexcept;
};

class Object;
class Scene;


// update 내에서 컴포넌트의 실행 순서 지정
enum class UpdatePhase
{
    Logic,
    Animation,
    Late
};


// Object에 붙이는 기능 또는 데이터의 공통 기반 클래스
// Scene이 활성 상태와 실행 단계를 확인해 Update()를 호출
class Component
{
public:
    virtual ~Component() = default;

    // 소유 객체, 장면, 입력·시간을 전달받아 행동 실행
    virtual void Update(Object&, Scene&, const FrameContext&) {}
    virtual UpdatePhase Phase() const
    {
        return UpdatePhase::Logic;
    }
    bool enabled = true;

private:
    friend class Object;
    friend class Scene;

    // 삭제 예약 여부. 실제 제거는 Scene::Flush()에서 수행
    bool m_removed = false;
};

class Object final
{
public:
    Object(ObjectId id, std::string name) : m_id(id), m_name(std::move(name)) {}

    ObjectId Id() const noexcept
    {
        return m_id;
    }

    ObjectId Parent() const noexcept
    {
        return m_parent;
    }

    const std::string& Name() const noexcept
    {
        return m_name;
    }

    void SetName(std::string name)
    {
        m_name = std::move(name);
    }

    // T 컴포넌트를 생성해 이 Object가 소유하고, 설정에 사용할 참조 반환
    template <class T, class... Args> T& Add(Args&&... args)
    {
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *value;
        m_components.push_back(std::move(value));
        return result;
    }

    // 삭제 예약되지 않은 컴포넌트 중 T로 변환 가능한 첫 항목 반환
    template <class T> T* Get() const noexcept
    {
        for (const auto& component : m_components)
            if (!component->m_removed)
                if (auto* value = dynamic_cast<T*>(component.get()))
                    return value;
        return nullptr;
    }

    // 첫 번째 일치 항목을 비활성화하고 삭제 예약
    template <class T> void Remove() noexcept
    {
        if (auto* value = Get<T>())
        {
            value->enabled = false;
            value->m_removed = true;
        }
    }

    // 삭제 예약되지 않은 컴포넌트들의 읽기 전용 포인터 목록 반환
    std::vector<const Component*> Components() const
    {
        std::vector<const Component*> result;
        for (const auto& component : m_components)
            if (!component->m_removed)
                result.push_back(component.get());
        return result;
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


// Object의 수명, 부모 관계, 단계별 업데이트를 관리
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


// 카메라의 시야각·투영 범위 설정. 현재 Component 파생형은 아님
// 위치와 회전은 별도 카메라 Object의 Transform을 사용
struct Camera
{
    float verticalFov = DirectX::XM_PIDIV4;
    float nearPlane = 0.1f, farPlane = 500.f;
    DirectX::XMMATRIX Projection(float aspect) const;
    DirectX::XMMATRIX View(const Scene& scene, ObjectId object) const;
};
} // namespace nereides
