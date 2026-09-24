// switch_input.cpp — leitura de gamepad/touch da libnx → injeção na fila Win32
// (winuser_sdl) e no HUD de toque (TouchControlsUI). É o "WYD_Switch_PollInput"
// que o pump do cliente chama a cada frame (SW-60/61).
//
// Field: mapa configurável (tabela kButtons × kActions), salvo em controls.cfg e
// editável na aba CONTROLE do painel de ajuda (H). Letras são injetadas como
// WM_CHAR, igual ao teclado do PC. Fixos: L-stick = andar, Plus = Esc.
// Fora do Field (login/seleção): sticks = cursor, A/ZR = clique esq,
// B = clique dir, ZL = teclado virtual no campo focado, D-pad = setas,
// Minus = Tab, Plus = Esc.
// Touch: HUD de toque primeiro; toque livre vira clique esquerdo na posição.

#ifdef WYD_SWITCH

#ifdef interface
#undef interface
#endif

#include <switch.h>
#include "winuser_sdl.h"

#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" void WYD_Switch_InjectMouseMove(float nx, float ny);
extern "C" void WYD_Switch_InjectMouseButton(int right, int down);
extern "C" void WYD_Switch_InjectKey(int vk, int down);
extern "C" void WYD_Switch_InjectChar(int ch);
extern "C" void WYD_Switch_InjectWheel(int delta);
extern "C" int WYD_Linux_TouchHudPointer(int id, int x, int y, int isDown, int isUp, int isMove);
extern "C" int WYD_Linux_HitsGameUi(int x, int y);
extern "C" void WYD_Linux_FieldNotify(const char* text);
extern "C" int WYD_Linux_PadMove(float nx, float ny);
extern "C" void WYD_Linux_PadStop(void);
extern "C" int WYD_Linux_PadAction(int action);
extern "C" int WYD_Linux_InFieldScene();
extern "C" int WYD_Linux_MessageBoxVisible();
extern "C" int WYD_Linux_MacroState();
extern "C" int WYD_Linux_SetMacroMode(int target);
extern "C" int WYD_Linux_PKState(void);
extern "C" void WYD_Switch_OpenMacroTab(void);
extern "C" int WYD_Switch_ConsumeOpenMacro(void);
extern "C" int WYD_Linux_ChatFocused();
extern "C" int WYD_Switch_LowMemApplet(void);
#if defined(WYD_TEX_PROBE) && WYD_TEX_PROBE
extern "C" void WYD_TexProbeCycle(void);
#endif

