#include "SketchyBuffer.h"

#include "Fvog/detail/Common.h"
#include "tracy/Tracy.hpp"

#include <bit>

namespace
{
  constexpr bool profileVoxelPool = true;

  [[maybe_unused]] constexpr auto poolTracyHeapName = "Voxel Storage (CPU & GPU)";
} // namespace

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

SketchyBuffer::~SketchyBuffer()
{
  // March 18, 2025: waiting for next release of Tracy, which will have TracyMemoryDiscard. Without it, the client will disconnect
  // after this call, seeing the same address allocated twice, as it has not observed the pool's destruction here.
  if constexpr (profileVoxelPool)
  {
    // TracyMemoryDiscard(poolTracyHeapName);
  }
  vmaDestroyVirtualBlock(allocator_);
}

SketchyBuffer::SketchyBuffer(SketchyBuffer&& old) noexcept
 : cpuBuffer_(std::move(old.cpuBuffer_)),
   allocator_(std::exchange(old.allocator_, nullptr))
#ifndef GAME_HEADLESS
   , gpuBuffer_(std::move(old.gpuBuffer_)),
   dirtyPages_(std::move(old.dirtyPages_))
#endif
{}

SketchyBuffer& SketchyBuffer::operator=(SketchyBuffer&& old) noexcept
{
  if (&old == this)
    return *this;
  this->~SketchyBuffer();
  return *new (this) SketchyBuffer(std::move(old));
}

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
  // We only expect to have one SketchyBuffer, so this should be fine.
  if constexpr (profileVoxelPool)
  {
    TracyAllocN(allocation, size, poolTracyHeapName);
  }

  // Push offset forward to multiple of the true alignment, then subtract that amount from the remaining size
  auto offsetAmount = (alignment - (offset % alignment)) % alignment;
  offset += offsetAmount;
  assert(offset % alignment == 0);
  return {offset, allocation};
}

void SketchyBuffer::Free(Alloc alloc)
{
  ZoneScoped;
  if constexpr (profileVoxelPool)
  {
    TracyFreeN(alloc.allocation, poolTracyHeapName);
  }
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
