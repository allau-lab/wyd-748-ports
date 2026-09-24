# include "pch.h"
# include "EventTranslator.h"
# include "TMGlobal.h"
# include "TMCamera.h"
# include "TMFieldScene.h"
# include "TMHuman.h"
# include "RenderDevice.h"
# include "TimerManager.h"
# include "TouchControlsUI.h"

# ifdef WYD_USE_SDL3
#   include <SDL3/SDL.h>
#   include <SDL3/SDL_events.h>
#   include <SDL3/SDL_mouse.h>
#   include <SDL3/SDL_timer.h>
#   include <SDL3/SDL_video.h>
# else
#   include <SDL.h>
# endif
# include <cstring>
# include <cmath>

// Port real do EventTranslator: mouse/teclado/roda via SDL (sem DirectInput no-op).
// PeekMessage (winuser_sdl) já consome SDL → WM_*; OnMouseEvent PRECISA atualizar
// g_pCursor / ObjectManager — stubs deixavam o cursor do jogo congelado.

namespace
{

SDL_Window* WindowFromHwnd(HWND h)
{
    return reinterpret_cast<SDL_Window*>(h);
}

// Mapear scancode SDL → índice estilo DIK aproximado (0..255) usado por m_bKey.
unsigned MapKey(SDL_Scancode sc)
{
    if (sc >= 0 && sc < 256)
        return static_cast<unsigned>(sc);
    return 0;
}

// PeekMessage drena SDL antes de ReadInputEventData; deltas relativos ficam aqui.
int g_pendingDx = 0;
int g_pendingDy = 0;
int g_pendingWheel = 0;

# ifdef WYD_USE_SDL3
void SetWindowGrabState(SDL_Window* win, bool grabbed)
{
    if (win)
        SDL_SetWindowMouseGrab(win, grabbed);
}
void SetWindowRelativeMouseState(SDL_Window* win, bool enabled)
{
    if (win)
        SDL_SetWindowRelativeMouseMode(win, enabled);
}
void SetMouseCapture(bool enabled)
{
    SDL_CaptureMouse(enabled);
}
# else
void SetWindowGrabState(SDL_Window*, bool) {}
void SetWindowRelativeMouseState(SDL_Window* win, bool enabled)
{
    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}
void SetMouseCapture(bool enabled)
{
    SDL_CaptureMouse(enabled ? SDL_TRUE : SDL_FALSE);
}
# endif

} // namespace

// Chamado por winuser_sdl::PumpSdl quando o evento SDL já virou WM_*.
extern "C" void WYD_Linux_AccumulateMouseDelta(int dx, int dy, int wheel);

// Soma os deltas entre frames; ReadInputEventData drena (zera) uma vez por frame.
extern "C" void WYD_Linux_AccumulateMouseDelta(int dx, int dy, int wheel)
{
    g_pendingDx += dx;
    g_pendingDy += dy;
    g_pendingWheel += wheel;
}

EventTranslator::EventTranslator()
{
    m_lpCandList = nullptr;
    for (int i = 0; i < 256; ++i)
        m_bKey[i] = 0;
    m_bLock = 0;
    m_pDI = nullptr;
    m_pMouseDevice = nullptr;
    m_wParam = 0;
    m_bRBtn = 0;
    std::memset(m_strComp, 0, sizeof(m_strComp));
    std::memset(m_szResultStr, 0, sizeof(m_szResultStr));
    std::memset(m_bCompAttr, 0, sizeof(m_bCompAttr));
    m_dwCompAttrLen = 0;
    g_pEventTranslator = this;
    m_bAlt = m_bShift = m_bCtrl = 0;
    dx = dy = wheel = viewchange = 0;
    button[0] = button[1] = button[2] = 0;
    lastButtonState[0] = lastButtonState[1] = lastButtonState[2] = 0;
    m_nLastLolHoldX = m_nLastLolHoldY = -1;
    m_hWnd = nullptr;
    m_hOldIMC = nullptr;
}

EventTranslator::~EventTranslator()
{
    Finalize();
    FinalizeIME();
    g_pEventTranslator = nullptr;
}

