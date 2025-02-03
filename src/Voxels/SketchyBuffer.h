#pragma once
#include "vk_mem_alloc.h"
#ifndef GAME_HEADLESS
  #include "Fvog/Buffer2.h"
#endif

#include <cassert>
#include <string>

// A CPU and GPU buffer pair, with CPU writes replicated on the GPU.
// Can be used to make index-based data structures "just work" on the GPU.
// Headless instances do not care about the GPU part, so that is disabled.
class SketchyBuffer
{
public:
  explicit SketchyBuffer(size_t bufferSize, std::string name = {});
  //~SketchyBuffer();

  //SketchyBuffer(const SketchyBuffer&) = delete;
  //SketchyBuffer& operator=(const SketchyBuffer&) = delete;

  //SketchyBuffer(SketchyBuffer&& old) noexcept;
  //SketchyBuffer& operator=(SketchyBuffer&& old) noexcept;

  struct Alloc
  {
    // Offset may differ from the one returned by
    size_t offset;
    VmaVirtualAllocation allocation;

    bool operator==(const Alloc&) const noexcept = default;
  };

  Alloc Allocate(size_t size, size_t alignment);
  void Free(Alloc alloc);

  // Get the address of the base object for indexing
  template<typename T>
  T* GetBase()
  {
    return reinterpret_cast<T*>(cpuBuffer_.get());
  }

  template<typename T>
  const T* GetBase() const
  {
    return reinterpret_cast<const T*>(cpuBuffer_.get());
  }

  auto GetAllocator() const
  {
    return allocator_;
  }

  size_t SizeBytes() const
  {
    return gpuBuffer_.SizeBytes();
  }

private:
  static constexpr size_t PAGE_SIZE = 1024;
  std::unique_ptr<std::byte[]> cpuBuffer_;
  VmaVirtualBlock allocator_{};

#ifndef GAME_HEADLESS
public:
  void FlushWritesToGPU(VkCommandBuffer cmd);

  Fvog::Buffer& GetGpuBuffer()
  {
    return gpuBuffer_;
  }

  // Update the object at an address that aliases the array returned by GetBase.
  template<typename T>
  void MarkDirtyPages(const T* address)
  {
    const auto* byteAddress = reinterpret_cast<const std::byte*>(address);
    assert(byteAddress >= cpuBuffer_.get());

    // Mark the pages affected by the write as dirty
    const auto offsetBytes = reinterpret_cast<uintptr_t>(byteAddress) - reinterpret_cast<uintptr_t>(cpuBuffer_.get());
    const auto minPage     = offsetBytes / PAGE_SIZE;
    const auto maxPage     = (offsetBytes + sizeof(T)) / PAGE_SIZE;
    for (auto i = minPage; i <= maxPage; i++)
    {
      dirtyPages_.insert((uint32_t)i);
    }
  }

private:
  Fvog::Buffer gpuBuffer_;
  std::unordered_set<uint32_t> dirtyPages_;
#endif
};