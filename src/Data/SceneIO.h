#pragma once
#include "Scene/Scene.h"
#include "Resource/Model.h"
#include <map>
namespace nereides
{
struct NumericParameter
{
    double value = 0, minimum = 0, maximum = 10000;
};
class Parameters final : public Component
{
public:
    std::map<std::string, NumericParameter> values;
};
struct LoadedScene
{
    Scene scene;
    ObjectId camera = 0, player = 0;
    Camera cameraData;
};
class SceneIO final
{
public:
    // Canonical in-memory form, also used by editor history and play/stop restoration.
    static std::string Encode(const Scene&, ObjectId camera, ObjectId player,
                              const Camera& cameraData = Camera{});
    static LoadedScene Decode(const std::string&, ModelCache& cache);
    static void Save(const Scene&, ObjectId camera, ObjectId player,
                     const std::filesystem::path& path, const Camera& cameraData = Camera{});
    static LoadedScene Load(const std::filesystem::path& path, ModelCache& cache);
};
} // namespace nereides
