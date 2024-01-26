#include "Rendering2.h"

#include "Buffer2.h"
#include "Texture2.h"
#include "detail/Common.h"
#include "detail/ApiToEnum2.h"

#include <volk.h>

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

  Context::Context(VkCommandBuffer commandBuffer) : commandBuffer_(commandBuffer) {}

  void Context::BeginRendering(const RenderInfo& renderInfo) const
  {
    auto colorAttachments = std::vector<VkRenderingAttachmentInfo>();
    colorAttachments.reserve(renderInfo.colorAttachments.size());

    for (const auto& attachment : renderInfo.colorAttachments)
    {
      colorAttachments.emplace_back(VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = attachment.texture.get().ImageView(),
        .imageLayout = attachment.layout,
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
      depthAttachment.imageLayout = renderInfo.depthAttachment->layout;
      depthAttachment.loadOp = renderInfo.depthAttachment->loadOp;
      depthAttachment.clearValue = VkClearValue{.depthStencil = VkClearDepthStencilValue{.depth = renderInfo.depthAttachment->clearValue.depth,}};
    }

    auto stencilAttachment = VkRenderingAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    if (renderInfo.depthAttachment.has_value())
    {
      stencilAttachment.imageView = renderInfo.depthAttachment->texture.get().ImageView();
      stencilAttachment.imageLayout = renderInfo.depthAttachment->layout;
      stencilAttachment.loadOp = renderInfo.depthAttachment->loadOp;
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
      minX = std::min(minX, attachment.texture.get().GetCreateInfo().extent.width);
      minY = std::min(minY, attachment.texture.get().GetCreateInfo().extent.height);
    }

    if (renderInfo.depthAttachment.has_value())
    {
      minX = std::min(minX, renderInfo.depthAttachment->texture.get().GetCreateInfo().extent.width);
      minY = std::min(minY, renderInfo.depthAttachment->texture.get().GetCreateInfo().extent.height);
    }

    if (renderInfo.stencilAttachment.has_value())
    {
      minX = std::min(minX, renderInfo.stencilAttachment->texture.get().GetCreateInfo().extent.width);
      minY = std::min(minY, renderInfo.stencilAttachment->texture.get().GetCreateInfo().extent.height);
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
    vkCmdEndRendering(commandBuffer_);
  }

  void Context::ImageBarrier(const Texture& texture, VkImageLayout oldLayout, VkImageLayout newLayout) const
  {
    const VkImageAspectFlags aspectMask = FormatIsColor(texture.GetCreateInfo().format) ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    ImageBarrier(texture.Image(), oldLayout, newLayout, aspectMask);
  }

  void Context::ImageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask) const
  {
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

  void Context::BufferBarrier(const Buffer& buffer) const
  {
    BufferBarrier(buffer.Handle());
  }

  void Context::BufferBarrier(VkBuffer buffer) const
  {
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
} // namespace Fvog
