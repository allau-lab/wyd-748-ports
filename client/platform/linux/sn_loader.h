#pragma once

// sn.bin 7.48: 11 nomes de 9 bytes + 11 int32 de ordem de grupo.
#include <cstdint>
#include <string>

constexpr std::size_t WYD_SN_COUNT = 11;
constexpr std::size_t WYD_SN_NAME_WIDTH = 9;

struct WYDServerNameList
{
	char names[WYD_SN_COUNT][16] {};
	int group_order[WYD_SN_COUNT] {};
	bool ok = false;
	std::string error;
};

bool WYD_LoadServerNameList(WYDServerNameList& out);
