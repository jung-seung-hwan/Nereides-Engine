#pragma once
#include <filesystem>

namespace nereides
{
// Sherlock 씬 JSON(Projects/*/Assets/Scenes/*.json)을 Nereides 씬으로 바꿔 data/scenes/ 에 저장하고, 그 파일을 다시 읽어 한 장 그려 logs/render-test-scene.bmp 로 저장함.
// 변환 결과는 에디터의 File > Open 으로 열 수 있음. 저장·로드·RenderFrame 수집·렌더 경로를 함께 검사하는 시험임.
int RunRenderTestScene(const std::filesystem::path& scenePath);
} // namespace nereides
