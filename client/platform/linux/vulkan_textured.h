#pragma once

#include "vulkan_present.h"
#include "wyt_decode.h"

#include <cstdint>

struct SDL_Window;

// Blit de textura RGBA na superfície Wayland via SPIR-V (não DX9 bytecode).
struct WYDVulkanTextured
{
	WYDVulkanPresent present {};
	void* render_pass = nullptr;
	void* pipeline_layout = nullptr;
	void* pipeline = nullptr;
	void* descriptor_set_layout = nullptr;
	void* descriptor_pool = nullptr;
	void* descriptor_set = nullptr;
	void* sampler = nullptr;
	void* texture_image = nullptr;
	void* texture_memory = nullptr;
	void* texture_view = nullptr;
	void* vertex_buffer = nullptr;
	void* vertex_memory = nullptr;
	void** framebuffers = nullptr; // image_count
	bool ready = false;
};

bool WYD_VulkanTexturedCreate(WYDVulkanTextured& out, SDL_Window* window,
	uint32_t width, uint32_t height);

bool WYD_VulkanTexturedUploadRgba(WYDVulkanTextured& vt,
	const WYDWytImage& image);

bool WYD_VulkanTexturedDraw(WYDVulkanTextured& vt);

void WYD_VulkanTexturedDestroy(WYDVulkanTextured& vt);
