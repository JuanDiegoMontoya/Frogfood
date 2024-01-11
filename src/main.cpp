/* 
 *
 * TODO rendering:
 * Core:
 * [X] glTF loader
 * [X] FSR 2
 * [X] PBR punctual lights
 * [ ] Skinned animation
 * [ ] Transparency
 *
 * Low-level
 * [X] Visibility buffer
 * [X] Frustum culling
 * [X] Hi-z occlusion culling
 * [ ] Clustered light culling
 * [-] Raster occlusion culling
 * [X] Multi-view
 * [X] Triangle culling: https://www.slideshare.net/gwihlidal/optimizing-the-graphics-pipeline-with-compute-gdc-2016
 *   [X] Back-facing
 *   [X] Zero-area
 *   [X] Small primitive (doesn't overlap pixel)
 *   [X] Frustum
 *
 * Atmosphere
 * [ ] Sky
 * [ ] Volumetric fog
 * [ ] Clouds
 *
 * Effects:
 * [X] Bloom
 *
 * Resolve:
 * [X] Auto exposure
 * [ ] Auto whitepoint
 * [ ] Local tonemapping
 * [ ] Purkinje shift
 *
 * Tryhard:
 * [ ] BVH builder
 * [ ] Path tracer
 * [ ] DDGI
 * [ ] Surfel GI
 *
 * TODO UI:
 * [X] Render into an ImGui window
 * [X] Install a font + header from here: https://github.com/juliettef/IconFontCppHeaders
 * [ ] Figure out an epic layout
 * [ ] Config file so stuff doesn't reset every time
 * [ ] Model browser
 * [ ] Command console
 *
 * Debugging:
 * [ ] Meshlet color view
 * [ ] Meshlet HZB LoD view
 * [ ] Frustum visualization for other views
 * [ ] Texture viewer (basically the one from RenderDoc):
 *   [ ] Register textures to list of viewable textures in GUI (perhaps just a vector of Texture*)
 *   [ ] Selector for range of displayable values
 *   [ ] Toggles for visible channels
 *   [ ] Selectors for mip level and array layer, if applicable
 *   [ ] Option to tint the window/image alpha to see the window behind it (i.e., overlay the image onto the main viewport)
 *   [ ] Toggle to scale the image to the viewport (would be helpful to view square textures like the hzb properly overlaid on the scene)
 *   [ ] Standard scroll zoom and drag controls
 */

//#include "FrogRenderer.h"

#include <tracy/Tracy.hpp>

#include <cstring>
#include <iostream>
#include <stdexcept>

#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <volk.h>

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <glslang/Public/ShaderLang.h>

#include "Fvog/Shader2.h"
#include "Fvog/Pipeline2.h"

//#include "RendererUtilities.h"

const char* gVertexSource = R"(
#version 460 core

//layout(location = 0) in vec2 a_pos;
//layout(location = 1) in vec3 a_color;

layout(location = 0) out vec3 v_color;

const vec2 positions[3] = {{-0, -0}, {1, -1}, {1, 1}};
const vec3 colors[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

void main()
{
  v_color = colors[gl_VertexIndex];
  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)";

const char* gFragmentSource = R"(
#version 460 core

layout(location = 0) out vec4 o_color;

layout(location = 0) in vec3 v_color;

void main()
{
  o_color = vec4(v_color, 1.0);
  //o_color = vec4(0.0, 1.0, 1.0, 1.0);
}
)";

#include <optional>
#include <memory>
#include <string_view>

template<class T>
[[nodiscard]] T* Address(T&& v)
{
  return std::addressof(v);
}

void CheckVkResult(VkResult result)
{
  // TODO: don't throw for certain non-success codes (since they aren't always errors)
  if (result != VK_SUCCESS)
  {
    throw std::runtime_error("result was not VK_SUCCESS");
  }
}

struct VulkanStuff
{
  VulkanStuff();
  ~VulkanStuff();

  void Run();

  constexpr static uint32_t frameOverlap = 2;

  struct PerFrameData
  {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkFence renderFence;
    VkSemaphore presentSemaphore;
    VkSemaphore renderSemaphore;
  };

