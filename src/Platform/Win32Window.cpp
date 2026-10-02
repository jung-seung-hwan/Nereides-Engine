#include "Platform/Win32Window.h"

#include <string>

namespace
{
constexpr wchar_t kClassName[] = L"NereidesEngineWindow";

void ReportError(const wchar_t* operation, DWORD error)
{
    wchar_t* systemMessage = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<wchar_t*>(&systemMessage), 0, nullptr);
    const std::wstring message = std::wstring(operation) + L" failed (Win32 " +
        std::to_wstring(error) + L"): " + (systemMessage ? systemMessage : L"Unknown error");
    OutputDebugStringW(message.c_str());
    OutputDebugStringW(L"\n");
    MessageBoxW(nullptr, message.c_str(), L"Nereides Engine error", MB_OK | MB_ICONERROR);
    if (systemMessage) LocalFree(systemMessage);
}
}

namespace nereides
{
Win32Window::~Win32Window()
{
    Destroy();
}

bool Win32Window::Create(HINSTANCE instance, int showCommand)
{
    if (m_classRegistered || m_window) return false;
    m_instance = instance;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    if (!RegisterClassExW(&windowClass))
    {
        ReportError(L"RegisterClassExW", GetLastError());
        return false;
    }
    m_classRegistered = true;

    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    RECT bounds{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    if (!AdjustWindowRectEx(&bounds, style, FALSE, 0))
    {
        ReportError(L"AdjustWindowRectEx", GetLastError());
        Destroy();
        return false;
    }
    m_window = CreateWindowExW(0, kClassName, L"Nereides Engine | Step 3 - Triangle",
        style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
        bounds.bottom - bounds.top, nullptr, nullptr, instance, this);
    if (!m_window)
    {
        ReportError(L"CreateWindowExW", GetLastError());
        Destroy();
        return false;
    }
    ShowWindow(m_window, showCommand);
    UpdateWindow(m_window);
    OutputDebugStringW(L"[Nereides] Win32 window created.\n");
    return true;
}

bool Win32Window::ProcessMessages(int& exitCode)
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            exitCode = static_cast<int>(message.wParam);
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

LRESULT CALLBACK Win32Window::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<Win32Window*>(create->lpCreateParams);
        self->m_window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    const LRESULT result = self->ProcessMessage(window, message, wParam, lParam);
    if (message == WM_NCDESTROY)
    {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->m_window = nullptr;
    }
    return result;
}

LRESULT Win32Window::ProcessMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        m_minimized = wParam == SIZE_MINIMIZED;
        m_width = LOWORD(lParam);
        m_height = HIWORD(lParam);
        OutputDebugStringW(m_minimized ? L"[Nereides] Minimized.\n" : L"[Nereides] Size changed.\n");
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        OutputDebugStringW(L"[Nereides] Window destroyed.\n");
        PostQuitMessage(0);
        return 0;
    }
    // Keep native system shortcuts, including Alt+F4, working.
    return DefWindowProcW(window, message, wParam, lParam);
}

void Win32Window::Destroy() noexcept
{
    if (m_window) DestroyWindow(m_window);
    if (m_classRegistered)
    {
        UnregisterClassW(kClassName, m_instance);
        m_classRegistered = false;
    }
}
}
