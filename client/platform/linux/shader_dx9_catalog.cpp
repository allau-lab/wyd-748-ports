#include "shader_dx9_catalog.h"
#include "asset_paths.h"

#include <cstdio>
#include <fstream>

namespace
{
	constexpr uint32_t kVsTokenMask = 0xFFFE0000u;
	constexpr uint32_t kPsTokenMask = 0xFFFF0000u;

	bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return false;
		in.seekg(0, std::ios::end);
		const auto size = in.tellg();
		if (size <= 0)
			return false;
		in.seekg(0, std::ios::beg);
		out.resize(static_cast<size_t>(size));
		in.read(reinterpret_cast<char*>(out.data()), size);
		return static_cast<bool>(in) || in.eof();
	}

	void AddEntry(WYDDx9ShaderCatalog& cat, WYDDx9ShaderKind kind, int index,
		const char* fmt)
	{
		WYDDx9ShaderEntry e;
		e.kind = kind;
		e.index = index;
		char rel[128];
		std::snprintf(rel, sizeof(rel), fmt, index);
		// TMPaths usa '\\'; no Linux normalizamos para '/'.
		for (char* p = rel; *p; ++p)
		{
			if (*p == '\\')
				*p = '/';
		}
		e.relative_path = rel;

		const std::string full = WYD_JoinAssetPath(rel);
		if (!ReadFileBytes(full, e.bytecode))
		{
			e.error = "arquivo ausente ou ilegível (case-sensitive?): " + full;
			++cat.failed;
			cat.entries.push_back(std::move(e));
			return;
		}
		if (e.bytecode.size() < 4)
		{
			e.error = "bytecode curto demais";
			++cat.failed;
			cat.entries.push_back(std::move(e));
			return;
		}

		e.version_token =
			static_cast<uint32_t>(e.bytecode[0]) |
			(static_cast<uint32_t>(e.bytecode[1]) << 8) |
			(static_cast<uint32_t>(e.bytecode[2]) << 16) |
			(static_cast<uint32_t>(e.bytecode[3]) << 24);

		const bool isVs = (e.version_token & 0xFFFF0000u) == kVsTokenMask;
		const bool isPs = (e.version_token & 0xFFFF0000u) == kPsTokenMask;
		if (kind == WYDDx9ShaderKind::PsEffect)
		{
			if (!isPs)
				e.error = "esperava token PS (0xFFFF01xx)";
		}
		else
		{
			if (!isVs)
				e.error = "esperava token VS (0xFFFE01xx)";
		}

		if (e.error.empty())
		{
			e.valid = true;
			++cat.loaded;
		}
		else
		{
			++cat.failed;
		}
		cat.entries.push_back(std::move(e));
	}
}

const char* WYD_Dx9ShaderVersionName(uint32_t token)
{
	switch (token)
	{
	case 0xFFFE0101: return "vs_1_1";
	case 0xFFFE0102: return "vs_1_2";
	case 0xFFFE0103: return "vs_1_3";
	case 0xFFFE0104: return "vs_1_4";
	case 0xFFFE0200: return "vs_2_0";
	case 0xFFFE0201: return "vs_2_x";
	case 0xFFFE0300: return "vs_3_0";
	case 0xFFFF0101: return "ps_1_1";
	case 0xFFFF0102: return "ps_1_2";
	case 0xFFFF0103: return "ps_1_3";
	case 0xFFFF0104: return "ps_1_4";
	case 0xFFFF0200: return "ps_2_0";
	case 0xFFFF0201: return "ps_2_x";
	case 0xFFFF0300: return "ps_3_0";
	default: return "unknown";
	}
}

bool WYD_LoadDx9ShaderCatalog(WYDDx9ShaderCatalog& out)
{
	out = {};
	for (int i = 1; i <= 8; ++i)
		AddEntry(out, WYDDx9ShaderKind::SkinMesh, i, "Shader/skinmesh%d.bin");
	for (int i = 1; i <= 4; ++i)
		AddEntry(out, WYDDx9ShaderKind::VsEffect, i, "Shader/vseffect%d.bin");
	for (int i = 1; i <= 6; ++i)
		AddEntry(out, WYDDx9ShaderKind::PsEffect, i, "Shader/pseffect%d.bin");

	std::fprintf(stderr,
		"[WYDLINUX][dx9sh] catálogo: %d OK, %d falha (total %zu)\n",
		out.loaded, out.failed, out.entries.size());
	for (const auto& e : out.entries)
	{
		if (e.valid)
		{
			std::fprintf(stderr, "  OK  %s  %s  %zu bytes\n",
				e.relative_path.c_str(),
				WYD_Dx9ShaderVersionName(e.version_token),
				e.bytecode.size());
		}
		else
		{
			std::fprintf(stderr, "  ERR %s  %s\n",
				e.relative_path.c_str(), e.error.c_str());
		}
	}
	return out.failed == 0 && out.loaded == 18;
}
