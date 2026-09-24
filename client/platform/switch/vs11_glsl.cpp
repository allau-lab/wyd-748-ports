#include "vs11_glsl.h"

#include <cstdio>
#include <cstring>
#include <map>

namespace {

constexpr int kNumConst = 96; // vs_1_1: c0..c95

enum RegType
{
	kTemp = 0,
	kInput = 1,
	kConst = 2,
	kAddr = 3,
	kRastOut = 4,  // 0=oPos 1=oFog 2=oPts
	kAttrOut = 5,  // oD0/oD1
	kTexCrdOut = 6 // oT0..oT7
};

int RegTypeOf(uint32_t tok) { return (int)(((tok >> 28) & 0x7u) | ((tok >> 8) & 0x18u)); }
int RegNumOf(uint32_t tok) { return (int)(tok & 0x7FFu); }

struct Op
{
	int srcCount;
	const char* name;
};

// SM 1.x não codifica o tamanho da instrução: número de fontes por opcode.
bool OpInfo(uint32_t op, Op& out)
{
	switch (op)
	{
		case 0x00: out = { 0, "nop" }; return true;
		case 0x01: out = { 1, "mov" }; return true;
		case 0x02: out = { 2, "add" }; return true;
		case 0x03: out = { 2, "sub" }; return true;
		case 0x04: out = { 3, "mad" }; return true;
		case 0x05: out = { 2, "mul" }; return true;
		case 0x06: out = { 1, "rcp" }; return true;
		case 0x07: out = { 1, "rsq" }; return true;
		case 0x08: out = { 2, "dp3" }; return true;
		case 0x09: out = { 2, "dp4" }; return true;
		case 0x0A: out = { 2, "min" }; return true;
		case 0x0B: out = { 2, "max" }; return true;
		case 0x0C: out = { 2, "slt" }; return true;
		case 0x0D: out = { 2, "sge" }; return true;
		case 0x0E: out = { 1, "exp" }; return true;
		case 0x0F: out = { 1, "log" }; return true;
		case 0x10: out = { 1, "lit" }; return true;
		case 0x11: out = { 2, "dst" }; return true;
		case 0x12: out = { 3, "lrp" }; return true;
		case 0x13: out = { 1, "frc" }; return true;
		case 0x14: out = { 2, "m4x4" }; return true;
		case 0x15: out = { 2, "m4x3" }; return true;
		case 0x16: out = { 2, "m3x4" }; return true;
		case 0x17: out = { 2, "m3x3" }; return true;
		case 0x18: out = { 2, "m3x2" }; return true;
		case 0x4E: out = { 1, "expp" }; return true;
		case 0x4F: out = { 1, "logp" }; return true;
		default: return false;
	}
}

class Emitter
{
public:
	std::map<int, std::string> defs; // def c# -> literal
	std::string body;
	std::string error;
	uint32_t usedInputs = 0;

	// Registrador cru (sem swizzle/modificador); offset soma no índice (linhas de matriz).
	std::string Reg(uint32_t tok, int offset = 0)
	{
		const int type = RegTypeOf(tok);
		const int num = RegNumOf(tok) + offset;
		char buf[64];
		switch (type)
		{
			case kTemp: std::snprintf(buf, sizeof(buf), "r%d", num); return buf;
			case kInput:
				if (num < 16)
					usedInputs |= 1u << num;
				std::snprintf(buf, sizeof(buf), "v%d", num);
				return buf;
			case kConst:
				if (tok & 0x2000u) // endereçamento relativo: c[a0.x + n]
				{
					std::snprintf(buf, sizeof(buf), "C(a0 + %d)", num);
					return buf;
				}
				if (auto it = defs.find(num); it != defs.end())
					return it->second;
				if (num >= kNumConst)
				{
					error = "constante fora do intervalo";
					return "vec4(0.0)";
				}
				std::snprintf(buf, sizeof(buf), "uC[%d]", num);
				return buf;
			case kAddr: return "vec4(float(a0))";
			default:
				error = "tipo de registrador de origem não suportado";
				return "vec4(0.0)";
		}
	}

	std::string Src(uint32_t tok)
	{
		std::string s = Reg(tok);
		const uint32_t swz = (tok >> 16) & 0xFFu;
		if (swz != 0xE4u)
		{
			static const char kComp[] = "xyzw";
			s += '.';
			for (int i = 0; i < 4; ++i)
				s += kComp[(swz >> (i * 2)) & 3u];
		}
		const uint32_t mod = (tok >> 24) & 0xFu;
		if (mod == 1)
			s = "(-" + s + ")";
		else if (mod != 0)
			error = "modificador de origem não suportado";
		return s;
	}

