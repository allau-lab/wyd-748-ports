#pragma once

#include "TouchActions.h"
#include "VirtualJoystick.h"

struct IDirect3DTexture9;
class TMHuman;

// Touch v2: stick/ATK + coluna de funções com ícones (sempre visível).
// Barra de skills = nativa do WYD. Sem botões inúteis no topo.

class TouchSystem
{
public:
	static TouchSystem& Instance();

	void SetEnabled(int enabled);
	// SW-90: no Switch o barramento é OBRIGATÓRIO (ForceEnable) — sem isso o
	// analógico morre nos menus (IsFieldActive=0 fecha IsEnabled) e o cursor do
	// pad nunca se move. Dentro do campo o mapa continua o mesmo (andar/câmera).
	int IsEnabled() const { return (s_bForceEnable || m_bEnabled) ? 1 : 0; }
	int HasCapture() const;
	int IsFieldActive() const;
	int WantMouseSync() const;

	int OnPointerDown(int id, int x, int y);
	int OnPointerMove(int id, int x, int y);
	int OnPointerUp(int id, int x, int y);

	void Frame(unsigned int dwServerTime);
	void Render();

	// SW-63: entrada de gamepad (Joy-Con / Pro Controller) no MESMO barramento de
	// ações do toque. O port não tinha NENHUM caminho de joystick: o analógico não
	// movia o personagem. Aqui o stick vira o mesmo vetor que o dedo produz, então
	// herda a histerese de 24°, a cadência de repath (90/200 ms), o look-ahead e o
	// ataque com mira automática já depurados no port de celular.
	// Valores normalizados (-1..1); `present` = há pad lido neste frame.
	//   moveX/moveY ....... analógico esquerdo (movimento do personagem)
	//   camX/camY ......... analógico direito (girar / zoom da câmera)
	//   zoom .............. ZL/ZR (zoom contínuo, soma no eixo vertical da câmera)
	//   attackHeld ........ ataque/confirmar (segurar = ataca em série)
	void FeedPad(float moveX, float moveY, float camX, float camY, float zoom,
		int attackHeld, int present);

	// SW-63: liga o barramento independente do Config do cliente. No Switch o touch
	// v2 nasce DESLIGADO (o config lido é o do port de celular: TOUCH_UI/TOUCH_V2),
	// e sem isto nem o pad nem o toque entram no jogo.
	static void ForceEnable(int on);

	const TouchActions& Actions() const { return m_actions; }

private:
	TouchSystem();

	enum class Zone
	{
		None = 0,
		Attack,
		Key0, // coluna: PK Skills Char Map Inv Names Target Help
		Key1,
		Key2,
		Key3,
		Key4,
		Key5,
		Key6,
		Key7,
		StickZone,
		CamPad, // canto invisível (só gesto)
		WorldTap,
	};

	static constexpr int kHotKeys = 8;

	struct Layout
	{
		float W = 0, H = 0, s = 0;
		float stickZoneCX = 0, stickZoneCY = 0, stickVisualR = 0;
		/** Raio de hit ≈ pad visual (+ folga de polegar). NÃO reservar coluna esquerda. */
		float stickHitR = 0;
		float attackCX = 0, attackCY = 0, attackR = 0;
		float keyCX[kHotKeys] = {}, keyCY[kHotKeys] = {}, keyR = 0;
		float camPadCX = 0, camPadCY = 0, camPadR = 0;
	};

	enum IconId
	{
		TI_Stick = 0,
		TI_Attack,
		TI_Inv,
		TI_Skills,
		TI_Char,
		TI_Pk,
		TI_Map,
		TI_Target,
		TI_Names,
		TI_Help,
		TI_Count
	};

	void RebuildLayout();
	Zone HitTest(float x, float y) const;
	int IsUiBlocked() const;
	int HitsNativeUi(float x, float y) const;
	void StopMoveNow();
	void EnsureIcons();
	void DrawDisc(float cx, float cy, float r, unsigned int color);
	void DrawTex(float cx, float cy, float r, IDirect3DTexture9* tex, unsigned int color);

	static int DrivesMove(int finger, int id);
	static int OwnsUp(int finger, int id);
	int ClaimOrEcho(int& fingerSlot, int id);
	void SyncStickToActions();
	void PollFingerStick();
	void BeginStick(int id, float fx, float fy);

	void ApplyPad(unsigned int now);
	void ApplyMove(unsigned int now);
	void ApplyCamPad();
	void ApplyCombat(unsigned int now);
	void ApplyHotKeys();
	void ApplyInteract();
	void FireAttack();
	void ResetCamera();
	TMHuman* FindBestTarget(int preferPlayers) const;

	int m_bEnabled = 0;
	int m_bUiBlocked = 0;
	int m_bLayoutReady = 0;

	Layout m_layout{};
	TouchActions m_actions{};

	static constexpr int kInvalid = -9999;
	static constexpr unsigned kCamDoubleTapMs = 350;

	int m_stickFinger = kInvalid;
	VirtualJoystick m_stick{};
	/** 0 = idle (quase invisível, estilo PS Vita); 1 = ativo (tocando). */
	float m_stickFade = 0.f;
	unsigned int m_dwLastStickFade = 0;
	float m_lastMoveAng = 0.f;
	int m_bHaveMoveAng = 0;
	int m_lastMoveCellX = -99999;
	int m_lastMoveCellY = -99999;

	int m_camFinger = kInvalid;
	float m_camLastX = 0, m_camLastY = 0;
	float m_camDownX = 0, m_camDownY = 0;
	int m_bCamDragging = 0;
	unsigned int m_dwLastCamTap = 0;

	int m_attackFinger = kInvalid;
	unsigned int m_dwLastAtk = 0;
	unsigned int m_dwLastMove = 0;

	int m_worldFinger = kInvalid;
	float m_worldDownX = 0, m_worldDownY = 0;
	int m_bWorldMoved = 0;

	int m_bPkAssist = 0;
	unsigned int m_dwLastAssist = 0;

	// SW-63: estado do gamepad (último FeedPad).
	float m_padMoveX = 0.f, m_padMoveY = 0.f;
	float m_padCamX = 0.f, m_padCamY = 0.f, m_padZoom = 0.f;
	int m_padAttack = 0;
	int m_padAttackLast = 0;
	int m_padPresent = 0;
	int m_padWasPresent = 0;
	unsigned int m_dwLastPadMs = 0;

	/** SW-63: ver ForceEnable() — 1 quando o bus é obrigatório (console). */
	inline static int s_bForceEnable = 0;

	IDirect3DTexture9* m_tex[TI_Count] = {};
	int m_bIconsTried = 0;
};

extern "C" int WYD_TouchV2_Enabled();
extern "C" int WYD_TouchV2_HasCapture();
extern "C" int WYD_TouchV2_WantMouseSync();
extern "C" int WYD_TouchV2_Pointer(int id, int x, int y, int isDown, int isUp, int isMove);
extern "C" int WYD_TouchV2_Finger(int id, float nx, float ny, int winW, int winH,
	int isDown, int isUp, int isMove);
extern "C" int WYD_TouchV2_PointerWindow(int id, int wx, int wy, int winW, int winH,
	int isDown, int isUp, int isMove);
extern "C" void WYD_TouchV2_ForceEnable(int on);
extern "C" void WYD_TouchV2_Pad(float moveX, float moveY, float camX, float camY,
	float zoom, int attackHeld, int present);
