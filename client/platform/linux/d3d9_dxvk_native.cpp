// Isolado: headers DXVK Native, SEM wincompat.h do port.
#if defined(WYD_HAS_DXVK_NATIVE_HEADERS) && WYD_HAS_DXVK_NATIVE_HEADERS

#include "d3d9_dxvk_native.h"
#include "shader_dx9_catalog.h"
#include "asset_paths.h"

#include <d3d9.h>

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	using Direct3DCreate9_fn = IDirect3D9* (*)(UINT);

	std::string Join(const std::string& a, const char* b)
	{
		if (a.empty())
			return b ? b : "";
		if (a.back() == '/')
			return a + (b ? b : "");
		return a + "/" + (b ? b : "");
	}

	bool FileOk(const std::string& p)
	{
		return access(p.c_str(), R_OK) == 0;
	}

	std::vector<std::string> CandidateLibs()
	{
		std::vector<std::string> out;
		if (const char* e = std::getenv("WYD_D3D9_SO"); e && e[0])
			out.push_back(e);
		if (const char* d = std::getenv("WYD_DXVK_LIBDIR"); d && d[0])
			out.push_back(Join(d, "libdxvk_d3d9.so"));

		const std::string root = WYD_AssetRoot();
		out.push_back(Join(root, "../third_party/dxvk-native/usr/lib/libdxvk_d3d9.so"));
		out.push_back(Join(root, "third_party/dxvk-native/usr/lib/libdxvk_d3d9.so"));

		// Relativo ao source tree (dev)
		out.push_back("third_party/dxvk-native/usr/lib/libdxvk_d3d9.so");
		out.push_back("../third_party/dxvk-native/usr/lib/libdxvk_d3d9.so");
		out.push_back("libdxvk_d3d9.so");
		return out;
	}

	void PreloadDxgi(const std::string& d3d9Path)
	{
		std::string dir = d3d9Path;
		const auto slash = dir.find_last_of('/');
		if (slash != std::string::npos)
			dir.resize(slash);
		const std::string dxgi = Join(dir, "libdxvk_dxgi.so");
		if (FileOk(dxgi))
			(void)dlopen(dxgi.c_str(), RTLD_NOW | RTLD_GLOBAL);
	}

	void EnsureWsiEnv()
	{
		if (!std::getenv("DXVK_WSI_DRIVER") || !std::getenv("DXVK_WSI_DRIVER")[0])
#ifdef WYD_USE_SDL3
			setenv("DXVK_WSI_DRIVER", "SDL3", 1);
#else
			setenv("DXVK_WSI_DRIVER", "SDL2", 1);
#endif
	}
}

bool WYD_D3D9NativeLoad(WYDD3D9NativeDevice& out)
{
	out = {};
	EnsureWsiEnv();

	for (const auto& path : CandidateLibs())
	{
		if (!FileOk(path))
			continue;
		PreloadDxgi(path);
		void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!h)
		{
			std::fprintf(stderr, "[WYDLINUX][dxvk] dlopen fail %s: %s\n",
				path.c_str(), dlerror());
			continue;
		}
		auto create = reinterpret_cast<Direct3DCreate9_fn>(dlsym(h, "Direct3DCreate9"));
		if (!create)
		{
			std::fprintf(stderr, "[WYDLINUX][dxvk] sem Direct3DCreate9 em %s\n", path.c_str());
			dlclose(h);
			continue;
		}
		out.so = h;
		out.lib_path = path;
		out.loaded = true;
		out.status = "lib carregada: " + path;
		std::fprintf(stderr, "[WYDLINUX][dxvk] loaded %s (WSI=%s)\n",
			path.c_str(), std::getenv("DXVK_WSI_DRIVER"));
		return true;
	}
	out.status = "libdxvk_d3d9.so ausente — rode scripts/fetch-dxvk-native.sh";
	return false;
}

bool WYD_D3D9NativeCreateDevice(WYDD3D9NativeDevice& out, SDL_Window* window,
	uint32_t width, uint32_t height)
{
	if (!out.loaded || !out.so || !window)
		return false;
	EnsureWsiEnv();

	auto create = reinterpret_cast<Direct3DCreate9_fn>(dlsym(out.so, "Direct3DCreate9"));
	if (!create)
		return false;

	IDirect3D9* d3d = create(D3D_SDK_VERSION);
	if (!d3d)
	{
		out.status = "Direct3DCreate9 retornou null";
		return false;
	}
	out.d3d9 = d3d;

	D3DPRESENT_PARAMETERS pp {};
	std::memset(&pp, 0, sizeof(pp));
	pp.BackBufferWidth = width;
	pp.BackBufferHeight = height;
	pp.BackBufferFormat = D3DFMT_A8R8G8B8;
	pp.BackBufferCount = 1;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.hDeviceWindow = reinterpret_cast<HWND>(window);
	pp.Windowed = TRUE;
	pp.EnableAutoDepthStencil = TRUE;
	pp.AutoDepthStencilFormat = D3DFMT_D24S8;
	// VSync por padrão (imagem estável). WYD_NO_VSYNC=1 desliga.
	const char* noVsync = std::getenv("WYD_NO_VSYNC");
	pp.PresentationInterval = (noVsync && noVsync[0] == '1')
		? D3DPRESENT_INTERVAL_IMMEDIATE
		: D3DPRESENT_INTERVAL_DEFAULT;

	// Tenta MSAA 8 → 4 → 2 → nenhum (qualidade máxima disponível).
	const D3DMULTISAMPLE_TYPE aaTry[] = {
		D3DMULTISAMPLE_8_SAMPLES,
		D3DMULTISAMPLE_4_SAMPLES,
		D3DMULTISAMPLE_2_SAMPLES,
		D3DMULTISAMPLE_NONE,
	};
	IDirect3DDevice9* device = nullptr;
	HRESULT hr = E_FAIL;
	for (D3DMULTISAMPLE_TYPE aa : aaTry)
	{
		pp.MultiSampleType = aa;
		pp.MultiSampleQuality = 0;
		if (aa != D3DMULTISAMPLE_NONE)
		{
			DWORD q = 0;
			if (FAILED(d3d->CheckDeviceMultiSampleType(
					D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pp.BackBufferFormat, TRUE, aa, &q)) ||
				FAILED(d3d->CheckDeviceMultiSampleType(
					D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pp.AutoDepthStencilFormat, TRUE, aa, &q)))
				continue;
		}
		hr = d3d->CreateDevice(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			reinterpret_cast<HWND>(window),
			D3DCREATE_HARDWARE_VERTEXPROCESSING,
			&pp,
			&device);
		if (SUCCEEDED(hr) && device)
		{
			if (aa != D3DMULTISAMPLE_NONE)
				std::fprintf(stderr, "[WYDLINUX][dxvk] MSAA=%d ativo\n", static_cast<int>(aa));
			break;
		}
		device = nullptr;
	}
	if (FAILED(hr) || !device)
	{
		char buf[96];
		std::snprintf(buf, sizeof(buf), "CreateDevice falhou hr=0x%08X",
			static_cast<unsigned>(hr));
		out.status = buf;
		std::fprintf(stderr, "[WYDLINUX][dxvk] %s\n", out.status.c_str());
		d3d->Release();
		out.d3d9 = nullptr;
		return false;
	}

	out.device = device;
	out.device_ok = true;
	out.back_w = width;
	out.back_h = height;
	out.status = "CreateDevice OK (DXVK Native SDL2 + Z)";
	std::fprintf(stderr, "[WYDLINUX][dxvk] %s %ux%u\n",
		out.status.c_str(), width, height);
	return true;
}

