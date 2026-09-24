#include "pch.h"
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
#include "ResourceControl.h"
#include "Basedef.h"

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
	constexpr int kInvalidFinger = -9999;
	constexpr float kPi = 3.14159265f;
	constexpr unsigned int kLongPressMs = 420;
	constexpr float kLongPressSlop = 18.0f;

	const char* kTouchIconFiles[TI_Count] = {
		"stick.tga",
		"zoom_in.tga",
		"zoom_out.tga",
		"rotate.tga",
		"reset.tga",
		"look.tga",
		"attack.tga",
		"menu.tga",
		"toggle.tga",
		"inventory.tga",
		"skills.tga",
		"char.tga",
		"pk.tga",
	};

	float Clampf(float v, float lo, float hi)
	{
		if (v < lo)
			return lo;
		if (v > hi)
			return hi;
		return v;
	}

	float Dist2(float ax, float ay, float bx, float by)
	{
		const float dx = ax - bx;
		const float dy = ay - by;
		return dx * dx + dy * dy;
	}

	std::string ExeDir()
	{
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

TouchControlsUI& TouchControlsUI::Instance()
{
	static TouchControlsUI inst;
	return inst;
}

TouchControlsUI::TouchControlsUI()
	: m_bFeature(0)
	, m_bOverlayOn(0)
	, m_bAutoAttack(0)
	, m_bMenuOpen(0)
	, m_bCamOpen(0)
	, m_bUiBlocked(0)
	, m_bLayoutReady(0)
	, m_stickFinger(kInvalidFinger)
	, m_stickDX(0.0f)
	, m_stickDY(0.0f)
	, m_dwLastStickMove(0)
	, m_dwLastPadMove(0)
	, m_fMoveDirX(0.0f)
	, m_fMoveDirY(0.0f)
	, m_bDirMoving(false)
	, m_lookFinger(kInvalidFinger)
	, m_lookLastX(0.0f)
	, m_lookLastY(0.0f)
	, m_lookDownX(0.0f)
	, m_lookDownY(0.0f)
	, m_lookArmed(0)
	, m_attackFinger(kInvalidFinger)
	, m_dwMacroTargetID(0)
	, m_dwLastMacroTick(0)
	, m_dwLastManualMove(0)
	, m_bPickerOpen(0)
	, m_equipSlot(-1)
	, m_nLearned(0)
	, m_nGeomUsed(0)
	, m_nFontUsed(0)
	, m_bTouchIconsTried(0)
{
	std::memset(&m_layout, 0, sizeof(m_layout));
	std::memset(m_learnedSkills, 0, sizeof(m_learnedSkills));
	std::memset(m_touchTex, 0, sizeof(m_touchTex));
	for (int i = 0; i < 5; ++i)
	{
		m_skillFinger[i] = kInvalidFinger;
		m_dwSkillDownTime[i] = 0;
		m_skillDownX[i] = m_skillDownY[i] = 0.0f;
		m_bSkillFired[i] = 0;
	}
}

void TouchControlsUI::SetFeatureEnabled(int enabled)
{
	m_bFeature = enabled ? 1 : 0;
	if (!m_bFeature)
	{
		m_stickFinger = kInvalidFinger;
		m_lookFinger = kInvalidFinger;
		m_lookArmed = 0;
		m_attackFinger = kInvalidFinger;
		for (int i = 0; i < 5; ++i)
			m_skillFinger[i] = kInvalidFinger;
		m_stickDX = m_stickDY = 0.0f;
		m_bMenuOpen = 0;
		m_bCamOpen = 0;
		m_bUiBlocked = 0;
		m_bAutoAttack = 0;
		m_dwMacroTargetID = 0;
		CloseSkillPicker();
	}
	// The HUD is opt-in: the right-side Main Menu remains available, while
	// movement/attack controls stay hidden until the player enables Touch HUD.
	m_bOverlayOn = 0;
	m_bLayoutReady = 0;
}

int TouchControlsUI::ControlTexture(unsigned int controlId) const
{
	if (!g_pCurrentScene || !g_pCurrentScene->m_pControlContainer)
		return -2;
	auto* c = g_pCurrentScene->m_pControlContainer->FindControl(controlId);
	if (!c)
		return -2;
	// Botões nativos (INV/SKL/CHR/PK) são SPanel/SButton.
	auto* panel = static_cast<SPanel*>(c);
	const int tex = panel->m_GCPanel.nTextureSetIndex;
	return tex >= 0 ? tex : -2;
}

int TouchControlsUI::SkillTexture(int shortSkillValue) const
{
	const unsigned char sk = static_cast<unsigned char>(shortSkillValue);
	if (sk >= 248)
		return -2;
	// m_cShortSkill guarda nIndexTexture (ou índice compatível com o cinto).
	// Reconstrói o item de skill como UpdateSkillBelt para obter a textura.
	const int itemIndex = sk < 105 ? sk + 5000 : sk + 5295;
	if (itemIndex >= 0 && itemIndex < MAX_ITEMLIST && g_pItemList[itemIndex].nIndexTexture > 0)
		return g_pItemList[itemIndex].nIndexTexture;
	return sk > 0 ? sk : -2;
}

void TouchControlsUI::RebuildLayout()
{
	if (!g_pDevice)
		return;

	const float W = static_cast<float>(g_pDevice->m_dwScreenWidth);
	const float H = static_cast<float>(g_pDevice->m_dwScreenHeight);
	const float s = (W < H ? W : H);

	// Wild Rift: stick discreto canto inferior esquerdo
	m_layout.stickR = s * 0.105f;
	m_layout.stickCX = W * 0.14f;
	m_layout.stickCY = H * 0.80f;

	// Look: zona compacta na borda direita — exige arrasto para capturar (não engole tap em mob).
	m_layout.lookR = s * 0.07f;
	m_layout.lookCX = W * 0.90f;
	m_layout.lookCY = H * 0.48f;

	// ATK grande + arco de skills (hierarquia WR)
	m_layout.attackR = s * 0.078f;
	m_layout.attackCX = W * 0.89f;
	m_layout.attackCY = H * 0.82f;

	m_layout.skillR = s * 0.052f;
	const float arcCX = m_layout.attackCX - s * 0.015f;
	const float arcCY = m_layout.attackCY - s * 0.015f;
	const float arcR = s * 0.175f;
	for (int i = 0; i < 5; ++i)
	{
		const float t = (-115.0f + 38.0f * static_cast<float>(i)) * (kPi / 180.0f);
		m_layout.skillCX[i] = arcCX + std::cos(t) * arcR;
		m_layout.skillCY[i] = arcCY + std::sin(t) * arcR;
	}

	// Utilitários pequenos canto superior direito: CAM | MENU | TOGGLE
	m_layout.toggleR = s * 0.028f;
	m_layout.toggleCX = W - m_layout.toggleR - 8.0f;
	m_layout.toggleCY = m_layout.toggleR + 8.0f;

	m_layout.menuR = m_layout.toggleR;
	m_layout.menuCX = m_layout.toggleCX - m_layout.toggleR * 2.35f;
	m_layout.menuCY = m_layout.toggleCY;

	m_layout.camBtnR = m_layout.toggleR;
	m_layout.camBtnCX = m_layout.menuCX - m_layout.menuR * 2.35f;
	m_layout.camBtnCY = m_layout.toggleCY;

	// Main Menu: painel vertical ancorado à direita. A versão anterior
	// espalhava seis círculos em uma linha; em resoluções 16:9 eles ficavam
	// sobrepostos ao HUD e os rótulos desapareciam fora da área útil.
	m_layout.subR = s * 0.030f;
	const float menuRowGap = m_layout.subR * 2.35f;
	for (int i = 0; i < 6; ++i)
	{
		m_layout.subCX[i] = W - m_layout.subR - 18.0f;
		m_layout.subCY[i] = m_layout.menuCY + m_layout.subR * 3.0f + menuRowGap * static_cast<float>(i);
	}

	// Câmera submenu: botões minúsculos em linha à esquerda do CAM
	m_layout.camR = s * 0.026f;
	const float camGap = m_layout.camR * 2.15f;
	for (int i = 0; i < 4; ++i)
	{
		m_layout.camCX[i] = m_layout.camBtnCX - (m_layout.camBtnR + 6.0f) - camGap * static_cast<float>(i + 1);
		m_layout.camCY[i] = m_layout.camBtnCY;
	}

	m_layout.pickerCols = 6.0f;
	m_layout.pickerRows = 4.0f;
	m_layout.pickerCell = s * 0.07f;
	m_layout.pickerCX = W * 0.50f;
	m_layout.pickerCY = H * 0.42f;

	m_bLayoutReady = 1;
}

TouchControlsUI::Zone TouchControlsUI::HitTest(float x, float y) const
{
	if (!m_bLayoutReady)
		return Zone::None;

	if (Dist2(x, y, m_layout.toggleCX, m_layout.toggleCY) <= m_layout.toggleR * m_layout.toggleR)
		return Zone::Toggle;

	// Main Menu is always available on the right, even with Touch HUD disabled.
	if (Dist2(x, y, m_layout.menuCX, m_layout.menuCY) <= m_layout.menuR * m_layout.menuR)
		return Zone::Menu;

	if (!m_bOverlayOn)
		return Zone::None;

	// Painéis nativos (loja/trade/etc.) precisam do mouse; não engolir toques do HUD.
	if (m_bUiBlocked)
		return Zone::None;

	if (m_bPickerOpen)
	{
		const int pi = PickerHitIndex(x, y);
		if (pi >= 0)
			return static_cast<Zone>(static_cast<int>(Zone::Picker0) + pi);
		// Qualquer toque no picker consome (fecha se fora das células no Up).
		return Zone::PickerBack;
	}

	if (Dist2(x, y, m_layout.camBtnCX, m_layout.camBtnCY) <= m_layout.camBtnR * m_layout.camBtnR)
		return Zone::CamMenu;

	// Submenus abertos têm prioridade de hit
	if (m_bMenuOpen)
	{
		for (int i = 0; i < 6; ++i)
		{
			if (Dist2(x, y, m_layout.subCX[i], m_layout.subCY[i]) <= m_layout.subR * m_layout.subR)
				return static_cast<Zone>(static_cast<int>(Zone::BtnInv) + i);
		}
	}

	if (m_bCamOpen)
	{
		for (int i = 0; i < 4; ++i)
		{
			if (Dist2(x, y, m_layout.camCX[i], m_layout.camCY[i]) <= m_layout.camR * m_layout.camR)
				return static_cast<Zone>(static_cast<int>(Zone::ZoomIn) + i);
		}
	}

	// The left movement pad was intentionally removed. World movement remains
	// available through the native game controls; this HUD reserves no opaque
	// touch surface on the left side.
	if (Dist2(x, y, m_layout.attackCX, m_layout.attackCY) <= m_layout.attackR * m_layout.attackR)
		return Zone::Attack;

	for (int i = 0; i < 5; ++i)
	{
		if (Dist2(x, y, m_layout.skillCX[i], m_layout.skillCY[i]) <= m_layout.skillR * m_layout.skillR)
			return static_cast<Zone>(static_cast<int>(Zone::Skill0) + i);
	}

	if (Dist2(x, y, m_layout.lookCX, m_layout.lookCY) <= m_layout.lookR * m_layout.lookR)
		return Zone::Look;

	return Zone::None;
}

int TouchControlsUI::PickerHitIndex(float x, float y) const
{
	if (!m_bPickerOpen || m_nLearned <= 0)
		return -1;
	const float cell = m_layout.pickerCell;
	const int cols = static_cast<int>(m_layout.pickerCols);
	const float gridW = cols * cell;
	const int rows = (m_nLearned + cols - 1) / cols;
	const float gridH = rows * cell;
	const float left = m_layout.pickerCX - gridW * 0.5f;
	const float top = m_layout.pickerCY - gridH * 0.5f;
	if (x < left || y < top || x >= left + gridW || y >= top + gridH)
		return -1;
	const int col = static_cast<int>((x - left) / cell);
	const int row = static_cast<int>((y - top) / cell);
	const int idx = row * cols + col;
	if (idx < 0 || idx >= m_nLearned)
		return -1;
	return idx;
}

void TouchControlsUI::DrawDisc(float cx, float cy, float r, unsigned int color, int layer)
{
	if (m_nGeomUsed >= kMaxGeom || !g_pCurrentScene || !g_pCurrentScene->m_pControlContainer)
		return;

	GeomControl& g = m_geoms[m_nGeomUsed++];
	g = GeomControl(RENDERCTRLTYPE::RENDER_IMAGE_STRETCH, -2,
		cx - r, cy - r, r * 2.0f, r * 2.0f, layer, color);
	g.bVisible = 1;
	AddRenderControlItem(g_pCurrentScene->m_pControlContainer->m_pDrawControl, &g, layer);
}

void TouchControlsUI::DrawIcon(float cx, float cy, float r, int textureSet, unsigned int color, int layer)
{
	if (textureSet < 0)
	{
		DrawDisc(cx, cy, r, color, layer);
		return;
	}
	if (m_nGeomUsed >= kMaxGeom || !g_pCurrentScene || !g_pCurrentScene->m_pControlContainer)
		return;

	GeomControl& g = m_geoms[m_nGeomUsed++];
	g = GeomControl(RENDERCTRLTYPE::RENDER_IMAGE_STRETCH, textureSet,
		cx - r, cy - r, r * 2.0f, r * 2.0f, layer, color);
	g.bVisible = 1;
	AddRenderControlItem(g_pCurrentScene->m_pControlContainer->m_pDrawControl, &g, layer);
}

void TouchControlsUI::EnsureTouchIconsLoaded()
{
	if (m_bTouchIconsTried)
		return;
	m_bTouchIconsTried = 1;
	if (!g_pDevice || !g_pDevice->m_pd3dDevice)
		return;

	const std::string exe = ExeDir();
	const char* candidates[] = {
		nullptr, // filled with exe/touch_icons
		"./touch_icons",
		"/game/touch_icons",
		"/home/user/client748/touch_icons",
	};
	std::string exeTouch;
	if (!exe.empty())
	{
		exeTouch = exe + "/touch_icons";
		candidates[0] = exeTouch.c_str();
	}

	int loaded = 0;
	for (int i = 0; i < TI_Count; ++i)
	{
		std::vector<unsigned char> bytes;
		for (const char* dir : candidates)
		{
			if (!dir || !dir[0])
				continue;
			const std::string path = std::string(dir) + "/" + kTouchIconFiles[i];
			if (!LoadFileBytes(path, bytes))
				continue;
			IDirect3DTexture9* tex = nullptr;
			const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
				g_pDevice->m_pd3dDevice,
				bytes.data(),
				static_cast<UINT>(bytes.size()),
				-1, -1, 1, 0,
				D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
				1, 1,
				0, nullptr, nullptr, &tex);
			if (SUCCEEDED(hr) && tex)
			{
				m_touchTex[i] = tex;
				++loaded;
				break;
			}
		}
	}
	std::fprintf(stderr, "[WYDLINUX] touch_icons loaded %d/%d\n", loaded, TI_Count);
}

