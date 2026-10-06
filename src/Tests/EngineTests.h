#pragma once
namespace nereides
{
int RunEngineTests();
}
#include <filesystem>
namespace nereides
{
int RunModelTest(const std::filesystem::path& path);
}
