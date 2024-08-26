#include "Rendering2.h"

#include "Buffer2.h"
#include "Pipeline2.h"
#include "Texture2.h"
#include "detail/Common.h"
#include "detail/ApiToEnum2.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <vector>

namespace Fvog
{
  using namespace detail;

  static VkClearColorValue ClearColorValueToVk(ClearColorValue v)
  {
    if (auto p = std::get_if<std::array<float, 4>>(&v.data))
    {
      auto c = *p;
      return VkClearColorValue{.float32 = {c[0], c[1], c[2], c[3]}};
    }

    if (auto p = std::get_if<std::array<uint32_t, 4>>(&v.data))
    {
      auto c = *p;
      return VkClearColorValue{.uint32 = {c[0], c[1], c[2], c[3]}};
    }

    if (auto p = std::get_if<std::array<int32_t, 4>>(&v.data))
    {
      auto c = *p;
      return VkClearColorValue{.int32 = {c[0], c[1], c[2], c[3]}};
    }

    assert(0);
    return {};
  }

  Context::Context(Device& device, VkCommandBuffer commandBuffer)
    : device_(&device),
      commandBuffer_(commandBuffer)
  {}

  void Context::BeginRendering(const RenderInfo& renderInfo) const
  {
    ZoneScoped;
    vkCmdBeginDebugUtilsLabelEXT(commandBuffer_, Address(VkDebugUtilsLabelEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
      .pLabelName = renderInfo.name ? renderInfo.name : "Render Pass",
      .color = {0.67f, 0.2f, 0.2f, 1.0f},
    }));

    auto colorAttachments = std::vector<VkRenderingAttachmentInfo>();
    colorAttachments.reserve(renderInfo.colorAttachments.size());