IDirect3DTexture9* TouchControlsUI::GetTouchIconTexture(int iconId)
{
	EnsureTouchIconsLoaded();
	if (iconId < 0 || iconId >= TI_Count)
		return nullptr;
	return m_touchTex[iconId];
}

void TouchControlsUI::DrawTouchIcon(float cx, float cy, float r, int iconId, unsigned int color, int layer)
{
	EnsureTouchIconsLoaded();
	if (iconId < 0 || iconId >= TI_Count || !m_touchTex[iconId])
	{
		DrawDisc(cx, cy, r, color, layer);
		return;
	}
	if (m_nGeomUsed >= kMaxGeom || !g_pCurrentScene || !g_pCurrentScene->m_pControlContainer)
		return;

	GeomControl& g = m_geoms[m_nGeomUsed++];
	g = GeomControl(RENDERCTRLTYPE::RENDER_IMAGE_STRETCH, kTouchCustomTexSet,
		cx - r, cy - r, r * 2.0f, r * 2.0f, layer, color);
	g.nTextureIndex = iconId;
	g.bVisible = 1;
	AddRenderControlItem(g_pCurrentScene->m_pControlContainer->m_pDrawControl, &g, layer);
}

void WYD_Touch_RenderCustomIcon(RenderDevice* rd, GeomControl* c)
{
	if (!rd || !c)
		return;
	IDirect3DTexture9* tex = TouchControlsUI::Instance().GetTouchIconTexture(c->nTextureIndex);
	if (!tex)
		return;
	D3DSURFACE_DESC desc {};
	if (FAILED(tex->GetLevelDesc(0, &desc)) || desc.Width == 0 || desc.Height == 0)
		return;
	rd->RenderRectTex(
		0.0f, 0.0f,
		static_cast<float>(desc.Width), static_cast<float>(desc.Height),
		c->nPosX, c->nPosY, c->nWidth, c->nHeight,
		tex, c->dwColor, 1, c->fAngle, 1.0f);
}

