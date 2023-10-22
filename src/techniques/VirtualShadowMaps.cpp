#include "VirtualShadowMaps.h"

#include "../RendererUtilities.h"

#include <Fwog/Buffer.h>
#include <Fwog/Rendering.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <bit>

namespace Techniques::VirtualShadowMaps
{
  namespace
  {
    Fwog::ComputePipeline CreateResetPageVisibilityPipeline()
    {
      auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmResetPageVisibility.comp.glsl");

      return Fwog::ComputePipeline({
        .shader = &comp,
      });
    }

    Fwog::ComputePipeline CreateMarkVisiblePipeline()
    {
      auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmMarkVisiblePages.comp.glsl");

      return Fwog::ComputePipeline({
        .shader = &comp,
      });
    }

    Fwog::ComputePipeline CreateAllocatorPipeline()
    {
      auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmAllocatePages.comp.glsl");

      return Fwog::ComputePipeline({
        .shader = &comp,
      });
    }

    Fwog::ComputePipeline CreateListDirtyPagesPipeline()
    {
      auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmListDirtyPages.comp.glsl");

      return Fwog::ComputePipeline({
        .shader = &comp,
      });
    }

    Fwog::ComputePipeline CreateClearDirtyPagesPipeline()
    {
      auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmClearDirtyPages.comp.glsl");

      return Fwog::ComputePipeline({
        .shader = &comp,
      });
    }

    Fwog::ComputePipeline CreateFreeNonVisiblePagesPipeline()
    {
      auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmFreeNonVisiblePages.comp.glsl");

      return Fwog::ComputePipeline({
        .shader = &comp,
      });
    }

    Fwog::ComputePipeline CreateReduceVsmHzbPipeline()
    {
      auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/shadows/vsm/VsmReduceBitmaskHzb.comp.glsl");

      return Fwog::ComputePipeline({
        .shader = &comp,
      });
    }
    
    using Barrier = Fwog::MemoryBarrierBit;
  }

