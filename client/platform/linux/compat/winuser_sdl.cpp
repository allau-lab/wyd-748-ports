// HWND = SDL_Window* — fila de mensagens Win32 alimentada por SDL_PollEvent.
#include "winuser_sdl.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef WYD_USE_SDL3
#  include <SDL3/SDL.h>
#  include <SDL3/SDL_events.h>
#  include <SDL3/SDL_mouse.h>
#  include <SDL3/SDL_timer.h>
#  include <SDL3/SDL_video.h>
#else
#  include <SDL.h>
#endif

namespace
{

struct ClassInfo
{
    WNDPROC proc = nullptr;
    std::string name;
};

struct WindowInfo
{
    WNDPROC proc = nullptr;
    std::string title;
    DWORD style = 0;
    bool alive = true;
};

std::mutex g_mtx;
std::unordered_map<std::string, ClassInfo> g_classes;
std::unordered_map<HWND, WindowInfo> g_windows;
std::deque<MSG> g_queue;
bool g_quitPosted = false;
HCURSOR g_cursor = nullptr; // default: cursor software (SCursor); SO oculto
SDL_Cursor* g_blankCursor = nullptr;
int g_cursorShowCount = 0; // espelha a API Win32 (contador)

SDL_Window* PrimaryWindow()
{
    if (g_windows.empty())
        return nullptr;
    return reinterpret_cast<SDL_Window*>(g_windows.begin()->first);
}

void EnsureBlankCursor()
{
    if (g_blankCursor)
        return;
    // 8x8 totalmente transparente — mais confiável que ShowCursor(DISABLE) no Wayland.
    Uint8 data[8] {};
    Uint8 mask[8] {};
    g_blankCursor = SDL_CreateCursor(data, mask, 8, 8, 0, 0);
}

void HideSystemCursor()
{
    EnsureBlankCursor();
#ifdef WYD_USE_SDL3
    if (g_blankCursor)
        SDL_SetCursor(g_blankCursor);
    SDL_ShowCursor();
#else
    if (g_blankCursor)
        SDL_SetCursor(g_blankCursor);
    SDL_ShowCursor(SDL_DISABLE);
#endif
}

void ShowSystemCursor()
{
#ifdef WYD_USE_SDL3
    if (SDL_Cursor* arrow = SDL_GetDefaultCursor())
        SDL_SetCursor(arrow);
    SDL_ShowCursor();
#else
    if (SDL_Cursor* arrow = SDL_GetDefaultCursor())
        SDL_SetCursor(arrow);
    SDL_ShowCursor(SDL_ENABLE);
#endif
}

// Captura eventos do mouse sem prender/confinar o ponteiro (sem grab/relative).
void SoftCaptureMouse(bool enable)
{
#ifdef WYD_USE_SDL3
    if (SDL_Window* win = PrimaryWindow()) {
        SDL_SetWindowRelativeMouseMode(win, false);
        SDL_SetWindowMouseGrab(win, false);
    }
    SDL_CaptureMouse(enable ? true : false);
#else
    SDL_SetRelativeMouseMode(SDL_FALSE);
    if (SDL_Window* win = PrimaryWindow())
        SDL_SetWindowGrab(win, SDL_FALSE);
    SDL_CaptureMouse(enable ? SDL_TRUE : SDL_FALSE);
#endif
}

// Ícone idêntico ao WYD.exe (RT_GROUP_ICON 101, 32x32).
#include "wyd_exe_icon.inc"
void ApplyWydExeWindowIcon(SDL_Window* win)
{
    if (!win)
        return;
#ifdef WYD_USE_SDL3
    SDL_Surface* surf = SDL_CreateSurfaceFrom(
        kWydExeIconW, kWydExeIconH,
        SDL_PIXELFORMAT_RGBA8888,
        const_cast<unsigned char*>(kWydExeIconRGBA),
        kWydExeIconW * 4);
    if (!surf)
        return;
    SDL_SetWindowIcon(win, surf);
    SDL_DestroySurface(surf);
#else
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<unsigned char*>(kWydExeIconRGBA),
        kWydExeIconW, kWydExeIconH, 32, kWydExeIconW * 4,
        SDL_PIXELFORMAT_RGBA32);
    if (!surf)
        return;
    SDL_SetWindowIcon(win, surf);
    SDL_FreeSurface(surf);
#endif
}