void TouchControlsUI::DrawLabel(float cx, float cy, float w, float h, const char* text, unsigned int color, int layer)
{
	if (m_nGeomUsed >= kMaxGeom || m_nFontUsed >= 36 || !text || !g_pCurrentScene ||
		!g_pCurrentScene->m_pControlContainer)
		return;

	if (m_nFontUsed < 35 && m_nGeomUsed + 1 < kMaxGeom)
	{
		TMFont2& shadow = m_fonts[m_nFontUsed++];
		shadow.SetText(text, 0xFF000000u, 0);
		GeomControl& gs = m_geoms[m_nGeomUsed++];
		gs = GeomControl(RENDERCTRLTYPE::RENDER_TEXT, -1, cx - w * 0.5f + 1.0f, cy - h * 0.5f + 1.0f, w, h, layer, 0xFF000000u);
		gs.pFont = &shadow;
		std::strncpy(gs.strString, text, sizeof(gs.strString) - 1);
		gs.strString[sizeof(gs.strString) - 1] = 0;
		gs.bVisible = 1;
		AddRenderControlItem(g_pCurrentScene->m_pControlContainer->m_pDrawControl, &gs, layer);
	}

	TMFont2& font = m_fonts[m_nFontUsed++];
	font.SetText(text, color, 0);

	GeomControl& g = m_geoms[m_nGeomUsed++];
	g = GeomControl(RENDERCTRLTYPE::RENDER_TEXT, -1, cx - w * 0.5f, cy - h * 0.5f, w, h, layer, color);
	g.pFont = &font;
	std::strncpy(g.strString, text, sizeof(g.strString) - 1);
	g.strString[sizeof(g.strString) - 1] = 0;
	g.bVisible = 1;
	AddRenderControlItem(g_pCurrentScene->m_pControlContainer->m_pDrawControl, &g, layer);
}

void TouchControlsUI::DrawIconButton(float cx, float cy, float r, int textureSet, const char* fallback,
	unsigned int tint, int layer)
{
	DrawDisc(cx, cy, r, 0xF0000000u, layer);
	if (textureSet >= 0)
	{
		DrawDisc(cx, cy, r * 0.92f, tint, layer);
		DrawIcon(cx, cy, r * 0.78f, textureSet, 0xFFFFFFFFu, layer + 1);
	}
	else
	{
		DrawDisc(cx, cy, r * 0.88f, tint, layer);
		if (fallback && fallback[0])
			DrawLabel(cx, cy, r * 2.4f, std::max(14.0f, r * 0.8f), fallback, 0xFFFFFFFFu, layer + 1);
	}
}

void TouchControlsUI::DrawTouchIconButton(float cx, float cy, float r, int iconId, const char* fallback,
	unsigned int tint, int layer)
{
	EnsureTouchIconsLoaded();
	if (iconId >= 0 && iconId < TI_Count && m_touchTex[iconId])
	{
		// Ícones já trazem moldura escura — só o bitmap.
		DrawTouchIcon(cx, cy, r, iconId, 0xFFFFFFFFu, layer);
		return;
	}
	DrawIconButton(cx, cy, r, -2, fallback, tint, layer);
}

int TouchControlsUI::FirstValidTexture(unsigned int a, unsigned int b) const
{
	const int ta = ControlTexture(a);
	if (ta >= 0)
		return ta;
	if (b != 0)
	{
		const int tb = ControlTexture(b);
		if (tb >= 0)
			return tb;
	}
	return -2;
}

int TouchControlsUI::IsGameUiBlocking() const
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;

	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	auto visible = [](const SPanel* p) -> int {
		return (p && p->IsVisible()) ? 1 : 0;
	};

	if (visible(scene->m_pShopPanel))
		return 1;
	if (visible(scene->m_pTradePanel))
		return 1;
	if (visible(scene->m_pAutoTrade))
		return 1;
	if (visible(scene->m_pCargoPanel) || visible(scene->m_pCargoPanel1))
		return 1;
	if (visible(scene->m_pInvenPanel))
		return 1;
	if (visible(scene->m_pSkillPanel))
		return 1;
	if (visible(scene->m_pCPanel))
		return 1;
	if (visible(scene->m_pSystemPanel))
		return 1;
	if (visible(scene->m_pPGTPanel))
		return 1;
	if (scene->m_pMessageBox && scene->m_pMessageBox->IsVisible())
		return 1;
	if (scene->m_pMessageBox2 && scene->m_pMessageBox2->IsVisible())
		return 1;

	if (auto* cc = scene->m_pControlContainer)
	{
		for (int i = 0; i < 8; ++i)
		{
			SControl* modal = cc->m_pModalControl[i];
			if (modal && modal->m_bVisible == 1 && modal->m_bModal == 1)
				return 1;
		}
	}

	if (g_pCursor && g_pCursor->m_pAttachedItem)
		return 1;

	return 0;
}

void TouchControlsUI::ResetTransientInput()
{
	m_stickFinger = kInvalidFinger;
	m_lookFinger = kInvalidFinger;
	m_lookArmed = 0;
	m_attackFinger = kInvalidFinger;
	for (int i = 0; i < 5; ++i)
	{
		m_skillFinger[i] = kInvalidFinger;
		m_bSkillFired[i] = 0;
	}
	m_stickDX = m_stickDY = 0.0f;
	m_bMenuOpen = 0;
	m_bCamOpen = 0;
	CloseSkillPicker();
}

