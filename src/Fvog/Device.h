#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>

#include <deque>

namespace Fvog
{
  class Device
  {
  public:
    Device(vkb::Instance& instance, VkSurfaceKHR surface);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    // TODO: make movable?
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    // Everything is public :(
  //private:
    // Things that shouldn't be in this class, but are because I'm lazy:
    constexpr static uint32_t frameOverlap = 2;

    struct PerFrameData
    {
      VkCommandPool commandPool;
      VkCommandBuffer commandBuffer;
      VkSemaphore renderTimelneSemaphore;
      uint64_t renderTimelineSemaphoreWaitValue{};
      VkSemaphore presentSemaphore;
      VkSemaphore renderSemaphore;
    };

    PerFrameData frameData[frameOverlap]{};

    uint64_t frameNumber{};

    PerFrameData& GetCurrentFrameData()
    {
      return frameData[frameNumber % frameOverlap];
    }

    vkb::Instance& instance_;
    VkSurfaceKHR surface_; // Not owned

    // Everything hereafter probably actually belongs in this class
    vkb::PhysicalDevice physicalDevice_{};
    vkb::Device device_{};
    vkb::Swapchain swapchain_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VmaAllocator allocator_{};

    // Queues
    VkQueue graphicsQueue_{};
    uint32_t graphicsQueueFamilyIndex_{};
    VkSemaphore graphicsQueueSemaphore_{};

    struct BufferDeleteInfo
    {
      uint64_t frameOfLastUse{};
      VmaAllocation allocation{};
      VkBuffer buffer{};
    };

    std::deque<BufferDeleteInfo> bufferDeletionQueue_;

    struct ImageDeleteInfo
    {
      uint64_t frameOfLastUse{};
      VmaAllocation allocation{};
      VkImage image{};
      VkImageView imageView{};
    };

    std::deque<BufferDeleteInfo> imageDeletionQueue_;
  };
}