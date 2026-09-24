#include "pch.h"
#include "TouchSystem.h"
#include "TouchControlsUI.h"
#include "TMGlobal.h"
#include "TMFieldScene.h"
#include "TMHuman.h"
#include "TMCamera.h"
#include "SControlContainer.h"
#include "SControl.h"
#include "SGrid.h"
#include "ObjectManager.h"
#include "TimerManager.h"
#include "RenderDevice.h"
#include "EventTranslator.h"
#include "TextureManager.h"
#include "Basedef.h"
#include "ResourceControl.h"

#if defined(WYD_SDL3)
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <limits.h>

namespace
{
	float Clampf(float v, float lo, float hi)
	{
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	}

	float Dist2(float ax, float ay, float bx, float by)
	{
		const float dx = ax - bx;
		const float dy = ay - by;
		return dx * dx + dy * dy;
	}

	unsigned WithAlpha(unsigned rgb, unsigned a)
	{
		return (a << 24) | (rgb & 0x00FFFFFFu);
	}

	const char* kIconFiles[] = {
		"stick.tga", "attack.tga",
		"inventory.tga", "skills.tga", "char.tga", "pk.tga",
		"look.tga", "swords2.tga", "reset.tga", "magic.tga",
	};

	std::string ExeDir()
	{
#if defined(__SWITCH__)
		// SWITCH PORT: sem /proc/self/exe no HOS — cwd é o root de assets.
		char cwd[PATH_MAX];
		if (!getcwd(cwd, sizeof(cwd)))
			return {};
		return cwd;
#else
		char buf[PATH_MAX];
		const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		if (n <= 0)
			return {};
		buf[n] = 0;
		std::string path(buf);
		const auto slash = path.find_last_of('/');
		if (slash == std::string::npos)
			return {};
		return path.substr(0, slash);
#endif
	}

	int LoadFileBytes(const std::string& path, std::vector<unsigned char>& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return 0;
		in.seekg(0, std::ios::end);
		const auto sz = in.tellg();
		if (sz <= 0)
			return 0;
		in.seekg(0, std::ios::beg);
		out.resize(static_cast<size_t>(sz));
		in.read(reinterpret_cast<char*>(out.data()), sz);
		return in.good() || in.eof();
	}
}

TouchSystem& TouchSystem::Instance()
{
	static TouchSystem inst;
	return inst;
}

TouchSystem::TouchSystem() = default;

void TouchSystem::ForceEnable(int on)
{
	s_bForceEnable = on ? 1 : 0;
	if (s_bForceEnable)
	{
		Instance().SetEnabled(1);
		// HUD único: o v1 (GeomControl) ficaria desenhando por cima se o Config
		// tivesse TOUCH_UI=1 — no console quem manda é este barramento.
		TouchControlsUI::Instance().SetFeatureEnabled(0);
	}
}

void TouchSystem::SetEnabled(int enabled)
{
	// SW-63: no console o bus é obrigatório — o Config do cliente é o do port de
	// celular e não tem TOUCH_V2, o que desligava pad e toque por completo.
	if (s_bForceEnable)
		enabled = 1;
	m_bEnabled = enabled ? 1 : 0;
	if (!m_bEnabled)
	{
		m_stickFinger = m_camFinger = m_attackFinger = m_worldFinger = kInvalid;
		m_stick.End();
		m_stickFade = 0.f;
		m_actions = TouchActions{};
		m_bHaveMoveAng = 0;
	}
	m_bLayoutReady = 0;
}

int TouchSystem::HasCapture() const
{
	if (!m_bEnabled || !IsFieldActive())
		return 0;
	if (m_stickFinger != kInvalid || m_camFinger != kInvalid || m_attackFinger != kInvalid ||
		m_worldFinger != kInvalid)
		return 1;
	return 0;
}

int TouchSystem::WantMouseSync() const
{
	if (!HasCapture())
		return 0;
	// Só sync GetMouseState se algum gesto ativo for do mouse (id==0).
	if (m_stickFinger == 0 || m_camFinger == 0 || m_attackFinger == 0 || m_worldFinger == 0)
		return 1;
	return 0;
}

int TouchSystem::IsFieldActive() const
{
	return (g_pCurrentScene && g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_FIELD) ? 1 : 0;
}

void TouchSystem::RebuildLayout()
{
	if (!g_pDevice)
		return;
	m_layout.W = static_cast<float>(g_pDevice->m_dwScreenWidth);
	m_layout.H = static_cast<float>(g_pDevice->m_dwScreenHeight);
	m_layout.s = (m_layout.W < m_layout.H) ? m_layout.W : m_layout.H;

	// Stick Vita: pad FIXO meio-esquerda (não canto baixo). Hit > 10028, sem engolir meia tela.
	m_layout.stickZoneCX = m_layout.W * 0.145f;
	m_layout.stickZoneCY = m_layout.H * 0.52f;
	m_layout.stickVisualR = m_layout.s * 0.132f;
	m_layout.stickHitR = m_layout.stickVisualR * 1.48f;

	// ATK alinhado à hotbar Mobile (direita, mesma faixa vertical dos slots)
	m_layout.attackR = m_layout.s * 0.088f;
	m_layout.attackCX = m_layout.W * 0.935f;
	m_layout.attackCY = m_layout.H - m_layout.attackR * 1.35f;

	// Coluna 2×4 removida (L-46): funções só via Menu Principal (orb HP).
	m_layout.keyR = 0.f;
	for (int i = 0; i < kHotKeys; ++i)
	{
		m_layout.keyCX[i] = -9999.f;
		m_layout.keyCY[i] = -9999.f;
	}

	// CamPad invisível (canto superior direito — só gesto, sem desenho)
	m_layout.camPadR = m_layout.s * 0.12f;
	m_layout.camPadCX = m_layout.W - m_layout.camPadR * 0.6f;
	m_layout.camPadCY = m_layout.camPadR * 0.8f;
	m_bLayoutReady = 1;
}