void SyncCursorForGame()
{
    // NULL / 0 = cursor software (SCursor). Qualquer outro handle = cursor do SO.
    if (!g_cursor)
        HideSystemCursor();
    else
        ShowSystemCursor();
}

UINT MapSdlKey(SDL_Keycode k)
{
    switch (k) {
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return VK_RETURN;
    case SDLK_TAB: return VK_TAB;
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_UP: return VK_UP;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_HOME: return VK_HOME;
    case SDLK_END: return VK_END;
    case SDLK_INSERT: return VK_INSERT;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return VK_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL: return VK_CONTROL;
    case SDLK_LALT:
    case SDLK_RALT: return VK_MENU;
    case SDLK_F10: return VK_F10;
    case SDLK_F11: return VK_F11;
    case SDLK_F12: return VK_F12;
    default:
        break;
    }
#ifdef WYD_USE_SDL3
    if (k >= SDLK_A && k <= SDLK_Z)
        return static_cast<UINT>('A' + (k - SDLK_A));
#else
    if (k >= SDLK_a && k <= SDLK_z)
        return static_cast<UINT>('A' + (k - SDLK_a));
#endif
    if (k >= SDLK_0 && k <= SDLK_9)
        return static_cast<UINT>('0' + (k - SDLK_0));
    if (k >= SDLK_KP_0 && k <= SDLK_KP_9)
        return static_cast<UINT>(VK_NUMPAD0 + (k - SDLK_KP_0));
    if (k >= SDLK_F1 && k <= SDLK_F9)
        return static_cast<UINT>(VK_F1 + (k - SDLK_F1));
    return static_cast<UINT>(k & 0xFF);
}

void PushMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MSG m {};
    m.hwnd = hwnd;
    m.message = msg;
    m.wParam = wp;
    m.lParam = lp;
    m.time = GetTickCount();
    std::lock_guard<std::mutex> lock(g_mtx);
    g_queue.push_back(m);
}

// EventTranslator_linux.cpp — deltas quando PumpSdl consome o SDL antes do ReadInput.
extern "C" void WYD_Linux_AccumulateMouseDelta(int dx, int dy, int wheel);

