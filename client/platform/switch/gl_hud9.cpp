// gl_hud9.cpp — HUD em GLES puro com microfonte 3x5 embutida (ver gl_hud9.h).
// Um draw call por frame: quads de fonte + fundo acumulados em um VBO stream.

#include "gl_hud9.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <GLES3/gl32.h>

namespace wyd9
{
namespace
{

// --- microfonte 3x5 ---------------------------------------------------------
// Cada glifo: 5 linhas x 3 bits ('#' = pixel aceso; LSB = coluna esquerda).
struct Glyph
{
	uint8_t rows[5];
};

constexpr Glyph MakeG(const char* d)
{
	Glyph g{};
	for (int r = 0; r < 5; ++r)
	{
		uint8_t v = 0;
		for (int c = 0; c < 3; ++c)
			if (d[r * 3 + c] == '#')
				v |= (uint8_t)(1u << c);
		g.rows[r] = v;
	}
	return g;
}

// Índices 0-9 dígitos, 10-35 A-Z, 36 ':' 37 '.' 38 '/' 39 'x' 40 '+' 41 '-' 42 '%' 43 espaço
const Glyph* Font()
{
	static const Glyph tab[] = {
		MakeG("###" "#.#" "#.#" "#.#" "###"), // 0
		MakeG(".#." ".#." ".#." ".#." ".#."), // 1
		MakeG("###" "..#" "###" "#.." "###"), // 2
		MakeG("###" "..#" "###" "..#" "###"), // 3
		MakeG("#.#" "#.#" "###" "..#" "..#"), // 4
		MakeG("###" "#.." "###" "..#" "###"), // 5
		MakeG("###" "#.." "###" "#.#" "###"), // 6
		MakeG("###" "..#" "..#" "..#" "..#"), // 7
		MakeG("###" "#.#" "###" "#.#" "###"), // 8
		MakeG("###" "#.#" "###" "..#" "###"), // 9
		MakeG("###" "#.#" "#.#" "#.#" "###"), // A
		MakeG("##." "#.#" "##." "#.#" "##."), // B
		MakeG("###" "#.." "#.." "#.." "###"), // C
		MakeG("##." "#.#" "#.#" "#.#" "##."), // D
		MakeG("###" "#.." "##." "#.." "###"), // E
		MakeG("###" "#.." "##." "#.." "#.."), // F
		MakeG("###" "#.." "#.#" "#.#" "###"), // G
		MakeG("#.#" "#.#" "###" "#.#" "#.#"), // H
		MakeG("###" ".#." ".#." ".#." "###"), // I
		MakeG("..#" "..#" "..#" "#.#" "###"), // J
		MakeG("#.#" "#.#" "##." "#.#" "#.#"), // K
		MakeG("#.." "#.." "#.." "#.." "###"), // L
		MakeG("#.#" "###" "###" "#.#" "#.#"), // M
		MakeG("##." "#.#" "#.#" "#.#" "#.#"), // N
		MakeG("###" "#.#" "#.#" "#.#" "###"), // O
		MakeG("###" "#.#" "###" "#.." "#.."), // P
		MakeG("###" "#.#" "#.#" "##." ".##"), // Q
		MakeG("###" "#.#" "##." "#.#" "#.#"), // R
		MakeG("###" "#.." "###" "..#" "###"), // S
		MakeG("###" ".#." ".#." ".#." ".#."), // T
		MakeG("#.#" "#.#" "#.#" "#.#" "###"), // U
		MakeG("#.#" "#.#" "#.#" "###" ".#."), // V
		MakeG("#.#" "#.#" "###" "###" "#.#"), // W
		MakeG("#.#" "#.#" ".#." "#.#" "#.#"), // X
		MakeG("#.#" "#.#" ".#." ".#." ".#."), // Y
		MakeG("###" "..#" ".#." "#.." "###"), // Z
		MakeG("..." ".#." "..." ".#." "..."), // 36 ':'
		MakeG("..." "..." "..." "..." ".#."), // 37 '.'
		MakeG("..#" "..#" ".#." "#.." "#.."), // 38 '/'
		MakeG("..." "#.#" ".#." "#.#" "..."), // 39 'x'
		MakeG("..." ".#." "###" ".#." "..."), // 40 '+'
		MakeG("..." "..." "###" "..." "..."), // 41 '-'
		MakeG(".#." "#.#" ".#." "#.#" ".#."), // 42 '%'
		MakeG("..." "..." "..." "..." "..."), // 43 espaço
	};
	return tab;
}

int GlyphIndex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'Z')
		return 10 + (c - 'A');
	if (c >= 'a' && c <= 'z')
		return 10 + (c - 'a');
	switch (c)
	{
		case ':': return 36;
		case '.': return 37;
		case '/': return 38;
		case '+': return 40;
		case '-': return 41;
		case '%': return 42;
		default: return 43; // espaço
	}
}

