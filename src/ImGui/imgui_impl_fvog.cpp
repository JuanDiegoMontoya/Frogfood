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
// - Common ImGui_ImplFvog_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper ImGui_ImplFvogH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in imgui_impl_vulkan.h.

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2024-XX-XX: Platform: Added support for multiple windows via the ImGuiPlatformIO interface.
//  2024-04-19: Vulkan: Added convenience support for Volk via IMGUI_IMPL_VULKAN_USE_VOLK define (you can also use IMGUI_IMPL_VULKAN_NO_PROTOTYPES + wrap Volk
//  via ImGui_ImplFvog_LoadFunctions().) 2024-02-14: *BREAKING CHANGE*: Moved RenderPass parameter from ImGui_ImplFvog_Init() function to
//  ImGui_ImplFvog_InitInfo structure. Not required when using dynamic rendering. 2024-02-12: *BREAKING CHANGE*: Dynamic rendering now require filling
//  PipelineRenderingCreateInfo structure. 2024-01-19: Vulkan: Fixed vkAcquireNextImageKHR() validation errors in VulkanSDK 1.3.275 by allocating one extra
//  semaphore than in-flight frames. (#7236) 2024-01-11: Vulkan: Fixed vkMapMemory() calls unnecessarily using full buffer size (#3957). Fixed MinAllocationSize
//  handing (#7189). 2024-01-03: Vulkan: Added MinAllocationSize field in ImGui_ImplFvog_InitInfo to workaround zealous "best practice" validation layer.
//  (#7189, #4238) 2024-01-03: Vulkan: Stopped creating command pools with VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT as we don't reset them. 2023-11-29:
//  Vulkan: Fixed mismatching allocator passed to vkCreateCommandPool() vs vkDestroyCommandPool(). (#7075) 2023-11-10: *BREAKING CHANGE*: Removed parameter from
//  ImGui_ImplFvog_CreateFontsTexture(): backend now creates its own command-buffer to upload fonts.
//              *BREAKING CHANGE*: Removed ImGui_ImplFvog_DestroyFontUploadObjects() which is now unnecessary as we create and destroy those objects in the
//              backend. ImGui_ImplFvog_CreateFontsTexture() is automatically called by NewFrame() the first time. You can call
//              ImGui_ImplFvog_CreateFontsTexture() again to recreate the font atlas texture. Added ImGui_ImplFvog_DestroyFontsTexture() but you probably
//              never need to call this.
//  2023-07-04: Vulkan: Added optional support for VK_KHR_dynamic_rendering. User needs to set init_info->UseDynamicRendering = true and
//  init_info->ColorAttachmentFormat. 2023-01-02: Vulkan: Fixed sampler passed to ImGui_ImplFvog_AddTexture() not being honored + removed a bunch of duplicate
//  code. 2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11. 2022-10-04: Vulkan: Added experimental ImGui_ImplFvog_RemoveTexture() for
//  api symmetry. (#914, #5738). 2022-01-20: Vulkan: Added support for ImTextureID as VkDescriptorSet. User need to call ImGui_ImplFvog_AddTexture(). Building
//  for 32-bit targets requires '#define ImTextureID ImU64'. (#914). 2021-10-15: Vulkan: Call vkCmdSetScissor() at the end of render a full-viewport to reduce
//  likehood of issues with people using VK_DYNAMIC_STATE_SCISSOR in their app without calling vkCmdSetScissor() explicitly every frame. 2021-06-29: Reorganized
//  backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX). 2021-03-22: Vulkan: Fix
//  mapped memory validation error when buffer sizes are not multiple of VkPhysicalDeviceLimits::nonCoherentAtomSize. 2021-02-18: Vulkan: Change blending
//  equation to preserve alpha in output buffer. 2021-01-27: Vulkan: Added support for custom function load and IMGUI_IMPL_VULKAN_NO_PROTOTYPES by using
//  ImGui_ImplFvog_LoadFunctions(). 2020-11-11: Vulkan: Added support for specifying which subpass to reference during VkPipeline creation. 2020-09-07:
//  Vulkan: Added VkPipeline parameter to ImGui_ImplFvog_RenderDrawData (default to one passed to ImGui_ImplFvog_Init). 2020-05-04: Vulkan: Fixed crash if
//  initial frame has no vertices. 2020-04-26: Vulkan: Fixed edge case where render callbacks wouldn't be called if the ImDrawData didn't have vertices. 2019-08-01:
//  Vulkan: Added support for specifying multisample count. Set ImGui_ImplFvog_InitInfo::MSAASamples to one of the VkSampleCountFlagBits values to use,
//  default is non-multisampled as before. 2019-05-29: Vulkan: Added support for large mesh (64K+ vertices), enable ImGuiBackendFlags_RendererHasVtxOffset flag.
//  2019-04-30: Vulkan: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2019-04-04: *BREAKING CHANGE*: Vulkan: Added ImageCount/MinImageCount fields in ImGui_ImplFvog_InitInfo, required for initialization (was previously a
//  hard #define IMGUI_VK_QUEUED_FRAMES 2). Added ImGui_ImplFvog_SetMinImageCount(). 2019-04-04: Vulkan: Added VkInstance argument to
//  ImGui_ImplFvogH_CreateWindow() optional helper. 2019-04-04: Vulkan: Avoid passing negative coordinates to vkCmdSetScissor, which debug validation layers
//  do not like. 2019-04-01: Vulkan: Support for 32-bit index buffer (#define ImDrawIdx unsigned int). 2019-02-16: Vulkan: Viewport and clipping rectangles
//  correctly using draw_data->FramebufferScale to allow retina display. 2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About
//  Window. 2018-08-25: Vulkan: Fixed mishandled VkSurfaceCapabilitiesKHR::maxImageCount=0 case. 2018-06-22: Inverted the parameters to
//  ImGui_ImplFvog_RenderDrawData() to be consistent with other backends. 2018-06-08: Misc: Extracted imgui_impl_vulkan.cpp/.h away from the old combined
//  GLFW+Vulkan example. 2018-06-08: Vulkan: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle. 2018-03-03:
//  Vulkan: Various refactor, created a couple of ImGui_ImplFvogH_XXX helper that the example can use and that viewport support will use. 2018-03-01: Vulkan:
//  Renamed ImGui_ImplFvog_Init_Info to ImGui_ImplFvog_InitInfo and fields to match more closely Vulkan terminology. 2018-02-16: Misc: Obsoleted the
//  io.RenderDrawListsFn callback, ImGui_ImplFvog_Render() calls ImGui_ImplFvog_RenderDrawData() itself. 2018-02-06: Misc: Removed call to ImGui::Shutdown()
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
struct ImGui_ImplFvog_FrameRenderBuffers;
struct ImGui_ImplFvog_WindowRenderBuffers;
static bool ImGui_ImplFvog_CreateDeviceObjects();
static void ImGui_ImplFvog_DestroyDeviceObjects();
static void ImGui_ImplFvog_DestroyFrameRenderBuffers(ImGui_ImplFvog_FrameRenderBuffers* buffers);
static void ImGui_ImplFvog_DestroyWindowRenderBuffers(ImGui_ImplFvog_WindowRenderBuffers* buffers);
static void ImGui_ImplFvogH_DestroyAllViewportsRenderBuffers(VkDevice device, const VkAllocationCallbacks* allocator);

  // Vulkan prototypes for use with custom loaders
  // (see description of IMGUI_IMPL_VULKAN_NO_PROTOTYPES in imgui_impl_vulkan.h
  #if defined(VK_NO_PROTOTYPES) && !defined(VOLK_H_)
    #define IMGUI_IMPL_VULKAN_USE_LOADER
