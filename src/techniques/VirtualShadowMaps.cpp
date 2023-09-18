#include "VirtualShadowMaps.h"

#include "../RendererUtilities.h"

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
    
    using Barrier = Fwog::MemoryBarrierBit;
  }

  Context::Context(const CreateInfo& createInfo)
    : freeLayersBitmask_(size_t(std::ceilf(float(createInfo.maxVsms) / 32)), 0xFFFFFFu),
      pageTables_(
        Fwog::TextureCreateInfo{
          .imageType = Fwog::ImageType::TEX_2D_ARRAY,
          .format = Fwog::Format::R32_UINT, // Ideally 16 bits, but image atomics are limited to 32-bit integer types
          .extent = Fwog::Extent3D{maxExtent, maxExtent, 1} / pageSize,
          .mipLevels = mipLevels,
          .arrayLayers = ((createInfo.maxVsms + 31) / 32) * 32, // Round up to the nearest multiple of 32 so we don't have any overflowing bits
        },
        "VSM Page Mappings"),
      pages_(Fwog::CreateTexture2D({(uint32_t)std::ceil(std::sqrt(createInfo.numPages)) * pageSize, (uint32_t)std::ceil(std::sqrt(createInfo.numPages)) * pageSize}, Fwog::Format::R32_UINT, "VSM Physical Pages")),
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
      clearDirtyPages_(CreateClearDirtyPagesPipeline())
  {
    // Clear every page mapping to zero
    for (uint32_t level = 0; level < mipLevels; level++)
    {
      pageTables_.ClearImage({
        .level = level,
      });
    }

    pages_.ClearImage({});
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
    // TODO: fix this
    //Fwog::Compute(
    //  "VSM Enqueue and Clear Dirty Pages",
    //  [&]
    //  {
    //    Fwog::Cmd::BindComputePipeline(listDirtyPages_);
    //    Fwog::MemoryBarrier(Barrier::SHADER_STORAGE_BIT | Barrier::IMAGE_ACCESS_BIT);
    //    Fwog::Cmd::BindImage("i_pageTables", pageTables_, 0);
    //    Fwog::Cmd::BindStorageBuffer("VsmDirtyPageList", pagesToClear_);
    //    Fwog::Cmd::BindStorageBuffer("VsmPageClearDispatchParams", pageClearDispatchParams_);
    //    Fwog::Cmd::DispatchInvocations(pageTables_.Extent().width, pageTables_.Extent().height, pageTables_.GetCreateInfo().arrayLayers);

    //    Fwog::Cmd::BindComputePipeline(clearDirtyPages_);
    //    Fwog::MemoryBarrier(Barrier::COMMAND_BUFFER_BIT | Barrier::SHADER_STORAGE_BIT);
    //    Fwog::Cmd::BindImage("i_physicalPages", pages_, 0);
    //    Fwog::Cmd::DispatchIndirect(pageClearDispatchParams_, 0);

    //    Fwog::MemoryBarrier(Barrier::BUFFER_UPDATE_BIT);
    //    pageClearDispatchParams_.FillData({.offset = offsetof(Fwog::DispatchIndirectCommand, groupCountZ), .size = sizeof(uint32_t)});
    //    pagesToClear_.FillData({.offset = 0, .size = sizeof(uint32_t)});
    //  });

    Fwog::MemoryBarrier(Barrier::IMAGE_ACCESS_BIT | Barrier::BUFFER_UPDATE_BIT);
    pageClearDispatchParams_.FillData({.offset = offsetof(Fwog::DispatchIndirectCommand, groupCountZ), .size = sizeof(uint32_t)});
    pagesToClear_.FillData({.offset = 0, .size = sizeof(uint32_t)});
    constexpr uint32_t beeg = ~0u;
    pages_.ClearImage({.data = &beeg});
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

  void DirectionalVirtualShadowMap::UpdateExpensive(const glm::mat4& viewMat, float firstClipmapWidth)
  {
    const auto sideLength = firstClipmapWidth / virtualExtent_;
    uniforms_.firstClipmapTexelLength = sideLength;
    
    for (uint32_t i = 0; i < uniforms_.numClipmaps; i++)
    {
      const auto width = firstClipmapWidth * (1 << i) / 2.0f;
      clipmapProjections[i] = glm::orthoZO(-width, width, -width, width, -100.f, 100.f);
      
      uniforms_.clipmapViewProjections[i] = clipmapProjections[i] * viewMat;
    }

    uniformBuffer_.UpdateData(uniforms_);

    // Invalidate all clipmaps (clearing to 0 marks pages as not backed)
    for (uint32_t i = 0; i < uniforms_.numClipmaps; i++)
    {
      auto extent = context_.pageTables_.Extent();
      extent.depth = 1;
      context_.pageTables_.ClearImage({.offset = {0, 0, uniforms_.clipmapTableIndices[i]}, .extent = extent});
    }
  }

  void DirectionalVirtualShadowMap::BindResourcesForDrawing()
  {
    Fwog::Cmd::BindStorageBuffer(5, uniformBuffer_);
    Fwog::Cmd::BindUniformBuffer(6, context_.uniformBuffer_);
    Fwog::Cmd::BindImage(0, context_.pageTables_, 0);
    Fwog::Cmd::BindImage(1, context_.pages_, 0);
  }
} // namespace Techniques
