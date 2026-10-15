#include "Data/SceneIO.h"
#include "Collision/Collision.h"
#include "Reflection/ComponentRegistry.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <fstream>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace nereides
{
namespace
{
using Json = nlohmann::json;
Json Vector(DirectX::XMFLOAT3 v)
{
    return {v.x, v.y, v.z};
}


DirectX::XMFLOAT3 Vector(const Json& j)
{
    if (!j.is_array() || j.size() != 3)
        throw std::runtime_error("Expected three vector coordinates");

    DirectX::XMFLOAT3 value{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
        throw std::runtime_error("Nonfinite coordinate");

    return value;
}


double Number(const Json& j, double minimum, double maximum)
{
    double value = j.get<double>();
    if (!std::isfinite(value) || value < minimum || value > maximum)
        throw std::runtime_error("Numeric setting out of range");

    return value;
}
Json EncodeProperties(const Component& component, const ComponentType& type)
{
    Json data = {{"type", type.name}};
    for (const auto& property : type.properties)
    {
        const auto value = property.read(component);
        property.Validate(value);
        data[property.name] = std::visit(
            [](const auto& v) -> Json {
                if constexpr (std::is_same_v<std::decay_t<decltype(v)>, DirectX::XMFLOAT3>)
                    return Vector(v);
                else
                    return v;
            },
            value);
    }
    return data;
}

std::unique_ptr<Component> DecodeProperties(const Json& data, const ComponentType& type)
{
    auto component = type.create();
    for (const auto& property : type.properties)
    {
        // 새 필드가 추가되기 전의 씬은 등록된 기본 설정을 사용한다.
        PropertyValue value = property.defaultValue;
        if (data.contains(property.name))
        {
            const auto& field = data.at(property.name);
            value = std::visit(
                [&](const auto& defaultValue) -> PropertyValue {
                    using V = std::decay_t<decltype(defaultValue)>;
                    if constexpr (std::is_same_v<V, DirectX::XMFLOAT3>)
                        return Vector(field);
                    else if constexpr (std::is_same_v<V, std::string>)
                        return field.get<std::string>();
                    else if constexpr (std::is_same_v<V, unsigned>)
                    {
                        if (!field.is_number_integer())
                            throw std::runtime_error("Expected unsigned property: " +
                                                     property.name);
                        return unsigned(Number(field, 0, std::numeric_limits<unsigned>::max()));
                    }
                    else if constexpr (std::is_same_v<V, bool>)
                        return field.get<bool>();
                    else if constexpr (std::is_same_v<V, int>)
                    {
                        if (!field.is_number_integer())
                            throw std::runtime_error("Expected integer property: " + property.name);
                        return int(Number(field, std::numeric_limits<int>::min(),
                                          std::numeric_limits<int>::max()));
                    }
                    else
                    {
                        if (!field.is_number())
                            throw std::runtime_error("Expected numeric property: " + property.name);
                        return float(Number(field, property.minimum, property.maximum));
                    }
                },
                property.defaultValue);
        }
        property.Set(*component, value);
    }
    return component;
}
} // namespace


std::string SceneIO::Encode(const Scene& scene, ObjectId camera, ObjectId player,
                            const Camera& cameraData)
{
    if (!scene.Find(camera) || (player && !scene.Find(player)))
        throw std::runtime_error("Scene role references missing object");

    std::map<ObjectId, ObjectId> ids{{0, 0}};

    for (auto id : scene.Objects())
        ids[id] = ids.size();
    Json file = {{"version", 1},
                 {"camera", ids.at(camera)},
                 {"player", ids.at(player)},
                 {"objects", Json::array()}};
    cameraData.Projection(1);
    cameraData.View(scene, camera);
    file["cameraSettings"] = {{"fov", cameraData.verticalFov},
                              {"near", cameraData.nearPlane},
                              {"far", cameraData.farPlane}};
    for (auto id : scene.Objects())
    {
        const auto& object = *scene.Find(id);
        const auto& t = object.transform;
        if (t.scale.x <= 0 || t.scale.y <= 0 || t.scale.z <= 0)
            throw std::runtime_error("Scene scale must be positive");

        Json entry = {{"id", ids.at(id)},
                      {"parent", ids.at(object.Parent())},
                      {"name", object.Name()},
                      {"enabled", object.Enabled()},
                      {"position", Vector(t.position)},
                      {"rotation", Vector(t.rotation)},
                      {"scale", Vector(t.scale)},
                      {"components", Json::array()}};
        std::set<std::string> types;

        for (const auto* component : object.Components())
        {
            Json data;
            if (const auto* mesh = dynamic_cast<const MeshComponent*>(component))
            {
                if (!mesh->mesh || !mesh->mesh->builtinCube)
                    throw std::runtime_error("Raw mesh has no serializable source");
                // This path supports only engine-created cube components; arbitrary meshes use
                // ModelComponent.
                data = {{"type", "cube"},
                        {"tint", {mesh->tint.x, mesh->tint.y, mesh->tint.z, mesh->tint.w}}};
            }
            else if (const auto* model = dynamic_cast<const ModelComponent*>(component))
            {
                std::error_code error;
                auto relative = std::filesystem::relative(
                    std::filesystem::path(std::u8string(
                        reinterpret_cast<const char8_t*>(model->model->path.c_str()))),
                    std::filesystem::current_path(), error);
                const auto utf8 = relative.generic_u8string();
                data = {
                    {"type", "model"},
                    {"path", error ? model->model->path : std::string(utf8.begin(), utf8.end())}};
            }
            else if (const auto* parameters = dynamic_cast<const Parameters*>(component))
            {
                data = {{"type", "parameters"}, {"values", Json::object()}};

                for (const auto& [name, p] : parameters->values)
                {
                    if (!std::isfinite(p.value) || !std::isfinite(p.minimum) ||
                        !std::isfinite(p.maximum) || p.minimum > p.maximum || p.value < p.minimum ||
                        p.value > p.maximum)
                        throw std::runtime_error("Invalid parameter: " + name);

                    data["values"][name] = {
                        {"value", p.value}, {"min", p.minimum}, {"max", p.maximum}};
                }
            }
            else if (const auto* type = ComponentTypes().Find(*component))
                data = EncodeProperties(*component, *type);
            else
                throw std::runtime_error("Component requires explicit serialization registration");

            if (data["type"] == "cube")
                for (const auto& channel : data["tint"])
                    Number(channel, 0, 1);

            if (!types.insert(data["type"].get<std::string>()).second)
                throw std::runtime_error("Duplicate serializable component");

            data["enabled"] = component->Enabled();
            entry["components"].push_back(std::move(data));
        }

        file["objects"].push_back(std::move(entry));
    }

    const auto flattened = file.flatten();

    for (const auto& [key, value] : flattened.items())
        if (value.is_number_float() && !std::isfinite(value.get<double>()))
            throw std::runtime_error("Nonfinite scene value at " + key);
    return file.dump(2);
}


/*
Encode로 문자열 생성
 → 필요한 폴더 생성
 → 대상 경로.tmp에 기록
 → 기록 성공 확인
 → 임시 파일을 최종 경로로 교체
*/
void SceneIO::Save(const Scene& scene, ObjectId camera, ObjectId player,
                   const std::filesystem::path& path, const Camera& cameraData)
{
    const auto encoded = Encode(scene, camera, player, cameraData);
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary);
        stream << encoded;
        stream.flush();
        if (!stream)
            throw std::runtime_error("Scene write failed");
    }

    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Scene file replace failed");
}


