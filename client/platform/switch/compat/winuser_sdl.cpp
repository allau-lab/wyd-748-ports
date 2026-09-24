// HWND = SDL_Window* — fila de mensagens Win32 alimentada por SDL_PollEvent.
#include "winuser_sdl.h"
#include "sdl_backend.h"
#if defined(WYD_SDL3)
#include <SDL3/SDL_vulkan.h>
#endif

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

struct ClassInfo {
	WNDPROC proc = nullptr;
	std::string name;
};

struct WindowInfo {
	WNDPROC proc = nullptr;
	std::string title;
	DWORD style = 0;
	bool alive = true;
};

extern "C" int WYD_Linux_TouchHudFinger(int id, float nx, float ny, int winW, int winH,
	int isDown, int isUp, int isMove);
extern "C" int WYD_Linux_TouchUiEnabled();
extern "C" int WYD_TouchV2_Finger(int id, float nx, float ny, int winW, int winH,
	int isDown, int isUp, int isMove);
extern "C" int WYD_TouchV2_Enabled();
extern "C" int WYD_TouchV2_Pointer(int id, int x, int y, int isDown, int isUp, int isMove);
extern "C" int WYD_TouchV2_HasCapture();
extern "C" int WYD_TouchV2_WantMouseSync();
extern "C" int WYD_TouchV2_PointerWindow(int id, int wx, int wy, int winW, int winH,
	int isDown, int isUp, int isMove);

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
	if (g_blankCursor)
		SDL_SetCursor(g_blankCursor);
	Wyd_SdlShowCursor(false);
}

void ShowSystemCursor()
{
	if (SDL_Cursor* arrow = SDL_GetDefaultCursor())
		SDL_SetCursor(arrow);
	Wyd_SdlShowCursor(true);
}

// Captura eventos do mouse sem prender/confinar o ponteiro (sem grab/relative).
void SoftCaptureMouse(bool enable)
{
	Wyd_SdlSetRelativeMouse(false);
	if (SDL_Window* win = PrimaryWindow())
		Wyd_SdlSetWindowGrab(win, false);
#if defined(WYD_SDL3)
	SDL_CaptureMouse(enable);
#else
	SDL_CaptureMouse(enable ? SDL_TRUE : SDL_FALSE);
#endif
}

