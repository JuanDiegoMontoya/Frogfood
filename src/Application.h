#pragma once
#include "Fvog/Device.h"
#include <VkBootstrap.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>
#include <string>
#include <utility>
#include <span>
#include <functional>
#include <optional>

#include <glm/gtx/transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct GLFWwindow;

namespace tracy
{
  class VkCtx;
}

// Represents the camera's position and orientation.
struct View
{
  glm::vec3 position{};
  float pitch{}; // pitch angle in radians
  float yaw{};   // yaw angle in radians

  glm::vec3 GetForwardDir() const
  {
    return glm::vec3{cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw)};
  }

  glm::mat4 GetViewMatrix() const
  {
    return glm::lookAt(position, position + GetForwardDir(), glm::vec3(0, 1, 0));
  }
};

// List of functions to execute in reverse order in its destructor
class DestroyList
{
public:
  DestroyList() = default;
  void Push(std::function<void()> fn);
  ~DestroyList();

  DestroyList(const DestroyList&) = delete;
  DestroyList(DestroyList&&) noexcept = delete;
  DestroyList&& operator=(const DestroyList&) = delete;
  DestroyList&& operator=(DestroyList&&) noexcept = delete;

private:
  std::vector<std::function<void()>> destructorList;
};

class Application
{
public:
  struct CreateInfo
  {
    std::string_view name = "";
    bool maximize = false;
    bool decorate = true;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  };

  // TODO: An easy way to load shaders should probably be a part of Fwog
  static std::string LoadFile(const std::filesystem::path& path);
  static std::pair<std::unique_ptr<std::byte[]>, std::size_t> LoadBinaryFile(const std::filesystem::path& path);

  Application(const CreateInfo& createInfo);
  Application(const Application&) = delete;
  Application(Application&&) noexcept = delete;
  Application& operator=(const Application&) = delete;
  Application& operator=(Application&&) noexcept = delete;

  virtual ~Application();

  void Run();

protected:
  // Create swapchain size-dependent resources
  virtual void OnFramebufferResize([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight){}
  virtual void OnUpdate([[maybe_unused]] double dt){}
  virtual void OnRender(
    [[maybe_unused]] double dt,
    [[maybe_unused]] VkCommandBuffer commandBuffer,
    [[maybe_unused]] uint32_t swapchainImageIndex) {}
  virtual void OnGui([[maybe_unused]] double dt, [[maybe_unused]] VkCommandBuffer commandBuffer) {}
  virtual void OnPathDrop([[maybe_unused]] std::span<const char*> paths){}

  // destroyList will be the last object to be automatically destroyed after the destructor returns
  DestroyList destroyList_;
  vkb::Instance instance_{};
  std::optional<Fvog::Device> device_;
  VkSurfaceKHR surface_{};
  VkDescriptorPool imguiDescriptorPool_{};
  vkb::Swapchain swapchain_{};
  std::vector<VkImage> swapchainImages_;
  std::vector<VkImageView> swapchainImageViews_;
  std::vector<VkSurfaceFormatKHR> availableSurfaceFormats_;
  static constexpr VkSurfaceFormatKHR defaultSwapchainFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  VkSurfaceFormatKHR swapchainFormat_ = defaultSwapchainFormat; // Only Application should modify this
  VkSurfaceFormatKHR nextSwapchainFormat_ = swapchainFormat_; // Workaround to prevent ImGui backend from using incorrect pipeline layout after changing swapchainFormat in GUI
  float maxDisplayNits = 200.0f;

  tracy::VkCtx* tracyVkContext_{};
  GLFWwindow* window;
  View mainCamera{};
  float cursorSensitivity = 0.0025f;
  float cameraSpeed = 4.5f;
  bool cursorIsActive = true;

  uint32_t windowFramebufferWidth{};
  uint32_t windowFramebufferHeight{};

  glm::dvec2 cursorPos{};

  // Resizing from UI is deferred until next frame so texture handles remain valid when ImGui is rendered
  bool shouldResizeNextFrame = false;
  bool shouldRemakeSwapchainNextFrame = false;
  VkPresentModeKHR presentMode;
  uint32_t numSwapchainImages = 3;

private:
  friend class ApplicationAccess;

  void RemakeSwapchain(uint32_t newWidth, uint32_t newHeight);
  void Draw();
  double timeOfLastDraw = 0;

  glm::dvec2 cursorFrameOffset{};
  bool cursorJustEnteredWindow = true;
  bool graveHeldLastFrame = false;
  bool swapchainOk = true;
};