  PerFrameData frameData[frameOverlap]{};

  PerFrameData& GetCurrentFrameData()
  {
    return frameData[frameNumber % frameOverlap];
  }

  GLFWwindow* window{};
  vkb::Instance vkbInstance{};
  VkSurfaceKHR surface{VK_NULL_HANDLE};
  vkb::PhysicalDevice vkbPhysicalDevice;
  vkb::Device vkbDevice;
  vkb::Swapchain vkbSwapchain;
  std::vector<VkImage> swapchainImages;
  std::vector<VkImageView> swapchainImageViews;
  std::optional<Fvog::GraphicsPipeline> pipeline;

  uint64_t frameNumber{};
  VkQueue graphicsQueue{};
  uint32_t graphicsQueueFamily{};
};

VulkanStuff::VulkanStuff()
{
  // init glfw and create window
  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error("rip");
  }

  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window = glfwCreateWindow(1920, 1080, "hello", nullptr, nullptr);
  if (!window)
  {
    throw std::runtime_error("rip");
  }

  // instance
  vkbInstance = vkb::InstanceBuilder()
    .set_app_name("Frogrenderer")
    .require_api_version(1, 3, 0)
    .request_validation_layers()
    .use_default_debug_messenger()
    .build()
    .value();

  if (volkInitialize() != VK_SUCCESS)
  {
    throw std::runtime_error("rip");
  }

  volkLoadInstance(vkbInstance.instance);

  // surface
  if (auto err = glfwCreateWindowSurface(vkbInstance.instance, window, nullptr, &surface); err != VK_SUCCESS)
  {
    const char* error_msg;
    if (int ret = glfwGetError(&error_msg))
    {
      std::cout << ret << " ";
      if (error_msg != nullptr)
        std::cout << error_msg;
      std::cout << "\n";
    }
    throw std::runtime_error("rip");
  }

  auto selector = vkb::PhysicalDeviceSelector{vkbInstance};

  // physical device
  vkbPhysicalDevice = selector
    .set_minimum_version(1, 3)
    .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
    .require_present()
    .set_surface(surface)
    .set_required_features({
      .multiDrawIndirect = true,
      .textureCompressionBC = true,
      .fragmentStoresAndAtomics = true,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      .shaderFloat64 = true,
      .shaderInt64 = true,
    })
    .set_required_features_11({
      .storageBuffer16BitAccess = true,
      .uniformAndStorageBuffer16BitAccess = true,
      .multiview = true,
      .variablePointersStorageBuffer = true,
      .variablePointers = true,
      .shaderDrawParameters = true,
    })
    .set_required_features_12({
      .drawIndirectCount = true,
      .storageBuffer8BitAccess = true,
      .uniformAndStorageBuffer8BitAccess = true,
      .shaderFloat16 = true,
      .shaderInt8 = true,
      .descriptorIndexing = true,
      .shaderInputAttachmentArrayDynamicIndexing = true,
      .shaderUniformTexelBufferArrayDynamicIndexing = true,
      .shaderStorageTexelBufferArrayDynamicIndexing = true,
      .shaderUniformBufferArrayNonUniformIndexing = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .shaderStorageBufferArrayNonUniformIndexing = true,
      .shaderStorageImageArrayNonUniformIndexing = true,
      .shaderUniformTexelBufferArrayNonUniformIndexing = true,
      .shaderStorageTexelBufferArrayNonUniformIndexing = true,
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingStorageImageUpdateAfterBind = true,
      .descriptorBindingStorageBufferUpdateAfterBind = true,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = true,
      .descriptorBindingUpdateUnusedWhilePending = true,
      .descriptorBindingPartiallyBound = true,
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
      .samplerFilterMinmax = true,
      .scalarBlockLayout = true,
      .imagelessFramebuffer = true,
      .uniformBufferStandardLayout = true,
      .shaderSubgroupExtendedTypes = true,
      .separateDepthStencilLayouts = true,
      .hostQueryReset = true,
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
      .vulkanMemoryModel = true,
      .vulkanMemoryModelDeviceScope = true,
      .subgroupBroadcastDynamicId = true,
    })
    .set_required_features_13({
      .shaderDemoteToHelperInvocation = true,
      .shaderTerminateInvocation = true,
      .synchronization2 = true,
      .dynamicRendering = true,
      .shaderIntegerDotProduct = true,
      .maintenance4 = true,
    })
    .select()
    .value();

  // device
  vkbDevice = vkb::DeviceBuilder{vkbPhysicalDevice}.build().value();
  graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
  graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

  // swapchain
  vkbSwapchain = vkb::SwapchainBuilder{vkbDevice}
    .set_old_swapchain(VK_NULL_HANDLE)
    .set_desired_min_image_count(2)
    .use_default_present_mode_selection()
    .set_desired_extent(1920, 1080)
    .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    .build()
    .value();

  swapchainImages = vkbSwapchain.get_images().value();
  swapchainImageViews = vkbSwapchain.get_image_views().value();

  // command pools and command buffers
  for (uint32_t i = 0; i < frameOverlap; i++)
  {
    CheckVkResult(vkCreateCommandPool(vkbDevice.device, Address(VkCommandPoolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = graphicsQueueFamily,
    }), nullptr, &frameData[i].commandPool));

    CheckVkResult(vkAllocateCommandBuffers(vkbDevice.device, Address(VkCommandBufferAllocateInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = frameData[i].commandPool,
      .commandBufferCount = 1,
    }), &frameData[i].commandBuffer));

    CheckVkResult(vkCreateFence(vkbDevice.device, Address(VkFenceCreateInfo{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    }), nullptr, &frameData[i].renderFence));

    CheckVkResult(vkCreateSemaphore(vkbDevice.device, Address(VkSemaphoreCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }), nullptr, &frameData[i].presentSemaphore));

    CheckVkResult(vkCreateSemaphore(vkbDevice.device, Address(VkSemaphoreCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    }), nullptr, &frameData[i].renderSemaphore));
  }

  if (!glslang::InitializeProcess())
  {
    throw std::runtime_error("rip");
  }
  
  auto pipelineLayout = VkPipelineLayout{};
  CheckVkResult(
    vkCreatePipelineLayout(
      vkbDevice.device,
      Address(VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
      }),
      nullptr,
      &pipelineLayout));

  const auto vertexShader = Fvog::Shader(vkbDevice.device, Fvog::PipelineStage::VERTEX_SHADER, gVertexSource);
  const auto fragmentShader = Fvog::Shader(vkbDevice.device, Fvog::PipelineStage::FRAGMENT_SHADER, gFragmentSource);
  const auto renderTargetFormats = {VK_FORMAT_B8G8R8A8_SRGB};
  pipeline = Fvog::GraphicsPipeline(vkbDevice.device, pipelineLayout, {
    .vertexShader = &vertexShader,
    .fragmentShader = &fragmentShader,
    .renderTargetFormats = {.colorAttachmentFormats = renderTargetFormats},
  });
}

