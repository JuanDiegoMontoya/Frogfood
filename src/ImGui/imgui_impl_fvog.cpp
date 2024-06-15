// dear imgui: Renderer Backend for Vulkan
// This needs to be used along with a Platform Backend (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [x] Renderer: User texture binding. Use 'VkDescriptorSet' as ImTextureID. Read the FAQ about ImTextureID! See https://github.com/ocornut/imgui/pull/914 for discussions.
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.
//  [x] Renderer: Multi-viewport / platform windows. With issues (flickering when creating a new viewport).

// Important: on 32-bit systems, user texture binding is only supported if your imconfig file has '#define ImTextureID ImU64'.
// This is because we need ImTextureID to carry a 64-bit value and by default ImTextureID is defined as void*.
// To build this on 32-bit systems and support texture changes:
// - [Solution 1] IDE/msbuild: in "Properties/C++/Preprocessor Definitions" add 'ImTextureID=ImU64' (this is what we do in our .vcxproj files)
// - [Solution 2] IDE/msbuild: in "Properties/C++/Preprocessor Definitions" add 'IMGUI_USER_CONFIG="my_imgui_config.h"' and inside 'my_imgui_config.h' add
// '#define ImTextureID ImU64' and as many other options as you like.
// - [Solution 3] IDE/msbuild: edit imconfig.h and add '#define ImTextureID ImU64' (prefer solution 2 to create your own config file!)
// - [Solution 4] command-line: add '/D ImTextureID=ImU64' to your cl.exe command-line (this is what we do in our batch files)

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

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2024-XX-XX: Platform: Added support for multiple windows via the ImGuiPlatformIO interface.
//  2024-04-19: Vulkan: Added convenience support for Volk via IMGUI_IMPL_VULKAN_USE_VOLK define (you can also use IMGUI_IMPL_VULKAN_NO_PROTOTYPES + wrap Volk
//  via ImGui_ImplVulkan_LoadFunctions().) 2024-02-14: *BREAKING CHANGE*: Moved RenderPass parameter from ImGui_ImplVulkan_Init() function to
//  ImGui_ImplVulkan_InitInfo structure. Not required when using dynamic rendering. 2024-02-12: *BREAKING CHANGE*: Dynamic rendering now require filling
//  PipelineRenderingCreateInfo structure. 2024-01-19: Vulkan: Fixed vkAcquireNextImageKHR() validation errors in VulkanSDK 1.3.275 by allocating one extra
//  semaphore than in-flight frames. (#7236) 2024-01-11: Vulkan: Fixed vkMapMemory() calls unnecessarily using full buffer size (#3957). Fixed MinAllocationSize
//  handing (#7189). 2024-01-03: Vulkan: Added MinAllocationSize field in ImGui_ImplVulkan_InitInfo to workaround zealous "best practice" validation layer.
//  (#7189, #4238) 2024-01-03: Vulkan: Stopped creating command pools with VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT as we don't reset them. 2023-11-29:
//  Vulkan: Fixed mismatching allocator passed to vkCreateCommandPool() vs vkDestroyCommandPool(). (#7075) 2023-11-10: *BREAKING CHANGE*: Removed parameter from
//  ImGui_ImplVulkan_CreateFontsTexture(): backend now creates its own command-buffer to upload fonts.
//              *BREAKING CHANGE*: Removed ImGui_ImplVulkan_DestroyFontUploadObjects() which is now unnecessary as we create and destroy those objects in the
//              backend. ImGui_ImplVulkan_CreateFontsTexture() is automatically called by NewFrame() the first time. You can call
//              ImGui_ImplVulkan_CreateFontsTexture() again to recreate the font atlas texture. Added ImGui_ImplVulkan_DestroyFontsTexture() but you probably
//              never need to call this.
//  2023-07-04: Vulkan: Added optional support for VK_KHR_dynamic_rendering. User needs to set init_info->UseDynamicRendering = true and
//  init_info->ColorAttachmentFormat. 2023-01-02: Vulkan: Fixed sampler passed to ImGui_ImplVulkan_AddTexture() not being honored + removed a bunch of duplicate
//  code. 2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11. 2022-10-04: Vulkan: Added experimental ImGui_ImplVulkan_RemoveTexture() for
//  api symmetry. (#914, #5738). 2022-01-20: Vulkan: Added support for ImTextureID as VkDescriptorSet. User need to call ImGui_ImplVulkan_AddTexture(). Building
//  for 32-bit targets requires '#define ImTextureID ImU64'. (#914). 2021-10-15: Vulkan: Call vkCmdSetScissor() at the end of render a full-viewport to reduce
//  likehood of issues with people using VK_DYNAMIC_STATE_SCISSOR in their app without calling vkCmdSetScissor() explicitly every frame. 2021-06-29: Reorganized
//  backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX). 2021-03-22: Vulkan: Fix
//  mapped memory validation error when buffer sizes are not multiple of VkPhysicalDeviceLimits::nonCoherentAtomSize. 2021-02-18: Vulkan: Change blending
//  equation to preserve alpha in output buffer. 2021-01-27: Vulkan: Added support for custom function load and IMGUI_IMPL_VULKAN_NO_PROTOTYPES by using
//  ImGui_ImplVulkan_LoadFunctions(). 2020-11-11: Vulkan: Added support for specifying which subpass to reference during VkPipeline creation. 2020-09-07:
//  Vulkan: Added VkPipeline parameter to ImGui_ImplVulkan_RenderDrawData (default to one passed to ImGui_ImplVulkan_Init). 2020-05-04: Vulkan: Fixed crash if
//  initial frame has no vertices. 2020-04-26: Vulkan: Fixed edge case where render callbacks wouldn't be called if the ImDrawData didn't have vertices. 2019-08-01:
//  Vulkan: Added support for specifying multisample count. Set ImGui_ImplVulkan_InitInfo::MSAASamples to one of the VkSampleCountFlagBits values to use,
//  default is non-multisampled as before. 2019-05-29: Vulkan: Added support for large mesh (64K+ vertices), enable ImGuiBackendFlags_RendererHasVtxOffset flag.
//  2019-04-30: Vulkan: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2019-04-04: *BREAKING CHANGE*: Vulkan: Added ImageCount/MinImageCount fields in ImGui_ImplVulkan_InitInfo, required for initialization (was previously a
//  hard #define IMGUI_VK_QUEUED_FRAMES 2). Added ImGui_ImplVulkan_SetMinImageCount(). 2019-04-04: Vulkan: Added VkInstance argument to
//  ImGui_ImplVulkanH_CreateWindow() optional helper. 2019-04-04: Vulkan: Avoid passing negative coordinates to vkCmdSetScissor, which debug validation layers
//  do not like. 2019-04-01: Vulkan: Support for 32-bit index buffer (#define ImDrawIdx unsigned int). 2019-02-16: Vulkan: Viewport and clipping rectangles
//  correctly using draw_data->FramebufferScale to allow retina display. 2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About
//  Window. 2018-08-25: Vulkan: Fixed mishandled VkSurfaceCapabilitiesKHR::maxImageCount=0 case. 2018-06-22: Inverted the parameters to
//  ImGui_ImplVulkan_RenderDrawData() to be consistent with other backends. 2018-06-08: Misc: Extracted imgui_impl_vulkan.cpp/.h away from the old combined
//  GLFW+Vulkan example. 2018-06-08: Vulkan: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle. 2018-03-03:
//  Vulkan: Various refactor, created a couple of ImGui_ImplVulkanH_XXX helper that the example can use and that viewport support will use. 2018-03-01: Vulkan:
//  Renamed ImGui_ImplVulkan_Init_Info to ImGui_ImplVulkan_InitInfo and fields to match more closely Vulkan terminology. 2018-02-16: Misc: Obsoleted the
//  io.RenderDrawListsFn callback, ImGui_ImplVulkan_Render() calls ImGui_ImplVulkan_RenderDrawData() itself. 2018-02-06: Misc: Removed call to ImGui::Shutdown()
//  which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves. 2017-05-15: Vulkan: Fix scissor offset being negative. Fix
//  new Vulkan validation warnings. Set required depth member for buffer image copy. 2016-11-13: Vulkan: Fix validation layer warnings and errors and redeclare
//  gl_PerVertex. 2016-10-18: Vulkan: Add location decorators & change to use structs as in/out in glsl, update embedded spv (produced with glslangValidator
//  -x). Null the released resources. 2016-08-27: Vulkan: Fix Vulkan example for use when a depth buffer is active.

