#pragma once

#include "Device.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include <Fwog/Buffer.h>

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
    Buffer(Device& device, const BufferCreateInfo& createInfo);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) noexcept; // TODO
    Buffer& operator=(Buffer&&) noexcept; // TODO

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

  private:
    Device& device_;
    VkBuffer buffer_{};
    VmaAllocation allocation_{};
    void* mappedMemory_{};
    VkDeviceAddress deviceAddress_{};
  };
}
