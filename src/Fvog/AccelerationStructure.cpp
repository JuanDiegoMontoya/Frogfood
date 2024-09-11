#include "AccelerationStructure.h"
#include "detail/Common.h"

#include <volk.h>

#include <utility>

namespace Fvog
{
  Blas::Blas(const BlasCreateInfo& createInfo, std::string name) : createInfo_(createInfo)
  {
    assert(createInfo.numIndices >= 3);
    //assert(createInfo.numIndices % 3 == 0);
    assert(createInfo.numVertices >= 3);
    assert(createInfo.vertexBuffer != 0);
    assert(createInfo.indexBuffer != 0);
    //const uint32_t maxVertex = uint32_t(createInfo.vertexBuffer->SizeBytes() / createInfo.vertexStride - 1);

    const uint32_t primitiveCount = createInfo.numIndices / 3;

    const VkAccelerationStructureGeometryKHR geometryInfo = {
      .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
      .geometry =
        {
          .triangles =
            {
              .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
              .vertexFormat = createInfo.vertexFormat,
              .vertexData   = {.deviceAddress = createInfo.vertexBuffer},
              .vertexStride = createInfo.vertexStride,
              .maxVertex    = createInfo.numVertices - 1,
              .indexType    = createInfo.indexType,
              .indexData    = {.deviceAddress = createInfo.indexBuffer},
            },
        },
      .flags = static_cast<VkGeometryFlagsKHR>(createInfo.geoemtryFlags),
    };

    //const uint32_t primitiveCount = uint32_t(createInfo.indexBuffer->SizeBytes() / sizeof(uint32_t) / 3);

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
      .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
      .flags         = static_cast<VkBuildAccelerationStructureFlagsKHR>(createInfo.buildFlags),
      .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = 1,
      .pGeometries   = &geometryInfo,
    };

    VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vkGetAccelerationStructureBuildSizesKHR(Fvog::GetDevice().device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &buildSizeInfo);

    Buffer blasBuffer(
      BufferCreateInfo{
        .size = buildSizeInfo.accelerationStructureSize,
        .flag = BufferFlagThingy::NO_DESCRIPTOR,
      },
      name + " BLAS Buffer");

    const VkAccelerationStructureCreateInfoKHR blasInfo = {
      .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
      .buffer = blasBuffer.Handle(),
      .size   = buildSizeInfo.accelerationStructureSize,
      .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    };

    VkAccelerationStructureKHR blas;
    detail::CheckVkResult(vkCreateAccelerationStructureKHR(Fvog::GetDevice().device_, &blasInfo, nullptr, &blas));

    Buffer scratchBuildBuffer(
      BufferCreateInfo{
        .size = buildSizeInfo.buildScratchSize,
        .flag = BufferFlagThingy::NO_DESCRIPTOR,
      },
      name + " BLAS Scratch Buffer");

