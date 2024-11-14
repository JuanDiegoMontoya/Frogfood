#include "Buffer2.h"

#include "detail/Common.h"

#include "Rendering2.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <tracy/Tracy.hpp>

#include <bit>
#include <cstddef>
#include <utility>

namespace Fvog
{
  Buffer::Buffer(const BufferCreateInfo& createInfo, std::string name)
    : createInfo_(createInfo),
      name_(std::move(name))
  {
    using namespace detail;
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name_.data(), name_.size());
    // The only usages that have a practical perf implication on modern desktop hardware are *_DESCRIPTOR_BUFFER_BIT
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (GetDevice().supportsRayTracing)
    {
      usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
               VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
    }

    auto vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    auto vmaAllocFlags = VmaAllocationCreateFlags{};

    if (createInfo.flag & BufferFlagThingy::MAP_SEQUENTIAL_WRITE)
    {
      vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
      vmaAllocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    if (createInfo.flag & BufferFlagThingy::MAP_RANDOM_ACCESS)
    {
      vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
      vmaAllocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    }

    // BAR/ReBAR memory
    if (createInfo.flag & BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE)
    {
      vmaAllocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    auto size = std::max(createInfo.size, VkDeviceSize(1));
    auto allocationInfo = VmaAllocationInfo{};
    CheckVkResult(vmaCreateBuffer(
      Fvog::GetDevice().allocator_,
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
    vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
      detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_BUFFER,
      .objectHandle = reinterpret_cast<uint64_t>(buffer_),
      .pObjectName = name_.data(),
    }));

    mappedMemory_ = allocationInfo.pMappedData;
    
    deviceAddress_ = vkGetBufferDeviceAddress(Fvog::GetDevice().device_,
      Address(VkBufferDeviceAddressInfo{
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = buffer_,
    }));

    if (!(createInfo.flag & BufferFlagThingy::NO_DESCRIPTOR))
    {
      descriptorInfo_ = Fvog::GetDevice().AllocateStorageBufferDescriptor(buffer_);
    }
  }

  Buffer::~Buffer()
  {
    if (buffer_ != VK_NULL_HANDLE)
    {
      Fvog::GetDevice().bufferDeletionQueue_.emplace_back(Fvog::GetDevice().frameNumber, allocation_, buffer_, std::move(name_));
    }
  }

  Buffer::Buffer(Buffer&& old) noexcept
    : createInfo_(std::exchange(old.createInfo_, {})),
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
    auto stagingBuffer = Buffer({.size = data.size_bytes(), .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE | BufferFlagThingy::NO_DESCRIPTOR}, "Staging Buffer (UpdateDataExpensive)");
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
    assert(data.size_bytes() <= stagingBuffer.GetCreateInfo().size);
    void* ptr = stagingBuffer.GetMappedMemory();
    memcpy(ptr, data.data(), data.size_bytes());