namespace
{

// Espelha TouchControlsUI::PadActionId.
enum PadAction : int
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

constexpr float kScreenW = 1280.0f;
constexpr float kScreenH = 720.0f;
constexpr int kTouchIdBase = 200;
constexpr int kMaxTouches = 16;

PadState g_pad;
u64 g_btnPrev = 0;
bool g_lmbDown = false;
bool g_rmbDown = false;
float g_cursorX = 0.5f;
float g_cursorY = 0.5f;
bool g_padInit = false;

struct TouchSlot
{
	u32 fingerId;
	bool active;
	bool hud;   // dono = HUD de toque; senão = mouse
	int lastX;
	int lastY;
};
TouchSlot g_touch[kMaxTouches] = {};
int g_mouseFinger = -1;
// Frames entre posicionar o cursor e pressionar: o campo resolve o pick 3D
// (chão/NPC/mob) no FrameMove a partir da posição anterior do cursor.
// Em painéis/UI o delay é 0 (inventário/2 toques/drag).
constexpr int kTouchPressDelayFrames = 2;
constexpr int kTouchTapSlopPx = 12;
int g_touchPressDelay = 0;
bool g_touchReleasePending = false;
int g_touchDownX = 0;
int g_touchDownY = 0;
int g_touchMaxMovePx = 0;
int g_touchLastWasTap = 1;
bool g_padMoving = false;
int g_touchDiagLines = 0;
bool g_lowMemWarned = false;

void TouchDiag(int px, int py, bool hud)
{
	if (g_touchDiagLines >= 300)
		return;
	++g_touchDiagLines;
	if (FILE* f = fopen("sdmc:/switch/client748/wyd748_diag.txt", "ab"))
	{
		fprintf(f, "[TOUCH] down %d,%d hud=%d\n", px, py, hud ? 1 : 0);
		fclose(f);
	}
}

float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

bool StickVec(int idx, float& nx, float& ny, float& mag)
{
	HidAnalogStickState st = padGetStickPos(&g_pad, idx);
	nx = (float)st.x / 32768.0f;
	ny = -(float)st.y / 32768.0f; // +y = baixo na tela
	mag = std::sqrt(nx * nx + ny * ny);
	return mag >= 0.18f;
}

void MoveCursorBy(float nx, float ny, float mag)
{
	// velocidade proporcional ao desvio (cursor progressivo, como todo console)
	const float speed = 0.010f + 0.020f * (mag - 0.18f);
	g_cursorX = Clamp01(g_cursorX + nx * speed);
	g_cursorY = Clamp01(g_cursorY + ny * speed);
	WYD_Switch_InjectMouseMove(g_cursorX, g_cursorY);
}

// UTF-8 → Latin-1 (fonte do cliente é 8 bits); fora do intervalo vira '?'.
void InjectUtf8(const char* s)
{
	const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
	while (*p)
	{
		unsigned cp = *p++;
		if (cp >= 0x80)
		{
			int extra = (cp & 0xE0) == 0xC0 ? 1 : (cp & 0xF0) == 0xE0 ? 2 : (cp & 0xF8) == 0xF0 ? 3 : 0;
			cp &= extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07;
			for (; extra > 0 && (*p & 0xC0) == 0x80; --extra)
				cp = (cp << 6) | (*p++ & 0x3F);
			if (extra != 0 || cp > 0xFF)
				cp = '?';
		}
		if (cp >= 0x20)
			WYD_Switch_InjectChar((int)cp);
	}
}

bool g_appletRan = false;

// Teclado virtual do sistema (applet bloqueante). Retorna false se cancelado.
bool RunSoftwareKeyboard(char* out, size_t outSize, const char* guide)
{
	g_appletRan = true;
	SwkbdConfig kbd;
	if (R_FAILED(swkbdCreate(&kbd, 0)))
		return false;
	swkbdConfigMakePresetDefault(&kbd);
	swkbdConfigSetGuideText(&kbd, guide);
	swkbdConfigSetStringLenMax(&kbd, 80);
	out[0] = 0;
	const Result rc = swkbdShow(&kbd, out, outSize);
	swkbdClose(&kbd);
	return R_SUCCEEDED(rc) && out[0] != 0;
}

void ChatButton(bool inField)
{
	char text[256];
	if (!inField)
	{
		if (RunSoftwareKeyboard(text, sizeof(text), "Texto"))
			InjectUtf8(text);
		return;
	}
	if (WYD_Linux_ChatFocused())
	{
		WYD_Switch_InjectChar(13);
		return;
	}
	if (!RunSoftwareKeyboard(text, sizeof(text), "Mensagem"))
		return;
	WYD_Switch_InjectChar(13); // abre/foca o chat nativo
	InjectUtf8(text);
	WYD_Switch_InjectChar(13); // envia (Enter) — um único ZL basta
}

void CycleMacro()
{
	switch (WYD_Linux_MacroState())
	{
	case 0: // OFF → MG
		WYD_Linux_SetMacroMode(2);
		break;
	case 2: // MG → DN
		WYD_Linux_SetMacroMode(1);
		break;
	case 1: // DN → OFF
		WYD_Linux_SetMacroMode(0);
		break;
	default: // C.C físico nativo (tecla A) ligado por fora → OFF
		WYD_Linux_SetMacroMode(0);
		break;
	}
}

// Leva o macro direto ao modo pedido (0 OFF, 1 DN, 2 MG).
void SetMacroMode(int target)
{
	WYD_Linux_SetMacroMode(target);
}

// ---------------------------------------------------------------------------
// Mapa configurável
// ---------------------------------------------------------------------------

enum ActKind : int { AK_None, AK_Macro, AK_Chat, AK_Char, AK_Pad, AK_MacroMenu };

struct ActionDef
{
	const char* key;    // nome no controls.cfg
	const char* label;  // texto na aba CONTROLE (fonte do cliente: sem acento)
	ActKind kind;
	int value;
};

const ActionDef kActions[] = {
	{ "none",      "(nenhuma)",               AK_None,  0 },
	{ "macro",     "Macro MG / DN / OFF",     AK_Macro, 0 },
	{ "macromenu", "Menu do macro",           AK_MacroMenu, 0 },
	{ "chat",      "Chat (teclado virtual)",  AK_Chat,  0 },
	// --- Funções de teclado do Field (OnKey*) ---
	{ "inventory", "Inventario (I/G)",        AK_Char,  'i' },
	{ "charinfo",  "Personagem (C)",          AK_Char,  'c' },
	{ "skills",    "Skills (S)",              AK_Char,  's' },
	{ "map",       "Mapa (M)",                AK_Char,  'm' },
	{ "party",     "Grupo (P)",               AK_Char,  'p' },
	{ "help",      "Ajuda / CONTROLE (H)",    AK_Char,  'h' },
	{ "pk",        "PK on/off (K)",           AK_Char,  'k' },
	{ "names",     "Nomes (N)",               AK_Char,  'n' },
	{ "camview",   "Visao de camera (Tab)",   AK_Char,  9 },
	{ "autorun",   "Auto-correr (])",         AK_Char,  ']' },
	{ "skillpage", "Pagina de skills (Z)",    AK_Char,  'z' },
	{ "quest",     "Quests (X)",              AK_Char,  'x' },
	{ "run",       "Correr (R)",              AK_Char,  'r' },
	{ "hpotion",   "Pocao HP (Q)",            AK_Char,  'q' },
	{ "mpotion",   "Pocao MP (W)",            AK_Char,  'w' },
	{ "ppotion",   "Pocao SP (E)",            AK_Char,  'e' },
	{ "feed",      "Alimentar montaria (V)",  AK_Char,  'v' },
	{ "auto",      "Usar automatico (F)",       AK_Char,  'f' },
	{ "autotarget","Alvo automatico (Y)",     AK_Char,  'y' },
	{ "guild",     "Guilda on/off (')",       AK_Char,  '\'' },
	{ "dash",      "Dash (-)",                AK_Char,  '-' },
	{ "chatsize",  "Tamanho do chat (+)",     AK_Char,  '+' },
	{ "skill1k",   "Atalho skill 1",          AK_Char,  '1' },
	{ "skill2k",   "Atalho skill 2",          AK_Char,  '2' },
	{ "skill3k",   "Atalho skill 3",          AK_Char,  '3' },
	{ "skill4k",   "Atalho skill 4",          AK_Char,  '4' },
	{ "skill5k",   "Atalho skill 5",          AK_Char,  '5' },
	{ "skill6k",   "Atalho skill 6",          AK_Char,  '6' },
	{ "skill7k",   "Atalho skill 7",          AK_Char,  '7' },
	{ "skill8k",   "Atalho skill 8",          AK_Char,  '8' },
	{ "skill9k",   "Atalho skill 9",          AK_Char,  '9' },
	{ "skill0k",   "Atalho skill 0",          AK_Char,  '0' },
	// --- Pad / camera / skills touch ---
	{ "attack",    "Atacar",                  AK_Pad,   PA_Attack },
	{ "skill1",    "Skill barra 1",           AK_Pad,   PA_Skill0 },
	{ "skill2",    "Skill barra 2",           AK_Pad,   PA_Skill1 },
	{ "skill3",    "Skill barra 3",           AK_Pad,   PA_Skill2 },
	{ "skill4",    "Skill barra 4",           AK_Pad,   PA_Skill3 },
	{ "skill5",    "Skill barra 5",           AK_Pad,   PA_Skill4 },
	{ "camrotate", "Girar camera",            AK_Pad,   PA_CamRotate },
	{ "camreset",  "Resetar camera",          AK_Pad,   PA_CamReset },
	{ "zoomin",    "Zoom +",                  AK_Pad,   PA_ZoomIn },
	{ "zoomout",   "Zoom -",                  AK_Pad,   PA_ZoomOut },
};
constexpr int kNumActions = (int)(sizeof(kActions) / sizeof(kActions[0]));

int FindAction(const char* key)
{
	for (int i = 0; i < kNumActions; ++i)
	{
		if (std::strcmp(kActions[i].key, key) == 0)
			return i;
	}
	return -1;
}

// Entradas remapeáveis; as 4 direções do R-stick viram botões virtuais.
enum VirtualInput : u32
{
	VI_RsUp = 1u << 0, VI_RsDown = 1u << 1, VI_RsLeft = 1u << 2, VI_RsRight = 1u << 3,
};

struct ButtonDef
{
	const char* key;
	const char* label;
	u64 hid;        // 0 = virtual
	u32 virt;
	const char* defAction;
};

const ButtonDef kButtons[] = {
	{ "A",       "A",             HidNpadButton_A,      0,          "macro" },
	{ "B",       "B",             HidNpadButton_B,      0,          "skills" },
	{ "X",       "X",             HidNpadButton_X,      0,          "inventory" },
	{ "Y",       "Y",             HidNpadButton_Y,      0,          "charinfo" },
	{ "L",       "L",             HidNpadButton_L,      0,          "skill5" },
	{ "R",       "R",             HidNpadButton_R,      0,          "camrotate" },
	{ "ZL",      "ZL",            HidNpadButton_ZL,     0,          "chat" },
	{ "ZR",      "ZR",            HidNpadButton_ZR,     0,          "map" },
	{ "Minus",   "Menos (-)",     HidNpadButton_Minus,  0,          "camreset" },
	{ "Up",      "D-pad cima",    HidNpadButton_Up,     0,          "skill1" },
	{ "Right",   "D-pad direita", HidNpadButton_Right,  0,          "skill2" },
	{ "Down",    "D-pad baixo",   HidNpadButton_Down,   0,          "skill3" },
	{ "Left",    "D-pad esquerda",HidNpadButton_Left,   0,          "skill4" },
	{ "RsUp",    "R-stick cima",  0,                    VI_RsUp,    "party" },
	{ "RsDown",  "R-stick baixo", 0,                    VI_RsDown,  "help" },
	{ "RsLeft",  "R-stick esq.",  0,                    VI_RsLeft,  "pk" },
	{ "RsRight", "R-stick dir.",  0,                    VI_RsRight, "skillpage" },
	{ "R3",      "R3 (apertar)",  HidNpadButton_StickR, 0,          "quest" },
	{ "L3",      "L3 (apertar)",  HidNpadButton_StickL, 0,          "macromenu" },
};
constexpr int kNumButtons = (int)(sizeof(kButtons) / sizeof(kButtons[0]));

constexpr const char* kControlsFile = "controls.cfg";

int g_binding[kNumButtons];
u32 g_virtPrev = 0;
u32 g_rDirHeld = 0;

void ResetBindings()
{
	for (int i = 0; i < kNumButtons; ++i)
	{
		const int a = FindAction(kButtons[i].defAction);
		g_binding[i] = a < 0 ? 0 : a;
	}
}

void SaveBindings()
{
	FILE* fp = fopen(kControlsFile, "wt");
	if (!fp)
		return;
	std::fprintf(fp, "# WYD748 Switch - mapa de botoes (editavel na aba CONTROLE da ajuda)\n");
	for (int i = 0; i < kNumButtons; ++i)
		std::fprintf(fp, "%s=%s\n", kButtons[i].key, kActions[g_binding[i]].key);
	std::fclose(fp);
}

void LoadBindings()
{
	ResetBindings();
	FILE* fp = fopen(kControlsFile, "rt");
	if (!fp)
		return;
	char line[96];
	while (std::fgets(line, sizeof(line), fp))
	{
		char btn[32] = {}, act[32] = {};
		if (line[0] == '#' || std::sscanf(line, " %31[^= ] = %31s", btn, act) != 2)
			continue;
		const int a = FindAction(act);
		for (int i = 0; i < kNumButtons && a >= 0; ++i)
		{
			if (std::strcmp(kButtons[i].key, btn) == 0)
				g_binding[i] = a;
		}
	}
	std::fclose(fp);
}

void RunAction(int action, bool typing)
{
	const ActionDef& a = kActions[action];
	switch (a.kind)
	{
	case AK_Macro:
		if (!typing)
			CycleMacro();
		break;
	case AK_Chat:
		ChatButton(true);
		break;
	case AK_Char:
		if (!typing)
			WYD_Switch_InjectChar(a.value);
		break;
	case AK_Pad:
		WYD_Linux_PadAction(a.value);
		break;
	case AK_MacroMenu:
		if (!typing)
			WYD_Switch_OpenMacroTab();
		break;
	default:
		break;
	}
}

// R-stick como 4 direções "seguradas", com histerese para não repetir.
u32 RightStickVirtual()
{
	float nx, ny, mag;
	StickVec(1, nx, ny, mag);
	if (g_rDirHeld != 0)
	{
		if (mag < 0.35f)
			g_rDirHeld = 0;
		return g_rDirHeld;
	}
	if (mag < 0.7f)
		return 0;
	if (std::fabs(nx) > std::fabs(ny))
		g_rDirHeld = nx > 0 ? VI_RsRight : VI_RsLeft;
	else
		g_rDirHeld = ny > 0 ? VI_RsDown : VI_RsUp;
	return g_rDirHeld;
}

// ---------------------------------------------------------------------------
// Aba CONTROLE (painel de ajuda): linhas geradas aqui, desenhadas pela cena.
// ---------------------------------------------------------------------------

enum MenuLineKind { ML_Header, ML_Fixed, ML_Button, ML_Action, ML_Reset, ML_Cancel, ML_MacroMode, ML_MacroPK };

struct MenuLine
{
	MenuLineKind kind;
	int index;
	unsigned int color;
	char text[96];
};

MenuLine g_menu[96];
int g_menuCount = 0;
int g_menuSel = -1;
int g_pickButton = -1;   // >= 0: escolhendo ação para este botão
unsigned int g_menuRev = 1;
bool g_menuActive = false;
int g_menuPage = 0;      // 0 = CONTROLE, 1 = MACRO
int g_macroSeen = -1;    // estado macro/PK exibido na página MACRO
bool g_openMacroPending = false;

bool Selectable(const MenuLine& l)
{
	return l.kind == ML_Button || l.kind == ML_Action || l.kind == ML_Reset || l.kind == ML_Cancel ||
		l.kind == ML_MacroMode || l.kind == ML_MacroPK;
}

void AddLine(MenuLineKind kind, int index, unsigned int color, const char* fmt, const char* a = "", const char* b = "")
{
	if (g_menuCount >= (int)(sizeof(g_menu) / sizeof(g_menu[0])))
		return;
	MenuLine& l = g_menu[g_menuCount++];
	l.kind = kind;
	l.index = index;
	l.color = color;
	std::snprintf(l.text, sizeof(l.text), fmt, a, b);
}

int MacroStateKey()
{
	return WYD_Linux_MacroState() * 2 + (WYD_Linux_PKState() ? 1 : 0);
}

void BuildMacroPage()
{
	const int mode = WYD_Linux_MacroState();
	const bool pk = WYD_Linux_PKState() != 0;
	static const char* const kModeName[] = { "DESLIGADO", "DN (ataque fisico)", "MG (C.C magico)", "C.C fisico nativo" };
	g_macroSeen = MacroStateKey();
	AddLine(ML_Header, 0, 0xFFFFFF88u, "MACRO - toque numa linha ou D-pad + A");
	AddLine(ML_Fixed, 0, 0xFFFFFFFFu, "Agora:  %s   |   PK: %s", kModeName[mode < 0 || mode > 3 ? 0 : mode],
		pk ? "LIGADO" : "desligado");
	AddLine(ML_MacroMode, 0, mode == 0 ? 0xFF88FF88u : 0xFFFFFFFFu, "%s Desligar macro", mode == 0 ? "[X]" : "[  ]");
	AddLine(ML_MacroMode, 1, mode == 1 ? 0xFF88FF88u : 0xFFFFFFFFu, "%s DN - ataque fisico automatico", mode == 1 ? "[X]" : "[  ]");
	AddLine(ML_MacroMode, 2, mode == 2 ? 0xFF88FF88u : 0xFFFFFFFFu, "%s MG - C.C magico (skills da barra)", mode == 2 ? "[X]" : "[  ]");
	AddLine(ML_MacroPK, 0, pk ? 0xFFFF6666u : 0xFFFFFFFFu, "%s PK - atacar jogadores (tecla K)", pk ? "[X]" : "[  ]");
	AddLine(ML_Fixed, 0, 0xFFFFFF88u, "Atacar jogadores (regra do PK original):");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, " 1. Ligue o PK (linha acima ou R-stick esquerda).");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, " 2. Voce e o alvo precisam estar em zona de PK,");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, "    fora da cidade.");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, " 3. Ligue DN ou MG (botao A). Jogador tem prioridade.");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, "Nunca ataca: grupo, sua guilda, guilda aliada,");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, "    quem esta em comercio ou em zona segura.");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, "PK desligado: so jogadores da guilda em guerra.");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, "Alcance 12 celulas. Andar com o L-stick pausa 1,2 s.");
	AddLine(ML_Fixed, 0, 0xFFCCCCCCu, "Botoes: A = alterna MG/DN/OFF   L3 = este menu");
}

