#pragma once

// Áudio mínimo via SDL2 — inicializa o subsystem sem bloquear o boot.
// Sem assets de BGM neste lote; falha de device não aborta o client.

#include <string>

struct WYDAudio
{
	bool ready = false;
	std::string status;
};

bool WYD_AudioInit(WYDAudio& out);
void WYD_AudioShutdown(WYDAudio& out);
