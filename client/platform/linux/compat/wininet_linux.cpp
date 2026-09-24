// Backend WinInet real via libcurl.
#include "WinInet.h"

#include <curl/curl.h>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>

namespace {

enum class HandleKind : uint32_t { Session = 1, Download = 2 };

struct HandleBase {
	HandleKind kind {};
};

struct CurlSession : HandleBase {
	CurlSession() { kind = HandleKind::Session; }
};

struct CurlDownload : HandleBase {
	CurlDownload() { kind = HandleKind::Download; }
	std::vector<char> data;
	size_t readPos = 0;
};

std::once_flag g_curlOnce;

void EnsureCurl()
{
	std::call_once(g_curlOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	auto* out = static_cast<std::vector<char>*>(userdata);
	const size_t n = size * nmemb;
	out->insert(out->end(), ptr, ptr + n);
	return n;
}

} // namespace

HINTERNET InternetOpenA(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD)
{
	EnsureCurl();
	return reinterpret_cast<HINTERNET>(new CurlSession{});
}

HINTERNET InternetOpenUrlA(HINTERNET session, LPCSTR url, LPCSTR, DWORD, DWORD, DWORD_PTR)
{
	if (!session || !url)
		return nullptr;
	auto* base = reinterpret_cast<HandleBase*>(session);
	if (base->kind != HandleKind::Session)
		return nullptr;

	EnsureCurl();
	auto* dl = new CurlDownload{};
	CURL* curl = curl_easy_init();
	if (!curl) {
		delete dl;
		return nullptr;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dl->data);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "WYD748-Linux");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	const CURLcode rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (rc != CURLE_OK) {
		delete dl;
		return nullptr;
	}
	return reinterpret_cast<HINTERNET>(dl);
}

BOOL InternetReadFile(HINTERNET file, LPVOID buffer, DWORD numberOfBytesToRead, LPDWORD numberOfBytesRead)
{
	if (!file || !buffer || !numberOfBytesRead)
		return FALSE;
	auto* base = reinterpret_cast<HandleBase*>(file);
	if (base->kind != HandleKind::Download)
		return FALSE;
	auto* dl = static_cast<CurlDownload*>(base);
	const size_t remain = dl->data.size() > dl->readPos ? dl->data.size() - dl->readPos : 0;
	const size_t n = remain < numberOfBytesToRead ? remain : numberOfBytesToRead;
	if (n)
		std::memcpy(buffer, dl->data.data() + dl->readPos, n);
	dl->readPos += n;
	*numberOfBytesRead = static_cast<DWORD>(n);
	return TRUE;
}

BOOL InternetCloseHandle(HINTERNET handle)
{
	if (!handle)
		return FALSE;
	auto* base = reinterpret_cast<HandleBase*>(handle);
	if (base->kind == HandleKind::Session)
		delete static_cast<CurlSession*>(base);
	else if (base->kind == HandleKind::Download)
		delete static_cast<CurlDownload*>(base);
	else
		return FALSE;
	return TRUE;
}

BOOL InternetGetConnectedState(LPDWORD flags, DWORD)
{
	if (flags)
		*flags = 0;
	EnsureCurl();
	CURL* curl = curl_easy_init();
	if (!curl)
		return FALSE;
	curl_easy_setopt(curl, CURLOPT_URL, "https://connectivitycheck.gstatic.com/generate_204");
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
	const CURLcode rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	return rc == CURLE_OK ? TRUE : FALSE;
}