// Ícone idêntico ao WYD.exe (RT_GROUP_ICON 101, 32x32).
#include "wyd_exe_icon.inc"
void ApplyWydExeWindowIcon(SDL_Window* win)
{
	if (!win)
		return;
	SDL_Surface* surf = Wyd_SdlCreateRgbaSurface(
		const_cast<unsigned char*>(kWydExeIconRGBA),
		kWydExeIconW, kWydExeIconH, kWydExeIconW * 4);
	if (!surf)
		return;
	SDL_SetWindowIcon(win, surf);
	Wyd_SdlDestroySurface(surf);
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
	if (k >= SDLK_a && k <= SDLK_z)
		return static_cast<UINT>('A' + (k - SDLK_a));
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

// ---------------------------------------------------------------------------
// SW-60/SW-61 — injeção de input do backend NATIVO do Switch (libnx).
//
// Por que existe: no HOS nada alimentava o jogo. O cliente não inicializa o
// subsistema de joystick do SDL (nenhuma referência a SDL_GameController no
// port) e o touch do console, quando chegava, ia para o HUD do port de celular
// (WYD_TouchV2_Enabled(), desligado fora do campo). Resultado: o jogo abria e
// nenhum input funcionava.
//
// A leitura de pad/touch fica no bootstrap (switch/source/main_switch.cpp,
// WYD_Switch_PollInput, chamada no topo deste pump a cada frame) porque a API
// nativa é Switch-only; a injeção entra AQUI, na mesma fila de mensagens do
// mouse/teclado. Assim todo o caminho já existente (MsgProc → MapCursor →
// cena/UI) é reaproveitado e nada no jogo precisa saber que existe controle.
// ---------------------------------------------------------------------------
static HWND g_pumpFocus = nullptr;	// hwnd do último PumpSdl (destino da injeção)
static int g_swCursorX = -1;		// cursor virtual em pixels de janela
static int g_swCursorY = -1;

static HWND PumpFocus()
{
	return g_pumpFocus ? g_pumpFocus : PrimaryWindow();
}

static bool PumpWindowSize(int* ww, int* wh)
{
	HWND h = PumpFocus();
	if (!h)
		return false;
	SDL_Window* win = reinterpret_cast<SDL_Window*>(h);
	int w = 0, hh = 0;
	SDL_GetWindowSize(win, &w, &hh);
	if (w <= 0 || hh <= 0)
		return false;
	*ww = w;
	*wh = hh;
	return true;
}

extern "C" void WYD_Switch_InjectMouseMove(float nx, float ny)
{
	int ww = 0, wh = 0;
	if (!PumpWindowSize(&ww, &wh))
		return;
	if (nx < 0.0f) nx = 0.0f;
	if (nx > 1.0f) nx = 1.0f;
	if (ny < 0.0f) ny = 0.0f;
	if (ny > 1.0f) ny = 1.0f;
	const int mx = static_cast<int>(nx * static_cast<float>(ww - 1));
	const int my = static_cast<int>(ny * static_cast<float>(wh - 1));
	g_swCursorX = mx;
	g_swCursorY = my;
	PushMsg(PumpFocus(), WM_MOUSEMOVE, 0,
		(static_cast<LPARAM>(my & 0xFFFF) << 16) | (mx & 0xFFFF));
#if defined(__SWITCH__)
	// SDL2 no Switch: mantém SDL_GetMouseState/GetCursorPos coerentes com o
	// cursor que injetamos (o jogo lê a posição por esses caminhos no pick).
	if (HWND h = PumpFocus())
		SDL_WarpMouseInWindow(reinterpret_cast<SDL_Window*>(h), mx, my);
#endif
}

extern "C" void WYD_Switch_InjectMouseButton(int right, int down)
{
	int ww = 0, wh = 0;
	if (!PumpWindowSize(&ww, &wh))
		return;
	if (g_swCursorX < 0) { g_swCursorX = ww / 2; g_swCursorY = wh / 2; }
	const LPARAM lp = (static_cast<LPARAM>(g_swCursorY & 0xFFFF) << 16) | (g_swCursorX & 0xFFFF);
	HWND h = PumpFocus();
	if (!h)
		return;
	if (!right)
		PushMsg(h, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0, lp);
	else
		PushMsg(h, down ? WM_RBUTTONDOWN : WM_RBUTTONUP, down ? MK_RBUTTON : 0, lp);
}

extern "C" void WYD_Switch_InjectKey(int vk, int down)
{
	if (HWND h = PumpFocus())
		PushMsg(h, down ? WM_KEYDOWN : WM_KEYUP, static_cast<WPARAM>(vk), 0);
}

extern "C" void WYD_Switch_InjectChar(int ch)
{
	// SW-62: rastro no stderr (limitado) para separar "o caracter não chegou ao
	// jogo" de "chegou e não foi desenhado" — os dois apareceram como "não
	// aparece nada" na tela de seleção.
	static int nLogged = 0;
	if (nLogged++ < 24)
		std::fprintf(stderr, "[INPUT] char injetado '%c' (%d)\n",
			(ch >= 32 && ch < 127) ? ch : '?', ch);
	if (HWND h = PumpFocus())
		PushMsg(h, WM_CHAR, static_cast<WPARAM>(static_cast<unsigned char>(ch)), 1);
}

extern "C" void WYD_Linux_AccumulateMouseDelta(int dx, int dy, int wheel); // definida abaixo

extern "C" void WYD_Switch_InjectWheel(int delta)
{
	if (HWND h = PumpFocus())
	{
		WYD_Linux_AccumulateMouseDelta(0, 0, delta);
		PushMsg(h, WM_MOUSEWHEEL, static_cast<WPARAM>(delta << 16), 0);
	}
}

// EventTranslator_linux.cpp — deltas quando PumpSdl consome o SDL antes do ReadInput.
extern "C" void WYD_Linux_AccumulateMouseDelta(int dx, int dy, int wheel);
extern "C" void WYD_Linux_SetCompositionText(const char* text);

static void PushClipboardAsChars(HWND focus)
{
	char* clip = SDL_GetClipboardText();
	if (!clip || !clip[0])
	{
		if (clip)
			SDL_free(clip);
		return;
	}
	for (const char* p = clip; *p; ++p)
		PushMsg(focus, WM_CHAR, static_cast<WPARAM>(static_cast<unsigned char>(*p)), 0);
	SDL_free(clip);
}

#if defined(__SWITCH__)
// Implementado no bootstrap (switch/source/main_switch.cpp): lê pad/touch pela
// libnx e injeta via WYD_Switch_Inject*. Chamada a cada frame do jogo (SW-60/61).
extern "C" void WYD_Switch_PollInput(void);
#endif

void PumpSdl(HWND focus)
{
	g_pumpFocus = focus;
#ifdef WYD_LINUX
	// -----------------------------------------------------------------------
	// SW-87: captura comparável entre backends. WYD_SNAPSHOT_AT=<frame> injeta
	// VK_SNAPSHOT (PrintScreen) na fila do PRÓPRIO cliente, que grava
	// ScreenShot/CaptureXXXX.bmp via D3DXSaveSurfaceToFile. É o mesmo caminho da
	// tecla do jogo, então GLES e DXVK produzem imagens comparáveis pixel a pixel
	// — sem depender de captura de tela do sistema (que não existe no Switch).
	{
		static long long s_pumpFrames = 0;
		static long long s_snapAt = -2;
		if (s_snapAt == -2) {
			const char* e = std::getenv("WYD_SNAPSHOT_AT");
			s_snapAt = e ? std::atoll(e) : -1;
		}
		++s_pumpFrames;
		if (s_snapAt > 0 && s_pumpFrames == s_snapAt)
			PushMsg(focus, WM_KEYUP, 0x2C /*VK_SNAPSHOT*/, 0);
	}
#endif
#if defined(__SWITCH__)
	WYD_Switch_PollInput();
#endif
	SDL_Window* focusWin = reinterpret_cast<SDL_Window*>(focus);
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			PushMsg(focus, WM_CLOSE, 0, 0);
			break;
#if defined(WYD_SDL3)
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			PushMsg(focus, WM_CLOSE, 0, 0);
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			PushMsg(focus, WM_ACTIVATEAPP, TRUE, 0);
			PushMsg(focus, WM_ACTIVATE, 1, 0); // WA_ACTIVE — BGM/IME em NewApp::MsgProc
			// Não forçar StartTextInput aqui — FocusChatInput / SetIMEOpenStatus cuidam disso.
			// Start automático prendia WASD no IME no PC (chat “morto” mas teclado preso).
			SoftCaptureMouse(true);
			SyncCursorForGame();
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			PushMsg(focus, WM_ACTIVATEAPP, FALSE, 0);
			PushMsg(focus, WM_ACTIVATE, 0, 0); // WA_INACTIVE
			SoftCaptureMouse(false);
			ShowSystemCursor();
			break;
		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			SoftCaptureMouse(true);
			SyncCursorForGame();
			break;
		case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
			float fx = 0, fy = 0;
			const Uint32 bs = SDL_GetMouseState(&fx, &fy);
			if ((bs & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK)) == 0)
				ShowSystemCursor();
			break;
		}
		case SDL_EVENT_WINDOW_RESIZED: {
			// Logical size — matches CreateDevice / Config backbuffer contract.
			const int rw = ev.window.data1;
			const int rh = ev.window.data2;
			PushMsg(focus, WM_SIZE, 0,
				(static_cast<LPARAM>(rh & 0xFFFF) << 16) | (rw & 0xFFFF));
			if (focusWin && rw > 0 && rh > 0)
			{
				SDL_Rect r { 0, 0, rw, rh };
				Wyd_SdlSetTextInputArea(focusWin, &r);
			}
			break;
		}
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
			// HiDPI drawable change: only forward WM_SIZE when pixels ≠ logical
			// (avoids duplicate resize with WINDOW_RESIZED under 1:1 density).
			int logicalW = 0, logicalH = 0, pixW = 0, pixH = 0;
			if (focusWin)
			{
				SDL_GetWindowSize(focusWin, &logicalW, &logicalH);
				SDL_GetWindowSizeInPixels(focusWin, &pixW, &pixH);
			}
			const int rw = ev.window.data1;
			const int rh = ev.window.data2;
			if (pixW > 0 && pixH > 0 && (pixW != logicalW || pixH != logicalH))
			{
				PushMsg(focus, WM_SIZE, 0,
					(static_cast<LPARAM>(rh & 0xFFFF) << 16) | (rw & 0xFFFF));
			}
			break;
		}
		case SDL_EVENT_WINDOW_MINIMIZED:
			PushMsg(focus, WM_ACTIVATEAPP, FALSE, 0);
			PushMsg(focus, WM_ACTIVATE, 0, 0);
			break;
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_SHOWN:
			PushMsg(focus, WM_ACTIVATEAPP, TRUE, 0);
			PushMsg(focus, WM_ACTIVATE, 1, 0);
			SoftCaptureMouse(true);
			SyncCursorForGame();
			break;
