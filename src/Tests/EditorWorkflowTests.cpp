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
        editor.m_assets.SetRoot(model.parent_path());
    }
    static DirectX::XMFLOAT2 AssetRow(const Editor& editor, const std::filesystem::path& path)
    {
        return editor.m_assets.m_rowCenters.at(std::filesystem::absolute(path).lexically_normal());
    }
    static DirectX::XMFLOAT2 PlaceButton(const Editor& editor)
    {
        return editor.m_assets.m_placeButton;
    }
    static DirectX::XMFLOAT2 ViewLogButton(const Editor& editor)
    {
        return editor.m_viewLogButton;
    }
    static DirectX::XMFLOAT2 ConsoleButton(const Editor& editor)
    {
        return editor.m_consoleButton;
    }
    static DirectX::XMFLOAT2 ClearLogsButton(const Editor& editor)
    {
        return editor.m_console.m_clearButton;
    }
    static DirectX::XMFLOAT2 ErrorFilter(const Editor& editor)
    {
        return editor.m_console.m_errorFilter;
    }
    static DirectX::XMFLOAT2 Splitter(const Editor& editor)
    {
        return editor.m_splitter;
    }
    static DirectX::XMFLOAT2 BackButton(const Editor& editor)
    {
        return editor.m_assets.m_backButton;
    }
    static DirectX::XMFLOAT2 AssetSearch(const Editor& editor)
    {
        return editor.m_assets.m_searchBox;
    }
    static std::vector<AssetFile> Files(const Editor& editor)
    {
        return editor.m_assets.Visible();
    }
    static std::filesystem::path Folder(const Editor& editor)
    {
        return editor.m_assets.m_folder;
    }
    static void Refresh(Editor& editor)
    {
        editor.m_assets.Refresh();
    }
    static bool HasMetadata(const Editor& editor, const std::filesystem::path& path)
    {
        const auto found =
            editor.m_assets.m_known.find(std::filesystem::absolute(path).lexically_normal());
        return found != editor.m_assets.m_known.end() && found->second.meshes > 0;
    }
    static bool ConsoleVisible(const Editor& editor)
    {
        return editor.m_console.open;
    }
    static std::uint64_t SelectedLog(const Editor& editor)
    {
        return editor.m_console.m_selected.id;
    }
    static std::uint64_t ErrorLog(const Editor& editor)
    {
        return editor.m_errorLog;
    }
    static std::uint64_t ClearedThrough(const Editor& editor)
    {
        return editor.m_console.m_clearThrough;
    }
    static int LogFilter(const Editor& editor)
    {
        return editor.m_console.m_level;
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
        const auto modelPath = std::filesystem::path(L"logs/editor-assets-test/편집 검증 모델.obj");
        std::filesystem::create_directories(modelPath.parent_path());
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
        const auto assetPoint = EditorTestAccess::AssetRow(editor, modelPath);
        click(assetPoint.x, assetPoint.y);
        require(scene.Objects().size() == 3, "single asset click only selects");
        click(assetPoint.x, assetPoint.y);
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
        io.AddMousePosEvent(assetPoint.x, assetPoint.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent(assetPoint.x + 15, assetPoint.y + 2);
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
        auto clickPoint = [&](DirectX::XMFLOAT2 p) { click(p.x, p.y); };
        // Folder navigation, scoped search and explicit placement use the same real UI.
        const auto folder = modelPath.parent_path() / "motions";
        std::filesystem::create_directories(folder);
        const auto nested = folder / "Nested.FBX";
        {
            std::ofstream fixture(nested);
            fixture << "not a valid model";
        }
        EditorTestAccess::Refresh(editor);
        frame();
        auto folderPoint = EditorTestAccess::AssetRow(editor, folder);
        clickPoint(folderPoint);
        clickPoint(folderPoint);
        require(EditorTestAccess::Folder(editor) ==
                    std::filesystem::absolute(folder).lexically_normal(),
                "asset folder double click navigates");
        clickPoint(EditorTestAccess::BackButton(editor));
        require(EditorTestAccess::Folder(editor) ==
                    std::filesystem::absolute(modelPath.parent_path()).lexically_normal(),
                "asset Back restores folder");
        clickPoint(EditorTestAccess::AssetSearch(editor));
        io.AddInputCharactersUTF8("nested");
        frame();
        require(EditorTestAccess::Files(editor).size() == 1, "asset search includes subfolders");
        shortcut(ImGuiKey_A);
        io.AddInputCharactersUTF8("no-such-file");
        frame();
        require(EditorTestAccess::Files(editor).empty(), "asset search empty result");
        shortcut(ImGuiKey_A);
        key(ImGuiKey_Backspace);
        key(ImGuiKey_Escape);
        frame();
        auto modelPoint = EditorTestAccess::AssetRow(editor, modelPath);
        clickPoint(modelPoint);
        const auto countBeforePlace = scene.Objects().size();
        clickPoint(EditorTestAccess::PlaceButton(editor));
        require(scene.Objects().size() == countBeforePlace + 1 &&
                    EditorTestAccess::HasMetadata(editor, modelPath),
                "Place button imports selected file and records real metadata");
        shortcut(ImGuiKey_Z);
        // Selecting an invalid model should preserve the scene and link to its exact error.
        folderPoint = EditorTestAccess::AssetRow(editor, folder);
        clickPoint(folderPoint);
        clickPoint(folderPoint);
        clickPoint(EditorTestAccess::AssetRow(editor, nested));
        const auto beforeFailure = SceneIO::Encode(scene, cameraId, player, camera);
        clickPoint(EditorTestAccess::PlaceButton(editor));
        frame();
        const auto failedId = EditorTestAccess::ErrorLog(editor);
        require(failedId && !EditorTestAccess::ConsoleVisible(editor),
                "import failure badges without stealing focus");
        require(SceneIO::Encode(scene, cameraId, player, camera) == beforeFailure,
                "failed import preserves scene");
        clickPoint(EditorTestAccess::ViewLogButton(editor));
        frame();
        require(EditorTestAccess::ConsoleVisible(editor) &&
                    EditorTestAccess::SelectedLog(editor) == failedId,
                "View log opens Console and selects matching error");
        renderer.RequestCapture("captures/editor-console.bmp");
        frame();
        clickPoint(EditorTestAccess::ErrorFilter(editor));
        require(EditorTestAccess::LogFilter(editor) == 3, "Console Error filter");
        const auto logSize = std::filesystem::file_size("logs/engine.log");
        clickPoint(EditorTestAccess::ClearLogsButton(editor));
        require(EditorTestAccess::ClearedThrough(editor) >= failedId &&
                    std::filesystem::file_size("logs/engine.log") == logSize,
                "Clear view retains disk log");
        const auto newError = Log::Write(LogLevel::Error, "Console test new error after clear");
        frame();
        require(FilterLogs(Log::Entries(), EditorTestAccess::ClearedThrough(editor), 3, "", true)
                            .size() == 1 &&
                    newError > failedId,
                "new errors remain visible after clear");
        clickPoint(EditorTestAccess::ConsoleButton(editor));
        require(!EditorTestAccess::ConsoleVisible(editor), "Console toggles independently");
        const auto divider = EditorTestAccess::Splitter(editor);
        io.AddMousePosEvent(divider.x, divider.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent(divider.x, divider.y - 40);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        require(EditorTestAccess::Splitter(editor).y < divider.y - 20,
                "Assets height resizes by dragging divider");
        std::filesystem::remove(nested);
        std::filesystem::remove(folder);
        // Refresh after deleting the test fixture, then verify the existing close guard.
        EditorTestAccess::Refresh(editor);
        require(editor.RequestClose(), "saved document closes without prompt");
        click(35, 106);
        require(!editor.RequestClose(), "unsaved edit blocks window close");
        frame();
        renderer.RequestCapture("captures/editor-workflow.bmp");
        frame();
        renderer.SetOverlay({});
        std::filesystem::remove("logs/editor-workflow-scene.json");
        std::filesystem::remove(modelPath);
        std::filesystem::remove(modelPath.parent_path());
        Log::Write(LogLevel::Info,
                   "Editor workflow PASS: place, Z edit, undo, redo, delete, asset "
                   "import, Unicode, focus, zoom, save, play/restore, close guard, "
                   "folders, search, explicit placement, Console reveal/filter/clear, "
                   "failed import preservation, Assets resize");
        return 0;
    }
    catch (const std::exception& e)
    {
        Log::Write(LogLevel::Error, std::string("Editor workflow: ") + e.what());
        return 1;
    }
}
} // namespace nereides