#include "imgui.h"
#ifndef IMGUI_DISABLE
  #include "imgui_impl_fvog.h"
  #include "Fvog/Shader2.h"
  #include "Fvog/Pipeline2.h"
  #include "Fvog/Rendering2.h"
  #include "Fvog/Buffer2.h"
  #include "Fvog/Texture2.h"
  #include <optional>

  #include <stdio.h>
  #ifndef IM_MAX
    #define IM_MAX(A, B) (((A) >= (B)) ? (A) : (B))
  #endif

  // Visual Studio warnings
  #ifdef _MSC_VER
    #pragma warning(disable : 4127) // condition expression is constant
  #endif

// Forward Declarations
struct ImGui_ImplVulkan_FrameRenderBuffers;
struct ImGui_ImplVulkan_WindowRenderBuffers;
bool ImGui_ImplVulkan_CreateDeviceObjects();
void ImGui_ImplVulkan_DestroyDeviceObjects();
void ImGui_ImplVulkan_DestroyFrameRenderBuffers(ImGui_ImplVulkan_FrameRenderBuffers* buffers);
void ImGui_ImplVulkan_DestroyWindowRenderBuffers(ImGui_ImplVulkan_WindowRenderBuffers* buffers);
void ImGui_ImplVulkanH_DestroyFrame(VkDevice device, ImGui_ImplVulkanH_Frame* fd, const VkAllocationCallbacks* allocator);
void ImGui_ImplVulkanH_DestroyFrameSemaphores(VkDevice device, ImGui_ImplVulkanH_FrameSemaphores* fsd, const VkAllocationCallbacks* allocator);
void ImGui_ImplVulkanH_DestroyAllViewportsRenderBuffers(VkDevice device, const VkAllocationCallbacks* allocator);
void ImGui_ImplVulkanH_CreateWindowSwapChain(
  VkPhysicalDevice physical_device, VkDevice device, ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator, int w, int h, uint32_t min_image_count);
void ImGui_ImplVulkanH_CreateWindowCommandBuffers(
  VkPhysicalDevice physical_device, VkDevice device, ImGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator);

  // Vulkan prototypes for use with custom loaders
  // (see description of IMGUI_IMPL_VULKAN_NO_PROTOTYPES in imgui_impl_vulkan.h
  #if defined(VK_NO_PROTOTYPES) && !defined(VOLK_H_)
    #define IMGUI_IMPL_VULKAN_USE_LOADER
static bool g_FunctionsLoaded = false;
  #else
static bool g_FunctionsLoaded = true;
  #endif

  #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
static PFN_vkCmdBeginRenderingKHR ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR;
static PFN_vkCmdEndRenderingKHR ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR;
  #endif

// Reusable buffers used for rendering 1 current in-flight frame, for ImGui_ImplVulkan_RenderDrawData()
// [Please zero-clear before use!]
struct ImGui_ImplVulkan_FrameRenderBuffers
{
  std::optional<Fvog::Buffer> vertexBuffer;
  std::optional<Fvog::Buffer> vertexUploadBuffer;
  std::optional<Fvog::Buffer> indexBuffer;
  std::optional<Fvog::Buffer> indexUploadBuffer;
};

// Each viewport will hold 1 ImGui_ImplVulkanH_WindowRenderBuffers
// [Please zero-clear before use!]
struct ImGui_ImplVulkan_WindowRenderBuffers
{
  uint32_t Index;
  uint32_t Count;
  ImGui_ImplVulkan_FrameRenderBuffers* FrameRenderBuffers;
};

// For multi-viewport support:
// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
struct ImGui_ImplVulkan_ViewportData
{
  ImGui_ImplVulkanH_Window Window;                    // Used by secondary viewports only
  ImGui_ImplVulkan_WindowRenderBuffers RenderBuffers; // Used by all viewports
  bool WindowOwned;
  bool SwapChainNeedRebuild; // Flag when viewport swapchain resized in the middle of processing a frame

  ImGui_ImplVulkan_ViewportData()
  {
    WindowOwned = SwapChainNeedRebuild = false;
    memset(&RenderBuffers, 0, sizeof(RenderBuffers));
  }
  ~ImGui_ImplVulkan_ViewportData() {}
};

// Vulkan data
struct ImGui_ImplVulkan_Data
{
  Fvog::Device* device{};
  ImGui_ImplVulkan_InitInfo VulkanInitInfo;
  VkDeviceSize BufferMemoryAlignment;
  VkPipelineCreateFlags PipelineCreateFlags;
  std::optional<Fvog::GraphicsPipeline> Pipeline; // pipeline for main render pass (created by app)
  VkPipeline PipelineForViewports; // pipeline for secondary viewports (created by backend)
  std::optional<Fvog::Shader> ShaderModuleVert;
  std::optional<Fvog::Shader> ShaderModuleFrag;

  // Font data
  std::optional<Fvog::Texture> FontImage;
  std::optional<Fvog::Sampler> FontSampler;

  // Render buffers for main window
  ImGui_ImplVulkan_WindowRenderBuffers MainWindowRenderBuffers;

  ImGui_ImplVulkan_Data()
  {
    memset((void*)this, 0, sizeof(*this));
    BufferMemoryAlignment = 256;
  }
};

//-----------------------------------------------------------------------------
// SHADERS
//-----------------------------------------------------------------------------

// Forward Declarations
//static void ImGui_ImplVulkan_InitPlatformInterface();
static void ImGui_ImplVulkan_ShutdownPlatformInterface();

struct ImGuiPushConstants
{
  uint32_t vertexBufferIndex{};
  uint32_t textureIndex{};
  uint32_t samplerIndex{};
  float scale[2]{};
  float translation[2]{};
};

// backends/vulkan/glsl_shader.vert, compiled with:
// # glslangValidator -V -x -o glsl_shader.vert.u32 glsl_shader.vert
constexpr static std::string_view glsl_shader_vert = R"(
#version 450 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

layout(push_constant, scalar) uniform PushConstants
{
  uint vertexBufferIndex;
  uint textureIndex;
  uint samplerIndex;
  vec2 scale;
  vec2 translation;
} pc;

struct Vertex
{
  vec2 position;
  vec2 texcoord;
  uint color;
};

layout(set = 0, binding = 0, scalar) readonly buffer VertexBuffer
{
  Vertex vertices[];
}buffers[];

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;
void main()
{
  Vertex vertex = buffers[pc.vertexBufferIndex].vertices[gl_VertexIndex];
  Out.Color = unpackUnorm4x8(vertex.color);
  Out.UV = vertex.texcoord;
  gl_Position = vec4(vertex.position * pc.scale + pc.translation, 0, 1);
}
)";

// backends/vulkan/glsl_shader.frag, compiled with:
// # glslangValidator -V -x -o glsl_shader.frag.u32 glsl_shader.frag
constexpr static std::string_view glsl_shader_frag = R"(
#version 450 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

layout(push_constant, scalar) uniform PushConstants
{
  uint vertexBufferIndex;
  uint textureIndex;
  uint samplerIndex;
  vec2 scale;
  vec2 translation;
} pc;

layout(set = 0, binding = 3) uniform texture2D textures[];
layout(set = 0, binding = 4) uniform sampler samplers[];

layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout(location = 0) out vec4 fColor;

void main()
{
  fColor = In.Color * texture(sampler2D(textures[pc.textureIndex], samplers[pc.samplerIndex]), In.UV);
}
)";

