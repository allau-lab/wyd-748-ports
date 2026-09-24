#pragma once

// Paths case-sensitive para assets 7.48 no Linux.
// Default: pasta do executável (client748/) quando contém config.txt.

#include <string>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <linux/limits.h>

namespace
{
	inline bool WYD_IsDirWithConfig(const std::string& dir)
	{
		struct stat st {};
		if (dir.empty() || ::stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
			return false;
		const std::string cfg = dir + "/config.txt";
		return ::stat(cfg.c_str(), &st) == 0 && S_ISREG(st.st_mode);
	}

	inline std::string WYD_ExecutableDir()
	{
		char buf[PATH_MAX];
		const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		if (n <= 0)
			return {};
		buf[n] = '\0';
		std::string path(buf);
		const auto slash = path.find_last_of('/');
		if (slash == std::string::npos)
			return {};
		return path.substr(0, slash);
	}
}

inline std::string WYD_AssetRoot()
{
	if (const char* env = std::getenv("WYD_ASSET_ROOT"))
	{
		if (env[0] != '\0')
			return env;
	}

	// 1) Pasta do binário (instalação em client748/project)
	const std::string exeDir = WYD_ExecutableDir();
	if (WYD_IsDirWithConfig(exeDir))
		return exeDir;

	// 2) ./client748 relativo ao CWD
	if (WYD_IsDirWithConfig("client748"))
		return "client748";

	// 3) Legado symlink de desenvolvimento
	if (WYD_IsDirWithConfig("client748-assets"))
		return "client748-assets";

	return "client748";
}

// Junta root + relative sem normalizar o casing do relative.
inline std::string WYD_JoinAssetPath(const char* relative)
{
	std::string root = WYD_AssetRoot();
	while (!root.empty() && root.back() == '/')
		root.pop_back();

	std::string rel = relative ? relative : "";
	for (char& c : rel)
	{
		if (c == '\\')
			c = '/';
	}
	while (!rel.empty() && rel.front() == '/')
		rel.erase(rel.begin());

	return root + "/" + rel;
}

inline bool WYD_AssetExists(const char* relative)
{
	const std::string path = WYD_JoinAssetPath(relative);
	struct stat st {};
	return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

inline void WYD_LogMissingAsset(const char* relative)
{
	std::fprintf(stderr,
		"[WYDLINUX] asset ausente (case-sensitive): %s\n",
		WYD_JoinAssetPath(relative).c_str());
}
