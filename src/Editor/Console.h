#pragma once
#include "Core/Log.h"
#include <DirectXMath.h>
namespace nereides
{
struct ConsoleRow
{
    LogEntry entry;
    unsigned count = 1;
    bool focused = false;
};
std::vector<ConsoleRow> FilterLogs(const std::vector<LogEntry>&, std::uint64_t after, int level,
                                   const std::string& search, bool collapse,
                                   std::uint64_t focus = 0);
class Console final
{
public:
    bool open = false;
    void Draw();
    void Reveal(std::uint64_t id);
    unsigned UnreadErrors() const;

private:
    friend struct EditorTestAccess;
    std::uint64_t m_clearThrough = 0, m_seenThrough = 0, m_focus = 0, m_lastDrawn = 0;
    bool m_collapse = true, m_follow = true, m_requestFocus = false;
    int m_level = 0;
    char m_search[256]{};
    LogEntry m_selected;
    DirectX::XMFLOAT2 m_clearButton{}, m_errorFilter{}, m_searchBox{};
};
} // namespace nereides