bool ImGui_ImplVulkan_LoadFunctions(PFN_vkVoidFunction (*loader_func)(const char* function_name, void* user_data), void* user_data)
{
  // Load function pointers
  // You can use the default Vulkan loader using:
  //      ImGui_ImplVulkan_LoadFunctions([](const char* function_name, void*) { return vkGetInstanceProcAddr(your_vk_isntance, function_name); });
  // But this would be roughly equivalent to not setting VK_NO_PROTOTYPES.
  #ifdef IMGUI_IMPL_VULKAN_USE_LOADER
    #define IMGUI_VULKAN_FUNC_LOAD(func)                                      \
      func = reinterpret_cast<decltype(func)>(loader_func(#func, user_data)); \
      if (func == nullptr)                                                    \
        return false;
  IMGUI_VULKAN_FUNC_MAP(IMGUI_VULKAN_FUNC_LOAD)
    #undef IMGUI_VULKAN_FUNC_LOAD

    #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
  // Manually load those two (see #5446)
  ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(loader_func("vkCmdBeginRenderingKHR", user_data));
  ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(loader_func("vkCmdEndRenderingKHR", user_data));
    #endif
  #else
  IM_UNUSED(loader_func);
  IM_UNUSED(user_data);
  #endif

  g_FunctionsLoaded = true;
  return true;
}

//-----------------------------------------------------------------------------
// FUNCTIONS
//-----------------------------------------------------------------------------

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not tested and probably dysfunctional in this backend.
static ImGui_ImplVulkan_Data* ImGui_ImplVulkan_GetBackendData()
{
  return ImGui::GetCurrentContext() ? (ImGui_ImplVulkan_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

static void check_vk_result(VkResult err)
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  if (!bd)
    return;
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
  if (v->CheckVkResultFn)
    v->CheckVkResultFn(err);
}

// Same as IM_MEMALIGN(). 'alignment' must be a power of two.
static inline VkDeviceSize AlignBufferSize(VkDeviceSize size, VkDeviceSize alignment)
{
  return (size + alignment - 1) & ~(alignment - 1);
}

static void ImGui_ImplVulkan_SetupRenderState(
  ImDrawData* draw_data, VkPipeline pipeline, VkCommandBuffer command_buffer, ImGui_ImplVulkan_FrameRenderBuffers* rb, int fb_width, int fb_height, ImGuiPushConstants& pushConstants)
{
  //ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();

  // Bind pipeline:
  {
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  }

  // Bind Vertex And Index Buffer:
  if (draw_data->TotalVtxCount > 0)
  {
    //VkBuffer vertex_buffers[1] = {rb->VertexBuffer};
    //VkDeviceSize vertex_offset[1] = {0};
    //vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, vertex_offset);
    pushConstants.vertexBufferIndex = rb->vertexBuffer->GetResourceHandle().index;
    vkCmdBindIndexBuffer(command_buffer, rb->indexBuffer->Handle(), 0, sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
  }

  // Setup viewport:
  {
    VkViewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)fb_width;
    viewport.height = (float)fb_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
  }

  // Setup scale and translation:
  // Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
  {
    pushConstants.scale[0] = 2.0f / draw_data->DisplaySize.x;
    pushConstants.scale[1] = 2.0f / draw_data->DisplaySize.y;
    pushConstants.translation[0] = -1.0f - draw_data->DisplayPos.x * pushConstants.scale[0];
    pushConstants.translation[1] = -1.0f - draw_data->DisplayPos.y * pushConstants.scale[1];
    //vkCmdPushConstants(command_buffer, bd->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 0, sizeof(float) * 2, scale);
    //vkCmdPushConstants(command_buffer, bd->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);
  }
}

// Render function
void ImGui_ImplVulkan_RenderDrawData(ImDrawData* draw_data, VkCommandBuffer command_buffer)
{
  // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
  int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (fb_width <= 0 || fb_height <= 0)
    return;

  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;

  auto ctx = Fvog::Context(*bd->device, command_buffer);

  // Allocate array to store enough vertex/index buffers. Each unique viewport gets its own storage.
  ImGui_ImplVulkan_ViewportData* viewport_renderer_data = (ImGui_ImplVulkan_ViewportData*)draw_data->OwnerViewport->RendererUserData;
  IM_ASSERT(viewport_renderer_data != nullptr);
  ImGui_ImplVulkan_WindowRenderBuffers* wrb = &viewport_renderer_data->RenderBuffers;
  if (wrb->FrameRenderBuffers == nullptr)
  {
    wrb->Index = 0;
    wrb->Count = v->ImageCount;
    wrb->FrameRenderBuffers = (ImGui_ImplVulkan_FrameRenderBuffers*)IM_ALLOC(sizeof(ImGui_ImplVulkan_FrameRenderBuffers) * wrb->Count);
    memset(wrb->FrameRenderBuffers, 0, sizeof(ImGui_ImplVulkan_FrameRenderBuffers) * wrb->Count);
  }
  IM_ASSERT(wrb->Count == v->ImageCount);
  wrb->Index = (wrb->Index + 1) % wrb->Count;
  ImGui_ImplVulkan_FrameRenderBuffers* rb = &wrb->FrameRenderBuffers[wrb->Index];

  if (draw_data->TotalVtxCount > 0)
  {
    // Create or resize the vertex/index buffers
    size_t vertex_size = AlignBufferSize(draw_data->TotalVtxCount * sizeof(ImDrawVert), bd->BufferMemoryAlignment);
    size_t index_size = AlignBufferSize(draw_data->TotalIdxCount * sizeof(ImDrawIdx), bd->BufferMemoryAlignment);
    if (!rb->vertexBuffer.has_value() || rb->vertexBuffer->SizeBytes() < vertex_size)
    {
      rb->vertexBuffer = Fvog::Buffer(*bd->device, {.size = vertex_size, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "ImGui Vertex Buffer");
      //rb->vertexUploadBuffer = Fvog::Buffer(*bd->device, {.size = vertex_size, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "ImGui Vertex Upload Buffer");
      //CreateOrResizeBuffer(rb->VertexBuffer, rb->VertexBufferMemory, rb->VertexBufferSize, vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (!rb->indexBuffer.has_value() || rb->indexBuffer->SizeBytes() < index_size)
    {
      rb->indexBuffer = Fvog::Buffer(*bd->device, {.size = index_size, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "ImGui Index Buffer");
      //rb->indexUploadBuffer = Fvog::Buffer(*bd->device, {.size = index_size, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "ImGui Index Upload Buffer");
      //CreateOrResizeBuffer(rb->IndexBuffer, rb->IndexBufferMemory, rb->IndexBufferSize, index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    // Upload vertex/index data into a single contiguous GPU buffer
    //ImDrawVert* vtx_dst = nullptr;
    //ImDrawIdx* idx_dst = nullptr;
    //VkResult err = vkMapMemory(v->Device, rb->VertexBufferMemory, 0, vertex_size, 0, (void**)&vtx_dst);
    //check_vk_result(err);
    //err = vkMapMemory(v->Device, rb->IndexBufferMemory, 0, index_size, 0, (void**)&idx_dst);
    //check_vk_result(err);
    auto* vtx_dst = static_cast<ImDrawVert*>(rb->vertexBuffer->GetMappedMemory());
    auto* idx_dst = static_cast<ImDrawIdx*>(rb->indexBuffer->GetMappedMemory());
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
      const ImDrawList* cmd_list = draw_data->CmdLists[n];
      memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
      vtx_dst += cmd_list->VtxBuffer.Size;
      idx_dst += cmd_list->IdxBuffer.Size;
    }

    //VkMappedMemoryRange range[2] = {};
    //range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    //range[0].memory = rb->VertexBufferMemory;
    //range[0].size = VK_WHOLE_SIZE;
    //range[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    //range[1].memory = rb->IndexBufferMemory;
    //range[1].size = VK_WHOLE_SIZE;
    //err = vkFlushMappedMemoryRanges(v->Device, 2, range);
    //check_vk_result(err);
    //vkUnmapMemory(v->Device, rb->VertexBufferMemory);
    //vkUnmapMemory(v->Device, rb->IndexBufferMemory);
  }

  auto pushConstants = ImGuiPushConstants{};

  // Setup desired Vulkan state
  ImGui_ImplVulkan_SetupRenderState(draw_data, bd->Pipeline->Handle(), command_buffer, rb, fb_width, fb_height, pushConstants);

  // Will project scissor/clipping rectangles into framebuffer space
  ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
  ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

  // Render command lists
  // (Because we merged all buffers into a single one, we maintain our own offset into them)
  int global_vtx_offset = 0;
  int global_idx_offset = 0;
  for (int n = 0; n < draw_data->CmdListsCount; n++)
  {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
    {
      const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
      if (pcmd->UserCallback != nullptr)
      {
        // User callback, registered via ImDrawList::AddCallback()
        // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
        if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
          ImGui_ImplVulkan_SetupRenderState(draw_data, bd->Pipeline->Handle(), command_buffer, rb, fb_width, fb_height, pushConstants);
        else
          pcmd->UserCallback(cmd_list, pcmd);
      }
      else
      {
        // Project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
        ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

        // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
        if (clip_min.x < 0.0f)
        {
          clip_min.x = 0.0f;
        }
        if (clip_min.y < 0.0f)
        {
          clip_min.y = 0.0f;
        }
        if (clip_max.x > fb_width)
        {
          clip_max.x = (float)fb_width;
        }
        if (clip_max.y > fb_height)
        {
          clip_max.y = (float)fb_height;
        }
        if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
          continue;

        // Apply scissor/clipping rectangle
        VkRect2D scissor;
        scissor.offset.x = (int32_t)(clip_min.x);
        scissor.offset.y = (int32_t)(clip_min.y);
        scissor.extent.width = (uint32_t)(clip_max.x - clip_min.x);
        scissor.extent.height = (uint32_t)(clip_max.y - clip_min.y);
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);

        // Bind DescriptorSet with font or user texture
        //vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bd->PipelineLayout, 0, 1, desc_set, 0, nullptr);
        auto textureSampler = ImTextureSampler(pcmd->TextureId);
        pushConstants.textureIndex = static_cast<uint32_t>(pcmd->TextureId);
        if (textureSampler.IsSamplerDefault())
        {
          pushConstants.samplerIndex = bd->FontSampler->GetResourceHandle().index;
        }
        else
        {
          pushConstants.samplerIndex = textureSampler.GetSamplerIndex();
        }

        vkCmdPushConstants(command_buffer, bd->device->defaultPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(pushConstants), &pushConstants);

        // Draw
        vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
      }
    }
    global_idx_offset += cmd_list->IdxBuffer.Size;
    global_vtx_offset += cmd_list->VtxBuffer.Size;
  }

  // Note: at this point both vkCmdSetViewport() and vkCmdSetScissor() have been called.
  // Our last values will leak into user/application rendering IF:
  // - Your app uses a pipeline with VK_DYNAMIC_STATE_VIEWPORT or VK_DYNAMIC_STATE_SCISSOR dynamic state
  // - And you forgot to call vkCmdSetViewport() and vkCmdSetScissor() yourself to explicitly set that state.
  // If you use VK_DYNAMIC_STATE_VIEWPORT or VK_DYNAMIC_STATE_SCISSOR you are responsible for setting the values before rendering.
  // In theory we should aim to backup/restore those values but I am not sure this is possible.
  // We perform a call to vkCmdSetScissor() to set back a full viewport which is likely to fix things for 99% users but technically this is not perfect. (See github #4644)
  VkRect2D scissor = {{0, 0}, {(uint32_t)fb_width, (uint32_t)fb_height}};
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

void ImGui_ImplVulkan_CreateFontsTexture()
{
  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  //ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;

  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  size_t upload_size = width * height * 4 * sizeof(char);

  bd->FontImage = Fvog::CreateTexture2D(*bd->device, {(uint32_t)width, (uint32_t)height}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::READ_ONLY, "ImGui Font Texture");

  bd->device->ImmediateSubmit(
    [&](VkCommandBuffer cmd) {
      auto ctx = Fvog::Context(*bd->device, cmd);
      auto uploadBuffer = Fvog::Buffer(*bd->device, {.size = upload_size, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "ImGui Upload Buffer");
      memcpy(uploadBuffer.GetMappedMemory(), pixels, upload_size);
      ctx.ImageBarrierDiscard(bd->FontImage.value(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      ctx.CopyBufferToTexture(uploadBuffer, bd->FontImage.value(), {.extent = {(uint32_t)width, (uint32_t)height, 1},});
      ctx.ImageBarrier(bd->FontImage.value(), VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    });

  // Store our identifier
  io.Fonts->SetTexID((ImTextureID)bd->FontImage->ImageView().GetSampledResourceHandle().index);
}

// You probably never need to call this, as it is called by ImGui_ImplVulkan_CreateFontsTexture() and ImGui_ImplVulkan_Shutdown().
void ImGui_ImplVulkan_DestroyFontsTexture()
{
  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  //ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
  
  io.Fonts->SetTexID(0);
  if (bd->FontImage)
  {
    bd->FontImage.reset();
  }
}

static void ImGui_ImplVulkan_CreateShaderModules(VkDevice device)
{
  // Create the shader modules
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  if (!bd->ShaderModuleVert.has_value())
  {
    bd->ShaderModuleVert = Fvog::Shader(device, Fvog::PipelineStage::VERTEX_SHADER, glsl_shader_vert, "ImGui Vertex Shader");
  }
  if (!bd->ShaderModuleFrag.has_value())
  {
    bd->ShaderModuleFrag = Fvog::Shader(device, Fvog::PipelineStage::FRAGMENT_SHADER, glsl_shader_frag, "ImGui Fragment Shader");
  }
}

static [[nodiscard]] Fvog::GraphicsPipeline ImGui_ImplVulkan_CreatePipeline(VkDevice device, VkSampleCountFlagBits MSAASamples)
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  ImGui_ImplVulkan_CreateShaderModules(device);

  return Fvog::GraphicsPipeline(
    *bd->device,
    Fvog::GraphicsPipelineInfo{
      .name = "ImGui Pipeline",
      .vertexShader = &bd->ShaderModuleVert.value(),
      .fragmentShader = &bd->ShaderModuleFrag.value(),
      .inputAssemblyState = {},
      .rasterizationState = {},
      .multisampleState = {.rasterizationSamples = (MSAASamples != 0) ? MSAASamples : VK_SAMPLE_COUNT_1_BIT},
      .depthState = {},
      .colorBlendState =  {
        .attachments = {
          {
            Fvog::ColorBlendAttachmentState{
              .blendEnable = true,
              .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
              .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
              .colorBlendOp = VK_BLEND_OP_ADD,
              .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
              .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
              .alphaBlendOp = VK_BLEND_OP_ADD,
           },
          }
        },
      },
      .renderTargetFormats = bd->VulkanInitInfo.formats,
    }
  );

  //VkVertexInputAttributeDescription attribute_desc[3] = {};
  //attribute_desc[0].location = 0;
  //attribute_desc[0].binding = binding_desc[0].binding;
  //attribute_desc[0].format = VK_FORMAT_R32G32_SFLOAT;
  //attribute_desc[0].offset = offsetof(ImDrawVert, pos);
  //attribute_desc[1].location = 1;
  //attribute_desc[1].binding = binding_desc[0].binding;
  //attribute_desc[1].format = VK_FORMAT_R32G32_SFLOAT;
  //attribute_desc[1].offset = offsetof(ImDrawVert, uv);
  //attribute_desc[2].location = 2;
  //attribute_desc[2].binding = binding_desc[0].binding;
  //attribute_desc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
  //attribute_desc[2].offset = offsetof(ImDrawVert, col);

  //VkPipelineVertexInputStateCreateInfo vertex_info = {};
  //vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  //vertex_info.vertexBindingDescriptionCount = 1;
  //vertex_info.pVertexBindingDescriptions = binding_desc;
  //vertex_info.vertexAttributeDescriptionCount = 3;
  //vertex_info.pVertexAttributeDescriptions = attribute_desc;
}

bool ImGui_ImplVulkan_CreateDeviceObjects()
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;

  if (!bd->FontSampler)
  {
    // Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling.
    bd->FontSampler = Fvog::Sampler(*bd->device, {
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .maxAnisotropy = 1.0f,
    }, "ImGui Font Sampler");
  }

  bd->Pipeline = ImGui_ImplVulkan_CreatePipeline(v->Device->device_, v->MSAASamples);

  return true;
}

void ImGui_ImplVulkan_DestroyDeviceObjects()
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
  ImGui_ImplVulkanH_DestroyAllViewportsRenderBuffers(v->Device->device_, nullptr);
  ImGui_ImplVulkan_DestroyFontsTexture();
  
  bd->ShaderModuleVert.reset();
  bd->ShaderModuleFrag.reset();
  bd->FontSampler.reset();
  bd->Pipeline.reset();

  if (bd->PipelineForViewports)
  {
    vkDestroyPipeline(v->Device->device_, bd->PipelineForViewports, nullptr);
    bd->PipelineForViewports = VK_NULL_HANDLE;
  }
}

bool ImGui_ImplVulkan_Init(ImGui_ImplVulkan_InitInfo* info)
{
  IM_ASSERT(g_FunctionsLoaded && "Need to call ImGui_ImplVulkan_LoadFunctions() if IMGUI_IMPL_VULKAN_NO_PROTOTYPES or VK_NO_PROTOTYPES are set!");
  
  #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
    #ifndef IMGUI_IMPL_VULKAN_USE_LOADER
    ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetInstanceProcAddr(info->Instance, "vkCmdBeginRenderingKHR"));
    ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetInstanceProcAddr(info->Instance, "vkCmdEndRenderingKHR"));
    #endif
    IM_ASSERT(ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR != nullptr);
    IM_ASSERT(ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR != nullptr);
  #else
    IM_ASSERT(0 && "Can't use dynamic rendering when neither VK_VERSION_1_3 or VK_KHR_dynamic_rendering is defined.");
  #endif

  ImGuiIO& io = ImGui::GetIO();
  IMGUI_CHECKVERSION();
  IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

  // Setup backend capabilities flags
  ImGui_ImplVulkan_Data* bd = IM_NEW(ImGui_ImplVulkan_Data)();
  io.BackendRendererUserData = (void*)bd;
  io.BackendRendererName = "imgui_impl_vulkan";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset; // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
  io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports; // We can create multi-viewports on the Renderer side (optional)

  IM_ASSERT(info->Instance != VK_NULL_HANDLE);
  IM_ASSERT(info->PhysicalDevice != VK_NULL_HANDLE);
  IM_ASSERT(info->Device != VK_NULL_HANDLE);
  IM_ASSERT(info->Queue != VK_NULL_HANDLE);
  IM_ASSERT(info->DescriptorPool != VK_NULL_HANDLE);
  IM_ASSERT(info->MinImageCount >= 2);
  IM_ASSERT(info->ImageCount >= info->MinImageCount);

  bd->VulkanInitInfo = *info;
  bd->device = info->Device;

  ImGui_ImplVulkan_CreateDeviceObjects();

  // Our render function expect RendererUserData to be storing the window render buffer we need (for the main viewport we won't use ->Window)
  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  main_viewport->RendererUserData = IM_NEW(ImGui_ImplVulkan_ViewportData)();

  //if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  //  ImGui_ImplVulkan_InitPlatformInterface();

  return true;
}

void ImGui_ImplVulkan_Shutdown()
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
  ImGuiIO& io = ImGui::GetIO();

  // First destroy objects in all viewports
  ImGui_ImplVulkan_DestroyDeviceObjects();

  // Manually delete main viewport render data in-case we haven't initialized for viewports
  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  if (ImGui_ImplVulkan_ViewportData* vd = (ImGui_ImplVulkan_ViewportData*)main_viewport->RendererUserData)
    IM_DELETE(vd);
  main_viewport->RendererUserData = nullptr;

  // Clean up windows
  ImGui_ImplVulkan_ShutdownPlatformInterface();

  io.BackendRendererName = nullptr;
  io.BackendRendererUserData = nullptr;
  io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports);
  IM_DELETE(bd);
}

void ImGui_ImplVulkan_NewFrame()
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplVulkan_Init()?");

  if (!bd->FontImage)
    ImGui_ImplVulkan_CreateFontsTexture();
}

void ImGui_ImplVulkan_SetMinImageCount(uint32_t min_image_count)
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  IM_ASSERT(min_image_count >= 2);
  if (bd->VulkanInitInfo.MinImageCount == min_image_count)
    return;

  IM_ASSERT(0); // FIXME-VIEWPORT: Unsupported. Need to recreate all swap chains!
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
  VkResult err = vkDeviceWaitIdle(v->Device->device_);
  check_vk_result(err);
  //ImGui_ImplVulkanH_DestroyAllViewportsRenderBuffers(v->Device, nullptr);

  bd->VulkanInitInfo.MinImageCount = min_image_count;
}

void ImGui_ImplVulkan_DestroyFrameRenderBuffers(ImGui_ImplVulkan_FrameRenderBuffers* buffers)
{
  buffers->vertexBuffer.reset();
  buffers->indexBuffer.reset();
}

void ImGui_ImplVulkan_DestroyWindowRenderBuffers(ImGui_ImplVulkan_WindowRenderBuffers* buffers)
{
  for (uint32_t n = 0; n < buffers->Count; n++)
    ImGui_ImplVulkan_DestroyFrameRenderBuffers(&buffers->FrameRenderBuffers[n]);
  IM_FREE(buffers->FrameRenderBuffers);
  buffers->FrameRenderBuffers = nullptr;
  buffers->Index = 0;
  buffers->Count = 0;
}

#if 0
//-------------------------------------------------------------------------
// Internal / Miscellaneous Vulkan Helpers
// (Used by example's main.cpp. Used by multi-viewport features. PROBABLY NOT used by your own app.)
//-------------------------------------------------------------------------
// You probably do NOT need to use or care about those functions.
// Those functions only exist because:
//   1) they facilitate the readability and maintenance of the multiple main.cpp examples files.
//   2) the upcoming multi-viewport feature will need them internally.
// Generally we avoid exposing any kind of superfluous high-level helpers in the backends,
// but it is too much code to duplicate everywhere so we exceptionally expose them.
//
// Your engine/app will likely _already_ have code to setup all that stuff (swap chain, render pass, frame buffers, etc.).
// You may read this code to learn about Vulkan, but it is recommended you use you own custom tailored code to do equivalent work.
// (The ImGui_ImplVulkanH_XXX functions do not interact with any of the state used by the regular ImGui_ImplVulkan_XXX functions)
//-------------------------------------------------------------------------

VkSurfaceFormatKHR ImGui_ImplVulkanH_SelectSurfaceFormat(
  VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkFormat* request_formats, int request_formats_count, VkColorSpaceKHR request_color_space)
{
  IM_ASSERT(g_FunctionsLoaded && "Need to call ImGui_ImplVulkan_LoadFunctions() if IMGUI_IMPL_VULKAN_NO_PROTOTYPES or VK_NO_PROTOTYPES are set!");
  IM_ASSERT(request_formats != nullptr);
  IM_ASSERT(request_formats_count > 0);

  // Per Spec Format and View Format are expected to be the same unless VK_IMAGE_CREATE_MUTABLE_BIT was set at image creation
  // Assuming that the default behavior is without setting this bit, there is no need for separate Swapchain image and image view format
  // Additionally several new color spaces were introduced with Vulkan Spec v1.0.40,
  // hence we must make sure that a format with the mostly available color space, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, is found and used.
  uint32_t avail_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &avail_count, nullptr);
  ImVector<VkSurfaceFormatKHR> avail_format;
  avail_format.resize((int)avail_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &avail_count, avail_format.Data);

  // First check if only one format, VK_FORMAT_UNDEFINED, is available, which would imply that any format is available
  if (avail_count == 1)
  {
    if (avail_format[0].format == VK_FORMAT_UNDEFINED)
    {
      VkSurfaceFormatKHR ret;
      ret.format = request_formats[0];
      ret.colorSpace = request_color_space;
      return ret;
    }
    else
    {
      // No point in searching another format
      return avail_format[0];
    }
  }
  else
  {
    // Request several formats, the first found will be used
    for (int request_i = 0; request_i < request_formats_count; request_i++)
      for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
        if (avail_format[avail_i].format == request_formats[request_i] && avail_format[avail_i].colorSpace == request_color_space)
          return avail_format[avail_i];

    // If none of the requested image formats could be found, use the first available
    return avail_format[0];
  }
}

