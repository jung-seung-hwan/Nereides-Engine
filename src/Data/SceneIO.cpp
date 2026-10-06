#include "Data/SceneIO.h"
#include "Collision/Collision.h"
#include "Sandbox/MoveComponent.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <fstream>
#include <cmath>
#include <stdexcept>
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
                      {"enabled", object.enabled},
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
            else if (const auto* move = dynamic_cast<const MoveComponent*>(component))
                data = {{"type", "move"}, {"speed", move->speed}};
            else if (const auto* collider = dynamic_cast<const Collider*>(component))
                data = {{"type", "collider"},
                        {"shape", collider->shape == Shape::Box ? "box" : "sphere"},
                        {"offset", Vector(collider->offset)},
                        {"extents", Vector(collider->halfExtents)},
                        {"radius", collider->radius},
                        {"layer", collider->layer},
                        {"mask", collider->mask},
                        {"trigger", collider->trigger}};
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
            else
                throw std::runtime_error("Component requires explicit serialization registration");
            if (data["type"] == "move")
                Number(data["speed"], 0, 1000);
            if (data["type"] == "cube")
                for (const auto& channel : data["tint"])
                    Number(channel, 0, 1);
            if (data["type"] == "collider")
            {
                Number(data["radius"], 0, 10000);
                for (const auto& extent : data["extents"])
                    Number(extent, 0, 1e9);
            }
            if (!types.insert(data["type"].get<std::string>()).second)
                throw std::runtime_error("Duplicate serializable component");
            data["enabled"] = component->enabled;
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
        object.enabled = entry.at("enabled").get<bool>();
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
            else if (type == "move")
            {
                auto& move = object.Add<MoveComponent>();
                move.speed = float(Number(data.at("speed"), 0, 1000));
                component = &move;
            }
            else if (type == "collider")
            {
                auto& collider = object.Add<Collider>();
                component = &collider;
                const auto shape = data.at("shape").get<std::string>();
                if (shape != "box" && shape != "sphere")
                    throw std::runtime_error("Unknown collider shape");
                collider.shape = shape == "box" ? Shape::Box : Shape::Sphere;
                collider.offset = Vector(data.at("offset"));
                collider.halfExtents = Vector(data.at("extents"));
                if (collider.halfExtents.x < 0 || collider.halfExtents.y < 0 ||
                    collider.halfExtents.z < 0)
                    throw std::runtime_error("Negative collider extents");
                collider.radius = float(Number(data.at("radius"), 0, 10000));
                collider.layer = data.at("layer").get<unsigned>();
                collider.mask = data.at("mask").get<unsigned>();
                collider.trigger = data.at("trigger").get<bool>();
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
            else
                throw std::runtime_error("Unknown component type: " + type);
            component->enabled = data.at("enabled").get<bool>();
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