#else
		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
				PushMsg(focus, WM_CLOSE, 0, 0);
			else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
				PushMsg(focus, WM_ACTIVATEAPP, TRUE, 0);
				PushMsg(focus, WM_ACTIVATE, 1, 0);
				SoftCaptureMouse(true);
				SyncCursorForGame();
			} else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
				PushMsg(focus, WM_ACTIVATEAPP, FALSE, 0);
				PushMsg(focus, WM_ACTIVATE, 0, 0);
				SoftCaptureMouse(false);
				ShowSystemCursor();
			} else if (ev.window.event == SDL_WINDOWEVENT_ENTER) {
				SoftCaptureMouse(true);
				SyncCursorForGame();
			} else if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
				const Uint32 bs = SDL_GetMouseState(nullptr, nullptr);
				if ((bs & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK)) == 0)
					ShowSystemCursor();
			} else if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
				|| ev.window.event == SDL_WINDOWEVENT_RESIZED) {
				const int rw = ev.window.data1;
				const int rh = ev.window.data2;
				PushMsg(focus, WM_SIZE, 0,
					(static_cast<LPARAM>(rh & 0xFFFF) << 16) | (rw & 0xFFFF));
				if (focusWin && rw > 0 && rh > 0) {
					SDL_Rect r { 0, 0, rw, rh };
					Wyd_SdlSetTextInputArea(focusWin, &r);
				}
			}
			break;