void TouchControlsUI::FrameMove(unsigned int dwServerTime)
{
	if (!m_bFeature || !g_pDevice || !g_pCurrentScene || !g_pCurrentScene->m_pControlContainer)
		return;
	if (g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;

	RebuildLayout();
	m_nGeomUsed = 0;
	m_nFontUsed = 0;

	const int blocked = IsGameUiBlocking();
	if (blocked && !m_bUiBlocked)
		ResetTransientInput();
	else if (!blocked && m_bUiBlocked)
		ResetTransientInput();
	m_bUiBlocked = blocked;

	const float labelH = std::max(16.0f, m_layout.toggleR * 0.8f);

	EnsureTouchIconsLoaded();

	// Touch is enabled from the right-side Main Menu, never from a second
	// floating toggle. This keeps the game view clean while the feature is off.
	if (m_bOverlayOn || m_bMenuOpen)
	{
		if (m_touchTex[TI_Toggle])
			DrawTouchIcon(m_layout.toggleCX, m_layout.toggleCY, m_layout.toggleR, TI_Toggle, 0xFFFFFFFFu, 28);
		else
		{
			DrawDisc(m_layout.toggleCX, m_layout.toggleCY, m_layout.toggleR, 0xEE000000u, 27);
			DrawDisc(m_layout.toggleCX, m_layout.toggleCY, m_layout.toggleR * 0.88f,
				m_bOverlayOn ? 0xF0FFAA33u : 0xE0666677u, 28);
			DrawLabel(m_layout.toggleCX, m_layout.toggleCY, m_layout.toggleR * 2.4f, labelH,
				m_bOverlayOn ? "ON" : "OFF", 0xFFFFFFFFu, 28);
		}
	}

	// The right-side Main Menu is the only persistent affordance. It remains
	// visible while the HUD is disabled and exposes Touch/Auto options.
	DrawTouchIconButton(m_layout.menuCX, m_layout.menuCY, m_layout.menuR, TI_Menu, "MENU",
		m_bMenuOpen ? 0xFF3A7AB0u : 0xFF445566u, 30);

	// Com painel modal/loja aberto: só o menu; toques vão ao UI nativo.
	if (!m_bOverlayOn || m_bUiBlocked)
		return;

	// No left movement pad: the area remains completely transparent and passes
	// through to the native game input.
	/*if (m_touchTex[TI_Stick])
		DrawTouchIcon(m_layout.stickCX, m_layout.stickCY, m_layout.stickR, TI_Stick, 0xC0FFFFFFu, 26);
	else
	{
		DrawDisc(m_layout.stickCX, m_layout.stickCY, m_layout.stickR, 0x88111118u, 26);
		DrawDisc(m_layout.stickCX, m_layout.stickCY, m_layout.stickR * 0.92f, 0x663A3A55u, 26);
	}
	DrawDisc(m_layout.stickCX + m_stickDX, m_layout.stickCY + m_stickDY,
		m_layout.stickR * 0.38f, 0xE09EC0FFu, 27);*/

	// Look: halo bem fraco (zona de arraste), sem ícone grande
	DrawDisc(m_layout.lookCX, m_layout.lookCY, m_layout.lookR,
		m_lookFinger != kInvalidFinger ? 0x33224455u : 0x18182028u, 25);

	DrawTouchIconButton(m_layout.attackCX, m_layout.attackCY, m_layout.attackR, TI_Attack, "ATK",
		0xFFFF5533u, 28);

	for (int i = 0; i < 5; ++i)
	{
		const int selected = (g_pObjectManager && g_pObjectManager->m_cSelectShortSkill == i);
		const unsigned int ring = (m_bPickerOpen && m_equipSlot == i) ? 0xFF66FFAAu :
			(selected ? 0xFFFFDD66u : 0xCC000000u);
		DrawDisc(m_layout.skillCX[i], m_layout.skillCY[i], m_layout.skillR, ring, 27);

		int tex = -2;
		if (g_pObjectManager)
			tex = SkillTexture(g_pObjectManager->m_cShortSkill[i]);
		if (tex > 0)
			DrawIcon(m_layout.skillCX[i], m_layout.skillCY[i], m_layout.skillR * 0.82f, tex, 0xFFFFFFFFu, 28);
		else
			DrawDisc(m_layout.skillCX[i], m_layout.skillCY[i], m_layout.skillR * 0.86f,
				selected ? 0xFF6688CCu : 0xAA2A2A44u, 28);
	}

	// Utilitários pequenos: CAM + MENU (mesmo tamanho do toggle)
	DrawTouchIconButton(m_layout.camBtnCX, m_layout.camBtnCY, m_layout.camBtnR, TI_Rotate, "CAM",
		m_bCamOpen ? 0xFF3A7AB0u : 0xFF3A4A5Au, 30);

	if (m_bCamOpen)
	{
		static const int kCamIcons[4] = { TI_ZoomIn, TI_ZoomOut, TI_Rotate, TI_Reset };
		static const char* kCamFb[4] = { "Z+", "Z-", "ROT", "RST" };
		static const unsigned int kCamTint[4] = { 0xFF2E6B4Eu, 0xFF2E6B4Eu, 0xFF3A5A7Au, 0xFF7A5A3Au };
		for (int i = 0; i < 4; ++i)
			DrawTouchIconButton(m_layout.camCX[i], m_layout.camCY[i], m_layout.camR,
				kCamIcons[i], kCamFb[i], kCamTint[i], 31);
	}

	if (m_bMenuOpen)
	{
		struct SubBtn { const char* label; int icon; unsigned int idA; unsigned int idB; unsigned int tint; };
		const SubBtn subs[6] = {
			{ "INV", TI_Inv, B_EQUIP, TMB_EQUIP, 0xFF3D7A45u },
			{ "SKL", TI_Skills, 65792u, TMB_SKILL, 0xFF3D5A8Au },
			{ "CHR", TI_Char, B_CHAR, TMB_CHAR, 0xFF8A6A3Du },
			{ "PK",  TI_Pk, B_PK_BTN_C, TMB_PKBTN, TMFieldScene::m_bPK ? 0xFFB03333u : 0xFF555555u },
			{ "TOUCH", TI_Toggle, 0u, 0u, m_bOverlayOn ? 0xFF2E8B57u : 0xFF555555u },
			{ "AUTO", TI_Attack, 0u, 0u, m_bAutoAttack ? 0xFFB03333u : 0xFF555555u },
		};
		// Cabeçalho e rótulos são desenhados separadamente dos ícones. Os
		// bitmaps de touch normalmente existem e, nesse caso, o fallback textual
		// nunca era executado; por isso o usuário só via símbolos sem saber o que
		// cada opção fazia.
		DrawLabel(m_layout.subCX[0] - m_layout.subR * 1.8f,
			m_layout.menuCY + m_layout.subR * 1.45f, m_layout.subR * 3.8f,
			m_layout.subR * 0.95f, "MAIN MENU", 0xFFFFFFFFu, 31);
		for (int i = 0; i < 6; ++i)
		{
			// INV/SKL usam arte custom; CHR/PK preferem ícone nativo do jogo.
			const int preferCustom = (subs[i].icon == TI_Inv || subs[i].icon == TI_Skills);
			if (preferCustom && m_touchTex[subs[i].icon])
			{
				DrawTouchIconButton(m_layout.subCX[i], m_layout.subCY[i], m_layout.subR, subs[i].icon,
					subs[i].label, subs[i].tint, 31);
			}
			else
			{
				const int tex = FirstValidTexture(subs[i].idA, subs[i].idB);
				if (tex >= 0)
					DrawIconButton(m_layout.subCX[i], m_layout.subCY[i], m_layout.subR, tex,
						subs[i].label, subs[i].tint, 31);
				else
					DrawTouchIconButton(m_layout.subCX[i], m_layout.subCY[i], m_layout.subR, subs[i].icon,
						subs[i].label, subs[i].tint, 31);
			}
			DrawLabel(m_layout.subCX[i] - m_layout.subR * 2.0f, m_layout.subCY[i],
				m_layout.subR * 3.2f, m_layout.subR * 0.9f, subs[i].label,
				subs[i].tint | 0xFF000000u, 32);
		}
	}

	if (m_bPickerOpen)
	{
		const float cell = m_layout.pickerCell;
		const int cols = static_cast<int>(m_layout.pickerCols);
		const int rows = std::max(1, (m_nLearned + cols - 1) / cols);
		const float gridW = cols * cell;
		const float gridH = rows * cell;
		DrawDisc(m_layout.pickerCX, m_layout.pickerCY,
			std::max(gridW, gridH) * 0.55f + 12.0f, 0xDD000000u, 32);
		DrawLabel(m_layout.pickerCX, m_layout.pickerCY - gridH * 0.5f - 18.0f,
			gridW, labelH * 1.2f, "EQUIPAR SKILL", 0xFFFFEE88u, 33);

		const float left = m_layout.pickerCX - gridW * 0.5f;
		const float top = m_layout.pickerCY - gridH * 0.5f;
		for (int i = 0; i < m_nLearned; ++i)
		{
			const int col = i % cols;
			const int row = i / cols;
			const float cx = left + (col + 0.5f) * cell;
			const float cy = top + (row + 0.5f) * cell;
			const int skillId = m_learnedSkills[i];
			const int itemIndex = skillId < 105 ? skillId + 5000 : skillId + 5295;
			int tex = -2;
			if (itemIndex >= 0 && itemIndex < MAX_ITEMLIST)
				tex = g_pItemList[itemIndex].nIndexTexture;
			DrawDisc(cx, cy, cell * 0.42f, 0xEE111122u, 33);
			if (tex > 0)
				DrawIcon(cx, cy, cell * 0.36f, tex, 0xFFFFFFFFu, 33);
			else
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "%d", skillId);
				DrawLabel(cx, cy, cell * 0.9f, labelH, buf, 0xFFFFFFFFu, 33);
			}
		}
	}

	if (m_stickFinger != kInvalidFinger)
		IssueStickMove(dwServerTime);
	if (m_bAutoAttack)
		AutoAttackTick(dwServerTime);
}

