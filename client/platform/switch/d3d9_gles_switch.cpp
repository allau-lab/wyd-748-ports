// d3d9_gles_switch.cpp — IDirect3D9/IDirect3DDevice9 sobre GLES3 (Nintendo Switch).
//
// Mesmo contrato COM do platform/android/d3d9_android.cpp (compila contra o
// d3d9.h do dxvk-native), trocando Vulkan por OpenGL ES 3.2 via SDL3.
//
// Estratégia de render (fase C do SWITCH_PORT.md, NintendoSwitch/01-D3D9-INVENTORY.md):
//   - Fixed-function emulada por 1 VS + 1 FS cobrindo a superfície que o jogo
//     usa: 3 estágios de textura (COLOROP DISABLE/SELECTARG1/2/MODULATE/
//     MODULATE2X, args DIFFUSE/CURRENT/TEXTURE/TFACTOR), LIGHTING (4 luzes),
//     alpha-test, fog linear/exp/exp2, FVF XYZ/XYZRHW/NORMAL/DIFFUSE/TEX1/TEX2
//     e vertex declaration via CreateVertexDeclaration.
//   - Buffers: repack para layout fixo (pos3 n3 c4ub uv0 uv1) num VBO dinâmico.
//     VB/IB vivem em memória de sistema (o Lock/Unlock do cliente já é CPU).
//   - Texturas: cópia CPU no LockRect + upload no primeiro uso. DXT1/3/5 sobe
//     comprimido quando a extensão S3TC existe; senão decodifica na CPU
//     (mesma decisão de runtime do port SHAR — docs/PORT-STUDY-SHAR-SWITCH.md).
//   - Vertex/pixel shaders do jogo (skinmesh/vseffect/pseffect): bytecode DX9
//     validado e guardado; o pipeline shader real é a fase D. Enquanto isso o
//     desenho segue por FFP e avisa UMA vez ([VS-BYPASS]) — sem stub mudo.
//
// Sem stub silencioso (PORT.md): o que ainda não tem efeito está logado aqui
// (VS-BYPASS, formato de textura sem conversor). Render targets: FBO real.

#ifdef WYD_SWITCH

#include "gl_hud9.h"
#include "vs11_glsl.h"

#include <d3d9.h>

#ifdef interface
#undef interface
#endif

#include <SDL3/SDL.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <sys/stat.h>
#include <utility>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef D3D_OK
#define D3D_OK S_OK
#endif

namespace
{

bool g_s3tcAvailable = false;
int g_probeMode = 2; // default NORMAL; X cicla 0=UV 1=SCREEN 2=NORMAL
unsigned g_uploadHw = 0;
unsigned g_uploadCpu = 0;
unsigned long long g_lastChk = 0;
float g_uvMinU = 0.f, g_uvMaxU = 0.f, g_uvMinV = 0.f, g_uvMaxV = 0.f;
bool g_uvSampleValid = false;

// ---------------------------------------------------------------------------
// HUD (fase D): diagnóstico na tela, sem PC — regra 27 da SKILL
// ---------------------------------------------------------------------------
unsigned g_texCreated = 0;
unsigned g_texUploaded = 0;
unsigned g_badFmtCount = 0;
DWORD g_badFmt[4] = {};
char g_lastErr[48] = {};
unsigned g_fps = 0;

extern "C" void WYD_TexProbeCycle(void)
{
#if defined(WYD_TEX_PROBE) && WYD_TEX_PROBE
	g_probeMode = (g_probeMode + 1) % 3;
#endif
}

void HudErr(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(g_lastErr, sizeof(g_lastErr), fmt, ap);
	va_end(ap);
}

void RecordBadFmt(DWORD fmt)
{
	for (unsigned i = 0; i < g_badFmtCount && i < 4; ++i)
		if (g_badFmt[i] == fmt)
			return; // já registrado
	if (g_badFmtCount < 4)
		g_badFmt[g_badFmtCount++] = fmt;
	HudErr("FMT 0x%04X", (unsigned)fmt);
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------
void SLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void SLog(const char* fmt, ...)
{
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	std::fputs(line, stderr);
	std::fputc('\n', stderr);
	std::fflush(stderr);
	for (const char* path : {
		"sdmc:/switch/client748/wyd748_diag.txt",
		"sdmc:/switch/client748/wyd748_ev.txt", // sobrevive ao truncate do diag
		"sdmc:/switch/wyd/wyd_client_diag.txt",
		"wyd748_diag.txt",
	})
	{
		if (FILE* f = std::fopen(path, "ab"))
		{
			std::fputs(line, f);
			std::fputc('\n', f);
			std::fclose(f);
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Conversões D3D -> GL
// ---------------------------------------------------------------------------
GLenum BlendToGL(DWORD v)
{
	switch (v)
	{
		case D3DBLEND_ZERO: return GL_ZERO;
		case D3DBLEND_ONE: return GL_ONE;
		case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
		case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
		case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
		case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
		case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
		case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
		case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
		case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
		case D3DBLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
		default: return GL_ONE;
	}
}

GLenum CmpToGL(DWORD v)
{
	switch (v)
	{
		case D3DCMP_NEVER: return GL_NEVER;
		case D3DCMP_LESS: return GL_LESS;
		case D3DCMP_EQUAL: return GL_EQUAL;
		case D3DCMP_LESSEQUAL: return GL_LEQUAL;
		case D3DCMP_GREATER: return GL_GREATER;
		case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
		case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
		case D3DCMP_ALWAYS: return GL_ALWAYS;
		default: return GL_LEQUAL;
	}
}

GLenum TexFilterToGL(DWORD v)
{
	switch (v)
	{
		case D3DTEXF_POINT: return GL_NEAREST;
		// Só o nível 0 é enviado ao GL: filtro *_MIPMAP_* deixaria a textura
		// incompleta e o sampler devolveria preto.
		case D3DTEXF_LINEAR:
		case D3DTEXF_ANISOTROPIC:
		default: return GL_LINEAR;
	}
}

GLenum PrimToGL(D3DPRIMITIVETYPE p)
{
	switch (p)
	{
		case D3DPT_POINTLIST: return GL_POINTS;
		case D3DPT_LINELIST: return GL_LINES;
		case D3DPT_LINESTRIP: return GL_LINE_STRIP;
		case D3DPT_TRIANGLELIST: return GL_TRIANGLES;
		case D3DPT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
		case D3DPT_TRIANGLEFAN: return GL_TRIANGLE_FAN;
		default: return GL_TRIANGLES;
	}
}

// Quantidade de vértices consumidos por um draw (índices ou array).
UINT VertsForPrim(D3DPRIMITIVETYPE p, UINT primCount)
{
	switch (p)
	{
		case D3DPT_POINTLIST: return primCount;
		case D3DPT_LINELIST: return primCount * 2;
		case D3DPT_LINESTRIP: return primCount + 1;
		case D3DPT_TRIANGLELIST: return primCount * 3;
		case D3DPT_TRIANGLESTRIP:
		case D3DPT_TRIANGLEFAN: return primCount + 2;
		default: return primCount * 3;
	}
}

// ---------------------------------------------------------------------------
// DXT -> RGBA (BC1/BC2/BC3) — caminho quando a GPU não expõe S3TC
// ---------------------------------------------------------------------------
struct DxtBlock565
{
	uint16_t c0, c1;
	uint32_t idx;
};

void DecodeColor565(const DxtBlock565& b, uint8_t out[16][4], bool oneBitAlpha)
{
	auto expand5 = [](uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); };
	auto expand6 = [](uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); };
	uint8_t cols[4][4];
	cols[0][0] = expand5((b.c0 >> 11) & 0x1F);
	cols[0][1] = expand6((b.c0 >> 5) & 0x3F);
	cols[0][2] = expand5(b.c0 & 0x1F);
	cols[0][3] = 255;
	cols[1][0] = expand5((b.c1 >> 11) & 0x1F);
	cols[1][1] = expand6((b.c1 >> 5) & 0x3F);
	cols[1][2] = expand5(b.c1 & 0x1F);
	cols[1][3] = 255;
	if (!oneBitAlpha || b.c0 > b.c1)
	{
		for (int i = 0; i < 3; ++i)
		{
			cols[2][i] = (uint8_t)((2 * cols[0][i] + cols[1][i]) / 3);
			cols[3][i] = (uint8_t)((cols[0][i] + 2 * cols[1][i]) / 3);
		}
		cols[2][3] = cols[3][3] = 255;
	}
	else
	{
		for (int i = 0; i < 3; ++i)
			cols[2][i] = (uint8_t)((cols[0][i] + cols[1][i]) / 2);
		cols[2][3] = 255;
		cols[3][0] = cols[3][1] = cols[3][2] = 0;
		cols[3][3] = 0;
	}
	for (int p = 0; p < 16; ++p)
	{
		const uint32_t sel = (b.idx >> (2 * p)) & 0x3;
		out[p][0] = cols[sel][0];
		out[p][1] = cols[sel][1];
		out[p][2] = cols[sel][2];
		out[p][3] = cols[sel][3];
	}
}

void DecodeBC1(const uint8_t* src, uint8_t* dst, int w, int h)
{
	const int bw = (w + 3) / 4, bh = (h + 3) / 4;
	for (int by = 0; by < bh; ++by)
		for (int bx = 0; bx < bw; ++bx)
		{
			DxtBlock565 blk;
			std::memcpy(&blk, src + (size_t)(by * bw + bx) * 8, 8);
			uint8_t px[16][4];
			DecodeColor565(blk, px, true);
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
				{
					const int dx = bx * 4 + x, dy = by * 4 + y;
					if (dx < w && dy < h)
						std::memcpy(dst + ((size_t)dy * w + dx) * 4, px[y * 4 + x], 4);
				}
		}
}

void DecodeBC2(const uint8_t* src, uint8_t* dst, int w, int h)
{
	const int bw = (w + 3) / 4, bh = (h + 3) / 4;
	for (int by = 0; by < bh; ++by)
		for (int bx = 0; bx < bw; ++bx)
		{
			const uint8_t* ablk = src + (size_t)(by * bw + bx) * 16;
			DxtBlock565 blk;
			std::memcpy(&blk, ablk + 8, 8);
			uint8_t px[16][4];
			DecodeColor565(blk, px, false);
			for (int p = 0; p < 16; ++p)
				px[p][3] = (uint8_t)(((ablk[p / 2] >> ((p % 2) * 4)) & 0xF) * 17);
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
				{
					const int dx = bx * 4 + x, dy = by * 4 + y;
					if (dx < w && dy < h)
						std::memcpy(dst + ((size_t)dy * w + dx) * 4, px[y * 4 + x], 4);
				}
		}
}

void DecodeBC3(const uint8_t* src, uint8_t* dst, int w, int h)
{
	const int bw = (w + 3) / 4, bh = (h + 3) / 4;
	for (int by = 0; by < bh; ++by)
		for (int bx = 0; bx < bw; ++bx)
		{
			const uint8_t* ablk = src + (size_t)(by * bw + bx) * 16;
			DxtBlock565 blk;
			std::memcpy(&blk, ablk + 8, 8);
			uint8_t px[16][4];
			DecodeColor565(blk, px, false);
			const uint8_t a0 = ablk[0], a1 = ablk[1];
			uint8_t alpha[8];
			alpha[0] = a0;
			alpha[1] = a1;
			if (a0 > a1)
			{
				for (int i = 0; i < 6; ++i)
					alpha[2 + i] = (uint8_t)(((6 - i) * a0 + (1 + i) * a1) / 7);
			}
			else
			{
				for (int i = 0; i < 4; ++i)
					alpha[2 + i] = (uint8_t)(((4 - i) * a0 + (1 + i) * a1) / 5);
				alpha[6] = 0;
				alpha[7] = 255;
			}
			uint64_t bits = 0;
			std::memcpy(&bits, ablk + 2, 6);
			for (int p = 0; p < 16; ++p)
				px[p][3] = alpha[(bits >> (3 * p)) & 0x7];
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
				{
					const int dx = bx * 4 + x, dy = by * 4 + y;
					if (dx < w && dy < h)
						std::memcpy(dst + ((size_t)dy * w + dx) * 4, px[y * 4 + x], 4);
				}
		}
}

size_t DxtBytes(DWORD fmt, int w, int h)
{
	const size_t bw = (w + 3) / 4, bh = (h + 3) / 4;
	return fmt == D3DFMT_DXT1 ? bw * bh * 8 : bw * bh * 16;
}

size_t FmtBpp(DWORD fmt)
{
	switch (fmt)
	{
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
		case D3DFMT_A8B8G8R8: return 4;
		case D3DFMT_A4R4G4B4:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_X1R5G5B5:
		case D3DFMT_R5G6B5:
		case D3DFMT_A8L8: return 2;
		default: return 4;
	}
}

// ---------------------------------------------------------------------------
// Shaders FFP (GLES3)
// ---------------------------------------------------------------------------
const char* kFfpVS = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
layout(location=3) in vec2 aUV0;
layout(location=4) in vec2 aUV1;
uniform mat4 uWVP;
uniform mat4 uWorld;
uniform mat4 uWV;
uniform int uRhw;
uniform float uRhwYFlip;
uniform vec2 uRhwVP; // dims do alvo corrente (RHW chega em PIXELS de tela)
uniform int uLighting;
uniform int uColorVertex;
uniform mat4 uTexM0;
uniform mat4 uTexM1;
uniform int uTexXform0; // 0=off, senao D3DTTFF_COUNT*
uniform int uTexXform1;
uniform vec4 uMatDiffuse;
uniform vec4 uMatAmbient;
uniform vec4 uMatEmissive;
uniform vec4 uGlobalAmbient;
// Por luz: Pos.xyz + w(0=direcional,1=pontual); Dir.xyz = -Direction;
// Diff.a = ligada; Att = (a0, a1, a2, range).
uniform vec4 uLightPos[8];
uniform vec4 uLightDir[8];
uniform vec4 uLightDiff[8];
uniform vec4 uLightAmb[8];
uniform vec4 uLightAtt[8];
out vec4 vColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogDepth;
void main() {
  vec4 wp = uWorld * vec4(aPos, 1.0);
  if (uRhw == 1) {
    // RHW do D3D9 é coordenada de tela: pixels -> NDC; uRhwYFlip corrige a
    // origem (D3D top-left vs GL bottom-left) por alvo (backbuffer/RT).
    vec2 ndc = vec2(aPos.x / max(uRhwVP.x, 1.0), aPos.y / max(uRhwVP.y, 1.0)) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, ndc.y * uRhwYFlip, clamp(aPos.z, 0.0, 1.0), 1.0);
    vColor = aColor;
    vFogDepth = 0.0;
  } else {
    gl_Position = uWVP * vec4(aPos, 1.0);
    vec3 n = normalize(mat3(uWorld) * aNormal);
    vec4 col;
    if (uLighting == 1) {
      vec4 base = (uColorVertex == 1) ? aColor : uMatDiffuse;
      vec3 amb = uGlobalAmbient.rgb;
      vec3 dif = vec3(0.0);
      for (int i = 0; i < 8; i++) {
        if (uLightDiff[i].a <= 0.0)
          continue;
        vec3 L;
        float att = 1.0;
        if (uLightPos[i].w < 0.5) {
          L = normalize(uLightDir[i].xyz);
        } else {
          vec3 d = uLightPos[i].xyz - wp.xyz;
          float dist = length(d);
          if (dist > uLightAtt[i].w)
            continue;
          L = d / max(dist, 1e-5);
          att = 1.0 / max(uLightAtt[i].x + uLightAtt[i].y * dist + uLightAtt[i].z * dist * dist, 1e-5);
        }
        amb += uLightAmb[i].rgb * att;
        dif += uLightDiff[i].rgb * max(dot(n, L), 0.0) * att;
      }
      col.rgb = uMatEmissive.rgb + amb * uMatAmbient.rgb + dif * base.rgb;
      col.a = base.a;
    } else {
      // D3D9 sem iluminação: cor do vértice direta (branco se o vértice não tem cor).
      col = aColor;
    }
    vColor = clamp(col, 0.0, 1.0);
    // Fog de vértice D3D: distância no espaço de câmera (z da view), não z de mundo.
    vFogDepth = (uWV * vec4(aPos, 1.0)).z;
  }
  // D3DTS_TEXTUREn + D3DTSS_TEXTURETRANSFORMFLAGS (UV scale/offset do FFP).
  if (uTexXform0 != 0) {
    vec4 t = uTexM0 * vec4(aUV0, 0.0, 1.0);
    vUV0 = t.xy;
  } else {
    vUV0 = aUV0;
  }
  if (uTexXform1 != 0) {
    vec4 t = uTexM1 * vec4(aUV1, 0.0, 1.0);
    vUV1 = t.xy;
  } else {
    vUV1 = aUV1;
  }
}
)GLSL";

const char* kFfpFS = R"GLSL(#version 300 es
precision highp float;
precision highp int;
in vec4 vColor;
in vec2 vUV0;
in vec2 vUV1;
in float vFogDepth;
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform sampler2D uTex2;
// Escalares (não arrays): Mesa 20.1/NV120 ignora glUniform1iv em int[] —
// uTexOn ficava 0 → tex=1.0 → só diffuse (silhueta flat).
uniform int uTexOn0;
uniform int uTexOn1;
uniform int uTexOn2;
// Estágio empacotado: op | arg1 << 8 | arg2 << 16 (D3DTOP_* / D3DTA_* com modificadores).
uniform int uColorSt0;
uniform int uColorSt1;
uniform int uColorSt2;
uniform int uAlphaSt0;
uniform int uAlphaSt1;
uniform int uAlphaSt2;
uniform int uStageUv0;
uniform int uStageUv1;
uniform int uStageUv2;
uniform vec4 uTexFactor;
uniform int uRhw;
uniform int uAlphaTest;
uniform float uAlphaRef;
uniform int uAlphaFunc;
uniform int uFogEnable;
uniform int uFogMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform int uProbeMode; // 0=UV 1=SCREEN 2=NORMAL
// Mesmo nome do VS (dims do alvo). Obrigatório: ramo PROBE SCREEN usa isto.
uniform vec2 uRhwVP;
out vec4 fragColor;
vec4 arg(int sel, vec4 t, vec4 diff, vec4 cur) {
  int s = sel & 15;
  vec4 v = diff;
  if (s == 1) v = cur;
  else if (s == 2) v = t;
  else if (s == 3) v = uTexFactor;
  else if (s == 4) v = vec4(0.0, 0.0, 0.0, 1.0); // SPECULAR (não emulado)
  if ((sel & 32) != 0) v = vec4(v.a);  // D3DTA_ALPHAREPLICATE
  if ((sel & 16) != 0) v = 1.0 - v;    // D3DTA_COMPLEMENT
  return v;
}
vec4 opv(int op, vec4 a, vec4 b, vec4 t, vec4 diff, vec4 cur) {
  if (op == 2) return a;                               // SELECTARG1
  if (op == 3) return b;                               // SELECTARG2
  if (op == 4) return a * b;                           // MODULATE
  if (op == 5) return a * b * 2.0;                     // MODULATE2X
  if (op == 6) return a * b * 4.0;                     // MODULATE4X
  if (op == 7) return a + b;                           // ADD
  if (op == 8) return a + b - 0.5;                     // ADDSIGNED
  if (op == 9) return (a + b - 0.5) * 2.0;             // ADDSIGNED2X
  if (op == 10) return a - b;                          // SUBTRACT
  if (op == 11) return a + b - a * b;                  // ADDSMOOTH
  if (op == 12) return mix(b, a, diff.a);              // BLENDDIFFUSEALPHA
  if (op == 13) return mix(b, a, t.a);                 // BLENDTEXTUREALPHA
  if (op == 14) return mix(b, a, uTexFactor.a);        // BLENDFACTORALPHA
  if (op == 15) return a + b * (1.0 - t.a);            // BLENDTEXTUREALPHAPM
  if (op == 16) return mix(b, a, cur.a);               // BLENDCURRENTALPHA
  if (op == 18) return vec4(a.rgb + a.a * b.rgb, a.a); // MODULATEALPHA_ADDCOLOR
  if (op == 19) return vec4(a.rgb * b.rgb + a.a, a.a); // MODULATECOLOR_ADDALPHA
  if (op == 20) return vec4((1.0 - a.a) * b.rgb + a.rgb, a.a);
  if (op == 21) return vec4((1.0 - a.rgb) * b.rgb + a.a, a.a);
  if (op == 24) return vec4(clamp(dot(a.rgb - 0.5, b.rgb - 0.5) * 4.0, 0.0, 1.0)); // DOTPRODUCT3
  return a;
}
vec4 stage(int cst, int ast, vec4 t, vec4 diff, vec4 cur) {
  int cop = cst & 255;
  vec3 rgb = opv(cop, arg((cst >> 8) & 255, t, diff, cur), arg((cst >> 16) & 255, t, diff, cur), t, diff, cur).rgb;
  int aop = ast & 255;
  float a = cur.a;
  if (aop > 1)
    a = opv(aop, arg((ast >> 8) & 255, t, diff, cur), arg((ast >> 16) & 255, t, diff, cur), t, diff, cur).a;
  return clamp(vec4(rgb, a), 0.0, 1.0);
}
void main() {
  // Probe visual (WYD_TEX_PROBE): diagnóstico sem FTP.
  if (uProbeMode == 0) {
    fragColor = vec4(fract(vUV0), 0.0, 1.0);
    return;
  }
  if (uProbeMode == 1) {
    // Normaliza com uRhwVP; fallback docked 1280x720 se uniform vier zero.
    vec2 vp = max(uRhwVP, vec2(1.0));
    vec2 suv = gl_FragCoord.xy / vp;
    fragColor = (uTexOn0 == 1) ? texture(uTex0, suv) : vec4(1.0, 0.0, 1.0, 1.0);
    return;
  }
  vec4 diff = vColor;
  // D3D9: estágio sem textura amostra (0,0,0,1).
  const vec4 kNoTex = vec4(0.0, 0.0, 0.0, 1.0);
  vec4 t0 = (uTexOn0 == 1) ? texture(uTex0, (uStageUv0 == 1) ? vUV1 : vUV0) : kNoTex;
  vec4 t1 = (uTexOn1 == 1) ? texture(uTex1, (uStageUv1 == 1) ? vUV1 : vUV0) : kNoTex;
  vec4 t2 = (uTexOn2 == 1) ? texture(uTex2, (uStageUv2 == 1) ? vUV1 : vUV0) : kNoTex;
  // COLOROP DISABLE encerra a cadeia; no estágio 0 a saída é o diffuse.
  vec4 c = diff;
  if ((uColorSt0 & 255) > 1) {
    c = stage(uColorSt0, uAlphaSt0, t0, diff, diff);
    if ((uColorSt1 & 255) > 1) {
      c = stage(uColorSt1, uAlphaSt1, t1, diff, c);
      if ((uColorSt2 & 255) > 1)
        c = stage(uColorSt2, uAlphaSt2, t2, diff, c);
    }
  }
  if (uAlphaTest == 1) {
    bool pass = true;
    if (uAlphaFunc == 1) pass = false;
    else if (uAlphaFunc == 2) pass = c.a < uAlphaRef;
    else if (uAlphaFunc == 3) pass = c.a == uAlphaRef;
    else if (uAlphaFunc == 4) pass = c.a <= uAlphaRef;
    else if (uAlphaFunc == 5) pass = c.a > uAlphaRef;
    else if (uAlphaFunc == 6) pass = c.a != uAlphaRef;
    else if (uAlphaFunc == 7) pass = c.a >= uAlphaRef;
    if (!pass) discard;
  }
  if (uFogEnable == 1) {
    float f = 1.0;
    if (uFogMode == 3) f = clamp((uFogEnd - vFogDepth) / (uFogEnd - uFogStart), 0.0, 1.0);
    else if (uFogMode == 1) f = clamp(exp(-uFogDensity * vFogDepth), 0.0, 1.0);
    else if (uFogMode == 2) f = clamp(exp(-uFogDensity * uFogDensity * vFogDepth * vFogDepth), 0.0, 1.0);
    else if (uFogMode == 100) f = clamp(vFogDepth, 0.0, 1.0); // vertex shader: oFog já é o fator
    c.rgb = mix(uFogColor, c.rgb, f);
  }
  fragColor = c;
}
)GLSL";

