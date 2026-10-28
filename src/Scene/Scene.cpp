#include "Scene/Scene.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace nereides
{
using namespace DirectX;

// 크기 * 회전 * 이동 순서로 변환 행렬 생성 (행 우선)
XMMATRIX Transform::Matrix() const noexcept
{
    return XMMatrixScaling(scale.x, scale.y, scale.z) *
           XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z) *
           XMMatrixTranslation(position.x, position.y, position.z);
}


// 객체 생성 및 ID 할당, 이름 설정 -> Scene에서 객체 소유
Object& Scene::Create(std::string name)
{
    static std::atomic<ObjectId> next{1}; // Never reused across Scene/Clear/restart.
    auto object = std::make_unique<Object>(next.fetch_add(1), std::move(name));
    auto& result = *object;
    m_objects.push_back(std::move(object));
    return result;
}


// ID로 객체 검색
// -> 추후 에디터 설정(태그) 기반으로 검색하는 기능 추가 필요
// 현재는 객체 수가 적어 선형 검색(O(n)) 사용. Active()/World()가 부모마다 호출하므로 tick 수만큼 비용 증가
// -> 객체 수가 늘어나거나 m_objects 정렬(ID 순서)을 바꾸는 기능이 추가되면 Find 로직 변경 필요 (이진 검색 / 해시맵 등)
Object* Scene::Find(ObjectId id) const noexcept
{
    for (const auto& object : m_objects)
        if (object->Id() == id && !object->m_destroyed)
            return object.get();
    return nullptr;
}


// 모든 객체 ID 반환
std::vector<ObjectId> Scene::Objects() const
{
    std::vector<ObjectId> ids;

    for (const auto& object : m_objects)
        if (!object->m_destroyed)
            ids.push_back(object->Id());
    return ids;
}


