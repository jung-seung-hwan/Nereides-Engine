#include "Editor/AssetBrowser.h"
#include "Core/Log.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
namespace nereides
{
namespace
{
std::string Utf8(const std::filesystem::path& p)
{
    const auto text = p.generic_u8string();
    return {text.begin(), text.end()};
}
std::string Fold(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
bool Inside(const std::filesystem::path& p, const std::filesystem::path& root)
{
    const auto relative = p.lexically_relative(root);
    return !relative.empty() && *relative.begin() != std::filesystem::path("..");
}
bool Supported(const std::filesystem::path& p)
{
    const auto ext = Fold(Utf8(p.extension()));
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb";
}
DirectX::XMFLOAT2 ItemCenter()
{
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    return {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
}
void ShortText(std::string text, float width)
{
    const auto full = text;
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
    {
        ImGui::TextUnformatted(text.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", full.c_str());
        return;
    }
    while (!text.empty() && ImGui::CalcTextSize((text + "...").c_str()).x > width)
    {
        text.pop_back();
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
            text.pop_back();
        if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0xC0)
            text.pop_back();
    }
    if (text != full)
        text += "...";
    ImGui::TextUnformatted(text.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", full.c_str());
}
} // namespace
void AssetBrowser::SetRoot(const std::filesystem::path& root)
{
    m_root = std::filesystem::absolute(root).lexically_normal();
    m_folder = m_root;
    m_selected.clear();
    m_back.clear();
    m_search[0] = 0;
    Refresh();
}
void AssetBrowser::Refresh()
{
    m_index.clear();
    m_known.clear();
    m_scanStatus.clear();
    std::error_code error;
    if (!std::filesystem::is_directory(m_root, error))
    {
        m_scanStatus = "No assets folder. Use Browse to select a model.";
        return;
    }
    for (std::filesystem::recursive_directory_iterator
             it(m_root, std::filesystem::directory_options::skip_permission_denied, error),
         end;
         it != end && !error; it.increment(error))
    {
        const bool directory = it->is_directory(error);
        if (error)
            break;
        if (directory && it->is_symlink(error))
        {
            it.disable_recursion_pending();
            continue;
        }
        if (directory || (it->is_regular_file(error) && Supported(it->path())))
        {
            const auto bytes = directory ? 0 : it->file_size(error);
            if (error)
                break;
            m_index.push_back({it->path().lexically_normal(), directory, bytes});
        }
        if (m_index.size() >= 10000)
        {
            m_scanStatus = "Index limited to 10000 entries. Narrow the assets directory.";
            break;
        }
        if (it.depth() >= 48)
            it.disable_recursion_pending();
    }
    if (error)
    {
        m_scanStatus = "Some assets could not be indexed: " + error.message();
        Log::Write(LogLevel::Warning, m_scanStatus);
    }
    if (!std::filesystem::is_directory(m_folder, error))
        m_folder = m_root;
}
void AssetBrowser::Navigate(const std::filesystem::path& folder)
{
    const auto target = folder.lexically_normal();
    if (target != m_folder && Inside(target, m_root))
    {
        m_back.push_back(m_folder);
        m_folder = target;
        m_search[0] = 0;
    }
}
std::vector<AssetFile> AssetBrowser::Visible() const
{
    std::vector<AssetFile> files;
    const auto query = Fold(m_search);
    const char* extensions[] = {"", ".fbx", ".obj", ".gltf", ".glb"};
    for (const auto& entry : m_index)
    {
        if (query.empty() ? entry.path.parent_path() != m_folder : !Inside(entry.path, m_folder))
            continue;
        if (!query.empty() &&
            Fold(Utf8(entry.path.lexically_relative(m_folder))).find(query) == std::string::npos)
            continue;
        if (!entry.directory && m_type && Fold(Utf8(entry.path.extension())) != extensions[m_type])
            continue;
        files.push_back(entry);
    }
    std::sort(files.begin(), files.end(), [&](const auto& a, const auto& b) {
        if (a.directory != b.directory)
            return a.directory > b.directory;
        const auto an = Fold(Utf8(a.path.filename())), bn = Fold(Utf8(b.path.filename()));
        if (an == bn)
            return Utf8(a.path) < Utf8(b.path);
        return m_ascending ? an < bn : an > bn;
    });
    return files;
}
void AssetBrowser::Remember(const std::filesystem::path& path, std::size_t meshes,
                            std::size_t clips)
{
    const auto key = std::filesystem::absolute(path).lexically_normal();
    m_known[key] = {meshes, clips};
    m_selected = key;
}
void AssetBrowser::Draw(bool playing, const std::function<std::filesystem::path()>& browse,
                        const std::function<void(const std::filesystem::path&)>& place)
{
    ImGui::BeginDisabled(m_back.empty());
    if (ImGui::Button("Back"))
    {
        m_folder = m_back.back();
        m_back.pop_back();
        m_search[0] = 0;
    }
    m_backButton = ItemCenter();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_folder == m_root);
    if (ImGui::Button("Up"))
        Navigate(m_folder.parent_path());
    m_upButton = ItemCenter();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        Refresh();
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        const auto path = browse();
        if (!path.empty())
            m_selected = std::filesystem::absolute(path).lexically_normal();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(110.f, ImGui::GetContentRegionAvail().x - 156));
    ImGui::InputTextWithHint("##assetSearch", "Search folder + subfolders", m_search,
                             sizeof(m_search));
    m_searchBox = ItemCenter();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::Combo("##type", &m_type, "All\0FBX\0OBJ\0glTF\0GLB\0");
    ImGui::SameLine();
    if (ImGui::Button(m_ascending ? "A-Z" : "Z-A"))
        m_ascending = !m_ascending;
    ShortText(Utf8(m_folder), ImGui::GetContentRegionAvail().x);
    const float body = std::max(55.f, ImGui::GetContentRegionAvail().y - 58);
    const float treeWidth = std::clamp(ImGui::GetContentRegionAvail().x * .22f, 135.f, 220.f);
    ImGui::BeginChild("Folders", {treeWidth, body}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    auto tree = [&](auto&& self, const std::filesystem::path& folder) -> void {
        const auto key = Utf8(folder);
        bool children = false;
        for (const auto& entry : m_index)
            children |= entry.directory && entry.path.parent_path() == folder;
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (folder == m_root)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (!children)
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (folder == m_folder)
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool open =
            ImGui::TreeNodeEx(key.c_str(), flags, "%s", Utf8(folder.filename()).c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            Navigate(folder);
        if (open)
        {
            std::vector<std::filesystem::path> childrenPaths;
            for (const auto& entry : m_index)
                if (entry.directory && entry.path.parent_path() == folder)
                    childrenPaths.push_back(entry.path);
            std::sort(childrenPaths.begin(), childrenPaths.end());
            for (const auto& path : childrenPaths)
                self(self, path);
            ImGui::TreePop();
        }
    };
    tree(tree, m_root);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Files", {0, body}, ImGuiChildFlags_Borders);
    auto files = Visible();
    m_rowCenters.clear();
    if (ImGui::BeginTable("Asset files", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("File / folder", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 58);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 68);
        ImGui::TableHeadersRow();
        for (const auto& entry : files)
        {
            ImGui::PushID(Utf8(entry.path).c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(Utf8(entry.path.filename()).c_str(), m_selected == entry.path,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick))
            {
                m_selected = entry.path;
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    if (entry.directory)
                        Navigate(entry.path);
                    else if (!playing)
                        place(entry.path);
                }
            }
            m_rowCenters[entry.path] = ItemCenter();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Utf8(entry.path).c_str());
            if (!playing && !entry.directory && ImGui::BeginDragDropSource())
            {
                const auto text = Utf8(entry.path);
                ImGui::SetDragDropPayload("NEREIDES_MODEL", text.c_str(), text.size() + 1);
                ImGui::TextUnformatted(Utf8(entry.path.filename()).c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s",
                                entry.directory ? "Folder" : Utf8(entry.path.extension()).c_str());
            ImGui::TableSetColumnIndex(2);
            if (!entry.directory)
                ImGui::Text("%.1f KB", double(entry.bytes) / 1024);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (files.empty())
        ImGui::TextWrapped(m_search[0]
                               ? "No matching files. Try another name or format."
                               : "Choose a folder on the left, or search below this folder.");
    ImGui::EndChild();
    ImGui::Separator();
    std::error_code error;
    const bool file = !m_selected.empty() && std::filesystem::is_regular_file(m_selected, error);
    ImGui::BeginDisabled(playing || !file);
    if (ImGui::Button("Place in Scene"))
        place(m_selected);
    m_placeButton = ItemCenter();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ShortText(m_selected.empty() ? "Select a model to inspect and place"
                                 : Utf8(m_selected.filename()),
              ImGui::GetContentRegionAvail().x);
    if (!m_selected.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Utf8(m_selected).c_str());
    if (auto found = m_known.find(m_selected); found != m_known.end())
        ImGui::TextDisabled("Last import: %zu meshes / %zu clips", found->second.meshes,
                            found->second.clips);
    else if (!m_scanStatus.empty())
        ShortText(m_scanStatus, ImGui::GetContentRegionAvail().x);
    else
        ImGui::TextDisabled("%zu entries | Double-click or drag a file into Scene", files.size());
}
} // namespace nereides
