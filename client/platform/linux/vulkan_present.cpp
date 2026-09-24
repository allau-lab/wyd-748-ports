#include "vulkan_present.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#else
#include <SDL.h>
#include <SDL_vulkan.h>
#endif

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace
{
	void LogVk(const char* where, VkResult r)
	{
		if (r != VK_SUCCESS)
			std::fprintf(stderr, "[WYDLINUX][vk] %s failed: %d\n", where, static_cast<int>(r));
	}

	uint32_t FindGraphicsPresentQueue(VkPhysicalDevice phys, VkSurfaceKHR surface)
	{
		uint32_t count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
		std::vector<VkQueueFamilyProperties> props(count);
		vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, props.data());
		for (uint32_t i = 0; i < count; ++i)
		{
			VkBool32 present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &present);
			if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
				return i;
		}
		return UINT32_MAX;
	}

	bool PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface,
		VkPhysicalDevice& outPhys, uint32_t& outFamily)
	{
		uint32_t count = 0;
		vkEnumeratePhysicalDevices(instance, &count, nullptr);
		if (!count)
			return false;
		std::vector<VkPhysicalDevice> devices(count);
		vkEnumeratePhysicalDevices(instance, &count, devices.data());

		VkPhysicalDevice fallback = VK_NULL_HANDLE;
		uint32_t fallbackFamily = UINT32_MAX;

		for (VkPhysicalDevice d : devices)
		{
			uint32_t family = FindGraphicsPresentQueue(d, surface);
			if (family == UINT32_MAX)
				continue;

			VkPhysicalDeviceProperties props {};
			vkGetPhysicalDeviceProperties(d, &props);
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				outPhys = d;
				outFamily = family;
				std::fprintf(stderr, "[WYDLINUX][vk] GPU: %s (discrete)\n", props.deviceName);
				return true;
			}
			if (fallback == VK_NULL_HANDLE)
			{
				fallback = d;
				fallbackFamily = family;
				std::fprintf(stderr, "[WYDLINUX][vk] GPU candidate: %s\n", props.deviceName);
			}
		}

		if (fallback == VK_NULL_HANDLE)
			return false;
		outPhys = fallback;
		outFamily = fallbackFamily;
		return true;
	}

	bool CreateSwapchainObjects(WYDVulkanPresent& vp)
	{
		auto instance = static_cast<VkInstance>(vp.instance);
		auto surface = static_cast<VkSurfaceKHR>(vp.surface);
		auto physical = static_cast<VkPhysicalDevice>(vp.physical);
		auto device = static_cast<VkDevice>(vp.device);
		(void)instance;

		VkSurfaceCapabilitiesKHR caps {};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data());

		VkSurfaceFormatKHR chosen = formats[0];
		for (const auto& f : formats)
		{
			if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_SRGB) &&
				f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				chosen = f;
				break;
			}
		}
		vp.format = static_cast<int>(chosen.format);

		VkExtent2D extent = caps.currentExtent;
		if (extent.width == UINT32_MAX)
		{
			extent.width = vp.width;
			extent.height = vp.height;
		}
		if (caps.minImageExtent.width > extent.width)
			extent.width = caps.minImageExtent.width;
		if (caps.minImageExtent.height > extent.height)
			extent.height = caps.minImageExtent.height;
		if (caps.maxImageExtent.width < extent.width)
			extent.width = caps.maxImageExtent.width;
		if (caps.maxImageExtent.height < extent.height)
			extent.height = caps.maxImageExtent.height;
		vp.width = extent.width;
		vp.height = extent.height;

		uint32_t imageCount = caps.minImageCount + 1;
		if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
			imageCount = caps.maxImageCount;

		VkSwapchainCreateInfoKHR sci {};
		sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		sci.surface = surface;
		sci.minImageCount = imageCount;
		sci.imageFormat = chosen.format;
		sci.imageColorSpace = chosen.colorSpace;
		sci.imageExtent = extent;
		sci.imageArrayLayers = 1;
		sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		sci.preTransform = caps.currentTransform;
		sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		sci.clipped = VK_TRUE;

		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		VkResult r = vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain);
		LogVk("vkCreateSwapchainKHR", r);
		if (r != VK_SUCCESS)
			return false;
		vp.swapchain = swapchain;

		vkGetSwapchainImagesKHR(device, swapchain, &vp.image_count, nullptr);
		std::vector<VkImage> images(vp.image_count);
		vkGetSwapchainImagesKHR(device, swapchain, &vp.image_count, images.data());

		vp.image_views = static_cast<void**>(std::calloc(vp.image_count, sizeof(void*)));
		for (uint32_t i = 0; i < vp.image_count; ++i)
		{
			VkImageViewCreateInfo vi {};
			vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			vi.image = images[i];
			vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
			vi.format = chosen.format;
			vi.components = {
				VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
			};
			vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			vi.subresourceRange.levelCount = 1;
			vi.subresourceRange.layerCount = 1;
			VkImageView view = VK_NULL_HANDLE;
			r = vkCreateImageView(device, &vi, nullptr, &view);
			LogVk("vkCreateImageView", r);
			if (r != VK_SUCCESS)
				return false;
			vp.image_views[i] = view;
		}
		return true;
	}

	void DestroySwapchainObjects(WYDVulkanPresent& vp)
	{
		auto device = static_cast<VkDevice>(vp.device);
		if (!device)
			return;
		if (vp.image_views)
		{
			for (uint32_t i = 0; i < vp.image_count; ++i)
			{
				if (vp.image_views[i])
					vkDestroyImageView(device, static_cast<VkImageView>(vp.image_views[i]), nullptr);
			}
			std::free(vp.image_views);
			vp.image_views = nullptr;
		}
		if (vp.swapchain)
		{
			vkDestroySwapchainKHR(device, static_cast<VkSwapchainKHR>(vp.swapchain), nullptr);
			vp.swapchain = nullptr;
		}
		vp.image_count = 0;
	}
}