void RebuildMenu()
{
	g_menuCount = 0;
	if (g_menuPage == 1)
		BuildMacroPage();
	else if (g_pickButton < 0)
	{
		AddLine(ML_Header, 0, 0xFFFFFF88u, "CONTROLE - toque na linha do botao; depois escolha a funcao");
		AddLine(ML_Fixed, 0, 0xFFAAAAAAu, "L-stick: Andar (fixo)  |  Mais (+): Esc (fixo)");
		AddLine(ML_Fixed, 0, 0xFFAAAAAAu, "Lista: funcoes de teclado do Field + pad/camera");
		for (int i = 0; i < kNumButtons; ++i)
			AddLine(ML_Button, i, 0xFFFFFFFFu, "%s:  %s", kButtons[i].label, kActions[g_binding[i]].label);
		AddLine(ML_Reset, 0, 0xFFFF9966u, "Restaurar padrao");
	}
	else
	{
		AddLine(ML_Header, 0, 0xFFFFFF88u, "Funcao para %s  (B = voltar)", kButtons[g_pickButton].label);
		for (int i = 0; i < kNumActions; ++i)
			AddLine(ML_Action, i, i == g_binding[g_pickButton] ? 0xFF88FF88u : 0xFFFFFFFFu, "%s", kActions[i].label);
		AddLine(ML_Cancel, 0, 0xFFFF9966u, "Cancelar");
	}
	if (g_menuSel < 0 || g_menuSel >= g_menuCount || !Selectable(g_menu[g_menuSel]))
	{
		g_menuSel = -1;
		for (int i = 0; i < g_menuCount; ++i)
		{
			if (Selectable(g_menu[i])) { g_menuSel = i; break; }
		}
	}
	++g_menuRev;
}

