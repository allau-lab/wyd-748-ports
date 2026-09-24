#pragma once

// Resolve paths de assets vindos de listas Win32 (MeshList etc.).
// - troca '\' por '/'
// - colapsa '//' (listas usam 'effect\\\\file')
// - tenta variantes de casing do primeiro segmento (effect→Effect)

#include "asset_paths.h"

#include <string>
#include <sys/stat.h>
#include <cctype>

inline std::string WYD_NormalizeListPath(const char* raw)
{
	std::string s = raw ? raw : "";
	for (char& c : s)
	{
		if (c == '\\')
			c = '/';
	}
	// colapsa barras duplicadas
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '/' && !out.empty() && out.back() == '/')
			continue;
		out.push_back(s[i]);
	}
	while (!out.empty() && out.front() == '/')
		out.erase(out.begin());
	return out;
}

inline bool WYD_FileExistsAbs(const std::string& abs)
{
	struct stat st {};
	return ::stat(abs.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Retorna caminho absoluto existente, ou string vazia.
inline std::string WYD_ResolveAssetAbsolute(const char* listPath)
{
	const std::string rel = WYD_NormalizeListPath(listPath);
	if (rel.empty())
		return {};

	std::string abs = WYD_JoinAssetPath(rel.c_str());
	if (WYD_FileExistsAbs(abs))
		return abs;

	// Tenta capitalizar o primeiro segmento: effect → Effect
	const auto slash = rel.find('/');
	if (slash != std::string::npos && slash > 0)
	{
		std::string alt = rel;
		alt[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(alt[0])));
		abs = WYD_JoinAssetPath(alt.c_str());
		if (WYD_FileExistsAbs(abs))
			return abs;

		// tudo lower no primeiro segmento
		alt = rel;
		for (size_t i = 0; i < slash; ++i)
			alt[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(alt[i])));
		abs = WYD_JoinAssetPath(alt.c_str());
		if (WYD_FileExistsAbs(abs))
			return abs;
	}
	return {};
}
