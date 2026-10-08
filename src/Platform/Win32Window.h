#pragma once

#include <Windows.h>
#include <cstdint>
#include <functional>

namespace nereides
{
class Input;

// Windows 창을 만들고, 운영체제에서 들어오는 메시지를 엔진에 연결
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

    void SetCloseGuard(std::function<bool()> guard)
    {
        m_closeGuard = std::move(guard);
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

    // 창 닫기 여부 판단
    std::function<bool()> m_closeGuard;

    // 메시지를 에디터에도 전달
    std::function<void(HWND, UINT, WPARAM, LPARAM)> m_messageHandler;
    std::uint32_t m_width = 1280;
    std::uint32_t m_height = 720;
};
} // namespace nereides
