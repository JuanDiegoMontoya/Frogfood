#pragma once
#include "Device.h"
#include "TriviallyCopyableByteSpan.h"
#include "detail/Flags.h"
#include "shaders/Resources.h.glsl"

#include <vulkan/vulkan_core.h>

#include <string>
#include <string_view>
#include <optional>
#include <cstddef>
#include <memory>
#include <unordered_set>

namespace Fvog
{
  // TODO: make this have less footgunny semantics
  enum class BufferFlagThingy
  {
    NONE                        = 0,
    MAP_SEQUENTIAL_WRITE        = 1,
    MAP_RANDOM_ACCESS           = 2,
    MAP_SEQUENTIAL_WRITE_DEVICE = 4,
    NO_DESCRIPTOR               = 8
  };
  FVOG_DECLARE_FLAG_TYPE(BufferFlags, BufferFlagThingy, uint32_t);

  struct BufferCreateInfo
  {
    // Size in bytes
    VkDeviceSize size{};
    BufferFlags flag{};
  };

  struct BufferFillInfo
  {
    VkDeviceSize offset = 0;
    VkDeviceSize size = VK_WHOLE_SIZE;
    uint32_t data = 0;
  };
  
  class Buffer
  {
  public:
    explicit Buffer(const BufferCreateInfo& createInfo, std::string name = {});
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;

    [[nodiscard]] VkBuffer Handle() const noexcept
    {
      return buffer_;
    }

    [[nodiscard]] void* GetMappedMemory() const noexcept
    {
      return mappedMemory_;
    }

    [[nodiscard]] const VkDeviceAddress& GetDeviceAddress() const noexcept
    {
      return deviceAddress_;
    }

    [[nodiscard]] BufferCreateInfo GetCreateInfo() const noexcept
    {
      return createInfo_;
    }

    [[nodiscard]] VkDeviceSize SizeBytes() const noexcept
    {
      return createInfo_.size;
    }

    void UpdateDataExpensive(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes = 0);

    void FillData(VkCommandBuffer commandBuffer, const BufferFillInfo& clear = {});

    Device::DescriptorInfo::ResourceHandle GetResourceHandle()
    {
      return descriptorInfo_.value().GpuResource();
    }

    [[nodiscard]] shared::Buffer GetBuffer() noexcept
    {
      return {descriptorInfo_.value().GpuResource().index};
    }

    const std::string& GetName() const
    {
      return name_;
    }

    [[nodiscard]] operator shared::Buffer() noexcept
    {
      return {descriptorInfo_.value().GpuResource().index};
    }

  protected:
    BufferCreateInfo createInfo_{};
    VkBuffer buffer_{};
    VmaAllocation allocation_{};
    void* mappedMemory_{};
    VkDeviceAddress deviceAddress_{};
    std::optional<Device::DescriptorInfo> descriptorInfo_;
    std::string name_;

    template<typename T>
    friend class NDeviceBuffer;

    static void UpdateDataGeneric(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes, Buffer& stagingBuffer, Buffer& deviceBuffer);
  };

  struct TypedBufferCreateInfo
  {
    uint32_t count{1};
    BufferFlags flag{};
  };
  
  template<typename T = std::byte>
    requires std::is_trivially_copyable_v<T>
  class TypedBuffer : public Buffer
  {
  public:
    explicit TypedBuffer(const TypedBufferCreateInfo& createInfo = {}, std::string name = {})
      : Buffer({.size = createInfo.count * sizeof(T), .flag = createInfo.flag}, std::move(name))
    {
      //assert(createInfo.count > 0);
    }

    // Number of elements of T that this buffer could hold
    [[nodiscard]] uint32_t Size() const noexcept
    {
      return static_cast<uint32_t>(createInfo_.size) / sizeof(T);
    }

    [[nodiscard]] T* GetMappedMemory() const noexcept
    {
      return static_cast<T*>(mappedMemory_);
    }

    // UpdateDataExpensive CAN be called multiple times per frame, but each time it allocates a new buffer
    void UpdateDataExpensive(VkCommandBuffer commandBuffer, std::span<const T> data, VkDeviceSize destOffsetBytes = 0)
    {
      Buffer::UpdateDataExpensive(commandBuffer, data, destOffsetBytes);
    }

    void UpdateDataExpensive(VkCommandBuffer commandBuffer, const T& data, VkDeviceSize destOffsetBytes = 0)
    {
      Buffer::UpdateDataExpensive(commandBuffer, std::span(&data, 1), destOffsetBytes);
    }

  private:
  };