	void Write(uint32_t dst, const std::string& expr)
	{
		const int type = RegTypeOf(dst);
		const int num = RegNumOf(dst);
		const uint32_t mask = (dst >> 16) & 0xFu;
		const bool sat = ((dst >> 20) & 0xFu) & 1u;
		std::string value = sat ? "clamp(" + expr + ", 0.0, 1.0)" : expr;
		body += "  t = " + value + ";\n";

		std::string name;
		char buf[32];
		switch (type)
		{
			case kTemp: std::snprintf(buf, sizeof(buf), "r%d", num); name = buf; break;
			case kAddr: body += "  a0 = int(floor(t.x + 0.5));\n"; return;
			case kRastOut:
				if (num == 0)
					name = "oPos";
				else if (num == 1)
				{
					body += "  oFog = t.x;\n";
					return;
				}
				else
					return; // oPts: sem suporte a point size
				break;
			case kAttrOut: std::snprintf(buf, sizeof(buf), "oD%d", num & 1); name = buf; break;
			case kTexCrdOut:
				if (num > 7)
				{
					error = "oT fora do intervalo";
					return;
				}
				std::snprintf(buf, sizeof(buf), "oT%d", num);
				name = buf;
				break;
			default: error = "tipo de registrador de destino não suportado"; return;
		}
		if (mask == 0xF)
		{
			body += "  " + name + " = t;\n";
			return;
		}
		static const char kComp[] = "xyzw";
		std::string m;
		for (int i = 0; i < 4; ++i)
			if (mask & (1u << i))
				m += kComp[i];
		body += "  " + name + "." + m + " = t." + m + ";\n";
	}

	std::string Matrix(uint32_t src0, uint32_t src1, int rows, bool vec3)
	{
		const std::string v = Src(src0);
		std::string e = "vec4(";
		for (int i = 0; i < 4; ++i)
		{
			if (i)
				e += ", ";
			if (i < rows)
			{
				const std::string row = Reg(src1, i);
				e += vec3 ? "dot((" + v + ").xyz, (" + row + ").xyz)" : "dot(" + v + ", " + row + ")";
			}
			else
				e += "0.0";
		}
		return e + ")";
	}
};

const char* kHeader = R"GLSL(#version 300 es
precision highp float;
precision highp int;
uniform vec4 uC[96];
uniform float uYFlip;
out vec4 vColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogDepth;
vec4 C(int i) { return uC[clamp(i, 0, 95)]; }
float rcp_(float x) { return x == 0.0 ? 3.4e38 : 1.0 / x; }
float rsq_(float x) { x = abs(x); return x == 0.0 ? 3.4e38 : inversesqrt(x); }
float log_(float x) { x = abs(x); return x == 0.0 ? -3.4e38 : log2(x); }
vec4 lit_(vec4 s) {
  float d = max(s.x, 0.0);
  float p = clamp(s.w, -128.0, 128.0);
  float sp = (s.x > 0.0) ? pow(max(s.y, 0.0), p) : 0.0;
  return vec4(1.0, d, sp, 1.0);
}
)GLSL";

} // namespace

