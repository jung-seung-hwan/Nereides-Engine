#pragma once
#include <array>
#include <cstddef>
namespace nereides
{
// Call BeginFrame before pumping platform events. Content owns input buffering.
class Input final
{
public:
    void BeginFrame() noexcept
    { m_pressed.fill(false); m_released.fill(false); m_mouseX = m_mouseY = m_wheel = 0; }
    void SetKey(unsigned key, bool down) noexcept
    {
        if (key >= m_down.size()) return;
        if (!down) m_blocked[key] = false;
        if (down && (!m_focused || m_blocked[key])) return;
        if (down && !m_down[key]) m_pressed[key] = true;
        if (!down && m_down[key]) m_released[key] = true;
        m_down[key] = down;
    }
    void SetFocus(bool focused) noexcept
    { if (focused != m_focused) DiscardHeld(); m_focused = focused; }
    void ReconcilePhysicalKey(unsigned key, bool physicallyDown) noexcept
    { if (key < 256 && !physicallyDown) m_blocked[key] = false; }
    // Held actions require a release before they can start again.
    void DiscardHeld() noexcept
    {
        for (std::size_t i = 0; i < m_down.size(); ++i) m_blocked[i] = m_blocked[i] || m_down[i];
        m_down.fill(false); BeginFrame();
    }
    void AddMouseDelta(int x, int y) noexcept { if (m_focused) { m_mouseX += x; m_mouseY += y; } }
    void Capture(bool keyboard,bool mouse) noexcept
    {
        for(unsigned key=0;key<256;++key) if((key<8 ? mouse : keyboard))
        {
            m_blocked[key]=m_blocked[key]||m_down[key];
            m_down[key]=m_pressed[key]=m_released[key]=false;
        }
        if(mouse) m_mouseX=m_mouseY=m_wheel=0;
    }
    void AddWheel(int amount) noexcept { if (m_focused) m_wheel += amount; }
    bool Held(unsigned key) const noexcept { return key < 256 && m_down[key]; }
    bool Pressed(unsigned key) const noexcept { return key < 256 && m_pressed[key]; }
    bool Released(unsigned key) const noexcept { return key < 256 && m_released[key]; }
    int MouseX() const noexcept { return m_mouseX; }
    int MouseY() const noexcept { return m_mouseY; }
    int Wheel() const noexcept { return m_wheel; }
private:
    std::array<bool, 256> m_down{}, m_pressed{}, m_released{}, m_blocked{};
    bool m_focused = true;
    int m_mouseX = 0, m_mouseY = 0, m_wheel = 0;
};
}
