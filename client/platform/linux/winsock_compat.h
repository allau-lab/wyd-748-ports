#pragma once

// Emula o mínimo de Winsock2 sobre BSD sockets para CPSock no Linux.
#include "wincompat.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>
#include "socket_poll_linux.h"

using SOCKET = int;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR
#endif

struct WSAData
{
	WORD wVersion;
	WORD wHighVersion;
	char szDescription[256];
	char szSystemStatus[128];
	unsigned short iMaxSockets;
	unsigned short iMaxUdpDg;
	char* lpVendorInfo;
};

inline int WSAStartup(WORD, WSAData*)
{
	return 0;
}

inline int WSACleanup()
{
	return 0;
}

inline int WSAGetLastError()
{
	return errno;
}

// No Linux: associa o socket ao hwnd/msg (ex. WM_USER+100) e deixa o poll notificar.
inline int WSAAsyncSelect(SOCKET s, HWND hWnd, unsigned wMsg, long)
{
	if (s == INVALID_SOCKET || !hWnd || !wMsg)
		return -1;
	WYD_Linux_SocketWatch(static_cast<unsigned int>(s), hWnd, wMsg);
	return 0;
}

inline int closesocket(SOCKET s)
{
	WYD_Linux_SocketUnwatch(static_cast<unsigned int>(s));
	return ::close(s);
}

inline int ioctlsocket(SOCKET s, unsigned long cmd, unsigned long* argp)
{
	(void)s;
	(void)cmd;
	(void)argp;
	return 0;
}

// Compat com o layout Microsoft de sockaddr_in (S_un.S_addr).
#ifndef __USE_MISC
// Em glibc, s_addr já existe; o código legado usa S_un.S_addr.
#endif

#ifndef S_un_compat_defined
#define S_un_compat_defined
// Helper: código legado escreve InAddr.sin_addr.S_un.S_addr
// Em Linux mapeamos via macro nos pontos de adaptação do CPSock.
#endif
