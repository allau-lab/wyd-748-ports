#pragma once

#include "GeomObject.h"
#include "TMFont2.h"

struct IDirect3DTexture9;
class RenderDevice;
class TMHuman;

// Touch HUD estilo Wild Rift: stick + arco ATK/skills,
// utilitários pequenos (toggle/menu/câmera); câmera em submenu.

// Magic nTextureSetIndex for GeomControl → RenderDevice::RenderGeomRectImage
constexpr int kTouchCustomTexSet = -9999;

enum TouchIconId : int
{
	TI_Stick = 0,
	TI_ZoomIn,
	TI_ZoomOut,
	TI_Rotate,
	TI_Reset,
	TI_Look,
	TI_Attack,
	TI_Menu,
	TI_Toggle,
	TI_Inv,
	TI_Skills,
	TI_Char,
	TI_Pk,
	TI_Count
};

void WYD_Touch_RenderCustomIcon(RenderDevice* rd, GeomControl* c);

class TouchControlsUI
{
public:
	static TouchControlsUI& Instance();

	void SetFeatureEnabled(int enabled);
	int IsFeatureEnabled() const { return m_bFeature; }
	int IsOverlayVisible() const { return m_bFeature && m_bOverlayOn; }

	void FrameMove(unsigned int dwServerTime);

	int OnPointerDown(int id, int x, int y);
	int OnPointerMove(int id, int x, int y);
	int OnPointerUp(int id, int x, int y);

	// Loja/trade/inventário/modais: HUD de combate em passthrough (só toggle).
	int IsGameUiBlocking() const;

	// Gamepad: direção em tela normalizada (+x direita, +y baixo), |v| <= 1.
	int PadMove(float nx, float ny, unsigned int dwServerTime);
	void PadStop();
	enum PadActionId : int
	{
		PA_Inventory = 0,
		PA_SkillPanel,
		PA_CharInfo,
		PA_Attack,
		PA_AutoAttack,
		PA_Skill0,
		PA_Skill1,
		PA_Skill2,
		PA_Skill3,
		PA_Skill4,
		PA_ZoomIn,
		PA_ZoomOut,
		PA_CamRotate,
		PA_CamReset,
	};
	int PadAction(int action);
	int IsMacroOn() const { return m_bAutoAttack; }
	/** Liga/desliga DN (macro físico) de forma síncrona. notify=1 → chat. */
	void SetAutoAttackOn(int on, int notify = 1);

	IDirect3DTexture9* GetTouchIconTexture(int iconId);

private:
	TouchControlsUI();

	enum class Zone
	{
		None = 0,
		Toggle,
		Menu,
		CamMenu,
		Stick,
		Look,
		Attack,
		Skill0,
		Skill1,
		Skill2,
		Skill3,
		Skill4,
		BtnInv,
		BtnSkill,
		BtnChar,
		BtnPK,
		BtnTouch,
		BtnAuto,
		ZoomIn,
		ZoomOut,
		CamRotate,
		CamReset,
		PickerBack,
		Picker0,
	};

	struct Layout
	{
		float stickCX, stickCY, stickR;
		float lookCX, lookCY, lookR;
		float attackCX, attackCY, attackR;
		float skillCX[5], skillCY[5], skillR;
		float toggleCX, toggleCY, toggleR;
		float menuCX, menuCY, menuR;
		float subCX[6], subCY[6], subR;
		float camBtnCX, camBtnCY, camBtnR;
		float camCX[4], camCY[4], camR;
		float pickerCX, pickerCY, pickerCell, pickerCols, pickerRows;
	};

	void RebuildLayout();
	Zone HitTest(float x, float y) const;
	int PickerHitIndex(float x, float y) const;
	void DrawDisc(float cx, float cy, float r, unsigned int color, int layer);
	void DrawIcon(float cx, float cy, float r, int textureSet, unsigned int color, int layer);
	void DrawTouchIcon(float cx, float cy, float r, int iconId, unsigned int color, int layer);
	void DrawLabel(float cx, float cy, float w, float h, const char* text, unsigned int color, int layer);
	void DrawIconButton(float cx, float cy, float r, int textureSet, const char* fallback,
		unsigned int tint, int layer);
	void DrawTouchIconButton(float cx, float cy, float r, int iconId, const char* fallback,
		unsigned int tint, int layer);
	void EnsureTouchIconsLoaded();
	void IssueStickMove(unsigned int dwServerTime);
	void IssueDirMove(float nx, float ny);
	void ApplyLookDelta(float dx, float dy);
	void ApplyZoom(int dir);
	void RotateCameraStep();
	void ResetCamera();
	void FireAttack();
	void ToggleAutoAttack();
	void AutoAttackTick(unsigned int dwServerTime);
	TMHuman* SelectAutoTarget() const;
	TMHuman* ResolveMacroTarget() const;
	void FireSkill(int slot);
	void ClearPointer(int id);
	void ResetTransientInput();
	void ToggleMenuPanel(int which);
	void OpenSkillPicker(int slot);
	void CloseSkillPicker();
	void RebuildLearnedSkills();
	void EquipLearnedSkill(int listIndex);
	int ControlTexture(unsigned int controlId) const;
	int SkillTexture(int shortSkillValue) const;
	int FirstValidTexture(unsigned int a, unsigned int b = 0) const;

	int m_bFeature;
	int m_bOverlayOn;
	int m_bAutoAttack;
	int m_bMenuOpen;
	int m_bCamOpen;
	int m_bUiBlocked;
	Layout m_layout;
	int m_bLayoutReady;

	int m_stickFinger;
	float m_stickDX;
	float m_stickDY;
	unsigned int m_dwLastStickMove;
	unsigned int m_dwLastPadMove;
	float m_fMoveDirX;
	float m_fMoveDirY;
	bool m_bDirMoving;

	int m_lookFinger;
	float m_lookLastX;
	float m_lookLastY;
	float m_lookDownX;
	float m_lookDownY;
	int m_lookArmed;

	int m_attackFinger;
	unsigned int m_dwMacroTargetID;
	unsigned int m_dwLastMacroTick;
	unsigned int m_dwLastManualMove;
	int m_skillFinger[5];
	unsigned int m_dwSkillDownTime[5];
	float m_skillDownX[5];
	float m_skillDownY[5];
	int m_bSkillFired[5];

	int m_bPickerOpen;
	int m_equipSlot;
	int m_learnedSkills[32];
	int m_nLearned;

	static constexpr int kMaxGeom = 112;
	GeomControl m_geoms[kMaxGeom];
	int m_nGeomUsed;

	TMFont2 m_fonts[36];
	int m_nFontUsed;

	IDirect3DTexture9* m_touchTex[TI_Count];
	int m_bTouchIconsTried;
};
