#include "Buffer2.h"

#include "detail/Common.h"

#include <volk.h>
#include <vk_mem_alloc.h>

namespace Fvog
{
  Buffer::Buffer(Device& device, const BufferCreateInfo& createInfo)
    : device_(device)
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
  }

  Buffer::~Buffer()
  {
    if (buffer_ != VK_NULL_HANDLE)
    {
      device_.bufferDeletionQueue_.emplace_back(device_.frameNumber, allocation_, buffer_);
    }
  }
}