void MenuActivate(int line)
{
	if (line < 0 || line >= g_menuCount)
		return;
	const MenuLine l = g_menu[line];
	switch (l.kind)
	{
	case ML_Button:
		g_pickButton = l.index;
		g_menuSel = -1;
		RebuildMenu();
		// seleção começa na ação atual
		for (int i = 0; i < g_menuCount; ++i)
		{
			if (g_menu[i].kind == ML_Action && g_menu[i].index == g_binding[g_pickButton]) { g_menuSel = i; break; }
		}
		++g_menuRev;
		return;
	case ML_Action:
		g_binding[g_pickButton] = l.index;
		SaveBindings();
		g_menuSel = 3 + g_pickButton;
		g_pickButton = -1;
		break;
	case ML_Reset:
		ResetBindings();
		SaveBindings();
		break;
	case ML_MacroMode:
		SetMacroMode(l.index);
		g_menuSel = line;
		break;
	case ML_MacroPK:
		WYD_Switch_InjectChar('k');
		g_menuSel = line;
		break;
	case ML_Cancel:
		g_menuSel = 3 + g_pickButton;
		g_pickButton = -1;
		break;
	default:
		return;
	}
	RebuildMenu();
}

void MenuMove(int dir)
{
	if (g_menuCount == 0)
		return;
	int i = g_menuSel;
	for (int n = 0; n < g_menuCount; ++n)
	{
		i = (i + dir + g_menuCount) % g_menuCount;
		if (Selectable(g_menu[i]))
		{
			g_menuSel = i;
			++g_menuRev;
			return;
		}
	}
}

