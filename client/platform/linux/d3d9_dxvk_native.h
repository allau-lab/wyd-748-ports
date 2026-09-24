#pragma once

// Fase H..Q: device D3D9 via DXVK Native (libdxvk_d3d9.so), sem Wine.
// HWND = SDL_Window*. Requer DXVK_WSI_DRIVER=SDL2.

#include "msa_loader.h"
#include "msh_loader.h"

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

struct WYDD3D9NativeDevice
{
	bool loaded = false;
	bool device_ok = false;
	bool shaders_ok = false;
	bool shader_draw_ok = false;
	bool gpu_draw_ok = false;
	bool multi_mesh_ok = false;
	bool bone_ok = false; // palette c9+ com blend indices (Fase N procedural)
	bool bone_real_ok = false; // palette c9+ de .msh/.bon/.ani (Fase O)
	bool multi_part_ok = false; // ≥2 partes .msh no mesmo skeleton (Fase P)
	bool skin_multi_ok = false; // draw com infl≥2 / skinmesh2+ (Fase Q)
	int char_parts_drawn = 0;
	int shaders_created = 0;
	int vs_skin_count = 0;
	int vs_effect_count = 0;
	int ps_effect_count = 0;
	int scene_draws = 0;
	uint32_t back_w = 0;
	uint32_t back_h = 0;
	uint32_t blit_w = 0;
	uint32_t blit_h = 0;
	std::string lib_path;
	std::string status;
	void* so = nullptr;
	void* d3d9 = nullptr;
	void* device = nullptr;
	void* blit_tex = nullptr;
	void* vdecl_skin[4] {}; // VertexDecl1..4
	bool texture_draw_ok = false;
	bool pixel_shader_draw_ok = false;
	bool scene_open = false;
	void* vs_skin[8] {};
	void* vs_effect[4] {};
	void* ps_effect[6] {};
};

struct WYDD3D9NativeMesh
{
	bool ready = false;
	bool gpu_ok = false;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0;
	uint32_t vb_stride = 36;
	uint32_t influences = 1; // 1..4 → skinmeshN / VertexDeclN
	uint32_t bone_count = 1; // palette size
	float center[3] {};
	float radius = 1.f;
	float min_y = 0.f;
	float max_y = 0.f;
	std::vector<float> positions;
	std::vector<float> uvs;
	std::vector<uint16_t> indices;
	void* vb = nullptr;
	void* ib = nullptr;
	void* tex = nullptr;
	std::string name;
};

bool WYD_D3D9NativeLoad(WYDD3D9NativeDevice& out);

bool WYD_D3D9NativeCreateDevice(WYDD3D9NativeDevice& out, SDL_Window* window,
	uint32_t width, uint32_t height);

int WYD_D3D9NativeCreateShadersFromCatalog(WYDD3D9NativeDevice& out,
	const void* catalog);

bool WYD_D3D9NativeClear(WYDD3D9NativeDevice& out, uint32_t argb);
bool WYD_D3D9NativePresent(WYDD3D9NativeDevice& out);

bool WYD_D3D9NativeUploadRgba(WYDD3D9NativeDevice& out,
	const uint8_t* rgba, uint32_t width, uint32_t height);

bool WYD_D3D9NativeDrawRgba(WYDD3D9NativeDevice& out,
	const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t clear_argb);

// boneCount: faixas de altura → blend index 0..N-1 (max 8).
bool WYD_D3D9NativeMeshUpload(WYDD3D9NativeDevice& device, WYDD3D9NativeMesh& mesh,
	const WYDMsaMesh& src, int boneCount = 4);

// Upload VB/IB direto do .msh (blend indices reais, skinmesh1).
bool WYD_D3D9NativeMeshUploadSkinned(WYDD3D9NativeDevice& device, WYDD3D9NativeMesh& mesh,
	const WYDMshMesh& src);

bool WYD_D3D9NativeMeshSetTextureRgba(WYDD3D9NativeDevice& device, WYDD3D9NativeMesh& mesh,
	const uint8_t* rgba, uint32_t width, uint32_t height);

bool WYD_D3D9NativeSceneBegin(WYDD3D9NativeDevice& out, uint32_t clear_argb);

// boneTime: anima palette procedural (onda) se boneMats==nullptr.
// boneMats: palette * 16 floats (bind*combined); força skinmesh1.
bool WYD_D3D9NativeMeshDrawInScene(WYDD3D9NativeDevice& out, const WYDD3D9NativeMesh& mesh,
	float angleRad, float r, float g, float b, int skinIndex, int psIndex,
	float ox, float oy, float oz, float scale = 1.f,
	float lookX = 0.f, float lookY = 0.f, float lookZ = 0.f, float frameRadius = 0.f,
	float boneTime = 0.f,
	const float* boneMats = nullptr, uint32_t boneMatCount = 0);

bool WYD_D3D9NativeSceneEnd(WYDD3D9NativeDevice& out);

bool WYD_D3D9NativeMeshDraw(WYDD3D9NativeDevice& out, const WYDD3D9NativeMesh& mesh,
	float angleRad, float r, float g, float b, int skinIndex = -1, int psIndex = -1);

void WYD_D3D9NativeMeshDestroy(WYDD3D9NativeMesh& mesh);

void WYD_D3D9NativeDestroy(WYDD3D9NativeDevice& out);

bool WYD_D3D9NativeAvailable();
