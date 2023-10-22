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
  //inline constexpr uint32_t maxExtent = 16384; // 2^14
  inline constexpr uint32_t maxExtent = 4096;
  inline constexpr uint32_t minExtent = pageSize;
  inline constexpr uint32_t pageTableSize = maxExtent / pageSize;
  inline const uint32_t pageTableMipLevels = 1 + static_cast<uint32_t>(std::log2(pageTableSize));
  inline constexpr uint32_t MAX_CLIPMAPS = 32;

  enum class DebugFlag
  {
    VSM_HZB_PHYSICAL_RETURN_ONE = 1 << 0,
    VSM_HZB_VIRTUAL_RETURN_ONE = 1 << 1,
    VSM_FORCE_DIRTY_VISIBLE_PAGES = 1 << 2,
  };

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
      uint32_t debugFlags{};
      char _padding[8]{};
    };

    void UpdateUniforms(const VsmGlobalUniforms& uniforms);

    /// TABLE MAPPINGS
    // If there is a free layer, returns its index, otherwise returns nothing
    [[nodiscard]] std::optional<uint32_t> AllocateLayer();
    void FreeLayer(uint32_t layerIndex);
    void ResetPageVisibility();

    void FreeNonVisiblePages();

    /// ALLOCATOR
    void AllocateRequestedPages();

    void ClearDirtyPages();

    void BindResourcesForCulling();

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
  public:
    Fwog::Texture pageTables_;
  private:

    // Reduced depth version of the page table.
    // Level 0 of this texture holds the reduction of the final mip of the corresponding physical pages
    // (which should be 2x2).
    // Un-backed pages have NEAR depth (0) so that objects which only overlap them are culled.
    //Fwog::Texture pageTablesHzb_;

  public:
    Fwog::Texture vsmBitmaskHzb_;
    // Physical memory used to back various VSMs
    Fwog::Texture physicalPages_;
    Fwog::TextureView physicalPagesUint_; // For doing atomic ops
  private:

    // Bitmask indicating whether each page is visible this frame
    // Only non-visible pages should be evicted
    Fwog::Buffer visiblePagesBitmask_;

    // Min-2-tree with the time (frame number) that each page was last seen
    // TODO: upgrade to a subgroup-optimized tree to speed up traversal, if needed
    Fwog::Buffer pageVisibleTimeTree_;

    /// BUFFERS
  public:
    Fwog::TypedBuffer<VsmGlobalUniforms> uniformBuffer_;
  private:

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
    Fwog::ComputePipeline freeNonVisiblePages_;
    //Fwog::ComputePipeline reducePhysicalPages_;
    //Fwog::ComputePipeline reduceVirtualPages_;
    Fwog::ComputePipeline reduceVsmHzb_;
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
    void UpdateExpensive(glm::vec3 worldOffset, glm::vec3 direction, float firstClipmapWidth, float projectionZLength);

    // Cheap, call every frame
    void UpdateOffset(glm::vec3 worldOffset);

    void BindResourcesForDrawing();

    void GenerateBitmaskHzb();

    [[nodiscard]] std::span<const glm::mat4> GetProjections() const noexcept
    {
      return {stableProjections.data(), numClipmaps_};
    }

    [[nodiscard]] std::span<const glm::mat4> GetViewMatrices() const noexcept
    {
      return {viewMatrices.data(), numClipmaps_};
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

    [[nodiscard]] glm::mat4 GetStableViewMatrix() const noexcept
    {
      return stableViewMatrix;
    }

  private:
    struct MarkVisiblePagesDirectionalUniforms
    {
      std::array<glm::mat4, MAX_CLIPMAPS> clipmapStableViewProjections;
      std::array<uint32_t, MAX_CLIPMAPS> clipmapTableIndices;
      std::array<glm::ivec2, MAX_CLIPMAPS> clipmapOrigins;
      uint32_t numClipmaps;
      float firstClipmapTexelLength;
      float projectionZLength;
    };

    Context& context_;
    uint32_t numClipmaps_;
    uint32_t virtualExtent_;
    MarkVisiblePagesDirectionalUniforms uniforms_{};

    // View matrix with rotation, but no translation component
    glm::mat4 stableViewMatrix{};

    // Per-clipmap view matrices that are offset by an amount proportional to the viewer's position.
    // Used for rendering the shadow map.
    std::array<glm::mat4, MAX_CLIPMAPS> viewMatrices{};

    // Subsequent projections are 2x larger than the previous on X and Y
    std::array<glm::mat4, MAX_CLIPMAPS> stableProjections{};

    Fwog::TypedBuffer<MarkVisiblePagesDirectionalUniforms> uniformBuffer_;
  };
} // namespace Techniques::VirtualShadowMaps