Vs11Result TranslateVs11(const uint32_t* code, size_t numDwords)
{
	Vs11Result res;
	if (!code || numDwords < 2)
	{
		res.error = "bytecode vazio";
		return res;
	}
	const uint32_t ver = code[0];
	if ((ver & 0xFFFF0000u) != 0xFFFE0000u || ((ver >> 8) & 0xFFu) != 1u)
	{
		res.error = "só vs_1_x é suportado";
		return res;
	}

	Emitter em;
	size_t i = 1;
	bool ended = false;
	while (i < numDwords && em.error.empty())
	{
		const uint32_t tok = code[i];
		const uint32_t op = tok & 0xFFFFu;
		if (op == 0xFFFFu)
		{
			ended = true;
			break;
		}
		if (op == 0xFFFEu) // comentário
		{
			i += 1 + ((tok >> 16) & 0x7FFFu);
			continue;
		}
		if (op == 0x1Fu) // dcl
		{
			if (i + 2 >= numDwords)
				break;
			const uint32_t usageTok = code[i + 1];
			const uint32_t regTok = code[i + 2];
			if (RegTypeOf(regTok) == kInput)
			{
				Vs11Input in;
				in.reg = RegNumOf(regTok);
				in.usage = (int)(usageTok & 0x1Fu);
				in.usageIndex = (int)((usageTok >> 16) & 0xFu);
				res.inputs.push_back(in);
			}
			i += 3;
			continue;
		}
		if (op == 0x51u) // def c#, x, y, z, w
		{
			if (i + 5 >= numDwords)
				break;
			float f[4];
			std::memcpy(f, &code[i + 2], sizeof(f));
			char buf[160];
			std::snprintf(buf, sizeof(buf), "vec4(%.9g, %.9g, %.9g, %.9g)", f[0], f[1], f[2], f[3]);
			em.defs[RegNumOf(code[i + 1])] = buf;
			i += 6;
			continue;
		}

		Op info;
		if (!OpInfo(op, info))
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "opcode 0x%x não suportado", (unsigned)op);
			res.error = buf;
			return res;
		}
		if (op == 0x00u)
		{
			++i;
			continue;
		}
		if (i + 1 + (size_t)info.srcCount >= numDwords)
			break;
		const uint32_t dst = code[i + 1];
		const uint32_t* s = &code[i + 2];
		std::string e;
		switch (op)
		{
			case 0x01: e = em.Src(s[0]); break;
			case 0x02: e = em.Src(s[0]) + " + " + em.Src(s[1]); break;
			case 0x03: e = em.Src(s[0]) + " - " + em.Src(s[1]); break;
			case 0x04: e = em.Src(s[0]) + " * " + em.Src(s[1]) + " + " + em.Src(s[2]); break;
			case 0x05: e = em.Src(s[0]) + " * " + em.Src(s[1]); break;
			case 0x06: e = "vec4(rcp_((" + em.Src(s[0]) + ").x))"; break;
			case 0x07: e = "vec4(rsq_((" + em.Src(s[0]) + ").x))"; break;
			case 0x08: e = "vec4(dot((" + em.Src(s[0]) + ").xyz, (" + em.Src(s[1]) + ").xyz))"; break;
			case 0x09: e = "vec4(dot(" + em.Src(s[0]) + ", " + em.Src(s[1]) + "))"; break;
			case 0x0A: e = "min(" + em.Src(s[0]) + ", " + em.Src(s[1]) + ")"; break;
			case 0x0B: e = "max(" + em.Src(s[0]) + ", " + em.Src(s[1]) + ")"; break;
			case 0x0C: e = "vec4(lessThan(" + em.Src(s[0]) + ", " + em.Src(s[1]) + "))"; break;
			case 0x0D: e = "vec4(greaterThanEqual(" + em.Src(s[0]) + ", " + em.Src(s[1]) + "))"; break;
			case 0x0E: e = "vec4(exp2((" + em.Src(s[0]) + ").x))"; break;
			case 0x0F:
			case 0x4F: e = "vec4(log_((" + em.Src(s[0]) + ").x))"; break;
			case 0x10: e = "lit_(" + em.Src(s[0]) + ")"; break;
			case 0x11:
			{
				const std::string a = em.Src(s[0]), b = em.Src(s[1]);
				e = "vec4(1.0, (" + a + ").y * (" + b + ").y, (" + a + ").z, (" + b + ").w)";
				break;
			}
			case 0x12: e = "mix(" + em.Src(s[2]) + ", " + em.Src(s[1]) + ", " + em.Src(s[0]) + ")"; break;
			case 0x13: e = "fract(" + em.Src(s[0]) + ")"; break;
			case 0x14: e = em.Matrix(s[0], s[1], 4, false); break;
			case 0x15: e = em.Matrix(s[0], s[1], 3, false); break;
			case 0x16: e = em.Matrix(s[0], s[1], 4, true); break;
			case 0x17: e = em.Matrix(s[0], s[1], 3, true); break;
			case 0x18: e = em.Matrix(s[0], s[1], 2, true); break;
			case 0x4E:
			{
				const std::string a = "(" + em.Src(s[0]) + ").x";
				e = "vec4(exp2(floor(" + a + ")), fract(" + a + "), exp2(" + a + "), 1.0)";
				break;
			}
			default: break;
		}
		em.Write(dst, e);
		++res.instructions;
		i += 2 + (size_t)info.srcCount;
	}

	if (!em.error.empty())
	{
		res.error = em.error;
		return res;
	}
	if (!ended)
	{
		res.error = "bytecode sem token de fim";
		return res;
	}

	// Shaders estilo DX8 (sem dcl): a declaração de vértice define as entradas.
	for (int r = 0; r < 16; ++r)
	{
		if (!(em.usedInputs & (1u << r)))
			continue;
		bool declared = false;
		for (const auto& in : res.inputs)
			declared |= in.reg == r;
		if (!declared)
			res.inputs.push_back({ r, -1, 0 });
	}

	std::string g = kHeader;
	for (const auto& in : res.inputs)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "layout(location=%d) in vec4 v%d;\n", in.reg, in.reg);
		g += buf;
	}
	g += "void main() {\n  vec4 t = vec4(0.0);\n";
	for (int r = 0; r < 12; ++r)
	{
		char buf[48];
		std::snprintf(buf, sizeof(buf), "  vec4 r%d = vec4(0.0);\n", r);
		g += buf;
	}
	g += "  int a0 = 0;\n"
	     "  vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);\n"
	     "  vec4 oD0 = vec4(1.0);\n"
	     "  vec4 oD1 = vec4(0.0);\n"
	     "  float oFog = 1.0;\n";
	for (int t = 0; t < 8; ++t)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "  vec4 oT%d = vec4(0.0, 0.0, 0.0, 1.0);\n", t);
		g += buf;
	}
	g += em.body;
	// D3D: z de clip em [0,w]; GL: [-w,w].
	g += "  gl_Position = vec4(oPos.x, oPos.y * uYFlip, 2.0 * oPos.z - oPos.w, oPos.w);\n"
	     "  vColor = clamp(oD0, 0.0, 1.0);\n"
	     "  vUV0 = oT0.xy;\n"
	     "  vUV1 = oT1.xy;\n"
	     "  vFogDepth = oFog;\n"
	     "}\n";
	res.glsl = std::move(g);
	res.ok = true;
	return res;
}
