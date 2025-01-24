#pragma once
#include <cstdint>

namespace Fvog
{
  class Device;

  enum class ResourceType : uint32_t
  {
    INVALID,
    STORAGE_BUFFER,
    COMBINED_IMAGE_SAMPLER,
    STORAGE_IMAGE,
    SAMPLED_IMAGE,
    SAMPLER,
    ACCELERATION_STRUCTURE,
  };

  class DescriptorInfo
  {
  public:
    struct ResourceHandle
    {
      ResourceType type{};
      uint32_t index{};
    };

    DescriptorInfo(const DescriptorInfo&)            = delete;
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
} // namespace Fvog
