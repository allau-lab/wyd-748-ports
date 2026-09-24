#pragma once

// iphlpapi.h do alvo Nintendo Switch.
//
// O shim do Linux (platform/linux/compat/iphlpapi.h) usa getifaddrs + AF_PACKET
// para pegar o MAC real das interfaces — nenhum dos dois existe no newlib/libnx
// (não há ifaddrs.h e o HOS não expõe MAC de Wi-Fi/Ethernet para homebrew).
//
// O cliente usa GetAdaptersInfo por UM motivo específico: o campo AdapterName
// vira o HWID do pacote de login (TMSelectServerScene.cpp: tira '{'/'-', faz
// sscanf "%x %x %x %x" e manda em MSG_AccountLogin.AdapterName). Ou seja: o que
// precisa existir é um identificador ESTÁVEL e ÚNICO POR CONSOLE.
//
// Solução: id de máquina persistido no SD, gerado uma vez com `randomGet` (CSRNG
// do kernel, sem precisar inicializar serviço) e gravado em
//   sdmc:/switch/client748/wyd_switch_machine.id
// Não é o MAC (o HOS não dá) mas é estável entre execuções/atualizações do .nro,
// que é o que o fingerprint exige. Sem stub silencioso: se o SD não for gravável,
// o aviso vai para stderr (console do libnx/nxlink) na primeira chamada.

#include "win32_extras.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// NOTA: fopen/fread/fwrite/fclose ficam SEM std:: de propósito — win32_extras.h
// redefine fopen → Wyd_fopen (normalização de '\\') e a forma qualificada não pega.

// libnx: randomGet nunca falha e não exige inicializar serviço (usa svcGetRandomBytes).
#include <switch/kernel/random.h>

#ifndef MAX_ADAPTER_NAME_LENGTH
#define MAX_ADAPTER_NAME_LENGTH 256
#define MAX_ADAPTER_DESCRIPTION_LENGTH 128
#define MAX_ADAPTER_ADDRESS_LENGTH 8
#endif

struct IP_ADDR_STRING {
	IP_ADDR_STRING* Next;
	char IpAddress[16];
	char IpMask[16];
	DWORD Context;
};

struct IP_ADAPTER_INFO {
	IP_ADAPTER_INFO* Next;
	DWORD ComboIndex;
	char AdapterName[MAX_ADAPTER_NAME_LENGTH + 4];
	char Description[MAX_ADAPTER_DESCRIPTION_LENGTH + 4];
	UINT AddressLength;
	BYTE Address[MAX_ADAPTER_ADDRESS_LENGTH];
	DWORD Index;
	UINT Type;
	UINT DhcpEnabled;
	IP_ADDR_STRING* CurrentIpAddress;
	IP_ADDR_STRING IpAddressList;
	IP_ADDR_STRING GatewayList;
	IP_ADDR_STRING DhcpServer;
	BOOL HaveWins;
	IP_ADDR_STRING PrimaryWinsServer;
	IP_ADDR_STRING SecondaryWinsServer;
	time_t LeaseObtained;
	time_t LeaseExpires;
};
using PIP_ADAPTER_INFO = IP_ADAPTER_INFO*;

namespace wyd_switch_iphlpapi
{

// Caminhos candidatos do arquivo de id (primeiro gravável vence).
inline const char** MachineIdPaths()
{
	static const char* kPaths[] = {
		"sdmc:/switch/client748/wyd_switch_machine.id",
		"sdmc:/wyd/wyd_switch_machine.id",
		"sdmc:/wyd_switch_machine.id",
	};
	return kPaths;
}

// Lê 32 dígitos hex do disco; se não houver, gera com CSRNG e persiste.
// Retorna false só quando não foi possível LER NEM GRAVAR (SD ausente/RO).
inline bool MachineIdHex(char out[33])
{
	const char** paths = MachineIdPaths();

	for (int i = 0; i < 3; ++i) {
		FILE* f = fopen(paths[i], "rb");
		if (!f)
			continue;
		char buf[64] = { 0 };
		const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		fclose(f);
		if (n < 32)
			continue;
		bool ok = true;
		for (int k = 0; k < 32; ++k) {
			const char c = buf[k];
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
				ok = false;
				break;
			}
			out[k] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
		}
		if (ok) {
			out[32] = 0;
			return true;
		}
	}