int TouchSystem::IsUiBlocked() const
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	auto vis = [](const SPanel* p) { return (p && p->IsVisible()) ? 1 : 0; };
	if (vis(scene->m_pShopPanel) || vis(scene->m_pTradePanel) || vis(scene->m_pAutoTrade))
		return 1;
	if (vis(scene->m_pCargoPanel) || vis(scene->m_pCargoPanel1))
		return 1;
	if (vis(scene->m_pInvenPanel) || vis(scene->m_pSkillPanel) || vis(scene->m_pCPanel))
		return 1;
	if (vis(scene->m_pccmode))
		return 1;
	if (vis(scene->m_pSystemPanel) || vis(scene->m_pPGTPanel))
		return 1;
	if (scene->m_pMessageBox && scene->m_pMessageBox->IsVisible())
		return 1;
	if (scene->m_pMessageBox2 && scene->m_pMessageBox2->IsVisible())
		return 1;
	if (auto* cc = scene->m_pControlContainer)
	{
		for (int i = 0; i < 8; ++i)
		{
			SControl* m = cc->m_pModalControl[i];
			if (m && m->m_bVisible == 1 && m->m_bModal == 1)
				return 1;
		}
	}
	if (g_pCursor && g_pCursor->m_pAttachedItem)
		return 1;
	return 0;
}

int TouchSystem::HitsNativeUi(float x, float y) const
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	auto* cc = scene->m_pControlContainer;
	if (!cc)
		return 0;

	const int ix = static_cast<int>(x);
	const int iy = static_cast<int>(y);

	auto hitCtrl = [&](SControl* c) -> int {
		return (c && c->IsVisible() && c->PtInControl(ix, iy)) ? 1 : 0;
	};

	// Botão + painel MACRO / Menu Principal / DROP / MENU PRINCIPAL (barra inferior)
	if (hitCtrl(scene->m_pCC_Btn))
		return 1;
	if (hitCtrl(scene->m_pccmode))
		return 1;
	if (hitCtrl(scene->m_pCPanel))
		return 1;
	if (hitCtrl(scene->m_pMiniPanel))
		return 1;
	if (hitCtrl(scene->m_pMiniBtn))
		return 1;
	if (hitCtrl(scene->m_pbutonDrop))
		return 1;
	if (hitCtrl(scene->m_pbutonShop))
		return 1;
	if (hitCtrl(scene->m_pHudMenuOrb))
		return 1;
	if (SControl* status = cc->FindControl(P_CHAR_STATE))
	{
		if (status->IsVisible() && status->PtInControl(ix, iy))
			return 1;
	}

	static const unsigned kHud[] = {
		B_CCMODE_SYSTEM, B_CCATTACK, B_CCPOTION, B_CCMOVE,
		B_HUD_MENU_ORB, B_HUD_MENU_MACRO, B_HUD_MENU_SKILL, B_HUD_MENU_PARTY,
		B_HUD_MENU_MAP, B_HUD_MENU_FRIEND, B_HUD_MENU_PK, B_HUD_MENU_TOUCH,
		B_CHAR, B_EQUIP, B_SKILL, B_QUESTLOG, B_HELP, B_SYSTEM, B_PARTY,
		B_PK_BTN, B_AUTOTRADEBTN,
	};
	for (unsigned id : kHud)
	{
		if (SControl* c = cc->FindControl(id))
		{
			if (c->IsVisible() && c->m_bSelectEnable && c->PtInControl(ix, iy))
				return 1;
		}
	}

	// Filhos do painel C.C. aberto
	if (scene->m_pccmode && scene->m_pccmode->IsVisible())
	{
		static const unsigned kCc[] = {
			B_CCMODE_DLG_MODE, B_CCMODE_DLG_HP, B_CCMODE_DLG_MOUNT,
			P_CCMODE_DLG_PONT, B_CCMODE_JEWEL,
		};
		for (unsigned id : kCc)
		{
			if (SControl* c = cc->FindControl(id))
			{
				if (c->IsVisible() && c->PtInControl(ix, iy))
					return 1;
			}
		}
	}

	// Barra nativa de skills / quick slots (não engolir com ATK/stick)
	if (hitCtrl(scene->m_pGridSkillBelt2) || hitCtrl(scene->m_pGridSkillBelt3) ||
		hitCtrl(scene->m_pShortSkillPanel) || hitCtrl(scene->m_pGridSkillBelt) ||
		hitCtrl(scene->m_pMainInfo1) || hitCtrl(scene->m_pMainInfo1_BG))
		return 1;
	if (hitCtrl(scene->m_pShortSkillTglBtn1) || hitCtrl(scene->m_pShortSkillTglBtn2))
		return 1;
	if (hitCtrl(scene->m_pAutoSkillPanel) || hitCtrl(scene->m_pAutoSkillMinus) ||
		hitCtrl(scene->m_pAutoSkillPlus) || hitCtrl(scene->m_pAutoSkillToggle) ||
		hitCtrl(scene->m_pAutoSkillNumText))
		return 1;
	if (hitCtrl(scene->m_pCC_Btn) || hitCtrl(scene->m_pccmode))
		return 1;
	for (int i = 0; i < 5; ++i)
	{
		if (hitCtrl(scene->m_pQuick_Sloat[i]))
			return 1;
	}
	return 0;
}

void TouchSystem::StopMoveNow()
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	scene->IssueStopMove();
	m_actions.moveActive = 0;
	m_actions.moveX = m_actions.moveY = 0.f;
	m_bHaveMoveAng = 0;
	m_lastMoveCellX = m_lastMoveCellY = -99999;
}