void PumpSdl(HWND focus)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
#ifdef WYD_USE_SDL3
        case SDL_EVENT_QUIT:
            PushMsg(focus, WM_CLOSE, 0, 0);
            break;

        // SDL3: eventos de janela chegam como tipos de topo (sem wrapper SDL_WINDOWEVENT).
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            PushMsg(focus, WM_CLOSE, 0, 0);
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            PushMsg(focus, WM_ACTIVATEAPP, TRUE, 0);
            SDL_StartTextInput(PrimaryWindow());
            SoftCaptureMouse(true);
            SyncCursorForGame();
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            PushMsg(focus, WM_ACTIVATEAPP, FALSE, 0);
            SoftCaptureMouse(false);
            ShowSystemCursor();
            break;

        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            SoftCaptureMouse(true);
            SyncCursorForGame();
            break;

        case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
            // Mantém captura se o botão estiver pressionado (arrastar fora da janela);
            // sem botão, devolve o cursor do compositor ao desktop.
            float mx = 0.f, my = 0.f;
            const SDL_MouseButtonFlags bs = SDL_GetMouseState(&mx, &my);
            if ((bs & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK)) == 0)
                ShowSystemCursor();
            break;
        }

        case SDL_EVENT_KEY_DOWN:
            PushMsg(focus, WM_KEYDOWN, MapSdlKey(ev.key.key), 0);
            break;

        case SDL_EVENT_KEY_UP:
            PushMsg(focus, WM_KEYUP, MapSdlKey(ev.key.key), 0);
            break;

        case SDL_EVENT_TEXT_INPUT:
            // SDL3: text é NUL-terminated (sem text_length).
            for (const char* p = ev.text.text; p && *p; ++p)
                PushMsg(focus, WM_CHAR, static_cast<WPARAM>(static_cast<unsigned char>(*p)), 0);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            WYD_Linux_AccumulateMouseDelta(
                static_cast<int>(ev.motion.xrel), static_cast<int>(ev.motion.yrel), 0);
            SyncCursorForGame();
            PushMsg(focus, WM_MOUSEMOVE, 0,
                (static_cast<LPARAM>(static_cast<int>(ev.motion.y) & 0xFFFF) << 16) | (static_cast<int>(ev.motion.x) & 0xFFFF));
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            SoftCaptureMouse(true);
            SyncCursorForGame();
            if (ev.button.button == SDL_BUTTON_LEFT)
                PushMsg(focus, WM_LBUTTONDOWN, MK_LBUTTON,
                    (static_cast<LPARAM>(static_cast<int>(ev.button.y) & 0xFFFF) << 16) | (static_cast<int>(ev.button.x) & 0xFFFF));
            else if (ev.button.button == SDL_BUTTON_RIGHT)
                PushMsg(focus, WM_RBUTTONDOWN, MK_RBUTTON,
                    (static_cast<LPARAM>(static_cast<int>(ev.button.y) & 0xFFFF) << 16) | (static_cast<int>(ev.button.x) & 0xFFFF));
            else if (ev.button.button == SDL_BUTTON_MIDDLE)
                PushMsg(focus, WM_MBUTTONDOWN, MK_MBUTTON,
                    (static_cast<LPARAM>(static_cast<int>(ev.button.y) & 0xFFFF) << 16) | (static_cast<int>(ev.button.x) & 0xFFFF));
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.button == SDL_BUTTON_LEFT)
                PushMsg(focus, WM_LBUTTONUP, 0,
                    (static_cast<LPARAM>(static_cast<int>(ev.button.y) & 0xFFFF) << 16) | (static_cast<int>(ev.button.x) & 0xFFFF));
            else if (ev.button.button == SDL_BUTTON_RIGHT)
                PushMsg(focus, WM_RBUTTONUP, 0,
                    (static_cast<LPARAM>(static_cast<int>(ev.button.y) & 0xFFFF) << 16) | (static_cast<int>(ev.button.x) & 0xFFFF));
            else if (ev.button.button == SDL_BUTTON_MIDDLE)
                PushMsg(focus, WM_MBUTTONUP, 0,
                    (static_cast<LPARAM>(static_cast<int>(ev.button.y) & 0xFFFF) << 16) | (static_cast<int>(ev.button.x) & 0xFFFF));
            break;

        case SDL_EVENT_MOUSE_WHEEL: {
            // SDL3: wheel.y é float; mesmo sinal do SDL2 (positivo = rolar para cima).
            const int delta = static_cast<int>(ev.wheel.y * 120.0f);
            WYD_Linux_AccumulateMouseDelta(0, 0, delta);
            PushMsg(focus, WM_MOUSEWHEEL,
                static_cast<WPARAM>((delta & 0xFFFF) << 16), 0);
            break;
        }
