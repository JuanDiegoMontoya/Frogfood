#include "SketchyBuffer.h"

#include "Fvog/detail/Common.h"
#include "tracy/Tracy.hpp"

#include <bit>

SketchyBuffer::SketchyBuffer(size_t bufferSize, [[maybe_unused]] std::string name)
#ifndef GAME_HEADLESS
  : gpuBuffer_({.size = bufferSize, .flag = Fvog::BufferFlagThingy::NONE}, std::move(name))
#endif
{
  ZoneScoped;
  cpuBuffer_ = std::make_unique_for_overwrite<std::byte[]>(bufferSize);
  std::memset(cpuBuffer_.get(), 0xCD, bufferSize);
  Fvog::detail::CheckVkResult(vmaCreateVirtualBlock(Fvog::detail::Address(VmaVirtualBlockCreateInfo{.size = bufferSize}), &allocator_));
}

// TODO: dtor and move ops
//SketchyBuffer::~SketchyBuffer()
//{
//  vmaDestroyVirtualBlock(allocator_);
//}

SketchyBuffer::Alloc SketchyBuffer::Allocate(size_t size, size_t alignment)
{
  ZoneScoped;
  auto vmaAlign = alignment;
  // Fixup alignment and size if alignment isn't a power of two, which is required for VMA
  if (!std::has_single_bit(alignment))
  {
    size += alignment;
    vmaAlign = std::bit_ceil(alignment * 2);
  }

  auto allocation = VmaVirtualAllocation{};
  auto offset     = VkDeviceSize{};
  Fvog::detail::CheckVkResult(vmaVirtualAllocate(allocator_,
    Fvog::detail::Address(VmaVirtualAllocationCreateInfo{
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

void SketchyBuffer::Free(Alloc alloc)
{
  ZoneScoped;
  vmaVirtualFree(allocator_, alloc.allocation);
}

#ifndef GAME_HEADLESS
void SketchyBuffer::FlushWritesToGPU(VkCommandBuffer cmd)
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
#endif
