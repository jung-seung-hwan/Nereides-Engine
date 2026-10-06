#include "Tests/EngineTests.h"
#include "Editor/Editor.h"
#include "Platform/Win32Window.h"
#include "Core/Log.h"
#include <imgui.h>
#include <fstream>
#include <cmath>
namespace nereides
{
struct EditorTestAccess
{
    static void Prepare(Editor& editor, const std::filesystem::path& model)
    {
        strcpy_s(editor.m_scenePath, "logs/editor-workflow-scene.json");
        editor.m_hasScenePath = true;
        editor.m_assets = {model};
    }
    static ObjectId Selection(const Editor& editor)
    {
        return editor.m_selected;
    }
    static float Distance(const Editor& editor)
    {
        return editor.m_distance;
    }
    static bool Dirty(const Editor& editor)
    {
        return editor.m_dirty;
    }
};
int RunEditorWorkflowTest()
{
    try
    {
        auto require = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto modelPath = std::filesystem::path(L"logs/편집 검증 모델.obj");
        std::filesystem::create_directories("logs");
        {
            std::ofstream model(modelPath);
            model << "o TestModel\nv -1 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n";
        }
        Win32Window window;
        require(window.Create(GetModuleHandleW(nullptr), SW_HIDE), "workflow window");
        D3D11Renderer renderer;
        require(renderer.Initialize(window.Handle(), 1280, 720), "workflow renderer");
        Editor editor;
        require(editor.Initialize(window.Handle(), renderer), "workflow editor");
        EditorTestAccess::Prepare(editor, modelPath);
        renderer.SetOverlay([&] { editor.Render(); });
        Scene scene;
        ObjectId cameraId = scene.Create("camera").Id(), player = scene.Create("player").Id();
        scene.Find(cameraId)->transform.position = {0, 1, -6};
        scene.Find(player)->transform.position = {0, 1, 0};
        scene.Find(player)->Add<MeshComponent>(MakeCube());
        Camera camera;
        Input input;
        Time time;
        CollisionWorld collision;
        auto frame = [&] {
            editor.Begin(scene, cameraId, player, camera, time, input, collision);
            time.Advance(1. / 60);
            if (editor.Playing())
                scene.Update({input, time});
            editor.DrawDebug(scene, cameraId, camera, collision);
            auto render = CollectRenderFrame(scene, cameraId, camera, 1280.f / 720, 0);
            editor.ApplyView(render);
            require(renderer.RenderScene(render), "workflow render");
        };
        auto& io = ImGui::GetIO();
        io.AddFocusEvent(true);
        frame();
        frame();
        auto click = [&](float x, float y) {
            io.AddMousePosEvent(x, y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
        };
        auto key = [&](ImGuiKey k) {
            io.AddKeyEvent(k, true);
            frame();
            io.AddKeyEvent(k, false);
            frame();
            frame();
        };
        auto shortcut = [&](ImGuiKey k) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            frame();
            key(k);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
        };
        // Exercise the actual widgets at the fixed 1280x720 regression layout.
        click(35, 106);
        require(scene.Objects().size() == 3, "UI add cube");
        const auto cubeState = SceneIO::Encode(scene, cameraId, player, camera);
        const auto cube = EditorTestAccess::Selection(editor);
        require(scene.Find(cube) && cube != player && cube != cameraId,
                "UI selects newly placed cube");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        click(1220, 231);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        io.AddInputCharactersUTF8("4.25");
        frame();
        key(ImGuiKey_Enter);
        require(std::abs(scene.Find(cube)->transform.position.z - 4.25f) < .001f,
                "UI edits Z coordinate");
        key(ImGuiKey_Escape);
        const auto moved = SceneIO::Encode(scene, cameraId, player, camera);
        shortcut(ImGuiKey_Z);
        require(SceneIO::Encode(scene, cameraId, player, camera) == cubeState,
                "UI Ctrl Z restores transform");
        shortcut(ImGuiKey_Y);
        require(SceneIO::Encode(scene, cameraId, player, camera) == moved,
                "UI Ctrl Y restores edit");
        click(92, 106);
        require(scene.Objects().size() == 2, "UI delete");
        shortcut(ImGuiKey_Z);
        require(SceneIO::Encode(scene, cameraId, player, camera) == moved, "UI undo deletion");
        // Import through the asset list, including a Unicode source path.
        click(130, 608);
        click(130, 608);
        require(scene.Objects().size() == 4, "UI double-click imports model");
        const auto selected = EditorTestAccess::Selection(editor);
        require(scene.Find(selected)->Get<ModelComponent>() != nullptr,
                "UI imported model selected");
        const auto imported = SceneIO::Encode(scene, cameraId, player, camera);
        ModelCache cache;
        auto loaded = SceneIO::Decode(imported, cache);
        require(SceneIO::Encode(loaded.scene, loaded.camera, loaded.player, loaded.cameraData) ==
                    imported,
                "Unicode model path survives scene round trip");
        // Drag the same asset into the Scene, then undo the new placement.
        io.AddMousePosEvent(130, 608);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent(145, 610);
        frame();
        io.AddMousePosEvent(680, 440);
        frame();
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        require(scene.Objects().size() == 5, "UI asset drag-drop places a model");
        require(std::abs(scene.Find(EditorTestAccess::Selection(editor))->transform.position.y) <
                    .001f,
                "asset drop places model on ground");
        shortcut(ImGuiKey_Z);
        require(SceneIO::Encode(scene, cameraId, player, camera) == imported,
                "undo drag-drop placement");
        io.AddMousePosEvent(600, 350);
        frame();
        const float distance = EditorTestAccess::Distance(editor);
        io.AddMouseWheelEvent(0, 1);
        frame();
        require(EditorTestAccess::Distance(editor) < distance, "viewport wheel zoom");
        key(ImGuiKey_F);
        require(EditorTestAccess::Distance(editor) > 0, "viewport focus");
        io.AddMouseButtonEvent(1, true);
        frame();
        io.AddMousePosEvent(620, 365);
        frame();
        io.AddMouseButtonEvent(1, false);
        frame();
        io.AddMouseButtonEvent(2, true);
        frame();
        io.AddMousePosEvent(630, 375);
        frame();
        io.AddMouseButtonEvent(2, false);
        frame();
        require(SceneIO::Encode(scene, cameraId, player, camera) == imported,
                "editor orbit and pan preserve authored camera and transforms");
        shortcut(ImGuiKey_S);
        require(!EditorTestAccess::Dirty(editor), "UI save clears dirty state");
        loaded = SceneIO::Load("logs/editor-workflow-scene.json", cache);
        require(SceneIO::Encode(loaded.scene, loaded.camera, loaded.player, loaded.cameraData) ==
                    imported,
                "saved authored scene matches imported scene");
        click(271, 42);
        require(editor.Playing(), "UI play");
        scene.Find(player)->transform.position.x = 77;
        frame();
        click(300, 42);
        require(!editor.Playing(), "UI stop");
        require(SceneIO::Encode(scene, cameraId, player, camera) == imported,
                "Stop restores authored state after runtime changes");
        require(editor.RequestClose(), "saved document closes without prompt");
        click(35, 106);
        require(!editor.RequestClose(), "unsaved edit blocks window close");
        frame();
        renderer.RequestCapture("captures/editor-workflow.bmp");
        frame();
        renderer.SetOverlay({});
        std::filesystem::remove("logs/editor-workflow-scene.json");
        std::filesystem::remove(modelPath);
        Log::Write(LogLevel::Info, "Editor workflow PASS: place, Z edit, undo, redo, delete, asset "
                                   "import, Unicode, focus, zoom, save, play/restore, close guard");
        return 0;
    }
    catch (const std::exception& e)
    {
        Log::Write(LogLevel::Error, std::string("Editor workflow: ") + e.what());
        return 1;
    }
}
} // namespace nereides
