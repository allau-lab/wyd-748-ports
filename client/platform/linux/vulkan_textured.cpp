#include "vulkan_textured.h"
#include "spirv_quad_embedded.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace
{
	void Log(const char* w, VkResult r)
	{
		if (r != VK_SUCCESS)
			std::fprintf(stderr, "[WYDLINUX][tex] %s: %d\n", w, static_cast<int>(r));
	}

	uint32_t FindMemoryType(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags props)
	{
		VkPhysicalDeviceMemoryProperties mp {};
		vkGetPhysicalDeviceMemoryProperties(phys, &mp);
		for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
		{
			if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
				return i;
		}
		return UINT32_MAX;
	}

	bool CreateBuffer(VkDevice device, VkPhysicalDevice phys, VkDeviceSize size,
		VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps,
		VkBuffer& buffer, VkDeviceMemory& memory)
	{
		VkBufferCreateInfo bi {};
		bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bi.size = size;
		bi.usage = usage;
		bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkResult r = vkCreateBuffer(device, &bi, nullptr, &buffer);
		Log("vkCreateBuffer", r);
		if (r != VK_SUCCESS)
			return false;
		VkMemoryRequirements req {};
		vkGetBufferMemoryRequirements(device, buffer, &req);
		VkMemoryAllocateInfo ai {};
		ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		ai.allocationSize = req.size;
		ai.memoryTypeIndex = FindMemoryType(phys, req.memoryTypeBits, memProps);
		r = vkAllocateMemory(device, &ai, nullptr, &memory);
		Log("vkAllocateMemory(buf)", r);
		if (r != VK_SUCCESS)
			return false;
		vkBindBufferMemory(device, buffer, memory, 0);
		return true;
	}

	VkShaderModule MakeShader(VkDevice device, const unsigned char* code, unsigned size)
	{
		VkShaderModuleCreateInfo ci {};
		ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		ci.codeSize = size;
		ci.pCode = reinterpret_cast<const uint32_t*>(code);
		VkShaderModule mod = VK_NULL_HANDLE;
		Log("vkCreateShaderModule", vkCreateShaderModule(device, &ci, nullptr, &mod));
		return mod;
	}
}

