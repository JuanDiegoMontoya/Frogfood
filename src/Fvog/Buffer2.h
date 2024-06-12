#pragma once

#include "Device.h"
#include "TriviallyCopyableByteSpan.h"

#include <vulkan/vulkan_core.h>

#include <string>
#include <string_view>
#include <optional>

namespace Fvog
{
  enum class BufferFlagThingy
  {
    NONE,
    MAP_SEQUENTIAL_WRITE,
    MAP_RANDOM_ACCESS,
  };

  struct BufferCreateInfo
  {
    // Size in bytes
    VkDeviceSize size{};
    BufferFlagThingy flag{};
  };

  struct BufferFillInfo
  {
    VkDeviceSize offset = 0;
    VkDeviceSize size = VK_WHOLE_SIZE;
    uint32_t data = 0;
  };
  
  class Buffer
  {
  public:
    explicit Buffer(Device& device, const BufferCreateInfo& createInfo, std::string name = {});
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;

    [[nodiscard]] VkBuffer Handle() const noexcept
    {
      return buffer_;
    }

    [[nodiscard]] void* GetMappedMemory() const noexcept
    {
      return mappedMemory_;
    }

    [[nodiscard]] const VkDeviceAddress& GetDeviceAddress() const noexcept
    {
      return deviceAddress_;
    }

    [[nodiscard]] BufferCreateInfo GetCreateInfo() const noexcept
    {
      return createInfo_;
    }

    [[nodiscard]] VkDeviceSize SizeBytes() const noexcept
    {
      return createInfo_.size;
    }

    void UpdateDataExpensive(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes = 0);

    void FillData(VkCommandBuffer commandBuffer, const BufferFillInfo& clear = {});

    Device::DescriptorInfo::ResourceHandle GetResourceHandle()
    {
      return descriptorInfo_.value().GpuResource();
    }

  protected:
    Device* device_{};
    BufferCreateInfo createInfo_{};
    VkBuffer buffer_{};
    VmaAllocation allocation_{};
    void* mappedMemory_{};
    VkDeviceAddress deviceAddress_{};
    std::optional<Device::DescriptorInfo> descriptorInfo_;
    std::string name_;

    template<typename T>
    friend class NDeviceBuffer;

    static void UpdateDataGeneric(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes, Buffer& stagingBuffer, Buffer& deviceBuffer);
  };

  struct TypedBufferCreateInfo
  {
    uint32_t count{1};
    BufferFlagThingy flag{};
  };
  
  template<typename T = std::byte>
    requires std::is_trivially_copyable_v<T>
  class TypedBuffer : public Buffer
  {
  public:
    explicit TypedBuffer(Device& device, const TypedBufferCreateInfo& createInfo = {}, std::string name = {})
      : Buffer(device, {.size = createInfo.count * sizeof(T), .flag = createInfo.flag}, std::move(name))
    {
      //assert(createInfo.count > 0);
    }

    // Number of elements of T that this buffer could hold
    [[nodiscard]] uint32_t Size() const noexcept
    {
      return static_cast<uint32_t>(createInfo_.size) / sizeof(T);
    }

    [[nodiscard]] T* GetMappedMemory() const noexcept
    {
      return static_cast<T*>(mappedMemory_);
    }

    // UpdateDataExpensive CAN be called multiple times per frame, but each time it allocates a new buffer
    void UpdateDataExpensive(VkCommandBuffer commandBuffer, std::span<const T> data, VkDeviceSize destOffsetBytes = 0)
    {
      Buffer::UpdateDataExpensive(commandBuffer, data, destOffsetBytes);
    }

    void UpdateDataExpensive(VkCommandBuffer commandBuffer, const T& data, VkDeviceSize destOffsetBytes = 0)
    {
      Buffer::UpdateDataExpensive(commandBuffer, std::span(&data, 1), destOffsetBytes);
    }

  private:
  };

  // Consists of N staging buffers for upload and one device buffer
  // Use for buffers that need to be uploaded every frame
  template<typename T = std::byte>
    //requires std::is_trivially_copyable_v<T>
  class NDeviceBuffer
  {
  public:
    explicit NDeviceBuffer(Device& device, uint32_t count = 1, std::string name = {})
    : hostStagingBuffers_{
        // TODO: create a helper for initializing this array so it doesn't fail to compile when Device::frameOverlap changes
        {TypedBuffer<T>(device, TypedBufferCreateInfo{.count = count, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, name + std::string(" (host)"))},
        {TypedBuffer<T>(device, TypedBufferCreateInfo{.count = count, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, name + std::string(" (host)"))},
      },
      deviceBuffer_(device, TypedBufferCreateInfo{.count = count}, std::move(name))
    {
    }

    // Number of elements of T that this buffer could hold
    [[nodiscard]] uint32_t Size() const noexcept
    {
      return deviceBuffer_.Size();
    }

    void UpdateData(VkCommandBuffer commandBuffer, std::span<const T> data, VkDeviceSize destOffsetElements = 0)
    {
      updateData(commandBuffer, data, destOffsetElements * sizeof(T));
    }

    void UpdateData(VkCommandBuffer commandBuffer, const T& data, VkDeviceSize destOffsetElements = 0)
    {
      updateData(commandBuffer, std::span(&data, 1), destOffsetElements * sizeof(T));
    }

    [[nodiscard]] Buffer& GetDeviceBuffer() noexcept
    {
      return deviceBuffer_;
    }

  private:
    // Only call within a command buffer, once per frame
    void updateData(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes = 0)
    {
      if (data.size() == 0)
      {
        return;
      }
      auto& stagingBuffer = hostStagingBuffers_[deviceBuffer_.device_->frameNumber % Device::frameOverlap];
      stagingBuffer.UpdateDataGeneric(commandBuffer, data, destOffsetBytes, stagingBuffer, deviceBuffer_);
    }

    TypedBuffer<T> hostStagingBuffers_[Device::frameOverlap];
    TypedBuffer<T> deviceBuffer_;
  };
}