static bool g_FunctionsLoaded = false;
  #else
static bool g_FunctionsLoaded = true;
  #endif

  #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
static PFN_vkCmdBeginRenderingKHR ImGuiImplFvogFuncs_vkCmdBeginRenderingKHR;
static PFN_vkCmdEndRenderingKHR ImGuiImplFvogFuncs_vkCmdEndRenderingKHR;
  #endif

// Reusable buffers used for rendering 1 current in-flight frame, for ImGui_ImplFvog_RenderDrawData()
// [Please zero-clear before use!]
struct ImGui_ImplFvog_FrameRenderBuffers
{
  std::optional<Fvog::Buffer> vertexBuffer;
  std::optional<Fvog::Buffer> vertexUploadBuffer;
  std::optional<Fvog::Buffer> indexBuffer;
  std::optional<Fvog::Buffer> indexUploadBuffer;
};

// Each viewport will hold 1 ImGui_ImplFvogH_WindowRenderBuffers
// [Please zero-clear before use!]
struct ImGui_ImplFvog_WindowRenderBuffers
{
  uint32_t Index;
  uint32_t Count;
  ImGui_ImplFvog_FrameRenderBuffers* FrameRenderBuffers;
};

// For multi-viewport support:
// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
struct ImGui_ImplFvog_ViewportData
{
  ImGui_ImplFvog_WindowRenderBuffers RenderBuffers; // Used by all viewports
  bool WindowOwned;
  bool SwapChainNeedRebuild; // Flag when viewport swapchain resized in the middle of processing a frame

