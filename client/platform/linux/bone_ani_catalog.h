#pragma once

// Catálogo BoneAni4.txt + ValidIndex.bin — personagem/mob (Fase P/Q).

#include <cstdint>
#include <string>
#include <vector>

struct WYDBoneAniEntry
{
	int index = -1;
	int num_ani_cuts = 0;
	int num_parts = 0;
	std::string prefix; // ex. "mesh/ch01"
};

struct WYDBoneAniCatalog
{
	std::vector<WYDBoneAniEntry> entries;
	std::string path;
	std::string error;
};

// ValidIndex.bin: 100 × 186 × int32 (MeshManager::m_stValidAniList).
struct WYDValidAniIndex
{
	static constexpr int kMaxBoneAni = 100;
	static constexpr int kMaxCuts = 186;
	int32_t table[kMaxBoneAni][kMaxCuts] {};
	bool loaded = false;
	std::string path;
	std::string error;
};

bool WYD_LoadBoneAniCatalog(WYDBoneAniCatalog& out, const char* relative = "mesh/BoneAni4.txt");

const WYDBoneAniEntry* WYD_BoneAniFind(const WYDBoneAniCatalog& cat, int index);

// Monta path: prefix + part(2) + look + ext  (TMSkinMesh "%s%02d%02d")
std::string WYD_BoneAniPartPath(const WYDBoneAniEntry& e, int part1based, int look,
	const char* ext);

bool WYD_LoadValidAniIndex(WYDValidAniIndex& out, const char* relative = "mesh/ValidIndex.bin");

// clipIndex em 0..num_ani_cuts-1 → "mesh/ch010101.ani" via ValidIndex[n][clip]+1
std::string WYD_BoneAniClipPath(const WYDBoneAniEntry& e, const WYDValidAniIndex& vi,
	int clipIndex);
