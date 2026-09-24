#include "sn_loader.h"
#include "asset_paths.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

bool WYD_LoadServerNameList(WYDServerNameList& out)
{
	out = {};
	const std::string path = WYD_JoinAssetPath("sn.bin");
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		out.error = "sn.bin ausente: " + path;
		return false;
	}

	constexpr std::size_t kSize =
		WYD_SN_COUNT * WYD_SN_NAME_WIDTH + WYD_SN_COUNT * sizeof(std::int32_t);
	std::vector<unsigned char> bytes(kSize);
	in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(kSize));
	if (static_cast<std::size_t>(in.gcount()) != kSize)
	{
		out.error = "sn.bin tamanho inválido (esperado 11*9 + 11*4)";
		return false;
	}

	const std::size_t orderOffset = WYD_SN_COUNT * WYD_SN_NAME_WIDTH;
	for (std::size_t i = 0; i < WYD_SN_COUNT; ++i)
	{
		std::int32_t order = 0;
		std::memcpy(&order, bytes.data() + orderOffset + i * 4, 4);
		if (order < 0 || order >= static_cast<std::int32_t>(WYD_SN_COUNT))
		{
			out.error = "sn.bin group_order fora do intervalo";
			return false;
		}
		out.group_order[i] = order;
		std::memcpy(out.names[i], bytes.data() + i * WYD_SN_NAME_WIDTH, WYD_SN_NAME_WIDTH);
		out.names[i][15] = '\0';
	}

	out.ok = true;
	std::fprintf(stderr, "[WYDLINUX][sn] %zu canais:", WYD_SN_COUNT);
	for (std::size_t i = 0; i < WYD_SN_COUNT; ++i)
		std::fprintf(stderr, " [%zu]=\"%s\"", i, out.names[i]);
	std::fprintf(stderr, "\n");
	return true;
}
