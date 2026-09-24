#include "login_session.h"
#include "winsock_compat.h"
#include "CPSock.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <errno.h>

// Símbolos exigidos por CPSock no Linux.
char EncodeByte[4] = {};
HWND hWndMain = nullptr;
unsigned int CurrentTime = 0;
unsigned int LastSendTime = 0;

namespace
{
	CPSock g_sock;

	bool SetNonBlocking(unsigned int fd)
	{
		const int flags = fcntl(static_cast<int>(fd), F_GETFL, 0);
		if (flags < 0)
			return false;
		return fcntl(static_cast<int>(fd), F_SETFL, flags | O_NONBLOCK) == 0;
	}

	void ApplyCnf(WYDLoginSession& out, const MSG_CNFAccountLogin& cnf)
	{
		std::memcpy(out.secret_code, cnf.SecretCode, sizeof(out.secret_code));
		std::memcpy(out.account_name, cnf.AccountName, sizeof(out.account_name));
		out.cargo_coin = cnf.Coin;
		out.cnf_received = true;
		out.last_opcode = MSG_CNFAccountLogin_Opcode;

		for (int i = 0; i < 4; ++i)
		{
			std::memset(out.slots[i].name, 0, sizeof(out.slots[i].name));
			std::memcpy(out.slots[i].name, cnf.SelChar.MobName[i], 15);
			out.slots[i].level = cnf.SelChar.Score[i].Level;
			out.slots[i].coin = cnf.SelChar.Coin[i];
			out.slots[i].occupied = out.slots[i].name[0] != '\0';
		}

		char summary[128];
		std::snprintf(summary, sizeof(summary),
			"CNFAccountLogin 0x10A OK account='%s' slots=",
			out.account_name[0] ? out.account_name : "?");
		out.status = summary;
		for (int i = 0; i < 4; ++i)
		{
			if (!out.slots[i].occupied)
				continue;
			char piece[48];
			std::snprintf(piece, sizeof(piece), "[%d]%s L%u ",
				i, out.slots[i].name, out.slots[i].level);
			out.status += piece;
		}

		std::fprintf(stderr, "[WYDLINUX][login] %s coin=%d\n",
			out.status.c_str(), out.cargo_coin);
	}

	void ApplyCnfCharacterLogin(WYDLoginSession& out, const char* raw, unsigned short size)
	{
		if (size < kCnfCharacterLoginSize)
		{
			out.status = "0x114 tamanho insuficiente";
			std::fprintf(stderr, "[WYDLINUX][login] %s size=%u need=%zu\n",
				out.status.c_str(), size, kCnfCharacterLoginSize);
			return;
		}

		short posX = 0;
		short posY = 0;
		unsigned short slot = 0;
		unsigned short clientId = 0;
		unsigned short weather = 0;
		char mobName[16] {};
		std::memcpy(&posX, raw + kCnfCharOffPosX, sizeof(posX));
		std::memcpy(&posY, raw + kCnfCharOffPosY, sizeof(posY));
		std::memcpy(mobName, raw + kCnfCharOffMobName, 15);
		std::memcpy(&slot, raw + kCnfCharOffSlot, sizeof(slot));
		std::memcpy(&clientId, raw + kCnfCharOffClientID, sizeof(clientId));
		std::memcpy(&weather, raw + kCnfCharOffWeather, sizeof(weather));

		out.pos_x = posX;
		out.pos_y = posY;
		out.client_id = clientId;
		out.weather = weather;
		std::memset(out.field_mob_name, 0, sizeof(out.field_mob_name));
		std::memcpy(out.field_mob_name, mobName, 15);
		out.selected_slot = static_cast<int>(slot);
		out.field_entered = true;
		out.last_opcode = MSG_CNFCharacterLogin_Opcode;

		char buf[128];
		std::snprintf(buf, sizeof(buf),
			"CNFCharacterLogin 0x114 OK slot=%u id=%u pos=%d,%d name='%s'",
			slot, clientId, posX, posY, out.field_mob_name);
		out.status = buf;
		std::fprintf(stderr, "[WYDLINUX][login] %s\n", out.status.c_str());
	}

