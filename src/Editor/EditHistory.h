#pragma once
#include <string>
#include <vector>
#include <stdexcept>

namespace nereides
{
// History contains authored scene data, never runtime or editor camera state.
class EditHistory final
{
public:
    /*
    기존 기록 제거
     → 전달받은 상태 하나만 보관
     → 현재 위치를 0으로 설정
     → 이 상태를 저장된 기준으로 설정
    */
    void Reset(std::string state)
    {
        m_states = {std::move(state)};
        m_cursor = 0;
        m_saved = m_states.front();
    }

    // 새로운 편집 결과 기록 (에디터의 편집 상태)
    void Commit(std::string state)
    {
        if (state == Current())
            return;

        m_states.resize(m_cursor + 1);
        m_states.push_back(std::move(state));
        ++m_cursor;

        // Bound both count and memory; always retain the current scene.
        std::size_t bytes = 0;

        for (const auto& entry : m_states)
            bytes += entry.size();

        // 기록 개수와 메모리 제한
        while (m_states.size() > 1 && (m_states.size() > 65 || bytes > 32 * 1024 * 1024))
        {
            bytes -= m_states.front().size();
            m_states.erase(m_states.begin());
            --m_cursor;
        }
    }

    const std::string& Current() const
    {
        return m_states.at(m_cursor);
    }

    bool CanUndo() const
    {
        return m_cursor > 0;
    }

    bool CanRedo() const
    {
        return m_cursor + 1 < m_states.size();
    }

    // 복원할 상태 반환 (대상 문자열만 반환)
    const std::string& Target(int direction) const
    {
        if ((direction != -1 && direction != 1) || (direction < 0 ? !CanUndo() : !CanRedo()))
            throw std::out_of_range("History boundary");

        return m_states.at(static_cast<std::size_t>(static_cast<int>(m_cursor) + direction));
    }

    // Call only after the target scene successfully decodes.
    // 복원할 상태로 확정 후 기록 위치 이동
    void Accept(int direction)
    {
        (void)Target(direction);
        m_cursor += direction;
    }

    // 현재 상태를 저장 기준으로 설정
    void MarkSaved()
    {
        m_saved = Current();
    }

    //현재 기록이 저장 기준과 다른지 여부 반환
    bool Dirty() const
    {
        return Current() != m_saved;
    }

private:
    std::vector<std::string> m_states;
    std::string m_saved;
    std::size_t m_cursor = 0;
};
} // namespace nereides
