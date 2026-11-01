#include "Renderer/RenderTestScene.h"
#include "Core/Log.h"
#include "Data/SceneIO.h"
#include "Graphics/D3D11Renderer.h"
#include "Graphics/RenderFrame.h"
#include "Platform/Win32Window.h"
#include "Renderer/SherlockSceneImport.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace nereides
{
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace


int RunRenderTestScene(const std::filesystem::path& scenePath)
{
    try
    {
        // 1) Sherlock 씬 → Nereides 씬 파일(에디터의 File > Open 으로 열 수 있음)
        ModelCache models;
        auto imported = ImportSherlockScene(scenePath, models);
        const auto output = std::filesystem::path("data/scenes") / scenePath.filename();
        SceneIO::Save(imported.scene, imported.player, output);

        // 2) 저장한 파일을 다시 읽어 RenderFrame 으로 모아 그림. 저장·로드·수집 경로를 함께 검사함.
        auto loaded = SceneIO::Load(output, models);
        Win32Window window;
        Require(window.Create(GetModuleHandleW(nullptr), SW_HIDE), "render test window");
        D3D11Renderer renderer;
        Require(renderer.Initialize(window.Handle(), window.Width(), window.Height()),
                "render test renderer");

        const float aspect = float(window.Width()) / window.Height();
        auto frame = CollectRenderFrame(loaded.scene, aspect, 0);
        Require(!frame.items.empty(), "render test scene has no draw items");

        // 조명 방향이 객체 회전을 거쳐도 원본 JSON 의 방향과 같아야 함
        std::ifstream file(scenePath);
        const auto source = nlohmann::json::parse(file);
        const auto& light = source.at("lights").at(0).at("direction");
        Require(frame.lights.size() == source.at("lights").size(), "render test light count");
        Require(std::abs(frame.lights[0].direction.x - light[0].get<float>()) < 1e-4f &&
                    std::abs(frame.lights[0].direction.y - light[1].get<float>()) < 1e-4f &&
                    std::abs(frame.lights[0].direction.z - light[2].get<float>()) < 1e-4f,
                "render test light direction");

        std::uint64_t hash = 0;
        renderer.RequestCapture("logs/render-test-scene.bmp");
        Require(renderer.RenderScene(frame, true, &hash), "render test scene visibility");
        // 한 번 더 그려도 같은 화면이어야 함 (캐시·텍스처 비동기 로딩 없음)
        std::uint64_t again = 0;
        Require(renderer.RenderScene(frame, true, &again), "render test scene second frame");
        Require(hash == again, "render test scene is not repeatable");

        Log::Write(LogLevel::Info, "Render test scene passed: " + std::to_string(frame.items.size()) +
                                       " draw items, lights=" + std::to_string(frame.lights.size()) +
                                       ", saved " + output.generic_string());
        return 0;
    }
    catch (const std::exception& error)
    {
        Log::Write(LogLevel::Error, std::string("Render test scene failed: ") + error.what());
        return 1;
    }
}
} // namespace nereides
