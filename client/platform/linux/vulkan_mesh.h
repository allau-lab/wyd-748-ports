#pragma once

#include "vulkan_present.h"
#include "msa_loader.h"

#include <cstdint>

struct SDL_Window;

struct WYDVulkanMesh
{
	WYDVulkanPresent present {};
	void* render_pass = nullptr;
	void* pipeline_layout = nullptr;
	void* pipeline = nullptr;
	void* vertex_buffer = nullptr;
	void* vertex_memory = nullptr;
	void* index_buffer = nullptr;
	void* index_memory = nullptr;
	void** framebuffers = nullptr;
	uint32_t index_count = 0;
	uint32_t vertex_count = 0;
	float center[3] {};
	float radius = 1.f;
	bool ready = false;
};

bool WYD_VulkanMeshCreate(WYDVulkanMesh& out, SDL_Window* window,
	uint32_t width, uint32_t height);

bool WYD_VulkanMeshUpload(WYDVulkanMesh& vm, const WYDMsaMesh& mesh);

// angleRad: rotação Y. rgb: tint do stand-in DX9 ativo (Fase G+).
bool WYD_VulkanMeshDraw(WYDVulkanMesh& vm, float angleRad,
	float r = 0.45f, float g = 0.75f, float b = 0.95f);

void WYD_VulkanMeshDestroy(WYDVulkanMesh& vm);