VkPresentModeKHR ImGui_ImplVulkanH_SelectPresentMode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkPresentModeKHR* request_modes, int request_modes_count)
{
  IM_ASSERT(g_FunctionsLoaded && "Need to call ImGui_ImplVulkan_LoadFunctions() if IMGUI_IMPL_VULKAN_NO_PROTOTYPES or VK_NO_PROTOTYPES are set!");
  IM_ASSERT(request_modes != nullptr);
  IM_ASSERT(request_modes_count > 0);

  // Request a certain mode and confirm that it is available. If not use VK_PRESENT_MODE_FIFO_KHR which is mandatory
  uint32_t avail_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &avail_count, nullptr);
  ImVector<VkPresentModeKHR> avail_modes;
  avail_modes.resize((int)avail_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &avail_count, avail_modes.Data);
  // for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
  //     printf("[vulkan] avail_modes[%d] = %d\n", avail_i, avail_modes[avail_i]);

  for (int request_i = 0; request_i < request_modes_count; request_i++)
    for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
      if (request_modes[request_i] == avail_modes[avail_i])
        return request_modes[request_i];

  return VK_PRESENT_MODE_FIFO_KHR; // Always available
}

void ImGui_ImplVulkanH_CreateWindowCommandBuffers(
  VkPhysicalDevice physical_device, VkDevice device, ImGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator)
{
  IM_ASSERT(physical_device != VK_NULL_HANDLE && device != VK_NULL_HANDLE);
  IM_UNUSED(physical_device);

  // Create Command Buffers
  VkResult err;
  for (uint32_t i = 0; i < wd->ImageCount; i++)
  {
    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[i];
    {
      VkCommandPoolCreateInfo info = {};
      info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      info.flags = 0;
      info.queueFamilyIndex = queue_family;
      err = vkCreateCommandPool(device, &info, allocator, &fd->CommandPool);
      check_vk_result(err);
    }
    {
      VkCommandBufferAllocateInfo info = {};
      info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      info.commandPool = fd->CommandPool;
      info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      info.commandBufferCount = 1;
      err = vkAllocateCommandBuffers(device, &info, &fd->CommandBuffer);
      check_vk_result(err);
    }
    {
      VkFenceCreateInfo info = {};
      info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      err = vkCreateFence(device, &info, allocator, &fd->Fence);
      check_vk_result(err);
    }
  }

  for (uint32_t i = 0; i < wd->SemaphoreCount; i++)
  {
    ImGui_ImplVulkanH_FrameSemaphores* fsd = &wd->FrameSemaphores[i];
    {
      VkSemaphoreCreateInfo info = {};
      info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      err = vkCreateSemaphore(device, &info, allocator, &fsd->ImageAcquiredSemaphore);
      check_vk_result(err);
      err = vkCreateSemaphore(device, &info, allocator, &fsd->RenderCompleteSemaphore);
      check_vk_result(err);
    }
  }
}

int ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(VkPresentModeKHR present_mode)
{
  if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
    return 3;
  if (present_mode == VK_PRESENT_MODE_FIFO_KHR || present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
    return 2;
  if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
    return 1;
  IM_ASSERT(0);
  return 1;
}

// Also destroy old swap chain and in-flight frames data, if any.
void ImGui_ImplVulkanH_CreateWindowSwapChain(
  VkPhysicalDevice physical_device, VkDevice device, ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator, int w, int h, uint32_t min_image_count)
{
  VkResult err;
  VkSwapchainKHR old_swapchain = wd->Swapchain;
  wd->Swapchain = VK_NULL_HANDLE;
  err = vkDeviceWaitIdle(device);
  check_vk_result(err);

  // We don't use ImGui_ImplVulkanH_DestroyWindow() because we want to preserve the old swapchain to create the new one.
  // Destroy old Framebuffer
  for (uint32_t i = 0; i < wd->ImageCount; i++)
    ImGui_ImplVulkanH_DestroyFrame(device, &wd->Frames[i], allocator);
  for (uint32_t i = 0; i < wd->SemaphoreCount; i++)
    ImGui_ImplVulkanH_DestroyFrameSemaphores(device, &wd->FrameSemaphores[i], allocator);
  IM_FREE(wd->Frames);
  IM_FREE(wd->FrameSemaphores);
  wd->Frames = nullptr;
  wd->FrameSemaphores = nullptr;
  wd->ImageCount = 0;
  if (wd->RenderPass)
    vkDestroyRenderPass(device, wd->RenderPass, allocator);

  // If min image count was not specified, request different count of images dependent on selected present mode
  if (min_image_count == 0)
    min_image_count = ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(wd->PresentMode);

  // Create Swapchain
  {
    VkSwapchainCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = wd->Surface;
    info.minImageCount = min_image_count;
    info.imageFormat = wd->SurfaceFormat.format;
    info.imageColorSpace = wd->SurfaceFormat.colorSpace;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // Assume that graphics family == present family
    info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = wd->PresentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old_swapchain;
    VkSurfaceCapabilitiesKHR cap;
    err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, wd->Surface, &cap);
    check_vk_result(err);
    if (info.minImageCount < cap.minImageCount)
      info.minImageCount = cap.minImageCount;
    else if (cap.maxImageCount != 0 && info.minImageCount > cap.maxImageCount)
      info.minImageCount = cap.maxImageCount;

    if (cap.currentExtent.width == 0xffffffff)
    {
      info.imageExtent.width = wd->Width = w;
      info.imageExtent.height = wd->Height = h;
    }
    else
    {
      info.imageExtent.width = wd->Width = cap.currentExtent.width;
      info.imageExtent.height = wd->Height = cap.currentExtent.height;
    }
    err = vkCreateSwapchainKHR(device, &info, allocator, &wd->Swapchain);
    check_vk_result(err);
    err = vkGetSwapchainImagesKHR(device, wd->Swapchain, &wd->ImageCount, nullptr);
    check_vk_result(err);
    VkImage backbuffers[16] = {};
    IM_ASSERT(wd->ImageCount >= min_image_count);
    IM_ASSERT(wd->ImageCount < IM_ARRAYSIZE(backbuffers));
    err = vkGetSwapchainImagesKHR(device, wd->Swapchain, &wd->ImageCount, backbuffers);
    check_vk_result(err);

    IM_ASSERT(wd->Frames == nullptr && wd->FrameSemaphores == nullptr);
    wd->SemaphoreCount = wd->ImageCount + 1;
    wd->Frames = (ImGui_ImplVulkanH_Frame*)IM_ALLOC(sizeof(ImGui_ImplVulkanH_Frame) * wd->ImageCount);
    wd->FrameSemaphores = (ImGui_ImplVulkanH_FrameSemaphores*)IM_ALLOC(sizeof(ImGui_ImplVulkanH_FrameSemaphores) * wd->SemaphoreCount);
    memset(wd->Frames, 0, sizeof(wd->Frames[0]) * wd->ImageCount);
    memset(wd->FrameSemaphores, 0, sizeof(wd->FrameSemaphores[0]) * wd->SemaphoreCount);
    for (uint32_t i = 0; i < wd->ImageCount; i++)
      wd->Frames[i].Backbuffer = backbuffers[i];
  }
  if (old_swapchain)
    vkDestroySwapchainKHR(device, old_swapchain, allocator);

  // Create the Render Pass
  if (wd->UseDynamicRendering == false)
  {
    VkAttachmentDescription attachment = {};
    attachment.format = wd->SurfaceFormat.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = wd->ClearEnable ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color_attachment = {};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    err = vkCreateRenderPass(device, &info, allocator, &wd->RenderPass);
    check_vk_result(err);

    // We do not create a pipeline by default as this is also used by examples' main.cpp,
    // but secondary viewport in multi-viewport mode may want to create one with:
    // ImGui_ImplVulkan_CreatePipeline(device, allocator, VK_NULL_HANDLE, wd->RenderPass, VK_SAMPLE_COUNT_1_BIT, &wd->Pipeline, v->Subpass);
  }

  // Create The Image Views
  {
    VkImageViewCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = wd->SurfaceFormat.format;
    info.components.r = VK_COMPONENT_SWIZZLE_R;
    info.components.g = VK_COMPONENT_SWIZZLE_G;
    info.components.b = VK_COMPONENT_SWIZZLE_B;
    info.components.a = VK_COMPONENT_SWIZZLE_A;
    VkImageSubresourceRange image_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    info.subresourceRange = image_range;
    for (uint32_t i = 0; i < wd->ImageCount; i++)
    {
      ImGui_ImplVulkanH_Frame* fd = &wd->Frames[i];
      info.image = fd->Backbuffer;
      err = vkCreateImageView(device, &info, allocator, &fd->BackbufferView);
      check_vk_result(err);
    }
  }

  // Create Framebuffer
  if (wd->UseDynamicRendering == false)
  {
    VkImageView attachment[1];
    VkFramebufferCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = wd->RenderPass;
    info.attachmentCount = 1;
    info.pAttachments = attachment;
    info.width = wd->Width;
    info.height = wd->Height;
    info.layers = 1;
    for (uint32_t i = 0; i < wd->ImageCount; i++)
    {
      ImGui_ImplVulkanH_Frame* fd = &wd->Frames[i];
      attachment[0] = fd->BackbufferView;
      err = vkCreateFramebuffer(device, &info, allocator, &fd->Framebuffer);
      check_vk_result(err);
    }
  }
}