void MapMenuButtons(u64 pressed)
{
	if (pressed & HidNpadButton_Up)
		MenuMove(-1);
	if (pressed & HidNpadButton_Down)
		MenuMove(1);
	if (pressed & HidNpadButton_A)
		MenuActivate(g_menuSel);
	if ((pressed & HidNpadButton_B) && g_pickButton >= 0)
	{
		g_menuSel = 3 + g_pickButton;
		g_pickButton = -1;
		RebuildMenu();
	}
}

void MapSticks(bool inField)
{
	float lx, ly, lm, rx, ry, rm;
	const bool lOn = StickVec(0, lx, ly, lm);
	if (inField)
	{
		if (lOn)
		{
			WYD_Linux_PadMove(lx, ly);
			g_padMoving = true;
		}
		else if (g_padMoving)
		{
			WYD_Linux_PadStop();
			g_padMoving = false;
		}
		return;
	}
	g_padMoving = false;
	const bool rOn = StickVec(1, rx, ry, rm);
	if (rOn)
		MoveCursorBy(rx, ry, rm);
	else if (lOn)
		MoveCursorBy(lx, ly, lm);
}

void SetMouse(bool right, bool down)
{
	bool& st = right ? g_rmbDown : g_lmbDown;
	if (st == down)
		return;
	st = down;
	WYD_Switch_InjectMouseButton(right ? 1 : 0, down ? 1 : 0);
}