  // Consists of N staging buffers for upload and one device buffer
  // Use for buffers that need to be uploaded every frame
  template<typename T = std::byte>
    //requires std::is_trivially_copyable_v<T>
  class NDeviceBuffer
  {
  public:
    explicit NDeviceBuffer(uint32_t count = 1, std::string name = {})
    : deviceBuffer_(TypedBufferCreateInfo{.count = count}, std::move(name))
    {
      for (auto& buffer : hostStagingBuffers_)
      {
        buffer = TypedBuffer<T>(TypedBufferCreateInfo{.count = count, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, name + std::string(" (host)"));
      }
    }

    // Number of elements of T that this buffer could hold
    [[nodiscard]] uint32_t Size() const noexcept
    {
      return deviceBuffer_.Size();
    }

    void UpdateData(VkCommandBuffer commandBuffer, std::span<const T> data, VkDeviceSize destOffsetElements = 0)
    {
      updateData(commandBuffer, data, destOffsetElements * sizeof(T));
    }

    void UpdateData(VkCommandBuffer commandBuffer, const T& data, VkDeviceSize destOffsetElements = 0)
    {
      updateData(commandBuffer, std::span(&data, 1), destOffsetElements * sizeof(T));
    }

    [[nodiscard]] Buffer& GetDeviceBuffer() noexcept
    {
      return deviceBuffer_;
    }

  private:
    // Only call within a command buffer, once per frame
    void updateData(VkCommandBuffer commandBuffer, TriviallyCopyableByteSpan data, VkDeviceSize destOffsetBytes = 0)
    {
      if (data.size() == 0)
      {
        return;
      }
      auto& stagingBuffer = *hostStagingBuffers_[Fvog::GetDevice().frameNumber % Device::frameOverlap];
      stagingBuffer.UpdateDataGeneric(commandBuffer, data, destOffsetBytes, stagingBuffer, deviceBuffer_);
    }

    // Use optional as a hacky way to allow for deferred initialization.
    std::optional<TypedBuffer<T>> hostStagingBuffers_[Device::frameOverlap];
    TypedBuffer<T> deviceBuffer_;
  };

  // A buffer from which chunks can be allocated and then safely freed on the GPU timeline.
  class ManagedBuffer
  {
  public:
    class Alloc
    {
    public:
      explicit Alloc(VmaVirtualBlock allocator, VmaVirtualAllocation allocation, size_t offset, size_t allocSize, size_t dataSize)
        : allocator_(allocator), allocation_(allocation), offset_(offset), allocSize_(allocSize), dataSize_(dataSize)
      {
      }
      ~Alloc();

      Alloc(const Alloc&)            = delete;
      Alloc& operator=(const Alloc&) = delete;
      Alloc(Alloc&& old) noexcept;
      Alloc& operator=(Alloc&& old) noexcept;

      bool operator==(const Alloc&) const noexcept = default;

      [[nodiscard]] VkDeviceSize GetOffset() const noexcept;
      [[nodiscard]] VkDeviceSize GetDataSize() const noexcept;
      [[nodiscard]] VkDeviceSize GetAllocSize() const noexcept;

    private:
      VmaVirtualBlock allocator_;
      VmaVirtualAllocation allocation_;
      size_t offset_;
      size_t allocSize_; // Adjusted for alignment
      size_t dataSize_;
    };

    explicit ManagedBuffer(size_t bufferSize, std::string name = {});
    ~ManagedBuffer();

    ManagedBuffer(const ManagedBuffer&) = delete;
    ManagedBuffer(ManagedBuffer&&) noexcept;
    ManagedBuffer& operator=(const ManagedBuffer&) = delete;
    ManagedBuffer& operator=(ManagedBuffer&&) noexcept;

    [[nodiscard]] std::byte* GetMappedMemory()
    {
      return static_cast<std::byte*>(buffer_.GetMappedMemory());
    }

    [[nodiscard]] Fvog::Device::DescriptorInfo::ResourceHandle GetResourceHandle()
    {
      return buffer_.GetResourceHandle();
    }
    
    [[nodiscard]] Alloc Allocate(VkDeviceSize size, VkDeviceSize alignment);

    [[nodiscard]] Buffer& GetBuffer() noexcept
    {
      return buffer_;
    }

    [[nodiscard]] VmaVirtualBlock GetVirtualBlock() const
    {
      return allocator;
    }

  private:
    Buffer buffer_;
    VmaVirtualBlock allocator{};
  };

  // Stores data contiguously, but without stable order, in a tightly packed array.
  // Ranges are deleted with a variant of the copy-pop pattern.
  class ContiguousManagedBuffer
  {
  public:
    // No RAII hehe
    struct Alloc
    {
      size_t offset;
      size_t size;
    };

    explicit ContiguousManagedBuffer(size_t bufferSize, std::string name = {});

    [[nodiscard]] Alloc Allocate(size_t size);
    void Free(Alloc allocation, VkCommandBuffer commandBuffer);

    [[nodiscard]] Buffer& GetBuffer() noexcept
    {
      return buffer_;
    }

    [[nodiscard]] size_t GetCurrentSize() const noexcept
    {
      return currentSize_;
    }

    [[nodiscard]] Fvog::Device::DescriptorInfo::ResourceHandle GetResourceHandle()
    {
      return buffer_.GetResourceHandle();
    }

  private:
    Buffer buffer_;
    size_t currentSize_ = 0;
  };

  // A CPU and GPU buffer pair, with CPU writes replicated on the GPU.
  // Can be used to make index-based data structures "just work" on the GPU.
  class ReplicatedBuffer
  {
  public:
    explicit ReplicatedBuffer(size_t bufferSize, std::string name = {});

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

    void FlushWritesToGPU(VkCommandBuffer cmd);

    auto GetAllocator() const
    {
      return allocator_;
    }

    Buffer& GetGpuBuffer()
    {
      return gpuBuffer_;
    }

    size_t SizeBytes() const
    {
      return gpuBuffer_.SizeBytes();
    }

  private:
    static constexpr size_t PAGE_SIZE = 1024;
    Buffer gpuBuffer_;
    std::unique_ptr<std::byte[]> cpuBuffer_;
    VmaVirtualBlock allocator_;
    std::unordered_set<uint32_t> dirtyPages_;
  };
}