// Create or resize window
void ImGui_ImplVulkanH_CreateOrResizeWindow(VkInstance instance,
                                            VkPhysicalDevice physical_device,
                                            VkDevice device,
                                            ImGui_ImplVulkanH_Window* wd,
                                            uint32_t queue_family,
                                            const VkAllocationCallbacks* allocator,
                                            int width,
                                            int height,
                                            uint32_t min_image_count)
{
  IM_ASSERT(g_FunctionsLoaded && "Need to call ImGui_ImplVulkan_LoadFunctions() if IMGUI_IMPL_VULKAN_NO_PROTOTYPES or VK_NO_PROTOTYPES are set!");
  (void)instance;
  ImGui_ImplVulkanH_CreateWindowSwapChain(physical_device, device, wd, allocator, width, height, min_image_count);
  // ImGui_ImplVulkan_CreatePipeline(device, allocator, VK_NULL_HANDLE, wd->RenderPass, VK_SAMPLE_COUNT_1_BIT, &wd->Pipeline, g_VulkanInitInfo.Subpass);
  ImGui_ImplVulkanH_CreateWindowCommandBuffers(physical_device, device, wd, queue_family, allocator);
}

void ImGui_ImplVulkanH_DestroyWindow(VkInstance instance, VkDevice device, ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator)
{
  vkDeviceWaitIdle(device); // FIXME: We could wait on the Queue if we had the queue in wd-> (otherwise VulkanH functions can't use globals)
  // vkQueueWaitIdle(bd->Queue);

  for (uint32_t i = 0; i < wd->ImageCount; i++)
    ImGui_ImplVulkanH_DestroyFrame(device, &wd->Frames[i], allocator);
  for (uint32_t i = 0; i < wd->SemaphoreCount; i++)
    ImGui_ImplVulkanH_DestroyFrameSemaphores(device, &wd->FrameSemaphores[i], allocator);
  IM_FREE(wd->Frames);
  IM_FREE(wd->FrameSemaphores);
  wd->Frames = nullptr;
  wd->FrameSemaphores = nullptr;
  vkDestroyRenderPass(device, wd->RenderPass, allocator);
  vkDestroySwapchainKHR(device, wd->Swapchain, allocator);
  vkDestroySurfaceKHR(instance, wd->Surface, allocator);

  *wd = ImGui_ImplVulkanH_Window();
}