TouchSystem::Zone TouchSystem::HitTest(float x, float y) const
{
	if (!m_bLayoutReady)
		return Zone::None;

	if (m_bUiBlocked)
		return Zone::None;

	if (Dist2(x, y, m_layout.attackCX, m_layout.attackCY) <= m_layout.attackR * m_layout.attackR)
		return Zone::Attack;

	if (Dist2(x, y, m_layout.camPadCX, m_layout.camPadCY) <= m_layout.camPadR * m_layout.camPadR)
		return Zone::CamPad;

	if (HitsNativeUi(x, y))
		return Zone::None;

	// Hit circular no pad — fora do círculo o toque segue para UI / mundo.
	const float hitR = m_layout.stickHitR > 1.f ? m_layout.stickHitR : m_layout.stickVisualR;
	if (Dist2(x, y, m_layout.stickZoneCX, m_layout.stickZoneCY) <= hitR * hitR)
		return Zone::StickZone;

	return Zone::WorldTap;
}

int TouchSystem::DrivesMove(int finger, int id)
{
	if (finger == kInvalid)
		return 0;
	// Mesmo ponteiro. Mouse (0) NÃO dirige gesto de finger — senão GetMouseState
	// congela a direção (bug Plasma / WR “não vira com o dedo”).
	if (id == finger)
		return 1;
	return 0;
}

int TouchSystem::OwnsUp(int finger, int id)
{
	if (finger == kInvalid)
		return 0;
	if (id == finger)
		return 1;
	return 0;
}

int TouchSystem::ClaimOrEcho(int& fingerSlot, int id)
{
	if (fingerSlot == kInvalid)
	{
		fingerSlot = id;
		return 1;
	}
	if (fingerSlot == 0 && id != 0)
	{
		fingerSlot = id;
		return 0;
	}
	if (fingerSlot != 0 && id == 0)
		return 0;
	if (fingerSlot == id)
		return 0;
	return 0;
}

void TouchSystem::SyncStickToActions()
{
	if (!m_stick.active)
	{
		m_actions.moveActive = 0;
		m_actions.moveX = m_actions.moveY = 0.f;
		return;
	}
	m_actions.moveActive = 1;
	m_actions.moveX = m_stick.outX;
	m_actions.moveY = m_stick.outY;
}

void TouchSystem::BeginStick(int id, float fx, float fy)
{
	const int isNew = ClaimOrEcho(m_stickFinger, id);
	m_stick.SetHome(m_layout.stickZoneCX, m_layout.stickZoneCY, m_layout.stickVisualR);
	// Sempre Fixed: pad “preso” no meio-esquerda (não Dynamic/Following flutuante).
	if (isNew || !m_stick.active)
	{
		m_stick.active = 1;
		m_stick.mode = VirtualJoystick::Mode::Fixed;
		m_stick.centerX = m_layout.stickZoneCX;
		m_stick.centerY = m_layout.stickZoneCY;
		m_stick.Update(fx, fy);
	}
	else
		m_stick.Update(fx, fy);
	SyncStickToActions();
}

void TouchSystem::PollFingerStick()
{
	if (m_stickFinger == kInvalid || m_stickFinger < 100 || !m_stick.active)
		return;
	if (!g_pDevice || g_pDevice->m_dwScreenWidth == 0 || g_pDevice->m_dwScreenHeight == 0)
		return;

	const int wantId = m_stickFinger - 100;
	const float ww = static_cast<float>(g_pDevice->m_dwScreenWidth);
	const float wh = static_cast<float>(g_pDevice->m_dwScreenHeight);

#if defined(WYD_SDL3)
	// SDL3: SDL_GetTouchDevices / SDL_GetTouchFingers
	int nDev = 0;
	SDL_TouchID* devices = SDL_GetTouchDevices(&nDev);
	if (!devices || nDev <= 0)
		return;
	for (int d = 0; d < nDev; ++d)
	{
		int nF = 0;
		SDL_Finger** fingers = SDL_GetTouchFingers(devices[d], &nF);
		if (!fingers)
			continue;
		for (int i = 0; i < nF; ++i)
		{
			SDL_Finger* f = fingers[i];
			if (!f || static_cast<int>(f->id) != wantId)
				continue;
			m_stick.Update(f->x * ww, f->y * wh);
			SyncStickToActions();
			SDL_free(fingers);
			SDL_free(devices);
			return;
		}
		SDL_free(fingers);
	}
	SDL_free(devices);
#else
	const int nDev = SDL_GetNumTouchDevices();
	for (int d = 0; d < nDev; ++d)
	{
		const SDL_TouchID tid = SDL_GetTouchDevice(d);
		const int nF = SDL_GetNumTouchFingers(tid);
		for (int i = 0; i < nF; ++i)
		{
			SDL_Finger* f = SDL_GetTouchFinger(tid, i);
			if (!f || static_cast<int>(f->id) != wantId)
				continue;
			m_stick.Update(f->x * ww, f->y * wh);
			SyncStickToActions();
			return;
		}
	}
#endif
}

