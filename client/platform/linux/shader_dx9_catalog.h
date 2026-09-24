#pragma once

// Catálogo dos 18 shaders DX9 pré-compilados exigidos pelo RenderDevice.
// Paths case-sensitive: Shader/skinmesh%d.bin, vseffect%d.bin, pseffect%d.bin.
// No Linux estes bins NÃO executam direto no Vulkan — validamos o bytecode
// e mantemos os bytes para futura ponte DXVK. Execução imediata usa SPIR-V.

#include <cstdint>
#include <string>
#include <vector>

enum class WYDDx9ShaderKind : uint8_t
{
	SkinMesh = 0,
	VsEffect = 1,
	PsEffect = 2,
};

struct WYDDx9ShaderEntry
{
	WYDDx9ShaderKind kind = WYDDx9ShaderKind::SkinMesh;
	int index = 0; // 1-based como no TMPaths
	std::string relative_path;
	std::vector<uint8_t> bytecode;
	uint32_t version_token = 0;
	bool valid = false;
	std::string error;
};

struct WYDDx9ShaderCatalog
{
	std::vector<WYDDx9ShaderEntry> entries;
	int loaded = 0;
	int failed = 0;
};

// Carrega os 18 arquivos a partir de WYD_ASSET_ROOT. Retorna false se algum
// crítico faltar ou o token VS/PS for inválido.
bool WYD_LoadDx9ShaderCatalog(WYDDx9ShaderCatalog& out);

// vs_1_x = 0xFFFE01xx, ps_1_x = 0xFFFF01xx (little-endian no arquivo).
const char* WYD_Dx9ShaderVersionName(uint32_t token);
