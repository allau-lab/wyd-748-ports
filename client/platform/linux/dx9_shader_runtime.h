#pragma once

// Fase G+: runtime dos shaders DX9 catalogados.
// Sem libdxvk_d3d9.so: Create*Shader valida bytecode e marca stand-in Vulkan.
// Com WYD_D3D9_SO / libdxvk_d3d9.so: probe de Direct3DCreate9 nativo.

#include "shader_dx9_catalog.h"

#include <cstdint>
#include <string>
#include <vector>

struct WYDDx9RuntimeEntry
{
	std::string path;
	WYDDx9ShaderKind kind = WYDDx9ShaderKind::SkinMesh;
	uint32_t version_token = 0;
	uint32_t word_count = 0;
	uint32_t instruction_count = 0;
	bool created = false;
	bool standin_ready = false; // elegível para pipeline Vulkan stand-in
	std::string error;
};

struct WYDDx9ShaderRuntime
{
	std::vector<WYDDx9RuntimeEntry> entries;
	int created = 0;
	int standin_ready = 0;
	int failed = 0;
	bool native_d3d9_probed = false;
	bool native_d3d9_available = false;
	std::string native_d3d9_path;
	std::string native_d3d9_note;
};

// Probe dlopen: WYD_D3D9_SO, depois libdxvk_d3d9.so / libd3d9.so.
void WYD_ProbeNativeD3D9(WYDDx9ShaderRuntime& rt);

// Create*Shader para cada entrada válida do catálogo + contagem de instruções.
bool WYD_Dx9RuntimeBuild(WYDDx9ShaderRuntime& rt, const WYDDx9ShaderCatalog& catalog);

// Índice 0..n-1 do stand-in ativo (cicla skinmesh para tint 3D).
int WYD_Dx9RuntimeActiveStandin(const WYDDx9ShaderRuntime& rt, uint32_t frame);