#else
        case SDL_QUIT:
            PushMsg(focus, WM_CLOSE, 0, 0);
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
                PushMsg(focus, WM_CLOSE, 0, 0);
            else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                PushMsg(focus, WM_ACTIVATEAPP, TRUE, 0);
                SDL_StartTextInput();
                SoftCaptureMouse(true);
                SyncCursorForGame();
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                PushMsg(focus, WM_ACTIVATEAPP, FALSE, 0);
                SoftCaptureMouse(false);
                ShowSystemCursor();
            } else if (ev.window.event == SDL_WINDOWEVENT_ENTER) {
                SoftCaptureMouse(true);
                SyncCursorForGame();
            } else if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
                // Mantém captura se o botão estiver pressionado (arrastar fora da janela);
                // sem botão, devolve o cursor do compositor ao desktop.
                Uint32 bs = SDL_GetMouseState(nullptr, nullptr);
                if ((bs & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK)) == 0)
                    ShowSystemCursor();
            }
            break;

        case SDL_KEYDOWN:
            PushMsg(focus, WM_KEYDOWN, MapSdlKey(ev.key.keysym.sym), 0);
            break;

        case SDL_KEYUP:
            PushMsg(focus, WM_KEYUP, MapSdlKey(ev.key.keysym.sym), 0);
            break;

        case SDL_TEXTINPUT:
            // SDL2 entrega texto UTF-8 NUL-terminated; não existe text_length.
            for (const char* p = ev.text.text; p && *p; ++p)
                PushMsg(focus, WM_CHAR, static_cast<WPARAM>(static_cast<unsigned char>(*p)), 0);
            break;

        case SDL_MOUSEMOTION:
            WYD_Linux_AccumulateMouseDelta(ev.motion.xrel, ev.motion.yrel, 0);
            SyncCursorForGame();
            PushMsg(focus, WM_MOUSEMOVE, 0,
                (static_cast<LPARAM>(ev.motion.y & 0xFFFF) << 16) | (ev.motion.x & 0xFFFF));
            break;

        case SDL_MOUSEBUTTONDOWN:
            SoftCaptureMouse(true);
            SyncCursorForGame();
            if (ev.button.button == SDL_BUTTON_LEFT)
                PushMsg(focus, WM_LBUTTONDOWN, MK_LBUTTON,
                    (static_cast<LPARAM>(ev.button.y & 0xFFFF) << 16) | (ev.button.x & 0xFFFF));
            else if (ev.button.button == SDL_BUTTON_RIGHT)
                PushMsg(focus, WM_RBUTTONDOWN, MK_RBUTTON,
                    (static_cast<LPARAM>(ev.button.y & 0xFFFF) << 16) | (ev.button.x & 0xFFFF));
            else if (ev.button.button == SDL_BUTTON_MIDDLE)
                PushMsg(focus, WM_MBUTTONDOWN, MK_MBUTTON,
                    (static_cast<LPARAM>(ev.button.y & 0xFFFF) << 16) | (ev.button.x & 0xFFFF));
            break;

        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT)
                PushMsg(focus, WM_LBUTTONUP, 0,
                    (static_cast<LPARAM>(ev.button.y & 0xFFFF) << 16) | (ev.button.x & 0xFFFF));
            else if (ev.button.button == SDL_BUTTON_RIGHT)
                PushMsg(focus, WM_RBUTTONUP, 0,
                    (static_cast<LPARAM>(ev.button.y & 0xFFFF) << 16) | (ev.button.x & 0xFFFF));
            break;

        case SDL_MOUSEWHEEL:
            WYD_Linux_AccumulateMouseDelta(0, 0, ev.wheel.y * 120);
            PushMsg(focus, WM_MOUSEWHEEL,
                static_cast<WPARAM>((static_cast<int>(ev.wheel.y) * 120) << 16), 0);
            break;
#endif

        default:
            break;
        }
    }
}

} // namespace

// Escopo global: D3DDevice.cpp chama via win32_bulk.h.
BOOL GetWindowRect(HWND hWnd, LPRECT r)
{
    if (!r)
        return FALSE;
    if (!hWnd) {
        r->left = r->top = 0;
        r->right = 1280;
        r->bottom = 720;
        return TRUE;
    }
    auto* win = reinterpret_cast<SDL_Window*>(hWnd);
    int x = 0, y = 0, w = 1280, h = 720;
    SDL_GetWindowPosition(win, &x, &y);
    SDL_GetWindowSize(win, &w, &h);
    r->left = x;
    r->top = y;
    r->right = x + w;
    r->bottom = y + h;
    return TRUE;
}

BOOL GetClientRect(HWND hWnd, LPRECT r)
{
    if (!r)
        return FALSE;
    int w = 1280, h = 720;
    if (hWnd)
        SDL_GetWindowSize(reinterpret_cast<SDL_Window*>(hWnd), &w, &h);
    r->left = 0;
    r->top = 0;
    r->right = w;
    r->bottom = h;
    return TRUE;
}

// Tamanho real da janela (NewApp: resolução virtual legível no celular).
extern "C" int WYD_Linux_GetWindowSize(unsigned int* outW, unsigned int* outH)
{
    SDL_Window* win = PrimaryWindow();
    if (!win)
        return 0;
    int w = 0, h = 0;
#ifdef WYD_USE_SDL3
    SDL_GetWindowSizeInPixels(win, &w, &h);
#else
    SDL_GetWindowSize(win, &w, &h);
#endif
    if (w <= 0 || h <= 0)
        return 0;
    if (outW)
        *outW = static_cast<unsigned int>(w);
    if (outH)
        *outH = static_cast<unsigned int>(h);
    return 1;
}

// Backbuffer virtual (estirado pelo swapchain); DXVK Native usa o tamanho da janela.
extern "C" void WYD_Linux_SetBackbuffer(int, int)
{
}

