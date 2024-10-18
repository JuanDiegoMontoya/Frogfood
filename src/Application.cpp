#include "Application.h"
#define GLFW_INCLUDE_VULKAN

#include "stb_image.h"
#include <GLFW/glfw3.h>
#include <volk.h>
#include <VkBootstrap.h>
#include <glslang/Public/ShaderLang.h>

#include "Fvog/Buffer2.h"
#include "Fvog/Shader2.h"
#include "Fvog/Pipeline2.h"
#include "Fvog/Texture2.h"
#include "Fvog/Rendering2.h"
#include "Fvog/detail/Common.h"
#include "Fvog/detail/ApiToEnum2.h"
#include "PipelineManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include "ImGui/imgui_impl_fvog.h"
#include <implot.h>

#include <glm/gtc/constants.hpp>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include <bit>
#include <exception>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>

#ifdef TRACY_ENABLE
#include <cstdlib>
void* operator new(std::size_t count)
{
  auto ptr = std::malloc(count);
  TracyAlloc(ptr, count);
  return ptr;
}

void operator delete(void* ptr) noexcept
{
  TracyFree(ptr);
  std::free(ptr);
}

void* operator new[](std::size_t count)
{
  auto ptr = std::malloc(count);
  TracyAlloc(ptr, count);
  return ptr;
}

void operator delete[](void* ptr) noexcept
{
  TracyFree(ptr);
  std::free(ptr);
}

void* operator new(std::size_t count, const std::nothrow_t&) noexcept
{
  auto ptr = std::malloc(count);
  TracyAlloc(ptr, count);
  return ptr;
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
  TracyFree(ptr);
  std::free(ptr);
}

void* operator new[](std::size_t count, const std::nothrow_t&) noexcept
{
  auto ptr = std::malloc(count);
  TracyAlloc(ptr, count);
  return ptr;
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
  TracyFree(ptr);
  std::free(ptr);
}
#endif

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
    for (auto image : swapchainImages)
    {
      VkImageView imageView{};
      Fvog::detail::CheckVkResult(vkCreateImageView(
      device,
      Fvog::detail::Address(VkImageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = VkImageSubresourceRange{
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = 1,
          .layerCount = 1,
        },
      }), nullptr, &imageView));
      imageViews.emplace_back(imageView);
    }
    return imageViews;
  }
}

// This class provides static callbacks for GLFW.
// It has access to the private members of Application and assumes a pointer to it is present in the window's user pointer.
class ApplicationAccess
{
public:
  static void CursorPosCallback(GLFWwindow* window, double currentCursorX, double currentCursorY)
  {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app->cursorJustEnteredWindow)
    {
      app->cursorPos = {currentCursorX, currentCursorY};
      app->cursorJustEnteredWindow = false;
    }

    app->cursorFrameOffset +=
        glm::dvec2{currentCursorX - app->cursorPos.x, app->cursorPos.y - currentCursorY};
    app->cursorPos = {currentCursorX, currentCursorY};
  }

  static void CursorEnterCallback(GLFWwindow* window, int entered)
  {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (entered)
    {
      app->cursorJustEnteredWindow = true;
    }
  }

  static void FramebufferResizeCallback(GLFWwindow* window, int newWidth, int newHeight)
  {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    app->windowFramebufferWidth = newWidth;
    app->windowFramebufferHeight = newHeight;

    if (newWidth > 0 && newHeight > 0)
    {
      app->RemakeSwapchain(newWidth, newHeight);
      //app->shouldResizeNextFrame = true;
      //app->Draw(0.016);
    }
  }

  static void PathDropCallback(GLFWwindow* window, int count, const char** paths)
  {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    app->OnPathDrop({paths, static_cast<size_t>(count)});
  }
};

std::string Application::LoadFile(const std::filesystem::path& path)
{
  std::ifstream file{path};
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::pair<std::unique_ptr<std::byte[]>, std::size_t> Application::LoadBinaryFile(const std::filesystem::path& path)
{
  std::size_t fsize = std::filesystem::file_size(path);
  auto memory = std::make_unique<std::byte[]>(fsize);
  std::ifstream file{path, std::ifstream::binary};
  std::copy(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), reinterpret_cast<char*>(memory.get()));
  return {std::move(memory), fsize};
}

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

