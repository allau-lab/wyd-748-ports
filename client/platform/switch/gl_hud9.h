#pragma once

// gl_hud9.h — HUD de diagnóstico do CLIENTE (fase D), desenhado em GLES puro
// por cima do jogo, no default framebuffer, imediatamente antes do SwapWindow.
// Microfonte 3x5 embutida: SEM fonte, SEM asset, SEM depender do SD — a tela é
// o canal de diagnóstico quando não há PC/nxlink (regra 27 da SKILL).
//
// Mostra: fps, texturas criadas/uploadadas, formatos D3D sem conversor (hex) e
// o último erro do backend. Habilite/desabilite com WYD_HUD=0 (default: ON).

namespace wyd9
{

struct Hud9State
{
	unsigned fps = 0;
	unsigned texCreated = 0;
	unsigned texUploaded = 0;
	unsigned badFmtCount = 0;      // formatos distintos sem conversor
	unsigned badFmt[4] = {0, 0, 0, 0};
	const char* lastErr = "";      // string estática do backend ("" = sem erro)
	unsigned frame = 0;            // para piscar o erro
	int w = 0, h = 0;              // tamanho do framebuffer (pixels)
	// WYD_TEX_PROBE / diagnóstico de textura (visível sem FTP)
	int probeMode = -1;            // -1 = off; 0=UV 1=SCREEN 2=NORMAL
	int s3tc = 0;                  // extensão reportada
	unsigned uploadHw = 0;         // DXT via glCompressed*
	unsigned uploadCpu = 0;        // DXT decode CPU / fallback
	unsigned long long lastChk = 0;
	float uvMinU = 0.f, uvMaxU = 0.f, uvMinV = 0.f, uvMaxV = 0.f;
};

// Cria programa/VBO. Exige contexto GL atual. Idempotente.
bool Hud9Init();

// Desenha no framebuffer ATUAL (bind 0 já feito pelo chamador). Não restaura
// estado — o backend reaplica o dele a cada frame (ApplyGlState).
void Hud9Draw(const Hud9State& st);

} // namespace wyd9