ATOM RegisterClassA(const WNDCLASS* wc)
{
    if (!wc || !wc->lpszClassName)
        return 0;
    std::lock_guard<std::mutex> lock(g_mtx);
    ClassInfo ci;
    ci.proc = wc->lpfnWndProc;
    ci.name = wc->lpszClassName;
    g_classes[ci.name] = ci;
    return 1;
}

HWND CreateWindowExA(DWORD, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle,
    int, int, int nWidth, int nHeight, HWND, HMENU, HINSTANCE, LPVOID)
{
#ifdef WYD_USE_SDL3
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) // SDL3: bool
        return nullptr;
#else
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        return nullptr;
#endif

#ifdef WYD_USE_SDL3
    Uint32 flags = SDL_WINDOW_VULKAN;
    if (dwStyle & WS_POPUP)
        flags |= SDL_WINDOW_FULLSCREEN;
#else
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN;
    if (dwStyle & WS_POPUP)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif

    // DXVK Native exige SDL_WINDOW_VULKAN para vkCreate*Surface.
    if (!std::getenv("DXVK_WSI_DRIVER") || !std::getenv("DXVK_WSI_DRIVER")[0])
#ifdef WYD_USE_SDL3
        setenv("DXVK_WSI_DRIVER", "SDL3", 1);
#else
        setenv("DXVK_WSI_DRIVER", "SDL2", 1);
#endif

#ifdef WYD_USE_SDL3
    SDL_Window* win = SDL_CreateWindow(
        lpWindowName ? lpWindowName : "WYD",
        nWidth > 0 ? nWidth : 1280,
        nHeight > 0 ? nHeight : 720,
        flags);
#else
    SDL_Window* win = SDL_CreateWindow(
        lpWindowName ? lpWindowName : "WYD",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        nWidth > 0 ? nWidth : 1280,
        nHeight > 0 ? nHeight : 720,
        flags);
#endif
    if (!win) {
        std::fprintf(stderr, "[WYDLINUX] SDL_CreateWindow falhou: %s\n", SDL_GetError());
        return nullptr;
    }
    ApplyWydExeWindowIcon(win);

    HWND hwnd = reinterpret_cast<HWND>(win);
    WindowInfo info;
    info.style = dwStyle;
    info.title = lpWindowName ? lpWindowName : "";
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (lpClassName && g_classes.count(lpClassName))
            info.proc = g_classes[lpClassName].proc;
        g_windows[hwnd] = info;
    }

    PushMsg(hwnd, WM_CREATE, 0, 0);
    PushMsg(hwnd, WM_SHOWWINDOW, TRUE, 0);
    SoftCaptureMouse(false);
    EnsureBlankCursor();
    // Digitação (login/chat): TEXTINPUT → WM_CHAR; Return/Tab/Back via TranslateMessage.

#ifdef WYD_USE_SDL3
    SDL_StartTextInput(win);
    {
        SDL_Rect r { 0, 0, nWidth > 0 ? nWidth : 1280, nHeight > 0 ? nHeight : 720 };
        SDL_SetTextInputArea(win, &r, 0);
    }
#else
    SDL_StartTextInput();
    {
        SDL_Rect r { 0, 0, nWidth > 0 ? nWidth : 1280, nHeight > 0 ? nHeight : 720 };
        SDL_SetTextInputRect(&r);
    }
#endif
    return hwnd;
}

BOOL DestroyWindow(HWND hWnd)
{
    if (!hWnd)
        return FALSE;
    PushMsg(hWnd, WM_DESTROY, 0, 0);
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_windows.erase(hWnd);
    }
    SDL_DestroyWindow(reinterpret_cast<SDL_Window*>(hWnd));
    return TRUE;
}

BOOL ShowWindow(HWND hWnd, int)
{
    if (!hWnd)
        return FALSE;
    SDL_ShowWindow(reinterpret_cast<SDL_Window*>(hWnd));
    return TRUE;
}

BOOL UpdateWindow(HWND) { return TRUE; }

BOOL AdjustWindowRect(LPRECT lpRect, DWORD, BOOL)
{
    (void)lpRect;
    return TRUE;
}

BOOL SetRect(LPRECT r, int l, int t, int ri, int b)
{
    if (!r)
        return FALSE;
    r->left = l;
    r->top = t;
    r->right = ri;
    r->bottom = b;
    return TRUE;
}