bool WYD_VulkanPresentCreate(WYDVulkanPresent& out, SDL_Window* window,
	uint32_t width, uint32_t height)
{
	out = {};
	out.window = window;
	out.width = width;
	out.height = height;
	out.clear_rgba = 0xFF1A2740;

	if (SDL_Vulkan_LoadLibrary(nullptr) != 0)
	{
		std::fprintf(stderr, "[WYDLINUX][vk] SDL_Vulkan_LoadLibrary: %s\n", SDL_GetError());
		return false;
	}        uint32_t extCount = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(&extCount))
        {
            std::fprintf(stderr, "[WYDLINUX][vk] GetInstanceExtensions: %s\n", SDL_GetError());
            return false;
        }
        std::vector<const char*> exts(extCount);
        auto extsPtr = SDL_Vulkan_GetInstanceExtensions(&extCount);
        if (!extsPtr)
        {
            std::fprintf(stderr, "[WYDLINUX][vk] GetInstanceExtensions(buf): %s\n", SDL_GetError());
            return false;
        }
        std::memcpy(exts.data(), extsPtr, extCount * sizeof(const char*));

	VkApplicationInfo app {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "WYDLINUX";
	app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
	app.pEngineName = "WYD748";
	app.engineVersion = VK_MAKE_VERSION(7, 48, 0);
	app.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo ici {};
	ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ici.pApplicationInfo = &app;
	ici.enabledExtensionCount = extCount;
	ici.ppEnabledExtensionNames = exts.data();

	VkInstance instance = VK_NULL_HANDLE;
	VkResult r = vkCreateInstance(&ici, nullptr, &instance);
	LogVk("vkCreateInstance", r);
	if (r != VK_SUCCESS)
		return false;
	out.instance = instance;

	VkSurfaceKHR surface = VK_NULL_HANDLE;        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
	{
		std::fprintf(stderr, "[WYDLINUX][vk] SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
		WYD_VulkanPresentDestroy(out);
		return false;
	}
	out.surface = surface;

	VkPhysicalDevice physical = VK_NULL_HANDLE;
	uint32_t family = UINT32_MAX;
	if (!PickPhysicalDevice(instance, surface, physical, family))
	{
		std::fprintf(stderr, "[WYDLINUX][vk] nenhum GPU com graphics+present\n");
		WYD_VulkanPresentDestroy(out);
		return false;
	}
	out.physical = physical;
	out.queue_family = family;

	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci {};
	qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qci.queueFamilyIndex = family;
	qci.queueCount = 1;
	qci.pQueuePriorities = &prio;

	const char* deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo dci {};
	dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = 1;
	dci.ppEnabledExtensionNames = deviceExts;

	VkDevice device = VK_NULL_HANDLE;
	r = vkCreateDevice(physical, &dci, nullptr, &device);
	LogVk("vkCreateDevice", r);
	if (r != VK_SUCCESS)
	{
		WYD_VulkanPresentDestroy(out);
		return false;
	}
	out.device = device;

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, family, 0, &queue);
	out.queue = queue;

	if (!CreateSwapchainObjects(out))
	{
		WYD_VulkanPresentDestroy(out);
		return false;
	}

	VkCommandPoolCreateInfo pci {};
	pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pci.queueFamilyIndex = family;
	pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkCommandPool pool = VK_NULL_HANDLE;
	r = vkCreateCommandPool(device, &pci, nullptr, &pool);
	LogVk("vkCreateCommandPool", r);
	if (r != VK_SUCCESS)
	{
		WYD_VulkanPresentDestroy(out);
		return false;
	}
	out.command_pool = pool;

	VkCommandBufferAllocateInfo cai {};
	cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cai.commandPool = pool;
	cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cai.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	r = vkAllocateCommandBuffers(device, &cai, &cmd);
	LogVk("vkAllocateCommandBuffers", r);
	if (r != VK_SUCCESS)
	{
		WYD_VulkanPresentDestroy(out);
		return false;
	}
	out.command_buffer = cmd;

	VkSemaphoreCreateInfo sci {};
	sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fci {};
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphore imgAvail = VK_NULL_HANDLE, renderFin = VK_NULL_HANDLE;
	VkFence inFlight = VK_NULL_HANDLE;
	vkCreateSemaphore(device, &sci, nullptr, &imgAvail);
	vkCreateSemaphore(device, &sci, nullptr, &renderFin);
	vkCreateFence(device, &fci, nullptr, &inFlight);
	out.image_available = imgAvail;
	out.render_finished = renderFin;
	out.in_flight = inFlight;

	std::fprintf(stderr, "[WYDLINUX][vk] Present pronto %ux%u images=%u\n",
		out.width, out.height, out.image_count);
	return true;
}