    Fvog::GetDevice().ImmediateSubmit(
      [&](VkCommandBuffer commandBuffer)
      {
        buildInfo.dstAccelerationStructure = blas;
        buildInfo.scratchData              = {scratchBuildBuffer.GetDeviceAddress()};

        vkCmdPipelineBarrier2(commandBuffer,
          detail::Address(VkDependencyInfo{
            .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers    = detail::Address(VkMemoryBarrier2{
                 .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                 .srcStageMask  = VK_PIPELINE_STAGE_2_NONE,
                 .srcAccessMask = VK_ACCESS_2_NONE,
                 .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                 .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            }),
          }));
        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {.primitiveCount = primitiveCount};
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, detail::Address(&buildRangeInfo));
        // TODO: better barrier
        vkCmdPipelineBarrier2(commandBuffer,
          detail::Address(VkDependencyInfo{
            .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers    = detail::Address(VkMemoryBarrier2{
                 .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                 .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                 .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                 .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            }),
          }));
      });

    if (createInfo.buildFlags & AccelerationStructureBuildFlag::ALLOW_COMPACTION)
    {
      const VkQueryPoolCreateInfo queryPoolInfo = {
        .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
        .queryCount = 1,
      };

      VkQueryPool compactedSizeQuery;
      detail::CheckVkResult(vkCreateQueryPool(Fvog::GetDevice().device_, &queryPoolInfo, nullptr, &compactedSizeQuery));

      Fvog::GetDevice().ImmediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
          vkCmdResetQueryPool(commandBuffer, compactedSizeQuery, 0, 1);
          vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, 1, &blas, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, compactedSizeQuery, 0);
        });
      uint64_t compactedSize;
      detail::CheckVkResult(vkGetQueryPoolResults(Fvog::GetDevice().device_,
        compactedSizeQuery,
        0,
        1,
        sizeof(uint64_t),
        &compactedSize,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      vkDestroyQueryPool(Fvog::GetDevice().device_, compactedSizeQuery, nullptr);

      Buffer compactBlasBuffer(
        BufferCreateInfo{
          .size = compactedSize,
          .flag = BufferFlagThingy::NO_DESCRIPTOR,
        },
        name + " Compact BLAS Buffer");

      VkAccelerationStructureCreateInfoKHR compactBlasInfo = {
        .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = compactBlasBuffer.Handle(),
        .size   = compactedSize,
        .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
      };

      VkAccelerationStructureKHR compactBlas;
      detail::CheckVkResult(vkCreateAccelerationStructureKHR(Fvog::GetDevice().device_, &compactBlasInfo, nullptr, &compactBlas));

      Fvog::GetDevice().ImmediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
          vkCmdPipelineBarrier2(commandBuffer,
            detail::Address(VkDependencyInfo{
              .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
              .memoryBarrierCount = 1,
              .pMemoryBarriers    = detail::Address(VkMemoryBarrier2{
                   .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                   .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                   .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                   .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                   .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
              }),
            }));
          vkCmdCopyAccelerationStructureKHR(commandBuffer,
            detail::Address(VkCopyAccelerationStructureInfoKHR{.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
              .src                                                    = blas,
              .dst                                                    = compactBlas,
              .mode                                                   = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR}));
          // TODO: better barrier
          vkCmdPipelineBarrier2(commandBuffer,
            detail::Address(VkDependencyInfo{
              .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
              .memoryBarrierCount = 1,
              .pMemoryBarriers    = detail::Address(VkMemoryBarrier2{
                   .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                   .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                   .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                   .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
              }),
            }));
        });

      vkDestroyAccelerationStructureKHR(Fvog::GetDevice().device_, blas, nullptr);
      blasBuffer = std::move(compactBlasBuffer);
      blas = compactBlas;
    }

    buffer_ = std::move(blasBuffer);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {
      .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = blas,
    };
    address_ = vkGetAccelerationStructureDeviceAddressKHR(Fvog::GetDevice().device_, &addressInfo);

    vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
      detail::Address(VkDebugUtilsObjectNameInfoEXT{
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType   = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
        .objectHandle = reinterpret_cast<uint64_t>(blas),
        .pObjectName  = name.c_str(),
      }));
    handle_ = blas;
  }

  Blas::~Blas()
  {
    if (handle_)
    {
      // TODO: Move to deletion queue
      vkDestroyAccelerationStructureKHR(Fvog::GetDevice().device_, handle_, nullptr);
    }
  }

  Blas::Blas(Blas&& other) noexcept
    : handle_(std::exchange(other.handle_, {})),
      buffer_(std::move(other.buffer_)),
      address_(std::exchange(other.address_, {})),
      createInfo_(std::exchange(other.createInfo_, {}))
  {
  }

  Blas& Blas::operator=(Blas&& other) noexcept
  {
    if (&other == this)
    {
      return *this;
    }
    this->~Blas();
    return *new (this) Blas(std::move(other));
  }

  Tlas::Tlas(const TlasCreateInfo& createInfo, std::string name) : createInfo_(createInfo)
  {
    const VkAccelerationStructureGeometryKHR geometryInfo = {
      .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
      .geometry     = {.instances =
                         {
                           .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                           .arrayOfPointers = false,
                           .data            = createInfo.instanceBuffer->GetDeviceAddress(),
                     }},
      .flags        = static_cast<VkGeometryFlagsKHR>(createInfo.geoemtryFlags),
    };

    const uint32_t instanceCount = uint32_t(createInfo.instanceBuffer->SizeBytes() / sizeof(TlasInstance));

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
      .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
      .flags         = static_cast<VkBuildAccelerationStructureFlagsKHR>(createInfo.buildFlags),
      .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = 1,
      .pGeometries   = &geometryInfo,
    };

    VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vkGetAccelerationStructureBuildSizesKHR(Fvog::GetDevice().device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &instanceCount, &buildSizeInfo);

    Buffer tlasBuffer(
      BufferCreateInfo{
        .size = buildSizeInfo.accelerationStructureSize,
        .flag = BufferFlagThingy::NO_DESCRIPTOR,
      },
      name + " TLAS Buffer");

    VkAccelerationStructureCreateInfoKHR tlasInfo = {
      .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
      .buffer = tlasBuffer.Handle(),
      .size   = buildSizeInfo.accelerationStructureSize,
      .type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    };

    VkAccelerationStructureKHR tlas;
    detail::CheckVkResult(vkCreateAccelerationStructureKHR(Fvog::GetDevice().device_, &tlasInfo, nullptr, &tlas));

    Buffer scratchBuildBuffer(
      BufferCreateInfo{
        .size = buildSizeInfo.buildScratchSize,
        .flag = BufferFlagThingy::NO_DESCRIPTOR,
      },
      name + " TLAS Scratch Buffer");

    Fvog::GetDevice().ImmediateSubmit(
      [&](VkCommandBuffer commandBuffer)
      {
        buildInfo.dstAccelerationStructure = tlas;
        buildInfo.scratchData              = {scratchBuildBuffer.GetDeviceAddress()};

        vkCmdPipelineBarrier2(commandBuffer,
          detail::Address(VkDependencyInfo{
            .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers    = detail::Address(VkMemoryBarrier2{
                 .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                 .srcStageMask  = VK_PIPELINE_STAGE_2_NONE,
                 .srcAccessMask = VK_ACCESS_2_NONE,
                 .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                 .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            }),
          }));
        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {.primitiveCount = instanceCount};
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, detail::Address(&buildRangeInfo));
        // TODO: better barrier
        vkCmdPipelineBarrier2(commandBuffer,
          detail::Address(VkDependencyInfo{
            .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers    = detail::Address(VkMemoryBarrier2{
                 .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                 .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                 .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                 .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            }),
          }));
      });

    if (createInfo.buildFlags & AccelerationStructureBuildFlag::ALLOW_COMPACTION)
    {
      const VkQueryPoolCreateInfo queryPoolInfo = {
        .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
        .queryCount = 1,
      };

      VkQueryPool compactedSizeQuery;
      detail::CheckVkResult(vkCreateQueryPool(Fvog::GetDevice().device_, &queryPoolInfo, nullptr, &compactedSizeQuery));

      Fvog::GetDevice().ImmediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
          vkCmdResetQueryPool(commandBuffer, compactedSizeQuery, 0, 1);
          vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, 1, &tlas, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, compactedSizeQuery, 0);
        });
      uint64_t compactedSize;
      detail::CheckVkResult(vkGetQueryPoolResults(Fvog::GetDevice().device_,
        compactedSizeQuery,
        0,
        1,
        sizeof(uint64_t),
        &compactedSize,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      vkDestroyQueryPool(Fvog::GetDevice().device_, compactedSizeQuery, nullptr);

      Buffer compactTlasBuffer(
        BufferCreateInfo{
          .size = compactedSize,
          .flag = BufferFlagThingy::NO_DESCRIPTOR,
        },
        name + " Compact TLAS Buffer");

      VkAccelerationStructureCreateInfoKHR compactTlasInfo = {
        .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = compactTlasBuffer.Handle(),
        .size   = compactedSize,
        .type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
      };

      VkAccelerationStructureKHR compactTlas;
      detail::CheckVkResult(vkCreateAccelerationStructureKHR(Fvog::GetDevice().device_, &compactTlasInfo, nullptr, &compactTlas));

      Fvog::GetDevice().ImmediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
          vkCmdPipelineBarrier2(commandBuffer,
            detail::Address(VkDependencyInfo{
              .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
              .memoryBarrierCount = 1,
              .pMemoryBarriers    = detail::Address(VkMemoryBarrier2{
                   .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                   .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                   .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                   .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                   .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
              }),
            }));
          vkCmdCopyAccelerationStructureKHR(commandBuffer,
            detail::Address(VkCopyAccelerationStructureInfoKHR{.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
              .src                                                    = tlas,
              .dst                                                    = compactTlas,
              .mode                                                   = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR}));
          vkCmdPipelineBarrier2(commandBuffer,
            detail::Address(VkDependencyInfo{
              .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
              .memoryBarrierCount = 1,
              .pMemoryBarriers    = detail::Address(VkMemoryBarrier2{
                   .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                   .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                   .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                   .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
              }),
            }));
        });

      vkDestroyAccelerationStructureKHR(Fvog::GetDevice().device_, tlas, nullptr);
      tlasBuffer = std::move(compactTlasBuffer);
      tlas = compactTlas;
    }

    buffer_ = std::move(tlasBuffer);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = {
      .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = tlas,
    };
    address_ = vkGetAccelerationStructureDeviceAddressKHR(Fvog::GetDevice().device_, &addressInfo);

    vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
      detail::Address(VkDebugUtilsObjectNameInfoEXT{
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType   = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
        .objectHandle = reinterpret_cast<uint64_t>(tlas),
        .pObjectName  = name.c_str(),
      }));
    handle_ = tlas;

    descriptorInfo_ = Fvog::GetDevice().AllocateAccelerationStructureDescriptor(tlas);
  }

  Tlas::~Tlas()
  {
    if (handle_)
    {
      Fvog::GetDevice().genericDeletionQueue_.emplace_back(
        [handle = handle_, frameOfLastUse = Fvog::GetDevice().frameNumber](uint64_t value) -> bool
        {
          if (value >= frameOfLastUse)
          {
            vkDestroyAccelerationStructureKHR(Fvog::GetDevice().device_, handle, nullptr);
            return true;
          }
          return false;
        });
    }
  }

  Tlas::Tlas(Tlas&& other) noexcept
    : handle_(std::exchange(other.handle_, {})),
      buffer_(std::move(other.buffer_)),
      address_(std::exchange(other.address_, {})),
      descriptorInfo_(std::move(other.descriptorInfo_)),
      createInfo_(std::exchange(other.createInfo_, {}))
  {
  }

  Tlas& Tlas::operator=(Tlas&& other) noexcept
  {
    if (&other == this)
    {
      return *this;
    }
    this->~Tlas();
    return *new (this) Tlas(std::move(other));
  }
} // namespace Fvog
