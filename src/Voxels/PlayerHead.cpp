#include "PlayerHead.h"

#define GLFW_INCLUDE_VULKAN

#include "Assets.h"
#include "stb_image.h"
#include <GLFW/glfw3.h>
#include <VkBootstrap.h>
#include <glslang/Public/ShaderLang.h>
#include <volk.h>

#include "Fvog/Buffer2.h"
#include "Fvog/Pipeline2.h"
#include "Fvog/Rendering2.h"
#include "Fvog/Shader2.h"
#include "Fvog/Texture2.h"
#include "Fvog/detail/ApiToEnum2.h"
#include "Fvog/detail/Common.h"
#include "PipelineManager.h"
#include "VoxelRenderer.h"

#include "ImGui/imgui_impl_fvog.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <implot.h>

#include <glm/gtc/constants.hpp>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include <bit>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace
{
  VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*)
  {
    if (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
    {
      return VK_FALSE;
    }

    auto ms = vkb::to_string_message_severity(messageSeverity);
    auto mt = vkb::to_string_message_type(messageType);
    printf("[%s: %s]\n%s\n", ms, mt, pCallbackData->pMessage);

    return VK_FALSE;
  }

  std::vector<VkImageView> MakeSwapchainImageViews(VkDevice device, std::span<const VkImage> swapchainImages, VkFormat format)
  {
    auto imageViews = std::vector<VkImageView>();
    for (int i = 0; auto image : swapchainImages)
    {
      VkImageView imageView{};
      Fvog::detail::CheckVkResult(vkCreateImageView(device,
        Fvog::detail::Address(VkImageViewCreateInfo{
          .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image    = image,
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format   = format,
          .subresourceRange =
            VkImageSubresourceRange{
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .levelCount = 1,
              .layerCount = 1,
            },
        }),
        nullptr,
        &imageView));
      imageViews.emplace_back(imageView);

      // TODO: gate behind compile-time switch
      vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
        Fvog::detail::Address(VkDebugUtilsObjectNameInfoEXT{
          .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
          .objectType   = VK_OBJECT_TYPE_IMAGE,
          .objectHandle = reinterpret_cast<uint64_t>(image),
          .pObjectName  = (std::string("Swapchain Image ") + std::to_string(i)).c_str(),
        }));
      vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
        Fvog::detail::Address(VkDebugUtilsObjectNameInfoEXT{
          .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
          .objectType   = VK_OBJECT_TYPE_IMAGE_VIEW,
          .objectHandle = reinterpret_cast<uint64_t>(imageView),
          .pObjectName  = (std::string("Swapchain Image View ") + std::to_string(i)).c_str(),
        }));

      i++;
    }
    return imageViews;
  }
} // namespace

// This class provides static callbacks for GLFW.
// It has access to the private members of PlayerHead and assumes a pointer to it is present in the window's user pointer.
class ApplicationAccess2
{
public:
  static void CursorPosCallback(GLFWwindow* window, double currentCursorX, double currentCursorY)
  {
    auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    app->inputSystem_->CursorPosCallback(currentCursorX, currentCursorY);
  }

  static void CursorEnterCallback(GLFWwindow* window, int entered)
  {
    auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    app->inputSystem_->CursorEnterCallback(entered);
  }

  static void FramebufferResizeCallback(GLFWwindow* window, int newWidth, int newHeight)
  {
    auto* app                    = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    app->windowFramebufferWidth  = newWidth;
    app->windowFramebufferHeight = newHeight;

    if (newWidth > 0 && newHeight > 0)
    {
      app->RemakeSwapchain(newWidth, newHeight);
      // app->shouldResizeNextFrame = true;
      // app->Draw(0.016);
    }
  }

  static void PathDropCallback(GLFWwindow*, int, const char**)
  {
    //auto* app = static_cast<PlayerHead*>(glfwGetWindowUserPointer(window));
    //app->OnPathDrop({paths, static_cast<size_t>(count)});
  }
};

static auto MakeVkbSwapchain(const vkb::Device& device,
  uint32_t width,
  uint32_t height,
  [[maybe_unused]] VkPresentModeKHR presentMode,
  uint32_t imageCount,
  VkSwapchainKHR oldSwapchain,
  VkSurfaceFormatKHR format)
{
  return vkb::SwapchainBuilder{device}
    .set_desired_min_image_count(imageCount)
    .set_old_swapchain(oldSwapchain)
    .set_desired_present_mode(presentMode)
    .add_fallback_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
    .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
    .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_RELAXED_KHR)
    .add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
    .set_desired_extent(width, height)
    .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    .set_desired_format(format)
    .build()
    .value();
}

