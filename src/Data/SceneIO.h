#pragma once
#include "Scene/Scene.h"
#include "Resource/Model.h"
#include <map>
namespace nereides
{
struct NumericParameter { double value=0, minimum=0, maximum=10000; };
class Parameters final : public Component
{
public:
    std::map<std::string,NumericParameter> values;
};
struct LoadedScene { Scene scene; ObjectId camera=0,player=0; Camera cameraData; };
class SceneIO final
{
public:
    static void Save(const Scene&,ObjectId camera,ObjectId player,const std::filesystem::path& path,const Camera& cameraData=Camera{});
    static LoadedScene Load(const std::filesystem::path& path,ModelCache& cache);
};
}
