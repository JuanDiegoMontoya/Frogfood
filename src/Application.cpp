#include "Application.h"

#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <volk.h>
#include <VkBootstrap.h>
#include <glslang/Public/ShaderLang.h>

#include "Fvog/Buffer2.h"
#include "Fvog/Shader2.h"
#include "Fvog/Pipeline2.h"
#include "Fvog/Texture2.h"
#include "Fvog/Rendering2.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
//#include <imgui_impl_vulkan.h>
#include <implot.h>

#include <glm/gtc/constants.hpp>

#include <tracy/Tracy.hpp>
//#include <tracy/TracyVulkan.hpp>

#include <bit>
#include <exception>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

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

// This class provides static callbacks for GLFW.
// It has access to the private members of Application and assumes a pointer to it is present in the window's user pointer.
class ApplicationAccess
{
public:
  static void CursorPosCallback(GLFWwindow* window, double currentCursorX, double currentCursorY)
  {
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
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
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (entered)
    {
      app->cursorJustEnteredWindow = true;
    }
  }

  static void FramebufferResizeCallback(GLFWwindow* window, int newWidth, int newHeight)
  {
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    app->windowWidth = static_cast<uint32_t>(newWidth);
    app->windowHeight = static_cast<uint32_t>(newHeight);

    if (newWidth > 0 && newHeight > 0)
    {
      app->shouldResizeNextFrame = true;
      app->Draw(0.016);
    }
  }

  static void PathDropCallback(GLFWwindow* window, int count, const char** paths)
  {
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
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

Application::Application(const CreateInfo& createInfo)
  : vsyncEnabled(createInfo.vsync)
{
  ZoneScoped;
  // Initialiize GLFW
  if (!glfwInit())
  {
    throw std::runtime_error("Failed to initialize GLFW");
  }

  destroyList_.Push([] { glfwTerminate(); });

  glfwSetErrorCallback([](int, const char* desc) { std::cout << "GLFW error: " << desc << '\n'; });

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_MAXIMIZED, createInfo.maximize);
  glfwWindowHint(GLFW_DECORATED, createInfo.decorate);

  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  if (monitor == nullptr)
  {
    throw std::runtime_error("No monitor detected");
  }
  const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
  window = glfwCreateWindow(static_cast<int>(videoMode->width * .75), static_cast<int>(videoMode->height * .75), createInfo.name.data(), nullptr, nullptr);
  if (!window)
  {
    throw std::runtime_error("Failed to create window");
  }

  int xSize{};
  int ySize{};
  glfwGetFramebufferSize(window, &xSize, &ySize);
  windowWidth = static_cast<uint32_t>(xSize);
  windowHeight = static_cast<uint32_t>(ySize);

  int monitorLeft{};
  int monitorTop{};
  glfwGetMonitorPos(monitor, &monitorLeft, &monitorTop);

  glfwSetWindowPos(window, videoMode->width / 2 - windowWidth / 2 + monitorLeft, videoMode->height / 2 - windowHeight / 2 + monitorTop);

  glfwSetWindowUserPointer(window, this);
  //glfwMakeContextCurrent(window);
  //glfwSwapInterval(vsyncEnabled ? 1 : 0);
  // TODO: configure vsync

  glfwSetCursorPosCallback(window, ApplicationAccess::CursorPosCallback);
  glfwSetCursorEnterCallback(window, ApplicationAccess::CursorEnterCallback);
  glfwSetFramebufferSizeCallback(window, ApplicationAccess::FramebufferResizeCallback);
  glfwSetDropCallback(window, ApplicationAccess::PathDropCallback);

  // Initialize Vulkan
  // instance
  instance_ = vkb::InstanceBuilder()
    .set_app_name("Frogrenderer")
    .require_api_version(1, 3, 0)
    .request_validation_layers() // TODO: make optional
    .use_default_debug_messenger() // TODO: make optional
    .build()
    .value();

  if (volkInitialize() != VK_SUCCESS)
  {
    throw std::runtime_error("rip");
  }

  destroyList_.Push([] { volkFinalize(); });

  volkLoadInstance(instance_);

  // surface
  VkSurfaceKHR surface;
  if (auto err = glfwCreateWindowSurface(instance_, window, nullptr, &surface); err != VK_SUCCESS)
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
  destroyList_.Push([this] { vkDestroySurfaceKHR(instance_, surface_, nullptr); });

  device_.emplace(instance_, surface);

  const auto commandPoolInfo = VkCommandPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // Required for Tracy
  };
  if (vkCreateCommandPool(device_->device_, &commandPoolInfo, nullptr, &tracyCommandPool_) != VK_SUCCESS)
  {
    throw std::runtime_error("rip");
  }