bool WYD_VulkanTexturedCreate(WYDVulkanTextured& out, SDL_Window* window,
	uint32_t width, uint32_t height)
{
	out = {};
	if (!WYD_VulkanPresentCreate(out.present, window, width, height))
		return false;

	auto device = static_cast<VkDevice>(out.present.device);
	auto physical = static_cast<VkPhysicalDevice>(out.present.physical);
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

	VkAttachmentReference colorRef {};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	VkSubpassDependency dep {};
	dep.srcSubpass = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass = 0;
	dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo rpci {};
	rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpci.attachmentCount = 1;
	rpci.pAttachments = &color;
	rpci.subpassCount = 1;
	rpci.pSubpasses = &subpass;
	rpci.dependencyCount = 1;
	rpci.pDependencies = &dep;
	VkRenderPass rp = VK_NULL_HANDLE;
	VkResult r = vkCreateRenderPass(device, &rpci, nullptr, &rp);
	Log("vkCreateRenderPass", r);
	if (r != VK_SUCCESS)
		return false;
	out.render_pass = rp;

	out.framebuffers = static_cast<void**>(std::calloc(out.present.image_count, sizeof(void*)));
	for (uint32_t i = 0; i < out.present.image_count; ++i)
	{
		VkImageView iv = static_cast<VkImageView>(out.present.image_views[i]);
		VkFramebufferCreateInfo fbci {};
		fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbci.renderPass = rp;
		fbci.attachmentCount = 1;
		fbci.pAttachments = &iv;
		fbci.width = out.present.width;
		fbci.height = out.present.height;
		fbci.layers = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		r = vkCreateFramebuffer(device, &fbci, nullptr, &fb);
		Log("vkCreateFramebuffer", r);
		if (r != VK_SUCCESS)
			return false;
		out.framebuffers[i] = fb;
	}

	VkDescriptorSetLayoutBinding binding {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo dslci {};
	dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = 1;
	dslci.pBindings = &binding;
	VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
	r = vkCreateDescriptorSetLayout(device, &dslci, nullptr, &dsl);
	Log("vkCreateDescriptorSetLayout", r);
	if (r != VK_SUCCESS)
		return false;
	out.descriptor_set_layout = dsl;

	VkPipelineLayoutCreateInfo plci {};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &dsl;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	r = vkCreatePipelineLayout(device, &plci, nullptr, &layout);
	Log("vkCreatePipelineLayout", r);
	if (r != VK_SUCCESS)
		return false;
	out.pipeline_layout = layout;

	VkShaderModule vert = MakeShader(device, kQuadVertSpv, kQuadVertSpvSize);
	VkShaderModule frag = MakeShader(device, kQuadFragSpv, kQuadFragSpvSize);
	if (!vert || !frag)
		return false;

	VkPipelineShaderStageCreateInfo stages[2] {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";

	VkVertexInputBindingDescription bind {};
	bind.binding = 0;
	bind.stride = sizeof(float) * 4;
	bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	VkVertexInputAttributeDescription attrs[2] {};
	attrs[0].location = 0;
	attrs[0].binding = 0;
	attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[0].offset = 0;
	attrs[1].location = 1;
	attrs[1].binding = 0;
	attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[1].offset = sizeof(float) * 2;

	VkPipelineVertexInputStateCreateInfo vi {};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 1;
	vi.pVertexBindingDescriptions = &bind;
	vi.vertexAttributeDescriptionCount = 2;
	vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia {};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	VkViewport viewport {};
	viewport.width = static_cast<float>(out.present.width);
	viewport.height = static_cast<float>(out.present.height);
	viewport.maxDepth = 1.0f;
	VkRect2D scissor {};
	scissor.extent = { out.present.width, out.present.height };

	VkPipelineViewportStateCreateInfo vp {};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.pViewports = &viewport;
	vp.scissorCount = 1;
	vp.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rs {};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms {};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAtt {};
	blendAtt.colorWriteMask = 0xF;
	blendAtt.blendEnable = VK_TRUE;
	blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
	blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo cb {};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1;
	cb.pAttachments = &blendAtt;

	VkGraphicsPipelineCreateInfo pci {};
	pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pci.stageCount = 2;
	pci.pStages = stages;
	pci.pVertexInputState = &vi;
	pci.pInputAssemblyState = &ia;
	pci.pViewportState = &vp;
	pci.pRasterizationState = &rs;
	pci.pMultisampleState = &ms;
	pci.pColorBlendState = &cb;
	pci.layout = layout;
	pci.renderPass = rp;

	VkPipeline pipeline = VK_NULL_HANDLE;
	r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
	Log("vkCreateGraphicsPipelines", r);
	vkDestroyShaderModule(device, vert, nullptr);
	vkDestroyShaderModule(device, frag, nullptr);
	if (r != VK_SUCCESS)
		return false;
	out.pipeline = pipeline;

	// Fullscreen quad: pos.xy + uv.xy
	const float verts[] = {
		-1.f, -1.f, 0.f, 1.f,
		 1.f, -1.f, 1.f, 1.f,
		-1.f,  1.f, 0.f, 0.f,
		 1.f,  1.f, 1.f, 0.f,
	};
	VkBuffer vbo = VK_NULL_HANDLE;
	VkDeviceMemory vmem = VK_NULL_HANDLE;
	if (!CreateBuffer(device, physical, sizeof(verts),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vbo, vmem))
		return false;
	void* mapped = nullptr;
	vkMapMemory(device, vmem, 0, sizeof(verts), 0, &mapped);
	std::memcpy(mapped, verts, sizeof(verts));
	vkUnmapMemory(device, vmem);
	out.vertex_buffer = vbo;
	out.vertex_memory = vmem;

	VkSamplerCreateInfo sci {};
	sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_LINEAR;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VkSampler sampler = VK_NULL_HANDLE;
	r = vkCreateSampler(device, &sci, nullptr, &sampler);
	Log("vkCreateSampler", r);
	if (r != VK_SUCCESS)
		return false;
	out.sampler = sampler;

	VkDescriptorPoolSize poolSize {};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = 1;
	VkDescriptorPoolCreateInfo dpci {};
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.maxSets = 1;
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes = &poolSize;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	r = vkCreateDescriptorPool(device, &dpci, nullptr, &pool);
	Log("vkCreateDescriptorPool", r);
	if (r != VK_SUCCESS)
		return false;
	out.descriptor_pool = pool;

	VkDescriptorSetAllocateInfo dsai {};
	dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool = pool;
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts = &dsl;
	VkDescriptorSet set = VK_NULL_HANDLE;
	r = vkAllocateDescriptorSets(device, &dsai, &set);
	Log("vkAllocateDescriptorSets", r);
	if (r != VK_SUCCESS)
		return false;
	out.descriptor_set = set;

	out.ready = true;
	std::fprintf(stderr, "[WYDLINUX][tex] pipeline texturizado pronto\n");
	return true;
}

bool WYD_VulkanTexturedUploadRgba(WYDVulkanTextured& vt, const WYDWytImage& image)
{
	if (!vt.ready || image.rgba.empty())
		return false;

	auto device = static_cast<VkDevice>(vt.present.device);
	auto physical = static_cast<VkPhysicalDevice>(vt.present.physical);
	auto queue = static_cast<VkQueue>(vt.present.queue);
	auto cmd = static_cast<VkCommandBuffer>(vt.present.command_buffer);

	if (vt.texture_view)
		vkDestroyImageView(device, static_cast<VkImageView>(vt.texture_view), nullptr);
	if (vt.texture_image)
		vkDestroyImage(device, static_cast<VkImage>(vt.texture_image), nullptr);
	if (vt.texture_memory)
		vkFreeMemory(device, static_cast<VkDeviceMemory>(vt.texture_memory), nullptr);
	vt.texture_view = vt.texture_image = vt.texture_memory = nullptr;

	VkImageCreateInfo ici {};
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = VK_FORMAT_R8G8B8A8_UNORM;
	ici.extent = { image.width, image.height, 1 };
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_LINEAR;
	ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	ici.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	VkImage imageVk = VK_NULL_HANDLE;
	VkResult r = vkCreateImage(device, &ici, nullptr, &imageVk);
	Log("vkCreateImage", r);
	if (r != VK_SUCCESS)
		return false;

	VkMemoryRequirements req {};
	vkGetImageMemoryRequirements(device, imageVk, &req);
	VkMemoryAllocateInfo ai {};
	ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = FindMemoryType(physical, req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory mem = VK_NULL_HANDLE;
	r = vkAllocateMemory(device, &ai, nullptr, &mem);
	Log("vkAllocateMemory(img)", r);
	if (r != VK_SUCCESS)
		return false;
	vkBindImageMemory(device, imageVk, mem, 0);

	VkImageSubresource sub {};
	sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	VkSubresourceLayout layout {};
	vkGetImageSubresourceLayout(device, imageVk, &sub, &layout);

	void* mapped = nullptr;
	vkMapMemory(device, mem, 0, req.size, 0, &mapped);
	for (uint32_t y = 0; y < image.height; ++y)
	{
		std::memcpy(
			static_cast<uint8_t*>(mapped) + layout.offset + y * layout.rowPitch,
			image.rgba.data() + static_cast<size_t>(y) * image.width * 4u,
			static_cast<size_t>(image.width) * 4u);
	}
	vkUnmapMemory(device, mem);

	VkImageViewCreateInfo vci {};
	vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vci.image = imageVk;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = VK_FORMAT_R8G8B8A8_UNORM;
	vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vci.subresourceRange.levelCount = 1;
	vci.subresourceRange.layerCount = 1;
	VkImageView view = VK_NULL_HANDLE;
	r = vkCreateImageView(device, &vci, nullptr, &view);
	Log("vkCreateImageView(tex)", r);
	if (r != VK_SUCCESS)
		return false;

	// Transition to shader read
	vkResetCommandBuffer(cmd, 0);
	VkCommandBufferBeginInfo begin {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &begin);
	VkImageMemoryBarrier barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = imageVk;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);
	vkEndCommandBuffer(cmd);
	VkSubmitInfo submit {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);

	vt.texture_image = imageVk;
	vt.texture_memory = mem;
	vt.texture_view = view;

	VkDescriptorImageInfo dii {};
	dii.sampler = static_cast<VkSampler>(vt.sampler);
	dii.imageView = view;
	dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet write {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = static_cast<VkDescriptorSet>(vt.descriptor_set);
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &dii;
	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

	std::fprintf(stderr, "[WYDLINUX][tex] upload %ux%u OK\n", image.width, image.height);
	return true;
}

bool WYD_VulkanTexturedDraw(WYDVulkanTextured& vt)
{
	if (!vt.ready || !vt.texture_view)
		return false;

	auto device = static_cast<VkDevice>(vt.present.device);
	auto queue = static_cast<VkQueue>(vt.present.queue);
	auto swapchain = static_cast<VkSwapchainKHR>(vt.present.swapchain);
	auto cmd = static_cast<VkCommandBuffer>(vt.present.command_buffer);
	auto imgAvail = static_cast<VkSemaphore>(vt.present.image_available);
	auto renderFin = static_cast<VkSemaphore>(vt.present.render_finished);
	auto inFlight = static_cast<VkFence>(vt.present.in_flight);

	vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &inFlight);

	uint32_t imageIndex = 0;
	VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imgAvail, VK_NULL_HANDLE, &imageIndex);
	if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
	{
		Log("vkAcquireNextImageKHR", r);
		return false;
	}

	vkResetCommandBuffer(cmd, 0);
	VkCommandBufferBeginInfo begin {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(cmd, &begin);

	VkClearValue clear {};
	clear.color = { { 0.05f, 0.08f, 0.12f, 1.0f } };
	VkRenderPassBeginInfo rpbi {};
	rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpbi.renderPass = static_cast<VkRenderPass>(vt.render_pass);
	rpbi.framebuffer = static_cast<VkFramebuffer>(vt.framebuffers[imageIndex]);
	rpbi.renderArea.extent = { vt.present.width, vt.present.height };
	rpbi.clearValueCount = 1;
	rpbi.pClearValues = &clear;
	vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(vt.pipeline));
	VkDescriptorSet set = static_cast<VkDescriptorSet>(vt.descriptor_set);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		static_cast<VkPipelineLayout>(vt.pipeline_layout), 0, 1, &set, 0, nullptr);
	VkBuffer vbo = static_cast<VkBuffer>(vt.vertex_buffer);
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);
	vkCmdDraw(cmd, 4, 1, 0, 0);
	vkCmdEndRenderPass(cmd);
	vkEndCommandBuffer(cmd);

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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
	Log("vkQueueSubmit", r);
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
	if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
	{
		Log("vkQueuePresentKHR", r);
		return false;
	}
	return true;
}