// --- pipeline ---------------------------------------------------------------
enum { kMaxVerts = 4096 };
struct Vtx
{
	float x, y, r, g, b;
};

Vtx g_v[kMaxVerts];
unsigned g_n = 0;
GLuint g_prog = 0, g_vbo = 0;
GLint g_a_pos = -1, g_a_col = -1, g_u_res = -1;

const char* const kVS =
	"attribute vec2 a_pos;\n"
	"attribute vec3 a_col;\n"
	"varying vec3 v_col;\n"
	"uniform vec2 u_res;\n"
	// y=0 no TOPO (convenção D3D/tela): o HUD lê de cima para baixo.
	"void main(){ v_col = a_col; gl_Position = vec4(a_pos.x / u_res.x * 2.0 - 1.0, 1.0 - a_pos.y / u_res.y * 2.0, 0.0, 1.0); }\n";

const char* const kFS =
	"precision mediump float;\n"
	"varying vec3 v_col;\n"
	"void main(){ gl_FragColor = vec4(v_col, 1.0); }\n";

const float kWhite[3] = {0.92f, 0.92f, 0.92f};
const float kYellow[3] = {0.95f, 0.80f, 0.20f};
const float kRed[3] = {1.0f, 0.25f, 0.20f};
const float kGrey[3] = {0.10f, 0.10f, 0.12f};
const float kDim[3] = {0.55f, 0.55f, 0.58f};

void Push(float x, float y, const float* c)
{
	if (g_n >= kMaxVerts)
		return;
	Vtx& v = g_v[g_n++];
	v.x = x; v.y = y; v.r = c[0]; v.g = c[1]; v.b = c[2];
}

void Quad(float x, float y, float w, float h, const float* c)
{
	Push(x, y, c); Push(x + w, y, c); Push(x, y + h, c);
	Push(x + w, y, c); Push(x + w, y + h, c); Push(x, y + h, c);
}

void GlyphDraw(const Glyph& g, float x, float y, float s, const float* c)
{
	for (int r = 0; r < 5; ++r)
		for (int col = 0; col < 3; ++col)
			if (g.rows[r] & (1u << col))
				Quad(x + col * s, y + r * s, s, s, c);
}

void Text(float x, float y, const char* str, const float* c, float s)
{
	for (; str && *str; ++str)
	{
		GlyphDraw(Font()[GlyphIndex(*str)], x, y, s, c);
		x += 4 * s;
	}
}