int TouchSystem::OnPointerDown(int id, int x, int y)
{
	if (!m_bEnabled)
		return 0;
	if (!IsFieldActive())
	{
		m_stickFinger = m_camFinger = m_attackFinger = m_worldFinger = kInvalid;
		m_actions.moveActive = 0;
		m_actions.attackHeld = 0;
		return 0;
	}
	if (!m_bLayoutReady)
		RebuildLayout();

	const float fx = static_cast<float>(x);
	const float fy = static_cast<float>(y);

	// UI nativa primeiro (C.C., inventário, etc.) — nunca engolir com stick/world
	if (HitsNativeUi(fx, fy))
		return 0;

	const Zone z = HitTest(fx, fy);

	static int s_loggedHit = 0;
	if (!s_loggedHit || z == Zone::StickZone)
	{
		std::fprintf(stderr, "[WYD] TouchV2 down id=%d zone=%d @%.0f,%.0f stickF=%d\n",
			id, static_cast<int>(z), fx, fy, m_stickFinger);
		s_loggedHit = 1;
	}

	if (m_bUiBlocked)
		return 0;

	if (z == Zone::Attack)
	{
		if (!ClaimOrEcho(m_attackFinger, id))
			return 1;
		m_actions.attackHeld = 1;
		m_actions.attackPulse = 1;
		m_dwLastAtk = g_pTimerManager ? g_pTimerManager->GetServerTime() : 0;
		return 1;
	}

	if (z == Zone::CamPad)
	{
		const unsigned now = g_pTimerManager ? g_pTimerManager->GetServerTime() : 0;
		if (m_dwLastCamTap != 0 && now - m_dwLastCamTap <= kCamDoubleTapMs)
		{
			m_actions.camReset = 1;
			m_dwLastCamTap = 0;
			return 1;
		}
		m_dwLastCamTap = now;
		if (!ClaimOrEcho(m_camFinger, id))
			return 1;
		m_camLastX = m_camDownX = fx;
		m_camLastY = m_camDownY = fy;
		m_bCamDragging = 0;
		return 1;
	}

	if (z == Zone::StickZone)
	{
		BeginStick(id, fx, fy);
		return 1;
	}

	// Zone::None (ex.: botão C.C.) → UI nativa
	if (z == Zone::None)
		return 0;

	// Tap mundo
	if (!ClaimOrEcho(m_worldFinger, id))
		return 1;
	m_worldDownX = fx;
	m_worldDownY = fy;
	m_bWorldMoved = 0;
	return 1;
}

int TouchSystem::OnPointerMove(int id, int x, int y)
{
	if (!m_bEnabled || !IsFieldActive())
		return 0;
	const float fx = static_cast<float>(x);
	const float fy = static_cast<float>(y);

	// Stick primeiro — só o mesmo id (finger MOTION ou mouse se dono=0).
	if (DrivesMove(m_stickFinger, id))
	{
		m_stick.Update(fx, fy);
		SyncStickToActions();
		return 1;
	}

	if (DrivesMove(m_camFinger, id))
	{
		if (std::sqrt(Dist2(fx, fy, m_camDownX, m_camDownY)) > 12.f)
			m_bCamDragging = 1;
		if (m_bCamDragging)
		{
			m_actions.lookDx += fx - m_camLastX;
			m_actions.lookDy += fy - m_camLastY;
		}
		m_camLastX = fx;
		m_camLastY = fy;
		return 1;
	}

	if (DrivesMove(m_attackFinger, id))
		return 1;

	if (DrivesMove(m_worldFinger, id))
	{
		if (std::sqrt(Dist2(fx, fy, m_worldDownX, m_worldDownY)) > 22.f)
			m_bWorldMoved = 1;
		return 1;
	}

	return 0;
}

int TouchSystem::OnPointerUp(int id, int x, int y)
{
	if (!m_bEnabled || !IsFieldActive())
		return 0;
	(void)x;
	(void)y;

	int handled = 0;

	if (OwnsUp(m_worldFinger, id))
	{
		if (!m_bWorldMoved)
		{
			m_actions.interactPending = 1;
			m_actions.interactX = static_cast<int>(m_worldDownX);
			m_actions.interactY = static_cast<int>(m_worldDownY);
		}
		m_worldFinger = kInvalid;
		handled = 1;
	}

	if (OwnsUp(m_camFinger, id))
	{
		m_camFinger = kInvalid;
		m_bCamDragging = 0;
		handled = 1;
	}

	if (OwnsUp(m_attackFinger, id))
	{
		m_attackFinger = kInvalid;
		m_actions.attackHeld = 0;
		handled = 1;
	}

	if (OwnsUp(m_stickFinger, id))
	{
		m_stickFinger = kInvalid;
		m_stick.End();
		StopMoveNow();
		handled = 1;
	}

	// Mouse Up (id=0) enquanto finger ainda pressiona: consumir eco, não matar gesto.
	if (id == 0 && (m_stickFinger != kInvalid || m_camFinger != kInvalid ||
		m_attackFinger != kInvalid || m_worldFinger != kInvalid))
		return 1;

	return handled;
}

// SW-63 — gamepad. Traduz o pad para o MESMO TouchActions que o dedo produz,
// de propósito: toda a lógica de movimento/câmera/combate (histerese, repath,
// look-ahead, mira automática) já é a que funciona no celular.
void TouchSystem::FeedPad(float moveX, float moveY, float camX, float camY, float zoom,
	int attackHeld, int present)
{
	m_padMoveX = moveX;
	m_padMoveY = moveY;
	m_padCamX = camX;
	m_padCamY = camY;
	m_padZoom = zoom;
	m_padAttack = attackHeld ? 1 : 0;
	m_padPresent = present ? 1 : 0;
}

void TouchSystem::ApplyPad(unsigned int now)
{
	if (!m_padPresent)
	{
		// Pad sumiu (controle desconectado/tela modal): para o personagem em vez
		// de deixá-lo andando até o destino antigo para sempre.
		if (m_padWasPresent)
		{
			m_padWasPresent = 0;
			m_padMoveX = m_padMoveY = 0.f;
			m_actions.moveX = m_actions.moveY = 0.f;
			m_actions.moveActive = 1;	// ApplyMove verá mag<0.18 → StopMoveNow()
			m_actions.attackHeld = 0;
			m_padAttack = m_padAttackLast = 0;
		}
		return;
	}
	m_padWasPresent = 1;

	float dt = 1.f / 60.f;
	if (m_dwLastPadMs != 0 && now > m_dwLastPadMs)
	{
		dt = static_cast<float>(now - m_dwLastPadMs) * 0.001f;
		if (dt > 0.05f)
			dt = 0.05f;
	}
	m_dwLastPadMs = now;

	// Dedo no stick virtual tem prioridade (o jogador escolheu o toque agora).
	const int touchStick = (m_stickFinger != kInvalid && m_stick.active) ? 1 : 0;
	if (!touchStick)
	{
		m_actions.moveX = m_padMoveX;
		m_actions.moveY = m_padMoveY;
		m_actions.moveActive = 1;
	}

	const float camX = m_padCamX;
	const float camY = m_padCamY + m_padZoom;
	if (camX != 0.f || camY != 0.f)
	{
		// ApplyCamPad consome "pixels de dedo" (0.0049 rad por unidade no giro).
		// O pad é taxa por segundo: stick cheio ≈ 2,4 rad/s de giro.
		m_actions.lookDx += camX * 490.f * dt;
		m_actions.lookDy += camY * 60.f * dt;
	}

	if (m_padAttack)
	{
		m_actions.attackHeld = 1;
		if (!m_padAttackLast)
			m_actions.attackPulse = 1;
	}
	m_padAttackLast = m_padAttack;
}