void MapFieldButtons(u64 btn, u64 pressed)
{
	if (g_mouseFinger < 0)
		SetMouse(false, false);
	SetMouse(true, false);

	const u32 virt = RightStickVirtual();
	const u32 virtPressed = virt & ~g_virtPrev;
	g_virtPrev = virt;

	if (g_menuActive)
	{
		MapMenuButtons(pressed);
		return;
	}

	// Teleporte / confirmações: A = Sim, B = Não (não dispara macro no pad).
	if (WYD_Linux_MessageBoxVisible())
	{
		if (pressed & HidNpadButton_A)
			WYD_Switch_InjectChar('Y');
		if (pressed & HidNpadButton_B)
			WYD_Switch_InjectChar('N');
		return;
	}

	// Com o chat focado as letras iriam para a caixa de texto.
	const bool typing = WYD_Linux_ChatFocused() != 0;
	for (int i = 0; i < kNumButtons; ++i)
	{
		const ButtonDef& b = kButtons[i];
		const bool edge = b.hid ? (pressed & b.hid) != 0 : (virtPressed & b.virt) != 0;
		if (edge)
			RunAction(g_binding[i], typing);
	}
}

void MapButtons(bool inField)
{
	const u64 btn = padGetButtons(&g_pad);
	const u64 pressed = btn & ~g_btnPrev;
	auto held = [&](u64 b) { return (btn & b) != 0; };

	if (!inField && (pressed & HidNpadButton_ZL))
		ChatButton(false);
	else if (inField)
		MapFieldButtons(btn, pressed);

	// o applet do teclado consome tempo; relê para não gerar bordas falsas
	if (g_appletRan)
	{
		g_appletRan = false;
		padUpdate(&g_pad);
		g_btnPrev = padGetButtons(&g_pad);
		return;
	}

	if (!inField)
	{
		if (g_mouseFinger < 0)
			SetMouse(false, held(HidNpadButton_A) || held(HidNpadButton_ZR));
		SetMouse(true, held(HidNpadButton_B));

		struct KeyMap { u64 button; int vk; };
		static constexpr KeyMap kNav[] = {
			{ HidNpadButton_Up,    VK_UP },
			{ HidNpadButton_Down,  VK_DOWN },
			{ HidNpadButton_Left,  VK_LEFT },
			{ HidNpadButton_Right, VK_RIGHT },
			{ HidNpadButton_Minus, VK_TAB },
		};
		for (const KeyMap& k : kNav)
		{
			const bool now = held(k.button);
			const bool was = (g_btnPrev & k.button) != 0;
			if (now != was)
				WYD_Switch_InjectKey(k.vk, now ? 1 : 0);
		}
	}

	if (pressed & HidNpadButton_Plus)
		WYD_Switch_InjectKey(VK_ESCAPE, 1);
	else if (!held(HidNpadButton_Plus) && (g_btnPrev & HidNpadButton_Plus))
		WYD_Switch_InjectKey(VK_ESCAPE, 0);

#if defined(WYD_TEX_PROBE) && WYD_TEX_PROBE
	// L3 sem ação no mapa alterna o PROBE de textura (diagnóstico).
	if ((pressed & HidNpadButton_StickL) && kActions[g_binding[kNumButtons - 1]].kind == AK_None)
		WYD_TexProbeCycle();
#endif
	g_btnPrev = btn;
}

