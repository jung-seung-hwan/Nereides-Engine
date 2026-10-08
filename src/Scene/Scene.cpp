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
// -> 추후 에디터 설정(태그) 기반으로 검색하는 기능 추가 필요 / 선형 기반 검색 개선 필요
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


// 실제 삭제 처리
// Scene::Update() 호출 후, 안전한 시점에 호출 (Scene 갱신 이후)
void Scene::Flush()
{
    if (m_updating)
        return;
    for (const auto& object : m_objects)
        std::erase_if(object->m_components,
                      [](const auto& component) { return component->m_removed; });
    std::erase_if(m_objects, [](const auto& object) { return object->m_destroyed; });
}


// 객체가 활성 상태인지 확인
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

// Scene 업데이트 처리
void Scene::Update(const FrameContext& context)
{
    // 중복 진입 불가 : Scene::Update() 호출 중에 다시 호출되면 예외 발생
    if (m_updating)
        throw std::logic_error("Recursive Scene::Update");

    m_updating = true;
    try
    {
        // Snapshot at the frame boundary: new objects/components begin next frame.
        // Scene의 모든 객체를 수집 후 업데이트 처리
        // -> 추후 에디터, 코드 우선순위 기반으로 객체 수집 및 업데이트 순서 조정 필요
        std::vector<std::pair<ObjectId, std::vector<Component*>>> batch;
        for (auto id : Objects())
        {
            std::vector<Component*> components;
            for (const auto& component : Find(id)->m_components)
                components.push_back(component.get());
            // Update () 호출 시점에 새로 생성된 객체/컴포넌트는 다음 프레임부터 업데이트 진행
            batch.emplace_back(id, std::move(components));
        }

        // 업데이트 실행 순서 : Logic -> Animation -> Late
        // 개별 객체별로 다 진행되면 다음 Phase로 넘어가는것이 아닌 전체 객체에 대해 Phase별로 진행
        for (auto phase : {UpdatePhase::Logic, UpdatePhase::Animation, UpdatePhase::Late})
            for (const auto& [id, components] : batch)
            {
                // 객체 유효, 활성 상태인지 검사
                auto* object = Find(id);
                if (!object || !Active(id))
                    continue;
                // 컴포넌트에 대해서도 유효, 활성 상태인지 검사 후 업데이트 진행
                for (auto* component : components)
                {
                    if (!Active(id))
                        break;
                    if (component->enabled && !component->m_removed && component->Phase() == phase)
                        component->Update(*object, *this, context);
                }
            }
    }
    // 오류 발생 즉시 남은 업데이트를 중단하고, 삭제 정리만 한 다음 오류를 호출자에게 전달
    catch (...)
    {
        m_updating = false;
        Flush();
        throw;
    }
    m_updating = false;
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
