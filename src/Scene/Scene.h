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
// Scene이 활성 상태와 실행 단계를 확인해 Update/FixedUpdate를 각각 호출
class Component
{
public:
    virtual ~Component() = default;

    // 씬 구성·설정 복원 후 한 번. 같은 갱신 목록의 Awake를 모두 끝낸 뒤 Start한다.
    virtual void Awake(Object&, Scene&) {}
    virtual void OnEnable(Object&, Scene&) {}
    virtual void Start(Object&, Scene&) {}

    // 정리 콜백에서는 예외를 던지지 않는다. 메모리 해제 전 외부 등록을 정리한다.
    virtual void OnDisable(Object&, Scene&) noexcept {}
    virtual void OnDestroy(Object&, Scene&) noexcept {}

    // 렌더 프레임마다 호출. 시각적 연출은 time.presentation.delta를 사용한다.
    virtual void Update(Object&, Scene&, const FrameContext&) {}

    // 고정 tick마다 호출. 이동·전투·전투 애니메이션은 time.combat.delta를 사용한다.
    virtual void FixedUpdate(Object&, Scene&, const FrameContext&) {}

    virtual UpdatePhase Phase() const
    {
        return UpdatePhase::Logic;
    }

    bool Enabled() const noexcept
    {
        return m_enabled;
    }
    // 실행 대상에서는 즉시 제외. 활성 전환 콜백은 다음 안전한 갱신 경계에서 호출한다.
    void SetEnabled(bool value) noexcept
    {
        m_enabled = value;
    }

private:
    friend class Object;
    friend class Scene;

    // 삭제 예약 여부. 실제 제거는 Scene::Flush()에서 수행
    bool m_removed = false, m_enabled = true;
    bool m_awake = false, m_started = false, m_active = false, m_disposed = false;
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
    template <class T, class... Args>
    T& Add(Args&&... args)
    {
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *value;
        m_components.push_back(std::move(value));
        return result;
    }

    // 삭제 예약되지 않은 컴포넌트 중 T로 변환 가능한 첫 항목 반환
    template <class T>
    T* Get() const noexcept
    {
        for (const auto& component : m_components)
            if (!component->m_removed)
                if (auto* value = dynamic_cast<T*>(component.get()))
                    return value;
        return nullptr;
    }

    // 첫 번째 일치 항목을 비활성화하고 삭제 예약
    template <class T>
    void Remove() noexcept
    {
        if (auto* value = Get<T>())
        {
            value->SetEnabled(false);
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
    bool Enabled() const noexcept
    {
        return m_enabled;
    }
    // 실행 대상에서는 즉시 제외. 활성 전환 콜백은 다음 안전한 갱신 경계에서 호출한다.
    void SetEnabled(bool value) noexcept
    {
        m_enabled = value;
    }

private:
    friend class Scene;
    ObjectId m_id, m_parent = 0;
    std::string m_name;
    bool m_destroyed = false, m_enabled = true;
    std::vector<std::unique_ptr<Component>> m_components;
};

// Object의 수명, 부모 관계, 단계별 업데이트를 관리
class Scene final
{
public:
    Scene() = default;
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    // 로드한 편집 씬만 이동한다. 실행 중 씬 이동은 콜백의 Scene 참조를 깨므로 금지한다.
    Scene(Scene&& other);
    Scene& operator=(Scene&& other);

    // BeginPlay 전에 객체·컴포넌트·설정·부모 관계 구성을 끝낸다.
    void BeginPlay();
    // 실행 객체를 정리한다. 에디터는 이후 별도로 편집 스냅샷을 복원한다.
    void EndPlay() noexcept;
    bool Playing() const noexcept
    {
        return m_playing;
    }

    Object& Create(std::string name);
    Object* Find(ObjectId id) const noexcept;
    std::vector<ObjectId> Objects() const;
    void Destroy(ObjectId id); // Immediately invisible; memory released at Flush.
    void Clear();
    void Flush();
    // 각 경로는 별도 스냅샷으로 Logic → Animation → Late를 실행한다.
    void Update(const FrameContext& context, bool flushAfterUpdate = true);
    // 외부 고정 tick에서 충돌·결과·이벤트 후 Flush하려면 false 지정.
    void FixedUpdate(const FrameContext& context, bool flushAfterUpdate = true);
    bool SetParent(ObjectId child, ObjectId parent);
    DirectX::XMMATRIX World(ObjectId id) const;
    bool Active(ObjectId id) const;

private:
    using Batch = std::vector<std::pair<Object*, std::vector<Component*>>>;
    Batch Snapshot() const;
    bool Runnable(const Object&, const Component&) const;
    void Initialize(const Batch&);
    void DisableInactive(const Batch&) noexcept;
    void Dispose(Object&, Component&) noexcept;
    void RunUpdate(const FrameContext& context, bool fixed, bool flushAfterUpdate);
    bool m_updating = false, m_playing = false, m_endRequested = false;
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