	void InjectMockField(WYDLoginSession& out)
	{
		if (out.field_entered || !out.charlogin_sent)
			return;

		alignas(8) char raw[kCnfCharacterLoginSize] {};
		auto* hdr = reinterpret_cast<MSG_STANDARD*>(raw);
		hdr->Size = static_cast<unsigned short>(kCnfCharacterLoginSize);
		hdr->Type = MSG_CNFCharacterLogin_Opcode;
		hdr->Tick = 2;

		const short posX = 2100;
		const short posY = 2100;
		const unsigned short slot = static_cast<unsigned short>(
			out.selected_slot >= 0 ? out.selected_slot : 0);
		const unsigned short clientId = 1001;
		const unsigned short weather = 0;
		std::memcpy(raw + kCnfCharOffPosX, &posX, sizeof(posX));
		std::memcpy(raw + kCnfCharOffPosY, &posY, sizeof(posY));
		const char* name = (slot < 4 && out.slots[slot].occupied)
			? out.slots[slot].name : "HeroA";
		std::memcpy(raw + kCnfCharOffMobName, name, std::strlen(name));
		std::memcpy(raw + kCnfCharOffSlot, &slot, sizeof(slot));
		std::memcpy(raw + kCnfCharOffClientID, &clientId, sizeof(clientId));
		std::memcpy(raw + kCnfCharOffWeather, &weather, sizeof(weather));

		ApplyCnfCharacterLogin(out, raw, static_cast<unsigned short>(kCnfCharacterLoginSize));
		out.status = std::string("MOCK ") + out.status;
	}

	bool InjectMockCnf(WYDLoginSession& out)
	{
		MSG_CNFAccountLogin cnf {};
		cnf.Header.Size = static_cast<unsigned short>(sizeof(cnf));
		cnf.Header.Type = MSG_CNFAccountLogin_Opcode;
		cnf.Header.Tick = 1;
		std::memcpy(cnf.SecretCode, "MOCKSECRETCODE!!", 16);
		std::strncpy(cnf.AccountName, "mockacct", sizeof(cnf.AccountName) - 1);
		std::strncpy(cnf.SelChar.MobName[0], "HeroA", 15);
		std::strncpy(cnf.SelChar.MobName[2], "HeroC", 15);
		cnf.SelChar.Score[0].Level = 100;
		cnf.SelChar.Score[2].Level = 50;
		cnf.SelChar.Coin[0] = 1234;
		cnf.Coin = 999;
		ApplyCnf(out, cnf);
		out.connected = true;
		out.login_sent = true;
		out.status = std::string("MOCK ") + out.status;
		return true;
	}

	void HandlePacket(WYDLoginSession& out, const MSG_STANDARD* hdr, const char* raw)
	{
		out.last_opcode = hdr->Type;

		if (hdr->Type == MSG_CNFAccountLogin_Opcode)
		{
			if (hdr->Size < sizeof(MSG_CNFAccountLogin))
			{
				out.status = "0x10A tamanho insuficiente";
				std::fprintf(stderr, "[WYDLINUX][login] %s size=%u\n",
					out.status.c_str(), hdr->Size);
				return;
			}
			MSG_CNFAccountLogin cnf {};
			std::memcpy(&cnf, raw, sizeof(cnf));
			ApplyCnf(out, cnf);
			return;
		}

		if (hdr->Type == MSG_AccountLoginRejectA_Opcode ||
			hdr->Type == MSG_AccountLoginRejectB_Opcode)
		{
			out.rejected = true;
			char buf[64];
			std::snprintf(buf, sizeof(buf), "login rejeitado opcode=0x%X", hdr->Type);
			out.status = buf;
			std::fprintf(stderr, "[WYDLINUX][login] %s\n", out.status.c_str());
			return;
		}

		if (hdr->Type == MSG_CharacterLoginReject_Opcode)
		{
			out.rejected = true;
			out.charlogin_sent = false;
			out.status = "CharacterLogin rejeitado 0x119";
			std::fprintf(stderr, "[WYDLINUX][login] %s\n", out.status.c_str());
			return;
		}

		if (hdr->Type == MSG_CNFCharacterLogin_Opcode)
		{
			ApplyCnfCharacterLogin(out, raw, hdr->Size);
			return;
		}

		if (hdr->Type == MSG_CreateMob_Opcode)
		{
			if (WYD_FieldWorldApplyCreateMob(out.world, raw, hdr->Size, out.client_id))
			{
				out.create_mob_count = out.world.create_total;
				if (out.world.create_total <= 8 || (out.world.create_total % 25) == 0)
				{
					std::fprintf(stderr,
						"[WYDLINUX][field] CreateMob 0x364 alive=%d total=%d\n",
						out.world.count, out.world.create_total);
				}
			}
			return;
		}

		if (hdr->Type == MSG_RemoveMob_Opcode)
		{
			(void)WYD_FieldWorldApplyRemoveMob(out.world, raw, hdr->Size);
			return;
		}

		std::fprintf(stderr, "[WYDLINUX][login] pacote opcode=0x%X size=%u\n",
			hdr->Type, hdr->Size);
	}
}

