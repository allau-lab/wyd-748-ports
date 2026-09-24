// platform_compat.cpp — implementação do funil de caminho/arquivo do port.
// Ver platform_compat.h para o motivo e docs/PORT-STUDY-FALLOUT2-SWITCH.md §1
// para o desenho original que inspirou isto (fallout2-ce-switch).

#include "platform_compat.h"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <string>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace wyd_compat
{
namespace
{

unsigned g_case_corrected = 0;

// Diretório que contém o último componente de `path` (já normalizado).
// "sdmc:/a/b" -> "sdmc:/a" | "a" -> "." | "/a" -> "/" | "sdmc:" -> "."
std::string DirNameOf(const std::string& path)
{
	const std::size_t slash = path.rfind('/');
	if (slash == std::string::npos)
		return std::string(".");
	if (slash == 0)
		return std::string("/");
	std::string dir = path.substr(0, slash);
	// Ponto de montagem do devoptab ("sdmc:") precisa da barra para ser abrível.
	if (!dir.empty() && dir.back() == ':')
		dir += '/';
	return dir;
}

// Procura `name` em `dir_path` ignorando caixa e devolve o nome REAL do disco.
bool NameInDir(const std::string& dir_path, const std::string& name, std::string* real)
{
	DIR* dir = opendir(dir_path.c_str());
	if (!dir)
		return false;

	const std::size_t want = name.size();
	bool found = false;
	while (struct dirent* entry = readdir(dir))
	{
		if (std::strlen(entry->d_name) != want)
			continue;
		if (Strnicmp(entry->d_name, name.c_str(), want) != 0)
			continue;
		*real = entry->d_name;
		found = true;
		break;
	}
	closedir(dir);
	return found;
}

} // namespace

void PathToNative(char* path)
{
	if (!path)
		return;
	for (char* p = path; *p; ++p)
		if (*p == '\\')
			*p = '/';
}

bool ResolvePath(char* path)
{
	if (!path || !*path)
		return false;

	PathToNative(path);

	const std::string in(path);
	std::string out;
	std::size_t i = 0;

	if (in.size() >= 1 && in[0] == '/')
	{
		out = "/";
		i = 1;
	}
	else if (in.size() >= 2 && in[0] == '.' && in[1] == '/')
	{
		out = "./";
		i = 2;
	}

	bool corrected = false;
	while (i < in.size())
	{
		std::size_t sep = in.find('/', i);
		if (sep == std::string::npos)
			sep = in.size();

		const std::string comp = in.substr(i, sep - i);
		i = sep + 1;
		if (comp.empty())
			continue;

		std::string candidate = out;
		if (!candidate.empty() && candidate.back() != '/')
			candidate += '/';
		candidate += comp;

		std::string real;
		if (NameInDir(DirNameOf(candidate), comp, &real))
		{
			if (real != comp)
			{
				++g_case_corrected;
				corrected = true;
			}
		}
		else
		{
			// Melhor esforço: mantém como veio (o erro real aparece no fopen).
			real = comp;
		}

		out += real;
		if (i < in.size())
			out += '/';
	}

	if (out.size() >= static_cast<std::size_t>(WYD_PATH_MAX))
		return corrected; // não cabe: deixamos o caminho original intacto

	std::memcpy(path, out.c_str(), out.size() + 1);
	return corrected;
}

unsigned CaseCorrectedCount()
{
	return g_case_corrected;
}

bool ResolveCopy(const char* path, char* out)
{
	if (!path || !out || !*path)
		return false;
	const std::size_t len = std::strlen(path);
	if (len >= static_cast<std::size_t>(WYD_PATH_MAX))
		return false;
	std::memcpy(out, path, len + 1);
	return ResolvePath(out);
}

std::FILE* Fopen(const char* path, const char* mode)
{
	char native[WYD_PATH_MAX];
	if (!ResolveCopy(path, native))
		return nullptr;
	return std::fopen(native, mode);
}

int Access(const char* path, int mode)
{
	char native[WYD_PATH_MAX];
	if (!ResolveCopy(path, native))
		return -1;
	return ::access(native, mode);
}

bool Exists(const char* path)
{
	return Access(path, F_OK) == 0;
}

bool Mkdir(const char* path)
{
	char native[WYD_PATH_MAX];
	if (!ResolveCopy(path, native))
		return false;

	// mkdir -p: cria cada prefixo. "sdmc:" é ponto de montagem (já existe).
	std::string current;
	for (std::size_t i = 0; i <= std::strlen(native); ++i)
	{
		const char c = native[i];
		if (c != '/' && c != '\0')
		{
			current += c;
			continue;
		}
		if (!current.empty() && current.back() != ':')
		{
			if (::mkdir(current.c_str(), 0777) != 0 && errno != EEXIST)
			{
				// Só falha de verdade se o diretório não existir no fim.
				if (!Exists(current.c_str()))
					return false;
			}
		}
		if (c == '/')
			current += '/';
	}
	return true;
}

int Remove(const char* path)
{
	char native[WYD_PATH_MAX];
	if (!ResolveCopy(path, native))
		return -1;
	return ::remove(native);
}

int Rename(const char* old_path, const char* new_path)
{
	char native_old[WYD_PATH_MAX];
	char native_new[WYD_PATH_MAX];
	if (!ResolveCopy(old_path, native_old) || !ResolveCopy(new_path, native_new))
		return -1;
	return ::rename(native_old, native_new);
}

int Stricmp(const char* a, const char* b)
{
	if (!a || !b)
		return a ? 1 : (b ? -1 : 0);
	while (*a && *b)
	{
		const int ca = std::tolower(static_cast<unsigned char>(*a));
		const int cb = std::tolower(static_cast<unsigned char>(*b));
		if (ca != cb)
			return ca - cb;
		++a;
		++b;
	}
	return std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
}

int Strnicmp(const char* a, const char* b, std::size_t n)
{
	if (!a || !b)
		return a ? 1 : (b ? -1 : 0);
	for (std::size_t i = 0; i < n; ++i)
	{
		const int ca = std::tolower(static_cast<unsigned char>(a[i]));
		const int cb = std::tolower(static_cast<unsigned char>(b[i]));
		if (ca != cb)
			return ca - cb;
		if (a[i] == '\0')
			return 0;
	}
	return 0;
}

char* Strupr(char* s)
{
	if (!s)
		return s;
	for (char* p = s; *p; ++p)
		*p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
	return s;
}

char* Strlwr(char* s)
{
	if (!s)
		return s;
	for (char* p = s; *p; ++p)
		*p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
	return s;
}

void NormalizeCrLf(char* text)
{
	if (!text)
		return;
	char* out = text;
	for (const char* p = text; *p; ++p)
	{
		if (p[0] == '\r' && p[1] == '\n')
			continue; // descarta o CR
		*out++ = *p;
	}
	*out = '\0';
}

} // namespace wyd_compat
