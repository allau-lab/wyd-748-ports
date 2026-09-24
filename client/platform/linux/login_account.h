#pragma once

// Pacotes de login 7.48 — layouts on-wire (iguais SharedStructs / Basedef).
#include "MessageHeader.h"

#include <cstddef>
#include <cstdint>

#pragma pack(push, 1)
struct MSG_AccountLogin
{
	MSG_STANDARD Header;
	char AccountName[16];
	char AccountPassword[12];
	std::int32_t ClientVersion;
	std::int32_t DBNeedSave;
	char Zero[52];
	std::int32_t AdapterName[4];
};
#pragma pack(pop)

// STRUCT_SCORE / SELCHAR / CNF usam alinhamento natural Win32 (CNF = 2360).
struct WYD_STRUCT_SCORE
{
	unsigned int Version;
	unsigned int Level;
	unsigned int Attack;
	unsigned int MagicAttack;
	unsigned int Defense;
	unsigned int MaxHP;
	unsigned int MaxMP;
	unsigned int CurHP;
	unsigned int CurMP;
	unsigned int Str;
	unsigned int Int;
	unsigned int Dex;
	unsigned int Con;
	unsigned int Accuracy;
	unsigned int Evasion;
	unsigned int Parry;
	unsigned int Critical;
	unsigned int Range;
	unsigned int ResistFire;
	unsigned int ResistIce;
	unsigned int ResistHoly;
	unsigned int ResistThunder;
	unsigned int SaveMana;
	unsigned int MagicAmp;
	unsigned int RegenHP;
	unsigned int RegenMP;
	unsigned int StatusPts;
	unsigned int MasterPts;
	unsigned int SkillPts;
	unsigned int Mastery[4];
	unsigned int AttackRun;
	unsigned int Merchant;
};

struct WYD_STRUCT_ITEM
{
	short sIndex;
	short stEffect[3];
};

constexpr int WYD_MAX_EQUIPITEM = 18;

struct WYD_STRUCT_SELCHAR
{
	unsigned short HomeTownX[4];
	unsigned short HomeTownY[4];
	char MobName[4][16];
	WYD_STRUCT_SCORE Score[4];
	WYD_STRUCT_ITEM Equip[4][WYD_MAX_EQUIPITEM];
	unsigned short Guild[4];
	int Coin[4];
	long long Exp[4];
};

struct MSG_CNFAccountLogin
{
	MSG_STANDARD Header;
	char SecretCode[16];
	WYD_STRUCT_SELCHAR SelChar;
	WYD_STRUCT_ITEM Cargo[128];
	int Coin;
	char AccountName[16];
	int SSN1;
	int SSN2;
};

constexpr unsigned short MSG_AccountLogin_Opcode = 0x20D;
constexpr unsigned short MSG_CNFAccountLogin_Opcode = 0x10A;
constexpr unsigned short MSG_CharacterLogin_Opcode = 0x213;
constexpr unsigned short MSG_CNFCharacterLogin_Opcode = 0x114;
constexpr unsigned short MSG_AccountLoginRejectA_Opcode = 0x11C;
constexpr unsigned short MSG_AccountLoginRejectB_Opcode = 0x11D;
constexpr unsigned short MSG_CharacterLoginReject_Opcode = 0x119;
constexpr unsigned short MSG_CreateMob_Opcode = 0x364;
constexpr unsigned short MSG_Action_Opcode = 0x366;
constexpr unsigned short MSG_RemoveMob_Opcode = 0x165;

constexpr std::size_t kCreateMobPacketSize = 328;
constexpr std::size_t kRemoveMobPacketSize = 16;

struct MSG_Action
{
	MSG_STANDARD Header;
	short PosX;
	short PosY;
	std::int32_t Speed;
	std::int32_t Effect;
	unsigned short TargetX;
	unsigned short TargetY;
	char Route[24];
};

static_assert(sizeof(MSG_Action) == 52, "MSG_Action ABI");
static_assert(offsetof(MSG_Action, PosX) == 12, "Action PosX");
static_assert(offsetof(MSG_Action, TargetX) == 24, "Action TargetX");
static_assert(offsetof(MSG_Action, Route) == 28, "Action Route");


// Offsets on-wire de MSG_CNFCharacterLogin (ABI 2104 B — WYD748Compat).
constexpr std::size_t kCnfCharacterLoginSize = 2104;
constexpr std::size_t kCnfCharOffPosX = 12;
constexpr std::size_t kCnfCharOffPosY = 14;
constexpr std::size_t kCnfCharOffMobName = 16;   // início de STRUCT_MOB
constexpr std::size_t kCnfCharOffSlot = 1240;
constexpr std::size_t kCnfCharOffClientID = 1242;
constexpr std::size_t kCnfCharOffWeather = 1244;

struct MSG_CharacterLogin
{
	MSG_STANDARD Header;
	int Slot;
	int Force;
	char SecretCode[16];
};

static_assert(sizeof(MSG_AccountLogin) == 116, "login packet must be 116 bytes");
static_assert(offsetof(MSG_AccountLogin, AccountName) == 12, "account offset");
static_assert(offsetof(MSG_AccountLogin, AccountPassword) == 28, "password offset");
static_assert(offsetof(MSG_AccountLogin, ClientVersion) == 40, "cliver offset");
static_assert(offsetof(MSG_AccountLogin, DBNeedSave) == 44, "DBNeedSave offset");

static_assert(sizeof(WYD_STRUCT_SCORE) == 140, "SCORE ABI");
static_assert(sizeof(WYD_STRUCT_SELCHAR) == 1272, "SELCHAR ABI");
static_assert(sizeof(MSG_CNFAccountLogin) == 2360, "CNFAccountLogin ABI");
static_assert(offsetof(MSG_CNFAccountLogin, SecretCode) == 12, "SecretCode offset");
static_assert(offsetof(MSG_CNFAccountLogin, SelChar) == 32, "SelChar offset");
static_assert(offsetof(MSG_CNFAccountLogin, Cargo) == 1304, "Cargo offset");
static_assert(offsetof(MSG_CNFAccountLogin, Coin) == 2328, "Coin offset");
static_assert(offsetof(MSG_CNFAccountLogin, AccountName) == 2332, "AccountName offset");

static_assert(sizeof(MSG_CharacterLogin) == 36, "CharacterLogin ABI");
static_assert(offsetof(MSG_CharacterLogin, Slot) == 12, "Slot offset");
static_assert(offsetof(MSG_CharacterLogin, Force) == 16, "Force offset");
static_assert(offsetof(MSG_CharacterLogin, SecretCode) == 20, "SecretCode offset");