void PlayerHead::VariableUpdatePre(DeltaTime dt, World& world)
{
  worldThisFrame_   = &world;

  inputSystem_->VariableUpdatePre(dt, world, swapchainOk);
}

void PlayerHead::VariableUpdatePost(DeltaTime dt, World& world)
{
  ZoneScopedN("Frame");

  for (auto&& [entity, transform, interpolatedTransform] : world.GetRegistry().view<Transform, InterpolatedTransform>().each())
  {
    interpolatedTransform.accumulator += dt.game;
    if (auto* renderTransform = world.GetRegistry().try_get<RenderTransform>(entity))
    {
      const auto alpha                    = interpolatedTransform.accumulator * world.GetSingletonComponent<TickRate>().hz;
      renderTransform->transform.position = glm::mix(interpolatedTransform.previousTransform.position, transform.position, alpha);
      renderTransform->transform.rotation = glm::slerp(interpolatedTransform.previousTransform.rotation, transform.rotation, alpha);
      renderTransform->transform.scale    = glm::mix(interpolatedTransform.previousTransform.scale, transform.scale, alpha);
    }
  }

  if (!swapchainOk)
  {
    return;
  }

  if (shouldRemakeSwapchainNextFrame)
  {
    swapchainFormat_ = nextSwapchainFormat_; // "Flush" nextSwapchainFormat_
    RemakeSwapchain(windowFramebufferWidth, windowFramebufferHeight);
    shouldRemakeSwapchainNextFrame = false;
  }

  if (windowFramebufferWidth > 0 && windowFramebufferHeight > 0)
  {
    Draw();
  }

  if (glfwWindowShouldClose(window))
  {
    world.CreateSingletonComponent<CloseApplication>();
  }
}

