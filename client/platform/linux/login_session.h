#pragma once

// Sessão de login + mundo de campo (CreateMob).
#include "login_account.h"
#include "field_entities.h"

#include <string>

struct WYDSelCharSlot
{
	char name[16] {};
	unsigned int level = 0;
	int coin = 0;
	bool occupied = false;
};

struct WYDLoginSession
{
	bool connected = false;
	bool login_sent = false;
	bool cnf_received = false;
	bool charlogin_sent = false;
	bool field_entered = false;
	bool rejected = false;
	bool peer_closed = false;
	unsigned int sock = 0;
	std::string host;
	int port = 8281;
	std::string status;
	unsigned short last_opcode = 0;
	char secret_code[16] {};
	char account_name[16] {};
	int cargo_coin = 0;
	WYDSelCharSlot slots[4] {};
	int selected_slot = -1;
	// Campos extraídos de MSG_CNFCharacterLogin (0x114, 2104 B).
	unsigned short client_id = 0;
	short pos_x = 0;
	short pos_y = 0;
	unsigned short weather = 0;
	char field_mob_name[16] {};
	int create_mob_count = 0;
	int action_sent = 0;
	WYDFieldWorld world {};
};

// host/port via args ou WYD_SERVER_HOST / WYD_SERVER_PORT.
// account/password via WYD_ACCOUNT / WYD_PASSWORD.
// WYD_LOGIN_MOCK=1 sintetiza CNF/0x114 sem servidor (smoke ABI).
bool WYD_LoginConnectAndSend(WYDLoginSession& out,
	const char* host, int port,
	const char* account, const char* password);

// Poll não-bloqueante: Receive + ReadMessage. Trata 0x10A / rejeições / 0x114.
bool WYD_LoginPoll(WYDLoginSession& session);

// Envia MSG_CharacterLogin 0x213 (36 B) para slot 0..3 após CNF.
bool WYD_LoginSendCharacter(WYDLoginSession& session, int slot);

// Envia MSG_Action 0x366 (52 B) — movimento local→servidor (skip em mock).
bool WYD_LoginSendAction(WYDLoginSession& session,
	short pos_x, short pos_y, short target_x, short target_y);

void WYD_LoginClose(WYDLoginSession& session);
