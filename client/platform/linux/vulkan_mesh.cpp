#include "vulkan_mesh.h"
#include "spirv_mesh_embedded.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace
{
	void Log(const char* w, VkResult r)
	{
		if (r != VK_SUCCESS)
			std::fprintf(stderr, "[WYDLINUX][mesh] %s: %d\n", w, static_cast<int>(r));
	}

	uint32_t FindMem(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags props)
	{
		VkPhysicalDeviceMemoryProperties mp {};
		vkGetPhysicalDeviceMemoryProperties(phys, &mp);
		for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
			if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
				return i;
		return UINT32_MAX;
	}

	bool CreateBuf(VkDevice dev, VkPhysicalDevice phys, VkDeviceSize size,
		VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem)
	{
		VkBufferCreateInfo bi { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bi.size = size;
		bi.usage = usage;
		bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(dev, &bi, nullptr, &buf) != VK_SUCCESS)
			return false;
		VkMemoryRequirements req {};
		vkGetBufferMemoryRequirements(dev, buf, &req);
		VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		ai.allocationSize = req.size;
		ai.memoryTypeIndex = FindMem(phys, req.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS)
			return false;
		vkBindBufferMemory(dev, buf, mem, 0);
		return true;
	}

	void MatIdentity(float m[16])
	{
		std::memset(m, 0, 16 * sizeof(float));
		m[0] = m[5] = m[10] = m[15] = 1.f;
	}

	void MatMul(float o[16], const float a[16], const float b[16])
	{
		float t[16];
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
			{
				t[c * 4 + r] =
					a[0 * 4 + r] * b[c * 4 + 0] +
					a[1 * 4 + r] * b[c * 4 + 1] +
					a[2 * 4 + r] * b[c * 4 + 2] +
					a[3 * 4 + r] * b[c * 4 + 3];
			}
		std::memcpy(o, t, sizeof(t));
	}

	void MatPerspective(float m[16], float fovy, float aspect, float zn, float zf)
	{
		MatIdentity(m);
		const float f = 1.f / std::tan(fovy * 0.5f);
		m[0] = f / aspect;
		m[5] = f;
		m[10] = zf / (zn - zf);
		m[11] = -1.f;
		m[14] = (zf * zn) / (zn - zf);
		m[15] = 0.f;
	}

	void MatLookAt(float m[16], float ex, float ey, float ez, float cx, float cy, float cz)
	{
		float fx = cx - ex, fy = cy - ey, fz = cz - ez;
		float len = std::sqrt(fx * fx + fy * fy + fz * fz);
		fx /= len; fy /= len; fz /= len;
		float ux = 0, uy = 1, uz = 0;
		float sx = fy * uz - fz * uy;
		float sy = fz * ux - fx * uz;
		float sz = fx * uy - fy * ux;
		len = std::sqrt(sx * sx + sy * sy + sz * sz);
		sx /= len; sy /= len; sz /= len;
		ux = sy * fz - sz * fy;
		uy = sz * fx - sx * fz;
		uz = sx * fy - sy * fx;
		MatIdentity(m);
		m[0] = sx; m[4] = sy; m[8] = sz;
		m[1] = ux; m[5] = uy; m[9] = uz;
		m[2] = -fx; m[6] = -fy; m[10] = -fz;
		m[12] = -(sx * ex + sy * ey + sz * ez);
		m[13] = -(ux * ex + uy * ey + uz * ez);
		m[14] = -(-fx * ex - fy * ey - fz * ez);
	}

	void MatRotateY(float m[16], float a)
	{
		MatIdentity(m);
		m[0] = std::cos(a); m[8] = std::sin(a);
		m[2] = -std::sin(a); m[10] = std::cos(a);
	}
}