void WYD_VulkanTexturedDestroy(WYDVulkanTextured& vt)
{
	auto device = static_cast<VkDevice>(vt.present.device);
	if (device)
	{
		vkDeviceWaitIdle(device);
		if (vt.framebuffers)
		{
			for (uint32_t i = 0; i < vt.present.image_count; ++i)
			{
				if (vt.framebuffers[i])
					vkDestroyFramebuffer(device, static_cast<VkFramebuffer>(vt.framebuffers[i]), nullptr);
			}
			std::free(vt.framebuffers);
		}
		if (vt.pipeline)
			vkDestroyPipeline(device, static_cast<VkPipeline>(vt.pipeline), nullptr);
		if (vt.pipeline_layout)
			vkDestroyPipelineLayout(device, static_cast<VkPipelineLayout>(vt.pipeline_layout), nullptr);
		if (vt.render_pass)
			vkDestroyRenderPass(device, static_cast<VkRenderPass>(vt.render_pass), nullptr);
		if (vt.descriptor_pool)
			vkDestroyDescriptorPool(device, static_cast<VkDescriptorPool>(vt.descriptor_pool), nullptr);
		if (vt.descriptor_set_layout)
			vkDestroyDescriptorSetLayout(device, static_cast<VkDescriptorSetLayout>(vt.descriptor_set_layout), nullptr);
		if (vt.sampler)
			vkDestroySampler(device, static_cast<VkSampler>(vt.sampler), nullptr);
		if (vt.texture_view)
			vkDestroyImageView(device, static_cast<VkImageView>(vt.texture_view), nullptr);
		if (vt.texture_image)
			vkDestroyImage(device, static_cast<VkImage>(vt.texture_image), nullptr);
		if (vt.texture_memory)
			vkFreeMemory(device, static_cast<VkDeviceMemory>(vt.texture_memory), nullptr);
		if (vt.vertex_buffer)
			vkDestroyBuffer(device, static_cast<VkBuffer>(vt.vertex_buffer), nullptr);
		if (vt.vertex_memory)
			vkFreeMemory(device, static_cast<VkDeviceMemory>(vt.vertex_memory), nullptr);
	}
	WYD_VulkanPresentDestroy(vt.present);
	vt = {};
}