BOOL EventTranslator::Initialize(HWND hWnd)
{
    m_hWnd = hWnd;
#ifdef WYD_USE_SDL3
    // SDL3: SDL_InitSubSystem retorna bool (true = sucesso); SDL2 retorna int 0.
    if (!SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO))
#else
    if (SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_VIDEO) != 0)
#endif
        return FALSE;
#ifdef WYD_USE_SDL3
    SDL_StartTextInput(WindowFromHwnd(hWnd));
#else
    SDL_StartTextInput();
#endif
    return InitializeInputDevice(hWnd) != 0;
}

int EventTranslator::InitializeIME()
{
    // IME nativo Wayland/SDL text input — composição via SDL_TEXTINPUT / TEXTEDITING.
    return 1;
}

int EventTranslator::InitializeInputDevice(HWND hWnd)
{
    m_hWnd = hWnd;
    m_bLock = 0;
    // Captura soft (eventos) sem relative mode / window grab (não prende o mouse).
    SetWindowRelativeMouseState(WindowFromHwnd(hWnd), false);
    SetWindowGrabState(WindowFromHwnd(hWnd), false);
    SetMouseCapture(true);
    return 1;
}

void EventTranslator::Finalize()
{
    SetWindowRelativeMouseState(WindowFromHwnd(m_hWnd), false);
    SetMouseCapture(false);
    SetWindowGrabState(WindowFromHwnd(m_hWnd), false);
    m_pMouseDevice = nullptr;
    m_pDI = nullptr;
    m_bLock = 0;
}

void EventTranslator::FinalizeIME()
{
#ifdef WYD_USE_SDL3
    SDL_StopTextInput(WindowFromHwnd(m_hWnd));
#else
    SDL_StopTextInput();
#endif
}

BOOL EventTranslator::IsNative() { return TRUE; }
void EventTranslator::SetIMENative()
{
#ifdef WYD_USE_SDL3
    SDL_StartTextInput(WindowFromHwnd(m_hWnd));
#else
    SDL_StartTextInput();
#endif
}
void EventTranslator::SetIMEAlphaNumeric() {}
void EventTranslator::SetIMEOpenStatus(int bOpen)
{
    if (bOpen) {
#ifdef WYD_USE_SDL3
        SDL_StartTextInput(WindowFromHwnd(m_hWnd));
#else
        SDL_StartTextInput();
#endif
    } else {
#ifdef WYD_USE_SDL3
        SDL_StopTextInput(WindowFromHwnd(m_hWnd));
#else
        SDL_StopTextInput();
#endif
    }
}
int EventTranslator::IsIMEOpenStatus()
{
#ifdef WYD_USE_SDL3
    return SDL_TextInputActive(WindowFromHwnd(m_hWnd)) ? 1 : 0;
#else
    return SDL_IsTextInputActive() ? 1 : 0;
#endif
}

void EventTranslator::SetVisibleCandidateList(int, int) {}

void EventTranslator::Lock()
{
    // Port Linux: captura eventos sem prender/confinar o ponteiro.
    m_bLock = 1;
    SetWindowRelativeMouseState(WindowFromHwnd(m_hWnd), false);
    SetWindowGrabState(WindowFromHwnd(m_hWnd), false);
    SetMouseCapture(true);
}

void EventTranslator::Unlock()
{
    m_bLock = 0;
    SetWindowRelativeMouseState(WindowFromHwnd(m_hWnd), false);
    SetWindowGrabState(WindowFromHwnd(m_hWnd), false);
    // Mantém captura enquanto a janela tem foco — só libera no FOCUS_LOST.
    SetMouseCapture(true);
}

int EventTranslator::ReadInputEventData()
{
    // Deltas vindos do PumpSdl (quando PeekMessage já consumiu o SDL).
    dx = g_pendingDx;
    dy = g_pendingDy;
    wheel = g_pendingWheel;
    viewchange = g_pendingWheel / 120;
    g_pendingDx = g_pendingDy = g_pendingWheel = 0;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
#ifdef WYD_USE_SDL3
        case SDL_EVENT_QUIT:
#else
        case SDL_QUIT:
#endif
            if (g_pApp && g_pApp->m_hWnd)
                PostMessage(g_pApp->m_hWnd, WM_CLOSE, 0, 0);
            break;

#ifdef WYD_USE_SDL3
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            // Teclado jogável via WM_* (PeekMessage). Aqui só mods + m_bKey DIK-ish.
            const unsigned k = MapKey(ev.key.scancode);
            const int down = ev.key.down ? 1 : 0;
            if (k < 256)
                m_bKey[k] = down;
            m_bAlt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
            m_bCtrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
            m_bShift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            break;
        }