    vkCmdCopyBuffer2(commandBuffer, detail::Address(VkCopyBufferInfo2{
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
      .srcBuffer = stagingBuffer.Handle(),
      .dstBuffer = deviceBuffer.Handle(),
      .regionCount = 1,
      .pRegions = detail::Address(VkBufferCopy2{
        .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
        .srcOffset = 0,
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

  ManagedBuffer::Alloc::~Alloc()
  {
    if (allocator_ && allocation_)
    {
      Fvog::GetDevice().genericDeletionQueue_.emplace_back(
        [allocator = allocator_, allocation = allocation_, frameOfLastUse = Fvog::GetDevice().frameNumber](uint64_t value) -> bool
        {
          if (value >= frameOfLastUse)
          {
            vmaVirtualFree(allocator, allocation);
            return true;
          }
          return false;
        });
    }
  }

  ManagedBuffer::Alloc::Alloc(Alloc&& old) noexcept
    : allocator_(std::exchange(old.allocator_, nullptr)),
      allocation_(std::exchange(old.allocation_, nullptr)),
      offset_(std::exchange(old.offset_, 0)),
      allocSize_(std::exchange(old.allocSize_, 0)),
      dataSize_(std::exchange(old.dataSize_, 0))
  {
  }

  ManagedBuffer::Alloc& ManagedBuffer::Alloc::operator=(Alloc&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~Alloc();
    return *new (this) Alloc(std::move(old));
  }

  VkDeviceSize ManagedBuffer::Alloc::GetOffset() const noexcept
  {
    return offset_;
  }

  VkDeviceSize ManagedBuffer::Alloc::GetDataSize() const noexcept
  {
    return dataSize_;
  }

  VkDeviceSize ManagedBuffer::Alloc::GetAllocSize() const noexcept
  {
    return allocSize_;
  }

  // TODO: Instances of this buffer will probably be huge (>256MB), so the map flag will require ReBAR on the user's system.
  // An upload system using staging buffers and buffer copies should be used instead.
  ManagedBuffer::ManagedBuffer(size_t bufferSize, std::string name)
    : buffer_({.size = bufferSize, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE}, std::move(name))
  {
    detail::CheckVkResult(vmaCreateVirtualBlock(detail::Address(VmaVirtualBlockCreateInfo{
      .size = bufferSize,
    }), &allocator));
  }

  ManagedBuffer::~ManagedBuffer()
  {
    vmaDestroyVirtualBlock(allocator);
  }

  ManagedBuffer::ManagedBuffer(ManagedBuffer&& old) noexcept
    : buffer_(std::move(old.buffer_)),
      allocator(std::exchange(old.allocator, nullptr))
  {
  }

  ManagedBuffer& ManagedBuffer::operator=(ManagedBuffer&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~ManagedBuffer();
    return *new (this) ManagedBuffer(std::move(old));
  }

  ManagedBuffer::Alloc ManagedBuffer::Allocate(VkDeviceSize size, const VkDeviceSize alignment)
  {
    const auto dataSize = size;
    auto vmaAlign = alignment;
    // Fixup alignment and size if alignment isn't a power of two, which is required for VMA
    if (!std::has_single_bit(alignment))
    {
      size += alignment;
      vmaAlign = std::bit_ceil(alignment * 2);
    }

    auto allocation = VmaVirtualAllocation{};
    auto offset     = VkDeviceSize{};
    detail::CheckVkResult(vmaVirtualAllocate(allocator,
      detail::Address(VmaVirtualAllocationCreateInfo{
        .size      = size,
        .alignment = vmaAlign,
      }),
      &allocation,
      &offset));

    // Push offset forward to multiple of the true alignment, then subtract that amount from the remaining size
    auto offsetAmount = (alignment - (offset % alignment)) % alignment;
    offset += offsetAmount;
    size -= offsetAmount;
    assert(offset % alignment == 0);
    return Alloc(allocator, allocation, offset, size, dataSize);
  }

  ContiguousManagedBuffer::ContiguousManagedBuffer(size_t bufferSize, std::string name)
    : buffer_({.size = bufferSize}, std::move(name)),
      currentSize_(0)
  {
  }

  ContiguousManagedBuffer::Alloc ContiguousManagedBuffer::Allocate(size_t size)
  {
    assert(currentSize_ + size <= buffer_.SizeBytes());
    assert(size > 0);
    const auto alloc = Alloc{currentSize_, size};

    currentSize_ += size;
    return alloc;
  }

  void ContiguousManagedBuffer::Free(Alloc allocation, VkCommandBuffer commandBuffer)
  {
    // Copy allocation.size bytes from the end of buffer_ to freed allocation, then pop.
    auto ctx = Context(commandBuffer);
    ctx.Barrier();
    ctx.CopyBuffer(buffer_, buffer_, {
      .srcOffset = currentSize_ - allocation.size,
      .dstOffset = allocation.offset,
      .size = allocation.size,
    });

    currentSize_ -= allocation.size;
  }

  ReplicatedBuffer::ReplicatedBuffer(size_t bufferSize, std::string name)
    : gpuBuffer_({.size = bufferSize, .flag = BufferFlagThingy::NONE}, std::move(name))
  {
    ZoneScoped;
    cpuBuffer_ = std::make_unique_for_overwrite<std::byte[]>(bufferSize);
    std::memset(cpuBuffer_.get(), 0xCD, bufferSize);
    detail::CheckVkResult(vmaCreateVirtualBlock(
      detail::Address(VmaVirtualBlockCreateInfo{
        .size = bufferSize,
      }),
      &allocator_));
  }

  ReplicatedBuffer::Alloc ReplicatedBuffer::Allocate(size_t size, size_t alignment)
  {
    ZoneScoped;
    auto vmaAlign       = alignment;
    // Fixup alignment and size if alignment isn't a power of two, which is required for VMA
    if (!std::has_single_bit(alignment))
    {
      size += alignment;
      vmaAlign = std::bit_ceil(alignment * 2);
    }

    auto allocation = VmaVirtualAllocation{};
    auto offset     = VkDeviceSize{};
    detail::CheckVkResult(vmaVirtualAllocate(allocator_,
      detail::Address(VmaVirtualAllocationCreateInfo{
        .size      = size,
        .alignment = vmaAlign,
      }),
      &allocation,
      &offset));

    // Push offset forward to multiple of the true alignment, then subtract that amount from the remaining size
    auto offsetAmount = (alignment - (offset % alignment)) % alignment;
    offset += offsetAmount;
    assert(offset % alignment == 0);
    return {offset, allocation};
  }

  void ReplicatedBuffer::Free(Alloc alloc)
  {
    ZoneScoped;
    vmaVirtualFree(allocator_, alloc.allocation);  
  }

  void ReplicatedBuffer::FlushWritesToGPU(VkCommandBuffer cmd)
  {
    ZoneScoped;
    // All pages need to be flushed or the underlying data may have invalid references/indices
    for (uint32_t page : dirtyPages_)
    {
      auto offset = page * PAGE_SIZE;
      vkCmdUpdateBuffer(cmd, gpuBuffer_.Handle(), offset, PAGE_SIZE, cpuBuffer_.get() + offset);
    }

    dirtyPages_.clear();
  }
} // namespace Fvog