    for (const auto& attachment : renderInfo.colorAttachments)
    {
      colorAttachments.emplace_back(VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = attachment.texture.get().ImageView(),
        .imageLayout = *attachment.texture.get().currentLayout,
        .loadOp = attachment.loadOp,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = VkClearValue{.color = ClearColorValueToVk(attachment.clearValue)},
      });
    }

    auto depthAttachment = VkRenderingAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    if (renderInfo.depthAttachment.has_value())
    {
      depthAttachment.imageView = renderInfo.depthAttachment->texture.get().ImageView();
      depthAttachment.imageLayout = *renderInfo.depthAttachment->texture.get().currentLayout;
      depthAttachment.loadOp = renderInfo.depthAttachment->loadOp;
      depthAttachment.clearValue = VkClearValue{.depthStencil = VkClearDepthStencilValue{.depth = renderInfo.depthAttachment->clearValue.depth,}};
    }

    auto stencilAttachment = VkRenderingAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    if (renderInfo.stencilAttachment.has_value())
    {
      stencilAttachment.imageView = renderInfo.stencilAttachment->texture.get().ImageView();
      stencilAttachment.imageLayout = *renderInfo.stencilAttachment->texture.get().currentLayout;
      stencilAttachment.loadOp = renderInfo.stencilAttachment->loadOp;
      stencilAttachment.clearValue = VkClearValue{.depthStencil = VkClearDepthStencilValue{.stencil = renderInfo.stencilAttachment->clearValue.stencil,}};
    }

    auto viewport = renderInfo.viewport.value_or(VkViewport{
      .x = 0,
      .y = 0,
      .width = 0,
      .height = 0,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
    });

    // For inferring the viewport and render area width and height
    // Render area is always inferred
    // Using the maximum possible area should have no perf impact on desktop
    // TODO: these should be based on the max framebuffer size for attachmentless rendering
    auto minX = uint32_t{100'000};
    auto minY = uint32_t{100'000};

    for (const auto& attachment : renderInfo.colorAttachments)
    {
      minX = std::min(minX, attachment.texture.get().GetTextureCreateInfo().extent.width);
      minY = std::min(minY, attachment.texture.get().GetTextureCreateInfo().extent.height);
    }

    if (renderInfo.depthAttachment.has_value())
    {
      minX = std::min(minX, renderInfo.depthAttachment->texture.get().GetTextureCreateInfo().extent.width);
      minY = std::min(minY, renderInfo.depthAttachment->texture.get().GetTextureCreateInfo().extent.height);
    }

    if (renderInfo.stencilAttachment.has_value())
    {
      minX = std::min(minX, renderInfo.stencilAttachment->texture.get().GetTextureCreateInfo().extent.width);
      minY = std::min(minY, renderInfo.stencilAttachment->texture.get().GetTextureCreateInfo().extent.height);
    }

    // If no attachments, render area should be viewport
    if (renderInfo.colorAttachments.empty() && !renderInfo.depthAttachment && !renderInfo.stencilAttachment)
    {
      minX = (uint32_t)viewport.width;
      minY = (uint32_t)viewport.height;
    }

    // Infer viewport width and height from union of attachment dimensions
    if (!renderInfo.viewport.has_value())
    {
      viewport.width = static_cast<float>(minX);
      viewport.height = static_cast<float>(minY);
    }

    auto renderArea = VkRect2D{.offset = VkOffset2D{0, 0}, .extent = VkExtent2D{minX, minY}};
    vkCmdBeginRendering(commandBuffer_, Address(VkRenderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = renderArea,
      .layerCount = 1,
      .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
      .pColorAttachments = colorAttachments.data(),
      .pDepthAttachment = &depthAttachment,
      .pStencilAttachment = &stencilAttachment,
    }));

    // These are assumed to be dynamic state
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &renderArea);
  }

  void Context::EndRendering() const
  {
    ZoneScoped;
    vkCmdEndRendering(commandBuffer_);
    vkCmdEndDebugUtilsLabelEXT(commandBuffer_);
  }

  void Context::ImageBarrier(const Texture& texture, VkImageLayout newLayout) const
  {
    ZoneScoped;
    VkImageAspectFlags aspectMask{};
    aspectMask |= FormatIsColor(texture.GetCreateInfo().format) ? VK_IMAGE_ASPECT_COLOR_BIT : 0;
    aspectMask |= FormatIsDepth(texture.GetCreateInfo().format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    aspectMask |= FormatIsStencil(texture.GetCreateInfo().format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    ImageBarrier(texture.Image(), *texture.currentLayout, newLayout, aspectMask);
    *texture.currentLayout = newLayout;
  }

  void Context::ImageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask) const
  {
    ZoneScoped;
    if (oldLayout == newLayout)
    {
      // Issue global memory barrier instead of same-layout transition.
      // This is a workaround for an AMD driver bug that causes images to become trashed in RenderDoc.
      // https://github.com/baldurk/renderdoc/issues/3360
      Barrier();
      return;
    }

    vkCmdPipelineBarrier2(commandBuffer_, Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = Address(VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = image,
        .subresourceRange = {
          .aspectMask = aspectMask,
          .levelCount = VK_REMAINING_MIP_LEVELS,
          .layerCount = VK_REMAINING_ARRAY_LAYERS,
        },
      }),
    }));
  }

  void Context::ImageBarrierDiscard(const Texture& texture, VkImageLayout newLayout) const
  {
    ZoneScoped;
    VkImageAspectFlags aspectMask{};
    aspectMask |= FormatIsColor(texture.GetCreateInfo().format) ? VK_IMAGE_ASPECT_COLOR_BIT : 0;
    aspectMask |= FormatIsDepth(texture.GetCreateInfo().format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    aspectMask |= FormatIsStencil(texture.GetCreateInfo().format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    ImageBarrier(texture.Image(), VK_IMAGE_LAYOUT_UNDEFINED, newLayout, aspectMask);
    *texture.currentLayout = newLayout;
  }

  void Context::BufferBarrier(const Buffer& buffer) const
  {
    BufferBarrier(buffer.Handle());
  }

  void Context::BufferBarrier(VkBuffer buffer) const
  {
    ZoneScoped;
    vkCmdPipelineBarrier2(commandBuffer_, Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount = 1,
      .pBufferMemoryBarriers = Address(VkBufferMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .buffer = buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
      }),
    }));
  }

  void Context::Barrier() const
  {
    ZoneScoped;
    vkCmdPipelineBarrier2(commandBuffer_, Address(VkDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = Address(VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
      }),
    }));
  }

  void Context::ClearTexture(const Texture& texture, const TextureClearInfo& clearInfo) const
  {
    ZoneScoped;
    vkCmdClearColorImage(commandBuffer_,
                         texture.Image(),
                         *texture.currentLayout,
                         Address(ClearColorValueToVk(clearInfo.color)),
                         1,
                         Address(VkImageSubresourceRange{
                           .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = clearInfo.baseMipLevel,
                           .levelCount = clearInfo.levelCount,
                           .baseArrayLayer = clearInfo.baseArrayLayer,
                           .layerCount = clearInfo.layerCount,
                         }));
  }

  void Context::CopyBufferToTexture(const Buffer& src, Texture& dst, const TextureUpdateInfo& info)
  {
    ZoneScoped;

    // Convenience- so the user doesn't have to explicitly specify 1 for height or depth when writing 1D or 2D images
    auto extent = info.extent;
    extent.height = std::max(extent.height, 1u);
    extent.depth = std::max(extent.depth, 1u);

    vkCmdCopyBufferToImage2(commandBuffer_, detail::Address(VkCopyBufferToImageInfo2{
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
      .srcBuffer = src.Handle(),
      .dstImage = dst.Image(),
      .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .regionCount = 1,
      .pRegions = detail::Address(VkBufferImageCopy2{
        .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
        .bufferOffset = 0,
        .bufferRowLength = info.rowLength,
        .bufferImageHeight = info.imageHeight,
        .imageSubresource = VkImageSubresourceLayers{
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .mipLevel = info.level,
          .layerCount = 1,
        },
        .imageOffset = info.offset,
        .imageExtent = extent,
      }),
    }));
  }

  void Context::CopyBuffer(const Buffer& src, Buffer& dst, const CopyBufferInfo& copyInfo)
  {
    vkCmdCopyBuffer2(commandBuffer_, Address(VkCopyBufferInfo2{
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
      .srcBuffer = src.Handle(),
      .dstBuffer = dst.Handle(),
      .regionCount = 1,
      .pRegions = Address(VkBufferCopy2{
        .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
        .srcOffset = copyInfo.srcOffset,
        .dstOffset = copyInfo.dstOffset,
        .size = copyInfo.size,
      }),
    }));
  }

  void Context::TeenyBufferUpdate(Buffer& buffer, TriviallyCopyableByteSpan data, size_t offset) const
  {
    ZoneScoped;
    vkCmdUpdateBuffer(commandBuffer_, buffer.Handle(), offset, data.size_bytes(), data.data());
  }

  void Context::BindGraphicsPipeline(const GraphicsPipeline& pipeline) const
  {
    ZoneScoped;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Handle());
  }

  void Context::BindComputePipeline(const ComputePipeline& pipeline) const
  {
    ZoneScoped;
    boundComputePipeline_ = &pipeline;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle());
  }

  void Context::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) const
  {
    Dispatch({groupCountX, groupCountY, groupCountZ});
  }

  void Context::Dispatch(Extent3D groupCount) const
  {
    ZoneScoped;
    vkCmdDispatch(commandBuffer_, groupCount.width, groupCount.height, groupCount.depth);
  }

  void Context::DispatchInvocations(uint32_t invocationCountX, uint32_t invocationCountY, uint32_t invocationCountZ) const
  {
    DispatchInvocations({invocationCountX, invocationCountY, invocationCountZ});
  }

  void Context::DispatchInvocations(Extent3D invocationCount) const
  {
    ZoneScoped;
    const auto workgroupSize = boundComputePipeline_->WorkgroupSize();
    const auto [x, y, z] = (invocationCount + workgroupSize - 1) / workgroupSize;

    vkCmdDispatch(commandBuffer_, x, y, z);
  }
  void Context::DispatchIndirect(const Fvog::Buffer& buffer, VkDeviceSize offset) const
  {
    ZoneScoped;
    vkCmdDispatchIndirect(commandBuffer_, buffer.Handle(), offset);
  }

  void Context::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const
  {
    ZoneScoped;
    vkCmdDraw(commandBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
  }

  void Context::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) const
  {
    ZoneScoped;
    vkCmdDrawIndexed(commandBuffer_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
  }

  void Context::DrawIndirect(const Fvog::Buffer& buffer, VkDeviceSize bufferOffset, uint32_t drawCount, uint32_t stride) const
  {
    ZoneScoped;
    vkCmdDrawIndirect(commandBuffer_, buffer.Handle(), bufferOffset, drawCount, stride);
  }

  void Context::DrawIndexedIndirect(const Fvog::Buffer& buffer, VkDeviceSize bufferOffset, uint32_t drawCount, uint32_t stride) const
  {
    ZoneScoped;
    vkCmdDrawIndexedIndirect(commandBuffer_, buffer.Handle(), bufferOffset, drawCount, stride);
  }

  void Context::BindIndexBuffer(const Buffer& buffer, VkDeviceSize offset, VkIndexType indexType) const
  {
    ZoneScoped;
    vkCmdBindIndexBuffer(commandBuffer_, buffer.Handle(), offset, indexType);
  }

  void Context::SetPushConstants(TriviallyCopyableByteSpan values, uint32_t offset) const
  {
    ZoneScoped;
    vkCmdPushConstants(commandBuffer_, device_->defaultPipelineLayout, VK_SHADER_STAGE_ALL, offset, static_cast<uint32_t>(values.size_bytes()), values.data());
  }

  ScopedDebugMarker::ScopedDebugMarker(VkCommandBuffer commandBuffer, const char* message, std::array<float, 4> color)
    : commandBuffer_(commandBuffer)
  {
    ZoneScoped;
    vkCmdBeginDebugUtilsLabelEXT(commandBuffer_, Address(VkDebugUtilsLabelEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
      .pLabelName = message,
      .color = {color[0], color[1], color[2], color[3]},
    }));
  }

  ScopedDebugMarker::~ScopedDebugMarker()
  {
    ZoneScoped;
    vkCmdEndDebugUtilsLabelEXT(commandBuffer_);
  }

  ScopedDebugMarker Context::MakeScopedDebugMarker(const char* message, std::array<float, 4> color) const
  {
    return {commandBuffer_, message, color};
  }
} // namespace Fvog
