#include "VirtualShadowMaps.h"

#include "shaders/Config.shared.h"

#include "../RendererUtilities.h"

#include <Fvog/Buffer2.h>
#include <Fvog/Rendering2.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <bit>

namespace Techniques::VirtualShadowMaps
{
  namespace
  {
    Fvog::ComputePipeline CreateResetPageVisibilityPipeline(Fvog::Device& device)
    {
      auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmResetPageVisibility.comp.glsl");

      return Fvog::ComputePipeline(device, {
        .shader = &comp,
      });
    }

    Fvog::ComputePipeline CreateMarkVisiblePipeline(Fvog::Device& device)
    {
      auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmMarkVisiblePages.comp.glsl");

      return Fvog::ComputePipeline(device, {
        .shader = &comp,
      });
    }

    Fvog::ComputePipeline CreateAllocatorPipeline(Fvog::Device& device)
    {
      auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmAllocatePages.comp.glsl");

      return Fvog::ComputePipeline(device, {
        .shader = &comp,
      });
    }

    Fvog::ComputePipeline CreateListDirtyPagesPipeline(Fvog::Device& device)
    {
      auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmListDirtyPages.comp.glsl");

      return Fvog::ComputePipeline(device, {
        .shader = &comp,
      });
    }

    Fvog::ComputePipeline CreateClearDirtyPagesPipeline(Fvog::Device& device)
    {
      auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmClearDirtyPages.comp.glsl");

      return Fvog::ComputePipeline(device, {
        .shader = &comp,
      });
    }

    Fvog::ComputePipeline CreateFreeNonVisiblePagesPipeline(Fvog::Device& device)
    {
      auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmFreeNonVisiblePages.comp.glsl");

      return Fvog::ComputePipeline(device, {
        .shader = &comp,
      });
    }

    Fvog::ComputePipeline CreateReduceVsmHzbPipeline(Fvog::Device& device)
    {
      auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmReduceBitmaskHzb.comp.glsl");

      return Fvog::ComputePipeline(device, {
        .shader = &comp,
      });
    }
  }

