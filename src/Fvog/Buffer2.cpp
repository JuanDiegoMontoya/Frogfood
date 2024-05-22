#include "Buffer2.h"

#include "detail/Common.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <tracy/Tracy.hpp>

#include <cstddef>
#include <utility>

namespace Fvog
{
  Buffer::Buffer(Device& device, const BufferCreateInfo& createInfo, std::string name)
    : device_(&device),
      createInfo_(createInfo),
      name_(std::move(name))
  {
    using namespace detail;
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name_.data(), name_.size());
    // The only usages that have a practical perf implication on modern desktop hardware are *_DESCRIPTOR_BUFFER_BIT
    constexpr auto usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    auto vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    auto vmaAllocFlags = VmaAllocationCreateFlags{};

    if (createInfo.flag == BufferFlagThingy::MAP_SEQUENTIAL_WRITE)
    {
      vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
      vmaAllocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    if (createInfo.flag == BufferFlagThingy::MAP_RANDOM_ACCESS)
    {
      vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
      vmaAllocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }

    auto size = std::max(createInfo.size, VkDeviceSize(1));
    auto allocationInfo = VmaAllocationInfo{};
    CheckVkResult(vmaCreateBuffer(
      device_->allocator_,
      Address(VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      }),
      Address(VmaAllocationCreateInfo{
        .flags = vmaAllocFlags,
        .usage = vmaUsage,
      }),
      &buffer_,
      &allocation_,
      &allocationInfo
    ));


    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(device_->device_, detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_BUFFER,
      .objectHandle = reinterpret_cast<uint64_t>(buffer_),
      .pObjectName = name_.data(),
    }));

    mappedMemory_ = allocationInfo.pMappedData;

    // Buffer is device-local
    if (createInfo.flag == BufferFlagThingy::NONE)
    {
      deviceAddress_ = vkGetBufferDeviceAddress(device_->device_, Address(VkBufferDeviceAddressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer_,
      }));

      descriptorInfo_ = device_->AllocateStorageBufferDescriptor(buffer_);
    }
  }

  Buffer::~Buffer()
  {
    if (buffer_ != VK_NULL_HANDLE)
    {
      device_->bufferDeletionQueue_.emplace_back(device_->frameNumber, allocation_, buffer_, std::move(name_));
    }
  }

  Buffer::Buffer(Buffer&& old) noexcept
    : device_(std::exchange(old.device_, nullptr)),
      createInfo_(std::exchange(old.createInfo_, {})),
      buffer_(std::exchange(old.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(old.allocation_, nullptr)),
      mappedMemory_(std::exchange(old.mappedMemory_, nullptr)),
      deviceAddress_(std::exchange(old.deviceAddress_, 0)),
      descriptorInfo_(std::move(old.descriptorInfo_)),
      name_(std::move(old.name_))
  {
  }

  Buffer& Buffer::operator=(Buffer&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~Buffer();
    return *new (this) Buffer(std::move(old));
  }

  void Buffer::UpdateDataExpensive(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes)
  {
    ZoneScoped;
    auto stagingBuffer = Buffer(*device_, {.size = createInfo_.size, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "Staging Buffer (UpdateDataExpensive)");
    UpdateDataGeneric(commandBuffer, data, destOffsetBytes, stagingBuffer, *this);
  }

  void Buffer::FillData(VkCommandBuffer commandBuffer, const BufferFillInfo& clear)
  {
    vkCmdFillBuffer(commandBuffer, buffer_, clear.offset, clear.size, clear.data);
  }

  void Buffer::UpdateDataGeneric(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes, Buffer& stagingBuffer, Buffer& deviceBuffer)
  {
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, deviceBuffer.name_.data(), deviceBuffer.name_.size());
    // TODO: temp
    vkCmdPipelineBarrier2(commandBuffer, detail::Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = detail::Address(VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
      }),
    }));

    // Overwrite some memory in a host-visible buffer, then copy it to the device buffer when the command buffer executes
    assert(data.size_bytes() + destOffsetBytes <= stagingBuffer.GetCreateInfo().size);
    void* ptr = stagingBuffer.GetMappedMemory();
    memcpy(static_cast<std::byte*>(ptr) + destOffsetBytes, data.data(), data.size_bytes());

    vkCmdCopyBuffer2(commandBuffer, detail::Address(VkCopyBufferInfo2{
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
      .srcBuffer = stagingBuffer.Handle(),
      .dstBuffer = deviceBuffer.Handle(),
      .regionCount = 1,
      .pRegions = detail::Address(VkBufferCopy2{
        .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
        .srcOffset = destOffsetBytes, // The 'dest' offset applies to both buffers
        .dstOffset = destOffsetBytes,
        .size = data.size_bytes(),
      }),
    }));

    // TODO: temp
    vkCmdPipelineBarrier2(commandBuffer, detail::Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = detail::Address(VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
      }),
    }));
  }
} // namespace Fvog
