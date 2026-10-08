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
    // Scene → JSON 문자열 변환
    static std::string Encode(const Scene&, ObjectId camera, ObjectId player,
                              const Camera& cameraData = Camera{});

    // JSON 문자열 → 새로운 장면 묶음
    static LoadedScene Decode(const std::string&, ModelCache& cache);

    // Encode한 문자열을 파일에 저장
    static void Save(const Scene&, ObjectId camera, ObjectId player,
                     const std::filesystem::path& path, const Camera& cameraData = Camera{});

    // 파일에서 장면 묶음을 읽어옴
    static LoadedScene Load(const std::filesystem::path& path, ModelCache& cache);
};
} // namespace nereides
