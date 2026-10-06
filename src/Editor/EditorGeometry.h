#pragma once
#include "Resource/Model.h"
#include <DirectXCollision.h>
#include <algorithm>
#include <cfloat>

namespace nereides
{
inline DirectX::BoundingBox EditorBounds(const Scene& scene, ObjectId id)
{
    using namespace DirectX;
    RenderFrame frame;
    const auto* object = scene.Find(id);
    if (const auto* mesh = object->Get<MeshComponent>(); mesh && mesh->mesh)
    {
        DrawItem item;
        item.mesh = mesh->mesh;
        XMStoreFloat4x4(&item.world, scene.World(id));
        frame.items.push_back(std::move(item));
    }
    if (const auto* model = object->Get<ModelComponent>())
        AppendModelDraws(*model, scene.World(id), frame);
    XMVECTOR low = XMVectorReplicate(FLT_MAX), high = XMVectorReplicate(-FLT_MAX);
    bool any = false;
    for (const auto& item : frame.items)
        for (const auto& vertex : item.mesh->vertices)
        {
            auto p = XMLoadFloat3(&vertex.position);
            if (!item.bones.empty())
            {
                auto skinned = XMVectorZero();
                for (unsigned i = 0; i < 4; ++i)
                    if (vertex.weights[i] > 0 && vertex.bones[i] < item.bones.size())
                        skinned += XMVector3TransformCoord(
                                       p, XMLoadFloat4x4(&item.bones[vertex.bones[i]])) *
                                   vertex.weights[i];
                p = skinned;
            }
            p = XMVector3TransformCoord(p, XMLoadFloat4x4(&item.world));
            low = XMVectorMin(low, p);
            high = XMVectorMax(high, p);
            any = true;
        }
    BoundingBox box;
    if (any)
        BoundingBox::CreateFromPoints(box, low, high);
    else
    {
        XMStoreFloat3(&box.Center, XMVector3TransformCoord(XMVectorZero(), scene.World(id)));
        box.Extents = {.25f, .25f, .25f};
    }
    return box;
}
inline ObjectId PickEditorObject(const Scene& scene, DirectX::FXMVECTOR origin,
                                 DirectX::FXMVECTOR direction)
{
    ObjectId result = 0;
    float closest = FLT_MAX;
    for (auto id : scene.Objects())
    {
        const auto* object = scene.Find(id);
        if (!scene.Active(id) || (!object->Get<MeshComponent>() && !object->Get<ModelComponent>()))
            continue;
        float distance = 0;
        if (EditorBounds(scene, id).Intersects(origin, direction, distance) && distance < closest)
        {
            closest = distance;
            result = id;
        }
    }
    return result;
}
} // namespace nereides