LoadedScene SceneIO::Load(const std::filesystem::path& path, ModelCache& cache)
{
    if (std::filesystem::file_size(path) > 16 * 1024 * 1024)
        throw std::runtime_error("Scene file exceeds 16 MiB");

    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open scene");

    return Decode(std::string(std::istreambuf_iterator<char>(input), {}), cache);
}


/*
문자열 크기 검사
 → JSON 해석
 → 버전 1 확인
 → 객체 목록과 개수 검사
 → 모든 객체·컴포넌트 생성
 → 부모 관계 연결
 → 월드 변환 검사
 → 카메라·플레이어 역할 복원
 → 카메라 설정 검사
 → LoadedScene 반환
*/
LoadedScene SceneIO::Decode(const std::string& text, ModelCache& cache)
{
    if (text.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Scene exceeds 16 MiB");

    const auto file = Json::parse(text);
    if (file.at("version") != 1)
        throw std::runtime_error("Unsupported scene version");

    const auto& entries = file.at("objects");
    if (!entries.is_array() || entries.size() > 10000)
        throw std::runtime_error("Invalid scene object list");

    LoadedScene result;
    std::map<ObjectId, ObjectId> remap;

    for (const auto& entry : entries)
    {
        auto old = entry.at("id").get<ObjectId>();
        if (!old || remap.contains(old))
            throw std::runtime_error("Duplicate/zero object id");

        auto& object = result.scene.Create(entry.at("name").get<std::string>());
        remap[old] = object.Id();
        object.SetEnabled(entry.at("enabled").get<bool>());
        object.transform.position = Vector(entry.at("position"));
        object.transform.rotation = Vector(entry.at("rotation"));
        object.transform.scale = Vector(entry.at("scale"));
        if (object.transform.scale.x <= 0 || object.transform.scale.y <= 0 ||
            object.transform.scale.z <= 0)
            throw std::runtime_error("Scene scale must be positive");

        std::set<std::string> types;

        for (const auto& data : entry.at("components"))
        {
            auto type = data.at("type").get<std::string>();
            if (!types.insert(type).second)
                throw std::runtime_error("Duplicate component type");

            Component* component = nullptr;
            if (type == "cube")
            {
                auto& mesh = object.Add<MeshComponent>(MakeCube());
                component = &mesh;
                const auto& tint = data.at("tint");
                if (tint.size() != 4)
                    throw std::runtime_error("Invalid tint");

                mesh.tint = {float(Number(tint[0], 0, 1)), float(Number(tint[1], 0, 1)),
                             float(Number(tint[2], 0, 1)), float(Number(tint[3], 0, 1))};
            }
            else if (type == "model")
            {
                const auto path = data.at("path").get<std::string>();
                component = &object.Add<ModelComponent>(cache.Load(std::filesystem::path(
                    std::u8string(reinterpret_cast<const char8_t*>(path.c_str())))));
            }
            else if (type == "parameters")
            {
                auto& parameters = object.Add<Parameters>();
                component = &parameters;

                for (const auto& [name, value] : data.at("values").items())
                {
                    const double minimum = Number(value.at("min"), -1e9, 1e9),
                                 maximum = Number(value.at("max"), minimum, 1e9);
                    parameters.values[name] = {Number(value.at("value"), minimum, maximum), minimum,
                                               maximum};
                }
            }
            else if (const auto* registered = ComponentTypes().Find(type))
                component = &object.AddComponent(DecodeProperties(data, *registered));
            else
                throw std::runtime_error("Unknown component type: " + type);

            component->SetEnabled(data.at("enabled").get<bool>());
        }
    }

    for (const auto& entry : entries)
    {
        auto old = entry.at("id").get<ObjectId>(), parent = entry.at("parent").get<ObjectId>();
        if (parent &&
            (!remap.contains(parent) || !result.scene.SetParent(remap.at(old), remap.at(parent))))
            throw std::runtime_error("Invalid parent/cycle");
    }

    for (auto id : result.scene.Objects())
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMStoreFloat4x4(&world, result.scene.World(id));

        for (const auto& row : world.m)
            for (float value : row)
                if (!std::isfinite(value))
                    throw std::runtime_error("World transform overflow");
    }

    const auto camera = file.at("camera").get<ObjectId>(),
               player = file.at("player").get<ObjectId>();
    if (!remap.contains(camera) || (player && !remap.contains(player)))
        throw std::runtime_error("Missing camera/player role");

    result.camera = remap.at(camera);
    result.player = player ? remap.at(player) : 0;
    const auto& cameraSettings = file.at("cameraSettings");
    result.cameraData.verticalFov = float(Number(cameraSettings.at("fov"), .01, 3.13));
    result.cameraData.nearPlane = float(Number(cameraSettings.at("near"), .001, 1e6));
    result.cameraData.farPlane = float(Number(cameraSettings.at("far"), .001, 1e6));
    result.cameraData.Projection(1);
    result.cameraData.View(
        result.scene,
        result.camera); // Reject singular inherited camera transforms before replacing live scene.
    return result;
}
} // namespace nereides
