#pragma once

#include "Device.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <string_view>
#include <span>
#include <type_traits>

namespace Fvog
{
  /// @brief Used to constrain the types accpeted by Buffer
  class TriviallyCopyableByteSpan : public std::span<const std::byte>
  {
  public:
    template<typename T>
      requires std::is_trivially_copyable_v<T>
    TriviallyCopyableByteSpan(const T& t) : std::span<const std::byte>(std::as_bytes(std::span{&t, static_cast<size_t>(1)}))
    {
    }

    template<typename T>
      requires std::is_trivially_copyable_v<T>
    TriviallyCopyableByteSpan(std::span<const T> t) : std::span<const std::byte>(std::as_bytes(t))
    {
    }

    template<typename T>
      requires std::is_trivially_copyable_v<T>
    TriviallyCopyableByteSpan(std::span<T> t) : std::span<const std::byte>(std::as_bytes(t))
    {
    }
  };

  enum class BufferFlagThingy
  {
    NONE,
    MAP_SEQUENTIAL_WRITE,
    MAP_RANDOM_ACCESS,
  };

  struct BufferCreateInfo
  {
    std::string_view name{};
    VkDeviceSize size{};
    BufferFlagThingy flag{};
  };
  
  class Buffer
  {
  public:
    explicit Buffer(Device& device, const BufferCreateInfo& createInfo);
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

  protected:
    Device* device_{};
    BufferCreateInfo createInfo_{};
    VkBuffer buffer_{};
    VmaAllocation allocation_{};
    void* mappedMemory_{};
    VkDeviceAddress deviceAddress_{};

    template<typename T>
    friend class NDeviceBuffer;

    void UpdateDataGeneric(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes, Buffer& stagingBuffer, Buffer& deviceBuffer);
  };

  struct TypedBufferCreateInfo
  {
    std::string_view name{};
    uint32_t count{1};
    BufferFlagThingy flag{};
  };
  
  template<typename T = std::byte>
    requires std::is_trivially_copyable_v<T>
  class TypedBuffer : public Buffer
  {
  public:
    explicit TypedBuffer(Device& device, const TypedBufferCreateInfo& createInfo)
      : Buffer(device, {.name = createInfo.name, .size = createInfo.count * sizeof(T), .flag = createInfo.flag})
    {
    }

    [[nodiscard]] T* GetMappedMemory() const noexcept
    {
      return static_cast<T*>(mappedMemory_);
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
    explicit NDeviceBuffer(Device& device, uint32_t count = 1)
    : hostStagingBuffers_{
        // TODO: create a helper for initializing this array so it doesn't fail to compile when Device::frameOverlap changes
        {TypedBuffer<T>(device, TypedBufferCreateInfo{.count = count, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE})},
        {TypedBuffer<T>(device, TypedBufferCreateInfo{.count = count, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE})},
      },
      deviceBuffer_(device, TypedBufferCreateInfo{.count = count})
    {
    }

    // Only call within a command buffer, once per frame
    void UpdateData(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes = 0)
    {
      auto& stagingBuffer = hostStagingBuffers_[deviceBuffer_.device_->frameNumber % Device::frameOverlap];
      stagingBuffer.UpdateDataGeneric(commandBuffer, data, destOffsetBytes, stagingBuffer, deviceBuffer_);
    }

    [[nodiscard]] Buffer& GetDeviceBuffer() noexcept
    {
      return deviceBuffer_;
    }

  private:

    TypedBuffer<T> hostStagingBuffers_[Device::frameOverlap];
    TypedBuffer<T> deviceBuffer_;
  };
}