PlayerHead::PlayerHead(const CreateInfo& createInfo) : presentMode(createInfo.presentMode)
{
  ZoneScoped;

  {
    ZoneScopedN("Initialize GLFW");
    if (!glfwInit())
    {
      throw std::runtime_error("Failed to initialize GLFW");
    }
  }

  destroyList_.Push([] { glfwTerminate(); });

  glfwSetErrorCallback([](int, const char* desc) { std::cout << "GLFW error: " << desc << '\n'; });

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_MAXIMIZED, createInfo.maximize);
  glfwWindowHint(GLFW_DECORATED, createInfo.decorate);
  glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);

  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  if (monitor == nullptr)
  {
    throw std::runtime_error("No monitor detected");
  }
  const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
  {
    ZoneScopedN("Create Window");
    window = glfwCreateWindow(static_cast<int>(videoMode->width * .75), static_cast<int>(videoMode->height * .75), createInfo.name.data(), nullptr, nullptr);
    if (!window)
    {
      throw std::runtime_error("Failed to create window");
    }
  }

  int xSize{};
  int ySize{};
  glfwGetFramebufferSize(window, &xSize, &ySize);
  windowFramebufferWidth  = static_cast<uint32_t>(xSize);
  windowFramebufferHeight = static_cast<uint32_t>(ySize);

  int monitorLeft{};
  int monitorTop{};
  glfwGetMonitorPos(monitor, &monitorLeft, &monitorTop);

  glfwSetWindowPos(window, videoMode->width / 2 - windowFramebufferWidth / 2 + monitorLeft, videoMode->height / 2 - windowFramebufferHeight / 2 + monitorTop);

  glfwSetWindowUserPointer(window, this);

  glfwSetCursorPosCallback(window, ApplicationAccess2::CursorPosCallback);
  glfwSetCursorEnterCallback(window, ApplicationAccess2::CursorEnterCallback);
  glfwSetFramebufferSizeCallback(window, ApplicationAccess2::FramebufferResizeCallback);
  glfwSetDropCallback(window, ApplicationAccess2::PathDropCallback);

  // Load app icon
  {
    int x             = 0;
    int y             = 0;
    const auto pixels = stbi_load((GetTextureDirectory() / "froge.png").string().c_str(), &x, &y, nullptr, 4);
    if (pixels)
    {
      const auto image = GLFWimage{
        .width  = x,
        .height = y,
        .pixels = pixels,
      };
      glfwSetWindowIcon(window, 1, &image);
      stbi_image_free(pixels);
    }
  }

  // Initialize Vulkan
  // instance
  {
    ZoneScopedN("Create Vulkan Instance");
    instance_ = vkb::InstanceBuilder()
                  .set_app_name("Frogrenderer")
                  .require_api_version(1, 3, 0)
                  .set_debug_callback(vulkan_debug_callback)
                  .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                  .enable_extension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME)
                  .build()
                  .value();

    destroyList_.Push([this] { vkb::destroy_instance(instance_); });
  }

  {
    ZoneScopedN("Initialize Volk");
    if (volkInitialize() != VK_SUCCESS)
    {
      throw std::runtime_error("rip");
    }

    destroyList_.Push([] { volkFinalize(); });

    volkLoadInstance(instance_);
  }

  // surface
  {
    ZoneScopedN("Create Window Surface");
    if (auto err = glfwCreateWindowSurface(instance_, window, nullptr, &surface_); err != VK_SUCCESS)
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
  }

  destroyList_.Push([this] { vkDestroySurfaceKHR(instance_, surface_, nullptr); });

  // device
  {
    ZoneScopedN("Create Device");
    Fvog::CreateDevice(instance_, surface_);
  }

  {
    ZoneScopedN("Create Pipeline Manager");
    CreateGlobalPipelineManager();
  }

  // swapchain
  {
    ZoneScopedN("Create Swapchain");
    swapchain_ =
      MakeVkbSwapchain(Fvog::GetDevice().device_, windowFramebufferWidth, windowFramebufferHeight, presentMode, numSwapchainImages, VK_NULL_HANDLE, swapchainFormat_);

    swapchainImages_     = swapchain_.get_images().value();
    swapchainImageViews_ = MakeSwapchainImageViews(Fvog::GetDevice().device_, swapchainImages_, swapchainFormat_.format);

    // Get available formats for this surface
    uint32_t surfaceFormatCount{};
    vkGetPhysicalDeviceSurfaceFormatsKHR(Fvog::GetDevice().physicalDevice_, surface_, &surfaceFormatCount, nullptr);
    availableSurfaceFormats_.resize(surfaceFormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(Fvog::GetDevice().physicalDevice_, surface_, &surfaceFormatCount, availableSurfaceFormats_.data());
  }

  glslang::InitializeProcess();
  destroyList_.Push([] { glslang::FinalizeProcess(); });

  // Initialize Tracy
  tracyVkContext_ = TracyVkContextHostCalibrated(Fvog::GetDevice().physicalDevice_,
    Fvog::GetDevice().device_,
    vkResetQueryPool,
    vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
    vkGetCalibratedTimestampsEXT);

  // Initialize ImGui and a backend for it.
  // Because we allow the GLFW backend to install callbacks, it will automatically call our own that we provided.
  ImGui::CreateContext();
  destroyList_.Push([] { ImGui::DestroyContext(); });
  ImPlot::CreateContext();
  destroyList_.Push([] { ImPlot::DestroyContext(); });
  ImGui_ImplGlfw_InitForVulkan(window, true);
  destroyList_.Push([] { ImGui_ImplGlfw_Shutdown(); });

  // ImGui may create many sets, but each will only have one combined image sampler
  vkCreateDescriptorPool(Fvog::GetDevice().device_,
    Fvog::detail::Address(VkDescriptorPoolCreateInfo{
      .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      .maxSets       = 1234, // TODO: make this constant a variable
      .poolSizeCount = 1,
      .pPoolSizes    = Fvog::detail::Address(VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2}),
    }),
    nullptr,
    &imguiDescriptorPool_);

  auto imguiVulkanInitInfo = ImGui_ImplFvog_InitInfo{
    .Instance        = instance_,
    .PhysicalDevice  = Fvog::GetDevice().physicalDevice_,
    .QueueFamily     = Fvog::GetDevice().graphicsQueueFamilyIndex_,
    .Queue           = Fvog::GetDevice().graphicsQueue_,
    .DescriptorPool  = imguiDescriptorPool_,
    .MinImageCount   = swapchain_.image_count,
    .ImageCount      = swapchain_.image_count,
    .CheckVkResultFn = Fvog::detail::CheckVkResult,
  };

  ImGui_ImplFvog_LoadFunctions([](const char* functionName, void* vulkanInstance)
    { return vkGetInstanceProcAddr(*static_cast<VkInstance*>(vulkanInstance), functionName); },
    &instance_.instance);
  ImGui_ImplFvog_Init(&imguiVulkanInitInfo);
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  voxelRenderer_ = std::make_unique<VoxelRenderer>(this, *createInfo.world);
  inputSystem_   = std::make_unique<InputSystem>(window);

  // Inform the user that the renderer is done loading
  glfwRequestWindowAttention(window);
}

