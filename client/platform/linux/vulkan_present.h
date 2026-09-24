#pragma once

// Swapchain Vulkan sobre janela SDL/Wayland.
// Responsabilidade: clear-color + Present. Sem shaders de jogo ainda.
// Ownership: Create possui instance/device/swapchain; Destroy libera tudo.

#include <cstdint>

struct SDL_Window;

struct WYDVulkanPresent
{
	void* window = nullptr; // SDL_Window*
	void* instance = nullptr;
	void* surface = nullptr;
	void* physical = nullptr;
	void* device = nullptr;
	void* queue = nullptr;
	void* swapchain = nullptr;
	void* command_pool = nullptr;
	void* command_buffer = nullptr;
	void* image_available = nullptr;
	void* render_finished = nullptr;
	void* in_flight = nullptr;
	void** image_views = nullptr;
	uint32_t image_count = 0;
	uint32_t queue_family = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t clear_rgba = 0xFF1A2740; // BGRA-ish packed as RRGGBBAA for API
	int format = 0; // VkFormat stored as int
};

// Extensões Wayland/Vulkan via SDL. Falha se driver Vulkan ausente.
bool WYD_VulkanPresentCreate(WYDVulkanPresent& out, SDL_Window* window,
	uint32_t width, uint32_t height);

void WYD_VulkanPresentDestroy(WYDVulkanPresent& vp);

// clear_rgba: 0xAARRGGBB (igual D3DCOLOR).
bool WYD_VulkanPresentFrame(WYDVulkanPresent& vp, uint32_t clear_argb);

bool WYD_VulkanPresentResize(WYDVulkanPresent& vp, uint32_t width, uint32_t height);