void TouchControlsUI::IssueStickMove(unsigned int dwServerTime)
{
	if (!g_pCurrentScene || !g_pCurrentScene->m_pMyHuman || !g_pObjectManager || !g_pObjectManager->m_pCamera)
		return;
	if (m_layout.stickR <= 1.0f)
		return;

	const float mag = std::sqrt(m_stickDX * m_stickDX + m_stickDY * m_stickDY);
	if (mag < m_layout.stickR * 0.18f)
		return;

	if (dwServerTime - m_dwLastStickMove < 100)
		return;
	m_dwLastStickMove = dwServerTime;
	m_dwLastManualMove = dwServerTime;

	IssueDirMove(m_stickDX / mag, m_stickDY / mag);
}

int TouchControlsUI::PadMove(float nx, float ny, unsigned int dwServerTime)
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD ||
		!g_pCurrentScene->m_pMyHuman || !g_pObjectManager || !g_pObjectManager->m_pCamera)
		return 0;
	const float mag = std::sqrt(nx * nx + ny * ny);
	if (mag < 0.2f)
		return 0;
	if (dwServerTime - m_dwLastPadMove < 100)
		return 1;
	m_dwLastPadMove = dwServerTime;
	m_dwLastManualMove = dwServerTime;
	IssueDirMove(nx / mag, ny / mag);
	return 1;
}

int TouchControlsUI::PadAction(int action)
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	switch (action)
	{
	case PA_Inventory:  ToggleMenuPanel(0); break;
	case PA_SkillPanel: ToggleMenuPanel(1); break;
	case PA_CharInfo:   ToggleMenuPanel(2); break;
	case PA_Attack:     FireAttack(); break;
	case PA_AutoAttack: ToggleAutoAttack(); break;
	case PA_Skill0: case PA_Skill1: case PA_Skill2: case PA_Skill3: case PA_Skill4:
		FireSkill(action - PA_Skill0);
		break;
	case PA_ZoomIn:     ApplyZoom(-1); break;
	case PA_ZoomOut:    ApplyZoom(1); break;
	case PA_CamRotate:  RotateCameraStep(); break;
	case PA_CamReset:   ResetCamera(); break;
	default: return 0;
	}
	return 1;
}

void TouchControlsUI::IssueDirMove(float nx, float ny)
{
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	auto* cam = g_pObjectManager->m_pCamera;
	cam->GetCameraLookatDir();
	if (m_bAutoAttack)
		scene->m_pTargetHuman = nullptr;

	// Direção normalizada: +X = direita na tela, -Y = cima na tela.
	const float stickRight = nx;
	const float stickFwd = -ny; // cima do stick = "para dentro" da tela

	// Direção da câmera no chão (WYD: human.x/y ↔ cam XZ).
	float fx = cam->m_vecCamDir.x;
	float fz = cam->m_vecCamDir.z;
	const float flen = std::sqrt(fx * fx + fz * fz);
	if (flen > 0.0001f)
	{
		fx /= flen;
		fz /= flen;
	}
	else
	{
		fx = 0.0f;
		fz = 1.0f;
	}
	// Right = cross(up, forward) em LH: (fz, -fx)
	const float rx = fz;
	const float rz = -fx;

	const float dx = rx * stickRight + fx * stickFwd;
	const float dy = rz * stickRight + fz * stickFwd;

	m_fMoveDirX = dx;
	m_fMoveDirY = dy;
	m_bDirMoving = true;
	scene->IssueStickStep(dx, dy, false);
}

void TouchControlsUI::PadStop()
{
	if (!m_bDirMoving)
		return;
	m_bDirMoving = false;
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD ||
		!g_pCurrentScene->m_pMyHuman)
		return;
	static_cast<TMFieldScene*>(g_pCurrentScene)->IssueStickStep(m_fMoveDirX, m_fMoveDirY, true);
}

void TouchControlsUI::ApplyLookDelta(float dx, float dy)
{
	if (!g_pObjectManager || !g_pObjectManager->m_pCamera)
		return;
	auto* cam = g_pObjectManager->m_pCamera;
	if (cam->m_nQuaterView || cam->m_dwSetTime != 0)
		return;

	cam->m_fHorizonAngle += dx * 0.0049f;
	if (cam->m_fHorizonAngle > 6.2831853f)
		cam->m_fHorizonAngle -= 6.2831853f;
	if (cam->m_fHorizonAngle < 0.0f)
		cam->m_fHorizonAngle += 6.2831853f;

	cam->m_fVerticalAngle -= dy * 0.002f;
	if (cam->m_fVerticalAngle < -0.98539817f)
		cam->m_fVerticalAngle = -0.98539817f;
	if (cam->m_fVerticalAngle > 0.75f)
		cam->m_fVerticalAngle = 0.75f;

	cam->m_fBackHorizonAngle = cam->m_fHorizonAngle;
	cam->m_fBackVerticalAngle = cam->m_fVerticalAngle;
}

void TouchControlsUI::ApplyZoom(int dir)
{
	if (!g_pObjectManager || !g_pObjectManager->m_pCamera)
		return;
	auto* cam = g_pObjectManager->m_pCamera;
	const float step = 1.2f * static_cast<float>(dir);
	float len = cam->m_fSightLength + step;
	float fClose = 1.2f;
	if (g_pCurrentScene && g_pCurrentScene->m_pMyHuman)
	{
		if (g_pCurrentScene->m_pMyHuman->m_cMount == 1)
			fClose = 2.5f;
		fClose += static_cast<float>(g_pCurrentScene->m_pMyHuman->m_stScore.Con) * 0.00019f;
	}
	if (len < fClose)
		len = fClose;
	if (len > cam->m_fMaxCamLen)
		len = cam->m_fMaxCamLen;
	cam->m_fSightLength = len;
	cam->m_fWantLength = len;
}

void TouchControlsUI::ResetCamera()
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	scene->InitCameraView();
	scene->SetCameraView();
}

void TouchControlsUI::RotateCameraStep()
{
	if (!g_pObjectManager || !g_pObjectManager->m_pCamera)
		return;
	auto* cam = g_pObjectManager->m_pCamera;
	if (cam->m_nQuaterView || cam->m_dwSetTime != 0)
		return;
	// 45° por toque — útil e previsível no celular.
	cam->m_fHorizonAngle += 0.78539819f;
	if (cam->m_fHorizonAngle > 6.2831853f)
		cam->m_fHorizonAngle -= 6.2831853f;
	cam->m_fBackHorizonAngle = cam->m_fHorizonAngle;
	cam->GetCameraLookatDir();
}