VulkanStuff::~VulkanStuff()
{
  vkDeviceWaitIdle(vkbDevice.device);

  //vkDestroyPipeline(vkbDevice.device, pipeline, nullptr);

  glslang::FinalizeProcess();

  for (auto& frame : frameData)
  {
    vkDestroyCommandPool(vkbDevice.device, frame.commandPool, nullptr);
    vkDestroyFence(vkbDevice.device, frame.renderFence, nullptr);
    vkDestroySemaphore(vkbDevice.device, frame.renderSemaphore, nullptr);
    vkDestroySemaphore(vkbDevice.device, frame.presentSemaphore, nullptr);
  }

  vkb::destroy_swapchain(vkbSwapchain);

  for (auto view : swapchainImageViews)
  {
    vkDestroyImageView(vkbDevice.device, view, nullptr);
  }

  vkb::destroy_surface(vkbInstance, surface);
  vkb::destroy_device(vkbDevice);
  vkb::destroy_debug_utils_messenger(vkbInstance.instance, vkbInstance.debug_messenger, nullptr);
  vkDestroyInstance(vkbInstance.instance, nullptr);

  glfwDestroyWindow(window);
  glfwTerminate();
}

void VulkanStuff::Run()
{
  while (!glfwWindowShouldClose(window))
  {
    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE))
    {
      glfwSetWindowShouldClose(window, true);
    }

    CheckVkResult(vkWaitForFences(vkbDevice.device, 1, &GetCurrentFrameData().renderFence, VK_TRUE, UINT64_MAX));
    CheckVkResult(vkResetFences(vkbDevice.device, 1, &GetCurrentFrameData().renderFence));

    // TODO:
    // On success, this command returns
    //    VK_SUCCESS
    //    VK_TIMEOUT
    //    VK_NOT_READY
    //    VK_SUBOPTIMAL_KHR
    uint32_t swapchainImageIndex{};
    CheckVkResult(vkAcquireNextImage2KHR(vkbDevice.device, Address(VkAcquireNextImageInfoKHR{
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = vkbSwapchain.swapchain,
      .timeout = static_cast<uint64_t>(-1),
      .semaphore = GetCurrentFrameData().presentSemaphore,
      .deviceMask = 1,
    }), &swapchainImageIndex));

    auto commandBuffer = GetCurrentFrameData().commandBuffer;

    CheckVkResult(vkResetCommandPool(vkbDevice.device, GetCurrentFrameData().commandPool, 0));

    CheckVkResult(vkBeginCommandBuffer(commandBuffer, Address(VkCommandBufferBeginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    })));

    // undefined -> general
    vkCmdPipelineBarrier2(commandBuffer, Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = Address(VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL, // TODO: pick a more specific layout
        .image = swapchainImages[swapchainImageIndex],
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = VK_REMAINING_MIP_LEVELS,
          .layerCount = VK_REMAINING_ARRAY_LAYERS,
        },
      }),
    }));

    vkCmdClearColorImage(
      commandBuffer,
      swapchainImages[swapchainImageIndex],
      VK_IMAGE_LAYOUT_GENERAL,
      Address(VkClearColorValue{.float32 = {1, 1, std::sinf(frameNumber / 1000.0f) * .5f + .5f, 1}}),
      1,
      Address(VkImageSubresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
      }));

    vkCmdBeginRendering(commandBuffer, Address(VkRenderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {0, 0, 1920, 1080},
      .layerCount = 1,
      .viewMask = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = Address(VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImageViews[swapchainImageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      }),
    }));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->Handle());
    vkCmdSetViewport(commandBuffer, 0, 1, Address(VkViewport{0, 0, 1920, 1080, 0, 1}));
    vkCmdSetScissor(commandBuffer, 0, 1, Address(VkRect2D{0, 0, 1920, 1080}));
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(commandBuffer);

    // general -> presentable
    vkCmdPipelineBarrier2(commandBuffer, Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = Address(VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL, // TODO: pick a more specific layout
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = swapchainImages[swapchainImageIndex],
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = VK_REMAINING_MIP_LEVELS,
          .layerCount = VK_REMAINING_ARRAY_LAYERS,
        },
      }),
    }));

    // End recording
    CheckVkResult(vkEndCommandBuffer(commandBuffer));

    // Submit
    CheckVkResult(vkQueueSubmit2(
      graphicsQueue,
      1,
      Address(VkSubmitInfo2{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = Address(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = GetCurrentFrameData().presentSemaphore,
          .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        }),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = Address(VkCommandBufferSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
          .commandBuffer = commandBuffer,
        }),
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = Address(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = GetCurrentFrameData().renderSemaphore,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        }),
      }),
      GetCurrentFrameData().renderFence)
    );
    
    // Present
    CheckVkResult(vkQueuePresentKHR(graphicsQueue, Address(VkPresentInfoKHR{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &GetCurrentFrameData().renderSemaphore,
      .swapchainCount = 1,
      .pSwapchains = &vkbSwapchain.swapchain,
      .pImageIndices = &swapchainImageIndex,
    })));

    frameNumber++;
  }
}

int main()
{
  ZoneScoped;

  VulkanStuff stuff;

  stuff.Run();

  //auto appInfo = Application::CreateInfo{.name = "FrogRender", .vsync = true};
  //auto app = FrogRenderer(appInfo);
  //app.Run();

  return 0;
}