Application::Application(const CreateInfo& createInfo)
  : presentMode(createInfo.presentMode)
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
  windowFramebufferWidth = static_cast<uint32_t>(xSize);
  windowFramebufferHeight = static_cast<uint32_t>(ySize);

  int monitorLeft{};
  int monitorTop{};
  glfwGetMonitorPos(monitor, &monitorLeft, &monitorTop);

  glfwSetWindowPos(window, videoMode->width / 2 - windowFramebufferWidth / 2 + monitorLeft, videoMode->height / 2 - windowFramebufferHeight / 2 + monitorTop);

  glfwSetWindowUserPointer(window, this);

  glfwSetCursorPosCallback(window, ApplicationAccess::CursorPosCallback);
  glfwSetCursorEnterCallback(window, ApplicationAccess::CursorEnterCallback);
  glfwSetFramebufferSizeCallback(window, ApplicationAccess::FramebufferResizeCallback);
  glfwSetDropCallback(window, ApplicationAccess::PathDropCallback);

  // Load app icon
  {
    int x = 0;
    int y = 0;
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
    swapchain_ = MakeVkbSwapchain(Fvog::GetDevice().device_,
      windowFramebufferWidth,
      windowFramebufferHeight,
      presentMode,
      numSwapchainImages,
      VK_NULL_HANDLE,
      swapchainFormat_);
    
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
  vkCreateDescriptorPool(Fvog::GetDevice().device_, Fvog::detail::Address(VkDescriptorPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    .maxSets = 1234, // TODO: make this constant a variable
    .poolSizeCount = 1,
    .pPoolSizes = Fvog::detail::Address(VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2}),
  }), nullptr, &imguiDescriptorPool_);

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

  ImGui_ImplFvog_LoadFunctions([](const char *functionName, void *vulkanInstance) {
    return vkGetInstanceProcAddr(*static_cast<VkInstance*>(vulkanInstance), functionName);
  }, &instance_.instance);
  ImGui_ImplFvog_Init(&imguiVulkanInitInfo);
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
}