#else
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            // Teclado jogável via WM_* (PeekMessage). Aqui só mods + m_bKey DIK-ish.
            const unsigned k = MapKey(ev.key.keysym.scancode);
            const int down = ev.type == SDL_KEYDOWN;
            if (k < 256)
                m_bKey[k] = down;
            m_bAlt = (SDL_GetModState() & KMOD_ALT) != 0;
            m_bCtrl = (SDL_GetModState() & KMOD_CTRL) != 0;
            m_bShift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            break;
        }
#endif

#ifdef WYD_USE_SDL3
        case SDL_EVENT_TEXT_INPUT:
            // Fallback se o pump Win32 não correu (fila vazia). SDL3: text é NUL-terminated.
            for (const char* p = ev.text.text; p && *p; ++p)
                OnChar(*p, 0);
            break;
#else
        case SDL_TEXTINPUT:
            // SDL2 entrega texto UTF-8 NUL-terminated; não existe length.
            for (const char* p = ev.text.text; p && *p; ++p)
                OnChar(*p, 0);
            break;
#endif

#ifdef WYD_USE_SDL3
        case SDL_EVENT_TEXT_EDITING:
#else
        case SDL_TEXTEDITING:
#endif
            std::strncpy(m_strComp, ev.edit.text, sizeof(m_strComp) - 1);
            m_strComp[sizeof(m_strComp) - 1] = 0;
            break;

