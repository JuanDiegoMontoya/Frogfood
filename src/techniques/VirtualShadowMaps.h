#pragma once
#include "Fvog/Buffer2.h"
#include "Fvog/Pipeline2.h"
#include "Fvog/Texture2.h"

#include "shaders/shadows/vsm/VsmCommon.h.glsl"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace Fvog
{
  class Device;
}

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
    VSM_HZB_FORCE_SUCCESS = 1 << 0,
    VSM_FORCE_DIRTY_VISIBLE_PAGES = 1 << 1,
  };

  struct Bitmap
  {
    uint8_t data[pageTableSize][pageTableSize];
  };

  class Context
  {
  public:
    struct CreateInfo
    {
      uint32_t maxVsms{};
      Fvog::Extent2D pageSize{};
      uint32_t numPages{};
    };

    explicit Context(Fvog::Device& device, const CreateInfo& createInfo);

    struct VsmGlobalUniforms
    {
      float lodBias{};
      uint32_t debugFlags{};
      char _padding[8]{};
    };

    void UpdateUniforms(VkCommandBuffer cmd, const VsmGlobalUniforms& uniforms);

    /// TABLE MAPPINGS
    // If there is a free layer, returns its index, otherwise returns nothing
    [[nodiscard]] std::optional<uint32_t> AllocateLayer();
    void FreeLayer(uint32_t layerIndex);
    void ResetPageVisibility(VkCommandBuffer cmd);

    void FreeNonVisiblePages(VkCommandBuffer cmd);

    /// ALLOCATOR
    void AllocateRequestedPages(VkCommandBuffer cmd);

    void ClearDirtyPages(VkCommandBuffer cmd);

    //void BindResourcesForCulling(VkCommandBuffer cmd);

    VsmPushConstants GetPushConstants();

    Fvog::Device* device_{};

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
    Fvog::Texture pageTables_;
  private:

  public:
    Fvog::Texture vsmBitmaskHzb_;
    // Physical memory used to back various VSMs
    Fvog::Texture physicalPages_;
    Fvog::TextureView physicalPagesUint_; // For doing atomic ops
    Fvog::Texture physicalPagesOverdrawHeatmap_; // Integer texture, used for debugging
  private:

    // Bitmask indicating whether each page is visible this frame
    // Only non-visible pages should be evicted
    Fvog::Buffer visiblePagesBitmask_;

    /// BUFFERS
  public:
    Fvog::TypedBuffer<VsmGlobalUniforms> uniformBuffer_;
  private:

    struct PageAllocRequest
    {
      // Address of the requester
      glm::ivec3 pageTableAddress;

      // Unused until local lights are supported
      uint32_t pageTableLevel;
    };
    Fvog::Buffer pageAllocRequests_;

    Fvog::Buffer pagesToClear_;
    Fvog::TypedBuffer<Fvog::DispatchIndirectCommand> pageClearDispatchParams_;

    /// PIPELINES
    Fvog::ComputePipeline resetPageVisibility_;
    Fvog::ComputePipeline allocatePages_;
    Fvog::ComputePipeline markVisiblePages_;
    Fvog::ComputePipeline listDirtyPages_;
    Fvog::ComputePipeline clearDirtyPages_;
    Fvog::ComputePipeline freeNonVisiblePages_;
    //Fwog::ComputePipeline reducePhysicalPages_;
    //Fwog::ComputePipeline reduceVirtualPages_;
    Fvog::ComputePipeline reduceVsmHzb_;
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
    void MarkVisiblePages(VkCommandBuffer cmd, Fvog::Texture& gDepth, Fvog::Buffer& globalUniforms);

    // Invalidates ALL pages in the referenced VSMs.
    // Call only when the light itself changes, since this invalidates ALL pages
    void UpdateExpensive(VkCommandBuffer cmd, glm::vec3 worldOffset, glm::vec3 direction, float firstClipmapWidth, float projectionZLength);

    // Cheap, call every frame
    void UpdateOffset(VkCommandBuffer cmd, glm::vec3 worldOffset);

    //void BindResourcesForDrawing();

    void GenerateBitmaskHzb(VkCommandBuffer cmd);

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

    [[nodiscard]] Fvog::Extent2D GetExtent() const noexcept
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
    struct ClipmapUniforms
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
    ClipmapUniforms uniforms_{};

    // View matrix with rotation, but no translation component
    glm::mat4 stableViewMatrix{};

    // Per-clipmap view matrices that are offset by an amount proportional to the viewer's position.
    // Used for rendering the shadow map.
    std::array<glm::mat4, MAX_CLIPMAPS> viewMatrices{};

    // Subsequent projections are 2x larger than the previous on X and Y
    std::array<glm::mat4, MAX_CLIPMAPS> stableProjections{};

  public:
    Fvog::TypedBuffer<ClipmapUniforms> clipmapUniformsBuffer_;
  };
} // namespace Techniques::VirtualShadowMaps