struct FfpProgram
{
	GLuint prog = 0;
	GLint uWVP = -1, uWorld = -1, uRhw = -1, uRhwYFlip = -1, uRhwVP = -1, uLighting = -1, uColorVertex = -1;
	GLint uMatDiffuse = -1, uMatAmbient = -1, uMatEmissive = -1, uGlobalAmbient = -1;
	GLint uLightPos = -1, uLightDiff = -1, uLightDir = -1, uLightAmb = -1, uLightAtt = -1;
	GLint uTex0 = -1, uTex1 = -1, uTex2 = -1;
	GLint uTexM0 = -1, uTexM1 = -1, uTexXform0 = -1, uTexXform1 = -1;
	GLint uTexOn[3] = { -1, -1, -1 };
	GLint uColorSt[3] = { -1, -1, -1 };
	GLint uAlphaSt[3] = { -1, -1, -1 };
	GLint uWV = -1;
	GLint uStageUv[3] = { -1, -1, -1 };
	GLint uTexFactor = -1;
	GLint uAlphaTest = -1, uAlphaRef = -1, uAlphaFunc = -1;
	GLint uFogEnable = -1, uFogMode = -1, uFogColor = -1, uFogStart = -1, uFogEnd = -1, uFogDensity = -1;
	GLint uProbeMode = -1;
	GLuint vbo = 0;
	GLuint vao = 0;
};

GLuint CompileShader(GLenum type, const char* src)
{
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 1, &src, nullptr);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[1024] = {};
		glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
		SLog("[GLES9] shader compile FALHOU: %s", log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

// vsSrc: VS do FFP ou GLSL traduzido de um vertex shader D3D9; o FS é sempre o do FFP.
bool BuildFfp(FfpProgram& p, const char* vsSrc = kFfpVS, bool quiet = false)
{
	GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFfpFS);
	if (!vs || !fs)
		return false;
	p.prog = glCreateProgram();
	glAttachShader(p.prog, vs);
	glAttachShader(p.prog, fs);
	glLinkProgram(p.prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(p.prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[1024] = {};
		glGetProgramInfoLog(p.prog, sizeof(log), nullptr, log);
		SLog("[GLES9] program link FALHOU: %s", log);
		return false;
	}
	auto loc = [&](const char* name) -> GLint
	{
		const GLint l = glGetUniformLocation(p.prog, name);
		if (l < 0 && !quiet)
			SLog("[GLES9] uniform ausente: %s", name);
		return l;
	};
	// GLES3: arrays ativos exigem "nome[0]" — "uTexOn" sozinho pode retornar -1
	// (sintoma: malha só com lighting, castelo cinza/verde sem textura).
	p.uWVP = loc("uWVP");
	p.uWorld = loc("uWorld");
	p.uRhw = loc("uRhw");
	p.uRhwYFlip = loc("uRhwYFlip");
	p.uRhwVP = loc("uRhwVP");
	p.uLighting = loc("uLighting");
	p.uColorVertex = loc("uColorVertex");
	p.uMatDiffuse = loc("uMatDiffuse");
	p.uMatAmbient = loc("uMatAmbient");
	p.uMatEmissive = loc("uMatEmissive");
	p.uGlobalAmbient = loc("uGlobalAmbient");
	p.uLightPos = loc("uLightPos[0]");
	if (p.uLightPos < 0)
		p.uLightPos = loc("uLightPos");
	p.uLightDiff = loc("uLightDiff[0]");
	if (p.uLightDiff < 0)
		p.uLightDiff = loc("uLightDiff");
	p.uLightDir = loc("uLightDir[0]");
	p.uLightAmb = loc("uLightAmb[0]");
	p.uLightAtt = loc("uLightAtt[0]");
	p.uTex0 = loc("uTex0");
	p.uTex1 = loc("uTex1");
	p.uTex2 = loc("uTex2");
	p.uTexM0 = loc("uTexM0");
	p.uTexM1 = loc("uTexM1");
	p.uTexXform0 = loc("uTexXform0");
	p.uTexXform1 = loc("uTexXform1");
	p.uTexOn[0] = loc("uTexOn0");
	p.uTexOn[1] = loc("uTexOn1");
	p.uTexOn[2] = loc("uTexOn2");
	p.uColorSt[0] = loc("uColorSt0");
	p.uColorSt[1] = loc("uColorSt1");
	p.uColorSt[2] = loc("uColorSt2");
	p.uAlphaSt[0] = loc("uAlphaSt0");
	p.uAlphaSt[1] = loc("uAlphaSt1");
	p.uAlphaSt[2] = loc("uAlphaSt2");
	p.uWV = loc("uWV");
	p.uStageUv[0] = loc("uStageUv0");
	p.uStageUv[1] = loc("uStageUv1");
	p.uStageUv[2] = loc("uStageUv2");
	p.uTexFactor = loc("uTexFactor");
	p.uAlphaTest = loc("uAlphaTest");
	p.uAlphaRef = loc("uAlphaRef");
	p.uAlphaFunc = loc("uAlphaFunc");
	p.uFogEnable = loc("uFogEnable");
	p.uFogMode = loc("uFogMode");
	p.uFogColor = loc("uFogColor");
	p.uFogStart = loc("uFogStart");
	p.uFogEnd = loc("uFogEnd");
	p.uFogDensity = loc("uFogDensity");
	p.uProbeMode = loc("uProbeMode");
	glUseProgram(p.prog);
	if (p.uTex0 >= 0)
		glUniform1i(p.uTex0, 0);
	if (p.uTex1 >= 0)
		glUniform1i(p.uTex1, 1);
	if (p.uTex2 >= 0)
		glUniform1i(p.uTex2, 2);
	glUseProgram(0);
	if (!quiet)
		SLog("[GLES9] FFP ok v%s texOn0=%d stageOp0=%d uTex0=%d uRhwVP=%d uProbe=%d",
#if defined(WYD_SWITCH_VERSION)
			WYD_SWITCH_VERSION,
#else
			"?",
#endif
			p.uTexOn[0], p.uColorSt[0], p.uTex0, p.uRhwVP, p.uProbeMode);
	glGenVertexArrays(1, &p.vao);
	glGenBuffers(1, &p.vbo);
	return p.uTexOn[0] >= 0 && p.uColorSt[0] >= 0 && p.uTex0 >= 0;
}

// ---------------------------------------------------------------------------
// Recursos
// ---------------------------------------------------------------------------
struct D3D9RT;

// ---------------------------------------------------------------------------
// Render target (FBO real): propriedade COMPARTILHADA entre a textura RT, suas
// superfícies e o device (estilo COM). Liberado quando a última ref cai.
// ---------------------------------------------------------------------------
struct D3D9RT
{
	UINT w = 0, h = 0;
	GLuint tex = 0, fbo = 0, depth = 0;
	std::vector<uint8_t> cpu; // RGBA p/ LockRect/GetRenderTargetData
	ULONG refCount = 1;

	void AddRef() { ++refCount; }
	void Release()
	{
		if (--refCount)
			return;
		if (fbo)
			glDeleteFramebuffers(1, &fbo);
		if (depth)
			glDeleteRenderbuffers(1, &depth);
		if (tex)
			glDeleteTextures(1, &tex);
		delete this;
	}
	bool Ensure()
	{
		if (fbo)
			return true;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glGenRenderbuffers(1, &depth);
		glBindRenderbuffer(GL_RENDERBUFFER, depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
		const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		if (!ok)
		{
			// FBO inutilizável: destruir e ZERAR os handles, senão o guard
			// `if (fbo) return true` rebindaria um FBO incompleto para sempre.
			SLog("[GLES9] FBO %ux%u incompleto", (unsigned)w, (unsigned)h);
			HudErr("FBO %ux%u BAD", (unsigned)w, (unsigned)h);
			glDeleteFramebuffers(1, &fbo);
			glDeleteRenderbuffers(1, &depth);
			glDeleteTextures(1, &tex);
			fbo = depth = tex = 0;
		}
		return ok;
	}
	void ResolveToCpu() // FBO → RAM (evita LockRect-preto do GLES — lição SHAR §6)
	{
		if (!Ensure())
			return;
		if (cpu.size() < (size_t)w * h * 4)
			cpu.resize((size_t)w * h * 4);
		// Preserva o bind corrente: LockRect pode ocorrer com RT ativo no meio
		// do frame — desync do alvo quebraria os draws seguintes.
		GLint prevFbo = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, cpu.data());
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
	}
};

struct D3D9Tex : IDirect3DTexture9
{
	UINT w = 0, h = 0;
	DWORD fmt = D3DFMT_A8R8G8B8;
	std::vector<uint8_t> cpu; // dados no formato D3D (DXT = blocos comprimidos)
	size_t pitch = 0;
	GLuint gl = 0;
	bool dirty = false;
	DWORD lockFlags = 0;
	DWORD usage = 0;
	D3D9RT* rt = nullptr; // != nullptr p/ textura com USAGE_RENDERTARGET (FBO próprio)
	ULONG refCount = 1;
	~D3D9Tex();

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return S_OK; }
	DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { return p; }
	DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
	void STDMETHODCALLTYPE PreLoad() override {}
	D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }
	DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { return l; }
	DWORD STDMETHODCALLTYPE GetLOD() override { return 0; }
	DWORD STDMETHODCALLTYPE GetLevelCount() override { return 1; }
	HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return S_OK; }
	D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_NONE; }
	void STDMETHODCALLTYPE GenerateMipSubLevels() override {}
	HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT, D3DSURFACE_DESC* d) override
	{
		if (d) { d->Width = w; d->Height = h; d->Format = (D3DFORMAT)fmt; d->Type = D3DRTYPE_TEXTURE; d->Usage = usage; d->Pool = (usage & D3DUSAGE_RENDERTARGET) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED; d->MultiSampleType = D3DMULTISAMPLE_NONE; }
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT, IDirect3DSurface9** pp) override;
	HRESULT STDMETHODCALLTYPE LockRect(UINT, D3DLOCKED_RECT* lr, const RECT*, DWORD flags) override
	{
		if (!lr || cpu.empty())
			return E_POINTER;
		lockFlags = flags;
		lr->Pitch = (INT)pitch;
		lr->pBits = cpu.data();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE UnlockRect(UINT) override
	{
		// READONLY não suja — evita re-upload a cada frame (TX 3000+/s → 21 FPS).
		if (!(lockFlags & D3DLOCK_READONLY))
			dirty = true;
		lockFlags = 0;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*) override { dirty = true; return S_OK; }
};

struct D3D9Surf : IDirect3DSurface9
{
	D3D9Tex* owner = nullptr;
	D3D9RT* rt = nullptr;    // != nullptr p/ superfície RT color
	bool backbuf = false;    // surf do backbuffer (LockRect = readback GLES)
	UINT w = 0, h = 0;
	DWORD fmt = D3DFMT_A8R8G8B8;
	std::vector<uint8_t> cpu; // backbuffer: preenchido no LockRect (readback)
	ULONG refCount = 1;
	DWORD lockFlags = 0;