void WYD_VulkanPresentDestroy(WYDVulkanPresent& vp)
{
	auto device = static_cast<VkDevice>(vp.device);
	auto instance = static_cast<VkInstance>(vp.instance);

	if (device)
	{
		vkDeviceWaitIdle(device);
		if (vp.in_flight)
			vkDestroyFence(device, static_cast<VkFence>(vp.in_flight), nullptr);
		if (vp.image_available)
			vkDestroySemaphore(device, static_cast<VkSemaphore>(vp.image_available), nullptr);
		if (vp.render_finished)
			vkDestroySemaphore(device, static_cast<VkSemaphore>(vp.render_finished), nullptr);
		if (vp.command_pool)
			vkDestroyCommandPool(device, static_cast<VkCommandPool>(vp.command_pool), nullptr);
		DestroySwapchainObjects(vp);
		vkDestroyDevice(device, nullptr);
	}
	if (instance && vp.surface)
		vkDestroySurfaceKHR(instance, static_cast<VkSurfaceKHR>(vp.surface), nullptr);
	if (instance)
		vkDestroyInstance(instance, nullptr);

	vp = {};
}

bool WYD_VulkanPresentResize(WYDVulkanPresent& vp, uint32_t width, uint32_t height)
{
	auto device = static_cast<VkDevice>(vp.device);
	if (!device)
		return false;
	vkDeviceWaitIdle(device);
	DestroySwapchainObjects(vp);
	vp.width = width;
	vp.height = height;
	return CreateSwapchainObjects(vp);
}

bool WYD_VulkanPresentFrame(WYDVulkanPresent& vp, uint32_t clear_argb)
{
	auto device = static_cast<VkDevice>(vp.device);
	auto queue = static_cast<VkQueue>(vp.queue);
	auto swapchain = static_cast<VkSwapchainKHR>(vp.swapchain);
	auto cmd = static_cast<VkCommandBuffer>(vp.command_buffer);
	auto imgAvail = static_cast<VkSemaphore>(vp.image_available);
	auto renderFin = static_cast<VkSemaphore>(vp.render_finished);
	auto inFlight = static_cast<VkFence>(vp.in_flight);

	if (!device || !swapchain)
		return false;

	vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &inFlight);

	uint32_t imageIndex = 0;
	VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imgAvail, VK_NULL_HANDLE, &imageIndex);
	if (r == VK_ERROR_OUT_OF_DATE_KHR)
		return WYD_VulkanPresentResize(vp, vp.width, vp.height);
	if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
	{
		LogVk("vkAcquireNextImageKHR", r);
		return false;
	}

	std::vector<VkImage> images(vp.image_count);
	uint32_t n = vp.image_count;
	vkGetSwapchainImagesKHR(device, swapchain, &n, images.data());

	const float a = ((clear_argb >> 24) & 0xFF) / 255.0f;
	const float red = ((clear_argb >> 16) & 0xFF) / 255.0f;
	const float g = ((clear_argb >> 8) & 0xFF) / 255.0f;
	const float b = (clear_argb & 0xFF) / 255.0f;

	vkResetCommandBuffer(cmd, 0);
	VkCommandBufferBeginInfo begin {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(cmd, &begin);

	VkImageMemoryBarrier toDst {};
	toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.image = images[imageIndex];
	toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toDst.subresourceRange.levelCount = 1;
	toDst.subresourceRange.layerCount = 1;
	toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &toDst);

	VkClearColorValue clear {};
	clear.float32[0] = red;
	clear.float32[1] = g;
	clear.float32[2] = b;
	clear.float32[3] = a;
	VkImageSubresourceRange range {};
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1;
	range.layerCount = 1;
	vkCmdClearColorImage(cmd, images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

	VkImageMemoryBarrier toPresent = toDst;
	toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toPresent.dstAccessMask = 0;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, nullptr, 0, nullptr, 1, &toPresent);

	vkEndCommandBuffer(cmd);

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkSubmitInfo submit {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &imgAvail;
	submit.pWaitDstStageMask = &waitStage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &renderFin;
	r = vkQueueSubmit(queue, 1, &submit, inFlight);
	LogVk("vkQueueSubmit", r);
	if (r != VK_SUCCESS)
		return false;

	VkPresentInfoKHR present {};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &renderFin;
	present.swapchainCount = 1;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &imageIndex;
	r = vkQueuePresentKHR(queue, &present);
	if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
		return WYD_VulkanPresentResize(vp, vp.width, vp.height);
	if (r != VK_SUCCESS)
	{
		LogVk("vkQueuePresentKHR", r);
		return false;
	}
	return true;
}
