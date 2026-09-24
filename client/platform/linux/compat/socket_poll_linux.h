#pragma once
// Substitui WSAAsyncSelect no Linux: poll + PostMessage(WM_USER+100, …, FD_READ).

#ifdef WYD_LINUX

void WYD_Linux_SocketWatch(unsigned int sock, void* hwnd, unsigned msg);
void WYD_Linux_SocketUnwatch(unsigned int sock);
void WYD_Linux_PollSockets();
void WYD_Linux_SocketAckRead(unsigned int sock);

#endif