Application::~Application()
{
  ZoneScoped;

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

void Application::Draw()
{
  ZoneScoped;

  auto prevTime = timeOfLastDraw;
  timeOfLastDraw = glfwGetTime();
  auto dtDraw = timeOfLastDraw - prevTime;

  Fvog::GetDevice().frameNumber++;
  auto& currentFrameData = Fvog::GetDevice().GetCurrentFrameData();

  {
    ZoneScopedN("vkWaitSemaphores (graphics queue timeline)");
    vkWaitSemaphores(Fvog::GetDevice().device_, Fvog::detail::Address(VkSemaphoreWaitInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &Fvog::GetDevice().graphicsQueueTimelineSemaphore_,
      .pValues = &currentFrameData.renderTimelineSemaphoreWaitValue,
    }), UINT64_MAX);
  }

  // Garbage collection
  Fvog::GetDevice().FreeUnusedResources();
  
  uint32_t swapchainImageIndex{};

  {
    // https://gist.github.com/nanokatze/bb03a486571e13a7b6a8709368bd87cf#file-handling-window-resize-md
    ZoneScopedN("vkAcquireNextImage2KHR");
    if (auto acquireResult = vkAcquireNextImage2KHR(Fvog::GetDevice().device_, Fvog::detail::Address(VkAcquireNextImageInfoKHR{
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain_,
      .timeout = static_cast<uint64_t>(-1),
      .semaphore = currentFrameData.swapchainSemaphore,
      .deviceMask = 1,
    }), &swapchainImageIndex); acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
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
    Fvog::detail::CheckVkResult(vkBeginCommandBuffer(commandBuffer, Fvog::detail::Address(VkCommandBufferBeginInfo{
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
      TracyVkZone(tracyVkContext_, commandBuffer, "OnRender");
      OnRender(dtDraw, commandBuffer, swapchainImageIndex);
    }
    {
      TracyVkZone(tracyVkContext_, commandBuffer, "OnGui");
      OnGui(dtDraw, commandBuffer);
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
      vkCmdBeginRendering(commandBuffer, Fvog::detail::Address(VkRenderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{}, {windowFramebufferWidth, windowFramebufferHeight}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = Fvog::detail::Address(VkRenderingAttachmentInfo{
          .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
          .imageView = swapchainImageViews_[swapchainImageIndex],
          .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        })
      }));
      //auto marker = Fwog::ScopedDebugMarker("Draw GUI");
      const bool isSurfaceHDR = swapchainFormat_.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT || swapchainFormat_.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
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
      const auto queueSubmitSignalSemaphores = std::array{
        VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = Fvog::GetDevice().graphicsQueueTimelineSemaphore_,
          .value = Fvog::GetDevice().frameNumber,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        },
        VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = currentFrameData.renderSemaphore,
          .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        }};

      Fvog::detail::CheckVkResult(vkQueueSubmit2(
        Fvog::GetDevice().graphicsQueue_,
        1,
        Fvog::detail::Address(VkSubmitInfo2{
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
          .waitSemaphoreInfoCount = 1,
          .pWaitSemaphoreInfos = Fvog::detail::Address(VkSemaphoreSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = currentFrameData.swapchainSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
          }),
          .commandBufferInfoCount = 1,
          .pCommandBufferInfos = Fvog::detail::Address(VkCommandBufferSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commandBuffer,
          }),
          .signalSemaphoreInfoCount = static_cast<uint32_t>(queueSubmitSignalSemaphores.size()),
          .pSignalSemaphoreInfos = queueSubmitSignalSemaphores.data(),
        }),
        VK_NULL_HANDLE)
      );

      currentFrameData.renderTimelineSemaphoreWaitValue = Fvog::GetDevice().frameNumber;
    }

    {
      ZoneScopedN("Present");
      if (auto presentResult = vkQueuePresentKHR(Fvog::GetDevice().graphicsQueue_, Fvog::detail::Address(VkPresentInfoKHR{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &currentFrameData.renderSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_.swapchain,
        .pImageIndices = &swapchainImageIndex,
      })); presentResult == VK_ERROR_OUT_OF_DATE_KHR)
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

void Application::Run()
{
  ZoneScoped;
  glfwSetInputMode(window, GLFW_CURSOR, cursorIsActive ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

  // Inform the user that the renderer is done loading
  glfwRequestWindowAttention(window);

  // The main loop.
  double prevFrame = glfwGetTime();
  while (!glfwWindowShouldClose(window))
  {
    ZoneScopedN("Frame");
    double curFrame = glfwGetTime();
    double dt = curFrame - prevFrame;
    prevFrame = curFrame;

    cursorFrameOffset = {0.0, 0.0};

    if (swapchainOk)
    {
      glfwPollEvents();
    }
    else
    {
      glfwWaitEvents();
      continue;
    }

    if (shouldRemakeSwapchainNextFrame)
    {
      swapchainFormat_ = nextSwapchainFormat_; // "Flush" nextSwapchainFormat_
      RemakeSwapchain(windowFramebufferWidth, windowFramebufferHeight);
      shouldRemakeSwapchainNextFrame = false;
    }

    // Close the app if the user presses Escape.
    if (glfwGetKey(window, GLFW_KEY_ESCAPE))
    {
      glfwSetWindowShouldClose(window, true);
    }

    // Sleep for a bit if the window is not focused
    if (!glfwGetWindowAttrib(window, GLFW_FOCUSED))
    {
      std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(50));
    }

    // Toggle the cursor if the grave accent (tilde) key is pressed.
    if (glfwGetKey(window, GLFW_KEY_GRAVE_ACCENT) && graveHeldLastFrame == false)
    {
      cursorIsActive = !cursorIsActive;
      cursorJustEnteredWindow = true;
      graveHeldLastFrame = true;
      glfwSetInputMode(window, GLFW_CURSOR, cursorIsActive ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    }

    if (!glfwGetKey(window, GLFW_KEY_GRAVE_ACCENT))
    {
      graveHeldLastFrame = false;
    }
    
    // Prevent the cursor from clicking ImGui widgets when it is disabled.
    if (!cursorIsActive)
    {
      glfwSetCursorPos(window, 0, 0);
      cursorPos.x = 0;
      cursorPos.y = 0;
    }

    // Update the main mainCamera.
    // WASD can be used to move the camera forwards, backwards, and side-to-side.
    // The mouse can be used to orient the camera.
    // Not all examples will use the main camera.
    if (!cursorIsActive)
    {
      const float dtf = static_cast<float>(dt);
      const glm::vec3 forward = mainCamera.GetForwardDir();
      const glm::vec3 right = glm::normalize(glm::cross(forward, {0, 1, 0}));
      float tempCameraSpeed = cameraSpeed;
      if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        tempCameraSpeed *= 4.0f;
      if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        tempCameraSpeed *= 0.25f;
      if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        mainCamera.position += forward * dtf * tempCameraSpeed;
      if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        mainCamera.position -= forward * dtf * tempCameraSpeed;
      if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        mainCamera.position += right * dtf * tempCameraSpeed;
      if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        mainCamera.position -= right * dtf * tempCameraSpeed;
      if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        mainCamera.position.y -= dtf * tempCameraSpeed;
      if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        mainCamera.position.y += dtf * tempCameraSpeed;
      mainCamera.yaw += static_cast<float>(cursorFrameOffset.x * cursorSensitivity);
      mainCamera.pitch += static_cast<float>(cursorFrameOffset.y * cursorSensitivity);
      mainCamera.pitch = glm::clamp(mainCamera.pitch, -glm::half_pi<float>() + 1e-4f, glm::half_pi<float>() - 1e-4f);
    }

    // Call the application's overriden functions each frame.
    OnUpdate(dt);

    if (windowFramebufferWidth > 0 && windowFramebufferHeight > 0)
    {
      Draw();
    }
  }
}

void Application::RemakeSwapchain([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight)
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
    swapchain_ = MakeVkbSwapchain(Fvog::GetDevice().device_,
                                  windowFramebufferWidth,
                                  windowFramebufferHeight,
                                  presentMode,
                                  numSwapchainImages,
                                  oldSwapchain,
                                  swapchainFormat_);
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

  swapchainImages_ = swapchain_.get_images().value();
  swapchainImageViews_ = MakeSwapchainImageViews(Fvog::GetDevice().device_, swapchainImages_, swapchainFormat_.format);

  swapchainOk = true;
  
  shouldResizeNextFrame = true;
  
  // This line triggers the recreation of window-size-dependent resources.
  // Commenting it out results in a faster, but lower quality resizing experience.
  //OnUpdate(0);
  Draw();
}

void DestroyList::Push(std::function<void()> fn)
{
  destructorList.emplace_back(std::move(fn));
}

DestroyList::~DestroyList()
{
  for (auto it = destructorList.rbegin(); it != destructorList.rend(); it++)
  {
    (*it)();
  }
}

std::filesystem::path GetAssetDirectory()
{
  static std::optional<std::filesystem::path> assetsPath;
  if (!assetsPath)
  {
    auto dir = std::filesystem::current_path();
    while (!dir.empty())
    {
      auto maybeAssets = dir / "data";
      if (exists(maybeAssets) && is_directory(maybeAssets))
      {
        assetsPath = maybeAssets;
        break;
      }

      if (!dir.has_parent_path())
      {
        break;
      }

      dir = dir.parent_path();
    }
  }
  return assetsPath.value(); // Will throw if asset directory wasn't found.
}

std::filesystem::path GetShaderDirectory()
{
  return GetAssetDirectory() / "shaders";
}

std::filesystem::path GetTextureDirectory()
{
  return GetAssetDirectory() / "textures";
}

std::filesystem::path GetConfigDirectory()
{
  return GetAssetDirectory() / "config";
}