#ifdef WYD_USE_SDL3
        case SDL_EVENT_MOUSE_MOTION: {
            dx += static_cast<int>(ev.motion.xrel);
            dy += static_cast<int>(ev.motion.yrel);
            if (g_bTouchUi && TouchControlsUI::Instance().OnPointerMove(
                    0, static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y)))
                break;
            OnMouseEvent(WM_MOUSEMOVE, 0,
                         static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y));
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int down = ev.button.down ? 1 : 0;
            const int bx = static_cast<int>(ev.button.x);
            const int by = static_cast<int>(ev.button.y);
            if (g_bTouchUi) {
                auto& touch = TouchControlsUI::Instance();
                if (down) {
                    if (touch.OnPointerDown(static_cast<int>(ev.button.button), bx, by))
                        break;
                } else if (touch.OnPointerUp(static_cast<int>(ev.button.button), bx, by))
                    break;
            }
            if (ev.button.button == SDL_BUTTON_LEFT) {
                button[0] = down;
                OnMouseEvent(down ? WM_LBUTTONDOWN : WM_LBUTTONUP,
                             down ? MK_LBUTTON : 0, bx, by);
                if (down)
                    OnLMousePressed();
                else
                    OnLMouseReleased();
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                button[1] = down;
                // Do NOT latch m_bRBtn before dispatch: RButton() treats it as
                // "already handled this click" and would skip all consumable uses.
                OnMouseEvent(down ? WM_RBUTTONDOWN : WM_RBUTTONUP,
                             down ? MK_RBUTTON : 0, bx, by);
                if (down) {
                    OnRMousePressed();
                    if (g_bLolControls) {
                        m_nLastLolHoldX = bx;
                        m_nLastLolHoldY = by;
                    }
                } else
                    OnRMouseReleased();
            } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
                button[2] = down;
            }
            break;
        }

        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION: {
            if (!g_bTouchUi || !g_pDevice)
                break;
            const int fx = static_cast<int>(ev.tfinger.x * static_cast<float>(g_pDevice->m_dwScreenWidth));
            const int fy = static_cast<int>(ev.tfinger.y * static_cast<float>(g_pDevice->m_dwScreenHeight));
            const int fid = static_cast<int>(ev.tfinger.fingerID) + 100;
            auto& touch = TouchControlsUI::Instance();
            if (ev.type == SDL_EVENT_FINGER_DOWN)
                touch.OnPointerDown(fid, fx, fy);
            else if (ev.type == SDL_EVENT_FINGER_UP)
                touch.OnPointerUp(fid, fx, fy);
            else
                touch.OnPointerMove(fid, fx, fy);
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
            // SDL3: y é float, mesmo sinal do SDL2 (positivo = para longe do usuário).
            wheel += static_cast<int>(ev.wheel.y * 120.0f);
            viewchange += static_cast<int>(ev.wheel.y);
            break;
#else
        case SDL_MOUSEMOTION:
            dx += ev.motion.xrel;
            dy += ev.motion.yrel;
            if (g_bTouchUi && TouchControlsUI::Instance().OnPointerMove(0, ev.motion.x, ev.motion.y))
                break;
            OnMouseEvent(WM_MOUSEMOVE, 0, ev.motion.x, ev.motion.y);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            const int down = ev.type == SDL_MOUSEBUTTONDOWN;
            if (g_bTouchUi) {
                auto& touch = TouchControlsUI::Instance();
                if (down) {
                    if (touch.OnPointerDown(static_cast<int>(ev.button.button),
                                            ev.button.x, ev.button.y))
                        break;
                } else if (touch.OnPointerUp(static_cast<int>(ev.button.button),
                                              ev.button.x, ev.button.y))
                    break;
            }
            if (ev.button.button == SDL_BUTTON_LEFT) {
                button[0] = down;
                OnMouseEvent(down ? WM_LBUTTONDOWN : WM_LBUTTONUP,
                             down ? MK_LBUTTON : 0, ev.button.x, ev.button.y);
                if (down)
                    OnLMousePressed();
                else
                    OnLMouseReleased();
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                button[1] = down;
                // Do NOT latch m_bRBtn before dispatch: RButton() treats it as
                // "already handled this click" and would skip all consumable uses.
                OnMouseEvent(down ? WM_RBUTTONDOWN : WM_RBUTTONUP,
                             down ? MK_RBUTTON : 0, ev.button.x, ev.button.y);
                if (down) {
                    OnRMousePressed();
                    if (g_bLolControls) {
                        m_nLastLolHoldX = ev.button.x;
                        m_nLastLolHoldY = ev.button.y;
                    }
                } else
                    OnRMouseReleased();
            } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
                button[2] = down;
            }
            break;
        }

        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION: {
            if (!g_bTouchUi || !g_pDevice)
                break;
            const int fx = static_cast<int>(ev.tfinger.x * static_cast<float>(g_pDevice->m_dwScreenWidth));
            const int fy = static_cast<int>(ev.tfinger.y * static_cast<float>(g_pDevice->m_dwScreenHeight));
            const int fid = static_cast<int>(ev.tfinger.fingerId) + 100;
            auto& touch = TouchControlsUI::Instance();
            if (ev.type == SDL_FINGERDOWN)
                touch.OnPointerDown(fid, fx, fy);
            else if (ev.type == SDL_FINGERUP)
                touch.OnPointerUp(fid, fx, fy);
            else
                touch.OnPointerMove(fid, fx, fy);
            break;
        }

        case SDL_MOUSEWHEEL:
            wheel += ev.wheel.y * 120;
            viewchange += ev.wheel.y;
            break;