  const auto commandBufferInfo = VkCommandBufferAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = tracyCommandPool_,
    .commandBufferCount = 1,
  };
  if (vkAllocateCommandBuffers(device_->device_, &commandBufferInfo, &tracyCommandBuffer_) != VK_SUCCESS)
  {
    throw std::runtime_error("rip");
  }

  glslang::InitializeProcess();
  destroyList_.Push([] { glslang::FinalizeProcess(); });

  // Initialize Tracy
  //tracyVkContext_ = TracyVkContext(device_.physicalDevice_, device_.device_, device_.graphicsQueue_, tracyCommandBuffer_)

  // Initialize ImGui and a backend for it.
  // Because we allow the GLFW backend to install callbacks, it will automatically call our own that we provided.
  ImGui::CreateContext();
  destroyList_.Push([] { ImGui::DestroyContext(); });
  ImPlot::CreateContext();
  destroyList_.Push([] { ImPlot::DestroyContext(); });
  //ImGui_ImplGlfw_InitForOpenGL(window, true); // TODO
  //ImGui_ImplOpenGL3_Init(); // TODO
  ImGui::StyleColorsDark();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
}

Application::~Application()
{
  ZoneScoped;

  // Destroying a command pool implicitly frees command buffers allocated from it
  vkDestroyCommandPool(device_->device_, tracyCommandPool_, nullptr);


  //ImGui_ImplOpenGL3_Shutdown(); // TODO
  //ImGui_ImplGlfw_Shutdown(); // TODO
  //ImPlot::DestroyContext();
  //ImGui::DestroyContext();
}

