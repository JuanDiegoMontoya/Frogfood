// dear imgui: Renderer Backend for Vulkan
// This needs to be used along with a Platform Backend (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [x] Renderer: User texture binding. Use 'VkDescriptorSet' as ImTextureID. Read the FAQ about ImTextureID! See https://github.com/ocornut/imgui/pull/914 for discussions.
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.
//  [x] Renderer: Multi-viewport / platform windows. With issues (flickering when creating a new viewport).

// Important: on 32-bit systems, user texture binding is only supported if your imconfig file has '#define ImTextureID ImU64'.
// See imgui_impl_vulkan.cpp file for details.

// The aim of imgui_impl_vulkan.h/.cpp is to be usable in your engine without any modification.
// IF YOU FEEL YOU NEED TO MAKE ANY CHANGE TO THIS CODE, please share them and your feedback at https://github.com/ocornut/imgui/

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important note to the reader who wish to integrate imgui_impl_vulkan.cpp/.h in their own engine/app.
// - Common ImGui_ImplVulkan_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper ImGui_ImplVulkanH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in imgui_impl_vulkan.h.

#pragma once
#ifndef IMGUI_DISABLE
  #include "imgui.h" // IMGUI_IMPL_API
#include "Fvog/Pipeline2.h"

// [Configuration] in order to use a custom Vulkan function loader:
// (1) You'll need to disable default Vulkan function prototypes.
//     We provide a '#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES' convenience configuration flag.
//     In order to make sure this is visible from the imgui_impl_vulkan.cpp compilation unit:
//     - Add '#define IMGUI_IMPL_VULKAN_NO_PROTOTYPES' in your imconfig.h file
//     - Or as a compilation flag in your build system
//     - Or uncomment here (not recommended because you'd be modifying imgui sources!)
//     - Do not simply add it in a .cpp file!
// (2) Call ImGui_ImplFvog_LoadFunctions() before ImGui_ImplFvog_Init() with your custom function.
// If you have no idea what this is, leave it alone!
// #define IMGUI_IMPL_VULKAN_NO_PROTOTYPES

// Convenience support for Volk
// (you can also technically use IMGUI_IMPL_VULKAN_NO_PROTOTYPES + wrap Volk via ImGui_ImplFvog_LoadFunctions().)
// #define IMGUI_IMPL_VULKAN_USE_VOLK

  #if defined(IMGUI_IMPL_VULKAN_NO_PROTOTYPES) && !defined(VK_NO_PROTOTYPES)
    #define VK_NO_PROTOTYPES
  #endif
  #if defined(VK_USE_PLATFORM_WIN32_KHR) && !defined(NOMINMAX)
    #define NOMINMAX
  #endif

  // Vulkan includes
  #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    #include <Volk/volk.h>
  #else
    #include <vulkan/vulkan_core.h>
  #endif
  #if defined(VK_VERSION_1_3) || defined(VK_KHR_dynamic_rendering)
    #define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
  #endif

// Initialization data, for ImGui_ImplFvog_Init()
// - VkDescriptorPool should be created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
//   and must contain a pool size large enough to hold an ImGui VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER descriptor.
// - When using dynamic rendering, set UseDynamicRendering=true and fill PipelineRenderingCreateInfo structure.
// [Please zero-clear before use!]
struct ImGui_ImplFvog_InitInfo
{
  VkInstance Instance;
  VkPhysicalDevice PhysicalDevice;
  Fvog::Device* Device;
  uint32_t QueueFamily;
  VkQueue Queue;
  VkDescriptorPool DescriptorPool;   // See requirements in note above
  VkRenderPass RenderPass;           // Ignored if using dynamic rendering
  uint32_t MinImageCount;            // >= 2
  uint32_t ImageCount;               // >= MinImageCount
  VkSampleCountFlagBits MSAASamples; // 0 defaults to VK_SAMPLE_COUNT_1_BIT

  // (Optional) Allocation, Debugging
  void (*CheckVkResultFn)(VkResult err);
  VkDeviceSize MinAllocationSize; // Minimum allocation size. Set to 1024*1024 to satisfy zealous best practices validation layer and waste a little memory.
};

// Called by user code
IMGUI_IMPL_API bool ImGui_ImplFvog_Init(ImGui_ImplFvog_InitInfo* info);
IMGUI_IMPL_API void ImGui_ImplFvog_Shutdown();
IMGUI_IMPL_API void ImGui_ImplFvog_NewFrame();
IMGUI_IMPL_API void ImGui_ImplFvog_RenderDrawData(ImDrawData* draw_data, VkCommandBuffer command_buffer, VkSurfaceFormatKHR format, float maxDisplayNits);
IMGUI_IMPL_API void ImGui_ImplFvog_CreateFontsTexture();
IMGUI_IMPL_API void ImGui_ImplFvog_DestroyFontsTexture();

// Optional: load Vulkan functions with a custom function loader
// This is only useful with IMGUI_IMPL_VULKAN_NO_PROTOTYPES / VK_NO_PROTOTYPES
IMGUI_IMPL_API bool ImGui_ImplFvog_LoadFunctions(PFN_vkVoidFunction (*loader_func)(const char* function_name, void* user_data), void* user_data = nullptr);

#define IMGUI_COLOR_SPACE_sRGB_NONLINEAR 0
#define IMGUI_COLOR_SPACE_scRGB_LINEAR   1
#define IMGUI_COLOR_SPACE_HDR10_ST2084   2
#define IMGUI_COLOR_SPACE_BT2020_LINEAR  3
#define IMGUI_COLOR_SPACE_sRGB_LINEAR    4

// Combined texture-sampler type that can be stored in ImTextureID
struct ImTextureSampler
{
  constexpr static uint32_t DefaultSamplerIndex = 65535;
  constexpr static uint32_t DefaultColorSpace = IMGUI_COLOR_SPACE_sRGB_NONLINEAR;

  ImTextureSampler(uint32_t textureIndex, uint32_t samplerIndex = DefaultSamplerIndex, uint32_t colorSpace = DefaultColorSpace)
  {
    SetTextureIndex(textureIndex);
    SetSamplerIndex(samplerIndex);
    SetColorSpace(colorSpace);
  }

  explicit ImTextureSampler(ImTextureID id) : id_(id) {}

  [[nodiscard]] uint32_t GetTextureIndex() const
  {
    return static_cast<uint32_t>(id_);
  }

  [[nodiscard]] uint32_t GetSamplerIndex() const
  {
    return static_cast<uint32_t>((id_ & 0x0000FFFF00000000) >> 32ull);
  }

  [[nodiscard]] uint32_t GetColorSpace() const
  {
    return static_cast<uint32_t>((id_ & 0xFFFF000000000000) >> 48ull);
  }

  [[nodiscard]] bool IsSamplerDefault() const
  {
    return GetSamplerIndex() == DefaultSamplerIndex;
  }

  void SetTextureIndex(uint32_t textureIndex)
  {
    id_ = (id_ & 0xFFFFFFFF00000000) | textureIndex;
  }

  void SetSamplerIndex(uint32_t samplerIndex)
  {
    id_ = (id_ & 0xFFFF0000FFFFFFFF) | ((uint64_t)samplerIndex << 32ull);
  }

  void SetColorSpace(uint32_t colorSpace)
  {
    id_ = (id_ & 0x0000FFFFFFFFFFFF) | ((uint64_t)colorSpace << 48ull);
  }

  operator ImTextureID() const
  {
    return id_;
  }

private:
  // sampler index is stored in the MSBs, texture index is stored in LSBs
  ImTextureID id_{};
};

#endif // #ifndef IMGUI_DISABLE