  Context::Context(Fvog::Device& device, const CreateInfo& createInfo)
    : device_(&device),
      freeLayersBitmask_(size_t(std::ceil(float(createInfo.maxVsms) / 32)), 0xFFFFFFu),
      pageTables_(device,
        Fvog::TextureCreateInfo{
          .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
          .format = Fvog::Format::R32_UINT, // Ideally 16 bits, but image atomics are limited to 32-bit integer types
          .extent = Fvog::Extent3D{pageTableSize, pageTableSize, 1},
          .mipLevels = pageTableMipLevels,
          .arrayLayers = ((createInfo.maxVsms + 31) / 32) * 32, // Round up to the nearest multiple of 32 so we don't have any overflowing bits
        },
        "VSM Page Tables"),
      vsmBitmaskHzb_(device,
        Fvog::TextureCreateInfo{
          .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
          .format = Fvog::Format::R8_UINT,
          .extent = pageTables_.GetCreateInfo().extent,
          .mipLevels = pageTableMipLevels,
          .arrayLayers = pageTables_.GetCreateInfo().arrayLayers,
        },
        "VSM Bitmask HZB"),
      physicalPages_(device,
        Fvog::TextureCreateInfo{
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = Fvog::Format::R32_SFLOAT,
          .extent = {(uint32_t)std::ceil(std::sqrt(createInfo.numPages)) * pageSize, (uint32_t)std::ceil(std::sqrt(createInfo.numPages)) * pageSize, 1},
          .mipLevels = 1,
          .arrayLayers = 1,
        },
        "VSM Physical Pages"),
      physicalPagesUint_(physicalPages_.CreateFormatView(Fvog::Format::R32_UINT)),
      physicalPagesOverdrawHeatmap_(Fvog::CreateTexture2D(device,
        {physicalPages_.GetCreateInfo().extent.width, physicalPages_.GetCreateInfo().extent.height},
        Fvog::Format::R32_UINT,
        Fvog::TextureUsage::GENERAL,
        "VSM Physical Pages Heatmap")),
      visiblePagesBitmask_(device, {sizeof(uint32_t) * createInfo.numPages / 32}, "Visible Pages Bitmask"),
      uniformBuffer_(device, {}, "VSM Uniforms"),
      pageAllocRequests_(device, {sizeof(PageAllocRequest) * (createInfo.numPages + 1)}, "Page Alloc Requests"),
      pagesToClear_(device, {sizeof(uint32_t) + sizeof(uint32_t) * createInfo.numPages}, "Pages to Clear"),
      pageClearDispatchParams_(device, {}, "Page Clear Dispatch Params"),
      resetPageVisibility_(CreateResetPageVisibilityPipeline(device)),
      allocatePages_(CreateAllocatorPipeline(device)),
      markVisiblePages_(CreateMarkVisiblePipeline(device)),
      listDirtyPages_(CreateListDirtyPagesPipeline(device)),
      clearDirtyPages_(CreateClearDirtyPagesPipeline(device)),
      freeNonVisiblePages_(CreateFreeNonVisiblePagesPipeline(device)),
      // reducePhysicalPages_(CreateReducePhysicalPipeline()),
      // reduceVirtualPages_(CreateReduceVirtualPipeline()),
      reduceVsmHzb_(CreateReduceVsmHzbPipeline(device))
  {
    device.ImmediateSubmit([this](VkCommandBuffer cmd) {
      auto ctx = Fvog::Context(*device_, cmd);
      // Clear every page mapping to zero
      ctx.ImageBarrierDiscard(pageTables_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      ctx.ImageBarrierDiscard(physicalPages_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      ctx.ImageBarrierDiscard(physicalPagesOverdrawHeatmap_, VK_IMAGE_LAYOUT_GENERAL);
      ctx.ClearTexture(pageTables_, {.levelCount = pageTableMipLevels});
      ctx.ClearTexture(physicalPages_, {});
      visiblePagesBitmask_.FillData(cmd);
      ctx.TeenyBufferUpdate(pageClearDispatchParams_, Fvog::DispatchIndirectCommand{pageSize / 8, pageSize / 8, 0});
    });
  }

  /*
   *  OUT:
   *    uniformBuffer_
   */
  void Context::UpdateUniforms(VkCommandBuffer cmd, const VsmGlobalUniforms& uniforms)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    ctx.TeenyBufferUpdate(uniformBuffer_, uniforms);
  }

  std::optional<uint32_t> Context::AllocateLayer()
  {
    for (size_t i = 0; i < freeLayersBitmask_.size(); i++)
    {
      if (auto bit = std::countr_zero(freeLayersBitmask_[i]); bit < 32)
      {
        freeLayersBitmask_[i] &= ~(1 << bit);
        return static_cast<uint32_t>(i * 32 + bit);
      }
    }
    
    return std::nullopt;
  }

  void Context::FreeLayer(uint32_t layerIndex)
  {
    assert(layerIndex < pageTables_.GetCreateInfo().arrayLayers);

    size_t i = layerIndex / 32;
    int bit = layerIndex % 32;
    freeLayersBitmask_[i] |= 1 << bit;
  }

  /*
   *  OUT:
   *    physicalPagesOverdrawHeatmap (if VSM_RENDER_OVERDRAW is enabled)
   *
   *  INOUT:
   *    pageTables_
   */
  // TODO: this should be per-VSM, since not every VSM is updated (and therefore requires a visibility reset) every frame
  // Should make it take a list of VSM indices to reset
  void Context::ResetPageVisibility(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Reset Page Visibility");
    
    ctx.ImageBarrier(pageTables_, VK_IMAGE_LAYOUT_GENERAL);

#if VSM_RENDER_OVERDRAW
    // This just needs to happen sometime before the shadow maps are rendered
    ctx.ImageBarrierDiscard(physicalPagesOverdrawHeatmap_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    ctx.ClearTexture(physicalPagesOverdrawHeatmap_, {});
    ctx.ImageBarrier(physicalPagesOverdrawHeatmap_, VK_IMAGE_LAYOUT_GENERAL);
#endif

    ctx.BindComputePipeline(resetPageVisibility_);

    auto pushConstants = GetPushConstants();

    for (uint32_t i = 0; i < pageTables_.GetCreateInfo().mipLevels; i++)
    {
      pushConstants.pageTablesIndex = pageTables_.CreateSingleMipView(i).GetStorageResourceHandle().index;
      ctx.SetPushConstants(pushConstants);
      auto extent = pageTables_.GetCreateInfo().extent / (1 << i);
      extent.depth = pageTables_.GetCreateInfo().arrayLayers;
      ctx.DispatchInvocations(extent);
    }
  }

  /*
   *  INOUT:
   *    pageTables_
   */
  // TODO: See TODO for ResetPageVisibility (should allow batching updates instead of operating on whole VSM every time)
  void Context::FreeNonVisiblePages(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Free Non-Visible Pages");

    ctx.Barrier(); // Appease sync val
    ctx.ImageBarrier(pageTables_, VK_IMAGE_LAYOUT_GENERAL);

    ctx.BindComputePipeline(freeNonVisiblePages_);

    auto pushConstants = GetPushConstants();

    for (uint32_t i = 0; i < pageTables_.GetCreateInfo().mipLevels; i++)
    {
      pushConstants.pageTablesIndex = pageTables_.CreateSingleMipView(i).GetStorageResourceHandle().index;
      ctx.SetPushConstants(pushConstants);
      auto extent = pageTables_.GetCreateInfo().extent / (1 << i);
      extent.depth = pageTables_.GetCreateInfo().arrayLayers;
      ctx.DispatchInvocations(extent);
    }
  }

  /*
   *  IN:
   *    pageTables_
   *
   *  INOUT:
   *    pageAllocRequests_
   *    visiblePagesBitmask_
   */
  void Context::AllocateRequestedPages(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Allocate Pages");

    auto pushConstants = GetPushConstants();
    ctx.SetPushConstants(pushConstants);

    ctx.BindComputePipeline(allocatePages_);
    ctx.Barrier();
    ctx.Dispatch(1, 1, 1); // Only 1-32 threads will allocate
  }

  /*
   *  ListDirtyPages
   *  IN:
   *    pageTables_
   *
   *  OUT:
   *    pageClearDispatchParams_
   *    pagesToClear_ (dirty pages)
   *
   *
   *  ClearDirtyPages
   *  IN:
   *    pageClearDispatchParams_
   *    pagesToClear_ (dirty pages)
   *
   *  INOUT:
   *    physicalPages_
   */
  void Context::ClearDirtyPages(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Enqueue and Clear Dirty Pages");

    ctx.Barrier();

    pageClearDispatchParams_.FillData(cmd, {.offset = offsetof(Fvog::DispatchIndirectCommand, groupCountZ), .size = sizeof(uint32_t)});
    pagesToClear_.FillData(cmd, {.offset = 0, .size = sizeof(uint32_t)});

    ctx.Barrier();

    ctx.ImageBarrier(pageTables_, VK_IMAGE_LAYOUT_GENERAL);
    ctx.ImageBarrier(physicalPages_, VK_IMAGE_LAYOUT_GENERAL);

    // Experimental
    //ctx.Barriers({{
    //  Fvog::GlobalBarrier{},
    //  Fvog::ImageBarrier{
    //    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
    //    .image = &pageTables_,
    //  },
    //  Fvog::ImageBarrier{
    //    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
    //    .image = &physicalPages_,
    //  }
    //}});

    auto pushConstants = GetPushConstants();
    ctx.SetPushConstants(pushConstants);

    // TODO: make the first half of this (create dirty page list) more efficient by only considering updated VSMs
    ctx.BindComputePipeline(listDirtyPages_);
    ctx.DispatchInvocations(pageTables_.GetCreateInfo().extent.width, pageTables_.GetCreateInfo().extent.height, pageTables_.GetCreateInfo().arrayLayers);
    
    ctx.BindComputePipeline(clearDirtyPages_);
    ctx.Barrier();
    ctx.DispatchIndirect(pageClearDispatchParams_);
  }

  VsmPushConstants Context::GetPushConstants()
  {
    return {
      .globalUniformsIndex = 0,
      .pageTablesIndex = pageTables_.ImageView().GetStorageResourceHandle().index,
      .physicalPagesIndex = physicalPages_.ImageView().GetStorageResourceHandle().index,
      .vsmBitmaskHzbIndex = vsmBitmaskHzb_.ImageView().GetSampledResourceHandle().index,
      .vsmUniformsBufferIndex = uniformBuffer_.GetResourceHandle().index,
      .dirtyPageListBufferIndex = pagesToClear_.GetResourceHandle().index,
      .clipmapUniformsBufferIndex = 0, // DirectionalVirtualShadowMap::clipmapUniformsBuffer_
      .nearestSamplerIndex = 0, // Set by caller
      .pageClearDispatchIndex = pageClearDispatchParams_.GetResourceHandle().index,
      .gDepthIndex = 0, // Set by caller
      .srcVsmBitmaskHzbIndex = 0, // GenerateBitmaskHzb
      .dstVsmBitmaskHzbIndex = 0, // GenerateBitmaskHzb
      .currentPass = 0,           // GenerateBitmaskHzb
      .meshletDataIndex = 0,     // Set by renderer
      .materialsIndex = 0,       // Set by renderer
      .materialSamplerIndex = 0, // Set by renderer
      .clipmapLod = 0,           // Set by renderer
      .allocRequestsIndex = pageAllocRequests_.GetResourceHandle().index,
      .visiblePagesBitmaskIndex = visiblePagesBitmask_.GetResourceHandle().index,
      .physicalPagesUintIndex = physicalPagesUint_.GetStorageResourceHandle().index,
      .physicalPagesOverdrawIndex = physicalPagesOverdrawHeatmap_.ImageView().GetSampledResourceHandle().index,
    };
  }

  DirectionalVirtualShadowMap::DirectionalVirtualShadowMap(const CreateInfo& createInfo)
    : context_(createInfo.context),
      numClipmaps_(createInfo.numClipmaps),
      virtualExtent_(createInfo.virtualExtent),
      clipmapUniformsBuffer_(*createInfo.context.device_, {}, "Directional VSM Uniforms")
  {
    uniforms_.numClipmaps = createInfo.numClipmaps;

    for (uint32_t i = 0; i < uniforms_.numClipmaps; i++)
    {
      uniforms_.clipmapTableIndices[i] = context_.AllocateLayer().value();
    }
  }

  DirectionalVirtualShadowMap::~DirectionalVirtualShadowMap()
  {
    for (auto& vsm : uniforms_.clipmapTableIndices)
    {
      context_.FreeLayer(vsm);
    }
  }

  /*
   *  IN:
   *    g-buffer depth
   *    global uniforms
   *    VSM uniforms
   *    clipmap uniforms
   *
   *  INOUT:
   *    visiblePagesBitmask_
   *    pageTables_
   *    pageAllocRequests_
   */
  void DirectionalVirtualShadowMap::MarkVisiblePages(VkCommandBuffer cmd, Fvog::Texture& gDepth, Fvog::Buffer& globalUniforms)
  {
    auto ctx = Fvog::Context(*context_.device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Mark Visible Pages");

    ctx.Barrier();

    context_.visiblePagesBitmask_.FillData(cmd);

    ctx.Barrier();
    ctx.ImageBarrier(gDepth, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(context_.pageTables_, VK_IMAGE_LAYOUT_GENERAL);

    ctx.BindComputePipeline(context_.markVisiblePages_);

    auto pushConstants = context_.GetPushConstants();
    pushConstants.gDepthIndex = gDepth.ImageView().GetSampledResourceHandle().index;
    pushConstants.globalUniformsIndex = globalUniforms.GetResourceHandle().index;
    pushConstants.clipmapUniformsBufferIndex = clipmapUniformsBuffer_.GetResourceHandle().index;
    ctx.SetPushConstants(pushConstants);
    
    ctx.DispatchInvocations(gDepth.GetCreateInfo().extent);
  }


  /*
   *  Base dependencies: UpdateOffset
   *
   *  OUT:
   *    pageTables_
   */
  void DirectionalVirtualShadowMap::UpdateExpensive(VkCommandBuffer cmd, glm::vec3 worldOffset, glm::vec3 direction, float firstClipmapWidth, float projectionZLength)
  {
    auto ctx = Fvog::Context(*context_.device_, cmd);

    ctx.Barrier();

    const auto sideLength = firstClipmapWidth / virtualExtent_;
    uniforms_.firstClipmapTexelLength = sideLength;
    uniforms_.projectionZLength = projectionZLength;

    auto up = glm::vec3(0, 1, 0);
    if (1.0f - glm::abs(glm::dot(direction, up)) < 1e-4f)
    {
      up = glm::vec3(0, 0, 1);
    }
    
    stableViewMatrix = glm::lookAt(direction, glm::vec3(0), up);

    // Invalidate all clipmaps (clearing to 0 marks pages as not backed, not dirty, and not visible)
    ctx.ClearTexture(context_.pageTables_, {});

    for (uint32_t i = 0; i < uniforms_.numClipmaps; i++)
    {
      const auto width = firstClipmapWidth * (1 << i) / 2.0f;
      // TODO: increase Z range for higher clipmaps (or for all?)
      stableProjections[i] = glm::orthoZO(-width, width, -width, width, -projectionZLength / 2.0f, projectionZLength / 2.0f);
    }

    this->UpdateOffset(cmd, worldOffset);
  }

  /*
   *  OUT:
   *    clipmapUniformsBuffer_
   */
  void DirectionalVirtualShadowMap::UpdateOffset(VkCommandBuffer cmd, glm::vec3 worldOffset)
  {
    auto ctx = Fvog::Context(*context_.device_, cmd);

    ctx.Barrier();

    for (uint32_t i = 0; i < uniforms_.numClipmaps; i++)
    {
      // Find the offset from the un-translated view matrix
      uniforms_.clipmapStableViewProjections[i] = stableProjections[i] * stableViewMatrix;
      const auto clip = stableProjections[i] * stableViewMatrix * glm::vec4(worldOffset, 1);
      const auto ndc = clip / clip.w;
      const auto uv = glm::vec2(ndc) * 0.5f; // Don't add the 0.5, since we want the center to be 0
      const auto pageOffset = glm::ivec2(uv * glm::vec2(context_.pageTables_.GetCreateInfo().extent.width, context_.pageTables_.GetCreateInfo().extent.height));
      //const auto oldOrigin = uniforms_.clipmapOrigins[i];
      uniforms_.clipmapOrigins[i] = pageOffset;

      const auto ndcShift = 2.0f * glm::vec2((float)pageOffset.x / context_.pageTables_.GetCreateInfo().extent.width,
                                             (float)pageOffset.y / context_.pageTables_.GetCreateInfo().extent.height);
      
      // Shift rendering projection matrix by opposite of page offset in clip space, then apply *only* that shift to the view matrix
      const auto shiftedProjection = glm::translate(glm::mat4(1), glm::vec3(-ndcShift, 0)) * stableProjections[i];
      viewMatrices[i] = glm::inverse(stableProjections[i]) * shiftedProjection * stableViewMatrix;

      //uniforms_.clipmapOrigins[i] = {};
      //viewMatrices[i] = stableViewMatrix;
    }
    
    ctx.TeenyBufferUpdate(clipmapUniformsBuffer_, uniforms_);
  }

  /*
   *  IN:
   *    pageTables_ (first pass)
   *
   *  OUT:
   *    vsmBitmaskHzb_
   */
  void DirectionalVirtualShadowMap::GenerateBitmaskHzb(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*context_.device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Generate Bitmap HZB");

    ctx.ImageBarrier(context_.pageTables_, VK_IMAGE_LAYOUT_GENERAL);

    // TODO: only reduce necessary VSMs
    ctx.BindComputePipeline(context_.reduceVsmHzb_);

    auto pushConstants = context_.GetPushConstants();

    for (uint32_t currentPass = 0; currentPass <= (uint32_t)std::log2(pageTableSize); currentPass++)
    {
      ctx.Barrier();

      if (currentPass > 0)
      {
        pushConstants.srcVsmBitmaskHzbIndex = context_.vsmBitmaskHzb_.CreateSingleMipView(currentPass - 1).GetStorageResourceHandle().index;
      }
      
      pushConstants.dstVsmBitmaskHzbIndex = context_.vsmBitmaskHzb_.CreateSingleMipView(currentPass).GetStorageResourceHandle().index;

      pushConstants.currentPass = currentPass;

      auto invocations = Fvog::Extent3D{
        context_.vsmBitmaskHzb_.GetCreateInfo().extent.width,
        context_.vsmBitmaskHzb_.GetCreateInfo().extent.height,
        1,
      };
      invocations = invocations >> currentPass;
      invocations.depth = context_.vsmBitmaskHzb_.GetCreateInfo().arrayLayers;
      ctx.SetPushConstants(pushConstants);
      ctx.DispatchInvocations(invocations);
    }
  }
} // namespace Techniques