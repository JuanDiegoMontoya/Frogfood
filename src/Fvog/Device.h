#pragma once

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

#include <deque>
#include <functional>
#include <stack>
#include <string>
#include <memory>

#include <vk_mem_alloc.h>

typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VmaVirtualAllocation);
VK_DEFINE_HANDLE(VmaVirtualBlock);

namespace Fvog
{
  namespace detail
  {
    class SamplerCache;
  }

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
      VkSemaphore swapchainSemaphore;
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
    VmaAllocator allocator_{};

    // Immediate submit stuff
    VkCommandPool immediateSubmitCommandPool_{};
    VkCommandBuffer immediateSubmitCommandBuffer_{};
    // TODO: maybe this should return a u64 representing a timeline semaphore value that can be waited on
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& function) const;

    void FreeUnusedResources();

    // Descriptor stuff
    class IndexAllocator
    {
    public:
      IndexAllocator(uint32_t numIndices);

      [[nodiscard]] uint32_t Allocate();
      void Free(uint32_t index);

    private:
      std::stack<uint32_t> freeSlots_;
    };

    constexpr static uint32_t maxResourceDescriptors = 100'000;
    constexpr static uint32_t maxSamplerDescriptors = 1000;
    constexpr static uint32_t storageBufferBinding = 0;
    constexpr static uint32_t combinedImageSamplerBinding = 1;
    constexpr static uint32_t storageImageBinding = 2;
    constexpr static uint32_t sampledImageBinding = 3;
    constexpr static uint32_t samplerBinding = 4;
    IndexAllocator storageBufferDescriptorAllocator = maxResourceDescriptors;
    IndexAllocator combinedImageSamplerDescriptorAllocator = maxResourceDescriptors;
    IndexAllocator storageImageDescriptorAllocator = maxResourceDescriptors;
    IndexAllocator sampledImageDescriptorAllocator = maxResourceDescriptors;
    IndexAllocator samplerDescriptorAllocator = maxSamplerDescriptors;
#ifdef FROGRENDER_RAYTRACING_ENABLE
    constexpr static uint32_t accelerationStructureBinding = 5;
    IndexAllocator accelerationStructureDescriptorAllocator = maxResourceDescriptors;
#endif
    VkDescriptorPool descriptorPool_{};
    VkDescriptorSetLayout descriptorSetLayout_{};
    VkDescriptorSet descriptorSet_{};
    VkPipelineLayout defaultPipelineLayout{};

    enum class ResourceType : uint32_t
    {
      INVALID,
      STORAGE_BUFFER,
      COMBINED_IMAGE_SAMPLER,
      STORAGE_IMAGE,
      SAMPLED_IMAGE,
      SAMPLER,
#ifdef FROGRENDER_RAYTRACING_ENABLE
      ACCELERATION_STRUCTURE,
#endif
    };

    class DescriptorInfo
    {
    public:
      struct ResourceHandle
      {
        ResourceType type{};
        uint32_t index{};
      };
      
      DescriptorInfo(const DescriptorInfo&) = delete;
      DescriptorInfo& operator=(const DescriptorInfo&) = delete;
      DescriptorInfo(DescriptorInfo&&) noexcept;
      DescriptorInfo& operator=(DescriptorInfo&&) noexcept;
      ~DescriptorInfo();

      [[nodiscard]] const ResourceHandle& GpuResource() const noexcept
      {
        return handle_;
      }

    private:
      friend class Device;
      DescriptorInfo(Device& device, ResourceHandle handle) : device_(&device), handle_(handle) {}
      Device* device_{};
      ResourceHandle handle_{};
    };

    DescriptorInfo AllocateStorageBufferDescriptor(VkBuffer buffer);
    DescriptorInfo AllocateCombinedImageSamplerDescriptor(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout);
    DescriptorInfo AllocateStorageImageDescriptor(VkImageView imageView, VkImageLayout imageLayout);
    DescriptorInfo AllocateSampledImageDescriptor(VkImageView imageView, VkImageLayout imageLayout);
    DescriptorInfo AllocateSamplerDescriptor(VkSampler sampler);

#ifdef FROGRENDER_RAYTRACING_ENABLE
    DescriptorInfo AllocateAccelerationStructureDescriptor(VkAccelerationStructureKHR tlas);
#endif

    // Queues
    VkQueue graphicsQueue_{};
    uint32_t graphicsQueueFamilyIndex_{};
    VkSemaphore graphicsQueueTimelineSemaphore_{};

    struct BufferDeleteInfo
    {
      uint64_t frameOfLastUse{};
      VmaAllocation allocation{};
      VkBuffer buffer{};
      std::string name;
    };

    std::deque<BufferDeleteInfo> bufferDeletionQueue_;

    struct ImageDeleteInfo
    {
      uint64_t frameOfLastUse{};
      VmaAllocation allocation{};
      VkImage image{};
      std::string name;
    };

    std::deque<ImageDeleteInfo> imageDeletionQueue_;

    struct ImageViewDeleteInfo
    {
      uint64_t frameOfLastUse{};
      VkImageView imageView{};
      std::string name;
    };
    std::deque<ImageViewDeleteInfo> imageViewDeletionQueue_;

    struct DescriptorDeleteInfo
    {
      uint64_t frameOfLastUse{};
      DescriptorInfo::ResourceHandle handle{};
    };

    std::deque<DescriptorDeleteInfo> descriptorDeletionQueue_;

    std::unique_ptr<detail::SamplerCache> samplerCache_;

    std::deque<std::function<bool(uint64_t)>> genericDeletionQueue_;
  };
}
