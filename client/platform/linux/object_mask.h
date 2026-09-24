#pragma once

// object.bin: máscaras 16x16 criptografadas (MeshManager::ReadObjectMask).
#include <cstdint>
#include <string>
#include <vector>

constexpr int WYD_MAX_OBJECT_MASK = 1024; // MAX_OBJECT_MASK no client; ajustado no .cpp se diferir

struct WYDObjectMaskTable
{
	// [mask][y][x] — valores pós-decrypt
	std::vector<int8_t> data; // flattened mask*16*16
	int mask_count = 0;
	bool checksum_ok = false;
	std::string error;
};

bool WYD_LoadObjectMask(WYDObjectMaskTable& out);