void Application::Draw(double dt)
{
  ZoneScoped;

  // Start a new ImGui frame
  //ImGui_ImplOpenGL3_NewFrame(); // TODO
  //ImGui_ImplGlfw_NewFrame();
  //ImGui::NewFrame();

  if (windowWidth > 0 && windowHeight > 0)
  {
    OnRender(dt);
    OnGui(dt);
  }

  // Updates ImGui.
  // A frame marker is inserted to distinguish ImGui rendering from the application's in a debugger.
  {
    ZoneScopedN("Draw UI");
    //ImGui::Render();
    //auto* drawData = ImGui::GetDrawData();
    //if (drawData->CmdListsCount > 0)
    {
      // TODO
      //auto marker = Fwog::ScopedDebugMarker("Draw GUI");
      //glDisable(GL_FRAMEBUFFER_SRGB);
      //glBindFramebuffer(GL_FRAMEBUFFER, 0);
      //ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }
  }

  {
    ZoneScopedN("SwapBuffers");
    //glfwSwapBuffers(window);
  }
  //TracyVkCollect(tracyVkContext_, tracyCommandBuffer_) // TODO: figure out how this is supposed to work
  FrameMark;
}

void Application::Run()
{
  ZoneScoped;
  glfwSetInputMode(window, GLFW_CURSOR, cursorIsActive ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

  static const char* gVertexSource = R"(
#version 460 core

const vec2 positions[3] = {{-0, -0}, {1, -1}, {1, 1}};

void main()
{
  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)";

  static const char* gFragmentSource = R"(
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) buffer TestBuffers
{
  vec4 color;
}buffers[];

layout(push_constant) uniform PushConstants
{
  uint index;
}pushConstants;

layout(location = 0) out vec4 o_color;

void main()
{
  o_color = buffers[pushConstants.index].color;
}
)";

  auto pipelineLayout = VkPipelineLayout{};
  CheckVkResult(
    vkCreatePipelineLayout(
      device_->device_,
      Address(VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &device_->descriptorSetLayout_,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = Address(VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_ALL,
          .offset = 0,
          .size = 128,
        }),
      }),
      nullptr,
      &pipelineLayout));

  const auto vertexShader = Fvog::Shader(device_->device_, Fvog::PipelineStage::VERTEX_SHADER, gVertexSource);
  const auto fragmentShader = Fvog::Shader(device_->device_, Fvog::PipelineStage::FRAGMENT_SHADER, gFragmentSource);
  const auto renderTargetFormats = {VK_FORMAT_B8G8R8A8_SRGB};
  auto pipeline = Fvog::GraphicsPipeline(device_->device_, pipelineLayout, {
    .vertexShader = &vertexShader,
    .fragmentShader = &fragmentShader,
    .renderTargetFormats = {.colorAttachmentFormats = renderTargetFormats},
  });
  
  auto testTexture = Fvog::Texture(device_.value(), {
    .imageViewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = VK_FORMAT_B8G8R8A8_SRGB,
    .extent = {192, 108, 1},
    .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
  });

  // The main loop.
  double prevFrame = glfwGetTime();
  while (!glfwWindowShouldClose(window))
  {
    ZoneScopedN("Frame");
    double curFrame = glfwGetTime();
    double dt = curFrame - prevFrame;
    prevFrame = curFrame;

    cursorFrameOffset = {0.0, 0.0};
    glfwPollEvents();

    // Close the app if the user presses Escape.
    if (glfwGetKey(window, GLFW_KEY_ESCAPE))
    {
      glfwSetWindowShouldClose(window, true);
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

    Draw(dt);

    device_->frameNumber++;
    auto& currentFrameData = device_->GetCurrentFrameData();

    vkWaitSemaphores(device_->device_, Address(VkSemaphoreWaitInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &device_->graphicsQueueTimelineSemaphore_,
      .pValues = &currentFrameData.renderTimelineSemaphoreWaitValue,
    }), UINT64_MAX);

    device_->FreeUnusedResources();

    // TODO:
    // On success, this command returns
    //    VK_SUCCESS
    //    VK_TIMEOUT
    //    VK_NOT_READY
    //    VK_SUBOPTIMAL_KHR
    uint32_t swapchainImageIndex{};
    CheckVkResult(vkAcquireNextImage2KHR(device_->device_, Address(VkAcquireNextImageInfoKHR{
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = device_->swapchain_,
      .timeout = static_cast<uint64_t>(-1),
      .semaphore = currentFrameData.swapchainSemaphore,
      .deviceMask = 1,
    }), &swapchainImageIndex));

    auto commandBuffer = currentFrameData.commandBuffer;
    auto ctx = Fvog::Context(commandBuffer);

    CheckVkResult(vkResetCommandPool(device_->device_, currentFrameData.commandPool, 0));

    CheckVkResult(vkBeginCommandBuffer(commandBuffer, Address(VkCommandBufferBeginInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    })));

    auto testBuffer = Fvog::Buffer(*device_, {.size = sizeof(glm::vec4)});
    const auto testColor = glm::vec4{1.0f, 1.0f, std::sinf(device_->frameNumber / 1000.0f) * .5f + .5f, 1.0f};
    vkCmdUpdateBuffer(commandBuffer, testBuffer.Handle(), 0, sizeof(testColor), &testColor);

    ctx.BufferBarrier(testBuffer);

    ctx.ImageBarrier(testTexture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    auto colorAttachment = Fvog::RenderColorAttachment{
      .texture = testTexture,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      //.clearValue = {1.0f, 1.0f, std::sinf(device_->frameNumber / 1000.0f) * .5f + .5f, 1.0f},
      .clearValue = {0.1f, 0.1f, 0.1f, 1.0f},
    };
    ctx.BeginRendering({
      .colorAttachments = {&colorAttachment, 1},
    });

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &device_->descriptorSet_, 0, nullptr);
    const auto descriptorInfo = device_->AllocateStorageBufferDescriptor(testBuffer.Handle());
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(uint32_t), &descriptorInfo.GpuResource().index);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Handle());
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    ctx.EndRendering();
    
    ctx.ImageBarrier(device_->swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    ctx.ImageBarrier(testTexture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    vkCmdBlitImage2(commandBuffer, Address(VkBlitImageInfo2{
      .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
      .srcImage = testTexture.Image(),
      .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .dstImage = device_->swapchainImages_[swapchainImageIndex],
      .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .regionCount = 1,
      .pRegions = Address(VkImageBlit2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
        .srcSubresource = VkImageSubresourceLayers{
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .mipLevel = 0,
          .baseArrayLayer = 0,
          .layerCount = 1,
        },
        .srcOffsets = {{}, {(int)windowWidth / 10, (int)windowHeight / 10, 1}},
        .dstSubresource = VkImageSubresourceLayers{
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .mipLevel = 0,
          .baseArrayLayer = 0,
          .layerCount = 1,
        },
        .dstOffsets = {{}, {(int)windowWidth, (int)windowHeight, 1}},
      }),
      .filter = VK_FILTER_NEAREST,
    }));
    
    ctx.ImageBarrier(device_->swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // End recording
    CheckVkResult(vkEndCommandBuffer(commandBuffer));

    // Submit
    const auto queueSubmitSignalSemaphores = std::array{
      VkSemaphoreSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = device_->graphicsQueueTimelineSemaphore_,
        .value = device_->frameNumber,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
      },
      VkSemaphoreSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = currentFrameData.renderSemaphore,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
      }};
    currentFrameData.renderTimelineSemaphoreWaitValue = device_->frameNumber;

    CheckVkResult(vkQueueSubmit2(
      device_->graphicsQueue_,
      1,
      Address(VkSubmitInfo2{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = Address(VkSemaphoreSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = currentFrameData.swapchainSemaphore,
          .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        }),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = Address(VkCommandBufferSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
          .commandBuffer = commandBuffer,
        }),
        .signalSemaphoreInfoCount = static_cast<uint32_t>(queueSubmitSignalSemaphores.size()),
        .pSignalSemaphoreInfos = queueSubmitSignalSemaphores.data(),
      }),
      VK_NULL_HANDLE)
    );

    // Present
    CheckVkResult(vkQueuePresentKHR(device_->graphicsQueue_, Address(VkPresentInfoKHR{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &currentFrameData.renderSemaphore,
      .swapchainCount = 1,
      .pSwapchains = &device_->swapchain_.swapchain,
      .pImageIndices = &swapchainImageIndex,
    })));
  }

  vkDeviceWaitIdle(device_->device_);

  vkDestroyPipelineLayout(device_->device_, pipelineLayout, nullptr);
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