void TouchControlsUI::FireAttack()
{
	if (!g_pCurrentScene || !g_pDevice || !g_pTimerManager)
		return;

	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	const int cx = static_cast<int>(g_pDevice->m_dwScreenWidth / 2);
	const int cy = static_cast<int>(g_pDevice->m_dwScreenHeight / 2);
	const unsigned int now = g_pTimerManager->GetServerTime();
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

void TouchControlsUI::SetAutoAttackOn(int on, int notify)
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	const int want = on ? 1 : 0;
	if (m_bAutoAttack == want)
		return;
	m_bAutoAttack = want;
	m_dwMacroTargetID = 0;
	// Ataque/perseguição ficam no auto-ataque nativo (m_cAutoAttack); o macro escolhe o alvo.
	if ((scene->m_cAutoAttack != 0) != (m_bAutoAttack != 0))
		scene->SetAutoTarget();
	if (m_bAutoAttack && scene->m_pTargetHuman && scene->CanAttackHuman(scene->m_pTargetHuman))
		m_dwMacroTargetID = scene->m_pTargetHuman->m_dwID;

	if (notify && scene->m_pChatList)
	{
		const char* text = !m_bAutoAttack ? "Macro desativado"
			: TMFieldScene::m_bPK ? "Macro ATIVO (PK: jogadores e monstros)"
			: "Macro ATIVO (monstros)";
		scene->m_pChatList->AddItem(new SListBoxItem(text, 0xFFFFAAAA, 0.0f, 0.0f, 300.0f, 16.0f, 0, 0x77777777u, 1u, 0));
	}
}

void TouchControlsUI::ToggleAutoAttack()
{
	SetAutoAttackOn(m_bAutoAttack ? 0 : 1, 1);
}

TMHuman* TouchControlsUI::ResolveMacroTarget() const
{
	if (!m_dwMacroTargetID || !g_pObjectManager)
		return nullptr;
	return g_pObjectManager->GetHumanByID(m_dwMacroTargetID);
}

TMHuman* TouchControlsUI::SelectAutoTarget() const
{
	if (!g_pCurrentScene || !g_pCurrentScene->m_pMyHuman || !g_pCurrentScene->m_pHumanContainer)
		return nullptr;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	const auto* me = scene->m_pMyHuman;
	const int nSX = static_cast<int>(me->m_vecPosition.x);
	const int nSY = static_cast<int>(me->m_vecPosition.y);
	// Mesmo raio de perseguição do C.C nativo (GameAuto).
	constexpr int kMaxDistance = 12;
	TMHuman* bestUser = nullptr;
	TMHuman* bestMob = nullptr;
	int bestUserDist = kMaxDistance + 1;
	int bestMobDist = kMaxDistance + 1;
	for (TreeNode* node = scene->m_pHumanContainer->m_pDown; node; node = node->m_pNextLink)
	{
		auto* human = static_cast<TMHuman*>(node);
		if (!human || human == me || human->m_stScore.CurHP == 0)
			continue;
		const int d = BASE_GetDistance(nSX, nSY,
			static_cast<int>(human->m_vecPosition.x), static_cast<int>(human->m_vecPosition.y));
		if (d < 0 || d > kMaxDistance)
			continue;
		const bool isUser = human->m_dwID > 0 && human->m_dwID < 1000;
		if (d >= (isUser ? bestUserDist : bestMobDist))
			continue;
		if (!scene->CanAttackHuman(human))
			continue;
		if (isUser)
		{
			bestUser = human;
			bestUserDist = d;
		}
		else
		{
			bestMob = human;
			bestMobDist = d;
		}
	}
	// Jogador só passa em CanAttackHuman quando as regras de PK/guerra permitem;
	// nesse caso ele tem prioridade sobre monstros.
	return bestUser ? bestUser : bestMob;
}

void TouchControlsUI::AutoAttackTick(unsigned int dwServerTime)
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD ||
		!g_pCurrentScene->m_pMyHuman || !g_pObjectManager)
	{
		m_bAutoAttack = 0;
		m_dwMacroTargetID = 0;
		return;
	}
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	// Desligado pelo botão/tecla nativo de auto-ataque.
	if (!scene->m_cAutoAttack)
	{
		m_bAutoAttack = 0;
		m_dwMacroTargetID = 0;
		return;
	}
	if (dwServerTime - m_dwLastMacroTick < 250)
		return;
	m_dwLastMacroTick = dwServerTime;

	// Movimento manual (stick) tem prioridade: pausa o laço nativo até soltar.
	if (dwServerTime - m_dwLastManualMove < 1200)
	{
		scene->m_pTargetHuman = nullptr;
		return;
	}

	// Alvo escolhido na mão (clique/toque) continua valendo se for atacável.
	if (scene->m_pTargetHuman && scene->m_pTargetHuman->m_dwID != m_dwMacroTargetID &&
		scene->CanAttackHuman(scene->m_pTargetHuman))
		m_dwMacroTargetID = scene->m_pTargetHuman->m_dwID;

	TMHuman* target = ResolveMacroTarget();
	if (target)
	{
		const int d = BASE_GetDistance(
			static_cast<int>(scene->m_pMyHuman->m_vecPosition.x), static_cast<int>(scene->m_pMyHuman->m_vecPosition.y),
			static_cast<int>(target->m_vecPosition.x), static_cast<int>(target->m_vecPosition.y));
		// PK desligado no meio do combate, alvo fugiu, morreu ou entrou na cidade.
		if (d < 0 || d > 12 || target->m_stScore.CurHP == 0 || !scene->CanAttackHuman(target))
			target = nullptr;
	}
	if (!target)
		target = SelectAutoTarget();
	m_dwMacroTargetID = target ? target->m_dwID : 0;
	scene->m_pTargetHuman = target;
}

void TouchControlsUI::FireSkill(int slot)
{
	if (!g_pCurrentScene || !g_pObjectManager || !g_pDevice || !g_pTimerManager)
		return;
	if (slot < 0 || slot > 4)
		return;

	g_pObjectManager->m_cSelectShortSkill = static_cast<char>(slot);
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	if (scene->m_pGridSkillBelt2)
		scene->OnKeyShortSkill(static_cast<char>('1' + slot), 0);

	const int cx = static_cast<int>(g_pDevice->m_dwScreenWidth / 2);
	const int cy = static_cast<int>(g_pDevice->m_dwScreenHeight / 2);
	const unsigned int now = g_pTimerManager->GetServerTime();
	scene->SkillUse(cx, cy, scene->GroundGetPickPos(), now, 1, 0);
}

void TouchControlsUI::ToggleMenuPanel(int which)
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	switch (which)
	{
	case 0: scene->SetVisibleInventory(); break;
	case 1: scene->SetVisibleSkill(); break;
	case 2: scene->SetVisibleCharInfo(); break;
	case 3: scene->SetPK(); break;
	default: break;
	}
}

void TouchControlsUI::RebuildLearnedSkills()
{
	m_nLearned = 0;
	if (!g_pObjectManager)
		return;

	const int cls = g_pObjectManager->m_stMobData.Class;
	const unsigned int learned0 = g_pObjectManager->m_stMobData.LearnedSkill[0];
	const unsigned int learned1 = g_pObjectManager->m_stMobData.LearnedSkill[1];

	auto addSkill = [&](int skillId)
	{
		if (m_nLearned >= 32 || skillId < 0 || skillId >= MAX_SPELL_LIST)
			return;
		if (g_pSpell[skillId].Passive == 1)
			return;
		for (int i = 0; i < m_nLearned; ++i)
		{
			if (m_learnedSkills[i] == skillId)
				return;
		}
		m_learnedSkills[m_nLearned++] = skillId;
	};

	for (int bit = 0; bit < 24; ++bit)
	{
		if (learned0 & (1u << bit))
			addSkill(cls * 24 + bit);
	}
	// Skills especiais / sephirot (faixas usadas pelo cliente).
	for (int skillId = 72; skillId < 96 && m_nLearned < 32; ++skillId)
	{
		if (learned0 & (1u << (skillId - 72)))
			addSkill(skillId);
	}
	for (int skillId = 200; skillId < 248 && m_nLearned < 32; ++skillId)
	{
		const int bit = 4 * ((skillId - 200) / 4);
		if (learned1 & (1u << bit))
			addSkill(skillId);
	}
}

