#include "Buffer2.h"

#include "detail/Common.h"

#include <volk.h>
#include <vk_mem_alloc.h>

namespace Fvog
{
  Buffer::Buffer(Device& device, const BufferCreateInfo& createInfo)
    : device_(device),
      createInfo_(createInfo)
  {
    using namespace detail;
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

    auto allocationInfo = VmaAllocationInfo{};
    CheckVkResult(vmaCreateBuffer(
      device_.allocator_,
      Address(VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = createInfo.size,
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

    mappedMemory_ = allocationInfo.pMappedData;

    if (createInfo.flag == BufferFlagThingy::NONE)
    {
      deviceAddress_ = vkGetBufferDeviceAddress(device_.device_, Address(VkBufferDeviceAddressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer_,
      }));
    }
  }

  Buffer::~Buffer()
  {
    if (buffer_ != VK_NULL_HANDLE)
    {
      device_.bufferDeletionQueue_.emplace_back(device_.frameNumber, allocation_, buffer_);
    }
  }

  NDeviceBuffer::NDeviceBuffer(Device& device, VkDeviceSize size)
    : device_(device),
      hostStagingBuffers_{
        // TODO: create a helper for initializing this array so it doesn't fail to compile when Device::frameOverlap changes
        {Buffer(device, BufferCreateInfo{.size = size, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE})},
        {Buffer(device, BufferCreateInfo{.size = size, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE})},
      },
      deviceBuffer_(device, BufferCreateInfo{.size = size})
  {
  }


  void NDeviceBuffer::UpdateData(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes)
  {
    // Overwrite some memory in a host-visible buffer, then copy it to the device buffer when the command buffer executes
    auto& stagingBuffer = hostStagingBuffers_[device_.frameNumber % Device::frameOverlap];
    assert(data.size_bytes() + destOffsetBytes <= stagingBuffer.GetCreateInfo().size);
    void* ptr = stagingBuffer.GetMappedMemory();
    memcpy((char*)ptr + destOffsetBytes, data.data(), data.size_bytes());

    vkCmdCopyBuffer2(commandBuffer, detail::Address(VkCopyBufferInfo2{
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
      .srcBuffer = stagingBuffer.Handle(),
      .dstBuffer = deviceBuffer_.Handle(),
      .regionCount = 1,
      .pRegions = detail::Address(VkBufferCopy2{
        .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
        .srcOffset = destOffsetBytes, // The 'dest' offset applies to both buffers
        .dstOffset = destOffsetBytes,
        .size = data.size_bytes(),
      }),
    }));
  }
} // namespace Fvog