void TouchSystem::ApplyMove(unsigned int now)
{
	if (!m_actions.moveActive)
		return;
	if (!g_pCurrentScene || !g_pCurrentScene->m_pMyHuman || !g_pObjectManager || !g_pObjectManager->m_pCamera)
		return;
	if (g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;

	const float mag = std::sqrt(m_actions.moveX * m_actions.moveX + m_actions.moveY * m_actions.moveY);
	// Dedo no centro / deadzone: parar (não continuar até o destino antigo)
	if (mag < 0.18f)
	{
		if (m_bHaveMoveAng)
			StopMoveNow();
		return;
	}

	const float ang = std::atan2(m_actions.moveY, m_actions.moveX);
	float dang = ang - m_lastMoveAng;
	if (dang > 3.14159265f) dang -= 6.2831853f;
	if (dang < -3.14159265f) dang += 6.2831853f;
	// ~24°: tremor do dedo não recorta a rota (GetRoute reseta interpolação = tela rasga).
	const int angChanged = (!m_bHaveMoveAng || std::fabs(dang) > 0.42f) ? 1 : 0;

	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	auto* me = scene->m_pMyHuman;
	if (!angChanged && m_bHaveMoveAng)
	{
		const int remain = me->m_nMaxRouteIndex - me->m_nLastRouteIndex;
		const float ndx = static_cast<float>(scene->m_vecMyNext.x) - me->m_vecPosition.x;
		const float ndy = static_cast<float>(scene->m_vecMyNext.y) - me->m_vecPosition.y;
		if (remain > 2 && (ndx * ndx + ndy * ndy) > 4.0f)
			return;
	}

	const unsigned gap = angChanged ? 90u : 200u;
	if (now - m_dwLastMove < gap)
		return;

	auto* cam = g_pObjectManager->m_pCamera;
	cam->GetCameraLookatDir();

	const float nx = m_actions.moveX / mag;
	const float ny = m_actions.moveY / mag;
	const float stickRight = nx;
	const float stickFwd = -ny;
	const float t = Clampf(mag, 0.f, 1.f);

	float fx = cam->m_vecCamDir.x;
	float fz = cam->m_vecCamDir.z;
	const float flen = std::sqrt(fx * fx + fz * fz);
	if (flen > 0.0001f) { fx /= flen; fz /= flen; }
	else { fx = 0.f; fz = 1.f; }
	const float rx = fz;
	const float rz = -fx;
	const float dx = rx * stickRight + fx * stickFwd;
	const float dy = rz * stickRight + fz * stickFwd;
	// Look-ahead médio: rota longa o bastante para não recortar a cada tile.
	// Soltar o stick ainda para na hora (IssueStopMove).
	const float worldDist = 5.0f + 5.0f * t;
	D3DXVECTOR3 pick{};
	pick.x = me->m_vecPosition.x + dx * worldDist;
	pick.z = me->m_vecPosition.y + dy * worldDist;
	pick.y = 0.f;

	const int cellX = static_cast<int>(pick.x);
	const int cellY = static_cast<int>(pick.z);
	if (!angChanged && cellX == m_lastMoveCellX && cellY == m_lastMoveCellY)
		return;

	m_dwLastMove = now;
	m_lastMoveAng = ang;
	m_bHaveMoveAng = 1;
	m_lastMoveCellX = cellX;
	m_lastMoveCellY = cellY;

	scene->IssueMoveToPick(pick, angChanged ? false : true);
}

void TouchSystem::ApplyCamPad()
{
	if (m_actions.camReset)
	{
		ResetCamera();
		m_actions.lookDx = m_actions.lookDy = 0.f;
		return;
	}
	if (m_actions.lookDx == 0.f && m_actions.lookDy == 0.f)
		return;
	if (!g_pObjectManager || !g_pObjectManager->m_pCamera)
		return;
	auto* cam = g_pObjectManager->m_pCamera;
	if (cam->m_nQuaterView || cam->m_dwSetTime != 0)
		return;

	// Horizontal: girar
	cam->m_fHorizonAngle += m_actions.lookDx * 0.0049f;
	if (cam->m_fHorizonAngle > 6.2831853f)
		cam->m_fHorizonAngle -= 6.2831853f;
	if (cam->m_fHorizonAngle < 0.f)
		cam->m_fHorizonAngle += 6.2831853f;
	cam->m_fBackHorizonAngle = cam->m_fHorizonAngle;

	// Vertical: zoom (arrastar para baixo = afastar)
	if (m_actions.lookDy != 0.f)
	{
		float len = cam->m_fSightLength + m_actions.lookDy * 0.035f;
		const float fClose = 1.2f;
		if (len < fClose)
			len = fClose;
		if (len > cam->m_fMaxCamLen)
			len = cam->m_fMaxCamLen;
		cam->m_fSightLength = len;
		cam->m_fWantLength = len;
	}

	// Inclinação leve (opcional, WR-like)
	cam->m_fVerticalAngle -= m_actions.lookDy * 0.0008f;
	if (cam->m_fVerticalAngle < -0.98539817f)
		cam->m_fVerticalAngle = -0.98539817f;
	if (cam->m_fVerticalAngle > 0.75f)
		cam->m_fVerticalAngle = 0.75f;
	cam->m_fBackVerticalAngle = cam->m_fVerticalAngle;
	cam->GetCameraLookatDir();
}

void TouchSystem::ResetCamera()
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	scene->InitCameraView();
	scene->SetCameraView();
}

