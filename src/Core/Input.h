#pragma once
#include <array>
#include <cstddef>

namespace nereides
{
// Call BeginFrame before pumping platform events. Content owns input buffering.
// MoveComponent.h에서 Held시 사용
// Windows 입력 메세지 -> Win32Window 메시지 처리 -> Input에 W 상태 저장 -> MoveComponent가 Held('W') 조회
class Input final
{
public:
    // 이전 프레임의 누름·뗌 기록과 마우스 변화량 초기화
    // 누르고 있는 상태(m_down)는 유지
    void BeginFrame() noexcept
    {
        m_pressed.fill(false);
        m_released.fill(false);
        m_mouseX = m_mouseY = m_wheel = 0;
    }

    void SetKey(unsigned key, bool down) noexcept
    {
        if (key >= m_down.size())
            return;

        if (!down)
            m_blocked[key] = false;

        // 누름 입력은 창 포커스가 없거나 해당 키가 차단 중이면 무시
        if (down && (!m_focused || m_blocked[key]))
            return;

        // 기존 상태비교
        // 지금 누름 입력이 들어왔고, 이전에는 누르지 않았다면 → 새로 누른 것
        if (down && !m_down[key])
            m_pressed[key] = true;

        // 기존 상태 비교
        // 지금 뗌 입력이 들어왔고, 이전에는 누르고 있었다면 → 새로 뗀 것
        if (!down && m_down[key])
            m_released[key] = true;

        m_down[key] = down;
    }

    void SetFocus(bool focused) noexcept
    {
        if (focused != m_focused)
            DiscardHeld();
        m_focused = focused;
    }

    void ReconcilePhysicalKey(unsigned key, bool physicallyDown) noexcept
    {
        if (key < 256 && !physicallyDown)
            m_blocked[key] = false;
    }

    // Held actions require a release before they can start again.
    // 기존 입력 상태를 초기화하고, 눌려 있던 키는 뗀 뒤 다시 눌러야 입력 허용
    // 기존 입력 때문에 일시정지 해제나 전환 직후 행동이 자동으로 이어지는 것을 막는 데 사용
    void DiscardHeld() noexcept
    {
        for (std::size_t i = 0; i < m_down.size(); ++i)
            m_blocked[i] = m_blocked[i] || m_down[i];

        m_down.fill(false);

        BeginFrame();
    }

    void AddMouseDelta(int x, int y) noexcept
    {
        if (m_focused)
        {
            m_mouseX += x;
            m_mouseY += y;
        }
    }

    // 에디터가 사용할 입력을 게임에서 제외
    void Capture(bool keyboard, bool mouse) noexcept
    {
        for (unsigned key = 0; key < 256; ++key)
            if ((key < 8 ? mouse : keyboard))
            {
                m_blocked[key] = m_blocked[key] || m_down[key];
                m_down[key] = m_pressed[key] = m_released[key] = false;
            }

        if (mouse)
            m_mouseX = m_mouseY = m_wheel = 0;
    }

    void AddWheel(int amount) noexcept
    {
        if (m_focused)
            m_wheel += amount;
    }

    // 이동에는 Held(), 한 번 눌러 상태를 전환하는 기능에는 Pressed()
    bool Held(unsigned key) const noexcept
    {
        return key < 256 && m_down[key];
    }

    bool Pressed(unsigned key) const noexcept
    {
        return key < 256 && m_pressed[key];
    }

    bool Released(unsigned key) const noexcept
    {
        return key < 256 && m_released[key];
    }

    int MouseX() const noexcept
    {
        return m_mouseX;
    }

    int MouseY() const noexcept
    {
        return m_mouseY;
    }

    int Wheel() const noexcept
    {
        return m_wheel;
    }

private:
    std::array<bool, 256> m_down{}, m_pressed{}, m_released{}, m_blocked{};
    bool m_focused = true;
    int m_mouseX = 0, m_mouseY = 0, m_wheel = 0;
};
} // namespace nereides