PlayerHead::~PlayerHead()
{
  ZoneScoped;

  voxelRenderer_.reset();

  // Must happen before device is destroyed, thus cannot go in the destroy list
  ImGui_ImplFvog_Shutdown();

  vkDestroyDescriptorPool(Fvog::GetDevice().device_, imguiDescriptorPool_, nullptr);

#ifdef TRACY_ENABLE
  DestroyVkContext(tracyVkContext_);
#endif

  vkb::destroy_swapchain(swapchain_);

  for (auto view : swapchainImageViews_)
  {
    vkDestroyImageView(Fvog::GetDevice().device_, view, nullptr);
  }

  DestroyGlobalPipelineManager();

  Fvog::DestroyDevice();
}

void PlayerHead::Draw()
{
  ZoneScoped;

  auto prevTime  = timeOfLastDraw;
  timeOfLastDraw = glfwGetTime();
  auto dtDraw    = timeOfLastDraw - prevTime;

  Fvog::GetDevice().frameNumber++;
  auto& currentFrameData = Fvog::GetDevice().GetCurrentFrameData();

  {
    ZoneScopedN("vkWaitSemaphores (graphics queue timeline)");
    vkWaitSemaphores(Fvog::GetDevice().device_,
      Fvog::detail::Address(VkSemaphoreWaitInfo{
        .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores    = &Fvog::GetDevice().graphicsQueueTimelineSemaphore_,
        .pValues        = &currentFrameData.renderTimelineSemaphoreWaitValue,
      }),
      UINT64_MAX);
  }

  // Garbage collection
  Fvog::GetDevice().FreeUnusedResources();

  uint32_t swapchainImageIndex{};

  {
    // https://gist.github.com/nanokatze/bb03a486571e13a7b6a8709368bd87cf#file-handling-window-resize-md
    ZoneScopedN("vkAcquireNextImage2KHR");
    if (auto acquireResult = vkAcquireNextImage2KHR(Fvog::GetDevice().device_,
          Fvog::detail::Address(VkAcquireNextImageInfoKHR{
            .sType      = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
            .swapchain  = swapchain_,
            .timeout    = static_cast<uint64_t>(-1),
            .semaphore  = currentFrameData.swapchainSemaphore,
            .deviceMask = 1,
          }),
          &swapchainImageIndex);
        acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
      swapchainOk = false;
    }
    else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
      throw std::runtime_error("vkAcquireNextImage failed");
    }

    if (!swapchainOk)
    {
      return;
    }
  }

  auto commandBuffer = currentFrameData.commandBuffer;

  {
    ZoneScopedN("vkResetCommandPool");
    Fvog::detail::CheckVkResult(vkResetCommandPool(Fvog::GetDevice().device_, currentFrameData.commandPool, 0));
  }

  {
    ZoneScopedN("vkBeginCommandBuffer");
    Fvog::detail::CheckVkResult(vkBeginCommandBuffer(commandBuffer,
      Fvog::detail::Address(VkCommandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      })));
  }

  auto ctx = Fvog::Context(commandBuffer);

  {
    ZoneScopedN("Begin ImGui frame");
    ImGui_ImplFvog_NewFrame();
    {
      ZoneScopedN("ImGui_ImplGlfw_NewFrame");
      ImGui_ImplGlfw_NewFrame();
    }
    {
      ZoneScopedN("ImGui::NewFrame");
      ImGui::NewFrame();
    }
  }

  {
    {
      if (renderCallback_)
      {
        TracyVkZone(tracyVkContext_, commandBuffer, "OnRender");
        renderCallback_((float)dtDraw, *worldThisFrame_, commandBuffer, swapchainImageIndex);
      }
    }
    {
      if (guiCallback_)
      {
        TracyVkZone(tracyVkContext_, commandBuffer, "OnGui");
        guiCallback_((float)dtDraw, *worldThisFrame_, commandBuffer);
      }
    }
  }

  // Render ImGui
  // A frame marker is inserted to distinguish ImGui rendering from the application's in a debugger.
  {
    ZoneScopedN("Draw UI");
    auto marker = ctx.MakeScopedDebugMarker("ImGui");
    ImGui::Render();
    auto* drawData = ImGui::GetDrawData();
    if (drawData->CmdListsCount > 0)
    {
      ctx.Barrier();
      vkCmdBeginRendering(commandBuffer,
        Fvog::detail::Address(VkRenderingInfo{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
          .renderArea                                = {{}, {windowFramebufferWidth, windowFramebufferHeight}},
          .layerCount                                = 1,
          .colorAttachmentCount                      = 1,
          .pColorAttachments                         = Fvog::detail::Address(VkRenderingAttachmentInfo{
                                    .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                    .imageView   = swapchainImageViews_[swapchainImageIndex],
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
                                    .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
          })}));
      // auto marker = Fwog::ScopedDebugMarker("Draw GUI");
      const bool isSurfaceHDR =
        swapchainFormat_.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT || swapchainFormat_.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
      ImGui_ImplFvog_RenderDrawData(drawData, commandBuffer, swapchainFormat_, isSurfaceHDR ? maxDisplayNits : 1);
      vkCmdEndRendering(commandBuffer);
    }

    ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  }

  ctx.Barrier();
  auto marker = ctx.MakeScopedDebugMarker("Everything else");

  {
    TracyVkCollect(tracyVkContext_, commandBuffer);

    {
      ZoneScopedN("End Recording");
      Fvog::detail::CheckVkResult(vkEndCommandBuffer(commandBuffer));
    }

    {
      ZoneScopedN("Submit");
      const auto queueSubmitSignalSemaphores = std::array{VkSemaphoreSubmitInfo{
                                                            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                                            .semaphore = Fvog::GetDevice().graphicsQueueTimelineSemaphore_,
                                                            .value     = Fvog::GetDevice().frameNumber,
                                                            .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                                                          },
        VkSemaphoreSubmitInfo{
          .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = currentFrameData.renderSemaphore,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        }};

      Fvog::detail::CheckVkResult(vkQueueSubmit2(Fvog::GetDevice().graphicsQueue_,
        1,
        Fvog::detail::Address(VkSubmitInfo2{
          .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
          .waitSemaphoreInfoCount   = 1,
          .pWaitSemaphoreInfos      = Fvog::detail::Address(VkSemaphoreSubmitInfo{
                 .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                 .semaphore = currentFrameData.swapchainSemaphore,
                 .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
          }),
          .commandBufferInfoCount   = 1,
          .pCommandBufferInfos      = Fvog::detail::Address(VkCommandBufferSubmitInfo{
                 .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                 .commandBuffer = commandBuffer,
          }),
          .signalSemaphoreInfoCount = static_cast<uint32_t>(queueSubmitSignalSemaphores.size()),
          .pSignalSemaphoreInfos    = queueSubmitSignalSemaphores.data(),
        }),
        VK_NULL_HANDLE));

      currentFrameData.renderTimelineSemaphoreWaitValue = Fvog::GetDevice().frameNumber;
    }

    {
      ZoneScopedN("Present");
      if (auto presentResult = vkQueuePresentKHR(Fvog::GetDevice().graphicsQueue_,
            Fvog::detail::Address(VkPresentInfoKHR{
              .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
              .waitSemaphoreCount = 1,
              .pWaitSemaphores    = &currentFrameData.renderSemaphore,
              .swapchainCount     = 1,
              .pSwapchains        = &swapchain_.swapchain,
              .pImageIndices      = &swapchainImageIndex,
            }));
          presentResult == VK_ERROR_OUT_OF_DATE_KHR)
      {
        swapchainOk = false;
      }
      else if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
      {
        throw std::runtime_error("vkQueuePresent failed");
      }
    }
  }

  FrameMark;
}