#endif
		case SDL_KEYDOWN: {
			const SDL_Keycode key = Wyd_SdlEventKey(ev);
			// Ctrl+V → colar clipboard como WM_CHAR (login/chat sem OpenClipboard Win32).
			const bool pasteV =
#if defined(WYD_SDL3)
				(key == SDLK_V || key == SDLK_v);
#else
				(key == SDLK_v);
#endif
			if ((SDL_GetModState() & KMOD_CTRL) != 0 && pasteV)
			{
				PushClipboardAsChars(focus);
				break;
			}
			PushMsg(focus, WM_KEYDOWN, MapSdlKey(key), 0);
			break;
		}
		case SDL_KEYUP:
			PushMsg(focus, WM_KEYUP, MapSdlKey(Wyd_SdlEventKey(ev)), 0);
			break;
		case SDL_TEXTINPUT: {
			// SW-62: rastro dos caracteres que vêm do teclado físico (SDL_TEXTINPUT)
			// para separar "não chegou ao jogo" de "chegou e não foi desenhado".
			static int nText = 0;
			if (nText++ < 24)
				std::fprintf(stderr, "[INPUT] texto SDL '%s' -> WM_CHAR\n", ev.text.text);
			for (const char* p = ev.text.text; *p; ++p)
				PushMsg(focus, WM_CHAR, static_cast<WPARAM>(static_cast<unsigned char>(*p)), 0);
			break;
		}
		case SDL_TEXTEDITING:
			WYD_Linux_SetCompositionText(ev.edit.text);
			break;
		case SDL_MOUSEMOTION:
			WYD_Linux_AccumulateMouseDelta(
#if defined(WYD_SDL3)
				(int)ev.motion.xrel, (int)ev.motion.yrel, 0);
			{
				int mx = (int)ev.motion.x;
				int my = (int)ev.motion.y;
#else
				ev.motion.xrel, ev.motion.yrel, 0);
			{
				int mx = ev.motion.x;
				int my = ev.motion.y;
#endif
				if (WYD_TouchV2_Enabled() && WYD_TouchV2_HasCapture())
				{
					auto* win = focus ? reinterpret_cast<SDL_Window*>(focus) : PrimaryWindow();
					int ww = 0, wh = 0;
					if (win)
						SDL_GetWindowSize(win, &ww, &wh);
					if (WYD_TouchV2_PointerWindow(0, mx, my, ww, wh, 0, 0, 1))
					{
						SyncCursorForGame();
						break; // não enfileirar WM_MOUSEMOVE no stick/look
					}
				}
				SyncCursorForGame();
				PushMsg(focus, WM_MOUSEMOVE, 0,
					(static_cast<LPARAM>(my & 0xFFFF) << 16) | (mx & 0xFFFF));
			}
			break;
		case SDL_MOUSEBUTTONDOWN: {
			SoftCaptureMouse(true);
			SyncCursorForGame();
#if defined(WYD_SDL3)
			int bx = (int)ev.button.x;
			int by = (int)ev.button.y;
#else
			int bx = ev.button.x;
			int by = ev.button.y;
#endif
			if (ev.button.button == SDL_BUTTON_LEFT && WYD_TouchV2_Enabled())
			{
				auto* win = focus ? reinterpret_cast<SDL_Window*>(focus) : PrimaryWindow();
				int ww = 0, wh = 0;
				if (win)
					SDL_GetWindowSize(win, &ww, &wh);
				if (WYD_TouchV2_PointerWindow(0, bx, by, ww, wh, 1, 0, 0))
					break; // consumido pelo Action Bus — sem WM_LBUTTONDOWN
			}
			if (ev.button.button == SDL_BUTTON_LEFT)
				PushMsg(focus, WM_LBUTTONDOWN, MK_LBUTTON,
					(static_cast<LPARAM>(by & 0xFFFF) << 16) | (bx & 0xFFFF));
			else if (ev.button.button == SDL_BUTTON_RIGHT)
				PushMsg(focus, WM_RBUTTONDOWN, MK_RBUTTON,
					(static_cast<LPARAM>(by & 0xFFFF) << 16) | (bx & 0xFFFF));
			else if (ev.button.button == SDL_BUTTON_MIDDLE)
				PushMsg(focus, WM_MBUTTONDOWN, MK_MBUTTON,
					(static_cast<LPARAM>(by & 0xFFFF) << 16) | (bx & 0xFFFF));
			break;
		}
		case SDL_MOUSEBUTTONUP: {
#if defined(WYD_SDL3)
			int bx = (int)ev.button.x;
			int by = (int)ev.button.y;
#else
			int bx = ev.button.x;
			int by = ev.button.y;
#endif
			if (ev.button.button == SDL_BUTTON_LEFT && WYD_TouchV2_Enabled())
			{
				auto* win = focus ? reinterpret_cast<SDL_Window*>(focus) : PrimaryWindow();
				int ww = 0, wh = 0;
				if (win)
					SDL_GetWindowSize(win, &ww, &wh);
				if (WYD_TouchV2_PointerWindow(0, bx, by, ww, wh, 0, 1, 0))
					break;
			}
			if (ev.button.button == SDL_BUTTON_LEFT)
				PushMsg(focus, WM_LBUTTONUP, 0,
					(static_cast<LPARAM>(by & 0xFFFF) << 16) | (bx & 0xFFFF));
			else if (ev.button.button == SDL_BUTTON_RIGHT)
				PushMsg(focus, WM_RBUTTONUP, 0,
					(static_cast<LPARAM>(by & 0xFFFF) << 16) | (bx & 0xFFFF));
			break;
		}
		case SDL_MOUSEWHEEL:
#if defined(WYD_SDL3)
			WYD_Linux_AccumulateMouseDelta(0, 0, (int)(ev.wheel.y * 120));
			PushMsg(focus, WM_MOUSEWHEEL, ((int)(ev.wheel.y * 120)) << 16, 0);
#else
			WYD_Linux_AccumulateMouseDelta(0, 0, ev.wheel.y * 120);
			PushMsg(focus, WM_MOUSEWHEEL, (ev.wheel.y * 120) << 16, 0);
#endif
			break;
		case SDL_FINGERDOWN:
		case SDL_FINGERUP:
		case SDL_FINGERMOTION: {
			// PeekMessage drenava FINGER no default — HUD nunca via toque nativo.
			// Fora do campo (select server / login / char): TouchV2 fica off e o HUD
			// GeomControl também (V2 desliga v1). Sem passthrough → toque morto (L-36).
			const int v2 = WYD_TouchV2_Enabled();
			auto* win = focus ? reinterpret_cast<SDL_Window*>(focus) : PrimaryWindow();
			int ww = 0, wh = 0;
			if (win)
				SDL_GetWindowSize(win, &ww, &wh);
			if (ww <= 0 || wh <= 0)
				break;
			const int fid = static_cast<int>(
#if defined(WYD_SDL3)
				ev.tfinger.fingerID
#else
				ev.tfinger.fingerId
#endif
			) + 100;
			const int isDown = (ev.type == SDL_FINGERDOWN) ? 1 : 0;
			const int isUp = (ev.type == SDL_FINGERUP) ? 1 : 0;
			const int isMove = (ev.type == SDL_FINGERMOTION) ? 1 : 0;
			int handled = 0;
#if defined(__SWITCH__)
			// SW-63: no Switch o dedo é lido pela libnx (WYD_Switch_PollInput) e entra
			// como mouse (id 0), que o TouchV2 consome no MsgProc — a mesma rota do
			// mouse no desktop. Se o backend do SDL também entregasse FINGER aqui, o
			// mesmo dedo viraria DOIS ponteiros (stick + clique). Fonte única: libnx.
			(void)fid; (void)v2; (void)ww; (void)wh;
#else
			if (v2)
				handled = WYD_TouchV2_Finger(fid, ev.tfinger.x, ev.tfinger.y, ww, wh, isDown, isUp, isMove);
			else if (WYD_Linux_TouchUiEnabled())
				handled = WYD_Linux_TouchHudFinger(fid, ev.tfinger.x, ev.tfinger.y, ww, wh, isDown, isUp, isMove);
#endif
			if (!handled)
			{
#if defined(__SWITCH__)
				// SW-61: no Switch o touch é lido pela libnx (WYD_Switch_PollInput) e
				// injetado por lá. Se o backend do SDL também entregasse FINGER, o
				// dedo viraria DOIS cliques — então aqui não sintetizamos nada.
				(void)isDown; (void)isUp; (void)isMove;
#else
				// Menus / UI nativa: finger → LMB (coords de janela; MsgProc faz MapCursor).
				const int mx = static_cast<int>(ev.tfinger.x * static_cast<float>(ww));
				const int my = static_cast<int>(ev.tfinger.y * static_cast<float>(wh));
				const LPARAM lp = (static_cast<LPARAM>(my & 0xFFFF) << 16) | (mx & 0xFFFF);
				if (isDown)
					PushMsg(focus, WM_LBUTTONDOWN, MK_LBUTTON, lp);
				else if (isUp)
					PushMsg(focus, WM_LBUTTONUP, 0, lp);
				else if (isMove)
					PushMsg(focus, WM_MOUSEMOVE, MK_LBUTTON, lp);
#endif
			}
			break;
		}
		default:
			break;
		}
	}
}

} // namespace

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
	if (!Wyd_SdlInitVideoEvents())
	{
		std::fprintf(stderr, "[WYDLINUX] SDL_Init video/events falhou: %s\n", SDL_GetError());
		return nullptr;
	}
	// Evita FINGER+MOUSE duplicado no mesmo toque (HUD recebe os dois e conflita id).
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

	const char* forceFs = std::getenv("WYD_FULLSCREEN");
	const char* fsMode = std::getenv("WYD_FS_MODE"); // desktop | exclusive | window
	bool wantFs = (dwStyle & WS_POPUP) || (forceFs && forceFs[0] == '1');