#endif

        default:
            break;
        }
    }

    // Estado de botão também via SDL (fallback se WM_* não atualizou).
    {
#ifdef WYD_USE_SDL3
        float mx = 0.f, my = 0.f;
        const Uint32 bs = SDL_GetMouseState(&mx, &my);
        button[0] = (bs & SDL_BUTTON_LMASK) != 0;
        button[1] = (bs & SDL_BUTTON_RMASK) != 0;
        button[2] = (bs & SDL_BUTTON_MMASK) != 0;
#else
        int mx = 0, my = 0;
        const Uint32 bs = SDL_GetMouseState(&mx, &my);
        button[0] = (bs & SDL_BUTTON_LMASK) != 0;
        button[1] = (bs & SDL_BUTTON_RMASK) != 0;
        button[2] = (bs & SDL_BUTTON_MMASK) != 0;
#endif
        // Never assign m_bRBtn from the raw button mask: UseItem latches it to
        // debounce a single click, and OnRMouseReleased clears it.
    }

    if (m_bAlt == 1 && !button[1]) {
        wheel = 3 * dy;
        viewchange = 3 * dy;
    }

    // Classic: while LMB held, re-fire move (DirectInput parity).
    // LoL: while RMB held, re-fire move only after the press frame and only when
    // the cursor moved enough — otherwise the camera-follow ground pick changes
    // every frame and GetRoute snaps facing toward a drifting click point.
    if (g_bLolControls) {
        if (button[1]
            && lastButtonState[1]
            && g_pCursor != nullptr && g_pDevice != nullptr && g_pCurrentScene != nullptr
            && g_pCursor->m_nPosX > 0.0f && (float)g_pDevice->m_dwScreenWidth > g_pCursor->m_nPosX
            && g_pCursor->m_nPosY > 0.0f && (float)g_pDevice->m_dwScreenHeight > g_pCursor->m_nPosY) {
            const int cx = static_cast<int>(g_pCursor->m_nPosX);
            const int cy = static_cast<int>(g_pCursor->m_nPosY);
            const int ddx = cx - m_nLastLolHoldX;
            const int ddy = cy - m_nLastLolHoldY;
            const int dist2 = ddx * ddx + ddy * ddy;
            // ~8px screen delta before repath while holding.
            if (dist2 >= 64) {
                m_nLastLolHoldX = cx;
                m_nLastLolHoldY = cy;
                // MK_MBUTTON marks holdrepeat for IssueMoveToPick debounce.
                g_pCurrentScene->OnMouseEvent(
                    WM_RBUTTONDOWN,
                    m_wParam | MK_RBUTTON | MK_MBUTTON,
                    cx,
                    cy);
            }
        }
        else if (!button[1]) {
            m_nLastLolHoldX = m_nLastLolHoldY = -1;
        }
        if (button[0])
            OnLMousePressed();
        else if (lastButtonState[0])
            OnLMouseReleased();
    }
    else if (button[0])
        OnLMousePressed();
    else if (lastButtonState[0])
        OnLMouseReleased();

    // Liberação do botão direito: limpa o latch m_bRBtn mesmo quando o
    // SDL_MOUSEBUTTONUP já foi consumido pelo PeekMessage/PumpSdl.
    if (!button[1] && lastButtonState[1])
        OnRMouseReleased();

    lastButtonState[0] = button[0];
    lastButtonState[1] = button[1];
    lastButtonState[2] = button[2];
    return 1;
}

