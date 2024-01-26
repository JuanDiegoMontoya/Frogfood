#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

// TODO: temp until vkb stops including vulkan.h
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <VkBootstrap.h>

#include <deque>
#include <functional>

typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

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

    // Immediate submit stuff
    VkCommandPool immediateSubmitCommandPool_{};
    VkCommandBuffer immediateSubmitCommandBuffer_{};
    // TODO: maybe this should return a u64 representing a timeline semaphore value that can be waited on
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& function) const;

    void FreeUnusedResources();

    // Descriptor stuff
    constexpr static uint32_t maxResourceDescriptors = 10'000;
    constexpr static uint32_t maxSamplerDescriptors = 100;
    constexpr static uint32_t storageBufferBinding = 0;
    constexpr static uint32_t combinedImageSamplerBinding = 1;
    constexpr static uint32_t storageImageBinding = 2;
    constexpr static uint32_t sampledImageBinding = 3;
    constexpr static uint32_t samplerBinding = 4;
    uint32_t currentStorageBufferDescriptorIndex = 0;
    uint32_t currentCombinedImageSamplerDescriptorIndex = 0;
    uint32_t currentStorageImageDescriptorIndex = 0;
    uint32_t currentSampledImageDescriptorIndex = 0;
    uint32_t currentSamplerDescriptorIndex = 0;
    VkDescriptorPool descriptorPool_{};
    VkDescriptorSetLayout descriptorSetLayout_{};
    VkDescriptorSet descriptorSet_{};

    uint32_t AllocateStorageBufferDescriptor(VkBuffer buffer);
    uint32_t AllocateCombinedImageSamplerDescriptor(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);
    uint32_t AllocateStorageImageDescriptor(VkImageView imageView, VkImageLayout imageLayout);
    uint32_t AllocateSampledImageDescriptor(VkImageView imageView, VkImageLayout imageLayout);
    uint32_t AllocateSamplerDescriptor(VkSampler sampler);

    // Queues
    VkQueue graphicsQueue_{};
    uint32_t graphicsQueueFamilyIndex_{};
    VkSemaphore graphicsQueueTimelineSemaphore_{};

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

    std::deque<ImageDeleteInfo> imageDeletionQueue_;
  };
}