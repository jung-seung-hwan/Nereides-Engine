#include "Editor/Console.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <map>
namespace nereides
{
namespace
{
std::string Fold(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}
DirectX::XMFLOAT2 ItemCenter()
{
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    return {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
}
ImVec4 LevelColor(LogLevel level)
{
    return level == LogLevel::Error     ? ImVec4(1, .4f, .35f, 1)
           : level == LogLevel::Warning ? ImVec4(1, .78f, .3f, 1)
                                        : ImVec4(.65f, .8f, .95f, 1);
}
} // namespace
std::vector<ConsoleRow> FilterLogs(const std::vector<LogEntry>& entries, std::uint64_t after,
                                   int level, const std::string& search, bool collapse,
                                   std::uint64_t focus)
{
    std::vector<ConsoleRow> rows;
    std::map<std::pair<LogLevel, std::string>, std::size_t> groups;
    const auto query = Fold(search);
    for (const auto& entry : entries)
    {
        if (entry.id <= after || (level && int(entry.level) != level - 1) ||
            (!query.empty() && Fold(Log::Format(entry)).find(query) == std::string::npos))
            continue;
        const auto key = std::make_pair(entry.level, entry.message);
        if (collapse)
            if (const auto found = groups.find(key); found != groups.end())
            {
                auto& row = rows[found->second];
                row.entry = entry;
                ++row.count;
                row.focused |= entry.id == focus;
                continue;
            }
        groups[key] = rows.size();
        rows.push_back({entry, 1, entry.id == focus});
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.entry.id < b.entry.id; });
    return rows;
}
unsigned Console::UnreadErrors() const
{
    unsigned count = 0;
    for (const auto& entry : Log::Entries())
        if (entry.id > std::max(m_seenThrough, m_clearThrough) && entry.level == LogLevel::Error)
            ++count;
    return count;
}
void Console::Reveal(std::uint64_t id)
{
    open = true;
    m_focus = id;
    m_clearThrough = 0;
    m_level = 0;
    m_search[0] = 0;
    m_requestFocus = true;
}
void Console::Draw()
{
    if (!open)
        return;
    ImGui::SetNextWindowPos({55, 145}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({840, 460}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({520, 300}, {FLT_MAX, FLT_MAX});
    if (m_requestFocus)
    {
        ImGui::SetNextWindowFocus();
        m_requestFocus = false;
    }
    if (!ImGui::Begin("Console", &open))
    {
        ImGui::End();
        return;
    }
    const auto entries = Log::Entries();
    const auto newest = entries.empty() ? 0 : entries.back().id;
    m_seenThrough = newest;
    const char* levels[] = {"All", "Info", "Warning", "Error"};
    for (int i = 0; i < 4; ++i)
    {
        if (i)
            ImGui::SameLine();
        ImGui::RadioButton(levels[i], &m_level, i);
        if (i == 3)
            m_errorFilter = ItemCenter();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Group repeats", &m_collapse);
    if (ImGui::GetContentRegionAvail().x > 560)
        ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_follow);
    ImGui::SetNextItemWidth(std::max(120.f, ImGui::GetContentRegionAvail().x - 210));
    ImGui::InputTextWithHint("##logsearch", "Search messages / paths", m_search, sizeof(m_search));
    m_searchBox = ItemCenter();
    ImGui::SameLine();
    if (ImGui::Button("Clear view"))
    {
        m_clearThrough = newest;
        m_focus = 0;
        m_selected = {};
    }
    m_clearButton = ItemCenter();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hides current entries. logs/engine.log is preserved.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_selected.id);
    if (ImGui::Button("Copy selected"))
        ImGui::SetClipboardText(Log::Format(m_selected).c_str());
    ImGui::EndDisabled();
    auto rows = FilterLogs(entries, m_clearThrough, m_level, m_search, m_collapse, m_focus);
    ImGui::TextDisabled("%zu rows / latest 1000 entries | logs/engine.log", rows.size());
    ImGui::BeginChild("Log entries", {0, std::max(80.f, ImGui::GetContentRegionAvail().y - 125)},
                      ImGuiChildFlags_Borders);
    const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2;
    bool scrolledToSelection = false;
    if (ImGui::BeginTable("Records", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 108);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 42);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const auto& row : rows)
        {
            ImGui::PushID(std::to_string(row.entry.id).c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const auto time =
                row.entry.time.size() > 11 ? row.entry.time.substr(11) : row.entry.time;
            const bool selected =
                m_selected.id == row.entry.id ||
                (m_collapse && m_selected.id && m_selected.level == row.entry.level &&
                 m_selected.message == row.entry.message);
            if (selected)
                m_selected = row.entry;
            if (ImGui::Selectable(time.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                m_selected = row.entry;
            if (row.focused)
            {
                m_selected = row.entry;
                ImGui::SetScrollHereY(.5f);
                scrolledToSelection = true;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(LevelColor(row.entry.level), "%s", Log::Label(row.entry.level));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", row.count);
            ImGui::TableSetColumnIndex(3);
            auto summary = row.entry.message.substr(0, row.entry.message.find('\n'));
            if (summary.size() > 220)
            {
                auto end = std::size_t(220);
                while (end && (static_cast<unsigned char>(summary[end]) & 0xC0) == 0x80)
                    --end;
                summary.resize(end);
                summary += "...";
            }
            ImGui::TextUnformatted(summary.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rows.empty())
        ImGui::TextDisabled("No matching log entries.");
    if (m_focus && !scrolledToSelection)
        ImGui::TextWrapped(
            "The requested entry has expired from the recent buffer. See logs/engine.log.");
    if (m_follow && wasAtBottom && newest != m_lastDrawn && !scrolledToSelection)
        ImGui::SetScrollHereY(1);
    m_focus = 0;
    m_lastDrawn = newest;
    ImGui::EndChild();
    ImGui::SeparatorText("Selected message");
    ImGui::BeginChild("Log detail", {0, 0}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (m_selected.id)
    {
        ImGui::TextColored(LevelColor(m_selected.level), "%s  [%s]", m_selected.time.c_str(),
                           Log::Label(m_selected.level));
        ImGui::TextUnformatted(m_selected.message.c_str());
    }
    else
        ImGui::TextDisabled("Select a row to read the full message and path.");
    ImGui::EndChild();
    ImGui::End();
}
} // namespace nereides