int EventTranslator::CameraEventData()
{
    if (!g_pObjectManager || !g_pObjectManager->m_pCamera)
        return 1;

    TMCamera* pCamera = g_pObjectManager->m_pCamera;

    float fClose = 1.2f;
    if (g_pCurrentScene != nullptr && g_pCurrentScene->m_pMyHuman != nullptr) {
        if (g_pCurrentScene->m_pMyHuman->m_cMount == 1)
            fClose = 2.5f;

        fClose = (float)((float)g_pCurrentScene->m_pMyHuman->m_stScore.Con * 0.00019f) + fClose;
    }

    if (g_pCurrentScene == nullptr)
        return 1;

    ESCENE_TYPE dwSceneType = g_pCurrentScene->m_eSceneType;
    if ((dwSceneType == ESCENE_TYPE::ESCENE_FIELD || dwSceneType == ESCENE_TYPE::ESCENE_SELECT_SERVER ||
         dwSceneType == ESCENE_TYPE::ESCENE_DEMO || dwSceneType == ESCENE_TYPE::ESCENE_SELCHAR) &&
        g_pCurrentScene->m_sPlayDemo < 0) {
        if (pCamera->m_dwSetTime == 0) {
            // 7.48: botão do meio gira a câmera; direito é SkillUse.
            if (!pCamera->m_nQuaterView && button[2] && g_pCurrentScene->m_pGround != nullptr) {
                pCamera->m_fVerticalAngle = pCamera->m_fVerticalAngle - (float)((float)dy * 0.002f);
                if (pCamera->m_fVerticalAngle < -0.98539817f)
                    pCamera->m_fVerticalAngle = -0.98539817f;
                if (pCamera->m_fVerticalAngle > 0.75f)
                    pCamera->m_fVerticalAngle = 0.75f;

                pCamera->m_fHorizonAngle = (float)((float)dx * 0.0049f) + pCamera->m_fHorizonAngle;
                if (pCamera->m_fHorizonAngle > D3DXToRadian(360))
                    pCamera->m_fHorizonAngle = pCamera->m_fHorizonAngle - D3DXToRadian(360);
                if (pCamera->m_fHorizonAngle < 0.0)
                    pCamera->m_fHorizonAngle = pCamera->m_fHorizonAngle + D3DXToRadian(360);

                if (g_pCurrentScene->m_pMyHuman != nullptr &&
                    g_pCurrentScene->m_pMyHuman->m_cMount != 0) {
                    float nMaxVerticalAngle = 0.449f;
                    TMHuman* pMyHuman = g_pCurrentScene->m_pMyHuman;
                    if (pMyHuman->m_nMountSkinMeshType == 39 || pMyHuman->m_nMountSkinMeshType == 40 ||
                        (pMyHuman->m_nMountSkinMeshType == 20 && pMyHuman->m_sMountIndex != 3)) {
                        nMaxVerticalAngle = 0.23f;
                    }
                    else if (pMyHuman->m_nMountSkinMeshType == 38) {
                        nMaxVerticalAngle = 0.22f;
                    }

                    if (pCamera->m_fVerticalAngle > nMaxVerticalAngle)
                        pCamera->m_fVerticalAngle = nMaxVerticalAngle;
                }
            }
            if (pCamera->m_nQuaterView == 0 || pCamera->m_nQuaterView == 1) {
                const int zoomDirection = (g_pApp && g_pApp->m_nCameraView == 0) ? viewchange : wheel;
                if (pCamera->m_fSightLength > fClose && zoomDirection < 0) {
                    pCamera->m_fSightLength = (float)((float)wheel / 240.0f) + pCamera->m_fSightLength;
                    pCamera->m_fWantLength = pCamera->m_fSightLength;
                }
                if (fClose > pCamera->m_fSightLength) {
                    pCamera->m_fSightLength = fClose;
                    pCamera->m_fWantLength = pCamera->m_fSightLength;
                }
                if (zoomDirection > 0 && pCamera->m_fMaxCamLen > pCamera->m_fSightLength) {
                    pCamera->m_fSightLength = (float)((float)wheel / 240.0f) + pCamera->m_fSightLength;
                    pCamera->m_fWantLength = pCamera->m_fSightLength;
                }
                if (pCamera->m_fSightLength > pCamera->m_fMaxCamLen) {
                    pCamera->m_fSightLength = pCamera->m_fMaxCamLen;
                    pCamera->m_fWantLength = pCamera->m_fSightLength;
                }
            }

            pCamera->m_fBackHorizonAngle = pCamera->m_fHorizonAngle;
            pCamera->m_fBackVerticalAngle = pCamera->m_fVerticalAngle;
        }
        else if (pCamera->m_nEarthLevel == 10) {
            float fProgress = sinf(((((float)(g_pTimerManager->GetServerTime() - pCamera->m_dwSetTime) / 3000.0f) * D3DXToRadian(180)) / 2.0f) + 4.712389f);
            fProgress += 1.0f;
            if (fProgress >= 1.0f)
                fProgress = 1.0f;

            pCamera->m_fVerticalAngle = (float)((float)(1.0f - fProgress) * 0.1f) - (float)(D3DXToRadian(45) * fProgress);
            pCamera->m_fBackVerticalAngle = (float)((float)(1.0f - fProgress) * 0.1f) - (float)(D3DXToRadian(45) * fProgress);
            pCamera->m_fHorizonAngle = (float)((float)(1.0f - fProgress) * D3DXToRadian(180)) + (float)(D3DXToRadian(45) * fProgress);
            pCamera->m_fBackHorizonAngle = (float)((float)(1.0f - fProgress) * D3DXToRadian(180)) + (float)(D3DXToRadian(45) * fProgress);
            pCamera->m_fSightLength = (float)((float)(1.0f - fProgress) * 3.5f) + (float)(pCamera->m_fMaxCamLen * fProgress);
            pCamera->m_fWantLength = (float)((float)(1.0f - fProgress) * 3.5f) + (float)(pCamera->m_fMaxCamLen * fProgress);
        }
        else {
            float fProgress = (float)(g_pTimerManager->GetServerTime() - pCamera->m_dwSetTime);
            fProgress /= 1000.0f;
            if (fProgress > 1.0f)
                fProgress = 1.0f;

            pCamera->m_fVerticalAngle = ((((sinf(fProgress * D3DXToRadian(180)) * 12.0f) * 0.01f) * (float)pCamera->m_nEarthLevel)
                                         * (float)(1.0f - fProgress))
                                        + pCamera->m_fBackVerticalAngle;

            pCamera->m_fHorizonAngle = ((((sinf(fProgress * D3DXToRadian(180) * 12.0f)) * 0.01f) * (float)pCamera->m_nEarthLevel)
                                        * (float)(1.0f - fProgress))
                                       + pCamera->m_fBackHorizonAngle;
        }
    }

    return 1;
}

