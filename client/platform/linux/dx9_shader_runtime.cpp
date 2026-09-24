#include "dx9_shader_runtime.h"
#include "d3d9_min_api.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	uint32_t CountInstructions(const uint32_t* words, uint32_t wordCount)
	{
		if (wordCount < 2)
			return 0;
		uint32_t n = 0;
		// Pula version token; conta tokens até END (simplificado: cada dword
		// com opcode length nos bits altos — vs_1_1/ps_1_1 usam encoding curto).
		for (uint32_t i = 1; i < wordCount; ++i)
		{
			if (words[i] == 0x0000FFFFu)
				break;
			// Token de instrução: bit 31 = 0 tipicamente para ops; dest/src têm bits.
			// Contamos palavras que não são puramente parâmetro de registrador
			// (heurística: opcode nos 16 bits baixos != 0xFFFF e bit 31 clear-ish).
			const uint32_t op = words[i] & 0xFFFFu;
			if (op != 0 && op != 0xFFFF)
				++n;
		}
		return n;
	}

	bool TryDlopen(const char* path, WYDDx9ShaderRuntime& rt)
	{
		void* h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
		if (!h)
			return false;
		void* sym = dlsym(h, "Direct3DCreate9");
		if (!sym)
			sym = dlsym(h, "D3D9Create");
		rt.native_d3d9_probed = true;
		if (sym)
		{
			rt.native_d3d9_available = true;
			rt.native_d3d9_path = path;
			rt.native_d3d9_note = "Direct3DCreate9 encontrado (não linkado ao device ainda)";
			std::fprintf(stderr, "[WYDLINUX][dx9rt] native D3D9 OK: %s\n", path);
		}
		else
		{
			rt.native_d3d9_path = path;
			rt.native_d3d9_note = std::string("aberto mas sem Direct3DCreate9: ") +
				(dlerror() ? dlerror() : "?");
			std::fprintf(stderr, "[WYDLINUX][dx9rt] %s\n", rt.native_d3d9_note.c_str());
		}
		dlclose(h);
		return rt.native_d3d9_available;
	}
}

void WYD_ProbeNativeD3D9(WYDDx9ShaderRuntime& rt)
{
	rt.native_d3d9_probed = true;
	rt.native_d3d9_available = false;
	rt.native_d3d9_path.clear();
	rt.native_d3d9_note = "nenhuma lib nativa";

	if (const char* env = std::getenv("WYD_D3D9_SO"); env && env[0])
	{
		if (TryDlopen(env, rt))
			return;
		rt.native_d3d9_note = std::string("WYD_D3D9_SO falhou: ") + env;
	}

	const char* candidates[] = {
		"libdxvk_d3d9.so",
		"libd3d9.so",
		"./libdxvk_d3d9.so",
		"/usr/lib/libdxvk_d3d9.so",
		"/usr/local/lib/libdxvk_d3d9.so",
		"../third_party/dxvk-native/usr/lib/libdxvk_d3d9.so",
		"third_party/dxvk-native/usr/lib/libdxvk_d3d9.so",
	};
	if (const char* d = std::getenv("WYD_DXVK_LIBDIR"); d && d[0])
	{
		char buf[512];
		std::snprintf(buf, sizeof(buf), "%s/libdxvk_d3d9.so", d);
		if (TryDlopen(buf, rt))
			return;
	}
	for (const char* c : candidates)
	{
		if (TryDlopen(c, rt))
			return;
	}
	std::fprintf(stderr,
		"[WYDLINUX][dx9rt] sem libdxvk_d3d9.so — usando stand-in Vulkan "
		"(ver scripts/fetch-dxvk-native.sh)\n");
}

bool WYD_Dx9RuntimeBuild(WYDDx9ShaderRuntime& rt, const WYDDx9ShaderCatalog& catalog)
{
	rt.entries.clear();
	rt.created = 0;
	rt.standin_ready = 0;
	rt.failed = 0;

	WYD_ProbeNativeD3D9(rt);

	IDirect3DDevice9_Linux device {};

	for (const auto& e : catalog.entries)
	{
		WYDDx9RuntimeEntry out {};
		out.path = e.relative_path;
		out.kind = e.kind;
		out.version_token = e.version_token;

		if (!e.valid || e.bytecode.size() < 4)
		{
			out.error = e.error.empty() ? "inválido" : e.error;
			++rt.failed;
			rt.entries.push_back(out);
			continue;
		}

		const auto* words = reinterpret_cast<const DWORD*>(e.bytecode.data());
		HRESULT hr = E_FAIL;
		if (e.kind == WYDDx9ShaderKind::PsEffect)
		{
			LPDIRECT3DPIXELSHADER9 ps = nullptr;
			hr = device.CreatePixelShader(words, &ps);
			if (SUCCEEDED(hr) && ps)
			{
				out.word_count = static_cast<uint32_t>(ps->bytecode.size() / 4);
				out.instruction_count = CountInstructions(
					reinterpret_cast<const uint32_t*>(ps->bytecode.data()),
					out.word_count);
				device.SetPixelShader(ps);
				ps->Release();
				out.created = true;
			}
		}
		else
		{
			LPDIRECT3DVERTEXSHADER9 vs = nullptr;
			hr = device.CreateVertexShader(words, &vs);
			if (SUCCEEDED(hr) && vs)
			{
				out.word_count = static_cast<uint32_t>(vs->bytecode.size() / 4);
				out.instruction_count = CountInstructions(
					reinterpret_cast<const uint32_t*>(vs->bytecode.data()),
					out.word_count);
				device.SetVertexShader(vs);
				vs->Release();
				out.created = true;
			}
		}

		if (out.created)
		{
			++rt.created;
			// Stand-in: bytecode DX9 validado + Create*Shader OK → pipeline
			// Vulkan do mesh/textured pode representar a bind (não traduz opcodes).
			out.standin_ready = true;
			++rt.standin_ready;
		}
		else
		{
			out.error = "Create*Shader rejeitou";
			++rt.failed;
		}
		rt.entries.push_back(out);
	}

	std::fprintf(stderr,
		"[WYDLINUX][dx9rt] Create*Shader %d OK, stand-in %d, fail %d | native=%s\n",
		rt.created, rt.standin_ready, rt.failed,
		rt.native_d3d9_available ? rt.native_d3d9_path.c_str() : "no");
	return rt.failed == 0 && rt.created > 0;
}

int WYD_Dx9RuntimeActiveStandin(const WYDDx9ShaderRuntime& rt, uint32_t frame)
{
	if (rt.entries.empty())
		return -1;
	// Prefere skinmesh (índices 0..7 tipicamente).
	int skin = 0;
	for (size_t i = 0; i < rt.entries.size(); ++i)
	{
		if (rt.entries[i].standin_ready &&
			rt.entries[i].kind == WYDDx9ShaderKind::SkinMesh)
			++skin;
	}
	if (skin <= 0)
		return static_cast<int>(frame % rt.entries.size());
	return static_cast<int>(frame % static_cast<uint32_t>(skin));
}
