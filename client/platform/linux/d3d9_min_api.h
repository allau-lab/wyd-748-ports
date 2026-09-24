#pragma once

// Subconjunto da API D3D9 usado pelo bootstrap Linux.
// Clear/Present via Vulkan. Create*Shader valida/armazena bytecode DX9
// (vs_1_1/ps_1_1). Fase G+: stand-in Vulkan via dx9_shader_runtime;
// execução real do ISA DX9 requer libdxvk_d3d9.so (WYD_D3D9_SO).

#include "d3d9_bridge.h"
#include "wincompat.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>
#include <cstring>

#ifndef D3D_OK
#define D3D_OK S_OK
#endif

using D3DCOLOR = DWORD;

inline D3DCOLOR D3DCOLOR_ARGB(int a, int r, int g, int b)
{
	return (static_cast<D3DCOLOR>(a & 255) << 24) |
		(static_cast<D3DCOLOR>(r & 255) << 16) |
		(static_cast<D3DCOLOR>(g & 255) << 8) |
		static_cast<D3DCOLOR>(b & 255);
}

constexpr DWORD D3DCLEAR_TARGET = 0x00000001u;
constexpr DWORD D3DCLEAR_ZBUFFER = 0x00000002u;
constexpr DWORD D3DCLEAR_STENCIL = 0x00000004u;

struct IDirect3DVertexShader9_Linux
{
	std::vector<uint8_t> bytecode;
	uint32_t version_token = 0;
	ULONG Release() { delete this; return 0; }
};

struct IDirect3DPixelShader9_Linux
{
	std::vector<uint8_t> bytecode;
	uint32_t version_token = 0;
	ULONG Release() { delete this; return 0; }
};

using LPDIRECT3DVERTEXSHADER9 = IDirect3DVertexShader9_Linux*;
using LPDIRECT3DPIXELSHADER9 = IDirect3DPixelShader9_Linux*;

struct IDirect3DDevice9_Linux
{
	WYDD3D9Bridge bridge {};
	bool in_scene = false;
	LPDIRECT3DVERTEXSHADER9 active_vs = nullptr;
	LPDIRECT3DPIXELSHADER9 active_ps = nullptr;
	int shaders_created = 0;

	HRESULT BeginScene()
	{
		in_scene = true;
		return D3D_OK;
	}

	HRESULT EndScene()
	{
		in_scene = false;
		return D3D_OK;
	}

	HRESULT Clear(DWORD /*Count*/, const void* /*pRects*/, DWORD Flags,
		D3DCOLOR Color, float /*Z*/, DWORD /*Stencil*/)
	{
		if (Flags & D3DCLEAR_TARGET)
		{
			if (!WYD_D3D9BridgeClear(bridge, Color))
				return E_FAIL;
		}
		return D3D_OK;
	}

	HRESULT Present(const void*, const void*, HWND, const void*)
	{
		return WYD_D3D9BridgePresent(bridge) ? D3D_OK : E_FAIL;
	}

	// Aceita DWORD* no estilo CreateVertexShader(DX9). Valida token 0xFFFE****.
	HRESULT CreateVertexShader(const DWORD* pFunction, LPDIRECT3DVERTEXSHADER9* ppShader)
	{
		if (!pFunction || !ppShader)
			return E_FAIL;
		const uint32_t token = pFunction[0];
		if ((token & 0xFFFF0000u) != 0xFFFE0000u)
			return E_FAIL;
		// Varre até D3DSIO_END (0x0000FFFF)
		size_t words = 0;
		while (words < 65536)
		{
			if (pFunction[words] == 0x0000FFFFu)
			{
				++words;
				break;
			}
			++words;
		}
		auto* vs = new IDirect3DVertexShader9_Linux();
		vs->version_token = token;
		vs->bytecode.resize(words * 4);
		std::memcpy(vs->bytecode.data(), pFunction, vs->bytecode.size());
		*ppShader = vs;
		++shaders_created;
		return D3D_OK;
	}

	HRESULT CreatePixelShader(const DWORD* pFunction, LPDIRECT3DPIXELSHADER9* ppShader)
	{
		if (!pFunction || !ppShader)
			return E_FAIL;
		const uint32_t token = pFunction[0];
		if ((token & 0xFFFF0000u) != 0xFFFF0000u)
			return E_FAIL;
		size_t words = 0;
		while (words < 65536)
		{
			if (pFunction[words] == 0x0000FFFFu)
			{
				++words;
				break;
			}
			++words;
		}
		auto* ps = new IDirect3DPixelShader9_Linux();
		ps->version_token = token;
		ps->bytecode.resize(words * 4);
		std::memcpy(ps->bytecode.data(), pFunction, ps->bytecode.size());
		*ppShader = ps;
		++shaders_created;
		return D3D_OK;
	}

	HRESULT SetVertexShader(LPDIRECT3DVERTEXSHADER9 pShader)
	{
		active_vs = pShader;
		return D3D_OK;
	}

	HRESULT SetPixelShader(LPDIRECT3DPIXELSHADER9 pShader)
	{
		active_ps = pShader;
		return D3D_OK;
	}
};

using LPDIRECT3DDEVICE9 = IDirect3DDevice9_Linux*;

struct IDirect3D9_Linux
{
	HRESULT CreateDevice(UINT /*Adapter*/, DWORD /*DeviceType*/, HWND hFocusWindow,
		DWORD /*BehaviorFlags*/, void* /*pPresentationParameters*/,
		LPDIRECT3DDEVICE9* ppReturnedDeviceInterface)
	{
		if (!ppReturnedDeviceInterface || !hFocusWindow)
			return E_FAIL;

		auto* device = new IDirect3DDevice9_Linux();
		auto* window = static_cast<SDL_Window*>(hFocusWindow);
		int w = 800, h = 600;
		SDL_GetWindowSize(window, &w, &h);
		if (!WYD_D3D9BridgeCreate(device->bridge, window,
			static_cast<uint32_t>(w), static_cast<uint32_t>(h)))
		{
			delete device;
			return E_FAIL;
		}
		*ppReturnedDeviceInterface = device;
		return D3D_OK;
	}

	ULONG Release()
	{
		delete this;
		return 0;
	}
};

using LPDIRECT3D9 = IDirect3D9_Linux*;

inline LPDIRECT3D9 Direct3DCreate9_Linux(UINT /*SDKVersion*/)
{
	return new IDirect3D9_Linux();
}

#ifndef Direct3DCreate9
#define Direct3DCreate9 Direct3DCreate9_Linux
#endif
