#include "Graphics/RenderFrame.h"
#include "Resource/Model.h"
#include "Renderer/RenderComponents.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace nereides
{
namespace
{
// 켜진 LightComponent·EnvironmentComponent를 frame에 반영. 없으면 RenderFrame의 기본값을 그대로 둠.
void CollectLighting(const Scene& scene, RenderFrame& frame)
{
    using namespace DirectX;
    std::vector<LightData> lights;
    bool environmentFound = false;

    for (auto id : scene.Objects())
    {
        const auto* object = scene.Find(id);
        if (!scene.Active(id))
            continue;

        if (const auto* light = object->Get<LightComponent>(); light && light->Enabled())
        {
            const auto world = scene.World(id);
            XMFLOAT3 direction;
            XMStoreFloat3(&direction,
                          XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), world)));
            XMFLOAT3 position;
            XMStoreFloat3(&position, world.r[3]);

            LightData data;
            data.position = position;
            data.direction = direction;
            data.color = light->color;
            data.intensity = light->intensity;
            data.range = light->range;
            data.attenuation = light->attenuation;
            data.innerConeCos = light->innerConeCos;
            data.outerConeCos = light->outerConeCos;
            data.type = static_cast<uint32_t>(light->type);
            lights.push_back(data);
        }

        if (const auto* environment = object->Get<EnvironmentComponent>();
            environment && environment->Enabled() && !environmentFound)
        {
            environmentFound = true;
            auto& target = frame.environment;
            switch (environment->preset)
            {
            case EnvironmentPreset::BuiltinSky:
                target.skybox = "builtin:sky";
                target.irradiance = target.specular = target.brdfLut = "";
                break;
            case EnvironmentPreset::CubemapSample:
                target.skybox = "Cubemap/CubemapSampleEnvHDR.dds";
                target.irradiance = "Cubemap/CubemapSampleDiffuseHDR.dds";
                target.specular = "Cubemap/CubemapSampleSpecularHDR.dds";
                target.brdfLut = "Cubemap/CubemapSampleBrdf.dds";
                break;
            case EnvironmentPreset::None:
                target.skybox = target.irradiance = target.specular = target.brdfLut = "";
                break;
            }
            target.iblIntensity = environment->iblIntensity;
            target.exposure = environment->exposure;
            target.toneMapping = environment->toneMapping;
            target.post.blurEnabled = environment->blurEnabled;
            target.post.blurRadius = environment->blurRadius;
            target.post.bloomEnabled = environment->bloomEnabled;
            target.post.bloomThreshold = environment->bloomThreshold;
            target.post.bloomKnee = environment->bloomKnee;
            target.post.bloomIntensity = environment->bloomIntensity;
            target.post.greyscale = environment->greyscale;
            target.ambientColor = environment->ambientColor;
            frame.clearColor = {environment->clearColor.x, environment->clearColor.y,
                                environment->clearColor.z, 1.f};
        }
    }

    if (!lights.empty())
        frame.lights = std::move(lights);
}
} // namespace


// Scene에서 그릴 정보를 수집해 렌더러에 전달할 RenderFrame 생성·반환 -> 해당 정보를 Application에서 렌더러에 전달
RenderFrame CollectRenderFrame(const Scene& scene, float aspect, double time,
                               ObjectId cameraObject)
{
    RenderFrame frame;
    if (!cameraObject)
        cameraObject = FindActiveCamera(scene);
    const auto* cameraOwner = scene.Find(cameraObject);
    const auto* camera = cameraOwner ? cameraOwner->Get<CameraComponent>() : nullptr;
    if (cameraObject && !camera)
        throw std::invalid_argument("Camera object has no CameraComponent");
    // 카메라가 없으면 원점에서 +Z를 보는 기본 투영을 쓴다. 에디터는 이 값을 편집 시점으로 덮어쓴다.
    DirectX::XMStoreFloat4x4(&frame.view, camera ? CameraView(scene, cameraObject)
                                                 : DirectX::XMMatrixIdentity());
    DirectX::XMStoreFloat4x4(&frame.projection,
                             (camera ? *camera : CameraComponent{}).Projection(aspect));

    frame.time = time;

    for (auto id : scene.Objects())
    {
        const auto* object = scene.Find(id);
        if (scene.Active(id))
            if (const auto* model = object->Get<ModelComponent>(); model && model->Enabled())
                AppendModelDraws(*model, scene.World(id), frame);
        const auto* mesh = object->Get<MeshComponent>();
        if (!scene.Active(id) || !mesh || !mesh->Enabled() || !mesh->mesh)
            continue;
        DrawItem item;
        item.mesh = mesh->mesh;
        item.material = mesh->RenderMaterial();
        DirectX::XMStoreFloat4x4(&item.world, scene.World(id));
        frame.items.push_back(std::move(item));
    }

    CollectLighting(scene, frame);
    return frame;
}


// MeshComponent의 tint를 baseColor로, metallic·roughness를 PBR 값으로 갖는 재질
std::shared_ptr<const Material> MeshComponent::RenderMaterial() const
{
    if (!m_material || m_materialTint.x != tint.x || m_materialTint.y != tint.y ||
        m_materialTint.z != tint.z || m_materialTint.w != tint.w || m_materialMetallic != metallic ||
        m_materialRoughness != roughness)
    {
        auto material = std::make_shared<Material>();
        material->name = "MeshComponent";
        material->baseColor = tint;
        material->metallic = metallic;
        material->roughness = roughness;
        m_material = std::move(material);
        m_materialTint = tint;
        m_materialMetallic = metallic;
        m_materialRoughness = roughness;
    }

    return m_material;
}