	// Superfície RT segura a própria ref do D3D9RT (estilo COM) — sem este
	// dtor, CreateRenderTarget() vaza o RT inteiro quando a surf morre.
	// Superfície não-RT segura ref do container (semântica D3D9: a textura
	// vive enquanto qualquer surf sua viva) — sem isso, surf sobrevivente
	// leria ponteiro pendente em GetDesc/LockRect.
	~D3D9Surf()
	{
		if (rt)
			rt->Release();
		if (owner)
		{
			auto* o = owner;
			owner = nullptr;
			o->Release();
		}
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return S_OK; }
	DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
	DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
	void STDMETHODCALLTYPE PreLoad() override {}
	D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_SURFACE; }
	HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void** pp) override { if (pp) *pp = nullptr; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* d) override
	{
		if (d) { d->Width = w; d->Height = h; d->Format = (D3DFORMAT)fmt; d->Type = D3DRTYPE_SURFACE; d->Usage = rt ? D3DUSAGE_RENDERTARGET : 0; d->Pool = (owner && !(owner->usage & D3DUSAGE_RENDERTARGET)) ? D3DPOOL_MANAGED : D3DPOOL_DEFAULT; d->MultiSampleType = D3DMULTISAMPLE_NONE; }
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* lr, const RECT*, DWORD flags) override
	{
		if (!lr)
			return E_POINTER;
		lockFlags = flags;
		if (owner)
		{
			lr->Pitch = (INT)owner->pitch;
			lr->pBits = owner->cpu.data();
			return S_OK;
		}
		if (rt)
		{
			rt->ResolveToCpu(); // FBO → RAM (evita o LockRect-preto do GLES)
			lr->Pitch = (INT)(w * 4);
			lr->pBits = rt->cpu.data();
			return S_OK;
		}
		if (backbuf)
		{
			if (cpu.size() < (size_t)w * h * 4)
				cpu.resize((size_t)w * h * 4);
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, cpu.data());
			lr->Pitch = (INT)(w * 4);
			lr->pBits = cpu.data();
			return S_OK;
		}
		lr->Pitch = (INT)(w * 4);
		lr->pBits = nullptr;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE UnlockRect() override
	{
		if (owner && !(lockFlags & D3DLOCK_READONLY))
			owner->dirty = true;
		lockFlags = 0;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetDC(HDC*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { return E_NOTIMPL; }
};

D3D9Tex::~D3D9Tex()
{
	if (rt)
		rt->Release();
}

HRESULT D3D9Tex::GetSurfaceLevel(UINT, IDirect3DSurface9** pp)
{
	if (!pp)
		return E_POINTER;
	auto* s = new D3D9Surf();
	s->owner = this;
	AddRef(); // a surf segura o container vivo (dtor dela dá Release)
	s->w = w;
	s->h = h;
	s->fmt = fmt;
	if (rt) // superfície de RT: mesma FBO-color por baixo (estilo D3D9)
	{
		s->rt = rt;
		rt->AddRef();
	}
	*pp = s;
	return S_OK;
}

struct D3D9VB : IDirect3DVertexBuffer9
{
	UINT size = 0;
	std::vector<uint8_t> data;
	ULONG refCount = 1;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return S_OK; }
	DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { return p; }
	DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
	void STDMETHODCALLTYPE PreLoad() override {}
	D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
	HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT, void** ppb, DWORD) override
	{
		if (!ppb)
			return E_POINTER;
		if (data.size() < size)
			data.resize(size);
		*ppb = data.data() + offset;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Unlock() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* d) override
	{
		if (d) { d->Format = D3DFMT_VERTEXDATA; d->Type = D3DRTYPE_VERTEXBUFFER; d->Usage = 0; d->Pool = D3DPOOL_MANAGED; d->Size = size; d->FVF = 0; }
		return S_OK;
	}
};

struct D3D9IB : IDirect3DIndexBuffer9
{
	UINT size = 0;
	D3DFORMAT fmt = D3DFMT_INDEX16;
	std::vector<uint8_t> data;
	ULONG refCount = 1;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return S_OK; }
	DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { return p; }
	DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
	void STDMETHODCALLTYPE PreLoad() override {}
	D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
	HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT, void** ppb, DWORD) override
	{
		if (!ppb)
			return E_POINTER;
		if (data.size() < size)
			data.resize(size);
		*ppb = data.data() + offset;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Unlock() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* d) override
	{
		if (d) { d->Format = fmt; d->Type = D3DRTYPE_INDEXBUFFER; d->Usage = 0; d->Pool = D3DPOOL_MANAGED; d->Size = size; }
		return S_OK;
	}
};

struct D3D9VS : IDirect3DVertexShader9
{
	std::vector<uint8_t> bytecode;
	// Tradução vs_1_x -> GLSL feita no primeiro draw (contexto GL garantido).
	int glState = 0; // 0 = pendente, 1 = pronto, 2 = falhou (draw cai no FFP)
	FfpProgram prog;
	std::vector<Vs11Input> inputs;
	GLint uC = -1, uYFlip = -1;
	ULONG refCount = 1;
	~D3D9VS()
	{
		if (prog.prog)
			glDeleteProgram(prog.prog);
		if (prog.vao)
			glDeleteVertexArrays(1, &prog.vao);
		if (prog.vbo)
			glDeleteBuffers(1, &prog.vbo);
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetFunction(void* p, UINT* size) override { if (size) *size = (UINT)bytecode.size(); if (p) std::memcpy(p, bytecode.data(), bytecode.size()); return S_OK; }
};

struct D3D9PS : IDirect3DPixelShader9
{
	std::vector<uint8_t> bytecode;
	ULONG refCount = 1;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetFunction(void* p, UINT* size) override { if (size) *size = (UINT)bytecode.size(); if (p) std::memcpy(p, bytecode.data(), bytecode.size()); return S_OK; }
};

struct D3D9VDecl : IDirect3DVertexDeclaration9
{
	std::vector<D3DVERTEXELEMENT9> elements;
	ULONG refCount = 1;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* el, UINT* count) override
	{
		if (count) *count = (UINT)elements.size();
		if (el) std::memcpy(el, elements.data(), elements.size() * sizeof(D3DVERTEXELEMENT9));
		return S_OK;
	}
};

struct D3D9Dev; // forward — StateBlock captura snapshot do device

struct FfpStateSnap
{
	DWORD rs[256]{};
	DWORD tss[8][33]{};
	DWORD smp[8][14]{};
	int tssUv[3]{};
	IDirect3DBaseTexture9* tex[8]{};
	D3DMATRIX world{}, view{}, proj{};
	D3DVIEWPORT9 vp{};
	D3DMATERIAL9 mat{};
	bool lighting = false, zEnable = true, zWrite = true, alphaBlend = false;
	bool alphaTest = false, fogEnable = false, scissorEnable = false;
	DWORD cull = D3DCULL_CCW, srcBlend = D3DBLEND_SRCALPHA, dstBlend = D3DBLEND_INVSRCALPHA;
	DWORD zFunc = D3DCMP_LESSEQUAL, alphaFunc = D3DCMP_GREATER, alphaRef = 0;
	DWORD fogMode = D3DFOG_LINEAR, fogColor = 0, texFactor = 0xFFFFFFFF;
	float fogStart = 0.f, fogEnd = 1.f, fogDensity = 1.f;
	RECT scissor{};
	D3DLIGHT9 lights[8]{};
	bool lightOn[8]{};
	DWORD curFVF = D3DFVF_XYZ;
};

struct D3D9StateBlock : IDirect3DStateBlock9
{
	D3D9Dev* dev = nullptr;
	FfpStateSnap snap{};
	bool hasSnap = false;
	ULONG refCount = 1;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE Capture() override;
	HRESULT STDMETHODCALLTYPE Apply() override;
};

// ---------------------------------------------------------------------------
// FVF / decl -> repack
//   origem: strides FVF clássicos ou decl (offsets do próprio elemento)
//   destino: pos(12) normal(12) color(4ub) uv0(8) uv1(8) = 44 bytes
// ---------------------------------------------------------------------------
struct VertexRepacker
{
	bool rhw = false;
	bool hasNormal = false;
	bool hasDiffuse = false;
	bool hasSpecular = false;
	int texCount = 0;
	size_t srcStride = 0;           // calculado no From*()
	std::vector<D3DVERTEXELEMENT9> decl;