  Context::Context(const CreateInfo& createInfo)
    : freeLayersBitmask_(size_t(std::ceil(float(createInfo.maxVsms) / 32)), 0xFFFFFFu),
      pageTables_(
        Fwog::TextureCreateInfo{
          .imageType = Fwog::ImageType::TEX_2D_ARRAY,
          .format = Fwog::Format::R32_UINT, // Ideally 16 bits, but image atomics are limited to 32-bit integer types
          .extent = Fwog::Extent3D{pageTableSize, pageTableSize, 1},
          .mipLevels = pageTableMipLevels,
          .arrayLayers = ((createInfo.maxVsms + 31) / 32) * 32, // Round up to the nearest multiple of 32 so we don't have any overflowing bits
        },
        "VSM Page Tables"),
      //pageTablesHzb_(
      //  Fwog::TextureCreateInfo{
      //    .imageType = Fwog::ImageType::TEX_2D_ARRAY,
      //    .format = Fwog::Format::R32_FLOAT,
      //    .extent = pageTables_.Extent(),
      //    .mipLevels = pageTableMipLevels,
      //    .arrayLayers = pageTables_.GetCreateInfo().arrayLayers,
      //  },
      //  "VSM Page Tables HZB"),
      vsmBitmaskHzb_(
        Fwog::TextureCreateInfo{
          .imageType = Fwog::ImageType::TEX_2D_ARRAY,
          .format = Fwog::Format::R8_UINT,
          .extent = pageTables_.Extent(),
          .mipLevels = pageTableMipLevels,
          .arrayLayers = pageTables_.GetCreateInfo().arrayLayers,
        },
        "VSM Bitmask HZB"),
      physicalPages_(
        Fwog::TextureCreateInfo{
          .imageType = Fwog::ImageType::TEX_2D,
          .format = Fwog::Format::R32_FLOAT,
          .extent = {(uint32_t)std::ceil(std::sqrt(createInfo.numPages)) * pageSize, (uint32_t)std::ceil(std::sqrt(createInfo.numPages)) * pageSize},
          .mipLevels = 1,
          .arrayLayers = 1,
        },
        "VSM Physical Pages"),
      physicalPagesUint_(physicalPages_.CreateFormatView(Fwog::Format::R32_UINT)),
      visiblePagesBitmask_(sizeof(uint32_t) * createInfo.numPages / 32),
      pageVisibleTimeTree_(sizeof(uint32_t) * createInfo.numPages * 2),
      uniformBuffer_(VsmGlobalUniforms{}, Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
      pageAllocRequests_(sizeof(PageAllocRequest) * (createInfo.numPages + 1)),
      pagesToClear_(sizeof(uint32_t) + sizeof(uint32_t) * createInfo.numPages),
      pageClearDispatchParams_(Fwog::DispatchIndirectCommand{pageSize / 8, pageSize / 8, 0}),
      resetPageVisibility_(CreateResetPageVisibilityPipeline()),
      allocatePages_(CreateAllocatorPipeline()),
      markVisiblePages_(CreateMarkVisiblePipeline()),
      listDirtyPages_(CreateListDirtyPagesPipeline()),
      clearDirtyPages_(CreateClearDirtyPagesPipeline()),
      freeNonVisiblePages_(CreateFreeNonVisiblePagesPipeline()),
      //reducePhysicalPages_(CreateReducePhysicalPipeline()),
      //reduceVirtualPages_(CreateReduceVirtualPipeline()),
      reduceVsmHzb_(CreateReduceVsmHzbPipeline())
  {
    // Clear every page mapping to zero
    for (uint32_t level = 0; level < pageTableMipLevels; level++)
    {
      pageTables_.ClearImage({
        .level = level,
      });
    }

    physicalPages_.ClearImage({});
    visiblePagesBitmask_.FillData();
    pageVisibleTimeTree_.FillData();
  }

  void Context::UpdateUniforms(const VsmGlobalUniforms& uniforms)
  {
    uniformBuffer_.UpdateData(uniforms);
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
    FWOG_ASSERT(layerIndex < pageTables_.GetCreateInfo().arrayLayers);

    size_t i = layerIndex / 32;
    int bit = layerIndex % 32;
    freeLayersBitmask_[i] |= 1 << bit;
  }

  // TODO: this should be per-VSM, since not every VSM is updated (and therefore requires a visibility reset) every frame
  // Should make it take a list of VSM indices to reset
  void Context::ResetPageVisibility()
  {
    Fwog::Compute(
      "VSM Reset Page Visibility",
      [&]
      {
        Fwog::Cmd::BindComputePipeline(resetPageVisibility_);
        Fwog::MemoryBarrier(Barrier::IMAGE_ACCESS_BIT);

        for (uint32_t i = 0; i < pageTables_.GetCreateInfo().mipLevels; i++)
        {
          Fwog::Cmd::BindImage(0, pageTables_, i);
          auto extent = pageTables_.Extent() / (1 << i);
          extent.depth = pageTables_.GetCreateInfo().arrayLayers;
          Fwog::Cmd::DispatchInvocations(extent);
        }
      });
  }

  // TODO: See TODO for ResetPageVisibility (should allow batching updates instead of operating on whole VSM every time)
  void Context::FreeNonVisiblePages()
  {
    Fwog::Compute(
      "VSM Free Non-Visible Pages",
      [&]
      {
        Fwog::Cmd::BindComputePipeline(freeNonVisiblePages_);
        Fwog::MemoryBarrier(Barrier::IMAGE_ACCESS_BIT);

        for (uint32_t i = 0; i < pageTables_.GetCreateInfo().mipLevels; i++)
        {
          Fwog::Cmd::BindImage(0, pageTables_, i);
          auto extent = pageTables_.Extent() / (1 << i);
          extent.depth = pageTables_.GetCreateInfo().arrayLayers;
          Fwog::Cmd::DispatchInvocations(extent);
        }
      });
  }

  void Context::AllocateRequestedPages()
  {
    Fwog::Compute(
      "VSM Allocate Pages",
      [&]
      {
        Fwog::Cmd::BindComputePipeline(allocatePages_);
        Fwog::MemoryBarrier(Barrier::SHADER_STORAGE_BIT | Barrier::IMAGE_ACCESS_BIT);
        Fwog::Cmd::BindImage("i_pageTables", pageTables_, 0);
        Fwog::Cmd::BindStorageBuffer("VsmVisiblePagesBitmask", visiblePagesBitmask_);
        Fwog::Cmd::BindStorageBuffer("VsmPageAllocRequests", pageAllocRequests_);
        Fwog::Cmd::Dispatch(1, 1, 1); // Only 1-32 threads will allocate
      });
  }

  void Context::ClearDirtyPages()
  {
    Fwog::Compute(
      "VSM Enqueue and Clear Dirty Pages",
      [&]
      {
        // TODO: make the first half of this (create dirty page list) more efficient by only considering updated VSMs
        Fwog::Cmd::BindComputePipeline(listDirtyPages_);
        Fwog::MemoryBarrier(Barrier::SHADER_STORAGE_BIT | Barrier::IMAGE_ACCESS_BIT);
        Fwog::Cmd::BindImage("i_pageTables", pageTables_, 0);
        Fwog::Cmd::BindStorageBuffer("VsmDirtyPageList", pagesToClear_);
        Fwog::Cmd::BindStorageBuffer("VsmPageClearDispatchParams", pageClearDispatchParams_);
        Fwog::Cmd::DispatchInvocations(pageTables_.Extent().width, pageTables_.Extent().height, pageTables_.GetCreateInfo().arrayLayers);

        Fwog::Cmd::BindComputePipeline(clearDirtyPages_);
        Fwog::MemoryBarrier(Barrier::COMMAND_BUFFER_BIT | Barrier::SHADER_STORAGE_BIT);
        Fwog::Cmd::BindImage("i_physicalPages", physicalPages_, 0);
        Fwog::Cmd::DispatchIndirect(pageClearDispatchParams_, 0);

        Fwog::MemoryBarrier(Barrier::BUFFER_UPDATE_BIT);
        pageClearDispatchParams_.FillData({.offset = offsetof(Fwog::DispatchIndirectCommand, groupCountZ), .size = sizeof(uint32_t)});
        pagesToClear_.FillData({.offset = 0, .size = sizeof(uint32_t)});
      });
  }

  void Context::BindResourcesForCulling()
  {
    Fwog::Cmd::BindImage(0, pageTables_, 0);

    const auto sampler = Fwog::Sampler(Fwog::SamplerState{
      .minFilter = Fwog::Filter::NEAREST,
      .magFilter = Fwog::Filter::NEAREST,
      .mipmapFilter = Fwog::Filter::NEAREST,
      .addressModeU = Fwog::AddressMode::REPEAT,
      .addressModeV = Fwog::AddressMode::REPEAT,
    });

    Fwog::Cmd::BindSampledImage(8, physicalPages_, sampler);
    //Fwog::Cmd::BindSampledImage(9, pageTablesHzb_, sampler);
    Fwog::Cmd::BindSampledImage(10, vsmBitmaskHzb_, sampler);
  }

  DirectionalVirtualShadowMap::DirectionalVirtualShadowMap(const CreateInfo& createInfo)
    : context_(createInfo.context),
      numClipmaps_(createInfo.numClipmaps),
      virtualExtent_(createInfo.virtualExtent),
      uniformBuffer_(Fwog::BufferStorageFlag::DYNAMIC_STORAGE)
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

  void DirectionalVirtualShadowMap::MarkVisiblePages(const Fwog::Texture& gDepth, const Fwog::Buffer& globalUniforms)
  {
    Fwog::Compute(
      "VSM Mark Visible Pages",
      [&]
      {
        Fwog::Cmd::BindComputePipeline(context_.markVisiblePages_);
        Fwog::MemoryBarrier(Barrier::SHADER_STORAGE_BIT | Barrier::IMAGE_ACCESS_BIT | Barrier::TEXTURE_FETCH_BIT | Barrier::BUFFER_UPDATE_BIT);
        context_.visiblePagesBitmask_.FillData();
        Fwog::Cmd::BindSampledImage(0, gDepth, Fwog::Sampler(Fwog::SamplerState{}));
        Fwog::Cmd::BindImage(0, context_.pageTables_, 0);
        Fwog::Cmd::BindStorageBuffer("VsmVisiblePagesBitmask", context_.visiblePagesBitmask_);
        Fwog::Cmd::BindStorageBuffer("VsmPageAllocRequests", context_.pageAllocRequests_);
        Fwog::Cmd::BindStorageBuffer("VsmMarkPagesDirectionalUniforms", uniformBuffer_);
        Fwog::Cmd::BindUniformBuffer("VsmGlobalUniforms", context_.uniformBuffer_);
        Fwog::Cmd::BindUniformBuffer(0, globalUniforms);
        Fwog::Cmd::DispatchInvocations(gDepth.Extent());
      });
  }

  void DirectionalVirtualShadowMap::UpdateExpensive(glm::vec3 worldOffset, glm::vec3 direction, float firstClipmapWidth, float projectionZLength)
  {
    const auto sideLength = firstClipmapWidth / virtualExtent_;
    uniforms_.firstClipmapTexelLength = sideLength;
    uniforms_.projectionZLength = projectionZLength;

    auto up = glm::vec3(0, 1, 0);
    if (1.0f - glm::abs(glm::dot(direction, up)) < 1e-4f)
    {
      up = glm::vec3(0, 0, 1);
    }
    
    stableViewMatrix = glm::lookAt(direction, glm::vec3(0), up);

    for (uint32_t i = 0; i < uniforms_.numClipmaps; i++)
    {
      const auto width = firstClipmapWidth * (1 << i) / 2.0f;
      // TODO: increase Z range for higher clipmaps (or for all?)
      stableProjections[i] = glm::orthoZO(-width, width, -width, width, -projectionZLength / 2.0f, projectionZLength / 2.0f);

      // Invalidate all clipmaps (clearing to 0 marks pages as not backed, not dirty, and not visible)
      auto extent = context_.pageTables_.Extent();
      extent.depth = 1;
      context_.pageTables_.ClearImage({.offset = {0, 0, uniforms_.clipmapTableIndices[i]}, .extent = extent});
    }

    this->UpdateOffset(worldOffset);
  }

  void DirectionalVirtualShadowMap::UpdateOffset(glm::vec3 worldOffset)
  {
    for (uint32_t i = 0; i < uniforms_.numClipmaps; i++)
    {
      // Find the offset from the un-translated view matrix
      uniforms_.clipmapStableViewProjections[i] = stableProjections[i] * stableViewMatrix;
      const auto clip = stableProjections[i] * stableViewMatrix * glm::vec4(worldOffset, 1);
      const auto ndc = clip / clip.w;
      const auto uv = glm::vec2(ndc) * 0.5f; // Don't add the 0.5, since we want the center to be 0
      const auto pageOffset = glm::ivec2(uv * glm::vec2(context_.pageTables_.Extent().width, context_.pageTables_.Extent().height));
      //const auto oldOrigin = uniforms_.clipmapOrigins[i];
      uniforms_.clipmapOrigins[i] = pageOffset;

      const auto ndcShift = 2.0f * glm::vec2((float)pageOffset.x / context_.pageTables_.Extent().width, (float)pageOffset.y / context_.pageTables_.Extent().height);
      
      // Shift rendering projection matrix by opposite of page offset in clip space, then apply *only* that shift to the view matrix
      const auto shiftedProjection = glm::translate(glm::mat4(1), glm::vec3(-ndcShift, 0)) * stableProjections[i];
      viewMatrices[i] = glm::inverse(stableProjections[i]) * shiftedProjection * stableViewMatrix;

      //uniforms_.clipmapOrigins[i] = {};
      //viewMatrices[i] = stableViewMatrix;
    }

    uniformBuffer_.UpdateData(uniforms_);
  }

  void DirectionalVirtualShadowMap::BindResourcesForDrawing()
  {
    Fwog::Cmd::BindStorageBuffer(5, uniformBuffer_);
    Fwog::Cmd::BindUniformBuffer(6, context_.uniformBuffer_);
    Fwog::Cmd::BindImage(0, context_.pageTables_, 0);
    Fwog::Cmd::BindImage(1, context_.physicalPages_, 0);
  }
  
  void DirectionalVirtualShadowMap::GenerateBitmaskHzb()
  {
     Fwog::Compute(
      "VSM Generate Bitmap HZB",
      [&]
      {
        // TODO: only reduce necessary VSMs
        Fwog::Cmd::BindComputePipeline(context_.reduceVsmHzb_);
        Fwog::MemoryBarrier(Barrier::TEXTURE_FETCH_BIT);

        auto uniforms = Fwog::TypedBuffer<int32_t>(Fwog::BufferStorageFlag::DYNAMIC_STORAGE);
        Fwog::Cmd::BindUniformBuffer(7, uniforms);
        Fwog::Cmd::BindImage(0, context_.pageTables_, 0);

        for (uint32_t currentPass = 0; currentPass <= (uint32_t)std::log2(pageTableSize); currentPass++)
        {
          Fwog::MemoryBarrier(Barrier::IMAGE_ACCESS_BIT);
          if (currentPass > 0)
          {
            Fwog::Cmd::BindImage("i_srcVsmBitmaskHzb", context_.vsmBitmaskHzb_, currentPass - 1);
          }

          Fwog::Cmd::BindImage("i_dstVsmBitmaskHzb", context_.vsmBitmaskHzb_, currentPass);

          uniforms.UpdateData(currentPass);

          auto invocations = Fwog::Extent3D{
            context_.vsmBitmaskHzb_.Extent().width,
            context_.vsmBitmaskHzb_.Extent().height,
            1,
          };
          invocations = invocations >> currentPass;
          invocations.depth = context_.vsmBitmaskHzb_.GetCreateInfo().arrayLayers;
          Fwog::Cmd::DispatchInvocations(invocations);
        }
      });
  }
} // namespace Techniques