// 한 변이 1인 큐브. 면마다 정점 4개(노멀·UV·탄젠트가 면마다 달라서). 바깥에서 볼 때 시계방향이 앞면.
std::shared_ptr<const MeshData> MakeCube()
{
    using namespace DirectX;
    auto mesh = std::make_shared<MeshData>();
    mesh->builtinCube = true;

    constexpr XMFLOAT3 normals[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    for (const auto& value : normals)
    {
        const XMVECTOR n = XMLoadFloat3(&value);
        // 화면 아래(v가 늘어나는 방향): 위쪽 면은 +Z를 위로 보고, 나머지는 +Y를 위로 봄
        const XMVECTOR up = value.y != 0 ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
        const XMVECTOR down = XMVectorNegate(up);
        const XMVECTOR right = XMVector3Cross(down, n); // cross(right, down) == n

        constexpr float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}; // 좌상, 우상, 우하, 좌하
        const auto first = static_cast<unsigned>(mesh->vertices.size());

        for (const auto& corner : corners)
        {
            MeshVertex v;
            XMStoreFloat3(&v.position, (n + right * corner[0] + down * corner[1]) * .5f);
            v.normal = value;
            v.uv = {corner[0] * .5f + .5f, corner[1] * .5f + .5f};
            XMStoreFloat4(&v.tangent, XMVectorSetW(right, 1.f));
            mesh->vertices.push_back(v);
        }

        mesh->indices.insert(mesh->indices.end(),
                             {first, first + 1, first + 2, first, first + 2, first + 3});
    }

    return mesh;
}


// 정점 하나. 탄젠트는 노멀에 직교화하고, w는 cross(N, T)가 B와 같은 쪽이면 +1 (Sherlock 정점 규칙)
static MeshVertex SphereVertex(DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 normal, float u, float v,
                               DirectX::XMFLOAT3 tangent, DirectX::XMFLOAT3 bitangent)
{
    using namespace DirectX;
    MeshVertex vertex;
    vertex.position = position;
    vertex.normal = normal;
    vertex.uv = {u, v};

    const auto n = XMVector3Normalize(XMLoadFloat3(&normal));
    auto t = XMLoadFloat3(&tangent);
    t = XMVector3Normalize(t - n * XMVector3Dot(n, t));
    const float handedness =
        XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), XMLoadFloat3(&bitangent))) < 0 ? -1.f : 1.f;
    XMStoreFloat4(&vertex.tangent, XMVectorSetW(t, handedness));
    return vertex;
}


// Sherlock Mesh::CreateSphere 와 같은 정점·인덱스 배치
std::shared_ptr<const MeshData> MakeSphere(float radius, unsigned slices, unsigned stacks)
{
    using namespace DirectX;
    slices = std::max(slices, 3u);
    stacks = std::max(stacks, 2u);
    auto mesh = std::make_shared<MeshData>();
    mesh->sphereRadius = radius;
    mesh->sphereSlices = slices;
    mesh->sphereStacks = stacks;
    const float phiStep = XM_PI / stacks, thetaStep = XM_2PI / slices;

    mesh->vertices.push_back(SphereVertex({0, radius, 0}, {0, 1, 0}, .5f, 0, {1, 0, 0}, {0, 0, -1}));
    for (unsigned i = 1; i < stacks; ++i)
    {
        const float phi = i * phiStep;
        for (unsigned j = 0; j <= slices; ++j)
        {
            const float theta = j * thetaStep;
            const XMFLOAT3 n{std::sin(phi) * std::cos(theta), std::cos(phi),
                             std::sin(phi) * std::sin(theta)};
            mesh->vertices.push_back(SphereVertex(
                {radius * n.x, radius * n.y, radius * n.z}, n, theta / XM_2PI, phi / XM_PI,
                {-std::sin(theta), 0, std::cos(theta)},
                {std::cos(phi) * std::cos(theta), -std::sin(phi), std::cos(phi) * std::sin(theta)}));
        }
    }
    mesh->vertices.push_back(SphereVertex({0, -radius, 0}, {0, -1, 0}, .5f, 1, {1, 0, 0}, {0, 0, 1}));

    for (unsigned i = 1; i <= slices; ++i)
        mesh->indices.insert(mesh->indices.end(), {0, i + 1, i});

    const unsigned ring = slices + 1;
    for (unsigned i = 0; i < stacks - 2; ++i)
        for (unsigned j = 0; j < slices; ++j)
        {
            const unsigned a = 1 + i * ring + j, b = 1 + (i + 1) * ring + j;
            mesh->indices.insert(mesh->indices.end(), {a, a + 1, b, b, a + 1, b + 1});
        }

    const unsigned south = static_cast<unsigned>(mesh->vertices.size() - 1), base = south - ring;
    for (unsigned i = 0; i < slices; ++i)
        mesh->indices.insert(mesh->indices.end(), {south, base + i, base + i + 1});
    return mesh;
}
} // namespace nereides