bool WYD_VulkanMeshCreate(WYDVulkanMesh& out, SDL_Window* window,
	uint32_t width, uint32_t height)
{
	out = {};
	if (!WYD_VulkanPresentCreate(out.present, window, width, height))
		return false;

	auto device = static_cast<VkDevice>(out.present.device);
	const VkFormat format = static_cast<VkFormat>(out.present.format);

	VkAttachmentDescription color {};
	color.format = format;
	color.samples = VK_SAMPLE_COUNT_1_BIT;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	VkAttachmentReference colorRef { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription sub {};
	sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub.colorAttachmentCount = 1;
	sub.pColorAttachments = &colorRef;
	VkSubpassDependency dep {};
	dep.srcSubpass = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass = 0;
	dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	VkRenderPassCreateInfo rpci { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rpci.attachmentCount = 1; rpci.pAttachments = &color;
	rpci.subpassCount = 1; rpci.pSubpasses = &sub;
	rpci.dependencyCount = 1; rpci.pDependencies = &dep;
	VkRenderPass rp = VK_NULL_HANDLE;
	if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
		return false;
	out.render_pass = rp;

	out.framebuffers = static_cast<void**>(std::calloc(out.present.image_count, sizeof(void*)));
	for (uint32_t i = 0; i < out.present.image_count; ++i)
	{
		VkImageView iv = static_cast<VkImageView>(out.present.image_views[i]);
		VkFramebufferCreateInfo fbci { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		fbci.renderPass = rp; fbci.attachmentCount = 1; fbci.pAttachments = &iv;
		fbci.width = out.present.width; fbci.height = out.present.height; fbci.layers = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		if (vkCreateFramebuffer(device, &fbci, nullptr, &fb) != VK_SUCCESS)
			return false;
		out.framebuffers[i] = fb;
	}

	VkPushConstantRange pcr {};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr.offset = 0;
	pcr.size = 80; // mat4 + vec4
	VkPipelineLayoutCreateInfo plci { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pcr;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	if (vkCreatePipelineLayout(device, &plci, nullptr, &layout) != VK_SUCCESS)
		return false;
	out.pipeline_layout = layout;

	VkShaderModuleCreateInfo smci { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
	smci.codeSize = kMeshVertSpvSize; smci.pCode = reinterpret_cast<const uint32_t*>(kMeshVertSpv);
	vkCreateShaderModule(device, &smci, nullptr, &vert);
	smci.codeSize = kMeshFragSpvSize; smci.pCode = reinterpret_cast<const uint32_t*>(kMeshFragSpv);
	vkCreateShaderModule(device, &smci, nullptr, &frag);

	VkPipelineShaderStageCreateInfo stages[2] {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

	VkVertexInputBindingDescription bind { 0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX };
	VkVertexInputAttributeDescription attr { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
	VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
	vi.vertexAttributeDescriptionCount = 1; vi.pVertexAttributeDescriptions = &attr;
	VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkViewport viewport { 0, 0, (float)out.present.width, (float)out.present.height, 0, 1 };
	VkRect2D scissor { {0, 0}, {out.present.width, out.present.height} };
	VkPipelineViewportStateCreateInfo vp { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vp.viewportCount = 1; vp.pViewports = &viewport; vp.scissorCount = 1; vp.pScissors = &scissor;
	VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.f;
	VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState ba {};
	ba.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	cb.attachmentCount = 1; cb.pAttachments = &ba;
	VkGraphicsPipelineCreateInfo pci { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pci.stageCount = 2; pci.pStages = stages;
	pci.pVertexInputState = &vi; pci.pInputAssemblyState = &ia;
	pci.pViewportState = &vp; pci.pRasterizationState = &rs;
	pci.pMultisampleState = &ms; pci.pColorBlendState = &cb;
	pci.layout = layout; pci.renderPass = rp;
	VkPipeline pipe = VK_NULL_HANDLE;
	VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe);
	vkDestroyShaderModule(device, vert, nullptr);
	vkDestroyShaderModule(device, frag, nullptr);
	Log("pipeline", r);
	if (r != VK_SUCCESS)
		return false;
	out.pipeline = pipe;
	out.ready = true;
	std::fprintf(stderr, "[WYDLINUX][mesh] pipeline OK\n");
	return true;
}

bool WYD_VulkanMeshUpload(WYDVulkanMesh& vm, const WYDMsaMesh& mesh)
{
	if (!vm.ready || mesh.positions.empty() || mesh.indices.empty())
		return false;
	auto device = static_cast<VkDevice>(vm.present.device);
	auto physical = static_cast<VkPhysicalDevice>(vm.present.physical);

	if (vm.vertex_buffer)
		vkDestroyBuffer(device, static_cast<VkBuffer>(vm.vertex_buffer), nullptr);
	if (vm.vertex_memory)
		vkFreeMemory(device, static_cast<VkDeviceMemory>(vm.vertex_memory), nullptr);
	if (vm.index_buffer)
		vkDestroyBuffer(device, static_cast<VkBuffer>(vm.index_buffer), nullptr);
	if (vm.index_memory)
		vkFreeMemory(device, static_cast<VkDeviceMemory>(vm.index_memory), nullptr);

	VkBuffer vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
	VkDeviceMemory vm_ = VK_NULL_HANDLE, im = VK_NULL_HANDLE;
	const VkDeviceSize vbytes = mesh.positions.size() * sizeof(float);
	const VkDeviceSize ibytes = mesh.indices.size() * sizeof(uint16_t);
	if (!CreateBuf(device, physical, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vb, vm_))
		return false;
	if (!CreateBuf(device, physical, ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ib, im))
		return false;

	void* mapped = nullptr;
	vkMapMemory(device, vm_, 0, vbytes, 0, &mapped);
	std::memcpy(mapped, mesh.positions.data(), static_cast<size_t>(vbytes));
	vkUnmapMemory(device, vm_);
	vkMapMemory(device, im, 0, ibytes, 0, &mapped);
	std::memcpy(mapped, mesh.indices.data(), static_cast<size_t>(ibytes));
	vkUnmapMemory(device, im);

	vm.vertex_buffer = vb; vm.vertex_memory = vm_;
	vm.index_buffer = ib; vm.index_memory = im;
	vm.index_count = static_cast<uint32_t>(mesh.indices.size());
	vm.vertex_count = mesh.vertex_count;
	vm.center[0] = 0.5f * (mesh.min_x + mesh.max_x);
	vm.center[1] = 0.5f * (mesh.min_y + mesh.max_y);
	vm.center[2] = 0.5f * (mesh.min_z + mesh.max_z);
	const float dx = mesh.max_x - mesh.min_x;
	const float dy = mesh.max_y - mesh.min_y;
	const float dz = mesh.max_z - mesh.min_z;
	vm.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
	if (vm.radius < 0.01f)
		vm.radius = 1.f;
	std::fprintf(stderr, "[WYDLINUX][mesh] upload verts=%u idx=%u radius=%.3f\n",
		vm.vertex_count, vm.index_count, vm.radius);
	return true;
}

bool WYD_VulkanMeshDraw(WYDVulkanMesh& vm, float angleRad,
	float r, float g, float b)
{
	if (!vm.ready || !vm.vertex_buffer)
		return false;
	auto device = static_cast<VkDevice>(vm.present.device);
	auto queue = static_cast<VkQueue>(vm.present.queue);
	auto swapchain = static_cast<VkSwapchainKHR>(vm.present.swapchain);
	auto cmd = static_cast<VkCommandBuffer>(vm.present.command_buffer);
	auto imgAvail = static_cast<VkSemaphore>(vm.present.image_available);
	auto renderFin = static_cast<VkSemaphore>(vm.present.render_finished);
	auto inFlight = static_cast<VkFence>(vm.present.in_flight);

	vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &inFlight);
	uint32_t imageIndex = 0;
	VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imgAvail, VK_NULL_HANDLE, &imageIndex);
	if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
		return false;

	float proj[16], view[16], rot[16], mv[16], mvp[16];
	const float aspect = static_cast<float>(vm.present.width) / static_cast<float>(vm.present.height);
	MatPerspective(proj, 0.8f, aspect, 0.05f, 100.f);
	const float dist = vm.radius * 3.2f;
	MatLookAt(view, vm.center[0], vm.center[1] + vm.radius * 0.4f, vm.center[2] + dist,
		vm.center[0], vm.center[1], vm.center[2]);
	MatRotateY(rot, angleRad);
	// Translate to origin for rotation around center
	float world[16];
	MatIdentity(world);
	world[12] = -vm.center[0]; world[13] = -vm.center[1]; world[14] = -vm.center[2];
	float worldR[16];
	MatMul(worldR, rot, world);
	worldR[12] += vm.center[0]; worldR[13] += vm.center[1]; worldR[14] += vm.center[2];
	MatMul(mv, view, worldR);
	MatMul(mvp, proj, mv);
	float push[20];
	std::memcpy(push, mvp, 64);
	push[16] = r; push[17] = g; push[18] = b; push[19] = 1.f;

	vkResetCommandBuffer(cmd, 0);
	VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	vkBeginCommandBuffer(cmd, &begin);
	VkClearValue clear {};
	clear.color = { { 0.04f, 0.06f, 0.10f, 1.f } };
	VkRenderPassBeginInfo rpbi { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rpbi.renderPass = static_cast<VkRenderPass>(vm.render_pass);
	rpbi.framebuffer = static_cast<VkFramebuffer>(vm.framebuffers[imageIndex]);
	rpbi.renderArea.extent = { vm.present.width, vm.present.height };
	rpbi.clearValueCount = 1; rpbi.pClearValues = &clear;
	vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(vm.pipeline));
	vkCmdPushConstants(cmd, static_cast<VkPipelineLayout>(vm.pipeline_layout),
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, push);
	VkBuffer vb = static_cast<VkBuffer>(vm.vertex_buffer);
	VkDeviceSize off = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);
	vkCmdBindIndexBuffer(cmd, static_cast<VkBuffer>(vm.index_buffer), 0, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(cmd, vm.index_count, 1, 0, 0, 0);
	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &imgAvail;
	submit.pWaitDstStageMask = &waitStage;
	submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
	submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &renderFin;
	if (vkQueueSubmit(queue, 1, &submit, inFlight) != VK_SUCCESS)
		return false;
	VkPresentInfoKHR present { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	present.waitSemaphoreCount = 1; present.pWaitSemaphores = &renderFin;
	present.swapchainCount = 1; present.pSwapchains = &swapchain;
	present.pImageIndices = &imageIndex;
	r = vkQueuePresentKHR(queue, &present);
	return r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
}

void WYD_VulkanMeshDestroy(WYDVulkanMesh& vm)
{
	auto device = static_cast<VkDevice>(vm.present.device);
	if (device)
	{
		vkDeviceWaitIdle(device);
		if (vm.framebuffers)
		{
			for (uint32_t i = 0; i < vm.present.image_count; ++i)
				if (vm.framebuffers[i])
					vkDestroyFramebuffer(device, static_cast<VkFramebuffer>(vm.framebuffers[i]), nullptr);
			std::free(vm.framebuffers);
		}
		if (vm.pipeline) vkDestroyPipeline(device, static_cast<VkPipeline>(vm.pipeline), nullptr);
		if (vm.pipeline_layout) vkDestroyPipelineLayout(device, static_cast<VkPipelineLayout>(vm.pipeline_layout), nullptr);
		if (vm.render_pass) vkDestroyRenderPass(device, static_cast<VkRenderPass>(vm.render_pass), nullptr);
		if (vm.vertex_buffer) vkDestroyBuffer(device, static_cast<VkBuffer>(vm.vertex_buffer), nullptr);
		if (vm.vertex_memory) vkFreeMemory(device, static_cast<VkDeviceMemory>(vm.vertex_memory), nullptr);
		if (vm.index_buffer) vkDestroyBuffer(device, static_cast<VkBuffer>(vm.index_buffer), nullptr);
		if (vm.index_memory) vkFreeMemory(device, static_cast<VkDeviceMemory>(vm.index_memory), nullptr);
	}
	WYD_VulkanPresentDestroy(vm.present);
	vm = {};
}