BOOL PeekMessageA(MSG* lpMsg, HWND hWnd, UINT, UINT, UINT wRemoveMsg)
{
    if (!lpMsg)
        return FALSE;
    HWND focus = hWnd;
    if (!focus && !g_windows.empty())
        focus = g_windows.begin()->first;
    PumpSdl(focus);

    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_queue.empty())
        return FALSE;
    *lpMsg = g_queue.front();
    if (wRemoveMsg & PM_REMOVE)
        g_queue.pop_front();
    return TRUE;
}

BOOL GetMessageA(MSG* lpMsg, HWND hWnd, UINT min, UINT max)
{
    for (;;) {
        if (PeekMessageA(lpMsg, hWnd, min, max, PM_REMOVE)) {
            if (lpMsg->message == WM_QUIT)
                return 0;
            return 1;
        }
        SDL_Delay(1);
        if (g_quitPosted)
            return 0;
    }
}

BOOL TranslateMessage(const MSG* lpMsg)
{
    // Equivalente mínimo ao TranslateMessage do Win32: gera WM_CHAR para teclas
    // que o SDL não manda em TEXTINPUT (Enter/Tab/Back/Esc). Letras vêm do TEXTINPUT.
    if (!lpMsg || lpMsg->message != WM_KEYDOWN)
        return TRUE;
    WPARAM ch = 0;
    switch (lpMsg->wParam) {
    case VK_RETURN: ch = VK_RETURN; break;
    case VK_TAB: ch = VK_TAB; break;
    case VK_ESCAPE: ch = VK_ESCAPE; break;
    case VK_BACK: ch = VK_BACK; break;
    default: break;
    }
    if (ch)
        PushMsg(lpMsg->hwnd, WM_CHAR, ch, 1);
    return TRUE;
}

LRESULT DispatchMessageA(const MSG* lpMsg)
{
    if (!lpMsg)
        return 0;
    WNDPROC proc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        auto it = g_windows.find(lpMsg->hwnd);
        if (it != g_windows.end())
            proc = it->second.proc;
    }
    if (proc)
        return proc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    return DefWindowProcA(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
}

void PostQuitMessage(int nExitCode)
{
    g_quitPosted = true;
    PushMsg(nullptr, WM_QUIT, static_cast<WPARAM>(nExitCode), 0);
}

BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    PushMsg(hWnd, Msg, wParam, lParam);
    return TRUE;
}

LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM, LPARAM)
{
    if (Msg == WM_CLOSE) {
        DestroyWindow(hWnd);
        PostQuitMessage(0);
    }
    return 0;
}

HICON LoadIconA(HINSTANCE, LPCSTR) { return reinterpret_cast<HICON>(1); }
HCURSOR LoadCursorA(HINSTANCE, LPCSTR) { return reinterpret_cast<HCURSOR>(1); }
HCURSOR SetCursor(HCURSOR c)
{
    HCURSOR prev = g_cursor;
    g_cursor = c;
    SyncCursorForGame();
    return prev;
}

// Não confina o ponteiro. Se o jogo pede ClipCursor(rect), só ativa captura soft.
BOOL ClipCursor(const RECT* lpRect)
{
    SoftCaptureMouse(lpRect != nullptr);
    return TRUE;
}

int ShowCursor(BOOL bShow)
{
    if (bShow)
        ++g_cursorShowCount;
    else
        --g_cursorShowCount;

    if (g_cursorShowCount < 0) {
        HideSystemCursor();
    } else {
        // Com cursor software (SetCursor NULL), manter o do SO oculto mesmo se
        // alguém incrementar o contador.
        if (!g_cursor)
            HideSystemCursor();
        else
            ShowSystemCursor();
    }
    return g_cursorShowCount;
}