	void FromFVF(DWORD f)
	{
		rhw = (f & D3DFVF_XYZRHW) != 0;
		hasNormal = (f & D3DFVF_NORMAL) != 0;
		hasDiffuse = (f & D3DFVF_DIFFUSE) != 0;
		hasSpecular = (f & D3DFVF_SPECULAR) != 0;
		texCount = (int)((f & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
		decl.clear();
		size_t s = rhw ? 16 : 12;
		if (hasNormal) s += 12;
		if (hasDiffuse) s += 4;
		if (hasSpecular) s += 4;
		s += (size_t)std::max(texCount, 0) * 8; // uv float 2D por estágio
		srcStride = s;
	}

void FromDecl(const std::vector<D3DVERTEXELEMENT9>& el)
{
	decl = el;
	rhw = hasNormal = hasDiffuse = hasSpecular = false;
	texCount = 0;
	size_t s = 0;
	const auto typeSize = [](BYTE t) -> size_t
	{
		switch (t)
		{
			case D3DDECLTYPE_FLOAT1: return 4;
			case D3DDECLTYPE_FLOAT2: return 8;
			case D3DDECLTYPE_FLOAT3: return 12;
			case D3DDECLTYPE_FLOAT4: return 16;
			case D3DDECLTYPE_D3DCOLOR: return 4;
			case D3DDECLTYPE_UBYTE4:
			case D3DDECLTYPE_UBYTE4N: return 4;
			case D3DDECLTYPE_SHORT2:
			case D3DDECLTYPE_SHORT2N:
			case D3DDECLTYPE_USHORT2N:
			case D3DDECLTYPE_FLOAT16_2: return 4;
			case D3DDECLTYPE_SHORT4:
			case D3DDECLTYPE_SHORT4N:
			case D3DDECLTYPE_USHORT4N:
			case D3DDECLTYPE_FLOAT16_4: return 8;
			default: return 0;
		}
	};
	for (const auto& e : el)
	{
		if (e.Stream != 0)
			continue;
		s = std::max(s, (size_t)e.Offset + typeSize(e.Type));
		if (e.Usage == D3DDECLUSAGE_NORMAL) hasNormal = true;
		else if (e.Usage == D3DDECLUSAGE_COLOR && e.UsageIndex == 0) hasDiffuse = true;
		else if (e.Usage == D3DDECLUSAGE_COLOR && e.UsageIndex == 1) hasSpecular = true;
		else if (e.Usage == D3DDECLUSAGE_TEXCOORD) texCount = std::max(texCount, e.UsageIndex + 1);
	}
	srcStride = s;
}
};

constexpr size_t kVertexStride = 44;

void RepackVertex(uint8_t* dst, const uint8_t* src, const VertexRepacker& r)
{
	std::memset(dst, 0, kVertexStride);
	if (r.decl.empty())
	{
		// caminho FVF
		size_t off = 0;
		std::memcpy(dst, src, r.rhw ? 16 : 12);
		off += r.rhw ? 16 : 12;
		if (r.hasNormal)
		{
			std::memcpy(dst + 12, src + off, 12);
			off += 12;
		}
		else
		{
			const float z = 1.0f;
			std::memcpy(dst + 20, &z, 4); // normal = (0,0,1)
		}
		if (r.hasDiffuse)
		{
			uint32_t argb;
			std::memcpy(&argb, src + off, 4);
			dst[24] = (uint8_t)((argb >> 16) & 0xFF);
			dst[25] = (uint8_t)((argb >> 8) & 0xFF);
			dst[26] = (uint8_t)(argb & 0xFF);
			dst[27] = (uint8_t)((argb >> 24) & 0xFF);
			off += 4;
		}
		else
		{
			dst[24] = dst[25] = dst[26] = dst[27] = 255;
		}
		if (r.hasSpecular)
			off += 4;
		float uvs[4] = {};
		const int n = std::min(r.texCount, 2);
		for (int t = 0; t < n; ++t)
			std::memcpy(&uvs[t * 2], src + off + (size_t)t * 8, 8);
		std::memcpy(dst + 28, uvs, 16);
	}
	else
	{
		// caminho decl
		dst[24] = dst[25] = dst[26] = dst[27] = 255;
		for (const auto& e : r.decl)
		{
			if (e.Stream != 0)
				continue;
			const uint8_t* s = src + e.Offset;
			switch (e.Usage)
			{
				case D3DDECLUSAGE_POSITION:
					std::memcpy(dst, s, 12);
					break;
				case D3DDECLUSAGE_POSITIONT:
					std::memcpy(dst, s, 16);
					break;
				case D3DDECLUSAGE_NORMAL:
					std::memcpy(dst + 12, s, 12);
					break;
				case D3DDECLUSAGE_COLOR:
					if (e.UsageIndex == 0)
					{
						if (e.Type == D3DDECLTYPE_D3DCOLOR)
						{
							dst[24] = s[2];
							dst[25] = s[1];
							dst[26] = s[0];
							dst[27] = s[3];
						}
						else if (e.Type == D3DDECLTYPE_FLOAT4)
						{
							for (int i = 0; i < 4; ++i)
							{
								float f;
								std::memcpy(&f, s + i * 4, 4);
								dst[24 + i] = (uint8_t)std::min(255.f, std::max(0.f, f * 255.f));
							}
						}
					}
					break;
				case D3DDECLUSAGE_TEXCOORD:
					if (e.UsageIndex < 2)
					{
						float* dstUv = reinterpret_cast<float*>(dst + 28 + (size_t)e.UsageIndex * 8);
						if (e.Type == D3DDECLTYPE_FLOAT2 || e.Type == D3DDECLTYPE_FLOAT3 || e.Type == D3DDECLTYPE_FLOAT4)
							std::memcpy(dstUv, s, 8);
						else if (e.Type == D3DDECLTYPE_SHORT2N)
						{
							int16_t sh[2];
							std::memcpy(sh, s, 4);
							dstUv[0] = (float)sh[0] / 32767.f;
							dstUv[1] = (float)sh[1] / 32767.f;
						}
						else if (e.Type == D3DDECLTYPE_USHORT2N)
						{
							uint16_t us[2];
							std::memcpy(us, s, 4);
							dstUv[0] = (float)us[0] / 65535.f;
							dstUv[1] = (float)us[1] / 65535.f;
						}
						else if (e.Type == D3DDECLTYPE_SHORT2)
						{
							int16_t sh[2];
							std::memcpy(sh, s, 4);
							dstUv[0] = (float)sh[0];
							dstUv[1] = (float)sh[1];
						}
						else if (e.Type == D3DDECLTYPE_FLOAT16_2)
						{
							// IEEE-754 binary16 → float32
							auto half = [](uint16_t h) -> float
							{
								const uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
								uint32_t exp = (h >> 10) & 0x1Fu;
								uint32_t mant = h & 0x3FFu;
								uint32_t bits;
								if (exp == 0)
								{
									if (mant == 0)
										bits = sign;
									else
									{
										exp = 127 - 15 + 1;
										while ((mant & 0x400u) == 0)
										{
											mant <<= 1;
											--exp;
										}
										mant &= 0x3FFu;
										bits = sign | (exp << 23) | (mant << 13);
									}
								}
								else if (exp == 31)
									bits = sign | 0x7F800000u | (mant << 13);
								else
									bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
								float f;
								std::memcpy(&f, &bits, 4);
								return f;
							};
							uint16_t h[2];
							std::memcpy(h, s, 4);
							dstUv[0] = half(h[0]);
							dstUv[1] = half(h[1]);
						}
					}
					break;
				default:
					break;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Device — D3D9Obj definido depois (back-pointer via void*, AddRef por vtable)
// ---------------------------------------------------------------------------
struct D3D9Obj;
inline void AddRefObj(IDirect3D9* o) { if (o) o->AddRef(); }

void FillCaps9(D3DCAPS9* caps)
{
	if (!caps)
		return;
	std::memset(caps, 0, sizeof(*caps));
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->Caps = D3DCAPS_READ_SCANLINE;
	caps->Caps2 = D3DCAPS2_DYNAMICTEXTURES;
	caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_TEXTUREVIDEOMEMORY;
	caps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_MATERIALSOURCE7;
	caps->MaxActiveLights = 8;
	caps->MaxTextureWidth = 4096;
	caps->MaxTextureHeight = 4096;
	caps->MaxTextureBlendStages = 8;
	caps->MaxSimultaneousTextures = 8;
	caps->MaxUserClipPlanes = 0;
	caps->MaxVertexBlendMatrices = 4;
	caps->MaxVertexBlendMatrixIndex = 255;
	caps->VertexShaderVersion = D3DVS_VERSION(2, 0);
	caps->PixelShaderVersion = D3DPS_VERSION(2, 0);
	caps->MaxVertexShaderConst = 256;
	caps->PS20Caps.NumTemps = 32;
	caps->RasterCaps = D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_SCISSORTEST;
	caps->TextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 | D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_MODULATE2X;
	caps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_CLAMP;
	caps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW;
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------
struct D3D9Dev : IDirect3DDevice9
{
	ULONG refCount = 1;
	D3D9Obj* d3d = nullptr;
	D3DPRESENT_PARAMETERS pp{};
	SDL_Window* window = nullptr;
	SDL_GLContext ctx = nullptr;

	FfpProgram ffp;
	FfpProgram* curProg = &ffp; // programa do draw corrente (FFP ou VS traduzido)
	std::vector<float> vsVertBuf;
	bool progReady = false;

	D3DMATRIX world{}, view{}, proj{}, ident{};
	D3DMATRIX texMatrix[8]{};

	D3DVIEWPORT9 vp{};
	D3DMATERIAL9 mat{};
	DWORD rs[256] = {};
	bool lighting = false, zEnable = true, zWrite = true, alphaBlend = false, alphaTest = false, fogEnable = false, scissorEnable = false;
	DWORD cull = D3DCULL_CCW, srcBlend = D3DBLEND_SRCALPHA, dstBlend = D3DBLEND_INVSRCALPHA, zFunc = D3DCMP_LESSEQUAL, alphaFunc = D3DCMP_GREATER;
	DWORD alphaRef = 0, fogMode = D3DFOG_LINEAR;
	float fogStart = 0.f, fogEnd = 1.f, fogDensity = 1.f;
	DWORD fogColor = 0, texFactor = 0xFFFFFFFF;
	RECT scissor{};

	IDirect3DBaseTexture9* tex[8] = {};
	DWORD tss[8][33] = {};
	DWORD smp[8][14] = {};
	int tssUv[3] = {};

	D3DLIGHT9 lights[8] = {};
	bool lightOn[8] = {};

	void* curVS = nullptr;
	void* curPS = nullptr;
	D3D9VDecl* curDecl = nullptr;
	DWORD curFVF = D3DFVF_XYZ;
	VertexRepacker repack;
	std::vector<uint8_t> repackBuf;
	unsigned frameNo = 0;

	D3D9RT* curRT = nullptr; // render target corrente (nullptr = backbuffer)
	GLuint fboBack = 0;      // FBO com depth p/ o backbuffer (só se o default não tiver)
	GLuint fboBackTex = 0, fboBackDepth = 0;
	int bbW = 0, bbH = 0;
	D3D9Surf* backBufSurf = nullptr; // proxy estável do backbuffer (save/restore)
	D3D9Surf* curDS = nullptr;       // depth/stencil corrente (tracking)

	// Cache do último ApplyGlState — evita glEnable/Blend/Cull repetidos por draw.
	struct GlStateCache
	{
		bool valid = false;
		bool blend = false;
		GLenum src = GL_ONE, dst = GL_ZERO;
		bool depthTest = false;
		GLenum depthFunc = GL_LESS;
		bool depthMask = true;
		bool cullOn = false;
		GLenum front = GL_CCW;
		bool scissorOn = false;
		int scX = 0, scY = 0, scW = 0, scH = 0;
	} glCache{};
	void InvalidateGlCache() { glCache.valid = false; }

	// Ring de VBO/EBO STREAM: evita stall de glBufferData no mesmo buffer a cada draw.
	struct StreamRing
	{
		static constexpr int N = 3;
		GLuint vbo[N]{};
		GLuint ebo[N]{};
		int cur = 0;
		bool ready = false;
		void Ensure()
		{
			if (ready)
				return;
			glGenBuffers(N, vbo);
			glGenBuffers(N, ebo);
			ready = true;
		}
		GLuint NextVbo() { Ensure(); cur = (cur + 1) % N; return vbo[cur]; }
		GLuint CurEbo() { Ensure(); return ebo[cur]; }
	} streamRing{};

	void UploadStream(GLenum target, GLuint buf, const void* data, GLsizeiptr size)
	{
		glBindBuffer(target, buf);
		// Orphan: aloca de novo sem sync com o draw anterior no mesmo slot.
		glBufferData(target, size, nullptr, GL_STREAM_DRAW);
		if (data && size > 0)
			glBufferSubData(target, 0, size, data);
	}

	D3D9Dev()
	{
		ident.m[0][0] = ident.m[1][1] = ident.m[2][2] = ident.m[3][3] = 1.f;
		world = view = proj = ident;
		for (int i = 0; i < 8; ++i)
			texMatrix[i] = ident;
		std::memset(lights, 0, sizeof(lights));
	}
	~D3D9Dev()
	{
		if (curRT)
			curRT->Release();
		if (backBufSurf)
			backBufSurf->Release();
		if (curDS)
			curDS->Release();
		if (fboBack)
			glDeleteFramebuffers(1, &fboBack);
		if (fboBackTex)
			glDeleteTextures(1, &fboBackTex);
		if (fboBackDepth)
			glDeleteRenderbuffers(1, &fboBackDepth);
	}

	UINT CurW() const { return curRT ? curRT->w : pp.BackBufferWidth; }
	UINT CurH() const { return curRT ? curRT->h : pp.BackBufferHeight; }

	FfpStateSnap MakeSnap() const
	{
		FfpStateSnap s{};
		std::memcpy(s.rs, rs, sizeof(rs));
		std::memcpy(s.tss, tss, sizeof(tss));
		std::memcpy(s.smp, smp, sizeof(smp));
		std::memcpy(s.tssUv, tssUv, sizeof(tssUv));
		std::memcpy(s.tex, tex, sizeof(tex));
		s.world = world; s.view = view; s.proj = proj;
		s.vp = vp; s.mat = mat;
		s.lighting = lighting; s.zEnable = zEnable; s.zWrite = zWrite;
		s.alphaBlend = alphaBlend; s.alphaTest = alphaTest;
		s.fogEnable = fogEnable; s.scissorEnable = scissorEnable;
		s.cull = cull; s.srcBlend = srcBlend; s.dstBlend = dstBlend;
		s.zFunc = zFunc; s.alphaFunc = alphaFunc; s.alphaRef = alphaRef;
		s.fogMode = fogMode; s.fogColor = fogColor; s.texFactor = texFactor;
		s.fogStart = fogStart; s.fogEnd = fogEnd; s.fogDensity = fogDensity;
		s.scissor = scissor;
		std::memcpy(s.lights, lights, sizeof(lights));
		std::memcpy(s.lightOn, lightOn, sizeof(lightOn));
		s.curFVF = curFVF;
		return s;
	}
	void ApplySnap(const FfpStateSnap& s)
	{
		std::memcpy(rs, s.rs, sizeof(rs));
		std::memcpy(tss, s.tss, sizeof(tss));
		std::memcpy(smp, s.smp, sizeof(smp));
		std::memcpy(tssUv, s.tssUv, sizeof(tssUv));
		std::memcpy(tex, s.tex, sizeof(tex));
		world = s.world; view = s.view; proj = s.proj;
		vp = s.vp; mat = s.mat;
		lighting = s.lighting; zEnable = s.zEnable; zWrite = s.zWrite;
		alphaBlend = s.alphaBlend; alphaTest = s.alphaTest;
		fogEnable = s.fogEnable; scissorEnable = s.scissorEnable;
		cull = s.cull; srcBlend = s.srcBlend; dstBlend = s.dstBlend;
		zFunc = s.zFunc; alphaFunc = s.alphaFunc; alphaRef = s.alphaRef;
		fogMode = s.fogMode; fogColor = s.fogColor; texFactor = s.texFactor;
		fogStart = s.fogStart; fogEnd = s.fogEnd; fogDensity = s.fogDensity;
		scissor = s.scissor;
		std::memcpy(lights, s.lights, sizeof(lights));
		std::memcpy(lightOn, s.lightOn, sizeof(lightOn));
		curFVF = s.curFVF;
	}

	// O default framebuffer do HOS/Mesa às vezes nasce SEM depth. Antes usávamos
	// FBO intermediário + blit; no Switch o blit chegava preto (HUD no default
	// OK). Desenhar DIRETO no default FB; sem depth → desliga Z no ApplyGlState.
	bool bbFboProbed = false;
	bool bbDirect = true; // true = sem FBO intermediário
	bool bbHasDepth = false;

	void EnsureBackFbo()
	{
		if (bbFboProbed || !progReady)
			return;
		bbFboProbed = true;
		bbDirect = true;
		fboBack = 0;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		GLint depthBits = 0, stencilBits = 0;
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH,
			GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depthBits);
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL,
			GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencilBits);
		while (glGetError() != GL_NO_ERROR) {}
		bbHasDepth = depthBits > 0;
		SLog("[GLES9] draw DIRETO no default FB %ux%u depth=%d stencil=%d",
			(unsigned)pp.BackBufferWidth, (unsigned)pp.BackBufferHeight, depthBits, stencilBits);
		if (!bbHasDepth)
			HudErr("DEFAULT FB SEM DEPTH");
	}
	void BindFbo()
	{
		InvalidateGlCache();
		if (curRT)
		{
			if (curRT->Ensure())
				glBindFramebuffer(GL_FRAMEBUFFER, curRT->fbo);
			else
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		else
			glBindFramebuffer(GL_FRAMEBUFFER, fboBack ? fboBack : 0);
	}

	// ------------------------------------------------------------ GL state
	void ApplyGlState()
	{
		const GLenum wantSrc = BlendToGL(srcBlend);
		const GLenum wantDst = BlendToGL(dstBlend);
		if (!glCache.valid || glCache.blend != alphaBlend
			|| (alphaBlend && (glCache.src != wantSrc || glCache.dst != wantDst)))
		{
			if (alphaBlend)
			{
				glEnable(GL_BLEND);
				glBlendFuncSeparate(wantSrc, wantDst, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			else
				glDisable(GL_BLEND);
			glCache.blend = alphaBlend;
			glCache.src = wantSrc;
			glCache.dst = wantDst;
		}

		const bool hasDepth = curRT ? curRT->depth != 0 : (bbHasDepth || !bbDirect);
		const bool wantDepth = zEnable && hasDepth;
		const GLenum wantZFunc = CmpToGL(zFunc);
		const bool wantZMask = (zWrite && hasDepth);
		if (!glCache.valid || glCache.depthTest != wantDepth
			|| (wantDepth && glCache.depthFunc != wantZFunc))
		{
			if (wantDepth)
			{
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(wantZFunc);
			}
			else
				glDisable(GL_DEPTH_TEST);
			glCache.depthTest = wantDepth;
			glCache.depthFunc = wantZFunc;
		}
		if (!glCache.valid || glCache.depthMask != wantZMask)
		{
			glDepthMask(wantZMask ? GL_TRUE : GL_FALSE);
			glCache.depthMask = wantZMask;
		}

		const bool wantCull = (cull != D3DCULL_NONE);
		const bool cullCw = cull == D3DCULL_CW;
		const bool mirrored = curRT != nullptr;
		const GLenum wantFront = (cullCw != mirrored) ? GL_CCW : GL_CW;
		if (!glCache.valid || glCache.cullOn != wantCull
			|| (wantCull && glCache.front != wantFront))
		{
			if (!wantCull)
				glDisable(GL_CULL_FACE);
			else
			{
				// D3DCULL_* descreve a ordem vista na tela. No backbuffer a imagem
				// não é espelhada (winding igual); no RT o flip de Y inverte.
				glEnable(GL_CULL_FACE);
				glFrontFace(wantFront);
				glCullFace(GL_BACK);
			}
			glCache.cullOn = wantCull;
			glCache.front = wantFront;
		}

		if (scissorEnable)
		{
			const int y = std::max(0, (int)CurH() - (int)scissor.bottom);
			const int sx = scissor.left;
			const int sy = y;
			const int sw = std::max(0, (int)(scissor.right - scissor.left));
			const int sh = std::max(0, (int)(scissor.bottom - scissor.top));
			if (!glCache.valid || !glCache.scissorOn
				|| glCache.scX != sx || glCache.scY != sy
				|| glCache.scW != sw || glCache.scH != sh)
			{
				glEnable(GL_SCISSOR_TEST);
				glScissor(sx, sy, sw, sh);
				glCache.scissorOn = true;
				glCache.scX = sx; glCache.scY = sy;
				glCache.scW = sw; glCache.scH = sh;
			}
		}
		else if (!glCache.valid || glCache.scissorOn)
		{
			glDisable(GL_SCISSOR_TEST);
			glCache.scissorOn = false;
		}
		glCache.valid = true;
	}

	// ---------------------------------------------------------- texturas
	void UploadTexture(D3D9Tex* t)
	{
		if (!t)
			return;
		if (t->rt) // textura RT: o conteúdo vive no FBO (criado no primeiro uso)
		{
			t->rt->Ensure();
			t->gl = t->rt->tex;
			return;
		}
		// Não subir textura virgem (cpu zerado, dirty==false).
		if (t->cpu.empty() || !t->dirty)
			return;
		// IMPORTANTE: o caller DEVE ter feito glActiveTexture no unit alvo.
		// Bind sem ActiveTexture no unit corrente — se Upload(t1) rodar com
		// unit 0 ativo, sobrescreve a textura do estágio 0 (sintoma: silhueta
		// flat = stage1 branco/lightmap no uTex0).
		if (t->gl == 0)
		{
			glGenTextures(1, &t->gl);
			glBindTexture(GL_TEXTURE_2D, t->gl);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		}
		else
			glBindTexture(GL_TEXTURE_2D, t->gl);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		const bool compressed = (t->fmt == D3DFMT_DXT1 || t->fmt == D3DFMT_DXT3 || t->fmt == D3DFMT_DXT5);
		// Tegra X1: DXT1/3/5 nativos (switchbrew + gta3-nx). Preferir HW; CPU só se falhar.
		bool usedHw = false;
		if (compressed && g_s3tcAvailable)
		{
			while (glGetError() != GL_NO_ERROR)
			{
			}
			const GLenum ifmt = t->fmt == D3DFMT_DXT1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
			                    : t->fmt == D3DFMT_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
			                                            : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			glCompressedTexImage2D(GL_TEXTURE_2D, 0, ifmt, t->w, t->h, 0, (GLsizei)t->cpu.size(), t->cpu.data());
			const GLenum err = glGetError();
			if (err == GL_NO_ERROR)
			{
				usedHw = true;
				++g_uploadHw;
				if (g_uploadHw <= 8)
					SLog("[GLES9] upload HW DXT fmt=0x%x %ux%u", (unsigned)t->fmt, (unsigned)t->w, (unsigned)t->h);
			}
			else
			{
				SLog("[GLES9] S3TC HW falhou err=0x%x — fallback CPU", (unsigned)err);
			}
		}
		if (!usedHw)
		{
			std::vector<uint8_t> rgba;
			rgba.resize((size_t)t->w * t->h * 4);
			const uint8_t* pixels = rgba.data();
			if (compressed)
			{
				if (t->fmt == D3DFMT_DXT1)
					DecodeBC1(t->cpu.data(), rgba.data(), t->w, t->h);
				else if (t->fmt == D3DFMT_DXT3)
					DecodeBC2(t->cpu.data(), rgba.data(), t->w, t->h);
				else
					DecodeBC3(t->cpu.data(), rgba.data(), t->w, t->h);
			}
			else
			{
				const size_t n = (size_t)t->w * t->h;
				switch (t->fmt)
				{
					case D3DFMT_A8R8G8B8:
					case D3DFMT_X8R8G8B8:
					{
						const uint8_t* s = t->cpu.data();
						for (size_t i = 0; i < n; ++i)
						{
							rgba[i * 4 + 0] = s[i * 4 + 2];
							rgba[i * 4 + 1] = s[i * 4 + 1];
							rgba[i * 4 + 2] = s[i * 4 + 0];
							rgba[i * 4 + 3] = t->fmt == D3DFMT_A8R8G8B8 ? s[i * 4 + 3] : 255;
						}
						break;
					}
					case D3DFMT_A4R4G4B4:
					{
						const uint16_t* s = reinterpret_cast<const uint16_t*>(t->cpu.data());
						for (size_t i = 0; i < n; ++i)
						{
							const uint16_t v = s[i];
							rgba[i * 4 + 0] = (uint8_t)(((v >> 8) & 0xF) * 17);
							rgba[i * 4 + 1] = (uint8_t)(((v >> 4) & 0xF) * 17);
							rgba[i * 4 + 2] = (uint8_t)((v & 0xF) * 17);
							rgba[i * 4 + 3] = (uint8_t)(((v >> 12) & 0xF) * 17);
						}
						break;
					}
					case D3DFMT_A1R5G5B5:
					case D3DFMT_X1R5G5B5:
					{
						const uint16_t* s = reinterpret_cast<const uint16_t*>(t->cpu.data());
						for (size_t i = 0; i < n; ++i)
						{
							const uint16_t v = s[i];
							rgba[i * 4 + 0] = (uint8_t)(((v >> 10) & 0x1F) << 3);
							rgba[i * 4 + 1] = (uint8_t)(((v >> 5) & 0x1F) << 3);
							rgba[i * 4 + 2] = (uint8_t)((v & 0x1F) << 3);
							if (t->fmt == D3DFMT_X1R5G5B5)
								rgba[i * 4 + 3] = 255;
							else
								rgba[i * 4 + 3] = (v & 0x8000) ? 255 : 0;
						}
						break;
					}
					case D3DFMT_R5G6B5:
					{
						const uint16_t* s = reinterpret_cast<const uint16_t*>(t->cpu.data());
						for (size_t i = 0; i < n; ++i)
						{
							const uint16_t v = s[i];
							rgba[i * 4 + 0] = (uint8_t)(((v >> 11) & 0x1F) << 3);
							rgba[i * 4 + 1] = (uint8_t)(((v >> 5) & 0x3F) << 2);
							rgba[i * 4 + 2] = (uint8_t)((v & 0x1F) << 3);
							rgba[i * 4 + 3] = 255;
						}
						break;
					}
					case D3DFMT_A8L8:
					{
						const uint16_t* s = reinterpret_cast<const uint16_t*>(t->cpu.data());
						for (size_t i = 0; i < n; ++i)
						{
							rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = (uint8_t)(s[i] & 0xFF);
							rgba[i * 4 + 3] = (uint8_t)(s[i] >> 8);
						}
						break;
					}
					default:
					{
						static DWORD lastWarned = 0;
						if (lastWarned != t->fmt)
						{
							lastWarned = t->fmt;
							SLog("[GLES9] formato de textura 0x%x sem conversor — dummy magenta", (unsigned)t->fmt);
							RecordBadFmt(t->fmt);
						}
						for (size_t i = 0; i < n; ++i)
						{
							rgba[i * 4 + 0] = 255;
							rgba[i * 4 + 1] = 0;
							rgba[i * 4 + 2] = 255;
							rgba[i * 4 + 3] = 255;
						}
						break;
					}
				}
			}
			if (g_texUploaded < 8 || compressed)
			{
				unsigned long long sum = 0;
				const size_t n = (size_t)t->w * t->h * 4;
				for (size_t i = 0; i < n; i += 16)
					sum += rgba[i] + rgba[i + 1] + rgba[i + 2];
				g_lastChk = sum;
				if (g_texUploaded < 8)
					SLog("[GLES9] decode chk=%llu fmt=0x%x %ux%u", sum,
						(unsigned)t->fmt, (unsigned)t->w, (unsigned)t->h);
			}
			if (compressed)
				++g_uploadCpu;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		}
		t->dirty = false;
		++g_texUploaded;
		if (g_texUploaded <= 8)
			SLog("[GLES9] upload#%u fmt=0x%x %ux%u cpu=%zu", g_texUploaded,
				(unsigned)t->fmt, (unsigned)t->w, (unsigned)t->h, t->cpu.size());
	}

	void BindTexturesAndSetStageUniforms()
	{
		FfpProgram& ffp = *curProg;
		int texOn[3] = {}, colorSt[3] = {}, alphaSt[3] = {}, stageUv[3] = {};
		for (int i = 0; i < 3; ++i)
		{
			auto* t = tex[i] ? static_cast<D3D9Tex*>(tex[i]) : nullptr;
			// ActiveTexture ANTES do upload — senão Upload(t1) cloba unit 0.
			glActiveTexture(GL_TEXTURE0 + i);
			if (t)
				UploadTexture(t);
			if (t && !t->gl && !t->rt)
			{
				SLog("[GLES9] textura vinculada sem upload fmt=0x%x %ux%u dirty=%d cpu=%zu",
					(unsigned)t->fmt, (unsigned)t->w, (unsigned)t->h,
					t->dirty ? 1 : 0, t->cpu.size());
				HudErr("TEX NOUPLOAD 0x%04X", (unsigned)t->fmt);
			}
			const bool hasTex = t && t->gl;
			glBindTexture(GL_TEXTURE_2D, hasTex ? t->gl : 0);
			texOn[i] = hasTex ? 1 : 0;
			const auto pack = [](DWORD op, DWORD a1, DWORD a2) -> int
			{
				if (op == 0)
					op = D3DTOP_DISABLE;
				return (int)((op & 0xFFu) | ((a1 & 0x3Fu) << 8) | ((a2 & 0x3Fu) << 16));
			};
			colorSt[i] = pack(tss[i][D3DTSS_COLOROP], tss[i][D3DTSS_COLORARG1], tss[i][D3DTSS_COLORARG2]);
			alphaSt[i] = pack(tss[i][D3DTSS_ALPHAOP], tss[i][D3DTSS_ALPHAARG1], tss[i][D3DTSS_ALPHAARG2]);
			stageUv[i] = tssUv[i] > 0 ? 1 : 0;
			if (hasTex)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, smp[i][D3DSAMP_ADDRESSU] == D3DTADDRESS_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, smp[i][D3DSAMP_ADDRESSV] == D3DTADDRESS_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, TexFilterToGL(smp[i][D3DSAMP_MINFILTER]));
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, TexFilterToGL(smp[i][D3DSAMP_MAGFILTER]));
			}
		}
		// Rebind final explícito unit0←tex0 (defesa contra qualquer clobber).
		for (int i = 0; i < 3; ++i)
		{
			auto* t = tex[i] ? static_cast<D3D9Tex*>(tex[i]) : nullptr;
			glActiveTexture(GL_TEXTURE0 + i);
			glBindTexture(GL_TEXTURE_2D, (t && t->gl) ? t->gl : 0);
		}
		glActiveTexture(GL_TEXTURE0);
		for (int i = 0; i < 3; ++i)
		{
			if (ffp.uTexOn[i] >= 0)
				glUniform1i(ffp.uTexOn[i], texOn[i]);
			if (ffp.uColorSt[i] >= 0)
				glUniform1i(ffp.uColorSt[i], colorSt[i]);
			if (ffp.uAlphaSt[i] >= 0)
				glUniform1i(ffp.uAlphaSt[i], alphaSt[i]);
			if (ffp.uStageUv[i] >= 0)
				glUniform1i(ffp.uStageUv[i], stageUv[i]);
		}
		if (ffp.uTex0 >= 0)
			glUniform1i(ffp.uTex0, 0);
		if (ffp.uTex1 >= 0)
			glUniform1i(ffp.uTex1, 1);
		if (ffp.uTex2 >= 0)
			glUniform1i(ffp.uTex2, 2);
		if ((frameNo % 600) == 1)
		{
			SLog("[GLES9] bind texOn=%d/%d/%d c0=0x%06x a0=0x%06x c1=0x%06x a1=0x%06x gl0=%u",
				texOn[0], texOn[1], texOn[2], colorSt[0], alphaSt[0], colorSt[1], alphaSt[1],
				(tex[0] && static_cast<D3D9Tex*>(tex[0])->gl) ? static_cast<D3D9Tex*>(tex[0])->gl : 0u);
		}
	}

	// ------------------------------------------------------------- draw
	void SetShaderState()
	{
		FfpProgram& ffp = *curProg;
		const auto mul = [](const D3DMATRIX& a, const D3DMATRIX& b)
		{
			D3DMATRIX o;
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					o.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c] + a.m[r][3] * b.m[3][c];
			return o;
		};
		// D3D (vetor-linha): v' = v * W * V * P. Com GL_FALSE + layout D3D
		// row-major no uniform, o caminho que já mostrou o castelo no Switch.
		const D3DMATRIX wv = mul(world, view);
		D3DMATRIX wvp = mul(wv, proj);
		if (curRT)
		{
			D3DMATRIX flip;
			std::memset(&flip, 0, sizeof(flip));
			flip.m[0][0] = flip.m[2][2] = flip.m[3][3] = 1.f;
			flip.m[1][1] = -1.f;
			wvp = mul(flip, wvp);
		}
		glUniformMatrix4fv(ffp.uWVP, 1, GL_FALSE, &wvp.m[0][0]);
		glUniformMatrix4fv(ffp.uWorld, 1, GL_FALSE, &world.m[0][0]);
		if (ffp.uWV >= 0)
			glUniformMatrix4fv(ffp.uWV, 1, GL_FALSE, &wv.m[0][0]);
		if (ffp.uTexM0 >= 0)
			glUniformMatrix4fv(ffp.uTexM0, 1, GL_FALSE, &texMatrix[0].m[0][0]);
		if (ffp.uTexM1 >= 0)
			glUniformMatrix4fv(ffp.uTexM1, 1, GL_FALSE, &texMatrix[1].m[0][0]);
		if (ffp.uTexXform0 >= 0)
			glUniform1i(ffp.uTexXform0, (int)(tss[0][D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFFu));
		if (ffp.uTexXform1 >= 0)
			glUniform1i(ffp.uTexXform1, (int)(tss[1][D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFFu));
		glUniform1f(ffp.uRhwYFlip, curRT ? 1.0f : -1.0f); // RHW: backbuffer flip / RT direto
		glUniform2f(ffp.uRhwVP, (float)CurW(), (float)CurH()); // RHW vive em pixels do alvo corrente
		glUniform1i(ffp.uRhw, repack.rhw ? 1 : 0);
		glUniform1i(ffp.uLighting, lighting ? 1 : 0);
		glUniform1i(ffp.uColorVertex, repack.hasDiffuse ? 1 : 0);
		glUniform4f(ffp.uMatDiffuse, mat.Diffuse.r, mat.Diffuse.g, mat.Diffuse.b, mat.Diffuse.a);
		glUniform4f(ffp.uMatAmbient, mat.Ambient.r, mat.Ambient.g, mat.Ambient.b, mat.Ambient.a);
		glUniform4f(ffp.uMatEmissive, mat.Emissive.r, mat.Emissive.g, mat.Emissive.b, mat.Emissive.a);
		glUniform4f(ffp.uGlobalAmbient,
		            ((rs[D3DRS_AMBIENT] >> 16) & 0xFF) / 255.f, ((rs[D3DRS_AMBIENT] >> 8) & 0xFF) / 255.f,
		            (rs[D3DRS_AMBIENT] & 0xFF) / 255.f, 1.f);
		float lp[32], ldir[32], ld[32], la[32], lat[32];
		for (int i = 0; i < 8; ++i)
		{
			const D3DLIGHT9& L = lights[i];
			const bool dir = L.Type == D3DLIGHT_DIRECTIONAL;
			lp[i * 4 + 0] = L.Position.x;
			lp[i * 4 + 1] = L.Position.y;
			lp[i * 4 + 2] = L.Position.z;
			lp[i * 4 + 3] = dir ? 0.f : 1.f;
			ldir[i * 4 + 0] = -L.Direction.x;
			ldir[i * 4 + 1] = -L.Direction.y;
			ldir[i * 4 + 2] = -L.Direction.z;
			ldir[i * 4 + 3] = 0.f;
			ld[i * 4 + 0] = L.Diffuse.r;
			ld[i * 4 + 1] = L.Diffuse.g;
			ld[i * 4 + 2] = L.Diffuse.b;
			ld[i * 4 + 3] = lightOn[i] ? 1.f : 0.f;
			la[i * 4 + 0] = L.Ambient.r;
			la[i * 4 + 1] = L.Ambient.g;
			la[i * 4 + 2] = L.Ambient.b;
			la[i * 4 + 3] = 0.f;
			lat[i * 4 + 0] = L.Attenuation0;
			lat[i * 4 + 1] = L.Attenuation1;
			lat[i * 4 + 2] = L.Attenuation2;
			lat[i * 4 + 3] = (dir || L.Range <= 0.f) ? 1e30f : L.Range;
		}
		glUniform4fv(ffp.uLightPos, 8, lp);
		glUniform4fv(ffp.uLightDiff, 8, ld);
		if (ffp.uLightDir >= 0)
			glUniform4fv(ffp.uLightDir, 8, ldir);
		if (ffp.uLightAmb >= 0)
			glUniform4fv(ffp.uLightAmb, 8, la);
		if (ffp.uLightAtt >= 0)
			glUniform4fv(ffp.uLightAtt, 8, lat);
		glUniform4f(ffp.uTexFactor, ((texFactor >> 16) & 0xFF) / 255.f, ((texFactor >> 8) & 0xFF) / 255.f,
		            (texFactor & 0xFF) / 255.f, ((texFactor >> 24) & 0xFF) / 255.f);
		glUniform1i(ffp.uAlphaTest, alphaTest ? 1 : 0);
		glUniform1f(ffp.uAlphaRef, (alphaRef & 0xFF) / 255.f);
		glUniform1i(ffp.uAlphaFunc, (int)alphaFunc);
		glUniform1i(ffp.uFogEnable, fogEnable ? 1 : 0);
		glUniform1i(ffp.uFogMode, (int)fogMode);
		glUniform3f(ffp.uFogColor, ((fogColor >> 16) & 0xFF) / 255.f, ((fogColor >> 8) & 0xFF) / 255.f, (fogColor & 0xFF) / 255.f);
		glUniform1f(ffp.uFogStart, fogStart);
		glUniform1f(ffp.uFogEnd, fogEnd);
		glUniform1f(ffp.uFogDensity, fogDensity);
#if defined(WYD_TEX_PROBE) && WYD_TEX_PROBE
		if (ffp.uProbeMode >= 0)
			glUniform1i(ffp.uProbeMode, g_probeMode);
#else
		if (ffp.uProbeMode >= 0)
			glUniform1i(ffp.uProbeMode, 2); // NORMAL
#endif
	}

	bool EnsureVsProgram(D3D9VS* vs)
	{
		if (vs->glState != 0)
			return vs->glState == 1;
		vs->glState = 2;
		const Vs11Result r = TranslateVs11(reinterpret_cast<const uint32_t*>(vs->bytecode.data()), vs->bytecode.size() / 4);
		if (!r.ok)
		{
			SLog("[VS] traducao FALHOU: %s (draw segue por FFP)", r.error.c_str());
			return false;
		}
		if (!BuildFfp(vs->prog, r.glsl.c_str(), true))
		{
			SLog("[VS] GLSL traduzido nao compilou/linkou (%d instr)", r.instructions);
			return false;
		}
		vs->inputs = r.inputs;
		vs->uC = glGetUniformLocation(vs->prog.prog, "uC[0]");
		if (vs->uC < 0)
			vs->uC = glGetUniformLocation(vs->prog.prog, "uC");
		vs->uYFlip = glGetUniformLocation(vs->prog.prog, "uYFlip");
		vs->glState = 1;
		SLog("[VS] traduzido ok instr=%d entradas=%zu uC=%d", r.instructions, r.inputs.size(), (int)vs->uC);
		return true;
	}

	static float HalfToFloat(uint16_t h)
	{
		const uint32_t s = (uint32_t)(h & 0x8000u) << 16;
		uint32_t e = (h >> 10) & 0x1Fu;
		uint32_t m = h & 0x3FFu;
		uint32_t bits;
		if (e == 0)
		{
			if (m == 0)
				bits = s;
			else
			{
				e = 127 - 15 + 1;
				while (!(m & 0x400u))
				{
					m <<= 1;
					--e;
				}
				bits = s | (e << 23) | ((m & 0x3FFu) << 13);
			}
		}
		else if (e == 31)
			bits = s | 0x7F800000u | (m << 13);
		else
			bits = s | ((e + 127 - 15) << 23) | (m << 13);
		float f;
		std::memcpy(&f, &bits, 4);
		return f;
	}

	// Elemento de vértice D3D9 -> vec4 como o hardware entrega ao vertex shader.
	static void FetchElement(const uint8_t* s, BYTE type, float* o)
	{
		o[0] = o[1] = o[2] = 0.f;
		o[3] = 1.f;
		int16_t sh[4];
		uint16_t us[4];
		switch (type)
		{
			case D3DDECLTYPE_FLOAT1: std::memcpy(o, s, 4); break;
			case D3DDECLTYPE_FLOAT2: std::memcpy(o, s, 8); break;
			case D3DDECLTYPE_FLOAT3: std::memcpy(o, s, 12); break;
			case D3DDECLTYPE_FLOAT4: std::memcpy(o, s, 16); break;
			case D3DDECLTYPE_D3DCOLOR: // bytes B,G,R,A -> (R,G,B,A)
				o[0] = s[2] / 255.f;
				o[1] = s[1] / 255.f;
				o[2] = s[0] / 255.f;
				o[3] = s[3] / 255.f;
				break;
			case D3DDECLTYPE_UBYTE4:
				for (int i = 0; i < 4; ++i)
					o[i] = (float)s[i];
				break;
			case D3DDECLTYPE_UBYTE4N:
				for (int i = 0; i < 4; ++i)
					o[i] = s[i] / 255.f;
				break;
			case D3DDECLTYPE_SHORT2:
				std::memcpy(sh, s, 4);
				o[0] = sh[0];
				o[1] = sh[1];
				break;
			case D3DDECLTYPE_SHORT4:
				std::memcpy(sh, s, 8);
				for (int i = 0; i < 4; ++i)
					o[i] = sh[i];
				break;
			case D3DDECLTYPE_SHORT2N:
				std::memcpy(sh, s, 4);
				o[0] = std::max(-1.f, sh[0] / 32767.f);
				o[1] = std::max(-1.f, sh[1] / 32767.f);
				break;
			case D3DDECLTYPE_SHORT4N:
				std::memcpy(sh, s, 8);
				for (int i = 0; i < 4; ++i)
					o[i] = std::max(-1.f, sh[i] / 32767.f);
				break;
			case D3DDECLTYPE_USHORT2N:
				std::memcpy(us, s, 4);
				o[0] = us[0] / 65535.f;
				o[1] = us[1] / 65535.f;
				break;
			case D3DDECLTYPE_USHORT4N:
				std::memcpy(us, s, 8);
				for (int i = 0; i < 4; ++i)
					o[i] = us[i] / 65535.f;
				break;
			case D3DDECLTYPE_FLOAT16_2:
				std::memcpy(us, s, 4);
				o[0] = HalfToFloat(us[0]);
				o[1] = HalfToFloat(us[1]);
				break;
			case D3DDECLTYPE_FLOAT16_4:
				std::memcpy(us, s, 8);
				for (int i = 0; i < 4; ++i)
					o[i] = HalfToFloat(us[i]);
				break;
			default: break;
		}
	}

	static void AddElement(std::vector<D3DVERTEXELEMENT9>& out, WORD& off, BYTE type, BYTE usage, BYTE index, WORD size)
	{
		D3DVERTEXELEMENT9 e{};
		e.Stream = 0;
		e.Offset = off;
		e.Type = type;
		e.Method = D3DDECLMETHOD_DEFAULT;
		e.Usage = usage;
		e.UsageIndex = index;
		out.push_back(e);
		off = (WORD)(off + size);
	}

	// Vertex shader com SetFVF (sem declaração): monta a declaração equivalente.
	static std::vector<D3DVERTEXELEMENT9> FvfToDecl(DWORD f)
	{
		std::vector<D3DVERTEXELEMENT9> el;
		WORD off = 0;
		const DWORD pos = f & D3DFVF_POSITION_MASK;
		if (pos == D3DFVF_XYZRHW)
			AddElement(el, off, D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0, 16);
		else
		{
			AddElement(el, off, D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, 12);
			if (pos >= D3DFVF_XYZB1 && pos <= D3DFVF_XYZB5)
			{
				const int n = (int)((pos - D3DFVF_XYZB1) / 2) + 1;
				const BYTE t = (BYTE)(D3DDECLTYPE_FLOAT1 + std::min(n, 4) - 1);
				AddElement(el, off, t, D3DDECLUSAGE_BLENDWEIGHT, 0, (WORD)(n * 4));
			}
		}
		if (f & D3DFVF_NORMAL)
			AddElement(el, off, D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0, 12);
		if (f & D3DFVF_PSIZE)
			AddElement(el, off, D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0, 4);
		if (f & D3DFVF_DIFFUSE)
			AddElement(el, off, D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0, 4);
		if (f & D3DFVF_SPECULAR)
			AddElement(el, off, D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1, 4);
		const int texCount = (int)((f & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
		for (int t = 0; t < texCount; ++t)
		{
			static const BYTE kType[4] = { D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4, D3DDECLTYPE_FLOAT1 };
			static const WORD kSize[4] = { 8, 12, 16, 4 };
			const DWORD fmt = (f >> (16 + t * 2)) & 3u;
			AddElement(el, off, kType[fmt], D3DDECLUSAGE_TEXCOORD, (BYTE)t, kSize[fmt]);
		}
		return el;
	}

	void UploadVsVertices(D3D9VS* vs, const uint8_t* src, UINT vstride, UINT numVtx)
	{
		const std::vector<D3DVERTEXELEMENT9> fvfDecl = curDecl ? std::vector<D3DVERTEXELEMENT9>() : FvfToDecl(curFVF);
		const std::vector<D3DVERTEXELEMENT9>& els = curDecl ? curDecl->elements : fvfDecl;
		const size_t nIn = vs->inputs.size();
		std::vector<const D3DVERTEXELEMENT9*> map(nIn, nullptr);
		for (size_t k = 0; k < nIn; ++k)
			for (const auto& e : els)
				if (e.Stream == 0 && e.Type != D3DDECLTYPE_UNUSED && e.Usage == vs->inputs[k].usage &&
				    e.UsageIndex == vs->inputs[k].usageIndex)
				{
					map[k] = &e;
					break;
				}

		glBindVertexArray(vs->prog.vao);
		if (nIn == 0)
			return;
		vsVertBuf.resize((size_t)numVtx * nIn * 4);
		for (UINT v = 0; v < numVtx; ++v)
		{
			const uint8_t* sv = src + (size_t)v * vstride;
			float* dv = vsVertBuf.data() + (size_t)v * nIn * 4;
			for (size_t k = 0; k < nIn; ++k)
			{
				if (map[k])
					FetchElement(sv + map[k]->Offset, map[k]->Type, dv + k * 4);
				else
				{
					dv[k * 4 + 0] = dv[k * 4 + 1] = dv[k * 4 + 2] = 0.f;
					dv[k * 4 + 3] = 1.f;
				}
			}
		}
		const GLuint vb = streamRing.NextVbo();
		UploadStream(GL_ARRAY_BUFFER, vb, vsVertBuf.data(),
			(GLsizeiptr)(vsVertBuf.size() * sizeof(float)));
		const GLsizei stride = (GLsizei)(nIn * 16);
		for (size_t k = 0; k < nIn; ++k)
		{
			const GLuint loc = (GLuint)vs->inputs[k].reg;
			glEnableVertexAttribArray(loc);
			glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride, (void*)(k * 16));
		}
	}

	void RepackFfpVertices(const uint8_t* src, UINT vstride, UINT numVtx)
	{
		repackBuf.resize((size_t)numVtx * kVertexStride);
		for (UINT i = 0; i < numVtx; ++i)
			RepackVertex(repackBuf.data() + (size_t)i * kVertexStride, src + (size_t)i * vstride, repack);

		if ((frameNo % 600) == 1 && tex[0])
		{
			float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
			for (UINT i = 0; i < numVtx; ++i)
			{
				float u, v;
				std::memcpy(&u, repackBuf.data() + (size_t)i * kVertexStride + 28, 4);
				std::memcpy(&v, repackBuf.data() + (size_t)i * kVertexStride + 32, 4);
				umin = std::min(umin, u); umax = std::max(umax, u);
				vmin = std::min(vmin, v); vmax = std::max(vmax, v);
			}
			g_uvMinU = umin; g_uvMaxU = umax; g_uvMinV = vmin; g_uvMaxV = vmax;
			g_uvSampleValid = true;
			SLog("[GLES9] UV fvf=0x%x decl=%d n=%u u=[%.3f..%.3f] v=[%.3f..%.3f] stride=%u",
				(unsigned)curFVF, curDecl ? 1 : 0, numVtx, umin, umax, vmin, vmax, vstride);
			return;
		}
		// Amostra leve todo frame p/ HUD (primeiro e último vértice).
		float u0, v0, u1, v1;
		std::memcpy(&u0, repackBuf.data() + 28, 4);
		std::memcpy(&v0, repackBuf.data() + 32, 4);
		const size_t last = (size_t)(numVtx - 1) * kVertexStride;
		std::memcpy(&u1, repackBuf.data() + last + 28, 4);
		std::memcpy(&v1, repackBuf.data() + last + 32, 4);
		g_uvMinU = std::min(u0, u1);
		g_uvMaxU = std::max(u0, u1);
		g_uvMinV = std::min(v0, v1);
		g_uvMaxV = std::max(v0, v1);
		g_uvSampleValid = true;
	}

	HRESULT DrawCore(D3DPRIMITIVETYPE prim, const void* vdata, UINT vstride,
	                 UINT minIdx, UINT numVtx,
	                 const uint16_t* idx16, const uint32_t* idx32, UINT idxCount, INT baseVtx)
	{
		if (!progReady || !window)
			return D3D_OK;
		if (vstride == 0 || numVtx == 0)
			return D3D_OK;

		// D3D9: vértice lido = VB[baseVtx + idx]; a faixa usada começa em baseVtx + minIdx.
		const INT firstVtx = std::max<INT>(0, baseVtx + (INT)minIdx);
		const uint8_t* src = (const uint8_t*)vdata + (size_t)firstVtx * vstride;

		auto* vs = static_cast<D3D9VS*>(curVS);
		const bool vsPath = vs && !repack.rhw && EnsureVsProgram(vs);
		curProg = vsPath ? &vs->prog : &ffp;

		if (vsPath)
			UploadVsVertices(vs, src, vstride, numVtx);
		else
			RepackFfpVertices(src, vstride, numVtx);

		BindFbo(); // desenho vai para o alvo corrente (RT FBO ou backbuffer)
		glViewport(0, 0, (GLsizei)CurW(), (GLsizei)CurH());
		// SetRenderState só atualiza o cache D3D9; reaplicar aqui é obrigatório
		// porque a UI troca alpha/z/cull entre draws (TMFont2 usa state block 2).
		// Sem isso, o atlas de fonte inteiro vira um retângulo opaco.
		ApplyGlState();
		if (repack.rhw)
		{
			// UI/D3DXSprite chega em RHW e usa alpha por textura. Não depender
			// apenas do cache RenderDevice: state blocks antigos podem ter sido
			// aplicados antes de outro draw e deixar o GLES com blend desligado.
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
		}
		if (!vsPath)
		{
			glBindVertexArray(ffp.vao);
			const GLuint vb = streamRing.NextVbo();
			UploadStream(GL_ARRAY_BUFFER, vb, repackBuf.data(), (GLsizeiptr)repackBuf.size());
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)kVertexStride, (void*)0);
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)kVertexStride, (void*)12);
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)kVertexStride, (void*)24);
			glEnableVertexAttribArray(3);
			glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, (GLsizei)kVertexStride, (void*)28);
			glEnableVertexAttribArray(4);
			glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, (GLsizei)kVertexStride, (void*)36);
		}