  ImGui_ImplFvog_ViewportData()
  {
    WindowOwned = SwapChainNeedRebuild = false;
    memset(&RenderBuffers, 0, sizeof(RenderBuffers));
  }
  ~ImGui_ImplFvog_ViewportData() {}
};

// Vulkan data
struct ImGui_ImplFvog_Data
{
  Fvog::Device* device{};
  ImGui_ImplFvog_InitInfo VulkanInitInfo;
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
  ImGui_ImplFvog_WindowRenderBuffers MainWindowRenderBuffers;

  ImGui_ImplFvog_Data()
  {
    memset((void*)this, 0, sizeof(*this));
    BufferMemoryAlignment = 256;
  }
};

//-----------------------------------------------------------------------------
// SHADERS
//-----------------------------------------------------------------------------

// Forward Declarations
//static void ImGui_ImplFvog_InitPlatformInterface();
static void ImGui_ImplFvog_ShutdownPlatformInterface();

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

bool ImGui_ImplFvog_LoadFunctions(PFN_vkVoidFunction (*loader_func)(const char* function_name, void* user_data), void* user_data)
{
  // Load function pointers
  // You can use the default Vulkan loader using:
  //      ImGui_ImplFvog_LoadFunctions([](const char* function_name, void*) { return vkGetInstanceProcAddr(your_vk_isntance, function_name); });
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
  ImGuiImplFvogFuncs_vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(loader_func("vkCmdBeginRenderingKHR", user_data));
  ImGuiImplFvogFuncs_vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(loader_func("vkCmdEndRenderingKHR", user_data));
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
static ImGui_ImplFvog_Data* ImGui_ImplFvog_GetBackendData()
{
  return ImGui::GetCurrentContext() ? (ImGui_ImplFvog_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

static void check_vk_result(VkResult err)
{
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  if (!bd)
    return;
  ImGui_ImplFvog_InitInfo* v = &bd->VulkanInitInfo;
  if (v->CheckVkResultFn)
    v->CheckVkResultFn(err);
}

// Same as IM_MEMALIGN(). 'alignment' must be a power of two.
static inline VkDeviceSize AlignBufferSize(VkDeviceSize size, VkDeviceSize alignment)
{
  return (size + alignment - 1) & ~(alignment - 1);
}

static void ImGui_ImplFvog_SetupRenderState(
  ImDrawData* draw_data, VkPipeline pipeline, VkCommandBuffer command_buffer, ImGui_ImplFvog_FrameRenderBuffers* rb, int fb_width, int fb_height, ImGuiPushConstants& pushConstants)
{
  // Bind pipeline:
  {
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  }

  // Bind Vertex And Index Buffer:
  if (draw_data->TotalVtxCount > 0)
  {
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
  }
}

// Render function
void ImGui_ImplFvog_RenderDrawData(ImDrawData* draw_data, VkCommandBuffer command_buffer)
{
  // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
  int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
  int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
  if (fb_width <= 0 || fb_height <= 0)
    return;

  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  ImGui_ImplFvog_InitInfo* v = &bd->VulkanInitInfo;

  // Allocate array to store enough vertex/index buffers. Each unique viewport gets its own storage.
  ImGui_ImplFvog_ViewportData* viewport_renderer_data = (ImGui_ImplFvog_ViewportData*)draw_data->OwnerViewport->RendererUserData;
  IM_ASSERT(viewport_renderer_data != nullptr);
  ImGui_ImplFvog_WindowRenderBuffers* wrb = &viewport_renderer_data->RenderBuffers;
  if (wrb->FrameRenderBuffers == nullptr)
  {
    wrb->Index = 0;
    wrb->Count = v->ImageCount;
    wrb->FrameRenderBuffers = (ImGui_ImplFvog_FrameRenderBuffers*)IM_ALLOC(sizeof(ImGui_ImplFvog_FrameRenderBuffers) * wrb->Count);
    memset(wrb->FrameRenderBuffers, 0, sizeof(ImGui_ImplFvog_FrameRenderBuffers) * wrb->Count);
  }
  IM_ASSERT(wrb->Count == v->ImageCount);
  wrb->Index = (wrb->Index + 1) % wrb->Count;
  ImGui_ImplFvog_FrameRenderBuffers* rb = &wrb->FrameRenderBuffers[wrb->Index];

  if (draw_data->TotalVtxCount > 0)
  {
    // Create or resize the vertex/index buffers
    size_t vertex_size = AlignBufferSize(draw_data->TotalVtxCount * sizeof(ImDrawVert), bd->BufferMemoryAlignment);
    size_t index_size = AlignBufferSize(draw_data->TotalIdxCount * sizeof(ImDrawIdx), bd->BufferMemoryAlignment);
    if (!rb->vertexBuffer.has_value() || rb->vertexBuffer->SizeBytes() < vertex_size)
    {
      rb->vertexBuffer = Fvog::Buffer(*bd->device, {.size = vertex_size, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "ImGui Vertex Buffer");
    }
    if (!rb->indexBuffer.has_value() || rb->indexBuffer->SizeBytes() < index_size)
    {
      rb->indexBuffer = Fvog::Buffer(*bd->device, {.size = index_size, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "ImGui Index Buffer");
    }
    
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
  }

  auto pushConstants = ImGuiPushConstants{};

  // Setup desired Vulkan state
  ImGui_ImplFvog_SetupRenderState(draw_data, bd->Pipeline->Handle(), command_buffer, rb, fb_width, fb_height, pushConstants);

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
          ImGui_ImplFvog_SetupRenderState(draw_data, bd->Pipeline->Handle(), command_buffer, rb, fb_width, fb_height, pushConstants);
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

void ImGui_ImplFvog_CreateFontsTexture()
{
  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();

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

// You probably never need to call this, as it is called by ImGui_ImplFvog_CreateFontsTexture() and ImGui_ImplFvog_Shutdown().
void ImGui_ImplFvog_DestroyFontsTexture()
{
  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  
  io.Fonts->SetTexID(0);
  if (bd->FontImage)
  {
    bd->FontImage.reset();
  }
}

static void ImGui_ImplFvog_CreateShaderModules(VkDevice device)
{
  // Create the shader modules
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  if (!bd->ShaderModuleVert.has_value())
  {
    bd->ShaderModuleVert = Fvog::Shader(device, Fvog::PipelineStage::VERTEX_SHADER, glsl_shader_vert, "ImGui Vertex Shader");
  }
  if (!bd->ShaderModuleFrag.has_value())
  {
    bd->ShaderModuleFrag = Fvog::Shader(device, Fvog::PipelineStage::FRAGMENT_SHADER, glsl_shader_frag, "ImGui Fragment Shader");
  }
}

static [[nodiscard]] Fvog::GraphicsPipeline ImGui_ImplFvog_CreatePipeline(VkDevice device, VkSampleCountFlagBits MSAASamples)
{
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  ImGui_ImplFvog_CreateShaderModules(device);

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

static bool ImGui_ImplFvog_CreateDeviceObjects()
{
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  ImGui_ImplFvog_InitInfo* v = &bd->VulkanInitInfo;

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

  bd->Pipeline = ImGui_ImplFvog_CreatePipeline(v->Device->device_, v->MSAASamples);

  return true;
}

void ImGui_ImplFvog_DestroyDeviceObjects()
{
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  ImGui_ImplFvog_InitInfo* v = &bd->VulkanInitInfo;
  ImGui_ImplFvogH_DestroyAllViewportsRenderBuffers(v->Device->device_, nullptr);
  ImGui_ImplFvog_DestroyFontsTexture();
  
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

bool ImGui_ImplFvog_Init(ImGui_ImplFvog_InitInfo* info)
{
  IM_ASSERT(g_FunctionsLoaded && "Need to call ImGui_ImplFvog_LoadFunctions() if IMGUI_IMPL_VULKAN_NO_PROTOTYPES or VK_NO_PROTOTYPES are set!");
  
  #ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
    #ifndef IMGUI_IMPL_VULKAN_USE_LOADER
    ImGuiImplFvogFuncs_vkCmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetInstanceProcAddr(info->Instance, "vkCmdBeginRenderingKHR"));
    ImGuiImplFvogFuncs_vkCmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetInstanceProcAddr(info->Instance, "vkCmdEndRenderingKHR"));
    #endif
    IM_ASSERT(ImGuiImplFvogFuncs_vkCmdBeginRenderingKHR != nullptr);
    IM_ASSERT(ImGuiImplFvogFuncs_vkCmdEndRenderingKHR != nullptr);
  #else
    IM_ASSERT(0 && "Can't use dynamic rendering when neither VK_VERSION_1_3 or VK_KHR_dynamic_rendering is defined.");
  #endif

  ImGuiIO& io = ImGui::GetIO();
  IMGUI_CHECKVERSION();
  IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

  // Setup backend capabilities flags
  ImGui_ImplFvog_Data* bd = IM_NEW(ImGui_ImplFvog_Data)();
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

  ImGui_ImplFvog_CreateDeviceObjects();

  // Our render function expect RendererUserData to be storing the window render buffer we need (for the main viewport we won't use ->Window)
  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  main_viewport->RendererUserData = IM_NEW(ImGui_ImplFvog_ViewportData)();

  //if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  //  ImGui_ImplFvog_InitPlatformInterface();

  return true;
}

void ImGui_ImplFvog_Shutdown()
{
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
  ImGuiIO& io = ImGui::GetIO();

  // First destroy objects in all viewports
  ImGui_ImplFvog_DestroyDeviceObjects();

  // Manually delete main viewport render data in-case we haven't initialized for viewports
  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  if (ImGui_ImplFvog_ViewportData* vd = (ImGui_ImplFvog_ViewportData*)main_viewport->RendererUserData)
    IM_DELETE(vd);
  main_viewport->RendererUserData = nullptr;

  // Clean up windows
  ImGui_ImplFvog_ShutdownPlatformInterface();

  io.BackendRendererName = nullptr;
  io.BackendRendererUserData = nullptr;
  io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports);
  IM_DELETE(bd);
}

void ImGui_ImplFvog_NewFrame()
{
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplFvog_Init()?");

  if (!bd->FontImage)
    ImGui_ImplFvog_CreateFontsTexture();
}

void ImGui_ImplFvog_SetMinImageCount(uint32_t min_image_count)
{
  ImGui_ImplFvog_Data* bd = ImGui_ImplFvog_GetBackendData();
  IM_ASSERT(min_image_count >= 2);
  if (bd->VulkanInitInfo.MinImageCount == min_image_count)
    return;

  IM_ASSERT(0); // FIXME-VIEWPORT: Unsupported. Need to recreate all swap chains!
  ImGui_ImplFvog_InitInfo* v = &bd->VulkanInitInfo;
  VkResult err = vkDeviceWaitIdle(v->Device->device_);
  check_vk_result(err);
  //ImGui_ImplFvogH_DestroyAllViewportsRenderBuffers(v->Device, nullptr);

  bd->VulkanInitInfo.MinImageCount = min_image_count;
}

void ImGui_ImplFvog_DestroyFrameRenderBuffers(ImGui_ImplFvog_FrameRenderBuffers* buffers)
{
  buffers->vertexBuffer.reset();
  buffers->indexBuffer.reset();
}

void ImGui_ImplFvog_DestroyWindowRenderBuffers(ImGui_ImplFvog_WindowRenderBuffers* buffers)
{
  for (uint32_t n = 0; n < buffers->Count; n++)
    ImGui_ImplFvog_DestroyFrameRenderBuffers(&buffers->FrameRenderBuffers[n]);
  IM_FREE(buffers->FrameRenderBuffers);
  buffers->FrameRenderBuffers = nullptr;
  buffers->Index = 0;
  buffers->Count = 0;
}

void ImGui_ImplFvogH_DestroyAllViewportsRenderBuffers(VkDevice, const VkAllocationCallbacks*)
{
  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  for (int n = 0; n < platform_io.Viewports.Size; n++)
    if (ImGui_ImplFvog_ViewportData* vd = (ImGui_ImplFvog_ViewportData*)platform_io.Viewports[n]->RendererUserData)
      ImGui_ImplFvog_DestroyWindowRenderBuffers(&vd->RenderBuffers);
}

void ImGui_ImplFvog_ShutdownPlatformInterface()
{
  ImGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

#endif // #ifndef IMGUI_DISABLE