BOOL GetCursorPos(LPPOINT pt)
{
    if (!pt)
        return FALSE;
    // Win32: coordenadas de tela. ScreenToClient converte para cliente.
#ifdef WYD_USE_SDL3
    float fx = 0.f, fy = 0.f;
    SDL_GetMouseState(&fx, &fy);
    int x = static_cast<int>(fx);
    int y = static_cast<int>(fy);
#else
    int x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
#endif
    if (!g_windows.empty()) {
        SDL_Window* win = reinterpret_cast<SDL_Window*>(g_windows.begin()->first);
        int wx = 0, wy = 0;
#ifdef WYD_USE_SDL3
        if (!SDL_GetWindowPosition(win, &wx, &wy))
            wx = wy = 0;
#else
        SDL_GetWindowPosition(win, &wx, &wy);
#endif
        pt->x = wx + x;
        pt->y = wy + y;
    } else {
        pt->x = x;
        pt->y = y;
    }
    return TRUE;
}

BOOL ScreenToClient(HWND hWnd, LPPOINT pt)
{
    if (!pt)
        return FALSE;
    SDL_Window* win = hWnd ? reinterpret_cast<SDL_Window*>(hWnd)
        : (!g_windows.empty() ? reinterpret_cast<SDL_Window*>(g_windows.begin()->first) : nullptr);
    if (!win)
        return FALSE;
    int wx = 0, wy = 0;
#ifdef WYD_USE_SDL3
    if (!SDL_GetWindowPosition(win, &wx, &wy))
        wx = wy = 0;
#else
    SDL_GetWindowPosition(win, &wx, &wy);
#endif
    pt->x -= wx;
    pt->y -= wy;
    return TRUE;
}

BOOL ClientToScreen(HWND hWnd, LPPOINT pt)
{
    if (!pt)
        return FALSE;
    SDL_Window* win = hWnd ? reinterpret_cast<SDL_Window*>(hWnd)
        : (!g_windows.empty() ? reinterpret_cast<SDL_Window*>(g_windows.begin()->first) : nullptr);
    if (!win)
        return FALSE;
    int wx = 0, wy = 0;
#ifdef WYD_USE_SDL3
    if (!SDL_GetWindowPosition(win, &wx, &wy))
        wx = wy = 0;
#else
    SDL_GetWindowPosition(win, &wx, &wy);
#endif
    pt->x += wx;
    pt->y += wy;
    return TRUE;
}

BOOL SetCursorPos(int x, int y)
{
    if (!g_windows.empty()) {
        SDL_Window* win = reinterpret_cast<SDL_Window*>(g_windows.begin()->first);
        int wx = 0, wy = 0;
        SDL_GetWindowPosition(win, &wx, &wy);
        SDL_WarpMouseInWindow(win, x - wx, y - wy);
    } else {
        SDL_WarpMouseGlobal(x, y);
    }
    return TRUE;
}

HGDIOBJ GetStockObject(int) { return reinterpret_cast<HGDIOBJ>(1); }

SHORT GetKeyState(int nVirtKey)
{
#ifdef WYD_USE_SDL3
    const bool* state = static_cast<const bool*>(SDL_GetKeyboardState(nullptr));
#else
    const Uint8* state = SDL_GetKeyboardState(nullptr);
#endif
    if (!state)
        return 0;
    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
    switch (nVirtKey) {
    case VK_SHIFT:
        if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT])
            return static_cast<SHORT>(0x8000);
        return 0;
    case VK_CONTROL:
        if (state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_RCTRL])
            return static_cast<SHORT>(0x8000);
        return 0;
    case VK_MENU:
        if (state[SDL_SCANCODE_LALT] || state[SDL_SCANCODE_RALT])
            return static_cast<SHORT>(0x8000);
        return 0;
    default: break;
    }
    if (sc != SDL_SCANCODE_UNKNOWN && state[sc])
        return static_cast<SHORT>(0x8000);
    return 0;
}

HACCEL LoadAcceleratorsA(HINSTANCE, LPCSTR) { return reinterpret_cast<HACCEL>(1); }
int TranslateAcceleratorA(HWND, HACCEL, MSG*) { return 0; }
BOOL DestroyAcceleratorTable(HACCEL) { return TRUE; }
HBITMAP LoadBitmapA(HINSTANCE, LPCSTR) { return reinterpret_cast<HBITMAP>(1); }
BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) { return TRUE; }
HWND GetFocus()
{
    if (!g_windows.empty())
        return g_windows.begin()->first;
    return nullptr;
}
BOOL SetFocus(HWND hWnd)
{
    if (hWnd)
        SDL_RaiseWindow(reinterpret_cast<SDL_Window*>(hWnd));
    return TRUE;
}
