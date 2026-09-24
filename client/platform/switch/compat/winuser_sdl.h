#pragma once
// Janela Win32 ↔ SDL — HWND é SDL_Window*.
#include "win32_extras.h"
#include "wingdi.h"
#include "sdl_backend.h"

#ifndef PM_REMOVE
#define PM_REMOVE 1
#define PM_NOREMOVE 0
#endif

#ifndef WS_POPUP
#define WS_POPUP 0x80000000L
#endif
#ifndef WS_VISIBLE
#define WS_VISIBLE 0x10000000L
#endif
#ifndef CS_DBLCLKS
#define CS_DBLCLKS 8
#endif
#ifndef SW_HIDE
#define SW_HIDE 0
#define SW_SHOW 5
#define SW_SHOWDEFAULT 10
#define SW_SHOWNORMAL 1
#define SW_HIDE 0
#endif

#ifndef WM_CREATE
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCACTIVATE 0x0086
#define WM_GETICON 0x007F
#define WM_SYSCOMMAND 0x0112
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MOUSEWHEEL 0x020A
#define WM_RBUTTONDBLCLK 0x0206
#define WM_SIZING 0x0214
#define WM_MOVING 0x0216
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_COMMAND 0x0111
#define WM_DRAWITEM 0x002B
#define WM_INPUTLANGCHANGE 0x0051
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_IME_CONTROL 0x0283
#define WM_IME_COMPOSITIONFULL 0x0284
#define WM_IME_SELECT 0x0285
#define WM_IME_CHAR 0x0286
#define WM_IME_REQUEST 0x0288
#define WM_IME_KEYDOWN 0x0290
#define WM_IME_KEYUP 0x0291
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_USER 0x0400
#define WM_APP 0x8000
#define SC_KEYMENU 0xF100
#define HTCLIENT 1
#endif

#ifndef VK_F10
#define VK_F10 0x79
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_SNAPSHOT 0x2C
#define VK_OEM_PLUS 0xBB
#define VK_OEM_MINUS 0xBD
#define VK_OEM_4 0xDB
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_INSERT 0x2D
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#endif

#ifndef MK_CONTROL
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#endif

#ifndef LOWORD
#define LOWORD(l) static_cast<WORD>(static_cast<DWORD_PTR>(l) & 0xffff)
#define HIWORD(l) static_cast<WORD>((static_cast<DWORD_PTR>(l) >> 16) & 0xffff)
#endif

#ifndef MAKEINTRESOURCE
#define MAKEINTRESOURCE(i) reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(LOWORD(i)))
#endif

#ifndef IDC_ARROW
#define IDC_ARROW MAKEINTRESOURCE(32512)
#endif

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef _In_
#define _In_
#define _In_opt_
#endif

using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
using HACCEL = HANDLE;
using ATOM = WORD;

struct WNDCLASS {
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HICON hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName;
	LPCSTR lpszClassName;
};

struct MSG {
	HWND hwnd;
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
	DWORD time;
	POINT pt;
};

ATOM RegisterClassA(const WNDCLASS* wc);
#define RegisterClass RegisterClassA
HWND CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle,
	int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
#define CreateWindowEx CreateWindowExA
BOOL DestroyWindow(HWND hWnd);
#ifndef CloseWindow
inline BOOL CloseWindow(HWND hWnd) { return DestroyWindow(hWnd); }
#endif
BOOL ShowWindow(HWND hWnd, int nCmdShow);
BOOL UpdateWindow(HWND hWnd);
BOOL AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu);
BOOL SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom);
BOOL GetClientRect(HWND hWnd, LPRECT r);
BOOL GetWindowRect(HWND hWnd, LPRECT r);

BOOL PeekMessageA(MSG* lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
#define PeekMessage PeekMessageA
BOOL GetMessageA(MSG* lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
#define GetMessage GetMessageA
BOOL TranslateMessage(const MSG* lpMsg);
LRESULT DispatchMessageA(const MSG* lpMsg);
#define DispatchMessage DispatchMessageA
void PostQuitMessage(int nExitCode);
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define DefWindowProc DefWindowProcA
#define PostMessage PostMessageA

HICON LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
#define LoadIcon LoadIconA
HCURSOR LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
#define LoadCursor LoadCursorA
HCURSOR SetCursor(HCURSOR hCursor);
BOOL ClipCursor(const RECT* lpRect);
int ShowCursor(BOOL bShow);
BOOL GetCursorPos(LPPOINT lpPoint);
BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint);
BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint);
BOOL SetCursorPos(int X, int Y);
HBITMAP LoadBitmapA(HINSTANCE hInstance, LPCSTR lpBitmapName);
#define LoadBitmap LoadBitmapA
BOOL StretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest, HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, DWORD rop);
HWND GetFocus();
BOOL SetFocus(HWND hWnd);
BOOL DestroyAcceleratorTable(HACCEL hAccel);
HGDIOBJ GetStockObject(int i);
SHORT GetKeyState(int nVirtKey);
HACCEL LoadAcceleratorsA(HINSTANCE hInstance, LPCSTR lpTableName);
#define LoadAccelerators LoadAcceleratorsA
int TranslateAcceleratorA(HWND hWnd, HACCEL hAccTable, MSG* lpMsg);
#define TranslateAccelerator TranslateAcceleratorA

using HKL = HANDLE;
struct DRAWITEMSTRUCT {
	UINT CtlType;
	UINT CtlID;
	UINT itemID;
	UINT itemAction;
	UINT itemState;
	HWND hwndItem;
	HDC hDC;
	RECT rcItem;
	ULONG_PTR itemData;
};
using LPDRAWITEMSTRUCT = DRAWITEMSTRUCT*;

inline DWORD ImmGetDescriptionA(HKL, LPSTR buf, DWORD n)
{
	if (buf && n)
		buf[0] = 0;
	return 0;
}
#define ImmGetDescription ImmGetDescriptionA
inline HKL GetKeyboardLayout(DWORD) { return reinterpret_cast<HKL>(1); }

#ifndef IMN_CLOSESTATUSWINDOW
#define IMN_CLOSESTATUSWINDOW 0x0001
#define IMN_OPENSTATUSWINDOW 0x0002
#define IMN_CHANGECANDIDATE 0x0003
#define IMN_CLOSECANDIDATE 0x0004
#define IMN_OPENCANDIDATE 0x0005
#define IMN_SETCONVERSIONMODE 0x0006
#define IMN_SETSENTENCEMODE 0x0007
#define IMN_SETOPENSTATUS 0x0008
#define IMN_SETCANDIDATEPOS 0x0009
#define IMN_SETCOMPOSITIONFONT 0x000A
#define IMN_SETCOMPOSITIONWINDOW 0x000B
#define IMN_SETSTATUSWINDOWPOS 0x000C
#define IMN_GUIDELINE 0x000D
#define IMN_PRIVATE 0x000E
#endif

#ifndef _LARGE_INTEGER
using _LARGE_INTEGER = LARGE_INTEGER;
#endif

inline errno_t freopen_s(FILE** pFile, const char* path, const char* mode, FILE* stream)
{
	FILE* f = freopen(path, mode, stream);
	if (pFile)
		*pFile = f;
	return f ? 0 : 1;
}

// Não usar #define min/max — quebra std::min/std::max. Código Win32
// que dependia das macros deve usar (std::min)/(std::max) ou estes helpers.
template <typename T>
constexpr const T& wyd_min(const T& a, const T& b) { return (a < b) ? a : b; }
template <typename T>
constexpr const T& wyd_max(const T& a, const T& b) { return (a > b) ? a : b; }
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
