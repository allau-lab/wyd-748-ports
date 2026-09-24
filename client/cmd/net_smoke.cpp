// Smoke de rede POSIX (Fase B): conecta, envia INIT_CODE, encerra.
// Não depende de Wayland/DX. Use: ./net_smoke <host> <port>

#include "winsock_compat.h"
#include "CPSock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

char EncodeByte[4] = {};
HWND hWndMain = nullptr;
unsigned int CurrentTime = 0;
unsigned int LastSendTime = 0;

int main(int argc, char** argv)
{
	const char* host = argc > 1 ? argv[1] : "127.0.0.1";
	const int port = argc > 2 ? std::atoi(argv[2]) : 8281;

	CPSock sock;
	if (!sock.WSAInitialize())
	{
		std::fprintf(stderr, "[net_smoke] WSAInitialize falhou\n");
		return 1;
	}

	std::fprintf(stderr, "[net_smoke] conectando %s:%d ...\n", host, port);
	unsigned int fd = sock.ConnectServer(const_cast<char*>(host), port, 0, 0);
	if (!fd)
	{
		std::fprintf(stderr, "[net_smoke] ConnectServer falhou\n");
		return 2;
	}

	std::fprintf(stderr, "[net_smoke] OK sock=%u INIT_CODE enviado\n", fd);
	sock.CloseSocket();
	return 0;
}
