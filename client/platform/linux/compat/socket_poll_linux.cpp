#include "socket_poll_linux.h"

#ifdef WYD_LINUX

#include "win32_extras.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/select.h>
#include <unistd.h>
#include <unordered_map>

namespace {

struct Watch {
	HWND hwnd = nullptr;
	unsigned msg = 0;
	bool read_pending = false;
};

std::mutex g_mu;
std::unordered_map<unsigned int, Watch> g_watches;

} // namespace

// Fallback fraco para alvos que não linkam winuser_sdl.cpp (ex.: net_smoke).
// O wyd_client fornece a definição forte em winuser_sdl.cpp, que vence na ligação.
// Keep C++ linkage: winuser_sdl.cpp provides the strong C++ implementation
// that pushes into the SDL/Win32 message queue. Using extern "C" here creates
// a different symbol, so the weak no-op wins and FD_READ notifications vanish.
__attribute__((weak)) BOOL PostMessageA(HWND, UINT, WPARAM, LPARAM)
{
	return TRUE;
}

void WYD_Linux_SocketWatch(unsigned int sock, void* hwnd, unsigned msg)
{
	if (!sock || sock == static_cast<unsigned int>(-1) || !hwnd || !msg)
		return;
	const int fd = static_cast<int>(sock);
	const int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	std::lock_guard<std::mutex> lock(g_mu);
	Watch w;
	w.hwnd = reinterpret_cast<HWND>(hwnd);
	w.msg = msg;
	w.read_pending = false;
	g_watches[sock] = w;
}

void WYD_Linux_SocketUnwatch(unsigned int sock)
{
	std::lock_guard<std::mutex> lock(g_mu);
	g_watches.erase(sock);
}

void WYD_Linux_PollSockets()
{
	std::unordered_map<unsigned int, Watch> snap;
	{
		std::lock_guard<std::mutex> lock(g_mu);
		snap = g_watches;
	}
	if (snap.empty())
		return;

	fd_set rfds;
	fd_set efds;
	FD_ZERO(&rfds);
	FD_ZERO(&efds);
	int maxfd = -1;
	for (const auto& kv : snap) {
		const int fd = static_cast<int>(kv.first);
		if (fd < 0)
			continue;
		FD_SET(fd, &rfds);
		FD_SET(fd, &efds);
		if (fd > maxfd)
			maxfd = fd;
	}
	if (maxfd < 0)
		return;

	timeval tv {};
	const int r = select(maxfd + 1, &rfds, nullptr, &efds, &tv);
	if (r <= 0)
		return;

	for (auto& kv : snap) {
		const unsigned int sock = kv.first;
		const int fd = static_cast<int>(sock);
		Watch& w = kv.second;
		if (FD_ISSET(fd, &efds)) {
			PostMessageA(w.hwnd, w.msg, 0, 0);
			std::lock_guard<std::mutex> lock(g_mu);
			g_watches.erase(sock);
			continue;
		}
		if (FD_ISSET(fd, &rfds)) {
			bool already = false;
			{
				std::lock_guard<std::mutex> lock(g_mu);
				auto it = g_watches.find(sock);
				if (it == g_watches.end())
					continue;
				already = it->second.read_pending;
				if (!already)
					it->second.read_pending = true;
			}
			if (!already)
				PostMessageA(w.hwnd, w.msg, 0, 1);
		}
	}
}

void WYD_Linux_SocketAckRead(unsigned int sock)
{
	std::lock_guard<std::mutex> lock(g_mu);
	auto it = g_watches.find(sock);
	if (it != g_watches.end())
		it->second.read_pending = false;
}

#endif
