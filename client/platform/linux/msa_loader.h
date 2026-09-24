#pragma once

// Loader MSA 7.48 (subconjunto de TMMesh::LoadMsa) sem DirectX.
// Extrai índices u16 e posições xyz para render Vulkan colorido.

#include <cstdint>
#include <string>
#include <vector>

struct WYDMsaMesh
{
	uint32_t fvf = 0;
	uint32_t vertex_stride = 0;
	uint32_t attribute_count = 0;
	std::string texture_hint; // nome curto lido do arquivo (sem path)
	std::vector<uint16_t> indices;
	std::vector<float> positions; // x,y,z por vértice
	std::vector<float> uvs;       // u,v por vértice (se FVF tiver TEX)
	uint32_t vertex_count = 0;
	float min_x = 0, max_x = 0, min_y = 0, max_y = 0, min_z = 0, max_z = 0;
	std::string error;
};

// relativeOrListPath: "mesh/throw07.msa" ou entrada crua do MeshList.
bool WYD_LoadMsaMesh(const char* relativeOrListPath, WYDMsaMesh& out);
