#include "Platform/Win32Window.h"
#include "Core/Input.h"

#include <string>

namespace
{
constexpr wchar_t kClassName[] = L"NereidesEngineWindow";

void ReportError(const wchar_t* operation, DWORD error)
{
    wchar_t* systemMessage = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<wchar_t*>(&systemMessage), 0, nullptr);
    const std::wstring message = std::wstring(operation) + L" failed (Win32 " +
                                 std::to_wstring(error) + L"): " +
                                 (systemMessage ? systemMessage : L"Unknown error");
    OutputDebugStringW(message.c_str());
    OutputDebugStringW(L"\n");
    MessageBoxW(nullptr, message.c_str(), L"Nereides Engine error", MB_OK | MB_ICONERROR);
    if (systemMessage)
        LocalFree(systemMessage);
}
} // namespace

namespace nereides
{
Win32Window::~Win32Window()
{
    Destroy();
}

bool Win32Window::Create(HINSTANCE instance, int showCommand)
{
    if (m_classRegistered || m_window)
        return false;
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
    m_window = CreateWindowExW(0, kClassName, L"Nereides Engine | Step 3 - Triangle", style,
                               CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                               bounds.bottom - bounds.top, nullptr, nullptr, instance, this);
    if (!m_window)
    {
        ReportError(L"CreateWindowExW", GetLastError());
        Destroy();
        return false;
    }
    ShowWindow(m_window, showCommand);
    RAWINPUTDEVICE mouse{0x01, 0x02, 0, m_window};
    if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse)))
    {
        ReportError(L"RegisterRawInputDevices", GetLastError());
        return false;
    }
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
    if (!self)
        return DefWindowProcW(window, message, wParam, lParam);
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
    if (m_messageHandler)
        m_messageHandler(window, message, wParam, lParam);
    switch (message)
    {
    case WM_SETFOCUS:
        m_focused = true;
        if (m_input)
        {
            m_input->SetFocus(true);
            for (unsigned key = 0; key < 256; ++key)
                m_input->ReconcilePhysicalKey(
                    key, (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0);
        }
        break;
    case WM_KILLFOCUS:
        m_focused = false;
        if (m_input)
            m_input->SetFocus(false);
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (m_input)
            m_input->SetKey(static_cast<unsigned>(wParam), true);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (m_input)
            m_input->SetKey(static_cast<unsigned>(wParam), false);
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        if (m_input)
            m_input->SetKey(VK_LBUTTON, message == WM_LBUTTONDOWN);
        if (message == WM_LBUTTONDOWN)
            SetCapture(window);
        else if (m_input && !m_input->Held(VK_RBUTTON) && GetCapture() == window)
            ReleaseCapture();
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        if (m_input)
            m_input->SetKey(VK_RBUTTON, message == WM_RBUTTONDOWN);
        if (message == WM_RBUTTONDOWN)
            SetCapture(window);
        else if (m_input && !m_input->Held(VK_LBUTTON) && GetCapture() == window)
            ReleaseCapture();
        break;
    case WM_CAPTURECHANGED:
        if (m_input)
        {
            m_input->SetKey(VK_LBUTTON, false);
            m_input->SetKey(VK_RBUTTON, false);
        }
        break;
    case WM_MOUSEWHEEL:
        if (m_input)
            m_input->AddWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        break;
    case WM_INPUT:
        if (m_input)
        {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size,
                                sizeof(RAWINPUTHEADER)) != UINT(-1) &&
                raw.header.dwType == RIM_TYPEMOUSE &&
                !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
                m_input->AddMouseDelta(raw.data.mouse.lLastX, raw.data.mouse.lLastY);
        }
        break;
    case WM_SIZE:
        m_minimized = wParam == SIZE_MINIMIZED;
        if (m_minimized && m_input)
            m_input->DiscardHeld();
        m_width = LOWORD(lParam);
        m_height = HIWORD(lParam);
        OutputDebugStringW(m_minimized ? L"[Nereides] Minimized.\n"
                                       : L"[Nereides] Size changed.\n");
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
    if (m_window)
        DestroyWindow(m_window);
    if (m_classRegistered)
    {
        UnregisterClassW(kClassName, m_instance);
        m_classRegistered = false;
    }
}
} // namespace nereides
