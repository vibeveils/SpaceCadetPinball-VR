#pragma once
// Runtime Vulkan loader: the game binary does not link against libvulkan, so the same
// executable still runs on machines without Vulkan (flat mode), and cross-compiling for
// the Steam Frame (Linux ARM64) only needs the Vulkan headers.

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define VR_VK_GLOBAL_FUNCS(X) \
	X(vkCreateInstance) \
	X(vkEnumerateInstanceExtensionProperties)

#define VR_VK_INSTANCE_FUNCS(X) \
	X(vkDestroyInstance) \
	X(vkGetDeviceProcAddr) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceFeatures) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceFormatProperties) \
	X(vkCreateDevice) \
	X(vkEnumerateDeviceExtensionProperties)

#define VR_VK_DEVICE_FUNCS(X) \
	X(vkDestroyDevice) \
	X(vkGetDeviceQueue) \
	X(vkQueueSubmit) \
	X(vkQueueWaitIdle) \
	X(vkDeviceWaitIdle) \
	X(vkCreateCommandPool) \
	X(vkDestroyCommandPool) \
	X(vkAllocateCommandBuffers) \
	X(vkBeginCommandBuffer) \
	X(vkEndCommandBuffer) \
	X(vkResetCommandBuffer) \
	X(vkCreateFence) \
	X(vkDestroyFence) \
	X(vkWaitForFences) \
	X(vkResetFences) \
	X(vkCreateImage) \
	X(vkDestroyImage) \
	X(vkGetImageMemoryRequirements) \
	X(vkAllocateMemory) \
	X(vkFreeMemory) \
	X(vkBindImageMemory) \
	X(vkCreateImageView) \
	X(vkDestroyImageView) \
	X(vkCreateBuffer) \
	X(vkDestroyBuffer) \
	X(vkGetBufferMemoryRequirements) \
	X(vkBindBufferMemory) \
	X(vkMapMemory) \
	X(vkUnmapMemory) \
	X(vkFlushMappedMemoryRanges) \
	X(vkCreateRenderPass) \
	X(vkDestroyRenderPass) \
	X(vkCreateFramebuffer) \
	X(vkDestroyFramebuffer) \
	X(vkCreateShaderModule) \
	X(vkDestroyShaderModule) \
	X(vkCreatePipelineLayout) \
	X(vkDestroyPipelineLayout) \
	X(vkCreateGraphicsPipelines) \
	X(vkDestroyPipeline) \
	X(vkCreateDescriptorSetLayout) \
	X(vkDestroyDescriptorSetLayout) \
	X(vkCreateDescriptorPool) \
	X(vkDestroyDescriptorPool) \
	X(vkAllocateDescriptorSets) \
	X(vkUpdateDescriptorSets) \
	X(vkCreateSampler) \
	X(vkDestroySampler) \
	X(vkCmdBeginRenderPass) \
	X(vkCmdEndRenderPass) \
	X(vkCmdBindPipeline) \
	X(vkCmdBindVertexBuffers) \
	X(vkCmdBindDescriptorSets) \
	X(vkCmdPushConstants) \
	X(vkCmdDraw) \
	X(vkCmdSetViewport) \
	X(vkCmdSetScissor) \
	X(vkCmdPipelineBarrier) \
	X(vkCmdCopyBufferToImage) \
	X(vkCmdCopyImageToBuffer) \
	X(vkCmdBlitImage)

namespace vkl
{
#define VR_VK_DECLARE(name) extern PFN_##name name;
	extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
	VR_VK_GLOBAL_FUNCS(VR_VK_DECLARE)
	VR_VK_INSTANCE_FUNCS(VR_VK_DECLARE)
	VR_VK_DEVICE_FUNCS(VR_VK_DECLARE)
#undef VR_VK_DECLARE

	bool LoadVulkanLibrary();
	bool LoadInstance(VkInstance instance);
	bool LoadDevice(VkDevice device);
	void UnloadVulkanLibrary();
}
