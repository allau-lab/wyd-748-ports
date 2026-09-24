#pragma once

// Loader .msh skinado 7.48 (CMesh::LoadMesh) — skinmesh1..4 (infl 1–4).

#include <cstdint>
#include <string>
#include <vector>

struct WYDMshMesh
{
	uint32_t parent_id = 0;
	uint32_t bone_id = 0;
	uint32_t fvf = 0;
	uint32_t stride = 0;
	uint32_t influences = 0;
	uint32_t palette = 0;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0; // FaceCount do arquivo (= índices u16)
	std::vector<float> bind_pose; // palette * 16
	std::vector<uint32_t> bone_names; // palette IDs no skeleton .bon
	std::vector<uint8_t> vb; // vertex_count * stride
	std::vector<uint16_t> indices;
	std::vector<float> positions; // xyz (para bbox/câmera)
	float min_x = 0, max_x = 0, min_y = 0, max_y = 0, min_z = 0, max_z = 0;
	std::string path;
	std::string error;
};

bool WYD_LoadMshMesh(const char* relativeOrAbs, WYDMshMesh& out);