void EventTranslator::OnKeyDown(unsigned int iKeyCode)
{
    if (iKeyCode >= 256)
        return;
    if (m_bKey[iKeyCode] == 0) {
        m_bKey[iKeyCode] = 1;
        if (g_pObjectManager != nullptr)
            g_pObjectManager->OnKeyDownEvent(iKeyCode);
    }
}

void EventTranslator::OnKeyUp(unsigned int iKeyCode)
{
    if (iKeyCode >= 256)
        return;
    if (m_bKey[iKeyCode] != 0) {
        m_bKey[iKeyCode] = 0;
        if (g_pObjectManager != nullptr)
            g_pObjectManager->OnKeyUpEvent(iKeyCode);
    }
}

void EventTranslator::OnChar(char iCharCode, int lParam)
{
    if (g_pObjectManager != nullptr)
        g_pObjectManager->OnCharEvent(iCharCode, lParam);
}

void EventTranslator::OnIME(char, int) {}
void EventTranslator::OnIME2() {}
void EventTranslator::UpdateCompositionPos() {}

void EventTranslator::OnLMousePressed()
{
    button[0] = 1;
    if (g_pCursor != nullptr && g_pDevice != nullptr && g_pCurrentScene != nullptr) {
        if (g_pCursor->m_nPosX > 0.0f && (float)g_pDevice->m_dwScreenWidth > g_pCursor->m_nPosX
            && g_pCursor->m_nPosY > 0.0f && (float)g_pDevice->m_dwScreenHeight > g_pCursor->m_nPosY) {
            g_pCurrentScene->OnMouseEvent(
                WM_LBUTTONDOWN,
                m_wParam,
                static_cast<int>(g_pCursor->m_nPosX),
                static_cast<int>(g_pCursor->m_nPosY));
        }
    }
}

void EventTranslator::OnLMouseReleased()
{
    button[0] = 0;
}

void EventTranslator::OnRMousePressed()
{
    button[1] = 1;
    // SDL already dispatched WM_RBUTTONDOWN via OnMouseEvent. Win32 OnRMousePressed
    // also synthesizes 516; doing both with m_bRBtn pre-latched skipped UseItem.
    // Keep this as button-state only — grid RButton runs on the SDL path above.
}

void EventTranslator::OnRMouseReleased()
{
    button[1] = 0;
    m_bRBtn = 0;
}

void EventTranslator::OnMouseEvent(unsigned int nFlags, unsigned int wParam, int ix, int iy)
{
    m_wParam = wParam;
    if (g_pCursor != nullptr)
        g_pCursor->SetPosition(ix, iy);

    // PeekMessage/PumpSdl entrega WM_RBUTTONUP sem passar por OnRMouseReleased
    // (o DirectInput do Win32 limpa no release). Sem isso m_bRBtn fica latched
    // após UseItem e o próximo clique direito em consumível é ignorado.
    if (nFlags == WM_RBUTTONUP)
        m_bRBtn = 0;

    if (g_pObjectManager != nullptr)
        g_pObjectManager->OnMouseEvent(nFlags, wParam, ix, iy);
}
