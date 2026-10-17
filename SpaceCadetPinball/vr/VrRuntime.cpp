#include "VrRuntime.h"

#include "vk_loader.h"
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "VrScene.h"
#include "vr_math.h"
#include "vr_shaders.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace vrm;

// Steam Frame controller profile (see Steamworks "Steam Frame Input" documentation).
#ifndef XR_VALVE_FRAME_CONTROLLER_INTERACTION_EXTENSION_NAME
#define XR_VALVE_FRAME_CONTROLLER_INTERACTION_EXTENSION_NAME "XR_VALVE_frame_controller_interaction"
#endif

namespace vr
{
	namespace
	{
		constexpr int FramesInFlight = 2;
		constexpr float NearZ = 0.03f;
		constexpr float FarZ = 400.0f;

		struct PushConstants
		{
			float ViewProj[16];
			float EyePos[4];
			float Params[4];
			float UvRect[4];
			float Glow[4];
		};
		static_assert(sizeof(PushConstants) == 128, "push constants must fit the guaranteed 128 bytes");

		struct EyeSwapchain
		{
			XrSwapchain Handle = XR_NULL_HANDLE;
			int32_t Width = 0, Height = 0;
			std::vector<XrSwapchainImageVulkan2KHR> Images;
			std::vector<VkImageView> Views;
			std::vector<VkFramebuffer> Framebuffers;
			VkImage Msaa = VK_NULL_HANDLE, Depth = VK_NULL_HANDLE;
			VkDeviceMemory MsaaMem = VK_NULL_HANDLE, DepthMem = VK_NULL_HANDLE;
			VkImageView MsaaView = VK_NULL_HANDLE, DepthView = VK_NULL_HANDLE;
		};

		struct FrameResources
		{
			VkCommandBuffer Cmd = VK_NULL_HANDLE;
			VkFence Fence = VK_NULL_HANDLE;
			VkBuffer Staging = VK_NULL_HANDLE;
			VkDeviceMemory StagingMem = VK_NULL_HANDLE;
			void* StagingPtr = nullptr;
			VkDeviceSize StagingSize = 0;
			VkImage Tex = VK_NULL_HANDLE;
			VkDeviceMemory TexMem = VK_NULL_HANDLE;
			VkImageView TexView = VK_NULL_HANDLE;
			int TexW = 0, TexH = 0;
			uint32_t Mips = 1;
			bool TexReady = false;
			VkDescriptorSet Set = VK_NULL_HANDLE;
			VkBuffer Vbuf = VK_NULL_HANDLE;
			VkDeviceMemory VbufMem = VK_NULL_HANDLE;
			void* VbufPtr = nullptr;
			VkDeviceSize VbufSize = 0;
			// Debug screenshot of the left eye
			VkBuffer Shot = VK_NULL_HANDLE;
			VkDeviceMemory ShotMem = VK_NULL_HANDLE;
			void* ShotPtr = nullptr;
			bool ShotPending = false;
			int ShotW = 0, ShotH = 0;
		};

		struct Panel
		{
			Vec3 Center, Right, Up;
			float HalfW = 0, HalfH = 0;
			bool Visible = false;
		};

		struct State
		{
			bool Active = false;
			std::string Error;
			Settings Cfg;

			// OpenXR
			XrInstance Instance = XR_NULL_HANDLE;
			XrSystemId System = XR_NULL_SYSTEM_ID;
			XrSession Session = XR_NULL_HANDLE;
			XrSpace AppSpace = XR_NULL_HANDLE;
			XrSpace ViewSpace = XR_NULL_HANDLE;
			bool StageSpace = false;
			XrSessionState SessionState = XR_SESSION_STATE_UNKNOWN;
			bool Running = false;
			bool Focused = false;
			bool FrameControllerExt = false;
			XrFrameState FrameState{XR_TYPE_FRAME_STATE};
			bool FrameBegun = false;
			std::string ProfileName = "none";

			PFN_xrGetVulkanGraphicsRequirements2KHR GetVkRequirements = nullptr;
			PFN_xrCreateVulkanInstanceKHR CreateVkInstance = nullptr;
			PFN_xrGetVulkanGraphicsDevice2KHR GetVkDevice = nullptr;
			PFN_xrCreateVulkanDeviceKHR CreateVkDevice = nullptr;

			// Vulkan
			VkInstance VkInst = VK_NULL_HANDLE;
			VkPhysicalDevice Gpu = VK_NULL_HANDLE;
			VkDevice Device = VK_NULL_HANDLE;
			uint32_t QueueFamily = 0;
			VkQueue Queue = VK_NULL_HANDLE;
			VkPhysicalDeviceMemoryProperties MemProps{};
			VkCommandPool CmdPool = VK_NULL_HANDLE;
			VkRenderPass RenderPass = VK_NULL_HANDLE;
			VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
			VkDescriptorPool DescPool = VK_NULL_HANDLE;
			VkPipelineLayout PipeLayout = VK_NULL_HANDLE;
			VkPipeline PipeOpaque = VK_NULL_HANDLE, PipeBlend = VK_NULL_HANDLE, PipeOverlay = VK_NULL_HANDLE;
			VkSampler Sampler = VK_NULL_HANDLE;
			VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
			VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
			VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
			bool CanBlitMips = false;
			bool Anisotropy = false;
			float MaxAnisotropy = 1.0f;

			EyeSwapchain Eyes[2];
			FrameResources Frames[FramesInFlight];
			uint32_t FrameIndex = 0;
			uint64_t FrameCounter = 0;

			// Input
			XrActionSet ActionSet = XR_NULL_HANDLE;
			XrAction FlipL = XR_NULL_HANDLE, FlipR = XR_NULL_HANDLE, Plunger = XR_NULL_HANDLE;
			XrAction NudgeL = XR_NULL_HANDLE, NudgeR = XR_NULL_HANDLE, NudgeUp = XR_NULL_HANDLE;
			XrAction Move = XR_NULL_HANDLE, Look = XR_NULL_HANDLE;
			XrAction Pause = XR_NULL_HANDLE, Menu = XR_NULL_HANDLE, NewGame = XR_NULL_HANDLE;
			XrAction ViewToggle = XR_NULL_HANDLE, Recenter = XR_NULL_HANDLE, Back = XR_NULL_HANDLE;
			XrAction Grab = XR_NULL_HANDLE;
			XrAction AimPose = XR_NULL_HANDLE, GripPose = XR_NULL_HANDLE, HapticOut = XR_NULL_HANDLE;
			XrPath HandPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
			XrSpace AimSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
			XrSpace GripSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

			// Tracking state
			Pose Head;
			bool HeadValid = false;
			Pose Aim[2], Grip[2];
			bool AimValid[2] = {false, false}, GripValid[2] = {false, false};
			int PointerHand = 1;
			bool StickNudgeL = false, StickNudgeR = false, StickNudgeUp = false;
			float NewGameHeld = 0;
			bool NewGameFired = false;
			std::chrono::steady_clock::time_point LastInputTime{};

			// World placement
			bool NeedRecenter = true;
			Vec3 AnchorPos{0, 1.6f, 0};
			float AnchorYaw = 0;
			float FloorY = 0;
			Vec3 Shake{}, ShakeVel{};

			// Grabbable cabinet controls, in world space (updated each frame when the cabinet is built).
			bool ControlsValid = false;
			Vec3 FlipButton[2], FlipButtonOut[2];  // button centre, outward direction
			Vec3 PlungerRest, PlungerAxis;          // knob at rest, pull direction (towards the player)
			float PlungerTravel = 0.07f;
			bool GrabHeld[2] = {false, false};
			int HandFlip[2] = {-1, -1};          // which flipper button each hand holds
			bool FlipPressed[2] = {false, false}; // for the button animation
			int PlungerHand = -1;
			float PlungerGrabOffset = 0;
			float PlungerPull = 0;
			float PlungerVisual = 0;               // knob position, springs back after release

			// Two-handed table move while paused.
			bool Moving = false;
			Vec3 MoveStartMid, MoveStartAnchor;
			float MoveStartAngle = 0, MoveStartYaw = 0, MoveStartDist = 1, MoveStartWidth = 0;

			int LastFrameW = 1200, LastFrameH = 880;
			float LastGameRect[4] = {0, 0.027f, 1, 0.973f};
			// The frame texture is two frames tall: top = frame with menus, bottom = game only (when sent).
			bool FrameDual = false;
			Panel UiPanel;
			bool PointerHit = false;
			Vec3 PointerHitPos{};

			SceneBuilder Scene;
			SceneBuilder EnvCache;
			Vec3 EnvAnchor{1e9f, 0, 0};
			float EnvYaw = 0, EnvFloor = 0;
			Vec3 GlowCenter{};
			float GlowStrength = 1.0f;

			// Debug
			std::string ShotPath;
			uint64_t ShotFrame = 0;
		};

		State S;

		void SetError(const std::string& msg)
		{
			S.Error = msg;
			printf("VR: %s\n", msg.c_str());
		}

		bool XrOk(XrResult r, const char* what)
		{
			if (XR_SUCCEEDED(r))
				return true;
			char buf[XR_MAX_RESULT_STRING_SIZE] = "";
			if (S.Instance)
				xrResultToString(S.Instance, r, buf);
			else
				snprintf(buf, sizeof buf, "%d", static_cast<int>(r));
			SetError(std::string(what) + " failed: " + buf);
			return false;
		}

		bool VkOk(VkResult r, const char* what)
		{
			if (r == VK_SUCCESS)
				return true;
			SetError(std::string(what) + " failed: VkResult " + std::to_string(static_cast<int>(r)));
			return false;
		}

#define XR_TRY(x) do { if (!XrOk((x), #x)) return false; } while (0)
#define VK_TRY(x) do { if (!VkOk((x), #x)) return false; } while (0)

		Pose ToPose(const XrPosef& p)
		{
			Pose r;
			r.orientation = Quat{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w};
			r.position = Vec3{p.position.x, p.position.y, p.position.z};
			return r;
		}

		XrPath Path(const char* str)
		{
			XrPath p = XR_NULL_PATH;
			xrStringToPath(S.Instance, str, &p);
			return p;
		}

		// ---------------------------------------------------------------- Vulkan helpers

		int FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props)
		{
			for (uint32_t i = 0; i < S.MemProps.memoryTypeCount; ++i)
				if ((typeBits & (1u << i)) && (S.MemProps.memoryTypes[i].propertyFlags & props) == props)
					return static_cast<int>(i);
			return -1;
		}

		bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem,
		                  void** mapped)
		{
			VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			bi.size = size;
			bi.usage = usage;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_TRY(vkl::vkCreateBuffer(S.Device, &bi, nullptr, &buf));
			VkMemoryRequirements req;
			vkl::vkGetBufferMemoryRequirements(S.Device, buf, &req);
			int type = FindMemoryType(req.memoryTypeBits,
			                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			if (type < 0)
			{
				SetError("no host visible memory");
				return false;
			}
			VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
			ai.allocationSize = req.size;
			ai.memoryTypeIndex = static_cast<uint32_t>(type);
			VK_TRY(vkl::vkAllocateMemory(S.Device, &ai, nullptr, &mem));
			VK_TRY(vkl::vkBindBufferMemory(S.Device, buf, mem, 0));
			if (mapped)
				VK_TRY(vkl::vkMapMemory(S.Device, mem, 0, VK_WHOLE_SIZE, 0, mapped));
			return true;
		}

		void DestroyBuffer(VkBuffer& buf, VkDeviceMemory& mem)
		{
			if (buf)
				vkl::vkDestroyBuffer(S.Device, buf, nullptr);
			if (mem)
				vkl::vkFreeMemory(S.Device, mem, nullptr);
			buf = VK_NULL_HANDLE;
			mem = VK_NULL_HANDLE;
		}

		bool CreateImage(uint32_t w, uint32_t h, uint32_t mips, VkFormat fmt, VkSampleCountFlagBits samples,
		                 VkImageUsageFlags usage, bool transient, VkImage& img, VkDeviceMemory& mem)
		{
			VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			ci.imageType = VK_IMAGE_TYPE_2D;
			ci.format = fmt;
			ci.extent = {w, h, 1};
			ci.mipLevels = mips;
			ci.arrayLayers = 1;
			ci.samples = samples;
			ci.tiling = VK_IMAGE_TILING_OPTIMAL;
			ci.usage = usage;
			ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VK_TRY(vkl::vkCreateImage(S.Device, &ci, nullptr, &img));
			VkMemoryRequirements req;
			vkl::vkGetImageMemoryRequirements(S.Device, img, &req);
			int type = -1;
			if (transient)
				type = FindMemoryType(req.memoryTypeBits,
				                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
			if (type < 0)
				type = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (type < 0)
				type = FindMemoryType(req.memoryTypeBits, 0);
			VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
			ai.allocationSize = req.size;
			ai.memoryTypeIndex = static_cast<uint32_t>(type);
			VK_TRY(vkl::vkAllocateMemory(S.Device, &ai, nullptr, &mem));
			VK_TRY(vkl::vkBindImageMemory(S.Device, img, mem, 0));
			return true;
		}

		bool CreateView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect, uint32_t mips, VkImageView& view)
		{
			VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
			vi.image = img;
			vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
			vi.format = fmt;
			vi.subresourceRange = {aspect, 0, mips, 0, 1};
			VK_TRY(vkl::vkCreateImageView(S.Device, &vi, nullptr, &view));
			return true;
		}

		void Barrier(VkCommandBuffer cmd, VkImage img, uint32_t baseMip, uint32_t mipCount,
		             VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
		             VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
		             VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
		{
			VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			b.oldLayout = from;
			b.newLayout = to;
			b.srcAccessMask = srcAccess;
			b.dstAccessMask = dstAccess;
			b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image = img;
			b.subresourceRange = {aspect, baseMip, mipCount, 0, 1};
			vkl::vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
		}

		// ---------------------------------------------------------------- OpenXR / Vulkan setup

		bool CreateInstance()
		{
			uint32_t count = 0;
			XR_TRY(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr));
			std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
			XR_TRY(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data()));

			bool hasVulkan2 = false;
			for (auto& p : props)
			{
				if (!strcmp(p.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))
					hasVulkan2 = true;
				if (!strcmp(p.extensionName, XR_VALVE_FRAME_CONTROLLER_INTERACTION_EXTENSION_NAME))
					S.FrameControllerExt = true;
			}
			if (!hasVulkan2)
			{
				SetError("OpenXR runtime does not support XR_KHR_vulkan_enable2");
				return false;
			}

			std::vector<const char*> exts{XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
			if (S.FrameControllerExt)
				exts.push_back(XR_VALVE_FRAME_CONTROLLER_INTERACTION_EXTENSION_NAME);

			XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
			strncpy(ci.applicationInfo.applicationName, "Space Cadet Pinball VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
			ci.applicationInfo.applicationVersion = 1;
			strncpy(ci.applicationInfo.engineName, "SpaceCadetPinball", XR_MAX_ENGINE_NAME_SIZE - 1);
			ci.applicationInfo.engineVersion = 1;
			ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
			ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
			ci.enabledExtensionNames = exts.data();
			XR_TRY(xrCreateInstance(&ci, &S.Instance));

			XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
			if (XR_SUCCEEDED(xrGetInstanceProperties(S.Instance, &ip)))
				printf("VR: OpenXR runtime: %s %u.%u.%u%s\n", ip.runtimeName,
				       XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion),
				       XR_VERSION_PATCH(ip.runtimeVersion),
				       S.FrameControllerExt ? " (Steam Frame controller profile available)" : "");

			XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
			si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
			XR_TRY(xrGetSystem(S.Instance, &si, &S.System));

			XR_TRY(xrGetInstanceProcAddr(S.Instance, "xrGetVulkanGraphicsRequirements2KHR",
				reinterpret_cast<PFN_xrVoidFunction*>(&S.GetVkRequirements)));
			XR_TRY(xrGetInstanceProcAddr(S.Instance, "xrCreateVulkanInstanceKHR",
				reinterpret_cast<PFN_xrVoidFunction*>(&S.CreateVkInstance)));
			XR_TRY(xrGetInstanceProcAddr(S.Instance, "xrGetVulkanGraphicsDevice2KHR",
				reinterpret_cast<PFN_xrVoidFunction*>(&S.GetVkDevice)));
			XR_TRY(xrGetInstanceProcAddr(S.Instance, "xrCreateVulkanDeviceKHR",
				reinterpret_cast<PFN_xrVoidFunction*>(&S.CreateVkDevice)));
			return true;
		}

		bool CreateVulkan()
		{
			if (!vkl::LoadVulkanLibrary())
			{
				SetError("Vulkan is not available on this system");
				return false;
			}

			XrGraphicsRequirementsVulkan2KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
			XR_TRY(S.GetVkRequirements(S.Instance, S.System, &req));

			VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
			app.pApplicationName = "Space Cadet Pinball VR";
			app.pEngineName = "SpaceCadetPinball";
			app.apiVersion = VK_API_VERSION_1_1;
			if (XR_VERSION_MAJOR(req.minApiVersionSupported) == 1 && XR_VERSION_MINOR(req.minApiVersionSupported) > 1)
				app.apiVersion = VK_MAKE_VERSION(1, XR_VERSION_MINOR(req.minApiVersionSupported), 0);

			VkInstanceCreateInfo vci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
			vci.pApplicationInfo = &app;

			XrVulkanInstanceCreateInfoKHR xci{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
			xci.systemId = S.System;
			xci.pfnGetInstanceProcAddr = vkl::vkGetInstanceProcAddr;
			xci.vulkanCreateInfo = &vci;
			VkResult vr = VK_SUCCESS;
			XR_TRY(S.CreateVkInstance(S.Instance, &xci, &S.VkInst, &vr));
			VK_TRY(vr);
			if (!vkl::LoadInstance(S.VkInst))
			{
				SetError("failed to load Vulkan instance functions");
				return false;
			}

			XrVulkanGraphicsDeviceGetInfoKHR gi{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
			gi.systemId = S.System;
			gi.vulkanInstance = S.VkInst;
			XR_TRY(S.GetVkDevice(S.Instance, &gi, &S.Gpu));

			VkPhysicalDeviceProperties gpuProps;
			vkl::vkGetPhysicalDeviceProperties(S.Gpu, &gpuProps);
			printf("VR: Vulkan device: %s\n", gpuProps.deviceName);
			vkl::vkGetPhysicalDeviceMemoryProperties(S.Gpu, &S.MemProps);

			uint32_t qfCount = 0;
			vkl::vkGetPhysicalDeviceQueueFamilyProperties(S.Gpu, &qfCount, nullptr);
			std::vector<VkQueueFamilyProperties> qfs(qfCount);
			vkl::vkGetPhysicalDeviceQueueFamilyProperties(S.Gpu, &qfCount, qfs.data());
			bool found = false;
			for (uint32_t i = 0; i < qfCount; ++i)
				if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					S.QueueFamily = i;
					found = true;
					break;
				}
			if (!found)
			{
				SetError("no graphics queue");
				return false;
			}

			VkPhysicalDeviceFeatures avail;
			vkl::vkGetPhysicalDeviceFeatures(S.Gpu, &avail);
			VkPhysicalDeviceFeatures features{};
			if (avail.samplerAnisotropy)
			{
				features.samplerAnisotropy = VK_TRUE;
				S.Anisotropy = true;
				S.MaxAnisotropy = std::min(8.0f, gpuProps.limits.maxSamplerAnisotropy);
			}

			float prio = 1.0f;
			VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
			qci.queueFamilyIndex = S.QueueFamily;
			qci.queueCount = 1;
			qci.pQueuePriorities = &prio;
			VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
			dci.queueCreateInfoCount = 1;
			dci.pQueueCreateInfos = &qci;
			dci.pEnabledFeatures = &features;

			XrVulkanDeviceCreateInfoKHR xdi{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
			xdi.systemId = S.System;
			xdi.pfnGetInstanceProcAddr = vkl::vkGetInstanceProcAddr;
			xdi.vulkanPhysicalDevice = S.Gpu;
			xdi.vulkanCreateInfo = &dci;
			XR_TRY(S.CreateVkDevice(S.Instance, &xdi, &S.Device, &vr));
			VK_TRY(vr);
			if (!vkl::LoadDevice(S.Device))
			{
				SetError("failed to load Vulkan device functions");
				return false;
			}
			vkl::vkGetDeviceQueue(S.Device, S.QueueFamily, 0, &S.Queue);

			// MSAA: 4x is nearly free on tile-based mobile GPUs like the Steam Frame's Adreno.
			auto counts = gpuProps.limits.framebufferColorSampleCounts & gpuProps.limits.framebufferDepthSampleCounts;
			S.Samples = (counts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;

			for (auto fmt : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT,
			                 VK_FORMAT_D16_UNORM})
			{
				VkFormatProperties fp;
				vkl::vkGetPhysicalDeviceFormatProperties(S.Gpu, fmt, &fp);
				if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
				{
					S.DepthFormat = fmt;
					break;
				}
			}

			VkFormatProperties texProps;
			vkl::vkGetPhysicalDeviceFormatProperties(S.Gpu, VK_FORMAT_B8G8R8A8_SRGB, &texProps);
			VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
				VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			S.CanBlitMips = (texProps.optimalTilingFeatures & need) == need;

			VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
			pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			pci.queueFamilyIndex = S.QueueFamily;
			VK_TRY(vkl::vkCreateCommandPool(S.Device, &pci, nullptr, &S.CmdPool));
			return true;
		}

		bool CreateSession()
		{
			XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
			binding.instance = S.VkInst;
			binding.physicalDevice = S.Gpu;
			binding.device = S.Device;
			binding.queueFamilyIndex = S.QueueFamily;
			binding.queueIndex = 0;
			XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
			sci.next = &binding;
			sci.systemId = S.System;
			XR_TRY(xrCreateSession(S.Instance, &sci, &S.Session));

			uint32_t count = 0;
			XR_TRY(xrEnumerateReferenceSpaces(S.Session, 0, &count, nullptr));
			std::vector<XrReferenceSpaceType> spaces(count);
			XR_TRY(xrEnumerateReferenceSpaces(S.Session, count, &count, spaces.data()));
			S.StageSpace = std::find(spaces.begin(), spaces.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != spaces.end();

			XrReferenceSpaceCreateInfo rci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
			rci.poseInReferenceSpace.orientation.w = 1.0f;
			rci.referenceSpaceType = S.StageSpace ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
			XR_TRY(xrCreateReferenceSpace(S.Session, &rci, &S.AppSpace));
			rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
			XR_TRY(xrCreateReferenceSpace(S.Session, &rci, &S.ViewSpace));
			printf("VR: using %s reference space\n", S.StageSpace ? "STAGE" : "LOCAL");
			return true;
		}

		bool CreateRenderPass()
		{
			bool msaa = S.Samples != VK_SAMPLE_COUNT_1_BIT;
			VkAttachmentDescription att[3]{};
			// 0: colour (multisampled scratch, or the swapchain image itself without MSAA)
			att[0].format = S.ColorFormat;
			att[0].samples = S.Samples;
			att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			att[0].storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
			att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			// 1: depth
			att[1].format = S.DepthFormat;
			att[1].samples = S.Samples;
			att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			// 2: resolve target = swapchain image
			att[2].format = S.ColorFormat;
			att[2].samples = VK_SAMPLE_COUNT_1_BIT;
			att[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			att[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			att[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
			VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
			VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
			VkSubpassDescription sub{};
			sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			sub.colorAttachmentCount = 1;
			sub.pColorAttachments = &colorRef;
			sub.pDepthStencilAttachment = &depthRef;
			sub.pResolveAttachments = msaa ? &resolveRef : nullptr;

			VkSubpassDependency dep{};
			dep.srcSubpass = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass = 0;
			dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dep.dstStageMask = dep.srcStageMask;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

			VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
			rci.attachmentCount = msaa ? 3 : 2;
			rci.pAttachments = att;
			rci.subpassCount = 1;
			rci.pSubpasses = &sub;
			rci.dependencyCount = 1;
			rci.pDependencies = &dep;
			VK_TRY(vkl::vkCreateRenderPass(S.Device, &rci, nullptr, &S.RenderPass));
			return true;
		}

		bool CreateSwapchains()
		{
			uint32_t count = 0;
			XR_TRY(xrEnumerateViewConfigurationViews(S.Instance, S.System,
				XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr));
			if (count != 2)
			{
				SetError("expected a stereo view configuration");
				return false;
			}
			XrViewConfigurationView views[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
			XR_TRY(xrEnumerateViewConfigurationViews(S.Instance, S.System,
				XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, views));

			XR_TRY(xrEnumerateSwapchainFormats(S.Session, 0, &count, nullptr));
			std::vector<int64_t> formats(count);
			XR_TRY(xrEnumerateSwapchainFormats(S.Session, count, &count, formats.data()));
			S.ColorFormat = static_cast<VkFormat>(formats.empty() ? VK_FORMAT_R8G8B8A8_SRGB : formats[0]);
			for (auto pref : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB})
				if (std::find(formats.begin(), formats.end(), static_cast<int64_t>(pref)) != formats.end())
				{
					S.ColorFormat = pref;
					break;
				}

			if (!CreateRenderPass())
				return false;

			for (int eye = 0; eye < 2; ++eye)
			{
				auto& sc = S.Eyes[eye];
				sc.Width = static_cast<int32_t>(views[eye].recommendedImageRectWidth);
				sc.Height = static_cast<int32_t>(views[eye].recommendedImageRectHeight);
				// Optional resolution override, e.g. SCP_VR_EYE_SIZE=1600x1600 (debugging / performance tuning).
				if (auto sz = getenv("SCP_VR_EYE_SIZE"))
				{
					int ow = 0, oh = 0;
					if (sscanf(sz, "%dx%d", &ow, &oh) == 2 && ow > 0 && oh > 0)
					{
						sc.Width = std::min<int32_t>(ow, static_cast<int32_t>(views[eye].maxImageRectWidth));
						sc.Height = std::min<int32_t>(oh, static_cast<int32_t>(views[eye].maxImageRectHeight));
					}
				}

				XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
				ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
					(S.ShotPath.empty() ? XrSwapchainUsageFlags(0) : XrSwapchainUsageFlags(XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT));
				ci.format = S.ColorFormat;
				ci.sampleCount = 1;
				ci.width = static_cast<uint32_t>(sc.Width);
				ci.height = static_cast<uint32_t>(sc.Height);
				ci.faceCount = 1;
				ci.arraySize = 1;
				ci.mipCount = 1;
				XR_TRY(xrCreateSwapchain(S.Session, &ci, &sc.Handle));

				uint32_t imgCount = 0;
				XR_TRY(xrEnumerateSwapchainImages(sc.Handle, 0, &imgCount, nullptr));
				sc.Images.assign(imgCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
				XR_TRY(xrEnumerateSwapchainImages(sc.Handle, imgCount, &imgCount,
					reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.Images.data())));

				bool msaa = S.Samples != VK_SAMPLE_COUNT_1_BIT;
				if (msaa)
				{
					if (!CreateImage(sc.Width, sc.Height, 1, S.ColorFormat, S.Samples,
					                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
					                 true, sc.Msaa, sc.MsaaMem))
						return false;
					if (!CreateView(sc.Msaa, S.ColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, sc.MsaaView))
						return false;
				}
				if (!CreateImage(sc.Width, sc.Height, 1, S.DepthFormat, S.Samples,
				                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
				                 true, sc.Depth, sc.DepthMem))
					return false;
				auto depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
				if (!CreateView(sc.Depth, S.DepthFormat, depthAspect, 1, sc.DepthView))
					return false;

				sc.Views.resize(imgCount);
				sc.Framebuffers.resize(imgCount);
				for (uint32_t i = 0; i < imgCount; ++i)
				{
					if (!CreateView(sc.Images[i].image, S.ColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1, sc.Views[i]))
						return false;
					VkImageView atts[3];
					uint32_t n;
					if (msaa)
					{
						atts[0] = sc.MsaaView;
						atts[1] = sc.DepthView;
						atts[2] = sc.Views[i];
						n = 3;
					}
					else
					{
						atts[0] = sc.Views[i];
						atts[1] = sc.DepthView;
						n = 2;
					}
					VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
					fci.renderPass = S.RenderPass;
					fci.attachmentCount = n;
					fci.pAttachments = atts;
					fci.width = static_cast<uint32_t>(sc.Width);
					fci.height = static_cast<uint32_t>(sc.Height);
					fci.layers = 1;
					VK_TRY(vkl::vkCreateFramebuffer(S.Device, &fci, nullptr, &sc.Framebuffers[i]));
				}
				printf("VR: eye %d swapchain %dx%d, %u images, %dx MSAA\n", eye, sc.Width, sc.Height, imgCount,
				       static_cast<int>(S.Samples));
			}
			return true;
		}

		bool CreatePipelines()
		{
			VkDescriptorSetLayoutBinding b{};
			b.binding = 0;
			b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			b.descriptorCount = 1;
			b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			lci.bindingCount = 1;
			lci.pBindings = &b;
			VK_TRY(vkl::vkCreateDescriptorSetLayout(S.Device, &lci, nullptr, &S.SetLayout));

			VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
			                        sizeof(PushConstants)};
			VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			plci.setLayoutCount = 1;
			plci.pSetLayouts = &S.SetLayout;
			plci.pushConstantRangeCount = 1;
			plci.pPushConstantRanges = &pcr;
			VK_TRY(vkl::vkCreatePipelineLayout(S.Device, &plci, nullptr, &S.PipeLayout));

			VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
			VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			smci.codeSize = sizeof(vr_shaders::SceneVert);
			smci.pCode = vr_shaders::SceneVert;
			VK_TRY(vkl::vkCreateShaderModule(S.Device, &smci, nullptr, &vs));
			smci.codeSize = sizeof(vr_shaders::SceneFrag);
			smci.pCode = vr_shaders::SceneFrag;
			VK_TRY(vkl::vkCreateShaderModule(S.Device, &smci, nullptr, &fs));

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vs;
			stages[0].pName = "main";
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fs;
			stages[1].pName = "main";

			VkVertexInputBindingDescription vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
			VkVertexInputAttributeDescription va[4] = {
				{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Pos)},
				{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Normal)},
				{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, Uv)},
				{3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, Color)},
			};
			VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
			vi.vertexBindingDescriptionCount = 1;
			vi.pVertexBindingDescriptions = &vb;
			vi.vertexAttributeDescriptionCount = 4;
			vi.pVertexAttributeDescriptions = va;

			VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
			vps.viewportCount = 1;
			vps.scissorCount = 1;
			VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode = VK_CULL_MODE_NONE;
			rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth = 1.0f;
			VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
			ms.rasterizationSamples = S.Samples;
			VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
			ds.depthTestEnable = VK_TRUE;
			ds.depthWriteEnable = VK_TRUE;
			ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			VkPipelineColorBlendAttachmentState cba{};
			cba.colorWriteMask = 0xF;
			VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
			cb.attachmentCount = 1;
			cb.pAttachments = &cba;
			VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
			dy.dynamicStateCount = 2;
			dy.pDynamicStates = dyn;

			VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
			gp.stageCount = 2;
			gp.pStages = stages;
			gp.pVertexInputState = &vi;
			gp.pInputAssemblyState = &ia;
			gp.pViewportState = &vps;
			gp.pRasterizationState = &rs;
			gp.pMultisampleState = &ms;
			gp.pDepthStencilState = &ds;
			gp.pColorBlendState = &cb;
			gp.pDynamicState = &dy;
			gp.layout = S.PipeLayout;
			gp.renderPass = S.RenderPass;
			VkResult r = vkl::vkCreateGraphicsPipelines(S.Device, VK_NULL_HANDLE, 1, &gp, nullptr, &S.PipeOpaque);

			if (r == VK_SUCCESS)
			{
				ds.depthWriteEnable = VK_FALSE;
				cba.blendEnable = VK_TRUE;
				cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				cba.colorBlendOp = VK_BLEND_OP_ADD;
				cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				cba.alphaBlendOp = VK_BLEND_OP_ADD;
				r = vkl::vkCreateGraphicsPipelines(S.Device, VK_NULL_HANDLE, 1, &gp, nullptr, &S.PipeBlend);
			}
			if (r == VK_SUCCESS)
			{
				// Overlay: menu panel and pointer always on top, so the cabinet can never clip them.
				ds.depthTestEnable = VK_FALSE;
				ds.depthWriteEnable = VK_FALSE;
				r = vkl::vkCreateGraphicsPipelines(S.Device, VK_NULL_HANDLE, 1, &gp, nullptr, &S.PipeOverlay);
			}
			vkl::vkDestroyShaderModule(S.Device, vs, nullptr);
			vkl::vkDestroyShaderModule(S.Device, fs, nullptr);
			VK_TRY(r);

			VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			sci.magFilter = VK_FILTER_LINEAR;
			sci.minFilter = VK_FILTER_LINEAR;
			sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sci.anisotropyEnable = S.Anisotropy ? VK_TRUE : VK_FALSE;
			sci.maxAnisotropy = S.MaxAnisotropy;
			sci.maxLod = VK_LOD_CLAMP_NONE;
			VK_TRY(vkl::vkCreateSampler(S.Device, &sci, nullptr, &S.Sampler));

			VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FramesInFlight};
			VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			dpci.maxSets = FramesInFlight;
			dpci.poolSizeCount = 1;
			dpci.pPoolSizes = &ps;
			VK_TRY(vkl::vkCreateDescriptorPool(S.Device, &dpci, nullptr, &S.DescPool));
			return true;
		}

		bool CreateFrameResources()
		{
			for (auto& f : S.Frames)
			{
				VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
				ai.commandPool = S.CmdPool;
				ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				ai.commandBufferCount = 1;
				VK_TRY(vkl::vkAllocateCommandBuffers(S.Device, &ai, &f.Cmd));
				VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
				fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
				VK_TRY(vkl::vkCreateFence(S.Device, &fci, nullptr, &f.Fence));
				VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
				dai.descriptorPool = S.DescPool;
				dai.descriptorSetCount = 1;
				dai.pSetLayouts = &S.SetLayout;
				VK_TRY(vkl::vkAllocateDescriptorSets(S.Device, &dai, &f.Set));
				f.VbufSize = 4 * 1024 * 1024;
				if (!CreateBuffer(f.VbufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, f.Vbuf, f.VbufMem, &f.VbufPtr))
					return false;
			}
			return true;
		}

		void DestroyFrameTexture(FrameResources& f)
		{
			if (f.TexView)
				vkl::vkDestroyImageView(S.Device, f.TexView, nullptr);
			if (f.Tex)
				vkl::vkDestroyImage(S.Device, f.Tex, nullptr);
			if (f.TexMem)
				vkl::vkFreeMemory(S.Device, f.TexMem, nullptr);
			f.TexView = VK_NULL_HANDLE;
			f.Tex = VK_NULL_HANDLE;
			f.TexMem = VK_NULL_HANDLE;
			f.TexReady = false;
			DestroyBuffer(f.Staging, f.StagingMem);
			f.StagingPtr = nullptr;
		}

		bool EnsureFrameTexture(FrameResources& f, int w, int h)
		{
			if (f.Tex && f.TexW == w && f.TexH == h)
				return true;
			DestroyFrameTexture(f);
			f.TexW = w;
			f.TexH = h;
			f.Mips = 1;
			if (S.CanBlitMips)
				while ((std::max(w, h) >> f.Mips) > 0)
					f.Mips++;
			if (!CreateImage(w, h, f.Mips, VK_FORMAT_B8G8R8A8_SRGB, VK_SAMPLE_COUNT_1_BIT,
			                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false, f.Tex, f.TexMem))
				return false;
			if (!CreateView(f.Tex, VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, f.Mips, f.TexView))
				return false;
			f.StagingSize = static_cast<VkDeviceSize>(w) * h * 4;
			if (!CreateBuffer(f.StagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, f.Staging, f.StagingMem,
			                  &f.StagingPtr))
				return false;

			VkDescriptorImageInfo ii{S.Sampler, f.TexView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			VkWriteDescriptorSet w2{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
			w2.dstSet = f.Set;
			w2.dstBinding = 0;
			w2.descriptorCount = 1;
			w2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			w2.pImageInfo = &ii;
			vkl::vkUpdateDescriptorSets(S.Device, 1, &w2, 0, nullptr);
			return true;
		}

		// ---------------------------------------------------------------- Actions

		bool CreateActions()
		{
			XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
			strcpy(asci.actionSetName, "pinball");
			strcpy(asci.localizedActionSetName, "Pinball");
			XR_TRY(xrCreateActionSet(S.Instance, &asci, &S.ActionSet));

			S.HandPath[0] = Path("/user/hand/left");
			S.HandPath[1] = Path("/user/hand/right");

			auto make = [&](XrAction& action, XrActionType type, const char* name, const char* loc, bool hands)
			{
				XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
				ci.actionType = type;
				strcpy(ci.actionName, name);
				strcpy(ci.localizedActionName, loc);
				if (hands)
				{
					ci.countSubactionPaths = 2;
					ci.subactionPaths = S.HandPath;
				}
				return XrOk(xrCreateAction(S.ActionSet, &ci, &action), name);
			};

			bool ok = true;
			ok &= make(S.FlipL, XR_ACTION_TYPE_BOOLEAN_INPUT, "flipper_left", "Left Flipper", false);
			ok &= make(S.FlipR, XR_ACTION_TYPE_BOOLEAN_INPUT, "flipper_right", "Right Flipper", false);
			ok &= make(S.Plunger, XR_ACTION_TYPE_BOOLEAN_INPUT, "plunger", "Plunger", false);
			ok &= make(S.NudgeL, XR_ACTION_TYPE_BOOLEAN_INPUT, "nudge_left", "Nudge Left", false);
			ok &= make(S.NudgeR, XR_ACTION_TYPE_BOOLEAN_INPUT, "nudge_right", "Nudge Right", false);
			ok &= make(S.NudgeUp, XR_ACTION_TYPE_BOOLEAN_INPUT, "nudge_up", "Nudge Up", false);
			ok &= make(S.Move, XR_ACTION_TYPE_VECTOR2F_INPUT, "nudge_stick", "Nudge Stick", false);
			ok &= make(S.Look, XR_ACTION_TYPE_VECTOR2F_INPUT, "adjust_view", "Adjust Table", false);
			ok &= make(S.Pause, XR_ACTION_TYPE_BOOLEAN_INPUT, "pause", "Pause", false);
			ok &= make(S.Menu, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", false);
			ok &= make(S.NewGame, XR_ACTION_TYPE_BOOLEAN_INPUT, "new_game", "New Game (hold)", false);
			ok &= make(S.ViewToggle, XR_ACTION_TYPE_BOOLEAN_INPUT, "toggle_view", "Cabinet / Theater", false);
			ok &= make(S.Recenter, XR_ACTION_TYPE_BOOLEAN_INPUT, "recenter", "Recenter", false);
			ok &= make(S.Back, XR_ACTION_TYPE_BOOLEAN_INPUT, "back", "Close Menu", false);
			ok &= make(S.Grab, XR_ACTION_TYPE_BOOLEAN_INPUT, "grab", "Grab (flipper buttons, plunger, move table)", true);
			ok &= make(S.AimPose, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim", true);
			ok &= make(S.GripPose, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip", true);
			ok &= make(S.HapticOut, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptics", true);
			if (!ok)
				return false;

			struct B
			{
				XrAction action;
				const char* path;
			};
			auto suggest = [&](const char* profile, std::vector<B> bindings)
			{
				std::vector<XrActionSuggestedBinding> sb;
				for (auto& b : bindings)
				{
					XrPath p = XR_NULL_PATH;
					if (XR_SUCCEEDED(xrStringToPath(S.Instance, b.path, &p)))
						sb.push_back({b.action, p});
				}
				for (int hand = 0; hand < 2; ++hand)
				{
					std::string h = hand ? "/user/hand/right" : "/user/hand/left";
					sb.push_back({S.AimPose, Path((h + "/input/aim/pose").c_str())});
					sb.push_back({S.GripPose, Path((h + "/input/grip/pose").c_str())});
					sb.push_back({S.HapticOut, Path((h + "/output/haptic").c_str())});
					if (strstr(profile, "vive_controller"))
						sb.push_back({S.Grab, Path((h + "/input/squeeze/click").c_str())});
					else if (!strstr(profile, "simple_controller"))
						sb.push_back({S.Grab, Path((h + "/input/squeeze/value").c_str())});
				}
				XrInteractionProfileSuggestedBinding ps{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
				ps.interactionProfile = Path(profile);
				ps.countSuggestedBindings = static_cast<uint32_t>(sb.size());
				ps.suggestedBindings = sb.data();
				auto r = xrSuggestInteractionProfileBindings(S.Instance, &ps);
				if (XR_FAILED(r))
					printf("VR: bindings for %s rejected (%d)\n", profile, static_cast<int>(r));
			};

			if (S.FrameControllerExt)
			{
				suggest("/interaction_profiles/valve/frame_controller_valve", {
					        {S.FlipL, "/user/hand/left/input/trigger/value"},
					        {S.FlipL, "/user/hand/left/input/bumper/click"},
					        {S.FlipR, "/user/hand/right/input/trigger/value"},
					        {S.FlipR, "/user/hand/right/input/bumper/click"},
					        {S.Plunger, "/user/hand/right/input/a/click"},
					        {S.NudgeL, "/user/hand/left/input/dpad_left/click"},
					        {S.NudgeR, "/user/hand/left/input/dpad_right/click"},
					        {S.NudgeUp, "/user/hand/left/input/dpad_up/click"},
					        {S.Back, "/user/hand/left/input/dpad_down/click"},
					        {S.Move, "/user/hand/left/input/thumbstick"},
					        {S.Look, "/user/hand/right/input/thumbstick"},
					        {S.Pause, "/user/hand/right/input/b/click"},
					        {S.NewGame, "/user/hand/right/input/x/click"},
					        {S.ViewToggle, "/user/hand/right/input/y/click"},
					        {S.Menu, "/user/hand/left/input/view/click"},
					        {S.Menu, "/user/hand/right/input/menu/click"},
					        {S.Recenter, "/user/hand/right/input/thumbstick/click"},
				        });
			}
			suggest("/interaction_profiles/oculus/touch_controller", {
				        {S.FlipL, "/user/hand/left/input/trigger/value"},
				        {S.FlipR, "/user/hand/right/input/trigger/value"},
				        {S.Plunger, "/user/hand/right/input/a/click"},
				        {S.Pause, "/user/hand/right/input/b/click"},
				        {S.NewGame, "/user/hand/left/input/x/click"},
				        {S.ViewToggle, "/user/hand/left/input/y/click"},
				        {S.Menu, "/user/hand/left/input/menu/click"},
				        {S.Move, "/user/hand/left/input/thumbstick"},
				        {S.Look, "/user/hand/right/input/thumbstick"},
				        {S.Recenter, "/user/hand/right/input/thumbstick/click"},
				        {S.Back, "/user/hand/left/input/thumbstick/click"},
			        });
			suggest("/interaction_profiles/valve/index_controller", {
				        {S.FlipL, "/user/hand/left/input/trigger/value"},
				        {S.FlipR, "/user/hand/right/input/trigger/value"},
				        {S.Plunger, "/user/hand/right/input/a/click"},
				        {S.Pause, "/user/hand/right/input/b/click"},
				        {S.NewGame, "/user/hand/left/input/a/click"},
				        {S.ViewToggle, "/user/hand/left/input/b/click"},
				        {S.Menu, "/user/hand/left/input/thumbstick/click"},
				        {S.Move, "/user/hand/left/input/thumbstick"},
				        {S.Look, "/user/hand/right/input/thumbstick"},
				        {S.Recenter, "/user/hand/right/input/thumbstick/click"},
			        });
			suggest("/interaction_profiles/htc/vive_controller", {
				        {S.FlipL, "/user/hand/left/input/trigger/value"},
				        {S.FlipR, "/user/hand/right/input/trigger/value"},
				        {S.Plunger, "/user/hand/right/input/squeeze/click"},
				        {S.Menu, "/user/hand/left/input/menu/click"},
				        {S.Pause, "/user/hand/right/input/menu/click"},
				        {S.Move, "/user/hand/left/input/trackpad"},
				        {S.Look, "/user/hand/right/input/trackpad"},
				        {S.Recenter, "/user/hand/right/input/trackpad/click"},
			        });
			suggest("/interaction_profiles/khr/simple_controller", {
				        {S.FlipL, "/user/hand/left/input/select/click"},
				        {S.FlipR, "/user/hand/right/input/select/click"},
				        {S.Menu, "/user/hand/left/input/menu/click"},
				        {S.Menu, "/user/hand/right/input/menu/click"},
			        });

			for (int hand = 0; hand < 2; ++hand)
			{
				XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
				sci.subactionPath = S.HandPath[hand];
				sci.poseInActionSpace.orientation.w = 1.0f;
				sci.action = S.AimPose;
				XR_TRY(xrCreateActionSpace(S.Session, &sci, &S.AimSpace[hand]));
				sci.action = S.GripPose;
				XR_TRY(xrCreateActionSpace(S.Session, &sci, &S.GripSpace[hand]));
			}

			XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
			ai.countActionSets = 1;
			ai.actionSets = &S.ActionSet;
			XR_TRY(xrAttachSessionActionSets(S.Session, &ai));
			return true;
		}

		bool GetBool(XrAction action, bool* changed = nullptr)
		{
			XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
			gi.action = action;
			XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
			if (XR_FAILED(xrGetActionStateBoolean(S.Session, &gi, &st)) || !st.isActive)
			{
				if (changed)
					*changed = false;
				return false;
			}
			if (changed)
				*changed = st.changedSinceLastSync == XR_TRUE;
			return st.currentState == XR_TRUE;
		}

		bool GetBoolHand(XrAction action, int hand)
		{
			XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
			gi.action = action;
			gi.subactionPath = S.HandPath[hand];
			XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
			if (XR_FAILED(xrGetActionStateBoolean(S.Session, &gi, &st)) || !st.isActive)
				return false;
			return st.currentState == XR_TRUE;
		}

		bool Pressed(XrAction action)
		{
			bool changed = false;
			bool state = GetBool(action, &changed);
			return state && changed;
		}

		Vec2 GetVec2(XrAction action)
		{
			XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
			gi.action = action;
			XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
			if (XR_FAILED(xrGetActionStateVector2f(S.Session, &gi, &st)) || !st.isActive)
				return {0, 0};
			return {st.currentState.x, st.currentState.y};
		}

		bool Locate(XrSpace space, XrTime time, Pose& out)
		{
			XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
			if (XR_FAILED(xrLocateSpace(space, S.AppSpace, time, &loc)))
				return false;
			const auto need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
			if ((loc.locationFlags & need) != need)
				return false;
			out = ToPose(loc.pose);
			return true;
		}

		// ---------------------------------------------------------------- World layout

		Quat AnchorRot()
		{
			return Quat::AxisAngle({0, 1, 0}, S.AnchorYaw);
		}

		void DoRecenter()
		{
			if (!S.HeadValid)
				return;
			S.AnchorPos = S.Head.position;
			Vec3 fwd = S.Head.orientation.Rotate({0, 0, -1});
			S.AnchorYaw = std::atan2(-fwd.x, -fwd.z);
			S.FloorY = S.StageSpace ? 0.0f : S.AnchorPos.y - 1.65f;
			if (S.StageSpace && S.AnchorPos.y < 0.5f)
				S.FloorY = S.AnchorPos.y - 1.2f; // stage space reporting a floor above the head: be defensive
			S.NeedRecenter = false;
			printf("VR: recentred at (%.2f, %.2f, %.2f) yaw %.0f\n", S.AnchorPos.x, S.AnchorPos.y, S.AnchorPos.z,
			       S.AnchorYaw * 180.0f / Pi);
		}

		float FrameAspect()
		{
			return static_cast<float>(S.LastFrameW) / std::max(1, S.LastFrameH);
		}

		void ComputePanel(bool uiVisible)
		{
			Panel& p = S.UiPanel;
			Quat rot = AnchorRot();
			Vec3 fwd = rot.Rotate({0, 0, -1});
			Vec3 right = rot.Rotate({1, 0, 0});
			Vec3 up{0, 1, 0};
			if (S.Cfg.Mode == ViewMode::Theater)
			{
				// Normally shows just the game area; grows to the whole frame when menus are open.
				float w = S.Cfg.TheaterWidth;
				float dist = 0.45f + w * 0.95f;
				float aspect = FrameAspect();
				if (!uiVisible)
				{
					float gw = (S.LastGameRect[2] - S.LastGameRect[0]) * S.LastFrameW;
					float gh = (S.LastGameRect[3] - S.LastGameRect[1]) * S.LastFrameH;
					if (gw > 0 && gh > 0)
						aspect = gw / gh;
				}
				p.HalfW = w * 0.5f;
				p.HalfH = p.HalfW / aspect;
				p.Center = S.AnchorPos + fwd * dist + up * (-0.08f);
				p.Right = right;
				p.Up = up;
				p.Visible = true;
			}
			else
			{
				// Floats between the player and the backbox, clear of the playfield.
				float w = 0.70f;
				p.HalfW = w * 0.5f;
				p.HalfH = p.HalfW / FrameAspect();
				float tilt = Deg(10.0f);
				p.Center = S.AnchorPos + fwd * 0.55f + up * (-0.03f + S.Cfg.HeightOffset * 0.5f);
				p.Right = right;
				// lean the top away from the viewer a little
				p.Up = Normalize(up * std::cos(tilt) + fwd * std::sin(tilt));
				p.Visible = uiVisible;
			}
		}

		bool IntersectPanel(const Pose& aim, float& u, float& v, Vec3& hit)
		{
			const Panel& p = S.UiPanel;
			Vec3 o = aim.position;
			Vec3 d = aim.orientation.Rotate({0, 0, -1});
			Vec3 n = Normalize(Cross(p.Right, p.Up));
			float denom = Dot(d, n);
			if (std::fabs(denom) < 1e-4f)
				return false;
			float t = Dot(p.Center - o, n) / denom;
			if (t <= 0.0f || t > 20.0f)
				return false;
			hit = o + d * t;
			Vec3 rel = hit - p.Center;
			float x = Dot(rel, p.Right) / (p.HalfW * 2.0f);
			float y = Dot(rel, p.Up) / (p.HalfH * 2.0f);
			u = x + 0.5f;
			v = 0.5f - y;
			return u >= -0.02f && u <= 1.02f && v >= -0.02f && v <= 1.02f;
		}

		// ---------------------------------------------------------------- Scene

		const Color NavyPaint{0.025f, 0.035f, 0.085f, 0.55f};
		const Color DarkTrim{0.012f, 0.012f, 0.02f, 0.35f};
		const Color Chrome{0.55f, 0.57f, 0.62f, 0.95f};
		const Color BlackChrome{0.08f, 0.08f, 0.1f, 0.9f};
		const Color NeonCyan{0.1f, 0.75f, 1.0f, 1.0f};
		const Color NeonMagenta{0.95f, 0.15f, 0.75f, 1.0f};
		const Color NeonAmber{1.0f, 0.55f, 0.1f, 1.0f};

		void TransformRange(size_t first, const Pose& pose)
		{
			auto& vs = S.Scene.Vertices;
			for (size_t i = first; i < vs.size(); ++i)
			{
				Vec3 p{vs[i].Pos[0], vs[i].Pos[1], vs[i].Pos[2]};
				Vec3 n{vs[i].Normal[0], vs[i].Normal[1], vs[i].Normal[2]};
				p = pose.Transform(p);
				n = pose.TransformDir(n);
				vs[i].Pos[0] = p.x;
				vs[i].Pos[1] = p.y;
				vs[i].Pos[2] = p.z;
				vs[i].Normal[0] = n.x;
				vs[i].Normal[1] = n.y;
				vs[i].Normal[2] = n.z;
			}
		}

		// Frame UV -> texture UV. game = the cabinet's screens, which use the menu-free copy when available.
		float TexV(float v, bool game)
		{
			return (game && S.FrameDual ? 0.5f : 0.0f) + v * 0.5f;
		}

		void SetUv(DrawItem& item, const float (&r)[4], bool game)
		{
			item.UvRect[0] = r[0];
			item.UvRect[1] = TexV(r[1], game);
			item.UvRect[2] = r[2];
			item.UvRect[3] = TexV(r[3], game);
		}

		void BuildEnvironmentGeometry();

		void BuildEnvironment()
		{
			// Sky, planet and floor only move when the view is recentred, so cache them.
			if (Length(S.EnvAnchor - S.AnchorPos) > 1e-4f || S.EnvYaw != S.AnchorYaw || S.EnvFloor != S.FloorY ||
				S.EnvCache.Vertices.empty())
			{
				std::swap(S.Scene, S.EnvCache);
				S.Scene.Clear();
				BuildEnvironmentGeometry();
				std::swap(S.Scene, S.EnvCache);
				S.EnvAnchor = S.AnchorPos;
				S.EnvYaw = S.AnchorYaw;
				S.EnvFloor = S.FloorY;
			}
			S.Scene.Vertices.insert(S.Scene.Vertices.end(), S.EnvCache.Vertices.begin(), S.EnvCache.Vertices.end());
			S.Scene.Items.insert(S.Scene.Items.end(), S.EnvCache.Items.begin(), S.EnvCache.Items.end());
		}

		void BuildEnvironmentGeometry()
		{
			auto& sc = S.Scene;
			Quat rot = AnchorRot();
			// Sky dome around the player.
			sc.Begin(Material::Sky);
			sc.Sphere(S.AnchorPos, 150.0f, 32, 16, {0, 0, 0, 0}, true);
			// A distant ringless gas giant for some depth.
			sc.Begin(Material::Planet);
			sc.Sphere(S.AnchorPos + rot.Rotate({-42.0f, 34.0f, -100.0f}), 20.0f, 40, 20, {1, 1, 1, 0}, false);
			// Floor
			sc.Begin(Material::Floor);
			float r = 60.0f;
			Vec3 c{S.AnchorPos.x, S.FloorY, S.AnchorPos.z};
			sc.Quad(c + Vec3{-r, 0, r}, c + Vec3{r, 0, r}, c + Vec3{r, 0, -r}, c + Vec3{-r, 0, -r}, {0, 0, 0, 0});
			sc.End();
		}

		// Table space: origin at the front edge centre of the playfield; x right, y up, +z towards the player.
		// The game's artwork already contains the machine's metal frames (around the playfield and the score
		// panel), so the cabinet only adds what the artwork lacks: the body, legs and backbox shell.
		void BuildCabinet(const FrameLayout& layout, bool uiVisible)
		{
			auto& sc = S.Scene;
			const float scale = S.Cfg.TableScale;
			const float boardPxW = (layout.Board[2] - layout.Board[0]) * S.LastFrameW;
			const float boardPxH = (layout.Board[3] - layout.Board[1]) * S.LastFrameH;
			const float sidePxW = (layout.Sidebar[2] - layout.Sidebar[0]) * S.LastFrameW;
			const float sidePxH = (layout.Sidebar[3] - layout.Sidebar[1]) * S.LastFrameH;
			const float sideAspect = sidePxH > 0 ? sidePxW / sidePxH : 0.55f;
			const bool mapped = layout.PlayfieldMap && layout.PlayfieldAspect > 0.1f;
			const float lengthOverWidth = mapped ? layout.PlayfieldAspect : (boardPxW > 0 ? boardPxH / boardPxW : 1.1f);

			const float W = 0.56f * scale;
			const float L = W * lengthOverWidth;
			const float th = Deg(S.Cfg.TiltDeg);
			const float sn = std::sin(th), cs = std::cos(th);
			auto P = [&](float s, float t, float h) { return Vec3{s, t * sn + h * cs, -t * cs + h * sn}; };

			Quat rot = AnchorRot();
			Vec3 fwd = rot.Rotate({0, 0, -1});
			Vec3 origin = S.AnchorPos + fwd * S.Cfg.Distance +
				Vec3{0, -0.56f * scale + S.Cfg.HeightOffset, 0} + rot.Rotate(S.Shake);
			Pose world;
			world.orientation = rot;
			world.position = origin;
			const float floorLocal = S.FloorY - origin.y;

			size_t first = sc.Vertices.size();

			// --- body, flush with the playfield artwork
			const float yb = std::max(-0.32f * scale, floorLocal + 0.05f);
			Vec3 tfl = P(-W / 2, 0, -0.004f), tfr = P(W / 2, 0, -0.004f);
			Vec3 tbl = P(-W / 2, L, -0.004f), tbr = P(W / 2, L, -0.004f);
			Vec3 body[8] = {
				{tfl.x, yb, tfl.z}, {tfr.x, yb, tfr.z}, tfr, tfl,
				{tbl.x, yb, tbl.z}, {tbr.x, yb, tbr.z}, tbr, tbl,
			};
			sc.Begin(Material::Lit);
			sc.Hexa(body, NavyPaint);

			// legs and feet
			if (floorLocal < yb - 0.05f)
			{
				const float lh = 0.022f * scale;
				float xs[2] = {body[0].x + 0.05f * scale, body[1].x - 0.05f * scale};
				float zs[2] = {body[0].z - 0.06f * scale, body[4].z + 0.06f * scale};
				for (float x : xs)
					for (float z : zs)
					{
						Pose id;
						float h = (yb - floorLocal) * 0.5f;
						sc.Box(id, {x, floorLocal + h, z}, {lh, h, lh}, Chrome);
						sc.Box(id, {x, floorLocal + 0.012f, z}, {lh * 1.6f, 0.012f, lh * 1.6f}, BlackChrome);
					}
			}

			// coin door on the front face
			const float frontTop = tfl.y;
			const float dc = yb + (frontTop - yb) * 0.5f;
			{
				Pose id;
				float dh = std::min(0.09f * scale, (frontTop - yb) * 0.3f);
				sc.Box(id, {0, dc, body[0].z + 0.006f}, {0.11f * scale, dh, 0.006f}, BlackChrome);
			}

			// --- backbox: as wide as the table, height from the score panel artwork's aspect ratio
			const float Ws = W;
			const float Hs = Ws / std::max(sideAspect, 0.1f);
			const float Db = 0.15f * scale;
			const float back = Deg(9.0f);
			Pose bb;
			bb.orientation = Quat::AxisAngle({1, 0, 0}, -back);
			bb.position = P(0, L, -0.004f);
			sc.Box(bb, {0, Hs / 2, -Db / 2}, {Ws / 2, Hs / 2, Db / 2}, NavyPaint);

			// --- physical controls: flipper buttons on the sides, plunger at the front right
			const Color ButtonRing{0.10f, 0.10f, 0.12f, 0.85f};
			const float btnT = std::min(0.13f * scale, L * 0.3f);   // distance up the table from the front
			const float btnDrop = std::min(0.07f * scale, (frontTop - yb) * 0.5f);
			Vec3 btnLocal[2], btnOut[2];
			for (int side = 0; side < 2; ++side)
			{
				const float sx = side == 0 ? -1.0f : 1.0f;
				Vec3 edge = P(sx * W / 2, btnT, -0.004f);
				btnLocal[side] = Vec3{edge.x + sx * 0.010f, edge.y - btnDrop, edge.z};
				btnOut[side] = Vec3{sx, 0, 0};
				Pose id;
				sc.Box(id, btnLocal[side] - Vec3{sx * 0.005f, 0, 0}, {0.005f, 0.024f, 0.024f}, ButtonRing);
			}
			const float faceZ = body[0].z;
			const Vec3 plungerBase{W / 2 - 0.07f * scale, frontTop - std::min(0.08f * scale, (frontTop - yb) * 0.45f),
			                       faceZ};
			const float rodRest = 0.055f * scale;
			S.PlungerTravel = 0.07f * scale;
			const float knobZ = rodRest + S.PlungerVisual * S.PlungerTravel;
			{
				Pose id;
				// housing, rod and knob
				sc.Box(id, plungerBase + Vec3{0, 0, 0.006f}, {0.022f, 0.022f, 0.006f}, Chrome);
				sc.Box(id, plungerBase + Vec3{0, 0, (0.012f + knobZ) * 0.5f}, {0.0045f, 0.0045f, (knobZ - 0.012f) * 0.5f},
				       Chrome);
				sc.Sphere(plungerBase + Vec3{0, 0, knobZ + 0.012f}, 0.017f * scale, 20, 10,
				          {0.75f, 0.06f, 0.05f, 0.8f}, false);
			}

			// --- lights on the cabinet body (not around the artwork)
			sc.Begin(Material::Unlit, 1.6f);
			auto stripe = [&](const Vec3& a, const Vec3& b, float y0, float y1, float dx, const Color& col)
			{
				sc.Quad(Vec3{a.x + dx, a.y + y0, a.z}, Vec3{b.x + dx, b.y + y0, b.z},
				        Vec3{b.x + dx, b.y + y1, b.z}, Vec3{a.x + dx, a.y + y1, a.z}, col);
			};
			stripe(body[4], body[0], 0.02f, 0.03f, -0.002f, NeonMagenta);  // left bottom
			stripe(body[1], body[5], 0.02f, 0.03f, 0.002f, NeonMagenta);   // right bottom
			for (int side = 0; side < 2; ++side)
			{
				// flipper button caps light up while pressed
				const float sx = side == 0 ? -1.0f : 1.0f;
				const float push = S.FlipPressed[side] ? 0.004f : 0.0f;
				Vec3 c = btnLocal[side] + Vec3{sx * (0.006f - push), 0, 0};
				sc.Disc(c, Vec3{0, 0, sx}, Vec3{0, 1, 0}, 0.0f, 0.019f, 24,
				        S.FlipPressed[side] ? Color{1.0f, 0.75f, 0.2f, 1} : Color{0.55f, 0.08f, 0.06f, 1});
			}
			{
				Vec3 a = body[0], b = body[1];
				sc.Quad(Vec3{a.x, a.y + 0.02f, a.z + 0.002f}, Vec3{b.x, b.y + 0.02f, b.z + 0.002f},
				        Vec3{b.x, b.y + 0.03f, b.z + 0.002f}, Vec3{a.x, a.y + 0.03f, a.z + 0.002f}, NeonMagenta);
				for (float x : {-0.045f * scale, 0.045f * scale})
					sc.Quad(Vec3{x - 0.012f, dc - 0.02f, a.z + 0.0135f}, Vec3{x + 0.012f, dc - 0.02f, a.z + 0.0135f},
					        Vec3{x + 0.012f, dc + 0.03f, a.z + 0.0135f}, Vec3{x - 0.012f, dc + 0.03f, a.z + 0.0135f},
					        NeonAmber);
			}

			// --- artwork
			// While a menu is open on the floating panel, dim the cabinet's copy of the frame.
			const float screenLevel = uiVisible ? 0.3f : 1.0f;
			auto& play = sc.Begin(Material::Screen);
			play.Brightness = screenLevel;
			play.EdgeFade = 0;
			if (mapped)
			{
				// Undo the artwork's baked-in perspective: a finely subdivided flat rectangle whose vertices
				// look up the image through the game's own camera. VR then supplies the real perspective.
				const int nx = 16, ny = 40;
				std::vector<Vec2> uv((nx + 1) * (ny + 1));
				for (int j = 0; j <= ny; ++j)
					for (int i = 0; i <= nx; ++i)
					{
						float u, v;
						layout.PlayfieldMap(static_cast<float>(i) / nx, static_cast<float>(j) / ny, &u, &v);
						uv[j * (nx + 1) + i] = {u, TexV(v, true)};
					}
				const Vec3 n = Normalize(Cross(P(1, 0, 0) - P(0, 0, 0), P(0, 1, 0) - P(0, 0, 0)));
				auto pos = [&](int i, int j) { return P(-W / 2 + W * i / nx, L * j / ny, 0); };
				for (int j = 0; j < ny; ++j)
					for (int i = 0; i < nx; ++i)
					{
						const Vec2 &a = uv[j * (nx + 1) + i], &b = uv[j * (nx + 1) + i + 1];
						const Vec2 &c = uv[(j + 1) * (nx + 1) + i + 1], &d = uv[(j + 1) * (nx + 1) + i];
						sc.Tri(pos(i, j), pos(i + 1, j), pos(i + 1, j + 1), a, b, c, n, {1, 1, 1, 1});
						sc.Tri(pos(i, j), pos(i + 1, j + 1), pos(i, j + 1), a, c, d, n, {1, 1, 1, 1});
					}
			}
			else
			{
				SetUv(play, layout.Board, true);
				sc.Quad(P(-W / 2, 0, 0), P(W / 2, 0, 0), P(W / 2, L, 0), P(-W / 2, L, 0), {1, 1, 1, 1});
			}
			auto& side = sc.Begin(Material::Screen);
			SetUv(side, layout.Sidebar, true);
			side.Brightness = screenLevel;
			side.EdgeFade = 0;
			sc.Quad(bb.Transform({-Ws / 2, 0, 0.001f}), bb.Transform({Ws / 2, 0, 0.001f}),
			        bb.Transform({Ws / 2, Hs, 0.001f}), bb.Transform({-Ws / 2, Hs, 0.001f}), {1, 1, 1, 1});

			// --- glass (blended, drawn last)
			sc.Begin(Material::Glass, 0, true);
			sc.Quad(P(-W / 2, 0, 0.012f), P(W / 2, 0, 0.012f), P(W / 2, L, 0.012f), P(-W / 2, L, 0.012f),
			        {1, 1, 1, 1});
			sc.Quad(bb.Transform({-Ws / 2, 0, 0.008f}), bb.Transform({Ws / 2, 0, 0.008f}),
			        bb.Transform({Ws / 2, Hs, 0.008f}), bb.Transform({-Ws / 2, Hs, 0.008f}), {1, 1, 1, 1});
			sc.End();

			TransformRange(first, world);

			for (int side = 0; side < 2; ++side)
			{
				S.FlipButton[side] = world.Transform(btnLocal[side]);
				S.FlipButtonOut[side] = world.TransformDir(btnOut[side]);
			}
			S.PlungerRest = world.Transform(plungerBase + Vec3{0, 0, rodRest + 0.012f});
			S.PlungerAxis = world.TransformDir({0, 0, 1});
			S.ControlsValid = true;

			S.GlowCenter = world.Transform(P(0, L * 0.5f, 0.05f));
			S.GlowStrength = 1.0f;
		}

		void BuildPanel(const Panel& p, const float (&uv)[4], bool bezel, bool overlay = false)
		{
			auto& sc = S.Scene;
			const size_t firstItem = sc.Items.size();
			Vec3 n = Normalize(Cross(p.Right, p.Up));
			auto corner = [&](float sx, float sy, float off, float grow)
			{
				return p.Center + p.Right * (sx * (p.HalfW + grow)) + p.Up * (sy * (p.HalfH + grow)) + n * off;
			};
			if (bezel)
			{
				sc.Begin(Material::Lit);
				float g = std::max(p.HalfW * 0.03f, 0.012f);
				Vec3 c[8] = {
					corner(-1, -1, 0.0f, g), corner(1, -1, 0.0f, g), corner(1, 1, 0.0f, g), corner(-1, 1, 0.0f, g),
					corner(-1, -1, -0.03f, g), corner(1, -1, -0.03f, g), corner(1, 1, -0.03f, g),
					corner(-1, 1, -0.03f, g),
				};
				sc.Hexa(c, BlackChrome);
				sc.Begin(Material::Unlit, 1.4f);
				float t = g * 0.25f;
				// thin neon outline around the panel
				sc.Quad(corner(-1, -1, 0.001f, g), corner(1, -1, 0.001f, g), corner(1, -1, 0.001f, g - t),
				        corner(-1, -1, 0.001f, g - t), NeonCyan);
				sc.Quad(corner(-1, 1, 0.001f, g - t), corner(1, 1, 0.001f, g - t), corner(1, 1, 0.001f, g),
				        corner(-1, 1, 0.001f, g), NeonCyan);
			}
			auto& item = sc.Begin(Material::Screen);
			SetUv(item, uv, false);
			sc.Quad(corner(-1, -1, 0.002f, 0), corner(1, -1, 0.002f, 0), corner(1, 1, 0.002f, 0),
			        corner(-1, 1, 0.002f, 0), {1, 1, 1, 1});
			sc.End();
			if (overlay)
				for (size_t i = firstItem; i < sc.Items.size(); ++i)
					sc.Items[i].Overlay = true;
		}

		void BuildControllers(bool uiVisible)
		{
			auto& sc = S.Scene;
			for (int hand = 0; hand < 2; ++hand)
			{
				if (!S.GripValid[hand])
					continue;
				sc.Begin(Material::Lit);
				sc.Box(S.Grip[hand], {0, -0.005f, 0.03f}, {0.018f, 0.022f, 0.055f}, {0.05f, 0.05f, 0.06f, 0.6f});
				sc.Box(S.Grip[hand], {0, 0.012f, -0.035f}, {0.03f, 0.012f, 0.03f}, {0.07f, 0.07f, 0.08f, 0.7f});
				sc.Begin(Material::Unlit, 1.5f);
				sc.Box(S.Grip[hand], {0, 0.0245f, -0.035f}, {0.024f, 0.0008f, 0.024f},
				       hand == 0 ? NeonCyan : NeonAmber);
			}
			sc.End();

			if (!uiVisible || !S.AimValid[S.PointerHand])
				return;
			const Pose& aim = S.Aim[S.PointerHand];
			Vec3 dir = aim.orientation.Rotate({0, 0, -1});
			Vec3 end = S.PointerHit ? S.PointerHitPos : aim.position + dir * 2.5f;
			sc.Begin(Material::Beam, 1.8f, true).Overlay = true;
			sc.Beam(aim.position + dir * 0.02f, end, 0.007f, {0.35f, 0.85f, 1.0f, 0.85f});
			if (S.PointerHit)
			{
				Vec3 n = Normalize(Cross(S.UiPanel.Right, S.UiPanel.Up));
				sc.Begin(Material::Unlit, 2.0f).Overlay = true;
				sc.Disc(S.PointerHitPos + n * 0.004f, S.UiPanel.Right, S.UiPanel.Up, 0.0f, 0.006f, 16,
				        {0.6f, 0.95f, 1.0f, 1.0f});
			}
			sc.End();
		}

		void BuildScene(const FrameLayout& layout, bool uiVisible)
		{
			S.Scene.Clear();
			BuildEnvironment();
			const float full[4] = {0, 0, 1, 1};
			if (S.Cfg.Mode == ViewMode::Cabinet)
			{
				BuildCabinet(layout, uiVisible);
				if (uiVisible)
					BuildPanel(S.UiPanel, full, true, true);
			}
			else
			{
				S.ControlsValid = false;
				BuildPanel(S.UiPanel, uiVisible ? full : layout.Game, true);
				S.GlowCenter = S.UiPanel.Center;
				S.GlowStrength = 2.5f;
			}
			BuildControllers(uiVisible);
		}

		// ---------------------------------------------------------------- Debug screenshot

		void WriteScreenshot(FrameResources& f)
		{
			std::string path = S.ShotPath;
			FILE* file = fopen(path.c_str(), "wb");
			if (!file)
				return;
			fprintf(file, "P6\n%d %d\n255\n", f.ShotW, f.ShotH);
			auto src = static_cast<const uint8_t*>(f.ShotPtr);
			bool bgra = S.ColorFormat == VK_FORMAT_B8G8R8A8_SRGB || S.ColorFormat == VK_FORMAT_B8G8R8A8_UNORM;
			std::vector<uint8_t> row(static_cast<size_t>(f.ShotW) * 3);
			for (int y = 0; y < f.ShotH; ++y)
			{
				for (int x = 0; x < f.ShotW; ++x)
				{
					const uint8_t* px = src + (static_cast<size_t>(y) * f.ShotW + x) * 4;
					row[x * 3 + 0] = bgra ? px[2] : px[0];
					row[x * 3 + 1] = px[1];
					row[x * 3 + 2] = bgra ? px[0] : px[2];
				}
				fwrite(row.data(), 1, row.size(), file);
			}
			fclose(file);
			printf("VR: wrote screenshot %s\n", path.c_str());
		}

		void DestroyAll()
		{
			if (S.Device)
				vkl::vkDeviceWaitIdle(S.Device);

			for (auto& f : S.Frames)
			{
				if (!S.Device)
					break;
				DestroyFrameTexture(f);
				DestroyBuffer(f.Vbuf, f.VbufMem);
				DestroyBuffer(f.Shot, f.ShotMem);
				if (f.Fence)
					vkl::vkDestroyFence(S.Device, f.Fence, nullptr);
				f = FrameResources{};
			}
			for (auto& sc : S.Eyes)
			{
				if (S.Device)
				{
					for (auto fb : sc.Framebuffers)
						vkl::vkDestroyFramebuffer(S.Device, fb, nullptr);
					for (auto v : sc.Views)
						vkl::vkDestroyImageView(S.Device, v, nullptr);
					if (sc.MsaaView) vkl::vkDestroyImageView(S.Device, sc.MsaaView, nullptr);
					if (sc.DepthView) vkl::vkDestroyImageView(S.Device, sc.DepthView, nullptr);
					if (sc.Msaa) vkl::vkDestroyImage(S.Device, sc.Msaa, nullptr);
					if (sc.Depth) vkl::vkDestroyImage(S.Device, sc.Depth, nullptr);
					if (sc.MsaaMem) vkl::vkFreeMemory(S.Device, sc.MsaaMem, nullptr);
					if (sc.DepthMem) vkl::vkFreeMemory(S.Device, sc.DepthMem, nullptr);
				}
				if (sc.Handle)
					xrDestroySwapchain(sc.Handle);
				sc = EyeSwapchain{};
			}
			if (S.Device)
			{
				if (S.Sampler) vkl::vkDestroySampler(S.Device, S.Sampler, nullptr);
				if (S.PipeOpaque) vkl::vkDestroyPipeline(S.Device, S.PipeOpaque, nullptr);
				if (S.PipeBlend) vkl::vkDestroyPipeline(S.Device, S.PipeBlend, nullptr);
				if (S.PipeOverlay) vkl::vkDestroyPipeline(S.Device, S.PipeOverlay, nullptr);
				if (S.PipeLayout) vkl::vkDestroyPipelineLayout(S.Device, S.PipeLayout, nullptr);
				if (S.DescPool) vkl::vkDestroyDescriptorPool(S.Device, S.DescPool, nullptr);
				if (S.SetLayout) vkl::vkDestroyDescriptorSetLayout(S.Device, S.SetLayout, nullptr);
				if (S.RenderPass) vkl::vkDestroyRenderPass(S.Device, S.RenderPass, nullptr);
				if (S.CmdPool) vkl::vkDestroyCommandPool(S.Device, S.CmdPool, nullptr);
			}
			for (int hand = 0; hand < 2; ++hand)
			{
				if (S.AimSpace[hand]) xrDestroySpace(S.AimSpace[hand]);
				if (S.GripSpace[hand]) xrDestroySpace(S.GripSpace[hand]);
			}
			if (S.ActionSet) xrDestroyActionSet(S.ActionSet);
			if (S.ViewSpace) xrDestroySpace(S.ViewSpace);
			if (S.AppSpace) xrDestroySpace(S.AppSpace);
			if (S.Session) xrDestroySession(S.Session);
			if (S.Device) vkl::vkDestroyDevice(S.Device, nullptr);
			if (S.VkInst) vkl::vkDestroyInstance(S.VkInst, nullptr);
			if (S.Instance) xrDestroyInstance(S.Instance);
			vkl::UnloadVulkanLibrary();

			std::string err = S.Error;
			S = State{};
			S.Error = err;
		}
	}

	// ==================================================================== public API

	bool Init(const Settings& settings)
	{
		if (S.Active)
			return true;
		S.Cfg = settings;
		if (auto shot = getenv("SCP_VR_SCREENSHOT"))
		{
			S.ShotPath = shot;
			auto frame = getenv("SCP_VR_SCREENSHOT_FRAME");
			S.ShotFrame = frame ? strtoull(frame, nullptr, 10) : 90;
		}

		bool ok = CreateInstance() && CreateVulkan() && CreateSession() && CreateSwapchains() &&
			CreatePipelines() && CreateFrameResources() && CreateActions();
		if (!ok)
		{
			DestroyAll();
			return false;
		}
		S.Active = true;
		S.LastInputTime = std::chrono::steady_clock::now();
		return true;
	}

	void Shutdown()
	{
		if (!S.Active)
			return;
		DestroyAll();
	}

	bool IsActive() { return S.Active; }
	const char* LastError() { return S.Error.c_str(); }
	bool IsSessionRunning() { return S.Active && S.Running; }
	bool IsSessionFocused() { return S.Active && S.Running && S.Focused; }
	Settings GetSettings() { return S.Cfg; }
	void SetSettings(const Settings& settings) { S.Cfg = settings; }
	void RequestRecenter() { S.NeedRecenter = true; }
	const char* InteractionProfileName() { return S.ProfileName.c_str(); }

	bool PollEvents()
	{
		if (!S.Active)
			return false;
		XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
		bool keepGoing = true;
		while (xrPollEvent(S.Instance, &ev) == XR_SUCCESS)
		{
			switch (ev.type)
			{
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				printf("VR: instance loss pending\n");
				keepGoing = false;
				break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
				{
					auto& sc = *reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
					S.SessionState = sc.state;
					S.Focused = sc.state == XR_SESSION_STATE_FOCUSED;
					switch (sc.state)
					{
					case XR_SESSION_STATE_READY:
						{
							XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
							bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
							if (XrOk(xrBeginSession(S.Session, &bi), "xrBeginSession"))
							{
								S.Running = true;
								S.NeedRecenter = true;
							}
							else
								keepGoing = false;
							break;
						}
					case XR_SESSION_STATE_STOPPING:
						xrEndSession(S.Session);
						S.Running = false;
						break;
					case XR_SESSION_STATE_EXITING:
					case XR_SESSION_STATE_LOSS_PENDING:
						keepGoing = false;
						break;
					default:
						break;
					}
					break;
				}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
				S.NeedRecenter = true;
				break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
				{
					XrInteractionProfileState st{XR_TYPE_INTERACTION_PROFILE_STATE};
					if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(S.Session, S.HandPath[1], &st)) &&
						st.interactionProfile != XR_NULL_PATH)
					{
						char buf[XR_MAX_PATH_LENGTH];
						uint32_t len = 0;
						if (XR_SUCCEEDED(xrPathToString(S.Instance, st.interactionProfile, sizeof buf, &len, buf)))
						{
							S.ProfileName = buf;
							printf("VR: controller profile %s\n", buf);
						}
					}
					break;
				}
			default:
				break;
			}
			ev = {XR_TYPE_EVENT_DATA_BUFFER};
		}
		return keepGoing;
	}

	bool BeginFrame()
	{
		if (!S.Active || !S.Running || S.FrameBegun)
			return false;
		S.FrameState = {XR_TYPE_FRAME_STATE};
		XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
		if (!XrOk(xrWaitFrame(S.Session, &wi, &S.FrameState), "xrWaitFrame"))
			return false;
		XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
		auto r = xrBeginFrame(S.Session, &bi);
		if (XR_FAILED(r))
		{
			XrOk(r, "xrBeginFrame");
			return false;
		}
		S.FrameBegun = true;
		return true;
	}

	void SyncInput(Input& out, bool uiVisible, bool paused)
	{
		out = Input{};
		if (!S.FrameBegun)
			return;

		auto now = std::chrono::steady_clock::now();
		float dt = std::chrono::duration<float>(now - S.LastInputTime).count();
		S.LastInputTime = now;
		dt = Clampf(dt, 0.0f, 0.1f);

		XrTime t = S.FrameState.predictedDisplayTime;
		Pose head;
		S.HeadValid = Locate(S.ViewSpace, t, head);
		if (S.HeadValid)
			S.Head = head;
		for (int hand = 0; hand < 2; ++hand)
		{
			S.AimValid[hand] = Locate(S.AimSpace[hand], t, S.Aim[hand]);
			S.GripValid[hand] = Locate(S.GripSpace[hand], t, S.Grip[hand]);
		}
		if (S.NeedRecenter)
			DoRecenter();

		// Table shake spring.
		{
			const float k = 900.0f, c = 28.0f;
			int steps = 4;
			float h = dt / steps;
			for (int i = 0; i < steps; ++i)
			{
				Vec3 acc = S.Shake * (-k) - S.ShakeVel * c;
				S.ShakeVel += acc * h;
				S.Shake += S.ShakeVel * h;
			}
		}

		ComputePanel(uiVisible);

		bool focused = S.Focused;
		if (focused)
		{
			XrActiveActionSet aas{S.ActionSet, XR_NULL_PATH};
			XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
			si.countActiveActionSets = 1;
			si.activeActionSets = &aas;
			if (XR_FAILED(xrSyncActions(S.Session, &si)))
				focused = false;
		}

		S.PointerHit = false;
		// The plunger knob springs back to rest once let go.
		if (S.PlungerHand < 0)
			S.PlungerVisual = std::max(0.0f, S.PlungerVisual - dt * 12.0f);
		if (!focused)
		{
			S.PlungerHand = -1;
			S.HandFlip[0] = S.HandFlip[1] = -1;
			S.Moving = false;
			S.FlipPressed[0] = S.FlipPressed[1] = false;
			return;
		}

		bool flipL = GetBool(S.FlipL), flipR = GetBool(S.FlipR);
		bool flipLChanged = false, flipRChanged = false;
		{
			XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
			XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
			gi.action = S.FlipL;
			if (XR_SUCCEEDED(xrGetActionStateBoolean(S.Session, &gi, &st)))
				flipLChanged = st.changedSinceLastSync && st.currentState;
			st = {XR_TYPE_ACTION_STATE_BOOLEAN};
			gi.action = S.FlipR;
			if (XR_SUCCEEDED(xrGetActionStateBoolean(S.Session, &gi, &st)))
				flipRChanged = st.changedSinceLastSync && st.currentState;
		}

		Vec2 move = GetVec2(S.Move);
		Vec2 look = GetVec2(S.Look);

		out.PausePressed = Pressed(S.Pause);
		out.MenuPressed = Pressed(S.Menu);
		out.BackPressed = Pressed(S.Back);
		if (Pressed(S.Recenter))
		{
			S.NeedRecenter = true;
			HapticBoth(0.3f, 0.05f);
		}
		if (Pressed(S.ViewToggle))
		{
			S.Cfg.Mode = S.Cfg.Mode == ViewMode::Cabinet ? ViewMode::Theater : ViewMode::Cabinet;
			HapticBoth(0.25f, 0.04f);
		}

		// Hold "new game" to avoid wiping a game by accident.
		if (GetBool(S.NewGame))
		{
			S.NewGameHeld += dt;
			if (S.NewGameHeld > 0.8f && !S.NewGameFired)
			{
				S.NewGameFired = true;
				out.NewGameTriggered = true;
				HapticBoth(0.6f, 0.12f);
			}
		}
		else
		{
			S.NewGameHeld = 0;
			S.NewGameFired = false;
		}

		// --- grips
		bool grab[2], grabPressed[2];
		for (int hand = 0; hand < 2; ++hand)
		{
			grab[hand] = GetBoolHand(S.Grab, hand) && S.GripValid[hand];
			grabPressed[hand] = grab[hand] && !S.GrabHeld[hand];
			S.GrabHeld[hand] = grab[hand];
		}

		// While paused (or in a menu), grabbing with both hands moves and turns the table or screen.
		const bool canMove = paused || uiVisible;
		if (canMove && grab[0] && grab[1])
		{
			Vec3 a = S.Grip[0].position, b = S.Grip[1].position;
			Vec3 mid = (a + b) * 0.5f, d = b - a;
			float angle = std::atan2(-d.z, d.x);
			float dist = std::max(0.05f, Length(Vec3{d.x, 0, d.z}));
			if (!S.Moving)
			{
				S.Moving = true;
				S.MoveStartMid = mid;
				S.MoveStartAnchor = S.AnchorPos;
				S.MoveStartAngle = angle;
				S.MoveStartYaw = S.AnchorYaw;
				S.MoveStartDist = dist;
				S.MoveStartWidth = S.Cfg.TheaterWidth;
				HapticBoth(0.35f, 0.04f);
			}
			float dYaw = angle - S.MoveStartAngle;
			Quat q = Quat::AxisAngle({0, 1, 0}, dYaw);
			S.AnchorYaw = S.MoveStartYaw + dYaw;
			S.AnchorPos = mid + q.Rotate(S.MoveStartAnchor - S.MoveStartMid);
			if (S.Cfg.Mode == ViewMode::Theater)
				S.Cfg.TheaterWidth = Clampf(S.MoveStartWidth * dist / S.MoveStartDist, 1.0f, 6.0f);
			ComputePanel(uiVisible);
		}
		else if (S.Moving)
		{
			S.Moving = false;
			HapticBoth(0.2f, 0.03f);
		}

		// While playing, grips grab the cabinet's flipper buttons and plunger.
		const bool canGrab = !canMove && S.ControlsValid && S.Cfg.Mode == ViewMode::Cabinet;
		for (int hand = 0; hand < 2; ++hand)
		{
			if (!grab[hand] || !canGrab)
			{
				if (S.PlungerHand == hand)
				{
					S.PlungerHand = -1;
					if (S.PlungerPull > 0.05f)
						Haptic(static_cast<Hand>(hand), 0.4f + 0.6f * S.PlungerPull, 0.05f);
				}
				S.HandFlip[hand] = -1;
				continue;
			}
			const Vec3 p = S.Grip[hand].position;
			if (grabPressed[hand])
			{
				const Vec3 knob = S.PlungerRest + S.PlungerAxis * (S.PlungerVisual * S.PlungerTravel);
				int nearest = Length(p - S.FlipButton[0]) < Length(p - S.FlipButton[1]) ? 0 : 1;
				if (S.PlungerHand < 0 && Length(p - knob) < 0.09f)
				{
					S.PlungerHand = hand;
					S.PlungerGrabOffset = Dot(p - S.PlungerRest, S.PlungerAxis) - S.PlungerVisual * S.PlungerTravel;
					Haptic(static_cast<Hand>(hand), 0.3f, 0.02f);
				}
				else if (Length(p - S.FlipButton[nearest]) < 0.12f)
				{
					S.HandFlip[hand] = nearest;
				}
			}
			if (S.PlungerHand == hand)
			{
				float pull = (Dot(p - S.PlungerRest, S.PlungerAxis) - S.PlungerGrabOffset) / S.PlungerTravel;
				pull = Clampf(pull, 0.0f, 1.0f);
				// a faint tick as the spring compresses
				if (static_cast<int>(pull * 8) != static_cast<int>(S.PlungerPull * 8))
					Haptic(static_cast<Hand>(hand), 0.12f + 0.3f * pull, 0.01f);
				S.PlungerPull = pull;
				S.PlungerVisual = pull;
			}
		}
		if (S.PlungerHand >= 0)
		{
			out.PlungerGrabbed = true;
			out.PlungerPull = S.PlungerPull;
		}
		else
			S.PlungerPull = 0;

		if (uiVisible)
		{
			S.FlipPressed[0] = S.FlipPressed[1] = false;
			// Laser pointer drives the game's menus. Either trigger clicks; the last one used aims.
			if (flipLChanged)
				S.PointerHand = 0;
			if (flipRChanged)
				S.PointerHand = 1;
			int ph = S.PointerHand;
			if (!S.AimValid[ph] && S.AimValid[1 - ph])
				ph = S.PointerHand = 1 - ph;
			float u, v;
			if (S.AimValid[ph] && S.UiPanel.Visible && IntersectPanel(S.Aim[ph], u, v, S.PointerHitPos))
			{
				S.PointerHit = true;
				out.PointerValid = true;
				out.PointerX = Clampf(u, 0, 1) * S.LastFrameW;
				out.PointerY = Clampf(v, 0, 1) * S.LastFrameH;
			}
			out.PointerDown = ph == 0 ? flipL : flipR;
			out.Scroll = std::fabs(look.y) > 0.3f ? look.y * dt * 12.0f : 0.0f;
			return;
		}

		out.FlipperLeft = flipL || S.HandFlip[0] == 0 || S.HandFlip[1] == 0;
		out.FlipperRight = flipR || S.HandFlip[0] == 1 || S.HandFlip[1] == 1;
		out.Plunger = GetBool(S.Plunger);
		S.FlipPressed[0] = out.FlipperLeft;
		S.FlipPressed[1] = out.FlipperRight;

		// Left stick: flick to nudge, with hysteresis.
		auto hyst = [](bool& state, float v)
		{
			if (!state && v > 0.75f)
				state = true;
			else if (state && v < 0.45f)
				state = false;
			return state;
		};
		hyst(S.StickNudgeL, -move.x);
		hyst(S.StickNudgeR, move.x);
		hyst(S.StickNudgeUp, move.y);
		out.NudgeLeft = S.StickNudgeL || GetBool(S.NudgeL);
		out.NudgeRight = S.StickNudgeR || GetBool(S.NudgeR);
		out.NudgeUp = S.StickNudgeUp || GetBool(S.NudgeUp);

		// Right stick: move the table nearer / further and change its tilt (or the theater screen size).
		float dead = 0.25f;
		float ly = std::fabs(look.y) > dead ? look.y : 0.0f;
		float lx = std::fabs(look.x) > dead ? look.x : 0.0f;
		if (S.Cfg.Mode == ViewMode::Cabinet)
		{
			S.Cfg.Distance = Clampf(S.Cfg.Distance + ly * dt * 0.35f, 0.05f, 1.2f);
			S.Cfg.TiltDeg = Clampf(S.Cfg.TiltDeg + lx * dt * 22.0f, 6.0f, 70.0f);
		}
		else
		{
			S.Cfg.TheaterWidth = Clampf(S.Cfg.TheaterWidth + (ly + lx) * dt * 0.9f, 1.0f, 6.0f);
		}
	}

	void EndFrame(const FrameImage& image, const FrameLayout& layout, bool uiVisible)
	{
		if (!S.FrameBegun)
			return;
		S.FrameBegun = false;

		XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
		ei.displayTime = S.FrameState.predictedDisplayTime;
		ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

		XrCompositionLayerProjectionView projViews[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
		                                              {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
		XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
		const XrCompositionLayerBaseHeader* layers[1] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};

		XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
		bool render = S.FrameState.shouldRender == XR_TRUE;
		if (render)
		{
			XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
			li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			li.displayTime = S.FrameState.predictedDisplayTime;
			li.space = S.AppSpace;
			XrViewState vs{XR_TYPE_VIEW_STATE};
			uint32_t count = 0;
			if (XR_FAILED(xrLocateViews(S.Session, &li, &vs, 2, &count, views)) ||
				!(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) ||
				!(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
				render = false;
		}

		if (render)
		{
			// Debug: tilt the rendered view (lets screenshots look down at the table on a simulated HMD).
			static const char* dbgPitch = getenv("SCP_VR_DEBUG_PITCH");
			if (dbgPitch)
			{
				float pitch = Deg(static_cast<float>(atof(dbgPitch)));
				Pose v0 = ToPose(views[0].pose), v1 = ToPose(views[1].pose);
				Vec3 c = (v0.position + v1.position) * 0.5f;
				Vec3 right = Normalize(v1.position - v0.position);
				if (Length(right) < 0.5f)
					right = v0.orientation.Rotate({1, 0, 0});
				Quat q = Quat::AxisAngle(right, -pitch);
				for (auto& view : views)
				{
					Pose p = ToPose(view.pose);
					p.position = c + q.Rotate(p.position - c);
					p.orientation = q * p.orientation;
					view.pose.position = {p.position.x, p.position.y, p.position.z};
					view.pose.orientation = {p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w};
				}
			}

			auto& f = S.Frames[S.FrameIndex];
			vkl::vkWaitForFences(S.Device, 1, &f.Fence, VK_TRUE, UINT64_MAX);
			if (f.ShotPending)
			{
				WriteScreenshot(f);
				f.ShotPending = false;
			}
			vkl::vkResetFences(S.Device, 1, &f.Fence);

			// Upload the game frame.
			bool haveImage = image.Pixels && image.Width > 0 && image.Height > 0;
			int w = haveImage ? image.Width : 4, h = haveImage ? image.Height : 4;
			if (haveImage)
			{
				S.LastFrameW = image.Width;
				S.LastFrameH = image.Height;
				memcpy(S.LastGameRect, layout.Game, sizeof S.LastGameRect);
			}
			const bool dual = haveImage && image.GamePixels;
			S.FrameDual = dual;
			bool texOk = EnsureFrameTexture(f, w, h * 2);
			if (texOk)
			{
				auto dst = static_cast<uint8_t*>(f.StagingPtr);
				if (haveImage)
				{
					// Alpha is ignored by the screen shader, so a straight copy is enough.
					const size_t rowBytes = static_cast<size_t>(w) * 4;
					auto copyFrame = [&](const void* pixels, uint8_t* out)
					{
						auto src = static_cast<const uint8_t*>(pixels);
						if (static_cast<size_t>(image.Pitch) == rowBytes)
							memcpy(out, src, rowBytes * h);
						else
							for (int y = 0; y < h; ++y)
								memcpy(out + y * rowBytes, src + static_cast<size_t>(y) * image.Pitch, rowBytes);
					};
					copyFrame(image.Pixels, dst);
					if (dual)
						copyFrame(image.GamePixels, dst + rowBytes * h);
				}
				else
					memset(dst, 0, static_cast<size_t>(f.StagingSize));
			}

			// Build geometry and upload vertices.
			BuildScene(layout, uiVisible);
			size_t vbytes = S.Scene.Vertices.size() * sizeof(Vertex);
			if (vbytes > f.VbufSize)
			{
				DestroyBuffer(f.Vbuf, f.VbufMem);
				f.VbufSize = vbytes * 2;
				CreateBuffer(f.VbufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, f.Vbuf, f.VbufMem, &f.VbufPtr);
			}
			if (f.VbufPtr)
				memcpy(f.VbufPtr, S.Scene.Vertices.data(), vbytes);

			// Acquire swapchain images.
			uint32_t imgIndex[2] = {0, 0};
			bool acquired = true;
			for (int eye = 0; eye < 2 && acquired; ++eye)
			{
				XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
				acquired = XR_SUCCEEDED(xrAcquireSwapchainImage(S.Eyes[eye].Handle, &ai, &imgIndex[eye]));
				if (acquired)
				{
					XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
					wi.timeout = XR_INFINITE_DURATION;
					acquired = XR_SUCCEEDED(xrWaitSwapchainImage(S.Eyes[eye].Handle, &wi));
				}
			}

			VkCommandBuffer cmd = f.Cmd;
			vkl::vkResetCommandBuffer(cmd, 0);
			VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
			cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkl::vkBeginCommandBuffer(cmd, &cbi);

			if (texOk)
			{
				Barrier(cmd, f.Tex, 0, f.Mips,
				        f.TexReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
				        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
				VkBufferImageCopy copy{};
				copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				copy.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(dual ? h * 2 : h), 1};
				vkl::vkCmdCopyBufferToImage(cmd, f.Staging, f.Tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
				int mw = w, mh = h * 2;
				for (uint32_t mip = 1; mip < f.Mips; ++mip)
				{
					Barrier(cmd, f.Tex, mip - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
					        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					        VK_PIPELINE_STAGE_TRANSFER_BIT);
					int nw = std::max(1, mw / 2), nh = std::max(1, mh / 2);
					VkImageBlit blit{};
					blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
					blit.srcOffsets[1] = {mw, mh, 1};
					blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
					blit.dstOffsets[1] = {nw, nh, 1};
					vkl::vkCmdBlitImage(cmd, f.Tex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.Tex,
					                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
					mw = nw;
					mh = nh;
				}
				if (f.Mips > 1)
					Barrier(cmd, f.Tex, 0, f.Mips - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
					        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				Barrier(cmd, f.Tex, f.Mips - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
				        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				f.TexReady = true;
			}

			bool wantShot = !S.ShotPath.empty() && S.FrameCounter == S.ShotFrame;
			float timeSec = static_cast<float>(static_cast<double>(S.FrameState.predictedDisplayTime % 1000000000000LL) * 1e-9);

			if (acquired && texOk && f.VbufPtr)
			{
				for (int eye = 0; eye < 2; ++eye)
				{
					auto& sc = S.Eyes[eye];
					VkClearValue clears[3]{};
					clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
					clears[1].depthStencil = {1.0f, 0};
					clears[2].color = clears[0].color;
					VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
					rbi.renderPass = S.RenderPass;
					rbi.framebuffer = sc.Framebuffers[imgIndex[eye]];
					rbi.renderArea.extent = {static_cast<uint32_t>(sc.Width), static_cast<uint32_t>(sc.Height)};
					rbi.clearValueCount = S.Samples != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
					rbi.pClearValues = clears;
					vkl::vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

					VkViewport vp{0, 0, static_cast<float>(sc.Width), static_cast<float>(sc.Height), 0, 1};
					VkRect2D sci{{0, 0}, {static_cast<uint32_t>(sc.Width), static_cast<uint32_t>(sc.Height)}};
					vkl::vkCmdSetViewport(cmd, 0, 1, &vp);
					vkl::vkCmdSetScissor(cmd, 0, 1, &sci);
					VkDeviceSize off = 0;
					vkl::vkCmdBindVertexBuffers(cmd, 0, 1, &f.Vbuf, &off);
					vkl::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, S.PipeLayout, 0, 1, &f.Set,
					                             0, nullptr);

					Pose eyePose = ToPose(views[eye].pose);
					Mat4 proj = Mat4::ProjectionFov(views[eye].fov.angleLeft, views[eye].fov.angleRight,
					                                views[eye].fov.angleUp, views[eye].fov.angleDown, NearZ, FarZ);
					Mat4 vp4 = proj * Mat4::ViewFromPose(eyePose);

					PushConstants pc{};
					memcpy(pc.ViewProj, vp4.m, sizeof pc.ViewProj);
					pc.EyePos[0] = eyePose.position.x;
					pc.EyePos[1] = eyePose.position.y;
					pc.EyePos[2] = eyePose.position.z;
					pc.EyePos[3] = timeSec;
					pc.Glow[0] = S.GlowCenter.x;
					pc.Glow[1] = S.GlowCenter.y;
					pc.Glow[2] = S.GlowCenter.z;
					pc.Glow[3] = S.GlowStrength;

					for (int pass = 0; pass < 3; ++pass)
					{
						vkl::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
						                       pass == 0 ? S.PipeOpaque : pass == 1 ? S.PipeBlend : S.PipeOverlay);
						for (auto& item : S.Scene.Items)
						{
							const int itemPass = item.Overlay ? 2 : item.Blend ? 1 : 0;
							if (itemPass != pass || item.Count == 0)
								continue;
							pc.Params[0] = static_cast<float>(item.Mat);
							pc.Params[1] = item.Emissive;
							pc.Params[2] = item.Brightness;
							pc.Params[3] = item.EdgeFade;
							memcpy(pc.UvRect, item.UvRect, sizeof pc.UvRect);
							vkl::vkCmdPushConstants(cmd, S.PipeLayout,
							                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
							                        sizeof pc, &pc);
							vkl::vkCmdDraw(cmd, item.Count, 1, item.First, 0);
						}
					}
					vkl::vkCmdEndRenderPass(cmd);

					if (eye == 0 && wantShot)
					{
						VkImage img = sc.Images[imgIndex[eye]].image;
						if (!f.Shot)
						{
							CreateBuffer(static_cast<VkDeviceSize>(sc.Width) * sc.Height * 4,
							             VK_BUFFER_USAGE_TRANSFER_DST_BIT, f.Shot, f.ShotMem, &f.ShotPtr);
						}
						Barrier(cmd, img, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						        VK_PIPELINE_STAGE_TRANSFER_BIT);
						VkBufferImageCopy c{};
						c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
						c.imageExtent = {static_cast<uint32_t>(sc.Width), static_cast<uint32_t>(sc.Height), 1};
						vkl::vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f.Shot, 1, &c);
						Barrier(cmd, img, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
						        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
						        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
						f.ShotPending = true;
						f.ShotW = sc.Width;
						f.ShotH = sc.Height;
					}
				}
			}

			vkl::vkEndCommandBuffer(cmd);
			VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
			si.commandBufferCount = 1;
			si.pCommandBuffers = &cmd;
			vkl::vkQueueSubmit(S.Queue, 1, &si, f.Fence);

			for (int eye = 0; eye < 2; ++eye)
			{
				XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
				xrReleaseSwapchainImage(S.Eyes[eye].Handle, &ri);
				projViews[eye].pose = views[eye].pose;
				projViews[eye].fov = views[eye].fov;
				projViews[eye].subImage.swapchain = S.Eyes[eye].Handle;
				projViews[eye].subImage.imageRect.extent = {S.Eyes[eye].Width, S.Eyes[eye].Height};
				projViews[eye].subImage.imageArrayIndex = 0;
			}
			if (wantShot && f.ShotPending)
			{
				// Make sure the screenshot is written even if the app exits right after.
				vkl::vkWaitForFences(S.Device, 1, &f.Fence, VK_TRUE, UINT64_MAX);
				WriteScreenshot(f);
				f.ShotPending = false;
			}

			layer.space = S.AppSpace;
			layer.viewCount = 2;
			layer.views = projViews;
			ei.layerCount = acquired ? 1 : 0;
			ei.layers = layers;
			S.FrameIndex = (S.FrameIndex + 1) % FramesInFlight;
			S.FrameCounter++;
		}

		XrOk(xrEndFrame(S.Session, &ei), "xrEndFrame");
	}

	void Haptic(Hand hand, float amplitude, float seconds)
	{
		if (!S.Active || !S.Running || !S.Focused)
			return;
		XrHapticVibration vib{XR_TYPE_HAPTIC_VIBRATION};
		vib.amplitude = Clampf(amplitude, 0, 1);
		vib.duration = static_cast<XrDuration>(seconds * 1e9);
		vib.frequency = XR_FREQUENCY_UNSPECIFIED;
		XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
		hi.action = S.HapticOut;
		hi.subactionPath = S.HandPath[hand == Hand::Left ? 0 : 1];
		xrApplyHapticFeedback(S.Session, &hi, reinterpret_cast<XrHapticBaseHeader*>(&vib));
	}

	void HapticBoth(float amplitude, float seconds)
	{
		Haptic(Hand::Left, amplitude, seconds);
		Haptic(Hand::Right, amplitude, seconds);
	}

	void ShakeTable(float dx, float dz)
	{
		S.ShakeVel += Vec3{dx, 0, dz} * 1.0f;
	}
}