#if defined(__aarch64__) || defined(__ARM_ARCH)
	wantFs = true; // phone: sempre fullscreen desktop
#endif
	// Default desktop (borderless covering display) — exclusive no tamanho do
	// backbuffer deixava faixas pretas nas laterais (ex.: 1280×800 em 1820×960).
	bool useExclusive = fsMode && std::strcmp(fsMode, "exclusive") == 0;
	const bool useBorderlessWin = fsMode && std::strcmp(fsMode, "window") == 0;
	bool useDesktopFs = wantFs && !useExclusive && !useBorderlessWin;

	int winW = nWidth > 0 ? nWidth : 1280;
	int winH = nHeight > 0 ? nHeight : 720;

#if defined(WYD_SDL3)
	// Produção: janela SDL3 (Vulkan flag) + DXVK WSI SDL3 → present Vulkan.
	// Sem OpenGL/GLX no caminho do cliente.
	if (!std::getenv("DXVK_WSI_DRIVER") || !std::getenv("DXVK_WSI_DRIVER")[0])
		setenv("DXVK_WSI_DRIVER", "SDL3", 1);

#if !defined(WYD_GLES)
	// Pré-carrega libvulkan via SDL antes do CreateDevice DXVK (WSI surface).
	if (!SDL_Vulkan_LoadLibrary(nullptr))
	{
		std::fprintf(stderr,
			"[WYDLINUX] AVISO: SDL_Vulkan_LoadLibrary: %s (DXVK ainda pode carregar)\n",
			SDL_GetError());
	}
#endif

	// Desktop FS: criar já no tamanho do monitor (evita pillarbox no WSI).
	// Display retrato: letterbox — janela landscape ocupando a largura do painel,
	// centralizada verticalmente, sem esticar o backbuffer no Present (o DXVK
	// esticaria 1280x720 landscape em 1080x2246 retrato).
	int portraitPosX = SDL_WINDOWPOS_CENTERED;
	int portraitPosY = SDL_WINDOWPOS_CENTERED;
	bool bPortraitLetterbox = false;
	if (useDesktopFs)
	{
		int count = 0;
		SDL_DisplayID* ids = SDL_GetDisplays(&count);
		if (ids && count > 0)
		{
			const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(ids[0]);
			if (mode && mode->w > 0 && mode->h > 0)
			{
				if (mode->h > mode->w)
				{
					// Aspect do jogo = nWidth/nHeight (landscape). Ocupa a largura
					// do retrato e deriva a altura; centraliza no eixo vertical.
					const int baseW = nWidth > 0 ? nWidth : 1280;
					const int baseH = nHeight > 0 ? nHeight : 720;
					winW = mode->w;
					winH = (int)((long long)mode->w * (long long)baseH / (long long)baseW);
					if (winH < 1)
						winH = 1;
					if (winH > mode->h)
					{
						winH = mode->h;
						winW = (int)((long long)mode->h * (long long)baseW / (long long)baseH);
					}
					bPortraitLetterbox = true;
					SDL_Rect bounds {};
					if (SDL_GetDisplayBounds(ids[0], &bounds))
					{
						portraitPosX = bounds.x + (bounds.w - winW) / 2;
						portraitPosY = bounds.y + (bounds.h - winH) / 2;
					}
				}
				else
				{
					winW = mode->w;
					winH = mode->h;
				}
			}
			SDL_free(ids);
		}
	}

	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
		lpWindowName ? lpWindowName : "WYD");
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, winW);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, winH);
#if defined(WYD_GLES)
	// GLES3: janela GL (contexto criado por SDL_GL_CreateContext no backend).
	// No EGL do Switch o config (depth/stencil) é escolhido AQUI, na janela.
	// Preferir ES 3.0 (Mesa switch); 3.2 pode não existir — CreateDevice faz fallback.
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#else
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
#endif
	// Manter backbuffer alinhado ao tamanho pedido (evita Present 2246/2880 sem controle).
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, false);
#if defined(__SWITCH__)
	// No HOS a NWindow já é fullscreen — forçar FS evita janela “windowed” quebrada
	// com [WINDOW] 1 (sintoma: CreateDevice → REF → exit).
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
	wantFs = true;
	useExclusive = false;
	useDesktopFs = true;