		glUseProgram(curProg->prog);
		SetShaderState();
		BindTexturesAndSetStageUniforms();
		if (vsPath)
		{
			if (vs->uC >= 0)
				glUniform4fv(vs->uC, 96, vsConst);
			if (vs->uYFlip >= 0)
				glUniform1f(vs->uYFlip, curRT ? -1.0f : 1.0f);
			// Com vertex shader e sem fog de tabela, o D3D9 usa oFog como fator.
			if (vs->prog.uFogMode >= 0 && (rs[D3DRS_FOGTABLEMODE] == D3DFOG_NONE))
				glUniform1i(vs->prog.uFogMode, 100);
		}
		curProg = &ffp;

		if (idx16 || idx32)
		{
			// GLES3: índices no ELEMENT_ARRAY_BUFFER (ponteiro CPU = draw no-op).
			const GLuint ebo = streamRing.CurEbo();
			if (idx16)
			{
				std::vector<uint16_t> adj16(idxCount);
				for (UINT i = 0; i < idxCount; ++i)
				{
					const int v = (int)idx16[i] - (int)minIdx;
					adj16[i] = (uint16_t)std::max(0, v);
				}
				UploadStream(GL_ELEMENT_ARRAY_BUFFER, ebo, adj16.data(),
					(GLsizeiptr)adj16.size() * 2);
				glDrawElements(PrimToGL(prim), (GLsizei)idxCount, GL_UNSIGNED_SHORT, (const void*)0);
			}
			else
			{
				std::vector<uint32_t> adj32(idxCount);
				for (UINT i = 0; i < idxCount; ++i)
				{
					const int v = (int)idx32[i] - (int)minIdx;
					adj32[i] = (uint32_t)std::max(0, v);
				}
				UploadStream(GL_ELEMENT_ARRAY_BUFFER, ebo, adj32.data(),
					(GLsizeiptr)adj32.size() * 4);
				glDrawElements(PrimToGL(prim), (GLsizei)idxCount, GL_UNSIGNED_INT, (const void*)0);
			}
		}
		else
			glDrawArrays(PrimToGL(prim), 0, (GLsizei)numVtx);

