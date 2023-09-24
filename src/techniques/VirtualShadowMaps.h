#pragma once
#include <Fwog/Buffer.h>
#include <Fwog/Pipeline.h>
#include <Fwog/Texture.h>

#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace Techniques::VirtualShadowMaps
{
  inline constexpr uint32_t pageSize = 128;
  inline constexpr uint32_t maxExtent = 16384; // 2^14
  inline constexpr uint32_t minExtent = pageSize;
  inline const uint32_t mipLevels = 1 + static_cast<uint32_t>(std::log2(maxExtent / pageSize));
  inline constexpr uint32_t MAX_CLIPMAPS = 32;

  class Context
  {
  public:
    struct CreateInfo
    {
      uint32_t maxVsms{};
      Fwog::Extent2D pageSize{};
      uint32_t numPages{};
    };

    explicit Context(const CreateInfo& createInfo);

    struct VsmGlobalUniforms
    {
      float lodBias{};
      char _padding[12]{};
    };

    void UpdateUniforms(const VsmGlobalUniforms& uniforms);

    /// TABLE MAPPINGS
    // If there is a free layer, returns its index, otherwise returns nothing
    [[nodiscard]] std::optional<uint32_t> AllocateLayer();
    void FreeLayer(uint32_t layerIndex);
    void ResetPageVisibility();

    /// ALLOCATOR
    void AllocateRequestedPages();

    void ClearDirtyPages();

  private:
    friend class DirectionalVirtualShadowMap;

    // Bitmask indicating which layers of the page mappings array are free
    std::vector<uint32_t> freeLayersBitmask_;
    
    // A texture that maps virtual pages to physical memory
    // Each layer and level of the array represents a single VSM
    // Level 0 = 16384x16384 (max)
    // Level 8 = 128x128 (min)
    // Each layer indicates whether the page is visible and whether it's dirty in addition to mapping to a physical page
    // Bit 0: is this page visible?
    // Bit 1: is this page dirty (object within it moved or the light source itself moved)?
    // Bit 2: is this page allocated? This could be implemented as a special page address
    // Bits 3-15: reserved
    // Bits 16-31: page address from 0 to 2^16-1
    Fwog::Texture pageTables_;

    // Physical memory used to back various VSMs
    Fwog::Texture pages_;

    // Bitmask indicating whether each page is visible this frame
    // Only non-visible pages should be evicted
    Fwog::Buffer visiblePagesBitmask_;

    // Min-2-tree with the time (frame number) that each page was last seen
    // TODO: upgrade to a subgroup-optimized tree to speed up traversal, if needed
    Fwog::Buffer pageVisibleTimeTree_;

    /// BUFFERS
    Fwog::TypedBuffer<VsmGlobalUniforms> uniformBuffer_;

    struct PageAllocRequest
    {
      // Address of the requester
      glm::ivec3 pageTableAddress;

      // Unused until local lights are supported
      uint32_t pageTableLevel;
    };
    Fwog::Buffer pageAllocRequests_;

    Fwog::Buffer pagesToClear_;
    Fwog::TypedBuffer<Fwog::DispatchIndirectCommand> pageClearDispatchParams_;

    /// PIPELINES
    Fwog::ComputePipeline resetPageVisibility_;
    Fwog::ComputePipeline allocatePages_;
    Fwog::ComputePipeline markVisiblePages_;
    Fwog::ComputePipeline listDirtyPages_;
    Fwog::ComputePipeline clearDirtyPages_;
  };

  class DirectionalVirtualShadowMap
  {
  public:
    struct CreateInfo
    {
      Context& context;
      uint32_t virtualExtent{};
      uint32_t numClipmaps{};
    };

    explicit DirectionalVirtualShadowMap(const CreateInfo& createInfo);
    ~DirectionalVirtualShadowMap();

    DirectionalVirtualShadowMap(const DirectionalVirtualShadowMap&) = delete;
    DirectionalVirtualShadowMap(DirectionalVirtualShadowMap&&) noexcept = delete;
    DirectionalVirtualShadowMap& operator=(const DirectionalVirtualShadowMap&) = delete;
    DirectionalVirtualShadowMap& operator=(DirectionalVirtualShadowMap&&) noexcept = delete;

    // void EnqueueDirtyPages();

    // Analyze the g-buffer depth to determine which pages of the VSMs are visible
    void MarkVisiblePages(const Fwog::Texture& gDepth, const Fwog::Buffer& globalUniforms);

    // Invalidates ALL pages in the referenced VSMs.
    // Call only when the light itself changes, since this invalidates ALL pages
    void UpdateExpensive(const glm::mat4& viewMat, float firstClipmapWidth);

    // Cheap, call every frame
    void UpdateOffset(const glm::mat4& viewMatNoTranslation, glm::vec3 worldOffset, glm::vec3 dir);

    void BindResourcesForDrawing();

    [[nodiscard]] std::span<const glm::mat4> GetProjections() const noexcept
    {
      return {clipmapProjections.data(), numClipmaps_};
    }

    [[nodiscard]] std::span<const glm::mat4> GetViews() const noexcept
    {
      return {clipmapViews.data(), numClipmaps_};
    }

    [[nodiscard]] std::span<const uint32_t> GetClipmapTableIndices() const noexcept
    {
      return {uniforms_.clipmapTableIndices.data(), numClipmaps_};
    }

    [[nodiscard]] Fwog::Extent2D GetExtent() const noexcept
    {
      return {virtualExtent_, virtualExtent_};
    }

    [[nodiscard]] uint32_t NumClipmaps() const noexcept
    {
      return numClipmaps_;
    }

  private:
    struct MarkVisiblePagesDirectionalUniforms
    {
      std::array<glm::mat4, MAX_CLIPMAPS> clipmapViewProjections;
      std::array<uint32_t, MAX_CLIPMAPS> clipmapTableIndices;
      std::array<glm::ivec2, MAX_CLIPMAPS> clipmapOrigins;
      uint32_t numClipmaps;
      float firstClipmapTexelLength;
    };

    Context& context_;
    uint32_t numClipmaps_;
    uint32_t virtualExtent_;
    MarkVisiblePagesDirectionalUniforms uniforms_{};

    // Subsequent projections are 2x larger than the previous on X and Y
    std::array<glm::mat4, MAX_CLIPMAPS> clipmapProjections{};

    // Shifted view matrix of each clipmap
    std::array<glm::mat4, MAX_CLIPMAPS> clipmapViews{};

    Fwog::TypedBuffer<MarkVisiblePagesDirectionalUniforms> uniformBuffer_;
  };
} // namespace Techniques::VirtualShadowMaps