TMHuman* TouchSystem::FindBestTarget(int preferPlayers) const
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return nullptr;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	if (!scene->m_pMyHuman || !scene->m_pHumanContainer)
		return nullptr;

	if (preferPlayers)
		return scene->PickPreferredPvpTarget();

	TMHuman* best = nullptr;
	float bestScore = 1.0e30f;
	const int nSX = static_cast<int>(scene->m_pMyHuman->m_vecPosition.x);
	const int nSY = static_cast<int>(scene->m_pMyHuman->m_vecPosition.y);
	const int maxDist = 12;

	for (auto* pNode = static_cast<TMHuman*>(scene->m_pHumanContainer->m_pDown);
		pNode; pNode = static_cast<TMHuman*>(pNode->m_pNextLink))
	{
		if (pNode == scene->m_pMyHuman)
			continue;
		if (pNode->m_cDeleted || pNode->m_dwDelayDel || pNode->m_cDie == 1 || pNode->m_cHide == 1)
			continue;

		if (pNode->IsMerchant())
			continue;
		if (pNode->m_bParty)
			continue;
		if (pNode->m_cSummons == 1)
			continue;
		const int isPlayer = (pNode->m_dwID > 0 && pNode->m_dwID < 1000) ? 1 : 0;
		if (isPlayer)
			continue;
		if (!scene->IsValidAutoCombatTarget(pNode))
			continue;

		const int dx = static_cast<int>(pNode->m_vecPosition.x) - nSX;
		const int dy = static_cast<int>(pNode->m_vecPosition.y) - nSY;
		const float dist = static_cast<float>(std::sqrt(static_cast<float>(dx * dx + dy * dy)));
		if (dist > static_cast<float>(maxDist))
			continue;
		if (dist < bestScore)
		{
			bestScore = dist;
			best = pNode;
		}
	}
	return best;
}

void TouchSystem::FireAttack()
{
	if (!g_pCurrentScene || !g_pDevice || !g_pTimerManager)
		return;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	if (!scene->m_pMyHuman)
		return;
	const unsigned int now = g_pTimerManager->GetServerTime();
	const int pkOn = TMFieldScene::m_bPK ? 1 : 0;
	TMHuman* target = FindBestTarget(pkOn);
	if (target)
	{
		scene->m_pTargetHuman = target;
		// Com AUTO Skill: prioriza magias elevated no alvo.
		if (scene->m_cAutoAttack == 1)
		{
			const int nTX = static_cast<int>(target->m_vecPosition.x);
			const int nTY = static_cast<int>(target->m_vecPosition.y);
			const D3DXVECTOR3 vec(
				target->m_vecPosition.x, target->m_fHeight, target->m_vecPosition.y);
			if (scene->AutoSkillUse(nTX, nTY, vec, now, 0, target) == 1)
				return;
		}
		scene->m_pMyHuman->MoveAttack(target);
		return;
	}
	// PK ligado: sem oponente válido → NÃO atacar NPC/mob/evocação (sem fallback).
	if (pkOn)
	{
		if (scene->m_pTargetHuman && !scene->IsValidPkOpponent(scene->m_pTargetHuman))
			scene->m_pTargetHuman = nullptr;
		return;
	}
	const int cx = static_cast<int>(g_pDevice->m_dwScreenWidth / 2);
	const int cy = static_cast<int>(g_pDevice->m_dwScreenHeight / 2);
	if (g_pCursor)
		g_pCursor->SetPosition(cx, cy);
	const D3DXVECTOR3 pick = scene->GroundGetPickPos();
	if (g_pObjectManager)
	{
		const int slot = g_pObjectManager->m_cSelectShortSkill;
		if (slot >= 0 && slot < 20 && g_pObjectManager->m_cShortSkill[slot] != -1)
		{
			if (scene->SkillUse(cx, cy, pick, now, 1, 0))
				return;
		}
	}
	scene->MobAttack(MK_LBUTTON, pick, now);
}

void TouchSystem::ApplyCombat(unsigned int now)
{
	if (m_actions.attackPulse ||
		(m_actions.attackHeld && now - m_dwLastAtk >= 180))
	{
		m_dwLastAtk = now;
		FireAttack();
	}

	if (m_bPkAssist && TMFieldScene::m_bPK && now - m_dwLastAssist >= 220)
	{
		if (g_pCurrentScene && g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_FIELD)
		{
			auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
			if (scene->m_pMyHuman && scene->IsValidPkOpponent(scene->m_pTargetHuman))
			{
				m_dwLastAssist = now;
				scene->m_pMyHuman->MoveAttack(scene->m_pTargetHuman);
			}
			else if (scene->m_pTargetHuman && !scene->IsValidPkOpponent(scene->m_pTargetHuman))
			{
				scene->m_pTargetHuman = nullptr;
			}
		}
	}
}