void TouchControlsUI::OpenSkillPicker(int slot)
{
	m_equipSlot = slot;
	m_bPickerOpen = 1;
	m_bMenuOpen = 0;
	m_bCamOpen = 0;
	RebuildLearnedSkills();
	if (m_nLearned == 0 && g_pCurrentScene)
	{
		// Sem lista — abre painel nativo de skills como fallback.
		static_cast<TMFieldScene*>(g_pCurrentScene)->SetVisibleSkill();
	}
}

void TouchControlsUI::CloseSkillPicker()
{
	m_bPickerOpen = 0;
	m_equipSlot = -1;
}

void TouchControlsUI::EquipLearnedSkill(int listIndex)
{
	if (!g_pCurrentScene || !g_pObjectManager || listIndex < 0 || listIndex >= m_nLearned)
		return;
	if (m_equipSlot < 0 || m_equipSlot > 4)
		return;

	const int skillId = m_learnedSkills[listIndex];
	const short itemIndex = static_cast<short>(skillId < 105 ? skillId + 5000 : skillId + 5295);
	if (IsPassiveSkill(itemIndex))
		return;

	auto* item = new STRUCT_ITEM;
	std::memset(item, 0, sizeof(STRUCT_ITEM));
	item->sIndex = itemIndex;
	auto* gridItem = new SGridControlItem(nullptr, item, 0.0f, 0.0f);
	if (!gridItem)
	{
		delete item;
		return;
	}

	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	scene->SetShortSkill(m_equipSlot, gridItem);
	scene->UpdateSkillBelt();
	SAFE_DELETE(gridItem);

	CloseSkillPicker();
}

void TouchControlsUI::ClearPointer(int id)
{
	if (m_stickFinger == id)
	{
		m_stickFinger = kInvalidFinger;
		m_stickDX = m_stickDY = 0.0f;
		PadStop();
	}
	if (m_lookFinger == id)
	{
		m_lookFinger = kInvalidFinger;
		m_lookArmed = 0;
	}
	if (m_attackFinger == id)
		m_attackFinger = kInvalidFinger;
	for (int i = 0; i < 5; ++i)
	{
		if (m_skillFinger[i] == id)
			m_skillFinger[i] = kInvalidFinger;
	}
}

int TouchControlsUI::OnPointerDown(int id, int x, int y)
{
	if (!m_bFeature)
		return 0;
	if (!m_bLayoutReady)
		RebuildLayout();

	const Zone z = HitTest(static_cast<float>(x), static_cast<float>(y));
	if (z == Zone::None)
		return 0;

	if (z == Zone::Toggle)
	{
		m_bOverlayOn = !m_bOverlayOn;
		ClearPointer(id);
		m_stickDX = m_stickDY = 0.0f;
		m_bMenuOpen = 0;
		m_bCamOpen = 0;
		CloseSkillPicker();
		return 1;
	}

	if (!m_bOverlayOn)
		return 0;

	if (z == Zone::PickerBack)
		return 1;

	if (z >= Zone::Picker0)
	{
		const int idx = static_cast<int>(z) - static_cast<int>(Zone::Picker0);
		EquipLearnedSkill(idx);
		return 1;
	}

	if (z == Zone::Menu)
	{
		m_bMenuOpen = !m_bMenuOpen;
		if (m_bMenuOpen)
		{
			m_bOverlayOn = 1;
			m_bCamOpen = 0;
		}
		return 1;
	}

	if (z == Zone::CamMenu)
	{
		m_bCamOpen = !m_bCamOpen;
		if (m_bCamOpen)
			m_bMenuOpen = 0;
		return 1;
	}

	if (z >= Zone::BtnInv && z <= Zone::BtnPK)
	{
		ToggleMenuPanel(static_cast<int>(z) - static_cast<int>(Zone::BtnInv));
		return 1;
	}
	if (z == Zone::BtnTouch)
	{
		m_bOverlayOn = !m_bOverlayOn;
		return 1;
	}
	if (z == Zone::BtnAuto)
	{
		ToggleAutoAttack();
		return 1;
	}

	if (z >= Zone::ZoomIn && z <= Zone::CamReset)
	{
		const int i = static_cast<int>(z) - static_cast<int>(Zone::ZoomIn);
		if (i == 0)
			ApplyZoom(-1); // aproximar
		else if (i == 1)
			ApplyZoom(1); // afastar
		else if (i == 2)
			RotateCameraStep();
		else
			ResetCamera();
		return 1;
	}

	if (z == Zone::Stick)
	{
		m_stickFinger = id;
		m_stickDX = Clampf(static_cast<float>(x) - m_layout.stickCX, -m_layout.stickR, m_layout.stickR);
		m_stickDY = Clampf(static_cast<float>(y) - m_layout.stickCY, -m_layout.stickR, m_layout.stickR);
		return 1;
	}

	if (z == Zone::Look)
	{
		m_lookFinger = id;
		m_lookLastX = static_cast<float>(x);
		m_lookLastY = static_cast<float>(y);
		m_lookDownX = m_lookLastX;
		m_lookDownY = m_lookLastY;
		m_lookArmed = 0; // só gira a câmera após arrastar
		return 1;
	}

	if (z == Zone::Attack)
	{
		m_attackFinger = id;
		FireAttack();
		return 1;
	}

	const int skill = static_cast<int>(z) - static_cast<int>(Zone::Skill0);
	if (skill >= 0 && skill < 5)
	{
		m_skillFinger[skill] = id;
		m_dwSkillDownTime[skill] = g_pTimerManager ? g_pTimerManager->GetServerTime() : 0;
		m_skillDownX[skill] = static_cast<float>(x);
		m_skillDownY[skill] = static_cast<float>(y);
		m_bSkillFired[skill] = 0;
		return 1;
	}
	return 0;
}

int TouchControlsUI::OnPointerMove(int id, int x, int y)
{
	if (!m_bFeature || !m_bOverlayOn)
		return 0;
	if (m_bUiBlocked || IsGameUiBlocking())
	{
		ClearPointer(id);
		return 0;
	}

	if (m_stickFinger == id)
	{
		float dx = static_cast<float>(x) - m_layout.stickCX;
		float dy = static_cast<float>(y) - m_layout.stickCY;
		const float mag = std::sqrt(dx * dx + dy * dy);
		if (mag > m_layout.stickR && mag > 0.001f)
		{
			dx = dx * m_layout.stickR / mag;
			dy = dy * m_layout.stickR / mag;
		}
		m_stickDX = dx;
		m_stickDY = dy;
		return 1;
	}

	if (m_lookFinger == id)
	{
		const float fx = static_cast<float>(x);
		const float fy = static_cast<float>(y);
		if (!m_lookArmed)
		{
			const float moved = std::sqrt(Dist2(fx, fy, m_lookDownX, m_lookDownY));
			if (moved < kLongPressSlop)
				return 1; // jitter: não engole o mundo de verdade, mas ainda não gira
			m_lookArmed = 1;
			m_lookLastX = fx;
			m_lookLastY = fy;
			return 1;
		}
		ApplyLookDelta(fx - m_lookLastX, fy - m_lookLastY);
		m_lookLastX = fx;
		m_lookLastY = fy;
		return 1;
	}

	if (m_attackFinger == id)
		return 1;

	for (int i = 0; i < 5; ++i)
	{
		if (m_skillFinger[i] != id)
			continue;
		// Long-press: abre picker sem precisar soltar.
		if (!m_bSkillFired[i] && g_pTimerManager)
		{
			const unsigned int now = g_pTimerManager->GetServerTime();
			const float moved = std::sqrt(Dist2(static_cast<float>(x), static_cast<float>(y),
				m_skillDownX[i], m_skillDownY[i]));
			if (now - m_dwSkillDownTime[i] >= kLongPressMs && moved < kLongPressSlop)
			{
				m_bSkillFired[i] = 2; // 2 = abriu picker
				OpenSkillPicker(i);
			}
		}
		return 1;
	}
	return 0;
}

