#include "Window.h"
#include <sstream>

// The purpose of this Window class is to encapsulate the window creation
Window::WindowClass Window::WindowClass::wndClass;

Window::WindowClass::WindowClass() noexcept
    : hInst(GetModuleHandle(nullptr))
{
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    // initial setup proc
    wc.lpfnWndProc = HandleMsgSetup;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetInstance();
    wc.hIcon = nullptr;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = GetName();
    wc.hIconSm = nullptr;
    RegisterClassEx(&wc);
}

Window::WindowClass::~WindowClass()
{
    UnregisterClass(wndClassName, GetInstance());
}

const char*
Window::WindowClass::GetName() noexcept
{
    return wndClassName;
}

HINSTANCE
Window::WindowClass::GetInstance() noexcept
{
    return wndClass.hInst;
}

// Window Stuff
Window::Window(int width, int height, const char* name)
{
    // calculate window size based on desired client region size
    RECT wr;
    wr.left = 100;
    wr.right = width + wr.left;
    wr.top = 100;
    wr.bottom = height + wr.top;
    if (FAILED(AdjustWindowRect(
        &wr, WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU, FALSE))) {
        throw ENGINE_WINDOW_LAST_EXCEPT();
    }
    // create window & get hWnd. The class name must match the one in WindowClass
    hWnd = CreateWindow(WindowClass::GetName(),
        name,
        WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        WindowClass::GetInstance(),
        this);
    if (hWnd == nullptr) {
        throw ENGINE_WINDOW_LAST_EXCEPT();
    }
    // show window
    ShowWindow(hWnd, SW_SHOWDEFAULT);
}

Window::~Window()
{
    DestroyWindow(hWnd);
}

LRESULT CALLBACK
Window::HandleMsgSetup(HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam) noexcept
{
    // use create parameter passed in from CreateWindow() to store window class
    // pointer at WinAPI side
    if (msg == WM_NCCREATE) {
        // extract ptr to window class from creation data
        const CREATESTRUCTW* const pCreate =
            reinterpret_cast<CREATESTRUCTW*>(lParam);
        Window* const pWnd = static_cast<Window*>(pCreate->lpCreateParams);
        // set WinAPI-managed user data to store ptr to window class
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWnd));
        // set message proc to normal (non-setup) handler now that setup is finished
        SetWindowLongPtr(
            hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Window::HandleMsgThunk));
        // forward message to window class handler
        return pWnd->HandleMsg(hWnd, msg, wParam, lParam);
    }
    // if we get a message before the WM_NCCREATE message, handle with default
    // handler
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK
Window::HandleMsgThunk(HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam) noexcept
{
    // retrieve ptr to window class
    Window* const pWnd =
        reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    // forward message to window class handler
    return pWnd->HandleMsg(hWnd, msg, wParam, lParam);
}

LRESULT
Window::HandleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (msg) {
        // we don't want the DefProc to handle this message because
        // we want our destructor to destroy the window, so return 0 instead of
        // break
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
        // prevents pop ups from creating zombie keys in buffer.
    case WM_KILLFOCUS:
        kbd.ClearState();
        break;
        /********* Keyboard Messages ***********/
    case WM_KEYDOWN:
        // tracks alt and f10, etc
    case WM_SYSKEYDOWN:
        // lParam bit 30 is set if the key was previously down
        if (!(lParam & 0x40000000) || kbd.AutorepeatIsEnabled()) {
            kbd.OnKeyPressed(static_cast<unsigned char>(wParam));
        }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        // tracks alt and f10, etc
        kbd.OnKeyReleased(static_cast<unsigned char>(wParam));
        break;
    case WM_CHAR:
        kbd.OnChar(static_cast<unsigned char>(wParam));
        break;
        /********* End Keyboard Messages ***********/
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Window Exception Implementation
Window::Exception::Exception(int line, const char* file, HRESULT hr) noexcept
// initializes class with EngineException constructor
    : EngineException(line, file)
    , hr(hr)
{
}

const char*
Window::Exception::GetType() const noexcept
{
    return "Engine Window Exception";
}

HRESULT
Window::Exception::GetErrorCode() const noexcept
{
    return hr;
}

char const*
Window::Exception::what() const noexcept
{
    std::ostringstream oss;
    oss << GetType() << std::endl
        << "[Error Code] " << GetErrorCode() << std::endl
        << "[Description] " << GetErrorString() << std::endl
        << GetOriginString();
    whatBuffer = oss.str();
    return whatBuffer.c_str();
}

std::string
Window::Exception::GetErrorString() const noexcept
{
    return TranslateErrorCode(hr);
}

std::string
Window::Exception::TranslateErrorCode(HRESULT hr) noexcept
{
    char* pMsgBuf = nullptr;

    // We have windows allocate to a pointer. We then pass a pointer to the
    // pMsgBug pointer to FormatMessage. nMsgLen is the length of that string.
    DWORD nMsgLen = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        // we do typecasting here because FormatMessage expects a pointer to a char
        // but we need to pass a pointer to a pointer.
        reinterpret_cast<LPSTR>(&pMsgBuf),
        0,
        nullptr);
    if (nMsgLen == 0) {
        return "Unidentified error code";
    }
    // copy error string from windows-allocated buffer to std::string
    std::string errorString = pMsgBuf;
    // LocalFree is used to free windows-allocated buffers.
    LocalFree(pMsgBuf);
    return errorString;
}