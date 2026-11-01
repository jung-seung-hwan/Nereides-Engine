#include "Renderer/SherlockSceneImport.h"
#include "Core/Log.h"
#include "Graphics/RenderFrame.h"
#include "Renderer/RenderComponents.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace nereides
{
using namespace DirectX;
using Json = nlohmann::json;
namespace
{
XMFLOAT3 Float3(const Json& parent, const char* key, XMFLOAT3 fallback)
{
    const auto found = parent.find(key);
    if (found == parent.end() || !found->is_array() || found->size() < 3)
        return fallback;
    return {(*found)[0].get<float>(), (*found)[1].get<float>(), (*found)[2].get<float>()};
}


const Json& ArrayAt(const Json& parent, const char* key)
{
    static const Json empty = Json::array();
    const auto found = parent.find(key);
    return found != parent.end() && found->is_array() ? *found : empty;
}


std::string Slashes(std::string name)
{
    if (name.rfind("asset:", 0) == 0)
        name.erase(0, 6);
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}


// 빛이 진행하는 방향 → 객체 회전(라디안). 객체의 앞쪽(+Z)이 그 방향을 향함.
XMFLOAT3 RotationFacing(XMFLOAT3 direction)
{
    const auto n = XMVector3Normalize(XMLoadFloat3(&direction));
    XMFLOAT3 f;
    XMStoreFloat3(&f, n);
    return {-std::asin(std::clamp(f.y, -1.f, 1.f)), std::atan2(f.x, f.z), 0};
}


float Unit(const Json& value, float fallback)
{
    return std::clamp(value.is_number() ? value.get<float>() : fallback, 0.f, 1.f);
}
} // namespace


LoadedScene ImportSherlockScene(const std::filesystem::path& path, ModelCache& models)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Sherlock scene file is missing");
    const Json source = Json::parse(file);

    LoadedScene result;
    const auto& meshes = ArrayAt(source, "meshes");
    const auto& materials = ArrayAt(source, "materials");
    std::vector<std::shared_ptr<const MeshData>> spheres(meshes.size());
    const Json* cameraObject = nullptr;
    const Json* cameraComponent = nullptr;
    ObjectId cameraId = 0;

    for (const auto& entry : ArrayAt(source, "objects"))
    {
        auto& object = result.scene.Create(entry.value("name", "Object"));
        object.transform.position = Float3(entry, "position", {0, 0, 0});
        object.transform.rotation = Float3(entry, "rotation", {0, 0, 0});
        object.transform.scale = Float3(entry, "scale", {1, 1, 1});

        for (const auto& component : ArrayAt(entry, "components"))
            if (component.value("type", "") == "CameraComponent" && component.value("enabled", true) &&
                (!cameraComponent ||
                 component.value("priority", 0) > cameraComponent->value("priority", 0)))
            {
                cameraObject = &entry;
                cameraComponent = &component;
                cameraId = object.Id();
            }

        const int meshIndex = entry.value("mesh", -1);
        if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size()))
            continue;

        const auto& mesh = meshes[meshIndex];
        const auto kind = mesh.value("type", "");
        if (kind == "model")
        {
            auto name = Slashes(mesh.value("path", ""));
            if (name.rfind("Models/", 0) == 0)
                name = "models/" + name.substr(7);
            object.Add<ModelComponent>(models.Load(std::filesystem::path("assets") / name));
        }
        else if (kind == "sphere")
        {
            if (!spheres[meshIndex])
            {
                const auto& params = mesh.at("params");
                spheres[meshIndex] = MakeSphere(params.at(0).get<float>(), mesh.value("a", 32u),
                                                mesh.value("b", 16u));
            }
            auto& component = object.Add<MeshComponent>(spheres[meshIndex]);
            const int materialIndex = entry.value("material", -1);
            if (materialIndex >= 0 && materialIndex < static_cast<int>(materials.size()))
            {
                const auto& material = materials[materialIndex];
                const auto color = material.value("baseColor", Json::array({1, 1, 1, 1}));
                if (color.is_array() && color.size() >= 4)
                    component.tint = {Unit(color[0], 1), Unit(color[1], 1), Unit(color[2], 1),
                                      Unit(color[3], 1)};
                component.metallic = Unit(material.value("metallic", Json()), 0);
                component.roughness = Unit(material.value("roughness", Json()), .5f);
            }
        }
        else
            Log::Write(LogLevel::Warning, "Sherlock import skipped unsupported mesh type: " + kind);
    }

    // 카메라: CameraComponent 객체. 없으면 씬 파일의 camera(위치, yaw, pitch)로 객체를 만듦.
    // 우선순위가 가장 높은 Sherlock 카메라 하나만 Nereides CameraComponent로 옮긴다.
    if (cameraObject)
    {
        auto& camera = result.scene.Find(cameraId)->Add<CameraComponent>();
        camera.verticalFovDegrees = cameraComponent->value("fovY", 45.f);
        camera.nearPlane = cameraComponent->value("nearZ", .1f);
        camera.farPlane = cameraComponent->value("farZ", 1000.f);
        camera.priority = cameraComponent->value("priority", 0);
    }
    else
    {
        const auto& camera = source.value("camera", Json::object());
        auto& object = result.scene.Create("Main Camera");
        object.transform.position = Float3(camera, "position", {0, 3, -9});
        object.transform.rotation = {camera.value("pitch", 0.f), camera.value("yaw", 0.f), 0};
        object.Add<CameraComponent>().farPlane = 1000.f;
    }

    int lightNumber = 0;
    for (const auto& light : ArrayAt(source, "lights"))
    {
        const auto type = light.value("type", 0);
        static const char* names[] = {"Directional Light", "Point Light", "Spot Light"};
        auto& object = result.scene.Create(std::string(names[std::clamp(type, 0, 2)]) +
                                           (lightNumber++ ? " " + std::to_string(lightNumber) : ""));
        object.transform.position = Float3(light, "position", {0, 10, -10});
        object.transform.rotation = RotationFacing(Float3(light, "direction", {0, -.4472136f, .8944272f}));

        auto& component = object.Add<LightComponent>();
        component.type = static_cast<LightType>(std::clamp(type, 0, 2));
        component.color = Float3(light, "color", component.color);
        component.attenuation = Float3(light, "attenuation", component.attenuation);
        component.intensity = light.value("intensity", component.intensity);
        component.range = light.value("range", component.range);
        component.innerConeCos = light.value("innerConeCos", component.innerConeCos);
        component.outerConeCos = light.value("outerConeCos", component.outerConeCos);
    }

    auto& environmentObject = result.scene.Create("Environment");
    auto& environment = environmentObject.Add<EnvironmentComponent>();
    environment.ambientColor = Float3(source, "ambientColor", environment.ambientColor);
    if (const auto clear = ArrayAt(source, "clearColor"); clear.size() >= 3)
        environment.clearColor = {Unit(clear[0], 0), Unit(clear[1], 0), Unit(clear[2], 0)};
    if (const auto found = source.find("environment"); found != source.end() && found->is_object())
    {
        const auto skybox = Slashes(found->value("skybox", ""));
        if (skybox.size() >= 23 && skybox.ends_with("CubemapSampleEnvHDR.dds"))
            environment.preset = EnvironmentPreset::CubemapSample;
        else if (skybox.empty())
            environment.preset = EnvironmentPreset::None;
        else if (skybox != "builtin:sky")
            Log::Write(LogLevel::Warning, "Sherlock import has no preset for skybox: " + skybox);
        environment.iblIntensity = found->value("iblIntensity", 1.f);
        environment.exposure = found->value("exposure", 1.f);
        environment.toneMapping = found->value("toneMapping", false);
        // 2026/10/23: 후처리. 키가 없는 옛 장면은 전부 꺼진 기본값
        if (const auto post = found->find("postProcess"); post != found->end() && post->is_object())
        {
            environment.blurEnabled = post->value("blurEnabled", environment.blurEnabled);
            environment.blurRadius = post->value("blurRadius", environment.blurRadius);
            environment.bloomEnabled = post->value("bloomEnabled", environment.bloomEnabled);
            environment.bloomThreshold = post->value("bloomThreshold", environment.bloomThreshold);
            environment.bloomKnee = post->value("bloomKnee", environment.bloomKnee);
            environment.bloomIntensity = post->value("bloomIntensity", environment.bloomIntensity);
            environment.greyscale = post->value("greyscale", environment.greyscale);
        }
    }
    return result;
}
} // namespace nereides
