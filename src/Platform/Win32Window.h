#pragma once

#include <Windows.h>
#include <cstdint>
#include <functional>

namespace nereides
{
class Input;
// Owns one Win32 window. DX11 and input integration will be added in later steps.
class Win32Window final
{
public:
    Win32Window() = default;
    ~Win32Window();
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    bool Create(HINSTANCE instance, int showCommand);
    bool ProcessMessages(int& exitCode);
    void SetInput(Input* input) noexcept
    {
        m_input = input;
    }
    void SetMessageHandler(std::function<void(HWND, UINT, WPARAM, LPARAM)> handler)
    {
        m_messageHandler = std::move(handler);
    }
    HWND Handle() const noexcept
    {
        return m_window;
    }
    std::uint32_t Width() const noexcept
    {
        return m_width;
    }
    std::uint32_t Height() const noexcept
    {
        return m_height;
    }
    bool IsMinimized() const noexcept
    {
        return m_minimized;
    }
    bool IsFocused() const noexcept
    {
        return m_focused;
    }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT ProcessMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Destroy() noexcept;

    HINSTANCE m_instance = nullptr;
    HWND m_window = nullptr;
    bool m_classRegistered = false;
    bool m_minimized = false;
    bool m_focused = false;
    Input* m_input = nullptr;
    std::function<void(HWND, UINT, WPARAM, LPARAM)> m_messageHandler;
    std::uint32_t m_width = 1280;
    std::uint32_t m_height = 720;
};
} // namespace nereides
