#include "Graphics/RenderFrame.h"
#include "Resource/Model.h"
namespace nereides
{
RenderFrame CollectRenderFrame(const Scene& scene, ObjectId cameraObject, const Camera& camera, float aspect, double time)
{
    RenderFrame frame;
    DirectX::XMStoreFloat4x4(&frame.view, camera.View(scene, cameraObject));
    DirectX::XMStoreFloat4x4(&frame.projection, camera.Projection(aspect));
    frame.time = time;
    for (auto id : scene.Objects())
    {
        const auto* object = scene.Find(id);
        if(scene.Active(id))
            if(const auto* model=object->Get<ModelComponent>(); model && model->enabled)
                AppendModelDraws(*model,scene.World(id),frame);
        const auto* mesh = object->Get<MeshComponent>();
        if (!scene.Active(id) || !mesh || !mesh->enabled || !mesh->mesh) continue;
        DrawItem item; item.mesh = mesh->mesh; item.tint = mesh->tint;
        DirectX::XMStoreFloat4x4(&item.world, scene.World(id));
        frame.items.push_back(std::move(item));
    }
    return frame;
}
std::shared_ptr<const MeshData> MakeCube()
{
    auto mesh = std::make_shared<MeshData>();
    for (unsigned i = 0; i < 8; ++i)
    {
        MeshVertex v;
        v.position = { (i&1) ? .5f : -.5f, (i&2) ? .5f : -.5f, (i&4) ? .5f : -.5f };
        mesh->vertices.push_back(v);
    }
    mesh->indices = {0,2,1,1,2,3,4,5,6,5,7,6,0,1,4,1,5,4,2,6,3,3,6,7,0,4,2,2,4,6,1,3,5,3,7,5};
    return mesh;
}
}