TouchSlot* FindSlot(u32 fingerId)
{
	for (TouchSlot& s : g_touch)
	{
		if (s.active && s.fingerId == fingerId)
			return &s;
	}
	return nullptr;
}

void MapTouch()
{
	HidTouchScreenState states = {};
	const size_t count = hidGetTouchScreenStates(&states, 1);
	const int n = (count > 0) ? (int)states.count : 0;

	bool seen[kMaxTouches] = {};
	for (int i = 0; i < n && i < (int)(sizeof(states.touches) / sizeof(states.touches[0])); ++i)
	{
		const HidTouchState& t = states.touches[i];
		const int px = (int)t.x;
		const int py = (int)t.y;
		TouchSlot* s = FindSlot(t.finger_id);
		if (!s)
		{
			for (TouchSlot& f : g_touch)
			{
				if (!f.active) { s = &f; break; }
			}
			if (!s)
				continue;
			s->active = true;
			s->fingerId = t.finger_id;
			const int id = kTouchIdBase + (int)(s - g_touch);
			s->hud = WYD_Linux_TouchHudPointer(id, px, py, 1, 0, 0) != 0;
			TouchDiag(px, py, s->hud);
			if (!s->hud && g_mouseFinger < 0)
			{
				g_mouseFinger = (int)(s - g_touch);
				g_cursorX = Clamp01((float)px / kScreenW);
				g_cursorY = Clamp01((float)py / kScreenH);
				WYD_Switch_InjectMouseMove(g_cursorX, g_cursorY);
				if (g_lmbDown)
					SetMouse(false, false);
				g_touchDownX = px;
				g_touchDownY = py;
				g_touchMaxMovePx = 0;
				g_touchLastWasTap = 1;
				g_touchReleasePending = false;
				// UI (inventário/cargo/HUD nativo): press imediato.
				// Mundo 3D: delay para o FrameMove resolver o pick.
				if (WYD_Linux_HitsGameUi(px, py))
				{
					g_touchPressDelay = 0;
					SetMouse(false, true);
				}
				else
					g_touchPressDelay = kTouchPressDelayFrames;
			}
		}
		else
		{
			const int idx = (int)(s - g_touch);
			if (s->hud)
				WYD_Linux_TouchHudPointer(kTouchIdBase + idx, px, py, 0, 0, 1);
			else if (idx == g_mouseFinger)
			{
				const int dx = px - g_touchDownX;
				const int dy = py - g_touchDownY;
				const int adx = dx < 0 ? -dx : dx;
				const int ady = dy < 0 ? -dy : dy;
				const int dist = adx > ady ? adx : ady;
				if (dist > g_touchMaxMovePx)
					g_touchMaxMovePx = dist;
				if (g_touchMaxMovePx > kTouchTapSlopPx)
					g_touchLastWasTap = 0;
				g_cursorX = Clamp01((float)px / kScreenW);
				g_cursorY = Clamp01((float)py / kScreenH);
				WYD_Switch_InjectMouseMove(g_cursorX, g_cursorY);
			}
		}
		s->lastX = px;
		s->lastY = py;
		seen[s - g_touch] = true;
	}

	for (int i = 0; i < kMaxTouches; ++i)
	{
		TouchSlot& s = g_touch[i];
		if (!s.active || seen[i])
			continue;
		if (s.hud)
			WYD_Linux_TouchHudPointer(kTouchIdBase + i, s.lastX, s.lastY, 0, 1, 0);
		else if (i == g_mouseFinger)
		{
			if (g_touchPressDelay > 0)
				g_touchReleasePending = true;
			else
				SetMouse(false, false);
			g_mouseFinger = -1;
		}
		s.active = false;
		s.hud = false;
	}

	if (g_touchPressDelay > 0 && --g_touchPressDelay == 0)
	{
		SetMouse(false, true);
		if (g_touchReleasePending)
		{
			SetMouse(false, false);
			g_touchReleasePending = false;
		}
	}
}

} // namespace

