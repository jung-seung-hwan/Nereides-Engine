#pragma once
#include "Core/Log.h"
#include <DirectXMath.h>

namespace nereides
{
// 화면에 표시할 로그 한 행. 대표 로그, 반복 횟수, 이동 대상 여부를 보관
struct ConsoleRow
{
    LogEntry entry;
    unsigned count = 1;
    bool focused = false;
};

// 표시 경계·심각도·검색어로 로그를 걸러내고, 필요하면 같은 메시지를 묶어 반환
// 원본 로그는 변경하지 않음
std::vector<ConsoleRow> FilterLogs(const std::vector<LogEntry>&, std::uint64_t after, int level,
                                   const std::string& search, bool collapse,
                                   std::uint64_t focus = 0);

// Log의 최근 기록을 조회해 검색·필터·선택·상세 표시하는 에디터 창
class Console final
{
public:
    bool open = false;

    // Console 창의 UI 구성과 사용자 조작 처리
    void Draw();

    // 창을 열고 표시 필터를 초기화해 지정한 로그 ID로 이동 요청
    void Reveal(std::uint64_t id);

    // 최근 로그 중 읽음·숨김 경계 이후에 발생한 Error 건수 반환
    unsigned UnreadErrors() const;

private:
    friend struct EditorTestAccess;

    // m_clearThrough: 이 ID까지 화면에서 숨김
    // m_seenThrough: 이 ID까지 확인한 것으로 처리
    // m_focus: 다음 표시에서 선택하고 이동할 로그 ID
    // m_lastDrawn: 이전 표시 시점의 최신 로그 ID
    std::uint64_t m_clearThrough = 0, m_seenThrough = 0, m_focus = 0, m_lastDrawn = 0;
    bool m_collapse = true, m_follow = true, m_requestFocus = false;
    int m_level = 0;
    char m_search[256]{};
    LogEntry m_selected;
    DirectX::XMFLOAT2 m_clearButton{}, m_errorFilter{}, m_searchBox{};
};
} // namespace nereides
