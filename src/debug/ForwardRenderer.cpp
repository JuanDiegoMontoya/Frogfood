#include "ForwardRenderer.h"
#include "RendererUtilities.h"
#include "Fvog/Rendering2.h"

#include <tracy/Tracy.hpp>

namespace Debug
{
  ForwardRenderer::ForwardRenderer(Fvog::Device& device)
    : device_(&device)
  {
    vertexShader_ = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/debug/Forward.vert.glsl");
    fragmentShader_ = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/Forward.frag.glsl");
    uniformBuffer_.emplace(device, Fvog::TypedBufferCreateInfo{1}, "Forward Uniform Buffer");
  }

  void ForwardRenderer::PushDraw(const Drawable& draw)
  {
    draws_.push_back(draw);
  }

  void ForwardRenderer::FlushAndRender(VkCommandBuffer commandBuffer, const ViewParams& view, const Fvog::TextureView& renderTarget)
  {
    ZoneScoped;
    auto ctx = Fvog::Context(*device_, commandBuffer);

    auto rtExtent = renderTarget.GetTextureCreateInfo().extent;
    if (!depthTexture_ || depthTexture_->GetCreateInfo().extent != rtExtent)
    {
      depthTexture_ = Fvog::CreateTexture2D(*device_, {rtExtent.width, rtExtent.height}, Fvog::Format::D32_SFLOAT, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Forward Depth Texture");
    }

    if (!pipeline_ || lastRenderTargetFormat != renderTarget.GetViewCreateInfo().format)
    {
      lastRenderTargetFormat = renderTarget.GetViewCreateInfo().format;

      pipeline_.emplace(*device_,
        Fvog::GraphicsPipelineInfo{
          .name           = "Forward Pipeline",
          .vertexShader   = &vertexShader_.value(),
          .fragmentShader = &fragmentShader_.value(),
          .depthState =
            {
              .depthTestEnable  = true,
              .depthWriteEnable = true,
              .depthCompareOp   = VK_COMPARE_OP_GREATER,
            },
          .renderTargetFormats =
            {
              .colorAttachmentFormats = {{lastRenderTargetFormat}},
              .depthAttachmentFormat  = depthTexture_->GetCreateInfo().format,
            },
        });
    }

    ctx.Barrier();
    ctx.ImageBarrierDiscard(depthTexture_.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

    const auto colorAttachment = Fvog::RenderColorAttachment{
      .texture    = renderTarget,
      .loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .clearValue = {.1f, .3f, .5f, 0.0f},
    };

    ctx.BeginRendering(Fvog::RenderInfo{
      .name = "Forward Renderer",
      .colorAttachments  = {{colorAttachment}},
      .depthAttachment   = Fvog::RenderDepthStencilAttachment{
        .texture = depthTexture_.value().ImageView(),
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .clearValue = {.depth = 0.0f},
      },
    });

    ctx.BindGraphicsPipeline(pipeline_.value());

    for (const auto& draw : draws_)
    {
      // Extremely efficient: make a buffer for every draw
      uniformBuffer_.emplace(*device_, Fvog::TypedBufferCreateInfo{.count = 1, .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE}, "Forward Uniform Buffer");

      auto uniforms = Uniforms{
        .clipFromWorld       = view.clipFromWorld,
        .worldFromObject     = draw.worldFromObject,
        .vertexBufferAddress = draw.vertexBufferAddress,
      };
      std::memcpy(uniformBuffer_.value().GetMappedMemory(), &uniforms, sizeof(Uniforms));

      ctx.BindIndexBuffer(*draw.indexBuffer, draw.indexBufferOffset, VK_INDEX_TYPE_UINT32);
      ctx.SetPushConstants(uniformBuffer_.value().GetResourceHandle().index);
      ctx.DrawIndexed(draw.indexCount, 1, 0, 0, 0);
    }
    ctx.EndRendering();

    ctx.Barrier();

    draws_.clear();
  }
} // namespace Debug