// 객체와 자식 객체 모두 삭제
// 안전한 시점에 삭제하기 위해 m_destroyed 플래그를 설정하고, Flush()에서 실제 메모리 해제
void Scene::Destroy(ObjectId id)
{
    auto* target = Find(id);
    if (!target)
        return;

    target->m_destroyed = true;
    bool changed;

    // 자식 객체에 대한 삭제 처리
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


// 모든 객체 삭제
// Destory와 동일하게 삭제 예약만 먼저 하고 안전한 시점에 객체 제거
void Scene::Clear()
{
    for (const auto& object : m_objects)
        object->m_destroyed = true;
    Flush();
}


// 콜백 동안 모든 객체의 메모리를 유지한 뒤, 정리가 끝난 대상만 해제한다.
void Scene::Flush()
{
    if (m_updating)
        return;
    m_updating = true;
    bool pending;
    do
    {
        pending = false;
        const auto batch = Snapshot();
        DisableInactive(batch);
        for (const auto& [object, components] : batch)
            for (auto* component : components)
                if ((object->m_destroyed || component->m_removed) && !component->m_disposed)
                {
                    Dispose(*object, *component);
                    pending = true;
                }
        // 정리 콜백이 앞서 방문한 객체를 삭제했을 수도 있으므로 다시 검사한다.
    } while (pending);

    for (const auto& object : m_objects)
        std::erase_if(object->m_components,
                      [](const auto& component) { return component->m_disposed; });
    std::erase_if(m_objects, [](const auto& object) { return object->m_destroyed; });
    m_updating = false;
    if (m_endRequested)
        EndPlay();
}


Scene::~Scene()
{
    EndPlay();
}


Scene::Scene(Scene&& other)
{
    *this = std::move(other);
}


Scene& Scene::operator=(Scene&& other)
{
    if (this == &other)
        return *this;
    if (m_updating || other.m_updating || other.m_playing)
        throw std::logic_error("Cannot move an executing Scene");
    EndPlay();
    m_objects = std::move(other.m_objects);
    return *this;
}


Scene::Batch Scene::Snapshot() const
{
    Batch batch;
    for (const auto& object : m_objects)
    {
        std::vector<Component*> components;
        for (const auto& component : object->m_components)
            components.push_back(component.get());
        batch.emplace_back(object.get(), std::move(components));
    }
    return batch;
}


bool Scene::Runnable(const Object& object, const Component& component) const
{
    return m_playing && !object.m_destroyed && !component.m_removed && !component.m_disposed &&
           component.Enabled() && Active(object.Id());
}


void Scene::DisableInactive(const Batch& batch) noexcept
{
    for (const auto& [object, components] : batch)
        for (auto* component : components)
            if (component->m_active && !Runnable(*object, *component))
            {
                component->m_active = false;
                component->OnDisable(*object, *this);
            }
}


void Scene::Dispose(Object& object, Component& component) noexcept
{
    // 플래그를 먼저 바꿔 콜백의 재삭제 요청에도 중복 호출하지 않는다.
    if (component.m_disposed)
        return;
    component.m_disposed = true;
    if (component.m_active)
    {
        component.m_active = false;
        component.OnDisable(object, *this);
    }
    if (component.m_awake)
        component.OnDestroy(object, *this);
}


void Scene::Initialize(const Batch& batch)
{
    // 객체가 계층상 활성일 때만 Awake한다. 컴포넌트 자체의 비활성은 초기화를 막지 않는다.
    for (const auto& [object, components] : batch)
        for (auto* component : components)
            if (m_playing && Active(object->Id()) && !component->m_removed && !component->m_awake)
            {
                component->m_awake = true;
                component->Awake(*object, *this);
            }

    DisableInactive(batch);
    for (const auto& [object, components] : batch)
        for (auto* component : components)
            if (Runnable(*object, *component) && component->m_awake && !component->m_active)
            {
                component->m_active = true;
                component->OnEnable(*object, *this);
            }

    DisableInactive(batch);
    for (const auto& [object, components] : batch)
        for (auto* component : components)
            if (Runnable(*object, *component) && component->m_active && !component->m_started)
            {
                component->m_started = true;
                component->Start(*object, *this);
            }
}


void Scene::BeginPlay()
{
    if (m_updating)
        throw std::logic_error("Cannot begin play inside a Scene callback");
    if (m_playing)
        return;
    m_playing = true;
    m_updating = true;
    try
    {
        Initialize(Snapshot());
    }
    catch (...)
    {
        m_updating = false;
        EndPlay();
        throw;
    }
    m_updating = false;
    Flush();
}


void Scene::EndPlay() noexcept
{
    m_playing = false;
    if (m_updating)
    {
        m_endRequested = true;
        return;
    }
    m_updating = true;
    // 전체 OnDisable 후 OnDestroy. 다른 객체의 메모리는 모든 콜백이 끝날 때까지 유지.
    const auto batch = Snapshot();
    DisableInactive(batch);
    for (const auto& [object, components] : batch)
        for (auto* component : components)
            Dispose(*object, *component);
    m_objects.clear();
    m_updating = false;
    m_endRequested = false;
}


// 객체가 활성 상태인지 확인
bool Scene::Active(ObjectId id) const
{
    for (auto* object = Find(id); object; object = Find(object->Parent()))
    {
        if (!object->Enabled())
            return false;

        if (!object->Parent())
            return true;
    }

    return false;
}


// Scene 업데이트 처리
void Scene::Update(const FrameContext& context, bool flushAfterUpdate)
{
    RunUpdate(context, false, flushAfterUpdate);
}


void Scene::FixedUpdate(const FrameContext& context, bool flushAfterUpdate)
{
    RunUpdate(context, true, flushAfterUpdate);
}


void Scene::RunUpdate(const FrameContext& context, bool fixed, bool flushAfterUpdate)
{
    // Update/FixedUpdate 실행 중 어느 갱신 경로로든 재진입하면 예외 발생
    if (m_updating)
        throw std::logic_error("Recursive Scene::Update");

    if (!m_playing)
        return; // 편집 씬에는 게임 콜백을 실행하지 않는다.
    m_updating = true;
    try
    {
        // 이 경계 이후 추가된 객체·컴포넌트는 다음 갱신에서 초기화하고 실행한다.
        const auto batch = Snapshot();
        Initialize(batch);
        for (auto phase : {UpdatePhase::Logic, UpdatePhase::Animation, UpdatePhase::Late})
            for (const auto& [object, components] : batch)
                for (auto* component : components)
                    if (Runnable(*object, *component) && component->m_active &&
                        component->m_started && component->Phase() == phase)
                    {
                        if (fixed)
                            component->FixedUpdate(*object, *this, context);
                        else
                            component->Update(*object, *this, context);
                    }
        DisableInactive(batch);
    }
    catch (...)
    {
        m_updating = false;
        EndPlay();
        throw;
    }
    m_updating = false;
    if (m_endRequested)
        EndPlay();
    else if (flushAfterUpdate)
        Flush();
}


// 부모 연결
// 부모 연결 이후에는 기존 좌표를 부모 기준으로 해석
// -> 추후 에디터에서 부모 변경 시, 자식 위치를 유지할지 여부 선택 가능하도록 옵션 추가 필요
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


// 객체의 월드 변환 행렬 계산
// 부모의 변환을 자식에게도 적용
XMMATRIX Scene::World(ObjectId id) const
{
    XMMATRIX result = XMMatrixIdentity();

    for (auto* object = Find(id); object; object = Find(object->Parent()))
        result *= object->transform.Matrix();
    return result;
}


// 원근 표현
XMMATRIX Camera::Projection(float aspect) const
{
    if (!std::isfinite(aspect) || aspect <= 0 || !std::isfinite(verticalFov) || verticalFov <= 0 ||
        verticalFov >= XM_PI || !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        nearPlane <= 0 || farPlane <= nearPlane)
        throw std::invalid_argument("Invalid camera projection");

    return XMMatrixPerspectiveFovLH(verticalFov, aspect, nearPlane, farPlane);
}


// 카메라 뷰 행렬 계산
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
