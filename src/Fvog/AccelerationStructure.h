#pragma once

#include "BasicTypes2.h"
#include "Buffer2.h"

#include <vulkan/vulkan.h>

#include <glm/mat4x4.hpp>

#include <optional>

namespace Fvog
{
  enum class AccelerationStructureGeometryFlag : uint32_t
  {
    OPAQUE                         = VK_GEOMETRY_OPAQUE_BIT_KHR,
    NO_DUPLICATE_ANYHIT_INVOCATION = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR,
  };
  FVOG_DECLARE_FLAG_TYPE(AccelerationStructureGeometryFlags, AccelerationStructureGeometryFlag, uint32_t);

  enum class AccelerationStructureBuildFlag : uint32_t
  {
    ALLOW_UPDATE                       = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
    ALLOW_COMPACTION                   = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR,
    FAST_TRACE                         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
    FAST_BUILD                         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
    ALLOW_MICROMAP_OPACITY_UPDATE      = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_EXT,
    ALLOW_DISABLE_OPACITY_MICROMAPS    = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DISABLE_OPACITY_MICROMAPS_EXT,
    ALLOW_OPACITY_MICROMAP_DATA_UPDATE = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_DATA_UPDATE_EXT,
    ALLOW_DATA_ACCESS                  = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR,
  };
  FVOG_DECLARE_FLAG_TYPE(AccelerationStructureBuildFlags, AccelerationStructureBuildFlag, uint32_t);

  enum class AccelerationStructureGeometryInstanceFlag : uint32_t
  {
    TRIANGLE_FACING_CULL_DISABLE_BIT = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
    TRIANGLE_FLIP_FACING_BIT = VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR,
    FORCE_OPAQUE_BIT = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR,
    FORCE_NO_OPAQUE_BIT = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR,
    FORCE_OPACITY_MICROMAP_2_STAT = VK_GEOMETRY_INSTANCE_FORCE_OPACITY_MICROMAP_2_STATE_EXT,
    DISABLE_OPACITY_MICROMAPS = VK_GEOMETRY_INSTANCE_DISABLE_OPACITY_MICROMAPS_EXT,
    TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT = VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR,
  };

  struct BlasCreateInfo
  {
    std::optional<VkCommandBuffer> commandBuffer     = {};
    AccelerationStructureGeometryFlags geoemtryFlags = {};
    AccelerationStructureBuildFlags buildFlags       = {};

    VkFormat vertexFormat        = VK_FORMAT_R32G32B32_SFLOAT;
    VkDeviceAddress vertexBuffer = 0;
    VkDeviceAddress indexBuffer  = 0;
    VkDeviceSize vertexStride    = 0;
    uint32_t numVertices         = 0;
    VkIndexType indexType        = VK_INDEX_TYPE_UINT32;
    uint32_t numIndices          = 0;
  };

  class Blas
  {
  public:
    explicit Blas(const BlasCreateInfo& createInfo, std::string name = {});
    ~Blas();

    Blas(const Blas& other)            = delete;
    Blas& operator=(const Blas& other) = delete;
    Blas(Blas&& other) noexcept;
    Blas& operator=(Blas&& other) noexcept;

    VkAccelerationStructureKHR Handle() const noexcept
    {
      return handle_;
    }

    const Buffer& GetBuffer() const noexcept
    {
      return *buffer_;
    }

    VkDeviceSize GetAddress() const noexcept
    {
      return address_;
    }

    const BlasCreateInfo& GetCreateInfo() const noexcept
    {
      return createInfo_;
    }

  private:
    VkAccelerationStructureKHR handle_;
    // Buffer holding the actual AS data
    std::optional<Buffer> buffer_;
    // Address of the acceleration structure
    VkDeviceSize address_;

    BlasCreateInfo createInfo_;
  };

  struct TlasInstance
  {
    VkTransformMatrixKHR transform = {};
    uint32_t instanceCustomIndex : 24 = 0;
    uint32_t mask : 8 = 0;
    uint32_t shaderBindingTableOffset : 24 = 0;
    AccelerationStructureGeometryInstanceFlag flags : 8 = {};
    VkDeviceAddress blasAddress = 0;
  };
  static_assert(sizeof(TlasInstance) == sizeof(VkAccelerationStructureInstanceKHR));

  struct TlasCreateInfo
  {
    std::optional<VkCommandBuffer> commandBuffer     = {};
    AccelerationStructureGeometryFlags geoemtryFlags = {};
    AccelerationStructureBuildFlags buildFlags       = {};

    const Buffer* instanceBuffer = nullptr;
  };

  class Tlas
  {
  public:
    Tlas(const TlasCreateInfo& createInfo, std::string name = {});
    ~Tlas();

    Tlas(const Tlas& other)            = delete;
    Tlas& operator=(const Tlas& other) = delete;
    Tlas(Tlas&& other) noexcept;
    Tlas& operator=(Tlas&& other) noexcept;

    VkAccelerationStructureKHR Handle() const noexcept
    {
      return handle_;
    }

    const Buffer& GetBuffer() const noexcept
    {
      return *buffer_;
    }

    VkDeviceSize GetAddress() const noexcept
    {
      return address_;
    }

    const TlasCreateInfo& GetCreateInfo() const noexcept
    {
      return createInfo_;
    }

    Device::DescriptorInfo::ResourceHandle GetResourceHandle()
    {
      return descriptorInfo_.value().GpuResource();
    }

  private:
    VkAccelerationStructureKHR handle_;
    // Buffer holding the actual AS data
    std::optional<Buffer> buffer_;
    // Address of the acceleration structure
    VkDeviceSize address_;
    std::optional<Device::DescriptorInfo> descriptorInfo_;

    TlasCreateInfo createInfo_;
  };
} // namespace Fvog