extern "C" int WYD_Switch_LastTouchWasTap(void)
{
	return g_touchLastWasTap;
}

extern "C" void WYD_Switch_PollInput(void)
{
	if (!g_padInit)
	{
		padConfigureInput(1, HidNpadStyleSet_NpadStandard);
		padInitializeDefault(&g_pad);
		hidInitializeTouchScreen();
		LoadBindings();
		RebuildMenu();
		g_padInit = true;
	}
	padUpdate(&g_pad);
	const bool inField = WYD_Linux_InFieldScene() != 0;
	if (inField && !g_lowMemWarned)
	{
		g_lowMemWarned = true;
		if (WYD_Switch_LowMemApplet())
		{
			WYD_Linux_FieldNotify(
				"Memoria limitada (Album). Segure R num jogo p/ ~3 GB e menos engasgos.");
		}
	}
	// Página MACRO acompanha mudanças de modo/PK.
	if (inField && g_menuPage == 1 && g_menuActive && MacroStateKey() != g_macroSeen)
		RebuildMenu();
	MapSticks(inField);
	MapTouch();
	MapButtons(inField);
}

extern "C" int WYD_Switch_ControlsLineCount(void)
{
	return g_menuCount;
}

extern "C" const char* WYD_Switch_ControlsLine(int i, unsigned int* color)
{
	static char buf[112];
	if (i < 0 || i >= g_menuCount)
		return "";
	const MenuLine& l = g_menu[i];
	if (color)
		*color = i == g_menuSel ? 0xFF66DDFFu : l.color;
	std::snprintf(buf, sizeof(buf), "%s%s", i == g_menuSel ? "> " : "   ", l.text);
	return buf;
}

extern "C" unsigned int WYD_Switch_ControlsRevision(void)
{
	return g_menuRev;
}

extern "C" void WYD_Switch_ControlsClick(int i)
{
	if (i >= 0 && i < g_menuCount && Selectable(g_menu[i]))
	{
		g_menuSel = i;
		MenuActivate(i);
	}
}

extern "C" void WYD_Switch_OpenMacroTab(void)
{
	g_menuPage = 1;
	g_pickButton = -1;
	g_menuSel = -1;
	g_openMacroPending = true;
	RebuildMenu();
}

extern "C" int WYD_Switch_ConsumeOpenMacro(void)
{
	if (!g_openMacroPending)
		return 0;
	g_openMacroPending = false;
	return 1;
}

extern "C" void WYD_Switch_ControlsSetPage(int page)
{
	page = page == 1 ? 1 : 0;
	if (g_menuPage == page)
		return;
	g_menuPage = page;
	g_pickButton = -1;
	g_menuSel = -1;
	RebuildMenu();
}

extern "C" int WYD_Switch_ControlsPage(void)
{
	return g_menuPage;
}

extern "C" void WYD_Switch_ControlsSetActive(int active)
{
	if (g_menuActive == (active != 0))
		return;
	g_menuActive = active != 0;
	if (!g_menuActive && g_pickButton >= 0)
	{
		g_pickButton = -1;
		RebuildMenu();
	}
}

#endif // WYD_SWITCH
