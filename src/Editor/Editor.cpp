#include "Editor/Editor.h"
#include "Sandbox/MoveComponent.h"
#include "Core/Log.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace nereides
{
namespace
{
bool SetLocalTransform(Transform& transform, DirectX::FXMMATRIX matrix)
{
    using namespace DirectX;
    XMVECTOR scale, rotation, position;
    if (!XMMatrixDecompose(&scale, &rotation, &position, matrix))
        return false;
    XMStoreFloat3(&transform.position, position);
    XMStoreFloat3(&transform.scale, scale);
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
void DebugShapes(const Scene& scene, ObjectId cameraId, const Camera& camera,
                 const CollisionWorld& collision)
{
    using namespace DirectX;
    const auto size = ImGui::GetIO().DisplaySize;
    const auto viewProjection = camera.View(scene, cameraId) * camera.Projection(size.x / size.y);
    auto project = [&](XMFLOAT3 position, ImVec2& out) {
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(position.x, position.y, position.z, 1),
                                                viewProjection));
        if (clip.w <= 0 || clip.z < 0)
            return false;
        out = {(clip.x / clip.w * .5f + .5f) * size.x, (-clip.y / clip.w * .5f + .5f) * size.y};
        return true;
    };
    auto* draw = ImGui::GetBackgroundDrawList();
    for (const auto& shape : collision.Collect(scene))
    {
        const auto color =
            shape.trigger ? IM_COL32(80, 220, 170, 255) : IM_COL32(255, 200, 80, 255);
        auto line = [&](XMFLOAT3 a, XMFLOAT3 b) {
            ImVec2 pa, pb;
            if (project(a, pa) && project(b, pb))
                draw->AddLine(pa, pb, color, 1.5f);
        };
        if (shape.shape == Shape::Box)
        {
            XMFLOAT3 points[8];
            for (unsigned i = 0; i < 8; ++i)
                points[i] = {shape.center.x + ((i & 1) ? 1 : -1) * shape.halfExtents.x,
                             shape.center.y + ((i & 2) ? 1 : -1) * shape.halfExtents.y,
                             shape.center.z + ((i & 4) ? 1 : -1) * shape.halfExtents.z};
            for (unsigned i = 0; i < 8; ++i)
                for (unsigned bit : {1u, 2u, 4u})
                    if (!(i & bit))
                        line(points[i], points[i | bit]);
        }
        else
            for (unsigned axis = 0; axis < 3; ++axis)
                for (unsigned n = 0; n < 32; ++n)
                {
                    auto point = [&](unsigned sample) {
                        float a = XM_2PI * sample / 32;
                        auto p = shape.center;
                        if (axis == 0)
                        {
                            p.x += std::cos(a) * shape.radius;
                            p.y += std::sin(a) * shape.radius;
                        }
                        else if (axis == 1)
                        {
                            p.x += std::cos(a) * shape.radius;
                            p.z += std::sin(a) * shape.radius;
                        }
                        else
                        {
                            p.y += std::cos(a) * shape.radius;
                            p.z += std::sin(a) * shape.radius;
                        }
                        return p;
                    };
                    line(point(n), point(n + 1));
                }
    }
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
bool Editor::Initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(window))
    {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX11_Init(device, context))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    m_textures.SetDevice(device);
    m_initialized = true;
    return true;
}
void Editor::Message(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (m_initialized)
        ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
}
bool Editor::Begin(Scene& scene, ObjectId& cameraId, ObjectId& player, Camera& camera, Time& time,
                   Input& input, CollisionWorld& collision)
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    bool replaced = false;
    if (!m_selected)
        m_selected = player;
    ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({340, 650}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Nereides Scene");
    ImGui::TextUnformatted(m_dirty ? "Unsaved scene changes" : "Scene workspace");
    ImGui::Checkbox("Pause simulation", &time.paused);
    ImGui::SameLine();
    ImGui::Checkbox("Collision shapes", &m_debug);
    ImGui::Text("Combat %.2f  Escape %.2f", time.combat.elapsed, time.escape.elapsed);
    ImGui::SeparatorText("Hierarchy");
    if (ImGui::Button("Add cube"))
    {
        auto& object = scene.Create("Cube");
        object.Add<MeshComponent>(MakeCube());
        m_selected = object.Id();
        m_dirty = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!scene.Find(m_selected) || m_selected == cameraId || m_selected == player);
    if (ImGui::Button("Delete"))
    {
        scene.Destroy(m_selected);
        scene.Flush();
        m_selected = 0;
        m_dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::BeginChild("Objects", {0, 120}, true);
    for (auto id : scene.Objects())
    {
        const auto* object = scene.Find(id);
        ImGui::PushID(static_cast<int>(id));
        const auto label = object->Name() + (object->Parent() ? " (child)" : "");
        if (ImGui::Selectable(label.c_str(), id == m_selected))
            m_selected = id;
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (auto* object = scene.Find(m_selected))
    {
        ImGui::SeparatorText("Inspector");
        m_dirty |= ImGui::Checkbox("Enabled", &object->enabled);
        m_dirty |= ImGui::DragFloat3("Position", &object->transform.position.x, .02f);
        m_dirty |= ImGui::DragFloat3("Rotation (rad)", &object->transform.rotation.x, .01f);
        m_dirty |= ImGui::DragFloat3("Scale", &object->transform.scale.x, .01f, .001f, 10000.f,
                                     "%.3f", ImGuiSliderFlags_AlwaysClamp);
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
                        m_dirty |= scene.SetParent(object->Id(), id);
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
            for (auto& [name, p] : parameters->values)
                m_dirty |=
                    ImGui::DragScalar(name.c_str(), ImGuiDataType_Double, &p.value, .1f, &p.minimum,
                                      &p.maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (auto* collider = object->Get<Collider>())
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
            for (std::size_t i = 0; i < model->model->clips.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Button(model->model->clips[i].name.c_str()))
                    model->player.Play(i, true, .15);
                ImGui::PopID();
            }
        ImGui::RadioButton("Move", &m_operation, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Rotate", &m_operation, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Scale tool", &m_operation, 2);
        DirectX::XMFLOAT4X4 view, projection, world;
        const auto size = ImGui::GetIO().DisplaySize;
        DirectX::XMStoreFloat4x4(&view, camera.View(scene, cameraId));
        DirectX::XMStoreFloat4x4(&projection, camera.Projection(size.x / size.y));
        DirectX::XMStoreFloat4x4(&world, scene.World(object->Id()));
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
        ImGuizmo::SetRect(0, 0, size.x, size.y);
        const ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE,
                                                  ImGuizmo::SCALE};
        if (ImGuizmo::Manipulate(&view._11, &projection._11, operations[m_operation],
                                 ImGuizmo::LOCAL, &world._11))
        {
            auto local = DirectX::XMLoadFloat4x4(&world);
            if (object->Parent())
                local *= DirectX::XMMatrixInverse(nullptr, scene.World(object->Parent()));
            m_dirty |= SetLocalTransform(object->transform, local);
        }
    }
    ImGui::SeparatorText("Models and files");
    ImGui::InputText("Model path", m_modelPath, sizeof(m_modelPath));
    if (ImGui::Button("Import model"))
        try
        {
            auto model = m_models.Load(m_modelPath);
            if (model->parts.empty())
                throw std::runtime_error("The file has no mesh");
            auto& object = scene.Create(std::filesystem::path(m_modelPath).stem().string());
            object.Add<ModelComponent>(model);
            m_selected = object.Id();
            m_dirty = true;
            m_status = "Model imported";
        }
        catch (const std::exception& e)
        {
            m_status = e.what();
            Log::Write(LogLevel::Error, m_status);
        }
    ImGui::InputText("Scene path", m_scenePath, sizeof(m_scenePath));
    if (ImGui::Button("Save scene"))
        try
        {
            SceneIO::Save(scene, cameraId, player, m_scenePath, camera);
            m_dirty = false;
            m_status = "Scene saved";
        }
        catch (const std::exception& e)
        {
            m_status = e.what();
        }
    ImGui::SameLine();
    if (ImGui::Button("Load scene"))
    {
        if (m_dirty)
            ImGui::OpenPopup("Replace unsaved scene?");
        else
            ImGui::OpenPopup("Load scene now");
    }
    bool load = false;
    if (ImGui::BeginPopupModal("Replace unsaved scene?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Current unsaved edits will be replaced.");
        if (ImGui::Button("Replace"))
        {
            load = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Load scene now"))
    {
        load = true;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (load)
        try
        {
            auto loaded = SceneIO::Load(m_scenePath, m_models);
            scene = std::move(loaded.scene);
            cameraId = loaded.camera;
            player = loaded.player;
            camera = loaded.cameraData;
            m_selected = 0;
            m_dirty = false;
            replaced = true;
            m_status = "Scene loaded";
            m_transition.Reset();
            time.Reset();
            time.paused = true;
        }
        catch (const std::exception& e)
        {
            m_status = e.what();
            Log::Write(LogLevel::Error, m_status);
        }
    ImGui::TextWrapped("%s", m_status.c_str());
    if (ImGui::CollapsingHeader("Log"))
        for (const auto& line : Log::Recent())
            ImGui::TextWrapped("%s", line.c_str());
    if (ImGui::CollapsingHeader("Transition preview"))
    {
        ImGui::InputText("Clip path", m_clipPath, sizeof(m_clipPath));
        if (ImGui::Button("Start preview"))
            try
            {
                StartPreview();
            }
            catch (const std::exception& e)
            {
                m_status = e.what();
                Log::Write(LogLevel::Error, m_status);
            }
        ImGui::SameLine();
        if (ImGui::Button("Skip"))
            m_transition.Skip(m_transition.Token());
        const auto state = m_transition.State();
        const char* labels[] = {"Idle",    "Fade out", "2D playing", "Waiting for scene",
                                "Fade in", "Complete", "Failed"};
        ImGui::Text("%s", labels[static_cast<unsigned>(state)]);
        if (state == TransitionState::WaitingForScene)
        {
            if (ImGui::Button("Scene ready"))
                m_transition.SceneReady(m_transition.Token());
            ImGui::SameLine();
            if (ImGui::Button("Simulate failure"))
                m_transition.Fail(m_transition.Token(), "Preview scene load failed");
        }
        if (!m_transition.Error().empty())
            ImGui::TextWrapped("%s", m_transition.Error().c_str());
        if (ImGui::Button("Reset preview"))
            m_transition.Reset();
    }
    ImGui::End();
    (void)collision;
    input.Capture(ImGui::GetIO().WantCaptureKeyboard,
                  ImGui::GetIO().WantCaptureMouse || ImGuizmo::IsUsing());
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
    if (m_debug)
        DebugShapes(scene, cameraId, camera, collision);
    const auto screen = ImGui::GetIO().DisplaySize;
    auto* draw = ImGui::GetBackgroundDrawList();
    if (m_transition.State() == TransitionState::Playing)
    {
        const auto& clip = m_transition.Clip();
        const float t = clip.duration > 0 ? float(m_transition.ClipPosition() / clip.duration) : 1;
        for (const auto& layer : clip.layers)
        {
            const float scale = layer.startScale + (layer.endScale - layer.startScale) * t;
            ImVec2 start{(layer.from.x + (layer.to.x - layer.from.x) * t) * screen.x,
                         (layer.from.y + (layer.to.y - layer.from.y) * t) * screen.y};
            ImVec2 end{start.x + layer.size.x * scale * screen.x,
                       start.y + layer.size.y * scale * screen.y};
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
        draw->AddRectFilled({0, 0}, screen,
                            IM_COL32(0, 0, 0, static_cast<int>(m_transition.Fade() * 255)));
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
