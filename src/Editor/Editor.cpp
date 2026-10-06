#include "Editor/Editor.h"
#include "Editor/EditorGeometry.h"
#include "Sandbox/MoveComponent.h"
#include "Core/Log.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <ImGuizmo.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#pragma comment(lib, "comdlg32.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace nereides
{
namespace
{
using namespace DirectX;
std::string Utf8(const std::filesystem::path& path)
{
    const auto s = path.generic_u8string();
    return {s.begin(), s.end()};
}
std::filesystem::path Path(const char* text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}
std::filesystem::path ChooseFile(HWND owner, bool save, bool model = false)
{
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrFilter =
        model ? L"Models\0*.fbx;*.obj;*.gltf;*.glb\0All files\0*.*\0" : L"Scene (*.json)\0*.json\0";
    dialog.lpstrDefExt = model ? L"fbx" : L"json";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const auto initial = std::filesystem::current_path() / (model ? L"assets" : L"data/scenes");
    dialog.lpstrInitialDir = initial.c_str();
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
        return path;
    if (CommDlgExtendedError())
        throw std::runtime_error("File dialog failed");
    return {};
}
bool SetLocalTransform(Transform& transform, FXMMATRIX matrix)
{
    XMVECTOR scale, rotation, position;
    if (!XMMatrixDecompose(&scale, &rotation, &position, matrix))
        return false;
    XMFLOAT3 s;
    XMStoreFloat3(&s, scale);
    if (s.x < .001f || s.y < .001f || s.z < .001f)
        return false;
    transform.scale = s;
    XMStoreFloat3(&transform.position, position);
    XMFLOAT4X4 r;
    XMStoreFloat4x4(&r, XMMatrixRotationQuaternion(rotation));
    transform.rotation.x = std::asin(std::clamp(-r._32, -1.f, 1.f));
    if (std::abs(std::cos(transform.rotation.x)) > .0001f)
    {
        transform.rotation.y = std::atan2(r._31, r._33);
        transform.rotation.z = std::atan2(r._12, r._22);
    }
    else
    {
        transform.rotation.y = std::atan2(-r._13, r._11);
        transform.rotation.z = 0;
    }
    return true;
}
bool VectorControl(const char* label, XMFLOAT3& value, float speed, bool positive = false)
{
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    bool changed = false;
    const char* names[] = {"X", "Y", "Z"};
    const ImVec4 colors[] = {{.95f, .35f, .32f, 1}, {.4f, .85f, .45f, 1}, {.35f, .65f, 1, 1}};
    float* values[] = {&value.x, &value.y, &value.z};
    const float width = (ImGui::GetContentRegionAvail().x - 16) / 3;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (axis)
            ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(colors[axis], "%s", names[axis]);
        ImGui::SetNextItemWidth(width);
        ImGui::PushID(axis);
        changed |= ImGui::DragFloat("##value", values[axis], speed, positive ? .001f : -100000.f,
                                    100000.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();
        ImGui::EndGroup();
    }
    ImGui::PopID();
    return changed;
}
bool ContainsRole(const Scene& scene, ObjectId root, ObjectId role)
{
    for (auto* object = scene.Find(role); object; object = scene.Find(object->Parent()))
        if (object->Id() == root)
            return true;
    return false;
}
void Panel(const char* name, ImVec2 position, ImVec2 size)
{
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(name, nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
}
} // namespace
Editor::~Editor()
{
    if (m_initialized)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}
bool Editor::Initialize(HWND window, D3D11Renderer& renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    if (std::filesystem::exists(L"C:/Windows/Fonts/malgun.ttf"))
        ImGui::GetIO().Fonts->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", 17.f);
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.FrameRounding = 3;
    style.WindowPadding = {10, 8};
    style.Colors[ImGuiCol_WindowBg] = {.075f, .085f, .105f, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {.12f, .17f, .23f, 1};
    style.Colors[ImGuiCol_Button] = {.18f, .27f, .36f, 1};
    if (!ImGui_ImplWin32_Init(window))
    {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX11_Init(renderer.Device(), renderer.Context()))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    m_window = window;
    m_renderer = &renderer;
    m_textures.SetDevice(renderer.Device());
    m_initialized = true;
    m_assets.SetRoot("assets");
    return true;
}
void Editor::Message(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (m_initialized)
        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
}
bool Editor::RequestClose()
{
    if (m_allowClose || (!m_dirty && (!m_historyReady || !m_history.Dirty())))
        return true;
    m_closeRequested = true;
    return false;
}
void Editor::Focus(const Scene& scene, ObjectId id)
{
    if (!scene.Find(id))
        return;
    const auto bounds = EditorBounds(scene, id);
    m_focus = bounds.Center;
    const float radius = XMVectorGetX(XMVector3Length(XMLoadFloat3(&bounds.Extents)));
    const float aspect = std::max(.1f, m_viewSize.x / m_viewSize.y);
    const float halfFov = std::atan(std::tan(XM_PIDIV4 * .5f) * std::min(1.f, aspect));
    m_distance = std::clamp(radius / std::sin(halfFov) * 1.15f, .5f, 5000.f);
}
void Editor::Import(Scene& scene, const std::filesystem::path& path, const XMFLOAT3* position)
{
    auto model = m_models.Load(path);
    if (model->parts.empty())
        throw std::runtime_error("The file contains no mesh");
    auto& object = scene.Create(Utf8(path.stem()));
    m_assets.Remember(path, model->parts.size(), model->clips.size());
    object.Add<ModelComponent>(std::move(model));
    object.transform.position = position ? *position : m_focus;
    m_selected = object.Id();
    Focus(scene, m_selected);
    m_dirty = true;
    m_errorLog = 0;
    m_status = "Placed " + Utf8(path.filename()) + ". F focuses the selection.";
}
void Editor::ApplyView(RenderFrame& frame) const
{
    frame.view = m_view;
    frame.projection = m_projection;
}
bool Editor::Begin(Scene& scene, ObjectId& cameraId, ObjectId& player, Camera& camera, Time& time,
                   Input& input, CollisionWorld& collision)
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    bool replaced = false;
    if (!m_historyReady)
    {
        m_history.Reset(SceneIO::Encode(scene, cameraId, player, camera));
        m_historyReady = true;
        m_selected = player;
    }
    auto report = [&](const std::exception& e) {
        m_status = e.what();
        m_errorLog = Log::Write(LogLevel::Error, m_status);
    };
    auto commit = [&] {
        m_history.Commit(SceneIO::Encode(scene, cameraId, player, camera));
        m_dirty = m_history.Dirty();
    };
    auto selectionIndex = [&] {
        auto ids = scene.Objects();
        return std::size_t(std::find(ids.begin(), ids.end(), m_selected) - ids.begin());
    };
    auto restore = [&](LoadedScene loaded, std::size_t selection) {
        scene = std::move(loaded.scene);
        cameraId = loaded.camera;
        player = loaded.player;
        camera = loaded.cameraData;
        const auto ids = scene.Objects();
        m_selected = selection < ids.size() ? ids[selection] : 0;
        m_transition.Reset();
        replaced = true;
    };
    auto save = [&](bool choose) {
        if (m_playing)
            return false;
        try
        {
            auto destination = Path(m_scenePath);
            if (choose || !m_hasScenePath)
            {
                const auto path = ChooseFile(m_window, true);
                if (path.empty())
                    return false;
                const auto text = Utf8(path);
                if (text.size() >= sizeof(m_scenePath))
                    throw std::runtime_error("Scene path is too long");
                destination = path;
            }
            commit();
            SceneIO::Save(scene, cameraId, player, destination, camera);
            strcpy_s(m_scenePath, Utf8(destination).c_str());
            m_hasScenePath = true;
            m_history.MarkSaved();
            m_dirty = false;
            m_status = "Saved: " + std::string(m_scenePath);
            return true;
        }
        catch (const std::exception& e)
        {
            report(e);
            return false;
        }
    };
    auto history = [&](int direction) {
        if (m_playing)
            return;
        try
        {
            commit();
            if (direction < 0 ? !m_history.CanUndo() : !m_history.CanRedo())
                return;
            const auto selected = selectionIndex();
            auto loaded = SceneIO::Decode(m_history.Target(direction), m_models);
            restore(std::move(loaded), selected);
            m_history.Accept(direction);
            m_dirty = m_history.Dirty();
            m_status = direction < 0 ? "Undo" : "Redo";
        }
        catch (const std::exception& e)
        {
            report(e);
        }
    };
    const auto display = ImGui::GetIO().DisplaySize;
    const float left = std::clamp(display.x * .19f, 170.f, 250.f);
    const float right = std::clamp(display.x * .26f, 240.f, 330.f);
    m_assetsHeight = std::clamp(m_assetsHeight, 210.f, std::max(210.f, display.y - 264.f));
    const float bottom = m_assetsHeight;
    const float top = 64;
    const float middle = std::max(120.f, display.x - left - right);
    const float height = std::max(120.f, display.y - top - bottom - 6);
    const auto& io = ImGui::GetIO();
    const bool shortcuts = !io.WantTextInput && !ImGuizmo::IsUsing() &&
                           !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    Panel("Workspace", {0, 0}, {display.x, top});
    ImGui::BeginDisabled(m_playing);
    if (ImGui::Button("Save") ||
        (shortcuts && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)))
        save(false);
    ImGui::SameLine();
    if (ImGui::Button("Save as"))
        save(true);
    ImGui::SameLine();
    if (ImGui::Button("Open"))
        ImGui::OpenPopup("Open scene");
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_history.CanUndo() && !m_dirty);
    if (ImGui::Button("Undo") ||
        (shortcuts && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
        history(-1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_history.CanRedo());
    if (ImGui::Button("Redo") ||
        (shortcuts && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)))
        history(1);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(m_playing ? "Stop / restore" : "Play"))
    {
        try
        {
            if (!m_playing)
            {
                commit();
                m_playSnapshot = m_history.Current();
                m_playSelection = selectionIndex();
                m_playing = true;
                time.Reset();
                input.DiscardHeld();
            }
            else
            {
                restore(SceneIO::Decode(m_playSnapshot, m_models), m_playSelection);
                m_playing = false;
                m_dirty = m_history.Dirty();
                time.Reset();
            }
        }
        catch (const std::exception& e)
        {
            report(e);
        }
    }
    ImGui::SameLine();
    if (m_playing)
        ImGui::Checkbox("Pause", &time.paused);
    else
        ImGui::TextColored(m_dirty ? ImVec4(1, .73f, .3f, 1) : ImVec4(.5f, .85f, .65f, 1), "%s",
                           m_dirty ? "Unsaved changes" : (m_hasScenePath ? "Saved" : "Untitled"));
    ImGui::SameLine();
    const auto consoleLabel =
        "Console (" + std::to_string(m_console.UnreadErrors()) + " new errors)";
    if (ImGui::Button(consoleLabel.c_str()))
        m_console.open = !m_console.open;
    {
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        m_consoleButton = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
    }
    if (ImGui::BeginPopupModal("Open scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(m_dirty ? "Save your changes before opening another scene?"
                                       : "Open a scene file.");
        bool open = false;
        if (m_dirty)
        {
            if (ImGui::Button("Save and open"))
                open = save(false);
            ImGui::SameLine();
        }
        if (ImGui::Button(m_dirty ? "Discard and open" : "Choose file"))
            open = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        if (open)
        {
            try
            {
                const auto path = ChooseFile(m_window, false);
                if (!path.empty())
                {
                    const auto text = Utf8(path);
                    if (text.size() >= sizeof(m_scenePath))
                        throw std::runtime_error("Scene path is too long");
                    auto loaded = SceneIO::Load(path, m_models);
                    const auto encoded = SceneIO::Encode(loaded.scene, loaded.camera, loaded.player,
                                                         loaded.cameraData);
                    restore(std::move(loaded), 0);
                    strcpy_s(m_scenePath, text.c_str());
                    m_hasScenePath = true;
                    m_history.Reset(encoded);
                    m_dirty = false;
                    m_status = "Loaded: " + text;
                }
            }
            catch (const std::exception& e)
            {
                report(e);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (m_closeRequested)
    {
        ImGui::OpenPopup("Unsaved changes");
        m_closeRequested = false;
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Save the edited scene before closing?");
        ImGui::BeginDisabled(m_playing);
        if (ImGui::Button("Save and close") && save(false))
            m_allowClose = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard and close"))
            m_allowClose = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        if (m_playing)
            ImGui::TextUnformatted("Stop preview to save the authored scene.");
        if (m_allowClose)
        {
            ImGui::CloseCurrentPopup();
            PostMessageW(m_window, WM_CLOSE, 0, 0);
        }
        ImGui::EndPopup();
    }
    ImGui::End();

    Panel("Hierarchy", {0, top}, {left, height});
    ImGui::BeginDisabled(m_playing);
    if (ImGui::Button("+ Cube"))
    {
        auto& object = scene.Create("Cube");
        object.Add<MeshComponent>(MakeCube());
        object.transform.position = m_focus;
        m_selected = object.Id();
        m_dirty = true;
    }
    ImGui::SameLine();
    const bool canDelete = scene.Find(m_selected) && !ContainsRole(scene, m_selected, cameraId) &&
                           !ContainsRole(scene, m_selected, player);
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("Delete"))
    {
        scene.Destroy(m_selected);
        scene.Flush();
        m_selected = 0;
        m_dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::Separator();
    auto tree = [&](auto&& self, ObjectId parent) -> void {
        for (auto id : scene.Objects())
        {
            auto* object = scene.Find(id);
            if (object->Parent() != parent)
                continue;
            bool children = false;
            for (auto other : scene.Objects())
                children |= scene.Find(other)->Parent() == id;
            ImGui::PushID(static_cast<int>(id));
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (m_selected == id)
                flags |= ImGuiTreeNodeFlags_Selected;
            if (!children)
                flags |= ImGuiTreeNodeFlags_Leaf;
            const bool open = ImGui::TreeNodeEx("object", flags, "%s", object->Name().c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_selected = id;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                Focus(scene, id);
            if (open)
            {
                self(self, id);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    };
    tree(tree, 0);
    ImGui::End();
    Panel("Inspector", {display.x - right, top}, {right, display.y - top});
    ImGui::BeginDisabled(m_playing);
    if (auto* object = scene.Find(m_selected))
    {
        char name[512];
        strcpy_s(name, object->Name().substr(0, 511).c_str());
        if (ImGui::InputText("Name", name, sizeof(name)))
        {
            object->SetName(name);
            m_dirty = true;
        }
        m_dirty |= ImGui::Checkbox("Enabled", &object->enabled);
        ImGui::SeparatorText("Transform");
        m_dirty |= VectorControl("Position (meters)", object->transform.position, .02f);
        auto degrees = object->transform.rotation;
        degrees.x *= 180 / XM_PI;
        degrees.y *= 180 / XM_PI;
        degrees.z *= 180 / XM_PI;
        if (VectorControl("Rotation (degrees)", degrees, .5f))
        {
            object->transform.rotation = {degrees.x * XM_PI / 180, degrees.y * XM_PI / 180,
                                          degrees.z * XM_PI / 180};
            m_dirty = true;
        }
        m_dirty |= VectorControl("Scale", object->transform.scale, .01f, true);
        if (ImGui::Button("Reset transform"))
        {
            object->transform = {};
            m_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Focus (F)"))
            Focus(scene, m_selected);
        if (ImGui::BeginCombo(
                "Parent", object->Parent() ? scene.Find(object->Parent())->Name().c_str() : "None"))
        {
            if (ImGui::Selectable("None", !object->Parent()))
                m_dirty |= scene.SetParent(object->Id(), 0);
            for (auto id : scene.Objects())
                if (id != object->Id())
                {
                    ImGui::PushID(static_cast<int>(id));
                    if (ImGui::Selectable(scene.Find(id)->Name().c_str()))
                    {
                        if (scene.SetParent(object->Id(), id))
                            m_dirty = true;
                        else
                            m_status = "Parent rejected: the hierarchy would contain a cycle.";
                    }
                    ImGui::PopID();
                }
            ImGui::EndCombo();
        }
        if (auto* mesh = object->Get<MeshComponent>())
            m_dirty |= ImGui::ColorEdit4("Color", &mesh->tint.x);
        if (auto* move = object->Get<MoveComponent>())
            m_dirty |= ImGui::DragFloat("Move speed", &move->speed, .1f, 0, 1000, "%.2f",
                                        ImGuiSliderFlags_AlwaysClamp);
        if (auto* parameters = object->Get<Parameters>())
            for (auto& [parameterName, p] : parameters->values)
                m_dirty |=
                    ImGui::DragScalar(parameterName.c_str(), ImGuiDataType_Double, &p.value, .1f,
                                      &p.minimum, &p.maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (auto* collider = object->Get<Collider>();
            collider && ImGui::CollapsingHeader("Collision"))
        {
            m_dirty |= ImGui::Checkbox("Trigger", &collider->trigger);
            if (collider->shape == Shape::Sphere)
                m_dirty |= ImGui::DragFloat("Radius", &collider->radius, .01f, 0, 10000, "%.3f",
                                            ImGuiSliderFlags_AlwaysClamp);
            else
                m_dirty |= ImGui::DragFloat3("Half extents", &collider->halfExtents.x, .01f, 0,
                                             10000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (auto* model = object->Get<ModelComponent>())
        {
            ImGui::SeparatorText("Model");
            ImGui::TextWrapped("%s", model->model->path.c_str());
            ImGui::Text("%zu parts / %zu clips", model->model->parts.size(),
                        model->model->clips.size());
        }
    }
    else
        ImGui::TextWrapped("Select an object in the Scene or Hierarchy.");
    ImGui::EndDisabled();
    if (m_playing)
        ImGui::TextWrapped("Preview is running. Stop restores the edited scene.");
    if (auto* selected = scene.Find(m_selected))
        if (auto* model = selected->Get<ModelComponent>(); model && !model->model->clips.empty())
        {
            ImGui::SeparatorText("Animation preview");
            ImGui::TextWrapped("Play the scene, then choose a clip. Stop restores edits.");
            ImGui::BeginDisabled(!m_playing);
            for (std::size_t i = 0; i < model->model->clips.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Button(model->model->clips[i].name.c_str()))
                    model->player.Play(i, true, .15);
                ImGui::PopID();
            }
            ImGui::EndDisabled();
        }
    ImGui::SeparatorText("Scene file");
    ImGui::TextWrapped("%s",
                       m_hasScenePath ? m_scenePath : "Untitled - choose a file on first Save");
    ImGui::SeparatorText("Status");
    ImGui::TextWrapped("%s", m_status.c_str());
    if (ImGui::CollapsingHeader("Transition preview"))
    {
        ImGui::InputText("Clip", m_clipPath, sizeof(m_clipPath));
        if (ImGui::Button("Start preview"))
            try
            {
                StartPreview();
            }
            catch (const std::exception& e)
            {
                report(e);
            }
        ImGui::SameLine();
        if (ImGui::Button("Skip"))
            m_transition.Skip(m_transition.Token());
        if (m_transition.State() == TransitionState::WaitingForScene &&
            ImGui::Button("Scene ready"))
            m_transition.SceneReady(m_transition.Token());
        if (ImGui::Button("Reset preview"))
            m_transition.Reset();
        if (!m_transition.Error().empty())
            ImGui::TextWrapped("%s", m_transition.Error().c_str());
    }
    ImGui::End();

    ImGui::SetNextWindowPos({0, top + height});
    ImGui::SetNextWindowSize({left + middle, 6});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::Begin("##AssetSplitter", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::InvisibleButton("Resize assets", {left + middle, 6});
    m_splitter = {(left + middle) * .5f, top + height + 3};
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive())
        m_assetsHeight -= io.MouseDelta.y;
    ImGui::End();
    ImGui::PopStyleVar(2);
    Panel("Assets", {0, top + height + 6}, {left + middle, bottom});
    ImGui::BeginChild("Asset workspace", {0, m_errorLog ? -29.f : 0.f});
    m_assets.Draw(
        m_playing,
        [&]() -> std::filesystem::path {
            try
            {
                return ChooseFile(m_window, false, true);
            }
            catch (const std::exception& e)
            {
                report(e);
                return {};
            }
        },
        [&](const std::filesystem::path& path) {
            try
            {
                Import(scene, path);
            }
            catch (const std::exception& e)
            {
                const auto message =
                    std::runtime_error("Model import failed: " + Utf8(path) + "\n" + e.what());
                report(message);
            }
        });
    ImGui::EndChild();
    if (m_errorLog)
    {
        ImGui::TextColored({1, .4f, .35f, 1}, "Operation failed.");
        ImGui::SameLine();
        if (ImGui::Button("View log"))
            m_console.Reveal(m_errorLog);
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        m_viewLogButton = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss"))
            m_errorLog = 0;
    }
    ImGui::End();
    Panel("Scene", {left, top}, {middle, height});
    ImGui::BeginDisabled(m_playing);
    const char* tools[] = {"Move (W)", "Rotate (E)", "Scale (R)"};
    for (int i = 0; i < 3; ++i)
    {
        if (i)
            ImGui::SameLine();
        ImGui::RadioButton(tools[i], &m_operation, i);
    }
    ImGui::EndDisabled();
    ImGui::Checkbox("Local", &m_local);
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_snap);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(65);
    ImGui::DragFloat("Step", &m_snapStep, .1f, .01f, 90.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &m_grid);
    ImGui::SameLine();
    ImGui::Checkbox("Colliders", &m_debug);
    ImGui::TextUnformatted("RMB orbit | MMB pan | Wheel zoom | F focus");
    const auto origin = ImGui::GetCursorScreenPos();
    auto size = ImGui::GetContentRegionAvail();
    size.x = std::max(1.f, size.x);
    size.y = std::max(1.f, size.y);
    m_viewOrigin = {origin.x, origin.y};
    m_viewSize = {size.x, size.y};
    if (!m_renderer->PrepareSceneView(static_cast<unsigned>(size.x), static_cast<unsigned>(size.y)))
        throw std::runtime_error("Cannot create Scene viewport");
    ImGui::Image(ImTextureRef(reinterpret_cast<ImTextureID>(m_renderer->SceneView())), size);
    const bool hovered = ImGui::IsItemHovered();
    if (!m_playing && ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload("NEREIDES_MODEL"))
            try
            {
                // Place on the ground under the drop, falling back to the focus plane.
                auto unproject = [&](float z) {
                    return XMVector3Unproject(XMVectorSet(io.MousePos.x, io.MousePos.y, z, 1),
                                              origin.x, origin.y, size.x, size.y, 0, 1,
                                              XMLoadFloat4x4(&m_projection),
                                              XMLoadFloat4x4(&m_view), XMMatrixIdentity());
                };
                const auto start = unproject(0),
                           direction = XMVector3Normalize(unproject(1) - start);
                const float dy = XMVectorGetY(direction);
                float distance = std::abs(dy) > .0001f ? -XMVectorGetY(start) / dy : -1;
                if (distance < 0 || distance > 10000)
                    distance = std::max(
                        .1f, XMVectorGetX(XMVector3Dot(XMLoadFloat3(&m_focus) - start, direction)));
                XMFLOAT3 position;
                XMStoreFloat3(&position, start + direction * distance);
                Import(scene, Path(static_cast<const char*>(payload->Data)), &position);
            }
            catch (const std::exception& e)
            {
                report(std::runtime_error(std::string("Model import failed: ") +
                                          static_cast<const char*>(payload->Data) + "\n" +
                                          e.what()));
            }
        ImGui::EndDragDropTarget();
    }
    if (hovered && !ImGuizmo::IsUsing())
    {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
        {
            m_yaw += io.MouseDelta.x * .008f;
            m_pitch = std::clamp(m_pitch + io.MouseDelta.y * .008f, -1.5f, 1.5f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            const auto orientation = XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0);
            const auto rightAxis = XMVector3TransformNormal(XMVectorSet(1, 0, 0, 0), orientation);
            const auto upAxis = XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), orientation);
            XMStoreFloat3(&m_focus, XMLoadFloat3(&m_focus) +
                                        (rightAxis * -io.MouseDelta.x + upAxis * io.MouseDelta.y) *
                                            m_distance * .002f);
        }
        m_distance = std::clamp(m_distance * std::pow(.85f, io.MouseWheel), .1f, 5000.f);
        if (shortcuts && ImGui::IsKeyPressed(ImGuiKey_F, false))
            Focus(scene, m_selected);
        if (!m_playing && shortcuts && !io.KeyCtrl && !ImGui::IsMouseDown(1))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false))
                m_operation = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false))
                m_operation = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                m_operation = 2;
        }
    }
    const auto forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0),
                                                  XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0));
    const auto eye = XMLoadFloat3(&m_focus) - forward * m_distance;
    XMStoreFloat4x4(&m_view,
                    XMMatrixLookAtLH(eye, XMLoadFloat3(&m_focus), XMVectorSet(0, 1, 0, 0)));
    XMStoreFloat4x4(&m_projection, XMMatrixPerspectiveFovLH(XM_PIDIV4, size.x / size.y,
                                                            std::max(.001f, m_distance * .0001f),
                                                            std::max(500.f, m_distance * 20)));
    if (m_playing)
    {
        XMStoreFloat4x4(&m_view, camera.View(scene, cameraId));
        XMStoreFloat4x4(&m_projection, camera.Projection(size.x / size.y));
    }
    m_sceneDrawList = ImGui::GetWindowDrawList();
    m_sceneDrawList->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    ImGuizmo::SetDrawlist(m_sceneDrawList);
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    ImGuizmo::SetOrthographic(false);
    if (!m_playing)
        if (auto* object = scene.Find(m_selected))
        {
            XMFLOAT4X4 world;
            XMStoreFloat4x4(&world, scene.World(m_selected));
            const ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE,
                                                      ImGuizmo::SCALE};
            float snap[] = {m_snapStep, m_snapStep, m_snapStep};
            ImGuizmo::Enable(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
                             (hovered || ImGuizmo::IsUsing()));
            if (ImGuizmo::Manipulate(&m_view._11, &m_projection._11, operations[m_operation],
                                     m_local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &world._11,
                                     nullptr, m_snap ? snap : nullptr))
            {
                auto local = XMLoadFloat4x4(&world);
                if (object->Parent())
                    local *= XMMatrixInverse(nullptr, scene.World(object->Parent()));
                m_dirty |= SetLocalTransform(object->transform, local);
            }
        }
    if (!m_playing && hovered && ImGui::IsMouseClicked(0) && !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing())
    {
        auto unproject = [&](float depth) {
            return XMVector3Unproject(XMVectorSet(io.MousePos.x, io.MousePos.y, depth, 1), origin.x,
                                      origin.y, size.x, size.y, 0, 1, XMLoadFloat4x4(&m_projection),
                                      XMLoadFloat4x4(&m_view), XMMatrixIdentity());
        };
        const auto rayStart = unproject(0), rayEnd = unproject(1);
        m_selected = PickEditorObject(scene, rayStart, XMVector3Normalize(rayEnd - rayStart));
    }
    m_sceneDrawList->PopClipRect();
    ImGui::End();
    if (!m_playing && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing())
        try
        {
            commit();
        }
        catch (const std::exception& e)
        {
            report(e);
        }
    time.paused = !m_playing || time.paused;
    time.pausePresentation = false;
    m_console.Draw();
    input.Capture(!m_playing || !hovered || io.WantTextInput, !m_playing || !hovered);
    (void)collision;
    return replaced;
}
void Editor::Render()
{
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
void Editor::DrawDebug(const Scene& scene, ObjectId cameraId, const Camera& camera,
                       const CollisionWorld& collision)
{
    if (m_playing)
        XMStoreFloat4x4(&m_view, camera.View(scene, cameraId));
    if (!m_sceneDrawList)
        return;
    auto* draw = m_sceneDrawList;
    const ImVec2 origin{m_viewOrigin.x, m_viewOrigin.y}, size{m_viewSize.x, m_viewSize.y};
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    const auto vp = XMLoadFloat4x4(&m_view) * XMLoadFloat4x4(&m_projection);
    auto line = [&](XMFLOAT3 a, XMFLOAT3 b, ImU32 color, float thickness = 1.f) {
        XMFLOAT4 ca, cb;
        XMStoreFloat4(&ca, XMVector4Transform(XMVectorSet(a.x, a.y, a.z, 1), vp));
        XMStoreFloat4(&cb, XMVector4Transform(XMVectorSet(b.x, b.y, b.z, 1), vp));
        if (ca.z < 0 && cb.z < 0)
            return;
        if ((ca.z < 0) != (cb.z < 0))
        {
            const float t = ca.z / (ca.z - cb.z);
            XMFLOAT4 p{ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, 0,
                       ca.w + (cb.w - ca.w) * t};
            if (ca.z < 0)
                ca = p;
            else
                cb = p;
        }
        if (ca.w <= 0 || cb.w <= 0)
            return;
        draw->AddLine({origin.x + (ca.x / ca.w * .5f + .5f) * size.x,
                       origin.y + (-ca.y / ca.w * .5f + .5f) * size.y},
                      {origin.x + (cb.x / cb.w * .5f + .5f) * size.x,
                       origin.y + (-cb.y / cb.w * .5f + .5f) * size.y},
                      color, thickness);
    };
    const ImU32 red = IM_COL32(245, 85, 75, 255), green = IM_COL32(95, 220, 110, 255),
                blue = IM_COL32(90, 160, 255, 255);
    if (!m_playing && m_grid)
    {
        for (int i = -20; i <= 20; ++i)
        {
            const float v = float(i);
            const auto c = IM_COL32(100, 120, 140, i % 5 == 0 ? 95 : 45);
            line({v, 0, -20}, {v, 0, 20}, i == 0 ? blue : c);
            line({-20, 0, v}, {20, 0, v}, i == 0 ? red : c);
        }
        line({0, 0, 0}, {0, 3, 0}, green, 2);
    }
    auto box = [&](const BoundingBox& bounds, ImU32 color) {
        XMFLOAT3 points[8];
        bounds.GetCorners(points);
        const unsigned edges[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                     {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (auto& edge : edges)
            line(points[edge[0]], points[edge[1]], color, 1.4f);
    };
    if (!m_playing && scene.Find(m_selected))
        box(EditorBounds(scene, m_selected), IM_COL32(255, 205, 90, 220));
    if (m_debug)
        for (const auto& shape : collision.Collect(scene))
        {
            const auto c =
                shape.trigger ? IM_COL32(70, 210, 175, 160) : IM_COL32(235, 190, 75, 130);
            if (shape.shape == Shape::Box)
                box(BoundingBox(shape.center, shape.halfExtents), c);
            else
                for (int axis = 0; axis < 3; ++axis)
                    for (int i = 0; i < 32; ++i)
                    {
                        auto p = [&](int n) {
                            const float a = XM_2PI * n / 32;
                            XMFLOAT3 v = shape.center;
                            float* coordinates[] = {&v.x, &v.y, &v.z};
                            *coordinates[axis] += std::cos(a) * shape.radius;
                            *coordinates[(axis + 1) % 3] += std::sin(a) * shape.radius;
                            return v;
                        };
                        line(p(i), p(i + 1), c);
                    }
        }
    const ImVec2 center{origin.x + size.x - 54, origin.y + 55};
    draw->AddCircleFilled(center, 43, IM_COL32(22, 28, 38, 210));
    const XMFLOAT3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const ImU32 colors[] = {red, green, blue};
    const char* labels[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i)
    {
        XMFLOAT3 direction;
        XMStoreFloat3(&direction,
                      XMVector3TransformNormal(XMLoadFloat3(&axes[i]), XMLoadFloat4x4(&m_view)));
        const ImVec2 end{center.x + direction.x * 28, center.y - direction.y * 28};
        draw->AddLine(center, end, colors[i], 2);
        draw->AddText({end.x + 3, end.y - 8}, colors[i], labels[i]);
    }
    draw->AddText({origin.x + 10, origin.y + size.y - 24}, IM_COL32(190, 205, 220, 255),
                  m_playing ? "GAME PREVIEW - Stop restores edits"
                            : "EDIT MODE  |  1 grid square = 1 meter");
    if (m_transition.State() == TransitionState::Playing)
    {
        const auto& clip = m_transition.Clip();
        const float t = clip.duration > 0 ? float(m_transition.ClipPosition() / clip.duration) : 1;
        for (const auto& layer : clip.layers)
        {
            const float scale = layer.startScale + (layer.endScale - layer.startScale) * t;
            ImVec2 start{origin.x + (layer.from.x + (layer.to.x - layer.from.x) * t) * size.x,
                         origin.y + (layer.from.y + (layer.to.y - layer.from.y) * t) * size.y};
            ImVec2 end{start.x + layer.size.x * scale * size.x,
                       start.y + layer.size.y * scale * size.y};
            const auto color = ImGui::ColorConvertFloat4ToU32(
                {layer.color.x, layer.color.y, layer.color.z, layer.color.w});
            if (layer.texture.empty())
                draw->AddRectFilled(start, end, color);
            else
                draw->AddImage(
                    ImTextureRef(reinterpret_cast<ImTextureID>(m_textures.Get(layer.texture))),
                    start, end, {0, 0}, {1, 1}, color);
            if (!layer.label.empty())
                draw->AddText({start.x + 12, start.y + 12}, IM_COL32_WHITE, layer.label.c_str());
        }
    }
    if (m_transition.Fade() > 0)
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(0, 0, 0, int(m_transition.Fade() * 255)));
    draw->PopClipRect();
}
void Editor::AdvancePresentation(double dt)
{
    m_transition.Advance(dt);
    for (const auto& event : m_transition.TakeNotices())
        Log::Write(LogLevel::Info, "Presentation token=" + std::to_string(event.token) + " state=" +
                                       std::to_string(static_cast<unsigned>(event.state)));
}
void Editor::StartPreview()
{
    auto clip = Clip2D::Load(m_clipPath);
    for (const auto& layer : clip.layers)
        if (!layer.texture.empty())
            m_textures.Get(layer.texture);
    m_transition.Begin(std::move(clip));
}
} // namespace nereides