void PlayerHead::RemakeSwapchain([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight)
{
  ZoneScoped;

  assert(newWidth > 0 && newHeight > 0);

  {
    ZoneScopedN("Device Wait Idle");
    vkDeviceWaitIdle(Fvog::GetDevice().device_);
  }

  const auto oldSwapchain = swapchain_;

  {
    ZoneScopedN("Create New Swapchain");
    swapchain_ =
      MakeVkbSwapchain(Fvog::GetDevice().device_, windowFramebufferWidth, windowFramebufferHeight, presentMode, numSwapchainImages, oldSwapchain, swapchainFormat_);
  }

  {
    ZoneScopedN("Destroy Old Swapchain");

    // Technically UB, but in practice the WFI makes it work
    vkb::destroy_swapchain(oldSwapchain);

    for (auto view : swapchainImageViews_)
    {
      vkDestroyImageView(Fvog::GetDevice().device_, view, nullptr);
    }
  }

  swapchainImages_     = swapchain_.get_images().value();
  swapchainImageViews_ = MakeSwapchainImageViews(Fvog::GetDevice().device_, swapchainImages_, swapchainFormat_.format);

  swapchainOk = true;

  shouldResizeNextFrame = true;

  // This line triggers the recreation of window-size-dependent resources.
  // Commenting it out results in a faster, but lower quality resizing experience.
  // OnUpdate(0);
  Draw();
}

void DestroyList2::Push(std::function<void()> fn)
{
  destructorList.emplace_back(std::move(fn));
}

DestroyList2::~DestroyList2()
{
  for (auto it = destructorList.rbegin(); it != destructorList.rend(); it++)
  {
    (*it)();
  }
}