GLuint Compile(GLenum type, const char* src)
{
	const GLuint sh = glCreateShader(type);
	if (!sh)
		return 0;
	glShaderSource(sh, 1, &src, nullptr);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[256] = {0};
		glGetShaderInfoLog(sh, 255, nullptr, log);
		std::fprintf(stderr, "[HUD9] shader falhou: %s\n", log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

} // namespace

bool Hud9Init()
{
	if (g_prog)
		return true;
	const GLuint vs = Compile(GL_VERTEX_SHADER, kVS);
	const GLuint fs = Compile(GL_FRAGMENT_SHADER, kFS);
	if (!vs || !fs)
	{
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
		return false;
	}
	g_prog = glCreateProgram();
	glAttachShader(g_prog, vs);
	glAttachShader(g_prog, fs);
	glLinkProgram(g_prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[256] = {0};
		glGetProgramInfoLog(g_prog, 255, nullptr, log);
		std::fprintf(stderr, "[HUD9] link falhou: %s\n", log);
		glDeleteProgram(g_prog);
		g_prog = 0;
		return false;
	}
	g_a_pos = glGetAttribLocation(g_prog, "a_pos");
	g_a_col = glGetAttribLocation(g_prog, "a_col");
	g_u_res = glGetUniformLocation(g_prog, "u_res");
	glGenBuffers(1, &g_vbo);
	return g_vbo != 0;
}

void Hud9Draw(const Hud9State& st)
{
	if (!g_prog || !g_vbo || st.w <= 0 || st.h <= 0)
		return;

	const float s = (float)st.h / 240.0f; // 720p → 3 px/glifo; 1080p → 4.5
	const float px = 4.0f * s;            // avanço por caractere
	const float lh = 7.0f * s;            // avanço de linha
	// Canto inferior direito — não cobre HP/MP (canto superior esquerdo).
	const float x = (float)st.w - 8.0f * s - 52.0f * px;
	float y = (float)st.h - 8.0f * s - 4.0f * lh;

	char l1[80], l2[80], l3[80], l4[80];
	const char* err = st.lastErr ? st.lastErr : "";
#if defined(WYD_SWITCH_VERSION)
	std::snprintf(l1, sizeof(l1), "v%s  FPS %u  TX %u/%u", WYD_SWITCH_VERSION, st.fps, st.texUploaded, st.texCreated);
#else
	std::snprintf(l1, sizeof(l1), "FPS %u  TX %u/%u", st.fps, st.texUploaded, st.texCreated);
#endif
	if (st.probeMode >= 0)
	{
		static const char* kMode[] = { "UV", "SCREEN", "NORMAL" };
		const char* mn = (st.probeMode < 3) ? kMode[st.probeMode] : "?";
		std::snprintf(l2, sizeof(l2), "PROBE %d %s  X=CYCLE", st.probeMode, mn);
		std::snprintf(l3, sizeof(l3), "S3TC %d HW %u CPU %u CHK %llu",
			st.s3tc, st.uploadHw, st.uploadCpu, (unsigned long long)st.lastChk);
		std::snprintf(l4, sizeof(l4), "UV %.2f..%.2f %.2f..%.2f",
			st.uvMinU, st.uvMaxU, st.uvMinV, st.uvMaxV);
	}
	else
	{
		std::snprintf(l2, sizeof(l2), "BADFMT %u", st.badFmtCount);
		l3[0] = 0;
		l4[0] = 0;
	}

	g_n = 0;
	const int lines = st.probeMode >= 0 ? 4 : (err[0] ? 3 : 2);
	size_t ml = std::strlen(l1);
	ml = std::max(ml, std::strlen(l2));
	if (l3[0])
		ml = std::max(ml, std::strlen(l3));
	if (l4[0])
		ml = std::max(ml, std::strlen(l4));
	if (err[0])
		ml = std::max(ml, std::strlen(err));
	const float bw = (float)ml * px + 8.0f * s;
	for (int i = 0; i < lines; ++i)
		Quad(x - 4.0f * s, y - 2.0f * s + (float)i * lh, bw, 6.0f * s, kGrey);
	Text(x, y + 0.0f * lh, l1, kWhite, s);
	Text(x, y + 1.0f * lh, l2, st.probeMode >= 0 ? kYellow : (st.badFmtCount ? kYellow : kDim), s);
	if (st.probeMode >= 0)
	{
		Text(x, y + 2.0f * lh, l3, kDim, s);
		Text(x, y + 3.0f * lh, l4, kWhite, s);
	}
	else if (err[0])
	{
		const bool on = ((st.frame / 30u) % 2u) == 0u;
		Text(x, y + 2.0f * lh, err, on ? kRed : kWhite, s);
	}

	glUseProgram(g_prog);
	glUniform2f(g_u_res, (float)st.w, (float)st.h);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_n * sizeof(Vtx), g_v, GL_STREAM_DRAW);
	glEnableVertexAttribArray((GLuint)g_a_pos);
	glVertexAttribPointer((GLuint)g_a_pos, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(Vtx), (const void*)0);
	glEnableVertexAttribArray((GLuint)g_a_col);
	glVertexAttribPointer((GLuint)g_a_col, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(Vtx),
	                      (const void*)(2 * sizeof(float)));
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_n);
	glDisableVertexAttribArray((GLuint)g_a_pos);
	glDisableVertexAttribArray((GLuint)g_a_col);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

} // namespace wyd9
