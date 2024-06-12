#include "VirtualShadowMaps.h"

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
      visiblePagesBitmask_(device, {sizeof(uint32_t) * createInfo.numPages / 32}, "Visible Pages Bitmask"),
      pageVisibleTimeTree_(device, {sizeof(uint32_t) * createInfo.numPages * 2}, "Page Visible Time Tree (remove)"),
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
      ctx.ClearTexture(pageTables_, {.levelCount = pageTableMipLevels});
      ctx.ClearTexture(physicalPages_, {});
      visiblePagesBitmask_.FillData(cmd);
      pageVisibleTimeTree_.FillData(cmd);
      ctx.TeenyBufferUpdate(pageClearDispatchParams_, Fvog::DispatchIndirectCommand{pageSize / 8, pageSize / 8, 0});
    });
  }

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

  // TODO: this should be per-VSM, since not every VSM is updated (and therefore requires a visibility reset) every frame
  // Should make it take a list of VSM indices to reset
  void Context::ResetPageVisibility(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Reset Page Visibility");

    ctx.BindComputePipeline(resetPageVisibility_);
    ctx.ImageBarrier(pageTables_, VK_IMAGE_LAYOUT_GENERAL);

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

  // TODO: See TODO for ResetPageVisibility (should allow batching updates instead of operating on whole VSM every time)
  void Context::FreeNonVisiblePages(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Free Non-Visible Pages");

    ctx.BindComputePipeline(freeNonVisiblePages_);
    ctx.ImageBarrier(pageTables_, VK_IMAGE_LAYOUT_GENERAL);

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

  void Context::AllocateRequestedPages(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Allocate Pages");

    auto pushConstants = GetPushConstants();
    ctx.SetPushConstants(pushConstants);

    ctx.BindComputePipeline(allocatePages_);
    ctx.Barrier();
    //Fvog::MemoryBarrier(Barrier::SHADER_STORAGE_BIT | Barrier::IMAGE_ACCESS_BIT);
    //Fvog::Cmd::BindImage("i_pageTables", pageTables_, 0);
    //Fvog::Cmd::BindStorageBuffer("VsmVisiblePagesBitmask", visiblePagesBitmask_);
    //Fvog::Cmd::BindStorageBuffer("VsmPageAllocRequests", pageAllocRequests_);
    ctx.Dispatch(1, 1, 1); // Only 1-32 threads will allocate
  }

  void Context::ClearDirtyPages(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Enqueue and Clear Dirty Pages");

    auto pushConstants = GetPushConstants();
    ctx.SetPushConstants(pushConstants);

    // TODO: make the first half of this (create dirty page list) more efficient by only considering updated VSMs
    ctx.BindComputePipeline(listDirtyPages_);
    ctx.Barrier();
    //Fvog::MemoryBarrier(Barrier::SHADER_STORAGE_BIT | Barrier::IMAGE_ACCESS_BIT);
    //Fvog::Cmd::BindImage("i_pageTables", pageTables_, 0);
    //Fvog::Cmd::BindStorageBuffer("VsmDirtyPageList", pagesToClear_);
    //Fvog::Cmd::BindStorageBuffer("VsmPageClearDispatchParams", pageClearDispatchParams_);
    ctx.DispatchInvocations(pageTables_.GetCreateInfo().extent.width, pageTables_.GetCreateInfo().extent.height, pageTables_.GetCreateInfo().arrayLayers);
    
    ctx.BindComputePipeline(clearDirtyPages_);
    ctx.Barrier();
    //Fvog::MemoryBarrier(Barrier::COMMAND_BUFFER_BIT | Barrier::SHADER_STORAGE_BIT);
    //Fvog::Cmd::BindImage("i_physicalPages", physicalPages_, 0);
    ctx.DispatchIndirect(pageClearDispatchParams_);

    ctx.Barrier();
    //Fvog::MemoryBarrier(Barrier::BUFFER_UPDATE_BIT);
    pageClearDispatchParams_.FillData(cmd, {.offset = offsetof(Fvog::DispatchIndirectCommand, groupCountZ), .size = sizeof(uint32_t)});
    pagesToClear_.FillData(cmd, {.offset = 0, .size = sizeof(uint32_t)});
  }

  //void Context::BindResourcesForCulling(VkCommandBuffer cmd)
  //{
  //  Fvog::Cmd::BindImage(0, pageTables_, 0);

  //  const auto sampler = Fvog::Sampler(Fvog::SamplerCreateInfo{
  //    .minFilter = Fvog::Filter::NEAREST,
  //    .magFilter = Fvog::Filter::NEAREST,
  //    .mipmapFilter = Fvog::Filter::NEAREST,
  //    .addressModeU = Fvog::AddressMode::REPEAT,
  //    .addressModeV = Fvog::AddressMode::REPEAT,
  //  });

  //  Fvog::Cmd::BindSampledImage(8, physicalPages_, sampler);
  //  Fvog::Cmd::BindSampledImage(10, vsmBitmaskHzb_, sampler);
  //}

  VsmPushConstants Context::GetPushConstants()
  {
    return {
      .globalUniformsIndex = 0,
      .pageTablesIndex = pageTables_.ImageView().GetStorageResourceHandle().index,
      .physicalPagesIndex = physicalPages_.ImageView().GetStorageResourceHandle().index,
      .vsmBitmaskHzbIndex = vsmBitmaskHzb_.ImageView().GetSampledResourceHandle().index,
      .vsmUniformsBufferIndex = uniformBuffer_.GetResourceHandle().index,
      .dirtyPageListBufferIndex = pagesToClear_.GetResourceHandle().index,
      .clipmapUniformsBufferIndex = 0, // DirectionalVirtualShadowMap::uniformBuffer_
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
    };
  }

  DirectionalVirtualShadowMap::DirectionalVirtualShadowMap(const CreateInfo& createInfo)
    : context_(createInfo.context),
      numClipmaps_(createInfo.numClipmaps),
      virtualExtent_(createInfo.virtualExtent),
      uniformBuffer_(*createInfo.context.device_, {}, "Directional VSM Uniforms")
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

  void DirectionalVirtualShadowMap::MarkVisiblePages(VkCommandBuffer cmd, Fvog::Texture& gDepth, Fvog::Buffer& globalUniforms)
  {
    auto ctx = Fvog::Context(*context_.device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Mark Visible Pages");

    auto pushConstants = context_.GetPushConstants();
    
    ctx.BindComputePipeline(context_.markVisiblePages_);
    ctx.Barrier();
    //Fvog::MemoryBarrier(Barrier::SHADER_STORAGE_BIT | Barrier::IMAGE_ACCESS_BIT | Barrier::TEXTURE_FETCH_BIT | Barrier::BUFFER_UPDATE_BIT);
    context_.visiblePagesBitmask_.FillData(cmd);
    pushConstants.gDepthIndex = gDepth.ImageView().GetSampledResourceHandle().index;
    pushConstants.globalUniformsIndex = globalUniforms.GetResourceHandle().index;
    pushConstants.clipmapUniformsBufferIndex = uniformBuffer_.GetResourceHandle().index;
    ctx.SetPushConstants(pushConstants);

    //Fvog::Cmd::BindSampledImage(0, gDepth, Fvog::Sampler(Fvog::SamplerState{}));
    //Fvog::Cmd::BindImage(0, context_.pageTables_, 0);
    //Fvog::Cmd::BindStorageBuffer("VsmVisiblePagesBitmask", context_.visiblePagesBitmask_);
    //Fvog::Cmd::BindStorageBuffer("VsmPageAllocRequests", context_.pageAllocRequests_);
    //Fvog::Cmd::BindStorageBuffer("VsmMarkPagesDirectionalUniforms", uniformBuffer_);
    //Fvog::Cmd::BindUniformBuffer("VsmGlobalUniforms", context_.uniformBuffer_);
    //Fvog::Cmd::BindUniformBuffer(0, globalUniforms);
    ctx.DispatchInvocations(gDepth.GetCreateInfo().extent);
  }

  void DirectionalVirtualShadowMap::UpdateExpensive(VkCommandBuffer cmd, glm::vec3 worldOffset, glm::vec3 direction, float firstClipmapWidth, float projectionZLength)
  {
    auto ctx = Fvog::Context(*context_.device_, cmd);
    const auto sideLength = firstClipmapWidth / virtualExtent_;
    uniforms_.firstClipmapTexelLength = sideLength;
    uniforms_.projectionZLength = projectionZLength;

    auto up = glm::vec3(0, 1, 0);
    if (1.0f - glm::abs(glm::dot(direction, up)) < 1e-4f)
    {
      up = glm::vec3(0, 0, 1);
    }
    
    stableViewMatrix = glm::lookAt(direction, glm::vec3(0), up);

    ctx.ClearTexture(context_.pageTables_, {});

    for (uint32_t i = 0; i < uniforms_.numClipmaps; i++)
    {
      const auto width = firstClipmapWidth * (1 << i) / 2.0f;
      // TODO: increase Z range for higher clipmaps (or for all?)
      stableProjections[i] = glm::orthoZO(-width, width, -width, width, -projectionZLength / 2.0f, projectionZLength / 2.0f);

      // Invalidate all clipmaps (clearing to 0 marks pages as not backed, not dirty, and not visible)
      //auto extent = context_.pageTables_.GetCreateInfo().extent;
      //extent.depth = 1;
      //context_.pageTables_.ClearImage(cmd, {.offset = {0, 0, uniforms_.clipmapTableIndices[i]}, .extent = extent});
    }

    this->UpdateOffset(cmd, worldOffset);
  }

  void DirectionalVirtualShadowMap::UpdateOffset(VkCommandBuffer cmd, glm::vec3 worldOffset)
  {
    auto ctx = Fvog::Context(*context_.device_, cmd);
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

    //uniformBuffer_.UpdateData(uniforms_);
    ctx.TeenyBufferUpdate(uniformBuffer_, uniforms_);
  }

  //void DirectionalVirtualShadowMap::BindResourcesForDrawing()
  //{
  //  Fvog::Cmd::BindStorageBuffer(5, uniformBuffer_);
  //  Fvog::Cmd::BindUniformBuffer(6, context_.uniformBuffer_);
  //  Fvog::Cmd::BindImage(0, context_.pageTables_, 0);
  //  Fvog::Cmd::BindImage(1, context_.physicalPages_, 0);
  //}
  
  void DirectionalVirtualShadowMap::GenerateBitmaskHzb(VkCommandBuffer cmd)
  {
    auto ctx = Fvog::Context(*context_.device_, cmd);
    auto marker = ctx.MakeScopedDebugMarker("VSM Generate Bitmap HZB");

    // TODO: only reduce necessary VSMs
    ctx.BindComputePipeline(context_.reduceVsmHzb_);
    //ctx.Barrier();
    //Fvog::MemoryBarrier(Barrier::TEXTURE_FETCH_BIT);

    //auto uniforms = Fvog::TypedBuffer<int32_t>(Fvog::BufferStorageFlag::DYNAMIC_STORAGE);
    //Fvog::Cmd::BindImage(0, context_.pageTables_, 0);

    auto pushConstants = context_.GetPushConstants();

    for (uint32_t currentPass = 0; currentPass <= (uint32_t)std::log2(pageTableSize); currentPass++)
    {
      ctx.Barrier();
      //Fvog::MemoryBarrier(Barrier::IMAGE_ACCESS_BIT);
      if (currentPass > 0)
      {
        //Fvog::Cmd::BindImage("i_srcVsmBitmaskHzb", context_.vsmBitmaskHzb_, currentPass - 1);
        pushConstants.srcVsmBitmaskHzbIndex = context_.vsmBitmaskHzb_.CreateSingleMipView(currentPass - 1).GetStorageResourceHandle().index;
      }

      //Fvog::Cmd::BindImage("i_dstVsmBitmaskHzb", context_.vsmBitmaskHzb_, currentPass);
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