#endif
	if (bPortraitLetterbox)
	{
		// Retrato: janela landscape borderless centralizada (letterbox), sem FS desktop.
		SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
		if (portraitPosX != SDL_WINDOWPOS_CENTERED)
			SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, portraitPosX);
		if (portraitPosY != SDL_WINDOWPOS_CENTERED)
			SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, portraitPosY);
	}
	else if (wantFs)
	{
		if (useBorderlessWin)
			SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
		else
			SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
	}

	SDL_Window* win = SDL_CreateWindowWithProperties(props);
	SDL_DestroyProperties(props);
	if (!win) {
		std::fprintf(stderr, "[WYDLINUX] SDL3_CreateWindow falhou: %s\n", SDL_GetError());
		return nullptr;
	}
	if (wantFs && !useBorderlessWin && !bPortraitLetterbox)
	{
		if (useDesktopFs)
		{
			SDL_SetWindowFullscreenMode(win, nullptr); // desktop / borderless
			SDL_SetWindowFullscreen(win, true);
		}
		else
		{
			SDL_DisplayID disp = SDL_GetDisplayForWindow(win);
			SDL_DisplayMode mode {};
			if (SDL_GetClosestFullscreenDisplayMode(disp, nWidth > 0 ? nWidth : winW,
					nHeight > 0 ? nHeight : winH, 0.0f, true, &mode))
				SDL_SetWindowFullscreenMode(win, &mode);
			SDL_SetWindowFullscreen(win, true);
		}
	}
	{
		int logicalW = 0, logicalH = 0, pixW = 0, pixH = 0;
		SDL_GetWindowSize(win, &logicalW, &logicalH);
		SDL_GetWindowSizeInPixels(win, &pixW, &pixH);
		const char* vdrv = SDL_GetCurrentVideoDriver();
		std::fprintf(stderr,
			"[WYDLINUX] SDL3+Vulkan WSI=%s video=%s logical=%dx%d pixels=%dx%d FS=%d mode=%s\n",
			std::getenv("DXVK_WSI_DRIVER") ? std::getenv("DXVK_WSI_DRIVER") : "?",
			vdrv ? vdrv : "?",
			logicalW, logicalH, pixW, pixH, wantFs ? 1 : 0,
			useExclusive ? "exclusive" : (useBorderlessWin ? "window" : "desktop"));
	}
#else
	const char* hiDpi = std::getenv("SDL_VIDEO_HIGHDPI_DISABLED");
	if (!hiDpi)
		SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");

#if defined(WYD_GLES)
	// SWITCH PORT: backend GLES3 → janela PRECISA do flag OPENGL (o
	// SDL_GL_CreateContext do d3d9_gles falha em janela criada p/ Vulkan).
	Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;
#else
	Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN;
#endif
	if (wantFs)
	{
		if (useBorderlessWin)
			flags |= SDL_WINDOW_BORDERLESS;
		else if (useExclusive)
			flags |= SDL_WINDOW_FULLSCREEN;
		else
			flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	if (!std::getenv("DXVK_WSI_DRIVER") || !std::getenv("DXVK_WSI_DRIVER")[0])
		setenv("DXVK_WSI_DRIVER", "SDL2", 1);

	SDL_DisplayMode mode {};
	if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) && SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
	{
		winW = mode.w;
		winH = mode.h;
	}

	SDL_Window* win = SDL_CreateWindow(
		lpWindowName ? lpWindowName : "WYD",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		winW, winH,
		flags);
	if (!win) {
		std::fprintf(stderr, "[WYDLINUX] SDL_CreateWindow falhou: %s\n", SDL_GetError());
		return nullptr;
	}
#endif

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
	PushMsg(hwnd, WM_ACTIVATEAPP, TRUE, 0);
	PushMsg(hwnd, WM_ACTIVATE, 1, 0); // foco inicial — BGM/IME
	SoftCaptureMouse(false);
	EnsureBlankCursor();
	// Text-input sob demanda (FocusChatInput / login edit). Start global prendia teclado no PC.
	{
		SDL_Rect r { 0, 0, nWidth > 0 ? nWidth : 1280, nHeight > 0 ? nHeight : 720 };
		Wyd_SdlSetTextInputArea(win, &r);
	}
	std::fprintf(stderr, "[WYDLINUX] window %dx%d WSI=%s FS_MODE=%s\n",
		winW, winH,
		std::getenv("DXVK_WSI_DRIVER") ? std::getenv("DXVK_WSI_DRIVER") : "?",
		fsMode ? fsMode : "(desktop)");
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

