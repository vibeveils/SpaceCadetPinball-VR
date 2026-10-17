#include "vk_loader.h"

#include <cstdio>
#include <initializer_list>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vkl
{
#define VR_VK_DEFINE(name) PFN_##name name = nullptr;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
	VR_VK_GLOBAL_FUNCS(VR_VK_DEFINE)
	VR_VK_INSTANCE_FUNCS(VR_VK_DEFINE)
	VR_VK_DEVICE_FUNCS(VR_VK_DEFINE)
#undef VR_VK_DEFINE

	static void* Library = nullptr;

	bool LoadVulkanLibrary()
	{
		if (Library)
			return true;
#ifdef _WIN32
		Library = ::LoadLibraryA("vulkan-1.dll");
		if (Library)
			vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
				GetProcAddress(static_cast<HMODULE>(Library), "vkGetInstanceProcAddr"));
#else
		for (auto name : {"libvulkan.so.1", "libvulkan.so"})
		{
			Library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
			if (Library)
				break;
		}
		if (Library)
			vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
				dlsym(Library, "vkGetInstanceProcAddr"));
#endif
		if (!vkGetInstanceProcAddr)
		{
			printf("VR: Vulkan library not found\n");
			return false;
		}

		bool ok = true;
#define VR_VK_LOAD_GLOBAL(name) \
		name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name)); \
		if (!name) { printf("VR: missing Vulkan function %s\n", #name); ok = false; }
		VR_VK_GLOBAL_FUNCS(VR_VK_LOAD_GLOBAL)
#undef VR_VK_LOAD_GLOBAL
		return ok;
	}

	bool LoadInstance(VkInstance instance)
	{
		bool ok = true;
#define VR_VK_LOAD_INSTANCE(name) \
		name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name)); \
		if (!name) { printf("VR: missing Vulkan function %s\n", #name); ok = false; }
		VR_VK_INSTANCE_FUNCS(VR_VK_LOAD_INSTANCE)
#undef VR_VK_LOAD_INSTANCE
		return ok;
	}

	bool LoadDevice(VkDevice device)
	{
		bool ok = true;
#define VR_VK_LOAD_DEVICE(name) \
		name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)); \
		if (!name) { printf("VR: missing Vulkan function %s\n", #name); ok = false; }
		VR_VK_DEVICE_FUNCS(VR_VK_LOAD_DEVICE)
#undef VR_VK_LOAD_DEVICE
		return ok;
	}

	void UnloadVulkanLibrary()
	{
		if (!Library)
			return;
#ifdef _WIN32
		FreeLibrary(static_cast<HMODULE>(Library));
#else
		dlclose(Library);
#endif
		Library = nullptr;
	}
}