bool WYD_LoginConnectAndSend(WYDLoginSession& out,
	const char* host, int port,
	const char* account, const char* password)
{
	out = {};
	out.host = host ? host : "127.0.0.1";
	out.port = port > 0 ? port : 8281;
	out.status = "conectando";

	if (const char* mock = std::getenv("WYD_LOGIN_MOCK"); mock && mock[0] == '1')
	{
		std::fprintf(stderr, "[WYDLINUX][login] WYD_LOGIN_MOCK=1 — CNF sintético\n");
		return InjectMockCnf(out);
	}

	if (!g_sock.WSAInitialize())
	{
		out.status = "WSAInitialize falhou";
		return false;
	}

	char hostBuf[256];
	std::snprintf(hostBuf, sizeof(hostBuf), "%s", out.host.c_str());
	std::fprintf(stderr, "[WYDLINUX][login] ConnectServer %s:%d ...\n",
		hostBuf, out.port);

	const unsigned int fd = g_sock.ConnectServer(hostBuf, out.port, 0, 0);
	if (!fd)
	{
		out.status = "ConnectServer falhou (servidor offline?)";
		std::fprintf(stderr, "[WYDLINUX][login] %s\n", out.status.c_str());
		return false;
	}

	out.connected = true;
	out.sock = fd;
	CurrentTime = GetTickCount();

	if (!SetNonBlocking(fd))
		std::fprintf(stderr, "[WYDLINUX][login] aviso: O_NONBLOCK falhou errno=%d\n", errno);

	MSG_AccountLogin pkt {};
	std::memset(&pkt, 0, sizeof(pkt));
	pkt.Header.Type = MSG_AccountLogin_Opcode;
	pkt.Header.ID = 0;
	pkt.Header.Size = static_cast<unsigned short>(sizeof(pkt));
	pkt.ClientVersion = 748;
	pkt.DBNeedSave = 0;

	if (account)
		std::strncpy(pkt.AccountName, account, sizeof(pkt.AccountName) - 1);
	if (password)
		std::strncpy(pkt.AccountPassword, password, sizeof(pkt.AccountPassword) - 1);

	if (!g_sock.AddMessage(reinterpret_cast<char*>(&pkt), static_cast<int>(sizeof(pkt))))
	{
		out.status = "AddMessage(AccountLogin) rejeitado";
		std::fprintf(stderr, "[WYDLINUX][login] %s\n", out.status.c_str());
		return false;
	}

	if (!g_sock.SendMessageA())
	{
		out.status = "SendMessage AccountLogin falhou";
		std::fprintf(stderr, "[WYDLINUX][login] %s\n", out.status.c_str());
		return false;
	}

	out.login_sent = true;
	out.status = "AccountLogin 0x20D enviado — aguardando 0x10A";
	std::fprintf(stderr, "[WYDLINUX][login] %s account='%s'\n",
		out.status.c_str(), pkt.AccountName);
	return true;
}

bool WYD_LoginPoll(WYDLoginSession& session)
{
	if (!session.connected || session.peer_closed)
		return false;

	if (std::getenv("WYD_LOGIN_MOCK") && std::getenv("WYD_LOGIN_MOCK")[0] == '1')
	{
		InjectMockField(session);
		return true;
	}
	const int r = g_sock.Receive();
	// r==0: idle (EAGAIN) ou peer fechou — ReadMessage distingue framing;
	// overflow (r<0) encerra a sessão.
	if (r < 0)
	{
		session.status = "Receive buffer overflow";
		session.peer_closed = true;
		return false;
	}

	for (;;)
	{
		int err = 0;
		int errType = 0;
		char* msg = g_sock.ReadMessage(&err, &errType);
		if (!msg)
		{
			if (err != 0)
			{
				char buf[80];
				std::snprintf(buf, sizeof(buf), "ReadMessage err=%d type=%d", err, errType);
				session.status = buf;
				std::fprintf(stderr, "[WYDLINUX][login] %s\n", session.status.c_str());
				session.peer_closed = true;
				return false;
			}
			break;
		}
		HandlePacket(session, reinterpret_cast<const MSG_STANDARD*>(msg), msg);
	}

	return true;
}