int TouchControlsUI::OnPointerUp(int id, int x, int y)
{
	if (!m_bFeature)
		return 0;

	// Com UI nativa bloqueando, não consumir ups (libera dedo→mouse / evita LMB preso).
	if (m_bUiBlocked || IsGameUiBlocking())
	{
		ClearPointer(id);
		return 0;
	}

	// Picker: só consome se este dedo era do HUD ou o toque é no grid do picker.
	if (m_bPickerOpen)
	{
		const int ownedSkill = [&]() {
			for (int i = 0; i < 5; ++i)
			{
				if (m_skillFinger[i] == id)
					return 1;
			}
			return 0;
		}();
		const int onPicker = (PickerHitIndex(static_cast<float>(x), static_cast<float>(y)) >= 0) ||
			(HitTest(static_cast<float>(x), static_cast<float>(y)) == Zone::PickerBack);
		if (ownedSkill || onPicker)
		{
			if (PickerHitIndex(static_cast<float>(x), static_cast<float>(y)) < 0)
				CloseSkillPicker();
			ClearPointer(id);
			return 1;
		}
	}

	for (int i = 0; i < 5; ++i)
	{
		if (m_skillFinger[i] != id)
			continue;
		if (m_bSkillFired[i] == 0)
		{
			unsigned int held = 0;
			if (g_pTimerManager)
				held = g_pTimerManager->GetServerTime() - m_dwSkillDownTime[i];
			const float moved = std::sqrt(Dist2(static_cast<float>(x), static_cast<float>(y),
				m_skillDownX[i], m_skillDownY[i]));
			if (held >= kLongPressMs && moved < kLongPressSlop)
				OpenSkillPicker(i);
			else
				FireSkill(i);
		}
		m_skillFinger[i] = kInvalidFinger;
		return 1;
	}

	const int owned = (m_stickFinger == id || m_lookFinger == id || m_attackFinger == id);
	ClearPointer(id);
	return owned ? 1 : 0;
}

extern "C" int WYD_Linux_TouchHudPointer(int id, int x, int y, int isDown, int isUp, int isMove)
{
	if (!g_bTouchUi)
		return 0;
	auto& hud = TouchControlsUI::Instance();
	if (!hud.IsFeatureEnabled())
		return 0;
	if (isDown)
		return hud.OnPointerDown(id, x, y);
	if (isUp)
		return hud.OnPointerUp(id, x, y);
	if (isMove)
		return hud.OnPointerMove(id, x, y);
	return 0;
}

extern "C" int WYD_Linux_PadMove(float nx, float ny)
{
	if (!g_pTimerManager)
		return 0;
	return TouchControlsUI::Instance().PadMove(nx, ny, g_pTimerManager->GetServerTime());
}

extern "C" void WYD_Linux_PadStop(void)
{
	TouchControlsUI::Instance().PadStop();
}

extern "C" int WYD_Linux_PadAction(int action)
{
	return TouchControlsUI::Instance().PadAction(action);
}

// 0 = desligado, 1 = macro DN (auto-ataque com PK), 2 = C.C mágico nativo,
// 3 = outro modo C.C nativo (físico/tecla A).
extern "C" int WYD_Linux_MacroState()
{
	if (g_GameAuto == 2)
		return 2;
	if (g_GameAuto != 0)
		return 3;
	return TouchControlsUI::Instance().IsMacroOn() ? 1 : 0;
}

// target: 0 OFF, 1 DN, 2 MG — síncrono (sem fila WM_CHAR).
extern "C" int WYD_Linux_SetMacroMode(int target)
{
	if (target < 0 || target > 2)
		return 0;
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	auto& ui = TouchControlsUI::Instance();

	if (WYD_Linux_MacroState() == target)
		return 1;

	if (g_GameAuto != 0)
	{
		g_GameAuto = 0;
		scene->NewCCMode(true);
	}
	ui.SetAutoAttackOn(0, 0);

	if (target == 1)
	{
		ui.SetAutoAttackOn(1, 1);
	}
	else if (target == 2)
	{
		g_GameAuto = 2;
		scene->NewCCMode(true);
		if (scene->m_pChatList)
		{
			scene->m_pChatList->AddItem(new SListBoxItem(
				"C.C magico ATIVO (tecla D)",
				0xFFFFAAAA, 0.0f, 0.0f, 300.0f, 16.0f, 0, 0x77777777u, 1u, 0));
		}
	}
	else if (scene->m_pChatList)
	{
		scene->m_pChatList->AddItem(new SListBoxItem(
			"Macro desativado",
			0xFFFFAAAA, 0.0f, 0.0f, 300.0f, 16.0f, 0, 0x77777777u, 1u, 0));
	}
	return 1;
}

// Toque sobre painel/HUD nativo (inventário, cargo, skills…) — sem delay de pick 3D.
extern "C" int WYD_Linux_HitsGameUi(int x, int y)
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	auto hit = [&](SControl* c) -> int {
		return (c && c->IsVisible() && c->PtInControl(x, y)) ? 1 : 0;
	};
	if (hit(scene->m_pInvenPanel) || hit(scene->m_pCargoPanel) || hit(scene->m_pCargoPanel1))
		return 1;
	if (hit(scene->m_pShopPanel) || hit(scene->m_pTradePanel) || hit(scene->m_pAutoTrade))
		return 1;
	if (hit(scene->m_pSkillPanel) || hit(scene->m_pCPanel) || hit(scene->m_pccmode))
		return 1;
	if (hit(scene->m_pSystemPanel) || hit(scene->m_pMiniPanel) || hit(scene->m_pPGTPanel))
		return 1;
	if (hit(scene->m_pGridSkillBelt) || hit(scene->m_pGridSkillBelt2) || hit(scene->m_pGridSkillBelt3))
		return 1;
	if (hit(scene->m_pShortSkillPanel) || hit(scene->m_pMainInfo1) || hit(scene->m_pMainInfo1_BG))
		return 1;
	if (scene->m_pMessageBox && scene->m_pMessageBox->IsVisible() && scene->m_pMessageBox->PtInControl(x, y))
		return 1;
	if (scene->m_pMessageBox2 && scene->m_pMessageBox2->IsVisible() && scene->m_pMessageBox2->PtInControl(x, y))
		return 1;
	return 0;
}

extern "C" void WYD_Linux_FieldNotify(const char* text)
{
	if (!text || !text[0])
		return;
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	if (!scene->m_pChatList)
		return;
	scene->m_pChatList->AddItem(new SListBoxItem(
		text, 0xFFFFFF88, 0.0f, 0.0f, 300.0f, 16.0f, 0, 0x77777777u, 1u, 0));
}

extern "C" int WYD_Linux_PKState(void)
{
	return TMFieldScene::m_bPK ? 1 : 0;
}

extern "C" int WYD_Linux_ChatFocused()
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	return (scene->m_pEditChat && scene->m_pEditChat->IsFocused()) ? 1 : 0;
}

extern "C" int WYD_Linux_InFieldScene()
{
	return (g_pCurrentScene && g_pCurrentScene->m_eSceneType == ESCENE_TYPE::ESCENE_FIELD &&
		g_pCurrentScene->m_pMyHuman) ? 1 : 0;
}

extern "C" int WYD_Linux_MessageBoxVisible()
{
	if (!g_pCurrentScene || g_pCurrentScene->m_eSceneType != ESCENE_TYPE::ESCENE_FIELD)
		return 0;
	auto* scene = static_cast<TMFieldScene*>(g_pCurrentScene);
	if (scene->m_pMessageBox && scene->m_pMessageBox->IsVisible())
		return 1;
	if (scene->m_pMessageBox2 && scene->m_pMessageBox2->IsVisible())
		return 1;
	return 0;
}

extern "C" int WYD_Linux_TouchUiEnabled()
{
	if (!g_bTouchUi)
		return 0;
	return TouchControlsUI::Instance().IsFeatureEnabled() ? 1 : 0;
}