void ImGui_ImplVulkanH_DestroyFrame(VkDevice device, ImGui_ImplVulkanH_Frame* fd, const VkAllocationCallbacks* allocator)
{
  vkDestroyFence(device, fd->Fence, allocator);
  vkFreeCommandBuffers(device, fd->CommandPool, 1, &fd->CommandBuffer);
  vkDestroyCommandPool(device, fd->CommandPool, allocator);
  fd->Fence = VK_NULL_HANDLE;
  fd->CommandBuffer = VK_NULL_HANDLE;
  fd->CommandPool = VK_NULL_HANDLE;

  vkDestroyImageView(device, fd->BackbufferView, allocator);
  vkDestroyFramebuffer(device, fd->Framebuffer, allocator);
}

void ImGui_ImplVulkanH_DestroyFrameSemaphores(VkDevice device, ImGui_ImplVulkanH_FrameSemaphores* fsd, const VkAllocationCallbacks* allocator)
{
  vkDestroySemaphore(device, fsd->ImageAcquiredSemaphore, allocator);
  vkDestroySemaphore(device, fsd->RenderCompleteSemaphore, allocator);
  fsd->ImageAcquiredSemaphore = fsd->RenderCompleteSemaphore = VK_NULL_HANDLE;
}

#endif // #if 0

void ImGui_ImplVulkanH_DestroyAllViewportsRenderBuffers(VkDevice, const VkAllocationCallbacks*)
{
  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  for (int n = 0; n < platform_io.Viewports.Size; n++)
    if (ImGui_ImplVulkan_ViewportData* vd = (ImGui_ImplVulkan_ViewportData*)platform_io.Viewports[n]->RendererUserData)
      ImGui_ImplVulkan_DestroyWindowRenderBuffers(&vd->RenderBuffers);
}

#if 0

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
//--------------------------------------------------------------------------------------------------------

static void ImGui_ImplVulkan_CreateWindow(ImGuiViewport* viewport)
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  ImGui_ImplVulkan_ViewportData* vd = IM_NEW(ImGui_ImplVulkan_ViewportData)();
  viewport->RendererUserData = vd;
  ImGui_ImplVulkanH_Window* wd = &vd->Window;
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;

  // Create surface
  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  VkResult err = (VkResult)platform_io.Platform_CreateVkSurface(viewport, (ImU64)v->Instance, (const void*)v->Allocator, (ImU64*)&wd->Surface);
  check_vk_result(err);

  // Check for WSI support
  VkBool32 res;
  vkGetPhysicalDeviceSurfaceSupportKHR(v->PhysicalDevice, v->QueueFamily, wd->Surface, &res);
  if (res != VK_TRUE)
  {
    IM_ASSERT(0); // Error: no WSI support on physical device
    return;
  }

  // Select Surface Format
  ImVector<VkFormat> requestSurfaceImageFormats;
  #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
  for (uint32_t n = 0; n < v->PipelineRenderingCreateInfo.colorAttachmentCount; n++)
    requestSurfaceImageFormats.push_back(v->PipelineRenderingCreateInfo.pColorAttachmentFormats[n]);
  #endif
  const VkFormat defaultFormats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
  for (VkFormat format : defaultFormats)
    requestSurfaceImageFormats.push_back(format);

  const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
  wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(v->PhysicalDevice,
                                                            wd->Surface,
                                                            requestSurfaceImageFormats.Data,
                                                            (size_t)requestSurfaceImageFormats.Size,
                                                            requestSurfaceColorSpace);

  // Select Present Mode
  // FIXME-VULKAN: Even thought mailbox seems to get us maximum framerate with a single window, it halves framerate with a second window etc. (w/ Nvidia and SDK 1.82.1)
  VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
  wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(v->PhysicalDevice, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
  // printf("[vulkan] Secondary window selected PresentMode = %d\n", wd->PresentMode);

  // Create SwapChain, RenderPass, Framebuffer, etc.
  wd->ClearEnable = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? false : true;
  wd->UseDynamicRendering = v->UseDynamicRendering;
  ImGui_ImplVulkanH_CreateOrResizeWindow(v->Instance,
                                         v->PhysicalDevice,
                                         v->Device,
                                         wd,
                                         v->QueueFamily,
                                         v->Allocator,
                                         (int)viewport->Size.x,
                                         (int)viewport->Size.y,
                                         v->MinImageCount);
  vd->WindowOwned = true;

  // Create pipeline (shared by all secondary viewports)
  if (bd->PipelineForViewports == VK_NULL_HANDLE)
    ImGui_ImplVulkan_CreatePipeline(v->Device, v->Allocator, VK_NULL_HANDLE, wd->RenderPass, VK_SAMPLE_COUNT_1_BIT, &bd->PipelineForViewports, 0);
}

