// Smoke D+++: shaders DX9 Create*Shader + MSA mesh + object.bin + UI WYT.

#include "wayland_window.h"
#include "shader_dx9_catalog.h"
#include "d3d9_min_api.h"
#include "msa_loader.h"
#include "vulkan_mesh.h"
#include "object_mask.h"
#include "wyt_decode.h"
#include "asset_resolve.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>

namespace
{
	std::vector<std::string> LoadMeshListPaths(int limit)
	{
		std::vector<std::string> out;
		const std::string path = WYD_JoinAssetPath("mesh/MeshList.txt");
		std::ifstream in(path);
		if (!in)
			return out;
		std::string line;
		while (std::getline(in, line) && static_cast<int>(out.size()) < limit)
		{
			if (line.empty())
				continue;
			const auto sp = line.find(' ');
			if (sp == std::string::npos)
				continue;
			out.push_back(line.substr(sp + 1));
		}
		return out;
	}
}

int main()
{
	WYDDx9ShaderCatalog catalog;
	if (!WYD_LoadDx9ShaderCatalog(catalog))
		return 1;

	WYDObjectMaskTable masks;
	if (!WYD_LoadObjectMask(masks))
		return 2;

	WYDWytImage logo;
	if (!WYD_LoadWytRgba("UI/logo1.wyt", logo))
	{
		std::fprintf(stderr, "[d3pp] logo: %s\n", logo.error.c_str());
		return 3;
	}

	// Resolve entradas do MeshList (effect\\ → Effect/)
	const auto list = LoadMeshListPaths(30);
	int resolved = 0;
	for (const auto& p : list)
	{
		if (!WYD_ResolveAssetAbsolute(p.c_str()).empty())
			++resolved;
	}
	std::fprintf(stderr, "[d3pp] MeshList: %d/%zu paths resolvidos\n",
		resolved, list.size());

	WYDMsaMesh mesh;
	const char* candidates[] = {
		"Effect/sphere.msa",
		"mesh/throw07.msa",
		"effect\\\\sphere.msa",
	};
	bool meshOk = false;
	for (const char* c : candidates)
	{
		if (WYD_LoadMsaMesh(c, mesh))
		{
			meshOk = true;
			break;
		}
		std::fprintf(stderr, "[d3pp] MSA skip: %s\n", mesh.error.c_str());
	}
	if (!meshOk)
		return 4;

	WYDWaylandWindow win;
	if (!WYD_CreateWaylandWindow(win, "WYD D+++ mesh/shaders", 800, 600, false, true))
		return 5;

	// Device lógico só para Create*Shader (sem segundo swapchain).
	auto* device = new IDirect3DDevice9_Linux();
	int bound = 0;
	for (const auto& e : catalog.entries)
	{
		if (!e.valid || e.bytecode.size() < 4)
			continue;
		const auto* words = reinterpret_cast<const DWORD*>(e.bytecode.data());
		if (e.kind == WYDDx9ShaderKind::PsEffect)
		{
			LPDIRECT3DPIXELSHADER9 ps = nullptr;
			if (SUCCEEDED(device->CreatePixelShader(words, &ps)) && ps)
			{
				device->SetPixelShader(ps);
				++bound;
				ps->Release();
			}
		}
		else
		{
			LPDIRECT3DVERTEXSHADER9 vs = nullptr;
			if (SUCCEEDED(device->CreateVertexShader(words, &vs)) && vs)
			{
				device->SetVertexShader(vs);
				++bound;
				vs->Release();
			}
		}
	}
	std::fprintf(stderr, "[d3pp] Create*Shader OK=%d bound=%d (bytecode stored; exec=DXVK futuro)\n",
		device->shaders_created, bound);

	WYDVulkanMesh vmesh;
	if (!WYD_VulkanMeshCreate(vmesh, WYD_GetSDLWindow(win), win.width, win.height) ||
		!WYD_VulkanMeshUpload(vmesh, mesh))
	{
		std::fprintf(stderr, "[d3pp] mesh vulkan falhou\n");
		delete device;
		WYD_DestroyWaylandWindow(win);
		return 7;
	}

	std::fprintf(stderr, "[d3pp] girando mesh — ESC | logo=%ux%u\n",
		logo.width, logo.height);

	uint32_t frames = 0;
	while (WYD_PollWaylandEvents(win))
	{
		const float angle = frames * 0.02f;
		if (!WYD_VulkanMeshDraw(vmesh, angle))
			break;
		++frames;
		SDL_Delay(16);
	}

	const int shadersCreated = device->shaders_created;
	WYD_VulkanMeshDestroy(vmesh);
	delete device;
	WYD_DestroyWaylandWindow(win);
	std::fprintf(stderr, "[d3pp] %u frames | masks=%d | shaders_created=%d | catalog=%d\n",
		frames, masks.mask_count, shadersCreated, catalog.loaded);
	return 0;
}
