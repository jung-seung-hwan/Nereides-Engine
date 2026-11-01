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

// 카메라는 CameraComponent로 씬 객체에 저장한다. 화면에 쓸 카메라는 FindActiveCamera로 고른다.
struct LoadedScene
{
    Scene scene;
    ObjectId player = 0;
};

class SceneIO final
{
public:
    // 현재 저장 버전. 1은 카메라를 최상위 camera·cameraSettings로 저장하던 형식이며 읽기만 지원한다.
    static constexpr int Version = 2;

    // Canonical in-memory form, also used by editor history and play/stop restoration.
    // Scene → JSON 문자열 변환
    static std::string Encode(const Scene&, ObjectId player);

    // JSON 문자열 → 새로운 장면 묶음. version 1 파일은 카메라 역할 객체에 CameraComponent를 붙여 변환한다.
    static LoadedScene Decode(const std::string&, ModelCache& cache);

    // Encode한 문자열을 파일에 저장
    static void Save(const Scene&, ObjectId player, const std::filesystem::path& path);

    // 파일에서 장면 묶음을 읽어옴
    static LoadedScene Load(const std::filesystem::path& path, ModelCache& cache);
};
} // namespace nereides