static void ImGui_ImplVulkan_DestroyWindow(ImGuiViewport* viewport)
{
  // The main viewport (owned by the application) will always have RendererUserData == 0 since we didn't create the data for it.
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  if (ImGui_ImplVulkan_ViewportData* vd = (ImGui_ImplVulkan_ViewportData*)viewport->RendererUserData)
  {
    ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
    if (vd->WindowOwned)
      ImGui_ImplVulkanH_DestroyWindow(v->Instance, v->Device, &vd->Window, v->Allocator);
    ImGui_ImplVulkan_DestroyWindowRenderBuffers(v->Device, &vd->RenderBuffers, v->Allocator);
    IM_DELETE(vd);
  }
  viewport->RendererUserData = nullptr;
}

static void ImGui_ImplVulkan_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  ImGui_ImplVulkan_ViewportData* vd = (ImGui_ImplVulkan_ViewportData*)viewport->RendererUserData;
  if (vd == nullptr) // This is nullptr for the main viewport (which is left to the user/app to handle)
    return;
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
  vd->Window.ClearEnable = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? false : true;
  ImGui_ImplVulkanH_CreateOrResizeWindow(v->Instance, v->PhysicalDevice, v->Device, &vd->Window, v->QueueFamily, v->Allocator, (int)size.x, (int)size.y, v->MinImageCount);
}

static void ImGui_ImplVulkan_RenderWindow(ImGuiViewport* viewport, void*)
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  ImGui_ImplVulkan_ViewportData* vd = (ImGui_ImplVulkan_ViewportData*)viewport->RendererUserData;
  ImGui_ImplVulkanH_Window* wd = &vd->Window;
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;
  VkResult err;

  if (vd->SwapChainNeedRebuild)
  {
    ImGui_ImplVulkanH_CreateOrResizeWindow(v->Instance,
                                           v->PhysicalDevice,
                                           v->Device,
                                           wd,
                                           v->QueueFamily,
                                           v->Allocator,
                                           (int)viewport->Size.x,
                                           (int)viewport->Size.y,
                                           v->MinImageCount);
    vd->SwapChainNeedRebuild = false;
  }

  ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
  ImGui_ImplVulkanH_FrameSemaphores* fsd = &wd->FrameSemaphores[wd->SemaphoreIndex];
  {
    {
      err = vkAcquireNextImageKHR(v->Device, wd->Swapchain, UINT64_MAX, fsd->ImageAcquiredSemaphore, VK_NULL_HANDLE, &wd->FrameIndex);
      if (err == VK_ERROR_OUT_OF_DATE_KHR)
      {
        // Since we are not going to swap this frame anyway, it's ok that recreation happens on next frame.
        vd->SwapChainNeedRebuild = true;
        return;
      }
      check_vk_result(err);
      fd = &wd->Frames[wd->FrameIndex];
    }
    for (;;)
    {
      err = vkWaitForFences(v->Device, 1, &fd->Fence, VK_TRUE, 100);
      if (err == VK_SUCCESS)
        break;
      if (err == VK_TIMEOUT)
        continue;
      check_vk_result(err);
    }
    {
      err = vkResetCommandPool(v->Device, fd->CommandPool, 0);
      check_vk_result(err);
      VkCommandBufferBeginInfo info = {};
      info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
      check_vk_result(err);
    }
    {
      ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
      memcpy(&wd->ClearValue.color.float32[0], &clear_color, 4 * sizeof(float));
    }
  #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
    if (v->UseDynamicRendering)
    {
      // Transition swapchain image to a layout suitable for drawing.
      VkImageMemoryBarrier barrier = {};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      barrier.image = fd->Backbuffer;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 1;
      vkCmdPipelineBarrier(fd->CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

      VkRenderingAttachmentInfo attachmentInfo = {};
      attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
      attachmentInfo.imageView = fd->BackbufferView;
      attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      attachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
      attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      attachmentInfo.clearValue = wd->ClearValue;

      VkRenderingInfo renderingInfo = {};
      renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
      renderingInfo.renderArea.extent.width = wd->Width;
      renderingInfo.renderArea.extent.height = wd->Height;
      renderingInfo.layerCount = 1;
      renderingInfo.viewMask = 0;
      renderingInfo.colorAttachmentCount = 1;
      renderingInfo.pColorAttachments = &attachmentInfo;

      ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR(fd->CommandBuffer, &renderingInfo);
    }
    else
  #endif
    {
      VkRenderPassBeginInfo info = {};
      info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      info.renderPass = wd->RenderPass;
      info.framebuffer = fd->Framebuffer;
      info.renderArea.extent.width = wd->Width;
      info.renderArea.extent.height = wd->Height;
      info.clearValueCount = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? 0 : 1;
      info.pClearValues = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? nullptr : &wd->ClearValue;
      vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }
  }

  ImGui_ImplVulkan_RenderDrawData(viewport->DrawData, fd->CommandBuffer, bd->PipelineForViewports);

  {
  #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
    if (v->UseDynamicRendering)
    {
      ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR(fd->CommandBuffer);

      // Transition image to a layout suitable for presentation
      VkImageMemoryBarrier barrier = {};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      barrier.image = fd->Backbuffer;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 1;
      vkCmdPipelineBarrier(fd->CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    else
  #endif
    {
      vkCmdEndRenderPass(fd->CommandBuffer);
    }
    {
      VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkSubmitInfo info = {};
      info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      info.waitSemaphoreCount = 1;
      info.pWaitSemaphores = &fsd->ImageAcquiredSemaphore;
      info.pWaitDstStageMask = &wait_stage;
      info.commandBufferCount = 1;
      info.pCommandBuffers = &fd->CommandBuffer;
      info.signalSemaphoreCount = 1;
      info.pSignalSemaphores = &fsd->RenderCompleteSemaphore;

      err = vkEndCommandBuffer(fd->CommandBuffer);
      check_vk_result(err);
      err = vkResetFences(v->Device, 1, &fd->Fence);
      check_vk_result(err);
      err = vkQueueSubmit(v->Queue, 1, &info, fd->Fence);
      check_vk_result(err);
    }
  }
}

static void ImGui_ImplVulkan_SwapBuffers(ImGuiViewport* viewport, void*)
{
  ImGui_ImplVulkan_Data* bd = ImGui_ImplVulkan_GetBackendData();
  ImGui_ImplVulkan_ViewportData* vd = (ImGui_ImplVulkan_ViewportData*)viewport->RendererUserData;
  ImGui_ImplVulkanH_Window* wd = &vd->Window;
  ImGui_ImplVulkan_InitInfo* v = &bd->VulkanInitInfo;

  if (vd->SwapChainNeedRebuild) // Frame data became invalid in the middle of rendering
    return;

  VkResult err;
  uint32_t present_index = wd->FrameIndex;

  ImGui_ImplVulkanH_FrameSemaphores* fsd = &wd->FrameSemaphores[wd->SemaphoreIndex];
  VkPresentInfoKHR info = {};
  info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &fsd->RenderCompleteSemaphore;
  info.swapchainCount = 1;
  info.pSwapchains = &wd->Swapchain;
  info.pImageIndices = &present_index;
  err = vkQueuePresentKHR(v->Queue, &info);
  if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
  {
    vd->SwapChainNeedRebuild = true;
    return;
  }
  check_vk_result(err);

  wd->FrameIndex = (wd->FrameIndex + 1) % wd->ImageCount;             // This is for the next vkWaitForFences()
  wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
}

void ImGui_ImplVulkan_InitPlatformInterface()
{
  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    IM_ASSERT(platform_io.Platform_CreateVkSurface != nullptr && "Platform needs to setup the CreateVkSurface handler.");
  platform_io.Renderer_CreateWindow = ImGui_ImplVulkan_CreateWindow;
  platform_io.Renderer_DestroyWindow = ImGui_ImplVulkan_DestroyWindow;
  platform_io.Renderer_SetWindowSize = ImGui_ImplVulkan_SetWindowSize;
  platform_io.Renderer_RenderWindow = ImGui_ImplVulkan_RenderWindow;
  platform_io.Renderer_SwapBuffers = ImGui_ImplVulkan_SwapBuffers;
}


#endif // #if 0

void ImGui_ImplVulkan_ShutdownPlatformInterface()
{
  ImGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

#endif // #ifndef IMGUI_DISABLE