bool WYD_LoginSendCharacter(WYDLoginSession& session, int slot)
{
	if (!session.cnf_received)
	{
		session.status = "CharacterLogin exige CNF prévio";
		return false;
	}
	if (slot < 0 || slot > 3)
	{
		session.status = "slot inválido (0..3)";
		return false;
	}
	if (!session.slots[slot].occupied)
	{
		session.status = "slot vazio";
		return false;
	}

	MSG_CharacterLogin pkt {};
	std::memset(&pkt, 0, sizeof(pkt));
	pkt.Header.Type = MSG_CharacterLogin_Opcode;
	pkt.Header.Size = static_cast<unsigned short>(sizeof(pkt));
	pkt.Slot = slot;
	pkt.Force = 0;
	std::memcpy(pkt.SecretCode, session.secret_code, sizeof(pkt.SecretCode));

	const char* mock = std::getenv("WYD_LOGIN_MOCK");
	if (mock && mock[0] == '1')
	{
		session.selected_slot = slot;
		session.charlogin_sent = true;
		char buf[96];
		std::snprintf(buf, sizeof(buf),
			"MOCK CharacterLogin 0x213 slot=%d name='%s' (36 B)",
			slot, session.slots[slot].name);
		session.status = buf;
		std::fprintf(stderr, "[WYDLINUX][login] %s\n", session.status.c_str());
		return true;
	}

	if (!session.connected)
	{
		session.status = "CharacterLogin sem conexão";
		return false;
	}

	if (!g_sock.AddMessage(reinterpret_cast<char*>(&pkt), static_cast<int>(sizeof(pkt))))
	{
		session.status = "AddMessage(CharacterLogin) rejeitado";
		return false;
	}
	if (!g_sock.SendMessageA())
	{
		session.status = "SendMessage CharacterLogin falhou";
		return false;
	}

	session.selected_slot = slot;
	session.charlogin_sent = true;
	char buf[96];
	std::snprintf(buf, sizeof(buf), "CharacterLogin 0x213 slot=%d name='%s'",
		slot, session.slots[slot].name);
	session.status = buf;
	std::fprintf(stderr, "[WYDLINUX][login] %s\n", session.status.c_str());
	return true;
}

bool WYD_LoginSendAction(WYDLoginSession& session,
	short pos_x, short pos_y, short target_x, short target_y)
{
	if (!session.field_entered)
		return false;

	const char* mock = std::getenv("WYD_LOGIN_MOCK");
	if (mock && mock[0] == '1')
	{
		++session.action_sent;
		return true; // movimento só local no mock
	}

	if (!session.connected)
		return false;

	MSG_Action pkt {};
	std::memset(&pkt, 0, sizeof(pkt));
	pkt.Header.Type = MSG_Action_Opcode;
	pkt.Header.Size = static_cast<unsigned short>(sizeof(pkt));
	pkt.Header.ID = session.client_id;
	pkt.PosX = pos_x;
	pkt.PosY = pos_y;
	pkt.Speed = 0;
	pkt.Effect = 0;
	pkt.TargetX = static_cast<unsigned short>(target_x);
	pkt.TargetY = static_cast<unsigned short>(target_y);

	if (!g_sock.AddMessage(reinterpret_cast<char*>(&pkt), static_cast<int>(sizeof(pkt))))
		return false;
	if (!g_sock.SendMessageA())
		return false;

	++session.action_sent;
	return true;
}

void WYD_LoginClose(WYDLoginSession& session)
{
	if (session.connected)
	{
		const char* mock = std::getenv("WYD_LOGIN_MOCK");
		if (!(mock && mock[0] == '1'))
			g_sock.CloseSocket();
	}
	session.connected = false;
	session.sock = 0;
}