int WYD_D3D9NativeCreateShadersFromCatalog(WYDD3D9NativeDevice& out, const void* catalogPtr)
{
	if (!out.device_ok || !out.device || !catalogPtr)
		return 0;
	const auto& catalog = *reinterpret_cast<const WYDDx9ShaderCatalog*>(catalogPtr);
	auto* device = static_cast<IDirect3DDevice9*>(out.device);

	// Libera anteriores se rebuild.
	for (auto& p : out.vs_skin)
	{
		if (p) { static_cast<IDirect3DVertexShader9*>(p)->Release(); p = nullptr; }
	}
	for (auto& p : out.vs_effect)
	{
		if (p) { static_cast<IDirect3DVertexShader9*>(p)->Release(); p = nullptr; }
	}
	for (auto& p : out.ps_effect)
	{
		if (p) { static_cast<IDirect3DPixelShader9*>(p)->Release(); p = nullptr; }
	}
	if (out.vdecl_skin[0] || out.vdecl_skin[1] || out.vdecl_skin[2] || out.vdecl_skin[3])
	{
		for (auto& d : out.vdecl_skin)
		{
			if (d)
			{
				static_cast<IDirect3DVertexDeclaration9*>(d)->Release();
				d = nullptr;
			}
		}
	}
	out.vs_skin_count = out.vs_effect_count = out.ps_effect_count = 0;

	int ok = 0;
	for (const auto& e : catalog.entries)
	{
		if (!e.valid || e.bytecode.size() < 4)
			continue;
		const auto* words = reinterpret_cast<const DWORD*>(e.bytecode.data());
		const int slot = e.index - 1;
		if (e.kind == WYDDx9ShaderKind::PsEffect)
		{
			if (slot < 0 || slot >= 6)
				continue;
			IDirect3DPixelShader9* ps = nullptr;
			if (SUCCEEDED(device->CreatePixelShader(words, &ps)) && ps)
			{
				out.ps_effect[slot] = ps;
				++out.ps_effect_count;
				++ok;
			}
		}
		else if (e.kind == WYDDx9ShaderKind::SkinMesh)
		{
			if (slot < 0 || slot >= 8)
				continue;
			IDirect3DVertexShader9* vs = nullptr;
			if (SUCCEEDED(device->CreateVertexShader(words, &vs)) && vs)
			{
				out.vs_skin[slot] = vs;
				++out.vs_skin_count;
				++ok;
			}
		}
		else // VsEffect
		{
			if (slot < 0 || slot >= 4)
				continue;
			IDirect3DVertexShader9* vs = nullptr;
			if (SUCCEEDED(device->CreateVertexShader(words, &vs)) && vs)
			{
				out.vs_effect[slot] = vs;
				++out.vs_effect_count;
				++ok;
			}
		}
	}

	// VertexDecl1..4 (RenderDevice): skinmesh1..4
	const D3DVERTEXELEMENT9 decl1[] = {
		{0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
		{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
		{0, 16, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
		{0, 28, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
		D3DDECL_END()
	};
	const D3DVERTEXELEMENT9 decl2[] = {
		{0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
		{0, 12, D3DDECLTYPE_FLOAT1,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
		{0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
		{0, 20, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
		{0, 32, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
		D3DDECL_END()
	};
	const D3DVERTEXELEMENT9 decl3[] = {
		{0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
		{0, 12, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
		{0, 20, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
		{0, 24, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
		{0, 36, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
		D3DDECL_END()
	};
	const D3DVERTEXELEMENT9 decl4[] = {
		{0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
		{0, 12, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
		{0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
		{0, 28, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
		{0, 40, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
		D3DDECL_END()
	};
	const D3DVERTEXELEMENT9* decls[4] = {decl1, decl2, decl3, decl4};
	int vdeclOk = 0;
	for (int i = 0; i < 4; ++i)
	{
		IDirect3DVertexDeclaration9* vdecl = nullptr;
		if (SUCCEEDED(device->CreateVertexDeclaration(decls[i], &vdecl)) && vdecl)
		{
			out.vdecl_skin[i] = vdecl;
			++vdeclOk;
		}
	}

	out.shaders_created = ok;
	out.shaders_ok = (ok > 0) && (vdeclOk > 0) && (out.vs_skin_count > 0);
	std::fprintf(stderr,
		"[WYDLINUX][dxvk] Create*Shader nativo %d/%zu (skinVS=%d effectVS=%d PS=%d vdecl=%d)\n",
		ok, catalog.entries.size(),
		out.vs_skin_count, out.vs_effect_count, out.ps_effect_count, vdeclOk);
	return ok;
}

bool WYD_D3D9NativeClear(WYDD3D9NativeDevice& out, uint32_t argb)
{
	if (!out.device_ok || !out.device)
		return false;
	auto* device = static_cast<IDirect3DDevice9*>(out.device);
	return SUCCEEDED(device->Clear(0, nullptr,
		D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, argb, 1.f, 0));
}

bool WYD_D3D9NativePresent(WYDD3D9NativeDevice& out)
{
	if (!out.device_ok || !out.device)
		return false;
	auto* device = static_cast<IDirect3DDevice9*>(out.device);
	return SUCCEEDED(device->Present(nullptr, nullptr, nullptr, nullptr));
}

namespace
{
	void ReleaseBlitTex(WYDD3D9NativeDevice& out)
	{
		if (out.blit_tex)
		{
			static_cast<IDirect3DTexture9*>(out.blit_tex)->Release();
			out.blit_tex = nullptr;
		}
		out.blit_w = 0;
		out.blit_h = 0;
	}
}

bool WYD_D3D9NativeUploadRgba(WYDD3D9NativeDevice& out,
	const uint8_t* rgba, uint32_t width, uint32_t height)
{
	if (!out.device_ok || !out.device || !rgba || width == 0 || height == 0)
		return false;
	auto* device = static_cast<IDirect3DDevice9*>(out.device);

	if (!out.blit_tex || out.blit_w != width || out.blit_h != height)
	{
		ReleaseBlitTex(out);
		IDirect3DTexture9* tex = nullptr;
		// DEFAULT+DYNAMIC: StretchRect exige pool DEFAULT (SYSTEMMEM falha).
		HRESULT hr = device->CreateTexture(
			width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
		if (FAILED(hr) || !tex)
		{
			hr = device->CreateTexture(
				width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
		}
		if (FAILED(hr) || !tex)
		{
			std::fprintf(stderr, "[WYDLINUX][dxvk] CreateTexture DEFAULT falhou hr=0x%08X\n",
				static_cast<unsigned>(hr));
			return false;
		}
		out.blit_tex = tex;
		out.blit_w = width;
		out.blit_h = height;
	}

	auto* tex = static_cast<IDirect3DTexture9*>(out.blit_tex);
	D3DLOCKED_RECT lr {};
	if (FAILED(tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
	{
		if (FAILED(tex->LockRect(0, &lr, nullptr, 0)))
			return false;
	}

	auto* dstBase = static_cast<uint8_t*>(lr.pBits);
	for (uint32_t y = 0; y < height; ++y)
	{
		auto* dst = dstBase + static_cast<size_t>(y) * static_cast<size_t>(lr.Pitch);
		const uint8_t* src = rgba + static_cast<size_t>(y) * width * 4u;
		for (uint32_t x = 0; x < width; ++x)
		{
			const uint8_t r = src[x * 4u + 0];
			const uint8_t g = src[x * 4u + 1];
			const uint8_t b = src[x * 4u + 2];
			const uint8_t a = src[x * 4u + 3];
			dst[x * 4u + 0] = b;
			dst[x * 4u + 1] = g;
			dst[x * 4u + 2] = r;
			dst[x * 4u + 3] = a;
		}
	}
	tex->UnlockRect(0);
	return true;
}

bool WYD_D3D9NativeDrawRgba(WYDD3D9NativeDevice& out,
	const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t clear_argb)
{
	if (!out.device_ok || !out.device)
		return false;
	auto* device = static_cast<IDirect3DDevice9*>(out.device);
	device->SetVertexShader(nullptr);
	device->SetPixelShader(nullptr);
	device->SetVertexDeclaration(nullptr);

	if (!WYD_D3D9NativeClear(out, clear_argb))
		return false;

	if (rgba && width && height)
	{
		if (!WYD_D3D9NativeUploadRgba(out, rgba, width, height) || !out.blit_tex)
			return false;

		IDirect3DSurface9* src = nullptr;
		IDirect3DSurface9* dst = nullptr;
		auto* tex = static_cast<IDirect3DTexture9*>(out.blit_tex);
		if (FAILED(tex->GetSurfaceLevel(0, &src)) || !src)
			return false;
		if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &dst)) || !dst)
		{
			src->Release();
			return false;
		}
		const HRESULT hr = device->StretchRect(src, nullptr, dst, nullptr, D3DTEXF_LINEAR);
		dst->Release();
		src->Release();
		if (FAILED(hr))
		{
			std::fprintf(stderr, "[WYDLINUX][dxvk] StretchRect falhou hr=0x%08X\n",
				static_cast<unsigned>(hr));
			return false;
		}
	}

	return WYD_D3D9NativePresent(out);
}

namespace
{
	void MatIdentity(float m[16])
	{
		std::memset(m, 0, 16 * sizeof(float));
		m[0] = m[5] = m[10] = m[15] = 1.f;
	}

	void MatMul(float o[16], const float a[16], const float b[16])
	{
		// Row-major (D3DXMATRIX / D3DMATRIX): o = a * b
		float t[16];
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
			{
				t[r * 4 + c] =
					a[r * 4 + 0] * b[0 * 4 + c] +
					a[r * 4 + 1] * b[1 * 4 + c] +
					a[r * 4 + 2] * b[2 * 4 + c] +
					a[r * 4 + 3] * b[3 * 4 + c];
			}
		std::memcpy(o, t, sizeof(t));
	}

	void MatPerspectiveD3D(float m[16], float fovy, float aspect, float zn, float zf)
	{
		MatIdentity(m);
		const float h = 1.f / std::tan(fovy * 0.5f);
		const float w = h / aspect;
		m[0] = w;
		m[5] = h;
		m[10] = zf / (zf - zn);
		m[11] = 1.f;
		m[14] = (-zn * zf) / (zf - zn);
		m[15] = 0.f;
	}

	void MatLookAtD3D(float m[16], float ex, float ey, float ez,
		float cx, float cy, float cz)
	{
		float fx = cx - ex, fy = cy - ey, fz = cz - ez;
		float len = std::sqrt(fx * fx + fy * fy + fz * fz);
		fx /= len; fy /= len; fz /= len;
		float ux = 0.f, uy = 1.f, uz = 0.f;
		float sx = uy * fz - uz * fy;
		float sy = uz * fx - ux * fz;
		float sz = ux * fy - uy * fx;
		len = std::sqrt(sx * sx + sy * sy + sz * sz);
		sx /= len; sy /= len; sz /= len;
		ux = fy * sz - fz * sy;
		uy = fz * sx - fx * sz;
		uz = fx * sy - fy * sx;
		MatIdentity(m);
		m[0] = sx; m[4] = sy; m[8] = sz;
		m[1] = ux; m[5] = uy; m[9] = uz;
		m[2] = fx; m[6] = fy; m[10] = fz;
		m[12] = -(sx * ex + sy * ey + sz * ez);
		m[13] = -(ux * ex + uy * ey + uz * ez);
		m[14] = -(fx * ex + fy * ey + fz * ez);
	}

	void MatRotateY(float m[16], float a)
	{
		MatIdentity(m);
		m[0] = std::cos(a); m[2] = std::sin(a);
		m[8] = -std::sin(a); m[10] = std::cos(a);
	}

	void MatRotateX(float m[16], float a)
	{
		MatIdentity(m);
		m[5] = std::cos(a); m[6] = -std::sin(a);
		m[9] = std::sin(a); m[10] = std::cos(a);
	}

	void MatRotateZ(float m[16], float a)
	{
		MatIdentity(m);
		m[0] = std::cos(a); m[1] = -std::sin(a);
		m[4] = std::sin(a); m[5] = std::cos(a);
	}

	void MatTranspose(float o[16], const float m[16])
	{
		float t[16];
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				t[r * 4 + c] = m[c * 4 + r];
		std::memcpy(o, t, sizeof(t));
	}

	void ToD3DMatrix(D3DMATRIX& out, const float m[16])
	{
		std::memcpy(&out, m, sizeof(float) * 16);
	}

	void ClearProgrammable(IDirect3DDevice9* device)
	{
		device->SetVertexShader(nullptr);
		device->SetPixelShader(nullptr);
		device->SetVertexDeclaration(nullptr);
	}

	void BuildWorldViewProj(const WYDD3D9NativeDevice& out,
		const WYDD3D9NativeMesh& mesh, float angleRad,
		float ox, float oy, float oz, float scale,
		float lookX, float lookY, float lookZ, float frameRadius,
		float world[16], float view[16], float proj[16])
	{
		float rot[16], tmp[16], local[16];
		MatIdentity(local);
		local[0] = local[5] = local[10] = (scale > 1e-4f) ? scale : 1.f;
		local[12] = -mesh.center[0] * scale;
		local[13] = -mesh.center[1] * scale;
		local[14] = -mesh.center[2] * scale;
		MatRotateY(rot, angleRad);
		MatMul(tmp, rot, local);
		tmp[12] += mesh.center[0] * scale + ox;
		tmp[13] += mesh.center[1] * scale + oy;
		tmp[14] += mesh.center[2] * scale + oz;
		std::memcpy(world, tmp, 16 * sizeof(float));

		const float rad = (frameRadius > 1e-3f) ? frameRadius : mesh.radius;
		const float dist = rad * 3.2f;
		const float aspect = (out.back_h > 0)
			? static_cast<float>(out.back_w) / static_cast<float>(out.back_h)
			: (4.f / 3.f);
		const float lx = lookX;
		const float ly = lookY;
		const float lz = lookZ;
		MatLookAtD3D(view,
			lx, ly + rad * 0.4f, lz - dist,
			lx, ly, lz);
		MatPerspectiveD3D(proj, 0.8f, aspect, 0.05f, 100.f);
	}
}

bool WYD_D3D9NativeMeshUpload(WYDD3D9NativeDevice& device, WYDD3D9NativeMesh& mesh,
	const WYDMsaMesh& src, int boneCount)
{
	WYD_D3D9NativeMeshDestroy(mesh);
	if (!device.device_ok || !device.device)
		return false;
	if (src.positions.empty() || src.indices.empty() || src.vertex_count == 0)
		return false;
	if (src.indices.size() < 3 || (src.indices.size() % 3) != 0)
		return false;

	if (boneCount < 1)
		boneCount = 1;
	if (boneCount > 8)
		boneCount = 8;

	mesh.positions = src.positions;
	mesh.uvs = src.uvs;
	mesh.indices = src.indices;
	mesh.vertex_count = src.vertex_count;
	mesh.index_count = static_cast<uint32_t>(src.indices.size());
	mesh.center[0] = 0.5f * (src.min_x + src.max_x);
	mesh.center[1] = 0.5f * (src.min_y + src.max_y);
	mesh.center[2] = 0.5f * (src.min_z + src.max_z);
	mesh.min_y = src.min_y;
	mesh.max_y = src.max_y;
	const float dx = src.max_x - src.min_x;
	const float dy = src.max_y - src.min_y;
	const float dz = src.max_z - src.min_z;
	mesh.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
	if (mesh.radius < 0.01f)
		mesh.radius = 1.f;
	mesh.vb_stride = 36;
	mesh.bone_count = static_cast<uint32_t>(boneCount);
	mesh.ready = true;
	if (mesh.name.empty() && !src.texture_hint.empty())
		mesh.name = src.texture_hint;

	struct SkinVert
	{
		float x, y, z;
		DWORD blend;
		float nx, ny, nz;
		float u, v;
	};
	static_assert(sizeof(SkinVert) == 36, "skinmesh1 stride");

	std::vector<SkinVert> sverts(mesh.vertex_count);
	const float ySpan = (dy > 1e-5f) ? dy : 1.f;
	for (uint32_t i = 0; i < mesh.vertex_count; ++i)
	{
		const float px = mesh.positions[i * 3u + 0];
		const float py = mesh.positions[i * 3u + 1];
		const float pz = mesh.positions[i * 3u + 2];
		float nx = px - mesh.center[0];
		float ny = py - mesh.center[1];
		float nz = pz - mesh.center[2];
		const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
		if (len > 1e-5f) { nx /= len; ny /= len; nz /= len; }
		else { nx = 0.f; ny = 1.f; nz = 0.f; }
		float u = 0.5f + nx * 0.5f;
		float v = 0.5f - ny * 0.5f;
		if (mesh.uvs.size() >= (i + 1u) * 2u)
		{
			u = mesh.uvs[i * 2u + 0];
			v = mesh.uvs[i * 2u + 1];
		}
		float t = (py - mesh.min_y) / ySpan;
		if (t < 0.f) t = 0.f;
		if (t > 1.f) t = 1.f;
		int bi = static_cast<int>(t * static_cast<float>(boneCount));
		if (bi >= boneCount)
			bi = boneCount - 1;
		if (bi < 0)
			bi = 0;
		// UBYTE4: índice no byte 0 (canal .x do blendindices).
		const DWORD blend = static_cast<DWORD>(bi & 0xFF);
		sverts[i] = {px, py, pz, blend, nx, ny, nz, u, v};
	}

	auto* d3d = static_cast<IDirect3DDevice9*>(device.device);
	const UINT vbytes = mesh.vertex_count * sizeof(SkinVert);
	const UINT ibytes = mesh.index_count * sizeof(uint16_t);

	IDirect3DVertexBuffer9* vb = nullptr;
	IDirect3DIndexBuffer9* ib = nullptr;
	HRESULT hr = d3d->CreateVertexBuffer(
		vbytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb, nullptr);
	if (FAILED(hr) || !vb)
	{
		hr = d3d->CreateVertexBuffer(
			vbytes, D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &vb, nullptr);
	}
	if (FAILED(hr) || !vb)
	{
		std::fprintf(stderr, "[WYDLINUX][dxvk] CreateVertexBuffer hr=0x%08X — CPU only\n",
			static_cast<unsigned>(hr));
		std::fprintf(stderr, "[WYDLINUX][dxvk] mesh upload verts=%u idx=%u radius=%.3f gpu=0\n",
			mesh.vertex_count, mesh.index_count, mesh.radius);
		return true;
	}

	hr = d3d->CreateIndexBuffer(
		ibytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr);
	if (FAILED(hr) || !ib)
	{
		hr = d3d->CreateIndexBuffer(
			ibytes, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib, nullptr);
	}
	if (FAILED(hr) || !ib)
	{
		vb->Release();
		std::fprintf(stderr, "[WYDLINUX][dxvk] CreateIndexBuffer hr=0x%08X — CPU only\n",
			static_cast<unsigned>(hr));
		std::fprintf(stderr, "[WYDLINUX][dxvk] mesh upload verts=%u idx=%u radius=%.3f gpu=0\n",
			mesh.vertex_count, mesh.index_count, mesh.radius);
		return true;
	}

	void* mapped = nullptr;
	if (FAILED(vb->Lock(0, vbytes, &mapped, 0)) || !mapped)
	{
		vb->Release();
		ib->Release();
		std::fprintf(stderr, "[WYDLINUX][dxvk] VB Lock falhou — CPU only\n");
		return true;
	}
	std::memcpy(mapped, sverts.data(), vbytes);
	vb->Unlock();

	mapped = nullptr;
	if (FAILED(ib->Lock(0, ibytes, &mapped, 0)) || !mapped)
	{
		vb->Release();
		ib->Release();
		std::fprintf(stderr, "[WYDLINUX][dxvk] IB Lock falhou — CPU only\n");
		return true;
	}
	std::memcpy(mapped, mesh.indices.data(), ibytes);
	ib->Unlock();

	mesh.vb = vb;
	mesh.ib = ib;
	mesh.gpu_ok = true;
	std::fprintf(stderr,
		"[WYDLINUX][dxvk] mesh upload verts=%u idx=%u radius=%.3f gpu=1 bones=%u (VB/IB)\n",
		mesh.vertex_count, mesh.index_count, mesh.radius, mesh.bone_count);
	return true;
}

bool WYD_D3D9NativeMeshUploadSkinned(WYDD3D9NativeDevice& device, WYDD3D9NativeMesh& mesh,
	const WYDMshMesh& src)
{
	WYD_D3D9NativeMeshDestroy(mesh);
	if (!device.device_ok || !device.device)
		return false;
	if (src.vertex_count == 0 || src.index_count == 0 || src.vb.empty() || src.indices.empty())
		return false;
	if (src.influences < 1 || src.influences > 4)
		return false;
	static const uint32_t kStride[] = {0, 36, 40, 44, 48};
	if (src.stride != kStride[src.influences])
		return false;

	mesh.positions = src.positions;
	mesh.indices = src.indices;
	mesh.vertex_count = src.vertex_count;
	mesh.index_count = src.index_count;
	mesh.center[0] = 0.5f * (src.min_x + src.max_x);
	mesh.center[1] = 0.5f * (src.min_y + src.max_y);
	mesh.center[2] = 0.5f * (src.min_z + src.max_z);
	mesh.min_y = src.min_y;
	mesh.max_y = src.max_y;
	const float dx = src.max_x - src.min_x;
	const float dy = src.max_y - src.min_y;
	const float dz = src.max_z - src.min_z;
	mesh.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
	if (mesh.radius < 0.01f)
		mesh.radius = 1.f;
	mesh.vb_stride = src.stride;
	mesh.influences = src.influences;
	mesh.bone_count = src.palette ? src.palette : 1u;
	if (mesh.bone_count > 20)
		mesh.bone_count = 20;
	mesh.ready = true;
	mesh.name = src.path;

	auto* d3d = static_cast<IDirect3DDevice9*>(device.device);
	const UINT vbytes = static_cast<UINT>(src.vb.size());
	const UINT ibytes = mesh.index_count * sizeof(uint16_t);

	IDirect3DVertexBuffer9* vb = nullptr;
	IDirect3DIndexBuffer9* ib = nullptr;
	HRESULT hr = d3d->CreateVertexBuffer(
		vbytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb, nullptr);
	if (FAILED(hr) || !vb)
	{
		hr = d3d->CreateVertexBuffer(
			vbytes, D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &vb, nullptr);
	}
	if (FAILED(hr) || !vb)
	{
		std::fprintf(stderr, "[WYDLINUX][dxvk] CreateVertexBuffer (skinned) hr=0x%08X\n",
			static_cast<unsigned>(hr));
		return false;
	}
	hr = d3d->CreateIndexBuffer(
		ibytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr);
	if (FAILED(hr) || !ib)
	{
		hr = d3d->CreateIndexBuffer(
			ibytes, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib, nullptr);
	}
	if (FAILED(hr) || !ib)
	{
		vb->Release();
		std::fprintf(stderr, "[WYDLINUX][dxvk] CreateIndexBuffer (skinned) hr=0x%08X\n",
			static_cast<unsigned>(hr));
		return false;
	}

	void* mapped = nullptr;
	if (FAILED(vb->Lock(0, vbytes, &mapped, 0)) || !mapped)
	{
		vb->Release();
		ib->Release();
		return false;
	}
	std::memcpy(mapped, src.vb.data(), vbytes);
	vb->Unlock();

	mapped = nullptr;
	if (FAILED(ib->Lock(0, ibytes, &mapped, 0)) || !mapped)
	{
		vb->Release();
		ib->Release();
		return false;
	}
	std::memcpy(mapped, mesh.indices.data(), ibytes);
	ib->Unlock();

	mesh.vb = vb;
	mesh.ib = ib;
	mesh.gpu_ok = true;
	std::fprintf(stderr,
		"[WYDLINUX][dxvk] mesh upload verts=%u idx=%u radius=%.3f gpu=1 bones=%u infl=%u (MSH)\n",
		mesh.vertex_count, mesh.index_count, mesh.radius, mesh.bone_count, mesh.influences);
	return true;
}

bool WYD_D3D9NativeMeshSetTextureRgba(WYDD3D9NativeDevice& device, WYDD3D9NativeMesh& mesh,
	const uint8_t* rgba, uint32_t width, uint32_t height)
{
	if (!device.device_ok || !device.device || !rgba || width == 0 || height == 0)
		return false;
	auto* d3d = static_cast<IDirect3DDevice9*>(device.device);
	if (mesh.tex)
	{
		static_cast<IDirect3DTexture9*>(mesh.tex)->Release();
		mesh.tex = nullptr;
	}

	IDirect3DTexture9* tex = nullptr;
	HRESULT hr = d3d->CreateTexture(
		width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
	if (FAILED(hr) || !tex)
	{
		hr = d3d->CreateTexture(
			width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
	}
	if (FAILED(hr) || !tex)
	{
		std::fprintf(stderr, "[WYDLINUX][dxvk] mesh CreateTexture hr=0x%08X\n",
			static_cast<unsigned>(hr));
		return false;
	}

	D3DLOCKED_RECT lr {};
	if (FAILED(tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)) &&
		FAILED(tex->LockRect(0, &lr, nullptr, 0)))
	{
		tex->Release();
		return false;
	}
	auto* dstBase = static_cast<uint8_t*>(lr.pBits);
	for (uint32_t y = 0; y < height; ++y)
	{
		auto* dst = dstBase + static_cast<size_t>(y) * static_cast<size_t>(lr.Pitch);
		const uint8_t* src = rgba + static_cast<size_t>(y) * width * 4u;
		for (uint32_t x = 0; x < width; ++x)
		{
			dst[x * 4u + 0] = src[x * 4u + 2];
			dst[x * 4u + 1] = src[x * 4u + 1];
			dst[x * 4u + 2] = src[x * 4u + 0];
			dst[x * 4u + 3] = src[x * 4u + 3];
		}
	}
	tex->UnlockRect(0);
	mesh.tex = tex;
	std::fprintf(stderr, "[WYDLINUX][dxvk] mesh texture %ux%u OK (%s)\n",
		width, height, mesh.name.empty() ? "?" : mesh.name.c_str());
	return true;
}

bool WYD_D3D9NativeSceneBegin(WYDD3D9NativeDevice& out, uint32_t clear_argb)
{
	if (!out.device_ok || !out.device || out.scene_open)
		return false;
	auto* device = static_cast<IDirect3DDevice9*>(out.device);
	if (!WYD_D3D9NativeClear(out, clear_argb))
		return false;
	if (FAILED(device->BeginScene()))
		return false;
	device->SetRenderState(D3DRS_ZENABLE, TRUE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	out.scene_open = true;
	out.scene_draws = 0;
	return true;
}

bool WYD_D3D9NativeMeshDrawInScene(WYDD3D9NativeDevice& out, const WYDD3D9NativeMesh& mesh,
	float angleRad, float r, float g, float b, int skinIndex, int psIndex,
	float ox, float oy, float oz, float scale,
	float lookX, float lookY, float lookZ, float frameRadius, float boneTime,
	const float* boneMats, uint32_t boneMatCount)
{
	if (!out.device_ok || !out.device || !out.scene_open || !mesh.ready)
		return false;
	auto* device = static_cast<IDirect3DDevice9*>(out.device);

	float world[16], view[16], proj[16];
	const float lx = (frameRadius > 1e-3f) ? lookX : mesh.center[0];
	const float ly = (frameRadius > 1e-3f) ? lookY : mesh.center[1];
	const float lz = (frameRadius > 1e-3f) ? lookZ : mesh.center[2];
	const float fr = (frameRadius > 1e-3f) ? frameRadius : 0.f;
	BuildWorldViewProj(out, mesh, angleRad, ox, oy, oz, scale, lx, ly, lz, fr,
		world, view, proj);

	device->SetRenderState(D3DRS_ALPHABLENDENABLE, mesh.tex ? TRUE : FALSE);
	if (mesh.tex)
	{
		device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		device->SetTexture(0, static_cast<IDirect3DTexture9*>(mesh.tex));
		device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	}
	else
	{
		device->SetTexture(0, nullptr);
	}

	const UINT primCount = mesh.index_count / 3u;
	bool usedShader = false;
	const bool useRealBones = (boneMats != nullptr && boneMatCount > 0);

	// MSH real: VS/decl = influences-1. MSA procedural: skinmesh1. Stand-in: skinIndex.
	int vsSlot = skinIndex;
	int declSlot = 0;
	if (useRealBones)
	{
		const int infl = static_cast<int>(mesh.influences > 0 ? mesh.influences : 1);
		vsSlot = infl - 1;
		if (vsSlot < 0) vsSlot = 0;
		if (vsSlot > 3) vsSlot = 3;
		declSlot = vsSlot;
	}
	else if (mesh.bone_count > 1)
	{
		vsSlot = 0;
		declSlot = 0;
	}
	if (vsSlot < 0) vsSlot = 0;
	if (vsSlot > 7) vsSlot = 7;
	if (declSlot < 0) declSlot = 0;
	if (declSlot > 3) declSlot = 3;

	void* vdeclPtr = out.vdecl_skin[declSlot];
	if (vsSlot >= 0 && vsSlot < 8 && out.vs_skin[vsSlot] && vdeclPtr)
	{
		float wv[16], projT[16], viewInv[16], viewInvT[16];
		MatMul(wv, world, view);
		MatTranspose(projT, proj);
		MatIdentity(viewInv);
		viewInv[0] = view[0]; viewInv[1] = view[4]; viewInv[2] = view[8];
		viewInv[4] = view[1]; viewInv[5] = view[5]; viewInv[6] = view[9];
		viewInv[8] = view[2]; viewInv[9] = view[6]; viewInv[10] = view[10];
		viewInv[12] = -(viewInv[0] * view[12] + viewInv[4] * view[13] + viewInv[8] * view[14]);
		viewInv[13] = -(viewInv[1] * view[12] + viewInv[5] * view[13] + viewInv[9] * view[14]);
		viewInv[14] = -(viewInv[2] * view[12] + viewInv[6] * view[13] + viewInv[10] * view[14]);
		MatTranspose(viewInvT, viewInv);

		const float light[4] = {-0.577f, 0.577f, 0.577f, 0.f};
		device->SetVertexDeclaration(static_cast<IDirect3DVertexDeclaration9*>(vdeclPtr));
		device->SetVertexShader(static_cast<IDirect3DVertexShader9*>(out.vs_skin[vsSlot]));
		IDirect3DPixelShader9* ps = nullptr;
		if (psIndex >= 0 && psIndex < 6 && out.ps_effect[psIndex])
			ps = static_cast<IDirect3DPixelShader9*>(out.ps_effect[psIndex]);
		device->SetPixelShader(ps);
		device->SetVertexShaderConstantF(1, light, 1);
		device->SetVertexShaderConstantF(2, projT, 4);
		device->SetVertexShaderConstantF(92, viewInvT, 4);
		device->SetRenderState(D3DRS_LIGHTING, FALSE);

		uint32_t bones = mesh.bone_count > 0 ? mesh.bone_count : 1u;
		if (useRealBones)
		{
			bones = boneMatCount;
			if (bones > 20)
				bones = 20;
		}
		for (uint32_t bi = 0; bi < bones; ++bi)
		{
			float boneLocal[16], boneWV[16], boneWVT[16];
			MatIdentity(boneLocal);
			if (useRealBones)
			{
				std::memcpy(boneLocal, boneMats + static_cast<size_t>(bi) * 16u, sizeof(boneLocal));
			}
			else if (bones > 1u)
			{
				const float wave = std::sin(boneTime * 2.2f + static_cast<float>(bi) * 0.85f) * 0.40f;
				const float twist = std::cos(boneTime * 1.6f + static_cast<float>(bi) * 0.55f) * 0.25f;
				float rx[16], rz[16], tmp[16];
				MatRotateX(rx, wave);
				MatRotateZ(rz, twist);
				MatMul(tmp, rz, rx);
				float toOrigin[16], back[16];
				MatIdentity(toOrigin);
				toOrigin[12] = -mesh.center[0];
				toOrigin[13] = -mesh.center[1];
				toOrigin[14] = -mesh.center[2];
				MatIdentity(back);
				back[12] = mesh.center[0];
				back[13] = mesh.center[1];
				back[14] = mesh.center[2];
				float a[16], b[16];
				MatMul(a, tmp, toOrigin);
				MatMul(b, back, a);
				std::memcpy(boneLocal, b, sizeof(boneLocal));
			}
			MatMul(boneWV, boneLocal, wv); // bone * worldView (row-major D3DX)
			MatTranspose(boneWVT, boneWV);
			device->SetVertexShaderConstantF(9 + 3 * bi, boneWVT, 3);
		}

		HRESULT hr = E_FAIL;
		const bool useGpu = mesh.gpu_ok && mesh.vb && mesh.ib;
		if (useGpu)
		{
			device->SetStreamSource(0, static_cast<IDirect3DVertexBuffer9*>(mesh.vb),
				0, mesh.vb_stride ? mesh.vb_stride : 36u);
			device->SetIndices(static_cast<IDirect3DIndexBuffer9*>(mesh.ib));
			hr = device->DrawIndexedPrimitive(
				D3DPT_TRIANGLELIST, 0, 0, mesh.vertex_count, 0, primCount);
		}
		else
		{
			struct SkinVert { float x, y, z; DWORD blend; float nx, ny, nz; float u, v; };
			std::vector<SkinVert> sverts(mesh.vertex_count);
			const float ySpan = (mesh.max_y - mesh.min_y > 1e-5f)
				? (mesh.max_y - mesh.min_y) : 1.f;
			const int boneN = static_cast<int>(bones);
			for (uint32_t i = 0; i < mesh.vertex_count; ++i)
			{
				const float px = mesh.positions[i * 3u + 0];
				const float py = mesh.positions[i * 3u + 1];
				const float pz = mesh.positions[i * 3u + 2];
				float nx = px - mesh.center[0], ny = py - mesh.center[1], nz = pz - mesh.center[2];
				const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
				if (len > 1e-5f) { nx /= len; ny /= len; nz /= len; }
				else { nx = 0.f; ny = 1.f; nz = 0.f; }
				float u = 0.5f + nx * 0.5f, v = 0.5f - ny * 0.5f;
				if (mesh.uvs.size() >= (i + 1u) * 2u)
				{
					u = mesh.uvs[i * 2u + 0];
					v = mesh.uvs[i * 2u + 1];
				}
				float t = (py - mesh.min_y) / ySpan;
				if (t < 0.f) t = 0.f;
				if (t > 1.f) t = 1.f;
				int bi = static_cast<int>(t * static_cast<float>(boneN));
				if (bi >= boneN) bi = boneN - 1;
				if (bi < 0) bi = 0;
				sverts[i] = {px, py, pz, static_cast<DWORD>(bi & 0xFF), nx, ny, nz, u, v};
			}
			hr = device->DrawIndexedPrimitiveUP(
				D3DPT_TRIANGLELIST, 0, mesh.vertex_count, primCount,
				mesh.indices.data(), D3DFMT_INDEX16, sverts.data(), sizeof(SkinVert));
		}

		if (SUCCEEDED(hr))
		{
			usedShader = true;
			if (mesh.tex)
				out.texture_draw_ok = true;
			if (ps)
				out.pixel_shader_draw_ok = true;
			if (useGpu)
				out.gpu_draw_ok = true;
			out.shader_draw_ok = true;
			if (useRealBones)
			{
				out.bone_real_ok = true;
				if (mesh.influences >= 2)
					out.skin_multi_ok = true;
			}
			else if (bones > 1u)
				out.bone_ok = true;
		}
		else
		{
			ClearProgrammable(device);
			device->SetStreamSource(0, nullptr, 0, 0);
			device->SetIndices(nullptr);
		}
	}

	if (!usedShader)
	{
		const uint32_t cr = static_cast<uint32_t>(std::max(0.f, std::min(1.f, r)) * 255.f + 0.5f);
		const uint32_t cg = static_cast<uint32_t>(std::max(0.f, std::min(1.f, g)) * 255.f + 0.5f);
		const uint32_t cb = static_cast<uint32_t>(std::max(0.f, std::min(1.f, b)) * 255.f + 0.5f);
		const DWORD diffuse = 0xFF000000u | (cr << 16) | (cg << 8) | cb;
		struct Vert { float x, y, z; DWORD color; float u, v; };
		std::vector<Vert> verts(mesh.vertex_count);
		for (uint32_t i = 0; i < mesh.vertex_count; ++i)
		{
			verts[i].x = mesh.positions[i * 3u + 0];
			verts[i].y = mesh.positions[i * 3u + 1];
			verts[i].z = mesh.positions[i * 3u + 2];
			verts[i].color = diffuse;
			verts[i].u = (mesh.uvs.size() >= (i + 1u) * 2u) ? mesh.uvs[i * 2u + 0] : 0.f;
			verts[i].v = (mesh.uvs.size() >= (i + 1u) * 2u) ? mesh.uvs[i * 2u + 1] : 0.f;
		}
		ClearProgrammable(device);
		D3DMATRIX mw {}, mv {}, mp {};
		ToD3DMatrix(mw, world);
		ToD3DMatrix(mv, view);
		ToD3DMatrix(mp, proj);
		device->SetRenderState(D3DRS_LIGHTING, FALSE);
		device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		device->SetTransform(D3DTS_WORLD, &mw);
		device->SetTransform(D3DTS_VIEW, &mv);
		device->SetTransform(D3DTS_PROJECTION, &mp);
		const HRESULT hr = device->DrawIndexedPrimitiveUP(
			D3DPT_TRIANGLELIST, 0, mesh.vertex_count, primCount,
			mesh.indices.data(), D3DFMT_INDEX16, verts.data(), sizeof(Vert));
		if (FAILED(hr))
			return false;
		if (mesh.tex)
			out.texture_draw_ok = true;
	}

	++out.scene_draws;
	if (out.scene_draws >= 2)
		out.multi_mesh_ok = true;
	return true;
}

bool WYD_D3D9NativeSceneEnd(WYDD3D9NativeDevice& out)
{
	if (!out.device_ok || !out.device || !out.scene_open)
		return false;
	auto* device = static_cast<IDirect3DDevice9*>(out.device);
	ClearProgrammable(device);
	device->SetStreamSource(0, nullptr, 0, 0);
	device->SetIndices(nullptr);
	device->SetTexture(0, nullptr);
	device->EndScene();
	out.scene_open = false;
	if (out.scene_draws >= 2 && !out.multi_mesh_ok)
		out.multi_mesh_ok = true;
	if (out.multi_mesh_ok && out.scene_draws >= 2)
	{
		static bool loggedM = false;
		if (!loggedM)
		{
			loggedM = true;
			std::fprintf(stderr, "[WYDLINUX][dxvk] Fase M: scene multi-mesh draws=%d OK\n",
				out.scene_draws);
		}
	}
	if (out.bone_ok)
	{
		static bool loggedN = false;
		if (!loggedN)
		{
			loggedN = true;
			std::fprintf(stderr,
				"[WYDLINUX][dxvk] Fase N: bone palette c9+ (skinmesh1) OK\n");
		}
	}
	if (out.bone_real_ok)
	{
		static bool loggedO = false;
		if (!loggedO)
		{
			loggedO = true;
			std::fprintf(stderr,
				"[WYDLINUX][dxvk] Fase O: real bone palette c9+ (skinmesh1) OK\n");
		}
	}
	if (out.multi_part_ok)
	{
		static bool loggedP = false;
		if (!loggedP)
		{
			loggedP = true;
			std::fprintf(stderr,
				"[WYDLINUX][dxvk] Fase P: multi-part char parts=%d OK\n",
				out.char_parts_drawn);
		}
	}
	if (out.skin_multi_ok)
	{
		static bool loggedQ = false;
		if (!loggedQ)
		{
			loggedQ = true;
			std::fprintf(stderr,
				"[WYDLINUX][dxvk] Fase Q: skinmesh2+ (VertexDecl2-4) OK\n");
		}
	}
	return WYD_D3D9NativePresent(out);
}

bool WYD_D3D9NativeMeshDraw(WYDD3D9NativeDevice& out, const WYDD3D9NativeMesh& mesh,
	float angleRad, float r, float g, float b, int skinIndex, int psIndex)
{
	if (!WYD_D3D9NativeSceneBegin(out, 0xFF0A0F1Au))
		return false;
	if (!WYD_D3D9NativeMeshDrawInScene(out, mesh, angleRad, r, g, b, skinIndex, psIndex,
		0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f))
	{
		out.scene_open = false;
		static_cast<IDirect3DDevice9*>(out.device)->EndScene();
		return false;
	}
	return WYD_D3D9NativeSceneEnd(out);
}

void WYD_D3D9NativeMeshDestroy(WYDD3D9NativeMesh& mesh)
{
	if (mesh.tex)
	{
		static_cast<IDirect3DTexture9*>(mesh.tex)->Release();
		mesh.tex = nullptr;
	}
	if (mesh.vb)
	{
		static_cast<IDirect3DVertexBuffer9*>(mesh.vb)->Release();
		mesh.vb = nullptr;
	}
	if (mesh.ib)
	{
		static_cast<IDirect3DIndexBuffer9*>(mesh.ib)->Release();
		mesh.ib = nullptr;
	}
	mesh.gpu_ok = false;
	mesh.ready = false;
	mesh.positions.clear();
	mesh.uvs.clear();
	mesh.indices.clear();
	mesh.vertex_count = 0;
	mesh.index_count = 0;
	mesh.name.clear();
}

void WYD_D3D9NativeDestroy(WYDD3D9NativeDevice& out)
{
	ReleaseBlitTex(out);
	for (auto& p : out.vs_skin)
	{
		if (p) { static_cast<IDirect3DVertexShader9*>(p)->Release(); p = nullptr; }
	}
	for (auto& p : out.vs_effect)
	{
		if (p) { static_cast<IDirect3DVertexShader9*>(p)->Release(); p = nullptr; }
	}
	for (auto& p : out.ps_effect)
	{
		if (p) { static_cast<IDirect3DPixelShader9*>(p)->Release(); p = nullptr; }
	}
	for (auto& d : out.vdecl_skin)
	{
		if (d)
		{
			static_cast<IDirect3DVertexDeclaration9*>(d)->Release();
			d = nullptr;
		}
	}
	if (out.device)
	{
		static_cast<IDirect3DDevice9*>(out.device)->Release();
		out.device = nullptr;
	}
	if (out.d3d9)
	{
		static_cast<IDirect3D9*>(out.d3d9)->Release();
		out.d3d9 = nullptr;
	}
	if (out.so)
	{
		dlclose(out.so);
		out.so = nullptr;
	}
	out.device_ok = false;
	out.loaded = false;
	out.shaders_ok = false;
	out.shader_draw_ok = false;
	out.gpu_draw_ok = false;
}

bool WYD_D3D9NativeAvailable()
{
	WYDD3D9NativeDevice tmp {};
	const bool ok = WYD_D3D9NativeLoad(tmp);
	if (ok)
		WYD_D3D9NativeDestroy(tmp);
	return ok;
}

#else // !WYD_HAS_DXVK_NATIVE_HEADERS

#include "d3d9_dxvk_native.h"
#include <cstdio>

bool WYD_D3D9NativeLoad(WYDD3D9NativeDevice& out)
{
	out = {};
	out.status = "headers DXVK Native ausentes";
	return false;
}
bool WYD_D3D9NativeCreateDevice(WYDD3D9NativeDevice&, SDL_Window*, uint32_t, uint32_t)
{
	return false;
}
int WYD_D3D9NativeCreateShadersFromCatalog(WYDD3D9NativeDevice&, const void*)
{
	return 0;
}
bool WYD_D3D9NativeClear(WYDD3D9NativeDevice&, uint32_t) { return false; }
bool WYD_D3D9NativePresent(WYDD3D9NativeDevice&) { return false; }
bool WYD_D3D9NativeUploadRgba(WYDD3D9NativeDevice&, const uint8_t*, uint32_t, uint32_t)
{
	return false;
}
bool WYD_D3D9NativeDrawRgba(WYDD3D9NativeDevice&, const uint8_t*, uint32_t, uint32_t, uint32_t)
{
	return false;
}
bool WYD_D3D9NativeMeshUpload(WYDD3D9NativeDevice&, WYDD3D9NativeMesh&,
	const WYDMsaMesh&, int)
{
	return false;
}
bool WYD_D3D9NativeMeshUploadSkinned(WYDD3D9NativeDevice&, WYDD3D9NativeMesh&,
	const WYDMshMesh&)
{
	return false;
}
bool WYD_D3D9NativeMeshSetTextureRgba(WYDD3D9NativeDevice&, WYDD3D9NativeMesh&,
	const uint8_t*, uint32_t, uint32_t)
{
	return false;
}
bool WYD_D3D9NativeSceneBegin(WYDD3D9NativeDevice&, uint32_t) { return false; }
bool WYD_D3D9NativeMeshDrawInScene(WYDD3D9NativeDevice&, const WYDD3D9NativeMesh&,
	float, float, float, float, int, int, float, float, float, float,
	float, float, float, float, float, const float*, uint32_t)
{
	return false;
}
bool WYD_D3D9NativeSceneEnd(WYDD3D9NativeDevice&) { return false; }
bool WYD_D3D9NativeMeshDraw(WYDD3D9NativeDevice&, const WYDD3D9NativeMesh&,
	float, float, float, float, int, int)
{
	return false;
}
void WYD_D3D9NativeMeshDestroy(WYDD3D9NativeMesh& mesh)
{
	mesh = {};
}
void WYD_D3D9NativeDestroy(WYDD3D9NativeDevice& out) { out = {}; }
bool WYD_D3D9NativeAvailable() { return false; }

#endif
