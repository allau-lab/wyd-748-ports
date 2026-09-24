#pragma once
// iphlpapi → getifaddrs (MAC real das interfaces).
#include "win32_extras.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <cstring>
#include <ctime>

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

inline DWORD GetAdaptersInfo(PIP_ADAPTER_INFO pAdapterInfo, PULONG pOutBufLen)
{
	if (!pOutBufLen)
		return 87; // ERROR_INVALID_PARAMETER
	struct ifaddrs* ifa = nullptr;
	if (getifaddrs(&ifa) != 0)
		return 1;

	size_t need = sizeof(IP_ADAPTER_INFO);
	if (!pAdapterInfo || *pOutBufLen < need) {
		*pOutBufLen = static_cast<ULONG>(need);
		freeifaddrs(ifa);
		return 111; // ERROR_BUFFER_OVERFLOW
	}

	std::memset(pAdapterInfo, 0, sizeof(*pAdapterInfo));
	pAdapterInfo->AddressLength = 6;
	for (auto* cur = ifa; cur; cur = cur->ifa_next) {
		if (!cur->ifa_addr || cur->ifa_addr->sa_family != AF_PACKET)
			continue;
		auto* sll = reinterpret_cast<sockaddr_ll*>(cur->ifa_addr);
		if (sll->sll_halen < 6)
			continue;
		std::memcpy(pAdapterInfo->Address, sll->sll_addr, 6);
		std::strncpy(pAdapterInfo->AdapterName, cur->ifa_name ? cur->ifa_name : "eth0", MAX_ADAPTER_NAME_LENGTH);
		std::strncpy(pAdapterInfo->Description, pAdapterInfo->AdapterName, MAX_ADAPTER_DESCRIPTION_LENGTH);
		break;
	}
	freeifaddrs(ifa);
	return 0;
}