void TouchSystem::ApplyInteract()
{
	if (!m_actions.interactPending || !g_pEventTranslator || !g_pCursor)
		return;
	const int x = m_actions.interactX;
	const int y = m_actions.interactY;
	g_pCursor->SetPosition(x, y);
	g_pEventTranslator->button[0] = 1;
	g_pEventTranslator->OnMouseEvent(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
	g_pEventTranslator->button[0] = 0;
	g_pEventTranslator->OnMouseEvent(WM_LBUTTONUP, 0, x, y);
}

void TouchSystem::ApplyHotKeys()
{
	if (m_actions.hotKey < 0)
		return;
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	const int i = m_actions.hotKey;
	if (i < 0 || i >= kHotKeys)
		return;

	// Chama a cena direto (OnChar falhava com chat/IME focado).
	switch (i)
	{
	case 0: // PK (K)
		scene->SetPK();
		m_bPkAssist = TMFieldScene::m_bPK ? 1 : 0;
		if (!m_bPkAssist && scene->m_pTargetHuman && scene->m_pTargetHuman->m_dwID > 0
			&& scene->m_pTargetHuman->m_dwID < 1000)
			scene->m_pTargetHuman = nullptr;
		break;
	case 1: // Skills (S)
		if (!scene->m_pAutoTrade || !scene->m_pAutoTrade->IsVisible())
			if (!scene->m_pShopPanel || scene->m_pShopPanel->IsVisible() != 1)
				scene->SetVisibleSkill();
		break;
	case 2: // Char (C)
		if ((!scene->m_pAutoTrade || !scene->m_pAutoTrade->IsVisible()) &&
			(!scene->m_pShopPanel || !scene->m_pShopPanel->IsVisible()) &&
			(!scene->m_pTradePanel || !scene->m_pTradePanel->IsVisible()))
			scene->SetVisibleCharInfo();
		break;
	case 3: // Mapa (M)
		scene->SetVisibleMiniMap();
		break;
	case 4: // Inventário (I)
		if (!scene->m_pAutoTrade || !scene->m_pAutoTrade->IsVisible())
			scene->SetVisibleInventory();
		break;
	case 5: // Nomes (N)
		scene->SetVisibleNameLabel();
		break;
	case 6: // Auto-target (Y)
		scene->SetAutoTarget();
		break;
	case 7: // Help (H)
		if (scene->m_pHelpPanel)
		{
			const int vis = scene->m_pHelpPanel->IsVisible();
			scene->m_pHelpPanel->SetVisible(vis == 0);
			if (scene->m_pHelpBtn)
				scene->m_pHelpBtn->SetSelected(vis == 0);
		}
		break;
	default:
		break;
	}
}

void TouchSystem::Frame(unsigned int dwServerTime)
{
	if (!m_bEnabled || !g_pDevice || !g_pCurrentScene)
		return;
	if (g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;

	RebuildLayout();
	m_stick.SetHome(m_layout.stickZoneCX, m_layout.stickZoneCY, m_layout.stickVisualR);
	m_bUiBlocked = IsUiBlocked();

	// Nunca esconder a barra nativa de skills (repetição / short skill).

	PollFingerStick();
	ApplyPad(dwServerTime);

	// Fade Vita: idle ~invisível → ativo ao tocar (~150–200 ms).
	{
		const float target = (m_stickFinger != kInvalid && m_stick.active) ? 1.f : 0.f;
		unsigned int now = dwServerTime;
		if (m_dwLastStickFade == 0)
			m_dwLastStickFade = now;
		float dt = static_cast<float>(now - m_dwLastStickFade) * 0.001f;
		if (dt < 0.f)
			dt = 0.f;
		if (dt > 0.05f)
			dt = 0.05f;
		m_dwLastStickFade = now;
		const float speed = (target > m_stickFade) ? 7.5f : 5.0f; // sobe mais rápido
		const float step = speed * dt;
		if (m_stickFade < target)
		{
			m_stickFade += step;
			if (m_stickFade > target)
				m_stickFade = target;
		}
		else if (m_stickFade > target)
		{
			m_stickFade -= step;
			if (m_stickFade < target)
				m_stickFade = target;
		}
	}

	ApplyMove(dwServerTime);
	ApplyCamPad();
	if (!m_bUiBlocked)
		ApplyCombat(dwServerTime);
	// ApplyHotKeys desligado: coluna 2×4 removida — use Menu Principal (orb).
	ApplyInteract();

	m_actions.ClearEdges();
}

void TouchSystem::EnsureIcons()
{
	if (m_bIconsTried)
		return;
	m_bIconsTried = 1;
	if (!g_pDevice || !g_pDevice->m_pd3dDevice)
		return;
	const std::string exe = ExeDir();
	const char* dirs[4] = { nullptr, "./touch_icons", "/game/touch_icons", nullptr };
	std::string exeTouch;
	if (!exe.empty())
	{
		exeTouch = exe + "/touch_icons";
		dirs[0] = exeTouch.c_str();
	}
	int loaded = 0;
	for (int i = 0; i < TI_Count; ++i)
	{
		std::vector<unsigned char> bytes;
		for (const char* dir : dirs)
		{
			if (!dir) continue;
			if (!LoadFileBytes(std::string(dir) + "/" + kIconFiles[i], bytes))
				continue;
			IDirect3DTexture9* tex = nullptr;
			if (SUCCEEDED(D3DXCreateTextureFromFileInMemoryEx(
				g_pDevice->m_pd3dDevice, bytes.data(), static_cast<UINT>(bytes.size()),
				-1, -1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 1, 1,
				0, nullptr, nullptr, &tex)) && tex)
			{
				m_tex[i] = tex;
				++loaded;
				break;
			}
		}
	}
	std::fprintf(stderr, "[WYD] TouchV2 icons %d/%d (coluna funções)\n", loaded, TI_Count);
}

void TouchSystem::DrawDisc(float cx, float cy, float r, unsigned int color)
{
	if (!g_pDevice)
		return;
	g_pDevice->SetMatrixForUI();
	g_pDevice->RenderRectNoTex(cx - r, cy - r, r * 2.f, r * 2.f, color, 1);
}

void TouchSystem::DrawTex(float cx, float cy, float r, IDirect3DTexture9* tex, unsigned int color)
{
	if (!g_pDevice || !tex)
		return;
	D3DSURFACE_DESC desc{};
	if (FAILED(tex->GetLevelDesc(0, &desc)) || desc.Width == 0)
		return;
	g_pDevice->SetMatrixForUI();
	g_pDevice->RenderRectTex(
		0.f, 0.f, static_cast<float>(desc.Width), static_cast<float>(desc.Height),
		cx - r, cy - r, r * 2.f, r * 2.f, tex, color, 1, 0.f, 1.f);
}

void TouchSystem::Render()
{
	if (!m_bEnabled || !g_pDevice || !g_pCurrentScene)
		return;
	if (g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;
	if (!m_bLayoutReady)
		RebuildLayout();

	g_pDevice->SetRenderState(D3DRS_ZENABLE, 0);
	g_pDevice->SetMatrixForUI();

	// Coluna 2×4 removida — atalhos só no Menu Principal (orb HP).

	if (m_bUiBlocked)
	{
		g_pDevice->SetRenderState(D3DRS_ZENABLE, 1);
		return;
	}

	EnsureIcons();

	const int stickActive = (m_stickFinger != kInvalid && m_stick.active) ? 1 : 0;
	const float sox = stickActive ? m_stick.centerX : m_layout.stickZoneCX;
	const float soy = stickActive ? m_stick.centerY : m_layout.stickZoneCY;
	const float skx = stickActive ? m_stick.knobX : sox;
	const float sky = stickActive ? m_stick.knobY : soy;
	const float sr = m_layout.stickVisualR;
	// PS Vita ghost stick: idle ~18–28% alpha; ativo ~75–95%.
	const float fade = Clampf(m_stickFade, 0.f, 1.f);
	auto stickA = [fade](unsigned idleA, unsigned activeA) -> unsigned {
		const float a = static_cast<float>(idleA) + (static_cast<float>(activeA) - static_cast<float>(idleA)) * fade;
		return static_cast<unsigned>(a + 0.5f) & 0xFFu;
	};
	DrawDisc(sox, soy, sr, WithAlpha(0x000000u, stickA(0x2Cu, 0x88u)));       // idle ~17%, act ~53%
	DrawDisc(sox, soy, sr * 0.92f, WithAlpha(0x2A4A6Au, stickA(0x22u, 0xAAu))); // idle ~13%, act ~67%
	if (m_tex[TI_Stick])
		DrawTex(sox, soy, sr * 0.8f, m_tex[TI_Stick], WithAlpha(0xFFFFFFu, stickA(0x38u, 0xF0u)));
	DrawDisc(skx, sky, sr * 0.40f, WithAlpha(0x9EC0FFu, stickA(0x28u, 0xDDu))); // knob idle ~16%

	// CamPad: sem desenho (gesto no canto)

	const int atkActive = (m_attackFinger != kInvalid) ? 1 : 0;
	DrawDisc(m_layout.attackCX, m_layout.attackCY, m_layout.attackR,
		WithAlpha(0x000000u, atkActive ? 0x66u : 0x2Au));
	DrawDisc(m_layout.attackCX, m_layout.attackCY, m_layout.attackR * 0.90f,
		WithAlpha(0xFF5533u, atkActive ? 0x88u : 0x40u));
	if (m_tex[TI_Attack])
		DrawTex(m_layout.attackCX, m_layout.attackCY, m_layout.attackR * 0.72f, m_tex[TI_Attack],
			WithAlpha(0xFFFFFFu, atkActive ? 0xDDu : 0x77u));

	g_pDevice->SetRenderState(D3DRS_ZENABLE, 1);
}

extern "C" int WYD_TouchV2_Enabled()
{
	return (TouchSystem::Instance().IsEnabled() && TouchSystem::Instance().IsFieldActive()) ? 1 : 0;
}

extern "C" int WYD_TouchV2_HasCapture()
{
	return TouchSystem::Instance().HasCapture() ? 1 : 0;
}

extern "C" int WYD_TouchV2_WantMouseSync()
{
	return TouchSystem::Instance().WantMouseSync() ? 1 : 0;
}

extern "C" int WYD_TouchV2_Pointer(int id, int x, int y, int isDown, int isUp, int isMove)
{
	auto& t = TouchSystem::Instance();
	if (!t.IsEnabled())
		return 0;
	if (isDown)
		return t.OnPointerDown(id, x, y);
	if (isUp)
		return t.OnPointerUp(id, x, y);
	if (isMove)
		return t.OnPointerMove(id, x, y);
	return 0;
}

extern "C" int WYD_TouchV2_Finger(int id, float nx, float ny, int winW, int winH,
	int isDown, int isUp, int isMove)
{
	if (winW <= 0 || winH <= 0)
		return 0;
	int fx = static_cast<int>(nx * static_cast<float>(winW));
	int fy = static_cast<int>(ny * static_cast<float>(winH));
	if (g_pDevice && g_pDevice->m_dwScreenWidth > 0 && g_pDevice->m_dwScreenHeight > 0)
	{
		fx = static_cast<int>((long long)fx * (long long)g_pDevice->m_dwScreenWidth / winW);
		fy = static_cast<int>((long long)fy * (long long)g_pDevice->m_dwScreenHeight / winH);
	}
	return WYD_TouchV2_Pointer(id, fx, fy, isDown, isUp, isMove);
}

extern "C" void WYD_TouchV2_ForceEnable(int on)
{
	TouchSystem::ForceEnable(on);
}

extern "C" void WYD_TouchV2_Pad(float moveX, float moveY, float camX, float camY,
	float zoom, int attackHeld, int present)
{
	TouchSystem::Instance().FeedPad(moveX, moveY, camX, camY, zoom, attackHeld, present);
}

extern "C" int WYD_TouchV2_PointerWindow(int id, int wx, int wy, int winW, int winH,
	int isDown, int isUp, int isMove)
{
	int fx = wx;
	int fy = wy;
	if (winW > 0 && winH > 0 && g_pDevice && g_pDevice->m_dwScreenWidth > 0 &&
		g_pDevice->m_dwScreenHeight > 0)
	{
		fx = static_cast<int>((long long)wx * (long long)g_pDevice->m_dwScreenWidth / winW);
		fy = static_cast<int>((long long)wy * (long long)g_pDevice->m_dwScreenHeight / winH);
	}
	return WYD_TouchV2_Pointer(id, fx, fy, isDown, isUp, isMove);
}
