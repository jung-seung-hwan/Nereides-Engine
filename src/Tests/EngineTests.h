#pragma once
namespace nereides
{
int RunEngineTests();
int RunEditorWorkflowTest();
} // namespace nereides
#include <filesystem>
namespace nereides
{
int RunModelTest(const std::filesystem::path& path);
}