		++frameNo;
		if ((frameNo % 3600) == 0)
			SLog("[GLES9] frames=%u vivo (verts=%u idx=%u)", frameNo, numVtx, idxCount);
		return D3D_OK;
	}

	size_t CurStride()
	{
		if (curDecl)
		{
			VertexRepacker tmp;
			tmp.FromDecl(curDecl->elements);
			return tmp.srcStride;
		}
		VertexRepacker tmp;
		tmp.FromFVF(curFVF);
		return tmp.srcStride;
	}

	// ------------------------------------------------------------ interface
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return S_OK; }
	UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return 256 * 1024 * 1024; }
	HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** d) override { if (d) { *d = (IDirect3D9*)d3d; AddRefObj((IDirect3D9*)d3d); } return S_OK; }
	HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* c) override { FillCaps9(c); return S_OK; }
	HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT, D3DDISPLAYMODE* m) override
	{
		if (m) { m->Width = pp.BackBufferWidth; m->Height = pp.BackBufferHeight; m->Format = D3DFMT_X8R8G8B8; m->RefreshRate = 60; }
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override { if (p) { p->hFocusWindow = (HWND)window; p->DeviceType = D3DDEVTYPE_HAL; } return S_OK; }
	HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT, UINT, IDirect3DSurface9*) override { return S_OK; }
	void STDMETHODCALLTYPE SetCursorPosition(int, int, DWORD) override {}
	WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL b) override { return b; }
	HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetSwapChain(UINT, IDirect3DSwapChain9**) override { return E_NOTIMPL; }
	UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return 0; }
	HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* p) override
	{
		if (p)
			pp = *p;
		vp.Width = pp.BackBufferWidth;
		vp.Height = pp.BackBufferHeight;
		glViewport(0, 0, pp.BackBufferWidth, pp.BackBufferHeight);
		SDL_GL_SetSwapInterval(pp.PresentationInterval == 0 ? 0 : 1);
		// Reset pode mudar o tamanho do backbuffer: recriar o FBO do backbuffer
		if (fboBack && (bbW != (int)pp.BackBufferWidth || bbH != (int)pp.BackBufferHeight))
		{
			glDeleteFramebuffers(1, &fboBack);
			glDeleteTextures(1, &fboBackTex);
			glDeleteRenderbuffers(1, &fboBackDepth);
			fboBack = fboBackTex = fboBackDepth = 0;
		}
		EnsureBackFbo();
		SLog("[GLES9] Reset %ux%u", (unsigned)pp.BackBufferWidth, (unsigned)pp.BackBufferHeight);
		return S_OK;
	}
	// Captura do default FB em sdmc:/switch/client748/shots/ (o álbum do sistema
	// não grava screenshots de homebrew em modo applet).
	void MaybeCaptureFrame()
	{
		static const unsigned kShotFrames[] = { 300, 900, 1800, 3600, 7200 };
		static unsigned presentNo = 0;
		++presentNo;
		int idx = -1;
		for (int i = 0; i < (int)(sizeof(kShotFrames) / sizeof(kShotFrames[0])); ++i)
			if (presentNo == kShotFrames[i])
				idx = i;
		if (idx < 0 || !window)
			return;
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(window, &w, &h);
		if (w <= 0 || h <= 0)
			return;
		std::vector<uint8_t> px((size_t)w * h * 4);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
		for (size_t i = 0; i < px.size(); i += 4)
		{
			std::swap(px[i], px[i + 2]);
			px[i + 3] = 255;
		}
		mkdir("sdmc:/switch/client748/shots", 0777);
		char path[128];
		std::snprintf(path, sizeof(path), "sdmc:/switch/client748/shots/shot%d.tga", idx);
		if (FILE* f = std::fopen(path, "wb"))
		{
			uint8_t hdr[18] = {};
			hdr[2] = 2; // truecolor sem compressão
			hdr[12] = (uint8_t)(w & 0xFF);
			hdr[13] = (uint8_t)(w >> 8);
			hdr[14] = (uint8_t)(h & 0xFF);
			hdr[15] = (uint8_t)(h >> 8);
			hdr[16] = 32;
			hdr[17] = 8; // 8 bits alpha, origem bottom-left (igual ao glReadPixels)
			std::fwrite(hdr, 1, sizeof(hdr), f);
			std::fwrite(px.data(), 1, px.size(), f);
			std::fclose(f);
			SLog("[SHOT] %s %dx%d present=%u probe=%d", path, w, h, presentNo, g_probeMode);
		}
	}
	HRESULT STDMETHODCALLTYPE Present(const RECT*, const RECT*, HWND, const RGNDATA*) override
	{
		BindFbo(); // garantir bind correto p/ o blit de volta ao default
		if (curRT == nullptr && fboBack && !bbDirect && window)
		{
			int dw = 0, dh = 0;
			SDL_GetWindowSizeInPixels(window, &dw, &dh);
			if (dw < 1)
				dw = bbW;
			if (dh < 1)
				dh = bbH;
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fboBack);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			glBlitFramebuffer(0, 0, bbW, bbH, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			static bool loggedBlit = false;
			if (!loggedBlit)
			{
				loggedBlit = true;
				SLog("[GLES9] Present blit FBO %dx%d → window %dx%d", bbW, bbH, dw, dh);
			}
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		MaybeCaptureFrame();
		if (curRT) // fecha o alvo corrente soltando a ref de SetRenderTarget —
		{		   // sem isso, +1 ref vazada por frame com RT ativo
			curRT->Release();
			curRT = nullptr;
		}

		// HUD (default ON; WYD_HUD=0 desliga): desenhado no default framebuffer,
		// depois do blit e antes do swap — a tela é o canal de diagnóstico sem PC.
		static const int hudOn = []()
		{ const char* e = std::getenv("WYD_HUD"); return !(e && e[0] == '0'); }();
		if (hudOn && window)
		{
			static Uint32 lastTick = 0;
			static unsigned acc = 0;
			const Uint32 now = SDL_GetTicks();
			++acc;
			if (now - lastTick >= 1000)
			{
				g_fps = acc;
				acc = 0;
				lastTick = now;
			}
			int pw = 0, ph = 0;
			SDL_GetWindowSizeInPixels(window, &pw, &ph);
			static int s_hud9 = -1;
		if (s_hud9 < 0)
		{
			const char* e = std::getenv("WYD_SWITCH_HUD");
			s_hud9 = (e && e[0] == '1') ? 1 : 0;
		}
		if (s_hud9 && wyd9::Hud9Init())
			{
				wyd9::Hud9State hs;
				hs.fps = g_fps;
				hs.texCreated = g_texCreated;
				hs.texUploaded = g_texUploaded;
				hs.badFmtCount = g_badFmtCount;
				for (unsigned i = 0; i < g_badFmtCount && i < 4; ++i)
					hs.badFmt[i] = g_badFmt[i];
				hs.lastErr = g_lastErr;
				hs.frame = frameNo;
				hs.w = pw;
				hs.h = ph;
#if defined(WYD_TEX_PROBE) && WYD_TEX_PROBE
				hs.probeMode = g_probeMode;
#else
				hs.probeMode = -1;
#endif
				hs.s3tc = g_s3tcAvailable ? 1 : 0;
				hs.uploadHw = g_uploadHw;
				hs.uploadCpu = g_uploadCpu;
				hs.lastChk = g_lastChk;
				if (g_uvSampleValid)
				{
					hs.uvMinU = g_uvMinU;
					hs.uvMaxU = g_uvMaxU;
					hs.uvMinV = g_uvMinV;
					hs.uvMaxV = g_uvMaxV;
				}
				wyd9::Hud9Draw(hs);
			}
		}

		if (window)
			SDL_GL_SwapWindow(window);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** s) override
	{
		if (!s)
			return E_POINTER;
		auto* b = new D3D9Surf();
		b->w = pp.BackBufferWidth;
		b->h = pp.BackBufferHeight;
		b->fmt = D3DFMT_A8R8G8B8;
		b->backbuf = true;
		*s = b;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT, D3DRASTER_STATUS* r) override { if (r) { r->InVBlank = TRUE; r->ScanLine = 0; } return S_OK; }
	HRESULT STDMETHODCALLTYPE SetDialogBoxMode(WINBOOL) override { return S_OK; }
	void STDMETHODCALLTYPE SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP*) override {}
	void STDMETHODCALLTYPE GetGammaRamp(UINT, D3DGAMMARAMP* r) override { if (r) std::memset(r, 0, sizeof(*r)); }
	HRESULT STDMETHODCALLTYPE CreateTexture(UINT w, UINT h, UINT, DWORD usage, D3DFORMAT fmt, D3DPOOL, IDirect3DTexture9** tex, HANDLE*) override
	{
		if (!tex)
			return E_POINTER;
		auto* t = new D3D9Tex();
		t->w = w;
		t->h = h;
		t->fmt = fmt;
		t->usage = usage;
		++g_texCreated;
		if (usage & D3DUSAGE_RENDERTARGET)
		{
			// D3DXCreateTexture(USAGE_RENDERTARGET + POOL_MANAGED) do jogo: FBO real
			t->rt = new D3D9RT();
			t->rt->w = w;
			t->rt->h = h;
			t->cpu.clear();
			t->pitch = 0;
			*tex = t;
			return S_OK;
		}
		if (fmt == D3DFMT_DXT1 || fmt == D3DFMT_DXT3 || fmt == D3DFMT_DXT5)
		{
			t->pitch = (fmt == D3DFMT_DXT1 ? 8u : 16u) * ((w + 3) / 4);
			t->cpu.resize(DxtBytes(fmt, (int)w, (int)h));
		}
		else
		{
			t->pitch = w * FmtBpp(fmt);
			t->cpu.resize(t->pitch * h);
		}
		*tex = t;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9**, HANDLE*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9**, HANDLE*) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT len, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9** vb, HANDLE*) override
	{
		if (!vb)
			return E_POINTER;
		auto* b = new D3D9VB();
		b->size = len;
		b->data.resize(len);
		*vb = b;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT len, DWORD, D3DFORMAT fmt, D3DPOOL, IDirect3DIndexBuffer9** ib, HANDLE*) override
	{
		if (!ib)
			return E_POINTER;
		auto* b = new D3D9IB();
		b->size = len;
		b->fmt = (fmt == D3DFMT_INDEX32) ? D3DFMT_INDEX32 : D3DFMT_INDEX16;
		b->data.resize(len);
		*ib = b;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9** s, HANDLE*) override
	{
		if (!s)
			return E_POINTER;
		auto* r = new D3D9Surf();
		r->w = w;
		r->h = h;
		r->fmt = fmt == D3DFMT_UNKNOWN ? D3DFMT_A8R8G8B8 : fmt;
		r->rt = new D3D9RT();
		r->rt->w = w;
		r->rt->h = h;
		*s = r;
		SLog("[GLES9] CreateRenderTarget %ux%u fmt=0x%x", (unsigned)w, (unsigned)h, (unsigned)r->fmt);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9** s, HANDLE*) override
	{
		if (!s)
			return E_POINTER;
		auto* r = new D3D9Surf();
		r->w = w;
		r->h = h;
		r->fmt = fmt;
		*s = r;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* src, const RECT* srcRect, IDirect3DSurface9* dst, const POINT* dstPt) override
	{
		if (!src || !dst)
			return E_POINTER;
		auto* s = (D3D9Surf*)src;
		auto* d = (D3D9Surf*)dst;
		const uint8_t* sBits = nullptr;
		uint8_t* dBits = nullptr;
		UINT sPitch = 0, dPitch = 0, sW = 0, sH = 0, dW = 0, dH = 0;
		DWORD sFmt = 0, dFmt = 0;
		if (s->owner)
		{
			sBits = s->owner->cpu.data();
			sPitch = s->owner->pitch;
			sW = s->owner->w;
			sH = s->owner->h;
			sFmt = s->owner->fmt;
		}
		else if (!s->cpu.empty())
		{
			sBits = s->cpu.data();
			sPitch = s->w * FmtBpp(s->fmt);
			sW = s->w;
			sH = s->h;
			sFmt = s->fmt;
		}
		if (d->owner)
		{
			dBits = d->owner->cpu.data();
			dPitch = d->owner->pitch;
			dW = d->owner->w;
			dH = d->owner->h;
			dFmt = d->owner->fmt;
		}
		else if (!d->cpu.empty())
		{
			dBits = d->cpu.data();
			dPitch = d->w * FmtBpp(d->fmt);
			dW = d->w;
			dH = d->h;
			dFmt = d->fmt;
		}
		if (!sBits || !dBits || sFmt != dFmt || sFmt == D3DFMT_DXT1 || sFmt == D3DFMT_DXT3 || sFmt == D3DFMT_DXT5)
			return E_NOTIMPL;
		RECT sr = srcRect ? *srcRect : RECT{0, 0, (LONG)sW, (LONG)sH};
		POINT dp = dstPt ? *dstPt : POINT{0, 0};
		const int copyW = (int)(sr.right - sr.left);
		const int copyH = (int)(sr.bottom - sr.top);
		if (copyW <= 0 || copyH <= 0)
			return S_OK;
		if (dp.x < 0 || dp.y < 0 || sr.left < 0 || sr.top < 0)
			return D3DERR_INVALIDCALL;
		if ((UINT)(dp.x + copyW) > dW || (UINT)(dp.y + copyH) > dH)
			return D3DERR_INVALIDCALL;
		if ((UINT)sr.right > sW || (UINT)sr.bottom > sH)
			return D3DERR_INVALIDCALL;
		const UINT bpp = FmtBpp(sFmt);
		for (int y = 0; y < copyH; ++y)
		{
			const uint8_t* rowS = sBits + (size_t)(sr.top + y) * sPitch + (size_t)sr.left * bpp;
			uint8_t* rowD = dBits + (size_t)(dp.y + y) * dPitch + (size_t)dp.x * bpp;
			std::memcpy(rowD, rowS, (size_t)copyW * bpp);
		}
		if (d->owner)
			d->owner->dirty = true;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* src, IDirect3DBaseTexture9* dst) override
	{
		if (!src || !dst)
			return E_POINTER;
		auto* s = (D3D9Tex*)src;
		auto* d = (D3D9Tex*)dst;
		if (s->w != d->w || s->h != d->h || s->fmt != d->fmt)
			return D3DERR_INVALIDCALL;
		if (s->cpu.empty() || d->cpu.size() != s->cpu.size())
			return E_NOTIMPL;
		std::memcpy(d->cpu.data(), s->cpu.data(), s->cpu.size());
		d->dirty = true;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* src, IDirect3DSurface9* dst) override
	{
		if (!src || !dst)
			return E_POINTER;
		auto* s = (D3D9Surf*)src;
		auto* d = (D3D9Surf*)dst;
		// Só existe cópia real RT→superfície-CPU (owner). Sem par copiável,
		// falhar EXPLÍCITO — nunca S_OK silencioso (regra do PORT.md).
		if (!s->rt || !d->owner)
			return E_NOTIMPL;
		s->rt->ResolveToCpu();
		const size_t n = std::min(s->rt->cpu.size(), d->owner->cpu.size());
		if (n)
		{
			std::memcpy(d->owner->cpu.data(), s->rt->cpu.data(), n);
			d->owner->dirty = true;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT, IDirect3DSurface9*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* src, const RECT* srcR, IDirect3DSurface9* dst, const RECT* dstR, D3DTEXTUREFILTERTYPE filt) override
	{
		if (!src || !dst)
			return E_POINTER;
		auto* s = (D3D9Surf*)src;
		auto* d = (D3D9Surf*)dst;
		if (!s->rt || !d->rt)
			return S_OK; // caminhos CPU (owned surfaces) seguem como estavam
		if (!s->rt->Ensure() || !d->rt->Ensure())
			return D3DERR_INVALIDCALL;
		RECT sr = srcR ? *srcR : RECT{0, 0, (LONG)s->w, (LONG)s->h};
		RECT dr = dstR ? *dstR : RECT{0, 0, (LONG)d->w, (LONG)d->h};
		if (sr.right <= sr.left || sr.bottom <= sr.top || dr.right <= dr.left || dr.bottom <= dr.top)
			return S_OK;
		// glBlitFramebuffer (GLES3): resolve escala (filt) e evita o feedback
		// loop de escrever via glCopyTexSubImage2D na textura anexada ao FBO
		// de destino. Y: D3D top-left → GL bottom-left em AMBOS os lados.
		glBindFramebuffer(GL_READ_FRAMEBUFFER, s->rt->fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d->rt->fbo);
		glDisable(GL_SCISSOR_TEST);
		glBlitFramebuffer((GLint)sr.left, (GLint)(s->h - sr.bottom), (GLint)sr.right, (GLint)(s->h - sr.top),
		                  (GLint)dr.left, (GLint)(d->h - dr.bottom), (GLint)dr.right, (GLint)(d->h - dr.top),
		                  GL_COLOR_BUFFER_BIT, filt == D3DTEXF_LINEAR ? GL_LINEAR : GL_NEAREST);
		BindFbo();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* surf, const RECT* rect, D3DCOLOR color) override
	{
		if (!surf)
			return E_POINTER;
		auto* s = (D3D9Surf*)surf;
		if (!s->rt)
			return S_OK; // superfícies CPU: o conteúdo é gerenciado pelo jogo/shim
		if (!s->rt->Ensure())
			return D3DERR_INVALIDCALL;
		RECT r = rect ? *rect : RECT{0, 0, (LONG)s->w, (LONG)s->h};
		if (r.right <= r.left || r.bottom <= r.top)
			return S_OK;
		glBindFramebuffer(GL_FRAMEBUFFER, s->rt->fbo);
		glEnable(GL_SCISSOR_TEST);
		glScissor((GLint)r.left, (GLint)(s->h - r.bottom), r.right - r.left, r.bottom - r.top);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(((color >> 16) & 0xFF) / 255.f, ((color >> 8) & 0xFF) / 255.f,
		             (color & 0xFF) / 255.f, ((color >> 24) & 0xFF) / 255.f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
		BindFbo(); // volta ao alvo CORRENTE (pode ser um RT, não o backbuffer)
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL, IDirect3DSurface9** s, HANDLE*) override
	{
		if (!s)
			return E_POINTER;
		auto* r = new D3D9Surf();
		r->w = w;
		r->h = h;
		r->fmt = fmt;
		*s = r;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD i, IDirect3DSurface9* s) override
	{
		if (i != 0)
			return S_OK; // MRT não suportado (o jogo usa só o slot 0)
		if (curRT)
		{
			curRT->Release();
			curRT = nullptr;
		}
		if (s)
		{
			auto* r = (D3D9Surf*)s;
			if (r->rt)
			{
				curRT = r->rt;
				curRT->AddRef();
			}
			else
				SLog("[GLES9] SetRenderTarget: superfície sem FBO — segue no backbuffer");
		}
		BindFbo();
		ApplyGlState();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD, IDirect3DSurface9** s) override
	{
		if (!s)
			return E_POINTER;
		if (curRT)
		{
			auto* b = new D3D9Surf();
			b->w = curRT->w;
			b->h = curRT->h;
			b->fmt = D3DFMT_A8R8G8B8;
			b->rt = curRT;
			curRT->AddRef();
			*s = b;
		}
		else
		{
			// MESMO proxy por chamada: o jogo salva/restaura este ponteiro
			if (!backBufSurf)
			{
				backBufSurf = new D3D9Surf();
				backBufSurf->w = pp.BackBufferWidth;
				backBufSurf->h = pp.BackBufferHeight;
				backBufSurf->fmt = D3DFMT_A8R8G8B8;
				backBufSurf->backbuf = true;
			}
			backBufSurf->AddRef();
			*s = backBufSurf;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* s) override
	{
		if (curDS)
		{
			curDS->Release();
			curDS = nullptr;
		}
		if (s)
		{
			curDS = (D3D9Surf*)s;
			curDS->AddRef();
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** s) override { if (s) { *s = curDS; if (curDS) curDS->AddRef(); } return S_OK; }
	HRESULT STDMETHODCALLTYPE BeginScene() override { BindFbo(); ApplyGlState(); return S_OK; }
	HRESULT STDMETHODCALLTYPE EndScene() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE Clear(DWORD Count, const D3DRECT* pRects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override
	{
		BindFbo();
		const GLint vpY = (GLint)CurH() - (GLint)(vp.Y + vp.Height);
		glViewport(vp.X, vpY, vp.Width, vp.Height);
		GLbitfield mask = 0;
		if (flags & D3DCLEAR_TARGET)
		{
			glClearColor(((color >> 16) & 0xFF) / 255.f, ((color >> 8) & 0xFF) / 255.f, (color & 0xFF) / 255.f, ((color >> 24) & 0xFF) / 255.f);
			mask |= GL_COLOR_BUFFER_BIT;
		}
		if (flags & D3DCLEAR_ZBUFFER)
		{
			glClearDepthf(z);
			mask |= GL_DEPTH_BUFFER_BIT;
		}
		if (flags & D3DCLEAR_STENCIL)
		{
			glClearStencil((GLint)stencil);
			mask |= GL_STENCIL_BUFFER_BIT;
		}
		if (!mask)
			return S_OK;
		// D3D9 Clear só limpa o viewport (ou pRects); glClear limpa o FBO inteiro
		// sem scissor — portrait/RT mid-frame apagava o mundo atrás das paredes.
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);
		glStencilMask(0xFF);
		const GLboolean hadScissor = glIsEnabled(GL_SCISSOR_TEST);
		GLint prevBox[4] = {0, 0, 0, 0};
		if (hadScissor)
			glGetIntegerv(GL_SCISSOR_BOX, prevBox);
		glEnable(GL_SCISSOR_TEST);
		if (Count > 0 && pRects)
		{
			for (DWORD i = 0; i < Count; ++i)
			{
				const D3DRECT& r = pRects[i];
				const GLint w = (GLint)(r.x2 - r.x1);
				const GLint h = (GLint)(r.y2 - r.y1);
				if (w <= 0 || h <= 0)
					continue;
				glScissor((GLint)r.x1, (GLint)CurH() - ((GLint)r.y1 + h), w, h);
				glClear(mask);
			}
		}
		else if ((GLint)vp.Width > 0 && (GLint)vp.Height > 0)
		{
			glScissor(vp.X, vpY, (GLint)vp.Width, (GLint)vp.Height);
			glClear(mask);
		}
		if (hadScissor)
			glScissor(prevBox[0], prevBox[1], prevBox[2], prevBox[3]);
		else
			glDisable(GL_SCISSOR_TEST);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE st, const D3DMATRIX* m) override
	{
		if (!m)
			return E_POINTER;
		if (st == D3DTS_WORLD) world = *m;
		else if (st == D3DTS_VIEW) view = *m;
		else if (st == D3DTS_PROJECTION) proj = *m;
		else if (st >= D3DTS_TEXTURE0 && st <= D3DTS_TEXTURE7)
			texMatrix[(int)st - (int)D3DTS_TEXTURE0] = *m;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE st, D3DMATRIX* m) override
	{
		if (!m)
			return E_POINTER;
		if (st == D3DTS_WORLD) *m = world;
		else if (st == D3DTS_VIEW) *m = view;
		else if (st == D3DTS_PROJECTION) *m = proj;
		else if (st >= D3DTS_TEXTURE0 && st <= D3DTS_TEXTURE7)
			*m = texMatrix[(int)st - (int)D3DTS_TEXTURE0];
		else
			*m = ident;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE st, const D3DMATRIX* m) override
	{
		if (!m)
			return E_POINTER;
		D3DMATRIX& s = st == D3DTS_WORLD ? world : st == D3DTS_VIEW ? view : st == D3DTS_PROJECTION ? proj : ident;
		D3DMATRIX o;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				o.m[r][c] = s.m[r][0] * m->m[0][c] + s.m[r][1] * m->m[1][c] + s.m[r][2] * m->m[2][c] + s.m[r][3] * m->m[3][c];
		s = o;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* v) override
	{
		if (!v)
			return E_POINTER;
		vp = *v;
		glViewport(vp.X, (GLint)CurH() - (GLint)(vp.Y + vp.Height), vp.Width, vp.Height);
		glDepthRangef(vp.MinZ, vp.MaxZ);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* v) override { if (v) *v = vp; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9* m) override { if (m) mat = *m; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* m) override { if (m) *m = mat; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetLight(DWORD i, const D3DLIGHT9* l) override { if (i < 8 && l) lights[i] = *l; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetLight(DWORD i, D3DLIGHT9* l) override { if (i < 8 && l) *l = lights[i]; return S_OK; }
	HRESULT STDMETHODCALLTYPE LightEnable(DWORD i, WINBOOL on) override { if (i < 8) lightOn[i] = on != 0; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD i, WINBOOL* on) override { if (on && i < 8) *on = lightOn[i] ? TRUE : FALSE; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD, const float*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD, float*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE st, DWORD v) override
	{
		if (st < 256)
			rs[st] = v;
		switch (st)
		{
			case D3DRS_ZENABLE: zEnable = v != 0; break;
			case D3DRS_ZWRITEENABLE: zWrite = v != 0; break;
			case D3DRS_ZFUNC: zFunc = v; break;
			case D3DRS_CULLMODE: cull = v; break;
			case D3DRS_ALPHABLENDENABLE: alphaBlend = v != 0; break;
			case D3DRS_SRCBLEND: srcBlend = v; break;
			case D3DRS_DESTBLEND: dstBlend = v; break;
			case D3DRS_ALPHATESTENABLE: alphaTest = v != 0; break;
			case D3DRS_ALPHAREF: alphaRef = v; break;
			case D3DRS_ALPHAFUNC: alphaFunc = v; break;
			case D3DRS_FOGENABLE: fogEnable = v != 0; break;
			case D3DRS_FOGCOLOR: fogColor = v; break;
			case D3DRS_FOGSTART: fogStart = *(const float*)&v; break;
			case D3DRS_FOGEND: fogEnd = *(const float*)&v; break;
			case D3DRS_FOGDENSITY: fogDensity = *(const float*)&v; break;
			case D3DRS_FOGVERTEXMODE: fogMode = v; break;
			case D3DRS_TEXTUREFACTOR: texFactor = v; break;
			case D3DRS_SCISSORTESTENABLE: scissorEnable = v != 0; break;
			case D3DRS_LIGHTING: lighting = v != 0; break;
			default: break;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE st, DWORD* v) override { if (v && st < 256) *v = rs[st]; return S_OK; }
	HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9** sb) override
	{
		if (!sb)
			return E_POINTER;
		auto* b = new D3D9StateBlock();
		b->dev = this;
		b->snap = MakeSnap();
		b->hasSnap = true;
		*sb = b;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** sb) override
	{
		if (!sb)
			return E_POINTER;
		auto* b = new D3D9StateBlock();
		b->dev = this;
		b->snap = MakeSnap();
		b->hasSnap = true;
		*sb = b;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetTexture(DWORD s, IDirect3DBaseTexture9** t) override { if (t) *t = s < 8 ? tex[s] : nullptr; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetTexture(DWORD s, IDirect3DBaseTexture9* t) override { if (s < 8) tex[s] = t; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD* v) override { if (v && s < 8 && t < 33) *v = tss[s][t]; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD v) override
	{
		if (s < 8 && t < 33)
		{
			tss[s][t] = v;
			if (t == D3DTSS_TEXCOORDINDEX && s < 3)
				tssUv[s] = (int)(v & 0xFFFF);
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD* v) override { if (v && s < 8 && t < 14) *v = smp[s][t]; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) override { if (s < 8 && t < 14) smp[s][t] = v; return S_OK; }
	HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* passes) override { if (passes) *passes = 1; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT, const PALETTEENTRY*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT, PALETTEENTRY*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* r) override { if (r) scissor = *r; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* r) override { if (r) *r = scissor; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(WINBOOL) override { return S_OK; }
	WINBOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return FALSE; }
	HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override { return S_OK; }
	float STDMETHODCALLTYPE GetNPatchMode() override { return 0.f; }
	HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE prim, UINT startVtx, UINT primCount) override
	{
		auto* vb = (D3D9VB*)curVB;
		if (!vb || vb->data.empty() || primCount == 0)
			return D3D_OK;
		if (curDecl)
			repack.FromDecl(curDecl->elements);
		else
			repack.FromFVF(curFVF);
		const UINT stride = curVBStride ? curVBStride : (UINT)repack.srcStride;
		const UINT numVtx = VertsForPrim(prim, primCount);
		return DrawCore(prim, vb->data.data() + curVBOffset, stride, startVtx, numVtx, nullptr, nullptr, 0, 0);
	}
	HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE prim, INT baseVtx, UINT minIdx, UINT numVtx, UINT startIdx, UINT primCount) override
	{
		auto* vb = (D3D9VB*)curVB;
		auto* ib = (D3D9IB*)curIB;
		if (!vb || !ib || vb->data.empty() || ib->data.empty() || primCount == 0)
			return D3D_OK;
		if (curDecl)
			repack.FromDecl(curDecl->elements);
		else
			repack.FromFVF(curFVF);
		const UINT stride = curVBStride ? curVBStride : (UINT)repack.srcStride;
		const UINT idxCount = VertsForPrim(prim, primCount);
		const UINT idxStride = (ib->fmt == D3DFMT_INDEX32) ? 4u : 2u;
		const uint8_t* vbase = vb->data.data() + curVBOffset;
		if (idxStride == 4)
			return DrawCore(prim, vbase, stride, minIdx, numVtx, nullptr,
			                (const uint32_t*)(ib->data.data() + (size_t)startIdx * 4), idxCount, baseVtx);
		return DrawCore(prim, vbase, stride, minIdx, numVtx,
		                (const uint16_t*)(ib->data.data() + (size_t)startIdx * 2), nullptr, idxCount, baseVtx);
	}
	HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE prim, UINT primCount, const void* data, UINT stride) override
	{
		if (!data || primCount == 0)
			return D3D_OK;
		if (curDecl)
			repack.FromDecl(curDecl->elements);
		else
			repack.FromFVF(curFVF);
		if (stride == 0)
			stride = (UINT)repack.srcStride;
		const UINT numVtx = VertsForPrim(prim, primCount);
		return DrawCore(prim, data, stride, 0, numVtx, nullptr, nullptr, 0, 0);
	}
	HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE prim, UINT minIdx, UINT numVtx, UINT primCount, const void* idxData, D3DFORMAT idxFmt, const void* vtxData, UINT stride) override
	{
		if (!idxData || !vtxData || primCount == 0)
			return D3D_OK;
		if (curDecl)
			repack.FromDecl(curDecl->elements);
		else
			repack.FromFVF(curFVF);
		if (stride == 0)
			stride = (UINT)repack.srcStride;
		const UINT idxCount = VertsForPrim(prim, primCount);
		if (idxFmt == D3DFMT_INDEX16)
			return DrawCore(prim, vtxData, stride, minIdx, numVtx, (const uint16_t*)idxData, nullptr, idxCount, 0);
		return DrawCore(prim, vtxData, stride, minIdx, numVtx, nullptr, (const uint32_t*)idxData, idxCount, 0);
	}
	HRESULT STDMETHODCALLTYPE ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*, DWORD) override { return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* el, IDirect3DVertexDeclaration9** decl) override
	{
		if (!decl)
			return E_POINTER;
		auto* d = new D3D9VDecl();
		if (el)
		{
			int c = 0;
			while (el[c].Stream != 0xFF && c < 64)
				++c;
			d->elements.assign(el, el + c);
		}
		*decl = d;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* d) override { curDecl = (D3D9VDecl*)d; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** d) override { if (d) *d = curDecl; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) override { curDecl = nullptr; curFVF = fvf; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetFVF(DWORD* fvf) override { if (fvf) *fvf = curDecl ? 0 : curFVF; return S_OK; }
	HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code, IDirect3DVertexShader9** sh) override
	{
		if (!code || !sh)
			return E_POINTER;
		if ((code[0] & 0xFFFF0000u) != 0xFFFE0000u)
			return D3DERR_INVALIDCALL;
		auto* s = new D3D9VS();
		size_t w = 0;
		while (w < 65536 && code[w] != 0x0000FFFFu)
			++w;
		s->bytecode.resize((w + 1) * 4);
		std::memcpy(s->bytecode.data(), code, s->bytecode.size());
		*sh = s;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* sh) override
	{
		curVS = sh;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** s) override { if (s) *s = (IDirect3DVertexShader9*)curVS; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT r, const float* d, UINT c) override
	{
		if (d && r + c <= 256)
			std::memcpy(vsConst + r * 4, d, (size_t)c * 16);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT r, float* d, UINT c) override
	{
		if (d && r + c <= 256)
			std::memcpy(d, vsConst + r * 4, (size_t)c * 16);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT, const int*, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT, int*, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT, const WINBOOL*, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT, WINBOOL*, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code, IDirect3DPixelShader9** sh) override
	{
		if (!code || !sh)
			return E_POINTER;
		if ((code[0] & 0xFFFF0000u) != 0xFFFF0000u)
			return D3DERR_INVALIDCALL;
		auto* s = new D3D9PS();
		size_t w = 0;
		while (w < 65536 && code[w] != 0x0000FFFFu)
			++w;
		s->bytecode.resize((w + 1) * 4);
		std::memcpy(s->bytecode.data(), code, s->bytecode.size());
		*sh = s;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* sh) override { curPS = sh; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** s) override { if (s) *s = (IDirect3DPixelShader9*)curPS; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT r, const float* d, UINT c) override
	{
		if (d && r + c <= 256)
			std::memcpy(psConst + r * 4, d, (size_t)c * 16);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT r, float* d, UINT c) override
	{
		if (d && r + c <= 256)
			std::memcpy(d, psConst + r * 4, (size_t)c * 16);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT, const int*, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT, int*, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT, const WINBOOL*, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT, WINBOOL*, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT, const float*, const D3DRECTPATCH_INFO*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT, const float*, const D3DTRIPATCH_INFO*) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE, IDirect3DQuery9**) override { return E_NOTIMPL; }

	// stream sources (membros extras, fora da vtable)
	void* curVB = nullptr;
	void* curIB = nullptr;
	UINT curVBStride = 0;
	UINT curVBOffset = 0;
	float vsConst[256 * 4] = {};
	float psConst[256 * 4] = {};

	HRESULT STDMETHODCALLTYPE SetStreamSource(UINT n, IDirect3DVertexBuffer9* vb, UINT off, UINT stride) override
	{
		if (n == 0)
		{
			curVB = vb;
			curVBStride = stride;
			curVBOffset = off;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetStreamSource(UINT n, IDirect3DVertexBuffer9** vb, UINT* off, UINT* stride) override
	{
		if (vb) *vb = n == 0 ? (IDirect3DVertexBuffer9*)curVB : nullptr;
		if (off) *off = n == 0 ? curVBOffset : 0;
		if (stride) *stride = curVBStride;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT, UINT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT, UINT* d) override { if (d) *d = 1; return S_OK; }
	HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* ib) override { curIB = ib; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ib) override { if (ib) *ib = (IDirect3DIndexBuffer9*)curIB; return S_OK; }
};

HRESULT STDMETHODCALLTYPE D3D9StateBlock::Capture()
{
	if (!dev)
		return D3DERR_INVALIDCALL;
	snap = dev->MakeSnap();
	hasSnap = true;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE D3D9StateBlock::Apply()
{
	if (!dev || !hasSnap)
		return D3DERR_INVALIDCALL;
	dev->ApplySnap(snap);
	return S_OK;
}

// ===========================================================================
// IDirect3D9
// ===========================================================================
struct D3D9Obj : IDirect3D9
{
	ULONG refCount = 1;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { if (ppv) { *ppv = this; AddRef(); return S_OK; } return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
	ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refCount; if (!n) delete this; return n; }
	HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) override { return S_OK; }
	UINT STDMETHODCALLTYPE GetAdapterCount() override { return 1; }
	HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9* p) override
	{
		if (p)
		{
			std::memset(p, 0, sizeof(*p));
			std::snprintf(p->Driver, sizeof(p->Driver), "gles3switch");
			std::snprintf(p->Description, sizeof(p->Description), "WYD D3D9-GLES3 (Switch)");
		}
		return S_OK;
	}
	UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT, D3DFORMAT) override { return 2; }
	HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT, D3DFORMAT, UINT mode, D3DDISPLAYMODE* m) override
	{
		if (!m)
			return E_POINTER;
		if (mode >= 2)
			return D3DERR_INVALIDCALL;
		if (mode == 0) { m->Width = 1280; m->Height = 720; }
		else { m->Width = 1920; m->Height = 1080; }
		m->Format = D3DFMT_X8R8G8B8;
		m->RefreshRate = 60;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* m) override
	{
		if (m) { m->Width = 1280; m->Height = 720; m->Format = D3DFMT_X8R8G8B8; m->RefreshRate = 60; }
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, WINBOOL) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, WINBOOL, D3DMULTISAMPLE_TYPE ms, DWORD* q) override
	{
		if (q) *q = 1;
		return ms == D3DMULTISAMPLE_NONE ? S_OK : D3DERR_NOTAVAILABLE;
	}
	HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9* caps) override { FillCaps9(caps); return S_OK; }
	HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override { return nullptr; }
	HRESULT STDMETHODCALLTYPE CreateDevice(UINT, D3DDEVTYPE, HWND hFocus, DWORD, D3DPRESENT_PARAMETERS* presp, IDirect3DDevice9** dev) override
	{
		if (!dev)
			return E_POINTER;
		auto* d = new D3D9Dev();
		d->d3d = this;
		d->window = (SDL_Window*)hFocus;
		if (presp)
			d->pp = *presp;
		else
		{
			d->pp.BackBufferWidth = 1280;
			d->pp.BackBufferHeight = 720;
			d->pp.Windowed = TRUE;
		}
		// Alinhar backbuffer ao tamanho real da NWindow (dock 720p/1080p).
		// config.txt com 1280x1024 + FBO interno → Present clipava → tela cinza.
		if (d->window)
		{
			int pw = 0, ph = 0;
			SDL_GetWindowSizeInPixels(d->window, &pw, &ph);
			if (pw > 0 && ph > 0)
			{
				d->pp.BackBufferWidth = (UINT)pw;
				d->pp.BackBufferHeight = (UINT)ph;
			}
		}
		if (!d->window)
		{
			SLog("[GLES9] CreateDevice sem HWND/SDL_Window");
			HudErr("NO WINDOW");
			delete d;
			return D3DERR_INVALIDCALL;
		}
		// Janela criada com attrs ES 3.0; shaders FFP em #version 300 es.
		// Preferir 3.0 (Mesa Switch), depois 3.1/3.2. Sem ES 2.0 (FFP precisa 3.x).
		static const struct { int maj, min; } kGl[] = {
			{3, 0}, {3, 1}, {3, 2},
		};
		d->ctx = nullptr;
		for (const auto& v : kGl)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v.maj);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v.min);
			SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
			SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
			d->ctx = SDL_GL_CreateContext(d->window);
			if (d->ctx)
			{
				SLog("[GLES9] SDL_GL_CreateContext OK ES %d.%d", v.maj, v.min);
				break;
			}
			SLog("[GLES9] ES %d.%d falhou: %s", v.maj, v.min, SDL_GetError());
		}
		if (!d->ctx)
		{
			SLog("[GLES9] SDL_GL_CreateContext falhou em todos os perfis");
			HudErr("CTX FAIL");
			delete d;
			return D3DERR_INVALIDCALL;
		}
		const char* ver = (const char*)glGetString(GL_VERSION);
		const char* rnd = (const char*)glGetString(GL_RENDERER);
		const char* ext = (const char*)glGetString(GL_EXTENSIONS);
		g_s3tcAvailable = ext && std::strstr(ext, "GL_EXT_texture_compression_s3tc") != nullptr;
		SLog("[GLES9] contexto %s | %s | s3tc=%d", ver ? ver : "?", rnd ? rnd : "?", g_s3tcAvailable ? 1 : 0);

		if (!BuildFfp(d->ffp))
		{
			SLog("[GLES9] FFP program não compilou");
			HudErr("FFP FAIL");
			SDL_GL_DestroyContext(d->ctx);
			delete d;
			return D3DERR_INVALIDCALL;
		}
		d->progReady = true;
		// Defaults D3D9 de texture stage (vazio = 0 quebrava ARG1=DIFFUSE vs TEXTURE).
		d->tss[0][D3DTSS_COLOROP] = D3DTOP_MODULATE;
		d->tss[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		d->tss[0][D3DTSS_COLORARG2] = D3DTA_CURRENT;
		d->tss[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
		d->tss[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		d->tss[0][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		for (int s = 1; s < 8; ++s)
		{
			d->tss[s][D3DTSS_COLOROP] = D3DTOP_DISABLE;
			d->tss[s][D3DTSS_ALPHAOP] = D3DTOP_DISABLE;
			d->tss[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
			d->tss[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
			d->tss[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
			d->tss[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		}
		for (int s = 0; s < 8; ++s)
		{
			d->smp[s][D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
			d->smp[s][D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
			d->smp[s][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
			d->smp[s][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
		}
		SDL_GL_SetSwapInterval(d->pp.PresentationInterval == 0 ? 0 : 1);
		d->vp.X = d->vp.Y = 0;
		d->vp.Width = d->pp.BackBufferWidth;
		d->vp.Height = d->pp.BackBufferHeight;
		d->vp.MinZ = 0.f;
		d->vp.MaxZ = 1.f;
		glViewport(0, 0, d->pp.BackBufferWidth, d->pp.BackBufferHeight);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		SLog("[GLES9] device criado %ux%u windowed=%d interval=%d",
		     (unsigned)d->pp.BackBufferWidth, (unsigned)d->pp.BackBufferHeight,
		     (int)d->pp.Windowed, d->pp.PresentationInterval == 0 ? 0 : 1);
		d->EnsureBackFbo(); // probe de DEPTH_BITS do default framebuffer
		*dev = d;
		return D3D_OK;
	}
};

} // namespace

// ---------------------------------------------------------------------------
// Direct3DCreate9
// ---------------------------------------------------------------------------
IDirect3D9* Direct3DCreate9(UINT sdk)
{
	SLog("[GLES9] Direct3DCreate9(%u) — backend GLES3 do Switch", sdk);
	return new D3D9Obj();
}

#endif // WYD_SWITCH