BOOL GetClientRect(HWND hWnd, LPRECT r)
{
	if (!hWnd || !r)
		return FALSE;
	int w = 0, h = 0;
	auto* win = reinterpret_cast<SDL_Window*>(hWnd);
#if defined(WYD_SDL3)
	// Com HIGH_PIXEL_DENSITY=false o contrato do backbuffer é o tamanho lógico.
	// Se pixels ≠ logical (HiDPI residual), preferir pixels só quando WYD_CLIENTRECT_PIXELS=1.
	const char* usePix = std::getenv("WYD_CLIENTRECT_PIXELS");
	if (usePix && usePix[0] == '1')
	{
		if (!SDL_GetWindowSizeInPixels(win, &w, &h) || w <= 0 || h <= 0)
			SDL_GetWindowSize(win, &w, &h);
	}
	else
	{
		SDL_GetWindowSize(win, &w, &h);
		if (w <= 0 || h <= 0)
			SDL_GetWindowSizeInPixels(win, &w, &h);
	}
#else
	SDL_GetWindowSize(win, &w, &h);
#endif
	r->left = 0;
	r->top = 0;
	r->right = w;
	r->bottom = h;
	return TRUE;
}

BOOL GetWindowRect(HWND hWnd, LPRECT r)
{
	if (!hWnd || !r)
		return FALSE;
	int x = 0, y = 0, w = 0, h = 0;
	auto* win = reinterpret_cast<SDL_Window*>(hWnd);
	SDL_GetWindowPosition(win, &x, &y);
#if defined(WYD_SDL3)
	const char* usePix = std::getenv("WYD_CLIENTRECT_PIXELS");
	if (usePix && usePix[0] == '1')
	{
		if (!SDL_GetWindowSizeInPixels(win, &w, &h) || w <= 0 || h <= 0)
			SDL_GetWindowSize(win, &w, &h);
	}
	else
	{
		SDL_GetWindowSize(win, &w, &h);
		if (w <= 0 || h <= 0)
			SDL_GetWindowSizeInPixels(win, &w, &h);
	}
#else
	SDL_GetWindowSize(win, &w, &h);
#endif
	r->left = x;
	r->top = y;
	r->right = x + w;
	r->bottom = y + h;
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

BOOL SetWindowPos(HWND hWnd, HWND /*hWndInsertAfter*/, int X, int Y, int cx, int cy, UINT uFlags)
{
	if (!hWnd)
		return FALSE;
	auto* win = reinterpret_cast<SDL_Window*>(hWnd);
	if (!(uFlags & SWP_NOSIZE) && cx > 0 && cy > 0) {
#if defined(WYD_SDL3)
		const bool isFs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN;
		const bool desktopFs = isFs && (SDL_GetWindowFullscreenMode(win) == nullptr);
		if (desktopFs) {
			// FULLSCREEN_DESKTOP: não encolher para o backbuffer (RENDER_SCALE).
			int curW = 0, curH = 0;
			SDL_GetWindowSize(win, &curW, &curH);
			if (cx >= curW && cy >= curH)
				SDL_SetWindowSize(win, cx, cy);
		} else if (isFs && SDL_GetWindowFullscreenMode(win) != nullptr) {
			SDL_DisplayID disp = SDL_GetDisplayForWindow(win);
			SDL_DisplayMode mode {};
			if (SDL_GetClosestFullscreenDisplayMode(disp, cx, cy, 0.0f, true, &mode))
				SDL_SetWindowFullscreenMode(win, &mode);
			SDL_SetWindowSize(win, cx, cy);
		} else {
			SDL_SetWindowSize(win, cx, cy);
		}
#else
		const Uint32 flags = SDL_GetWindowFlags(win);
		if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
			int curW = 0, curH = 0;
			SDL_GetWindowSize(win, &curW, &curH);
			if (cx >= curW && cy >= curH)
				SDL_SetWindowSize(win, cx, cy);
		} else {
			SDL_SetWindowSize(win, cx, cy);
		}
#endif
		{
			SDL_Rect r { 0, 0, cx, cy };
			Wyd_SdlSetTextInputArea(win, &r);
		}
	}
	if (!(uFlags & SWP_NOMOVE))
		SDL_SetWindowPosition(win, X, Y);
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
#if defined(WYD_SDL3)
	float fx = 0, fy = 0;
	SDL_GetMouseState(&fx, &fy);
	int x = (int)fx, y = (int)fy;
#else
	int x = 0, y = 0;
	SDL_GetMouseState(&x, &y);
#endif
	if (!g_windows.empty()) {
		SDL_Window* win = reinterpret_cast<SDL_Window*>(g_windows.begin()->first);
		int wx = 0, wy = 0;
		SDL_GetWindowPosition(win, &wx, &wy);
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
	SDL_GetWindowPosition(win, &wx, &wy);
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
	SDL_GetWindowPosition(win, &wx, &wy);
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
	const auto* state = Wyd_SdlGetKeyboardState(nullptr);
	if (!state)
		return 0;
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
