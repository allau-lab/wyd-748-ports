#pragma once
// Registra no wyd748_diag.txt (Switch) cada asset que o client não conseguiu abrir.
#include <cstdio>

#if defined(__SWITCH__)
#include <string>
#include <unordered_set>

inline void WYD_DiagMissing(const char* kind, const char* path)
{
	static std::unordered_set<std::string> seen;
	if (!path || !path[0] || seen.size() >= 500)
		return;
	if (!seen.insert(path).second)
		return;
	if (FILE* f = fopen("sdmc:/switch/client748/wyd748_diag.txt", "ab"))
	{
		fprintf(f, "[MISSING] %s %s\n", kind, path);
		fclose(f);
	}
}
#else
inline void WYD_DiagMissing(const char*, const char*) {}
#endif