	// Primeira execução (ou arquivo corrompido): 16 bytes do CSRNG do kernel.
	unsigned char raw[16] = { 0 };
	randomGet(raw, sizeof(raw));
	for (int i = 0; i < 16; ++i)
		std::snprintf(out + i * 2, 3, "%02x", (unsigned)raw[i]);
	out[32] = 0;

	for (int i = 0; i < 3; ++i) {
		FILE* f = fopen(paths[i], "wb");
		if (!f)
			continue;
		const size_t written = fwrite(out, 1, 32, f);
		fclose(f);
		if (written == 32)
			return true;
	}

	std::fprintf(stderr,
		"[WARN] wyd_switch: nao foi possivel persistir sdmc:/switch/client748/wyd_switch_machine.id "
		"(HWID sera novo a cada execucao — o login vai cair em conta diferente)\n");
	return false;
}

} // namespace wyd_switch_iphlpapi

inline DWORD GetAdaptersInfo(PIP_ADAPTER_INFO pAdapterInfo, PULONG pOutBufLen)
{
	if (!pOutBufLen)
		return 87; // ERROR_INVALID_PARAMETER

	const size_t need = sizeof(IP_ADAPTER_INFO);
	if (!pAdapterInfo || *pOutBufLen < need) {
		*pOutBufLen = static_cast<ULONG>(need);
		return 111; // ERROR_BUFFER_OVERFLOW (mesma convenção do shim Linux)
	}

	char id[33] = { 0 };
	wyd_switch_iphlpapi::MachineIdHex(id);

	std::memset(pAdapterInfo, 0, sizeof(*pAdapterInfo));

	// Formato GUID como o Win32 ("{AABBCCDD-...}"): o login tira '{'/'-' e faz
	// sscanf "%x %x %x %x" — precisa de exatamente 32 dígitos hex.
	std::snprintf(pAdapterInfo->AdapterName, sizeof(pAdapterInfo->AdapterName),
		"{%c%c%c%c%c%c%c%c-%c%c%c%c-%c%c%c%c-%c%c%c%c-%c%c%c%c%c%c%c%c%c%c%c%c}",
		id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7],
		id[8], id[9], id[10], id[11],
		id[12], id[13], id[14], id[15],
		id[16], id[17], id[18], id[19],
		id[20], id[21], id[22], id[23], id[24], id[25], id[26], id[27],
		id[28], id[29], id[30], id[31]);

	// "MAC" derivado dos 6 primeiros bytes do id (HOS não expõe o MAC real).
	pAdapterInfo->AddressLength = 6;
	pAdapterInfo->Address[0] = 0x02; // bit local-administered: não finge ser OUI de fabricante
	for (int i = 1; i < 6; ++i) {
		const char hi = id[(i - 1) * 2];
		const char lo = id[(i - 1) * 2 + 1];
		const auto hex2 = [](char c) -> unsigned {
			return (c >= '0' && c <= '9') ? (unsigned)(c - '0') : (unsigned)(c - 'a' + 10);
		};
		pAdapterInfo->Address[i] = (BYTE)((hex2(hi) << 4) | hex2(lo));
	}

	std::strncpy(pAdapterInfo->Description, "sdmc machine id (HOS)",
		MAX_ADAPTER_DESCRIPTION_LENGTH);

	// IP/gateway ficam zerados de propósito: o cliente da fase C lê só
	// AdapterName. O IP real entra junto do socketInitialize()/nifm na subida de
	// rede da fase C (SWITCH_PORT.md), não aqui.
	return 0;
}
