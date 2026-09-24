#include "bone_ani_catalog.h"
#include "asset_resolve.h"

#include <cstdio>
#include <fstream>
#include <sstream>

bool WYD_LoadBoneAniCatalog(WYDBoneAniCatalog& out, const char* relative)
{
	out = {};
	if (!relative || !relative[0])
	{
		out.error = "path vazio";
		return false;
	}
	std::string path = WYD_ResolveAssetAbsolute(relative);
	if (path.empty())
		path = relative;
	std::ifstream in(path);
	if (!in)
	{
		out.error = std::string("falha ao ler ") + path;
		return false;
	}
	out.path = path;
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		for (char& c : line)
		{
			if (c == '\\')
				c = '/';
		}
		std::istringstream ss(line);
		WYDBoneAniEntry e {};
		if (!(ss >> e.index >> e.num_ani_cuts >> e.num_parts >> e.prefix))
			continue;
		if (e.index < 0 || e.num_parts <= 0 || e.prefix.empty())
			continue;
		out.entries.push_back(e);
	}
	if (out.entries.empty())
	{
		out.error = "BoneAni4 sem entradas";
		return false;
	}
	std::fprintf(stderr, "[WYDLINUX][boneani] %s entries=%zu\n",
		path.c_str(), out.entries.size());
	return true;
}

const WYDBoneAniEntry* WYD_BoneAniFind(const WYDBoneAniCatalog& cat, int index)
{
	for (const auto& e : cat.entries)
	{
		if (e.index == index)
			return &e;
	}
	return nullptr;
}

std::string WYD_BoneAniPartPath(const WYDBoneAniEntry& e, int part1based, int look,
	const char* ext)
{
	// Igual TMSkinMesh: "%s%02d%02d.msh" (look pode ter >2 dígitos).
	char buf[128];
	std::snprintf(buf, sizeof(buf), "%s%02d%02d%s",
		e.prefix.c_str(), part1based, look, ext ? ext : ".msh");
	return buf;
}

bool WYD_LoadValidAniIndex(WYDValidAniIndex& out, const char* relative)
{
	out = {};
	if (!relative || !relative[0])
	{
		out.error = "path vazio";
		return false;
	}
	std::string path = WYD_ResolveAssetAbsolute(relative);
	if (path.empty())
		path = relative;
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		out.error = std::string("falha ao ler ") + path;
		return false;
	}
	const size_t need = sizeof(out.table);
	in.read(reinterpret_cast<char*>(out.table), static_cast<std::streamsize>(need));
	if (static_cast<size_t>(in.gcount()) != need)
	{
		out.error = "ValidIndex.bin tamanho inválido";
		return false;
	}
	out.path = path;
	out.loaded = true;
	std::fprintf(stderr, "[WYDLINUX][validani] %s OK (%dx%d)\n",
		path.c_str(), WYDValidAniIndex::kMaxBoneAni, WYDValidAniIndex::kMaxCuts);
	return true;
}

std::string WYD_BoneAniClipPath(const WYDBoneAniEntry& e, const WYDValidAniIndex& vi,
	int clipIndex)
{
	int fileIndex = clipIndex; // fallback: clipIndex+1 sem ValidIndex
	if (vi.loaded && e.index >= 0 && e.index < WYDValidAniIndex::kMaxBoneAni &&
		clipIndex >= 0 && clipIndex < WYDValidAniIndex::kMaxCuts)
	{
		fileIndex = vi.table[e.index][clipIndex];
	}
	char buf[128];
	std::snprintf(buf, sizeof(buf), "%s%04d.ani", e.prefix.c_str(), fileIndex + 1);
	return buf;
}
