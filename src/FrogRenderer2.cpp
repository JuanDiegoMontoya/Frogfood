#include "FrogRenderer2.h"

#include "Pipelines2.h"

#include "Fvog/Rendering2.h"
#include "Fvog/Shader2.h"
#include "Fvog/detail/Common.h"
using namespace Fvog::detail;

#include "MathUtilities.h"

#include <stb_image.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <tracy/Tracy.hpp>

static const char* gVertexSource = R"(
#version 460 core

const vec2 positions[4] = {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}};
const vec2 texcoords[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
const uint indices[6] = {0, 2, 1, 0, 3, 2};

layout(location = 0) out vec2 v_texcoord;

void main()
{
  uint index = indices[gl_VertexIndex];
  v_texcoord = texcoords[index];
  gl_Position = vec4(positions[index], 0.0, 1.0);
}
)";

static const char* gFragmentSource = R"(
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : require

layout(buffer_reference, scalar) buffer ColorBuffer
{
  vec4 color;
};

layout(set = 0, binding = 0) buffer TestBuffers
{
  ColorBuffer colorBuffer;
}buffers[];

layout(push_constant) uniform PushConstants
{
  uint bufferIndex;
  uint samplerIndex;
}pushConstants;

layout(set = 0, binding = 1) uniform sampler2D samplers[];

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 o_color;

void main()
{
  vec4 bufferColor = buffers[pushConstants.bufferIndex].colorBuffer.color;
  vec4 samplerColor = texture(samplers[pushConstants.samplerIndex], v_texcoord);
  samplerColor.a = 1;
  o_color = bufferColor * samplerColor;
}
)";

FrogRenderer2::FrogRenderer2(const Application::CreateInfo& createInfo)
  : Application(createInfo),
    // Create constant-size buffers
    // TODO: Don't abuse the comma operator here. This is awful
    globalUniformsBuffer((Pipelines2::InitPipelineLayout(device_->device_, device_->descriptorSetLayout_), *device_)),
    shadingUniformsBuffer(*device_),
    shadowUniformsBuffer(*device_),
    // Create the pipelines used in the application
    cullMeshletsPipeline(Pipelines2::CullMeshlets(device_->device_)),
    cullTrianglesPipeline(Pipelines2::CullTriangles(device_->device_)),
    hzbCopyPipeline(Pipelines2::HzbCopy(device_->device_)),
    hzbReducePipeline(Pipelines2::HzbReduce(device_->device_)),
    visbufferPipeline(Pipelines2::Visbuffer(device_->device_,
                                            {
                                              .colorAttachmentFormats = {{Frame::visbufferFormat}},
                                              .depthAttachmentFormat = Frame::gDepthFormat,
                                            })),
    materialDepthPipeline(Pipelines2::MaterialDepth(device_->device_,
                                                    {
                                                      .depthAttachmentFormat = Frame::materialDepthFormat,
                                                    })),
    visbufferResolvePipeline(Pipelines2::VisbufferResolve(device_->device_,
                                                          {
                                                            .colorAttachmentFormats = {
                                                              {
                                                                Frame::gAlbedoFormat,
                                                                Frame::gMetallicRoughnessAoFormat,
                                                                Frame::gNormalAndFaceNormalFormat,
                                                                Frame::gSmoothVertexNormalFormat,
                                                                Frame::gEmissionFormat,
                                                                Frame::gMotionFormat,
                                                              }},
                                                            .depthAttachmentFormat = Frame::materialDepthFormat,
                                                          })),
    shadingPipeline(Pipelines2::Shading(device_->device_,
                                        {
                                          .colorAttachmentFormats = {{Frame::colorHdrRenderResFormat}},
                                        })),
    tonemapPipeline(Pipelines2::Tonemap(device_->device_)),
    tonemapUniformBuffer(*device_)
    //debugTexturePipeline(Pipelines2::DebugTexture(device_->device_)),
    //debugLinesPipeline(Pipelines2::DebugLines(device_->device_)),
    //debugAabbsPipeline(Pipelines2::DebugAabbs(device_->device_)),
    //debugRectsPipeline(Pipelines2::DebugRects(device_->device_))
{
  ZoneScoped;
  CheckVkResult(
    vkCreatePipelineLayout(
      device_->device_,
      Address(VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &device_->descriptorSetLayout_,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = Address(VkPushConstantRange{
          .stageFlags = VK_SHADER_STAGE_ALL,
          .offset = 0,
          .size = 64,
        }),
      }),
      nullptr,
      &pipelineLayout));

  const auto vertexShader = Fvog::Shader(device_->device_, Fvog::PipelineStage::VERTEX_SHADER, gVertexSource);
  const auto fragmentShader = Fvog::Shader(device_->device_, Fvog::PipelineStage::FRAGMENT_SHADER, gFragmentSource);
  const auto renderTargetFormats = {Fvog::Format::B8G8R8A8_SRGB};
  pipeline = Fvog::GraphicsPipeline(device_->device_, pipelineLayout, {
    .vertexShader = &vertexShader,
    .fragmentShader = &fragmentShader,
    .renderTargetFormats = {.colorAttachmentFormats = renderTargetFormats},
  });

  //Utility::LoadModelFromFileMeshlet(*device_, scene, "models/simple_scene.glb", glm::scale(glm::vec3{.5}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf", glm::scale(glm::vec3{.5}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/Main/NewSponza_Main_Blender_glTF.gltf", glm::scale(glm::vec3{1}));

  int x{};
  int y{};
  auto* imageData = stbi_load("textures/bluenoise32.png", &x, &y, nullptr, 4);
  assert(imageData);

  testSampledTexture.emplace(device_.value(), Fvog::TextureCreateInfo{
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = Fvog::Format::R8G8B8A8_SRGB,
    .extent = {(uint32_t)x, (uint32_t)y, 1},
    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
  });

  testUploadBuffer = Fvog::Buffer(*device_, {.size = uint32_t(x * y * 4), .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE});
  memcpy(testUploadBuffer->GetMappedMemory(), imageData, x * y * 4);

  stbi_image_free(imageData);

  device_->ImmediateSubmit(
    [this](VkCommandBuffer commandBuffer)
    {
      auto ctx = Fvog::Context(commandBuffer);
      ctx.ImageBarrier(*testSampledTexture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdCopyBufferToImage2(commandBuffer, Address(VkCopyBufferToImageInfo2{
        .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
        .srcBuffer = testUploadBuffer->Handle(),
        .dstImage = testSampledTexture->Image(),
        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .regionCount = 1,
        .pRegions = Address(VkBufferImageCopy2{
          .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
          .bufferOffset = 0,
          //.bufferRowLength = testSampledTexture.GetCreateInfo().extent.width * 4,
          .imageSubresource = VkImageSubresourceLayers{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
          },
          .imageOffset = {},
          .imageExtent = testSampledTexture->GetCreateInfo().extent,
        }),
      }));
      ctx.ImageBarrier(*testSampledTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    });
  
  CheckVkResult(vkCreateSampler(device_->device_, Address(VkSamplerCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter = VK_FILTER_NEAREST,
    .minFilter = VK_FILTER_NEAREST,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
  }), nullptr, &sampler));

  updatedBuffer = Fvog::NDeviceBuffer(*device_, sizeof(glm::vec4));

  OnWindowResize(windowWidth, windowHeight);
}

FrogRenderer2::~FrogRenderer2()
{
  vkDeviceWaitIdle(device_->device_);

  Pipelines2::DestroyPipelineLayout(device_->device_);
  vkDestroyDescriptorPool(device_->device_, imguiDescriptorPool_, nullptr);
  vkDestroySampler(device_->device_, sampler, nullptr);
  vkDestroyPipelineLayout(device_->device_, pipelineLayout, nullptr);
}

void FrogRenderer2::OnWindowResize([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight)
{
  ZoneScoped;

  testTexture = Fvog::Texture(device_.value(), Fvog::TextureCreateInfo{
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = Fvog::Format::B8G8R8A8_SRGB, // TODO: get from swapchain
    .extent = {windowWidth, windowHeight, 1},
    .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
  });

  {
    renderWidth = newWidth;
    renderHeight = newHeight;
  }

  constexpr auto usageColorFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  constexpr auto usageDepthFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  
  // Visibility buffer textures
  frame.visbuffer = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::visbufferFormat, usageColorFlags, "visbuffer");
  frame.materialDepth = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::materialDepthFormat, usageDepthFlags, "materialDepth");
  {
    const uint32_t hzbWidth = Math::PreviousPower2(renderWidth);
    const uint32_t hzbHeight = Math::PreviousPower2(renderHeight);
    const uint32_t hzbMips = 1 + static_cast<uint32_t>(glm::floor(glm::log2(static_cast<float>(glm::max(hzbWidth, hzbHeight)))));
    frame.hzb = Fvog::CreateTexture2DMip(*device_, {hzbWidth, hzbHeight}, Frame::hzbFormat, hzbMips, usageColorFlags, "HZB");
  }

  // Create gbuffer textures and render info
  frame.gAlbedo = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gAlbedoFormat, usageColorFlags, "gAlbedo");
  frame.gMetallicRoughnessAo = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gMetallicRoughnessAoFormat, usageColorFlags, "gMetallicRoughnessAo");
  frame.gNormalAndFaceNormal = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gNormalAndFaceNormalFormat, usageColorFlags, "gNormalAndFaceNormal");
  frame.gSmoothVertexNormal = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gSmoothVertexNormalFormat, usageColorFlags, "gSmoothVertexNormal");
  frame.gEmission = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gEmissionFormat, usageColorFlags, "gEmission");
  frame.gDepth = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gDepthFormat, usageDepthFlags, "gDepth");
  frame.gMotion = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gMotionFormat, usageColorFlags, "gMotion");
  frame.gNormaAndFaceNormallPrev = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gNormalAndFaceNormalFormat, usageColorFlags, "gNormaAndFaceNormallPrev");
  frame.gDepthPrev = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gDepthPrevFormat, usageDepthFlags, "gDepthPrev");
  frame.colorHdrRenderRes = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::colorHdrRenderResFormat, usageColorFlags, "colorHdrRenderRes");
  frame.colorHdrWindowRes = Fvog::CreateTexture2D(*device_, {newWidth, newHeight}, Frame::colorHdrWindowResFormat, usageColorFlags, "colorHdrWindowRes");
  frame.colorHdrBloomScratchBuffer = Fvog::CreateTexture2DMip(*device_, {newWidth / 2, newHeight / 2}, Frame::colorHdrBloomScratchBufferFormat, 8, usageColorFlags, "colorHdrBloomScratchBuffer");
  frame.colorLdrWindowRes = Fvog::CreateTexture2D(*device_, {newWidth, newHeight}, Frame::colorLdrWindowResFormat, usageColorFlags, "colorLdrWindowRes");

  //// Create debug views with alpha swizzle set to one so they can be seen in ImGui
  //frame.gAlbedoSwizzled = frame.gAlbedo->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
  //frame.gRoughnessMetallicAoSwizzled = frame.gMetallicRoughnessAo->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
  //frame.gEmissionSwizzled = frame.gEmission->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
  //frame.gNormalSwizzled = frame.gNormalAndFaceNormal->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
  //frame.gDepthSwizzled = frame.gDepth->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
}

void FrogRenderer2::OnUpdate([[maybe_unused]] double dt) {}

void FrogRenderer2::OnRender([[maybe_unused]] double dt, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
  ZoneScoped;

  auto ctx = Fvog::Context(commandBuffer);
  // Holds actual data
  //auto testBuffer = Fvog::Buffer(*device_, {.size = sizeof(glm::vec4)});
  const auto testColor = glm::vec4{1.0f, 1.0f, std::sinf(device_->frameNumber / 1000.0f) * .5f + .5f, 1.0f};

  // Holds pointer to testBuffer
  auto testBuffer2 = Fvog::Buffer(*device_, {.size = sizeof(VkDeviceAddress)});
  updatedBuffer->UpdateData(commandBuffer, testColor);
  //vkCmdUpdateBuffer(commandBuffer, testBuffer.Handle(), 0, sizeof(testColor), &testColor);
  vkCmdUpdateBuffer(commandBuffer, testBuffer2.Handle(), 0, sizeof(VkDeviceAddress), &updatedBuffer->GetDeviceBuffer().GetDeviceAddress());

  ctx.BufferBarrier(updatedBuffer->GetDeviceBuffer());

  ctx.ImageBarrier(*testTexture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  auto colorAttachment = Fvog::RenderColorAttachment{
    .texture = *testTexture,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    //.clearValue = {1.0f, 1.0f, std::sinf(device_->frameNumber / 1000.0f) * .5f + .5f, 1.0f},
    .clearValue = {0.1f, 0.1f, 0.1f, 1.0f},
  };
  ctx.BeginRendering({
    .colorAttachments = {&colorAttachment, 1},
  });

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &device_->descriptorSet_, 0, nullptr);
  struct
  {
    uint32_t bufferIndex;
    uint32_t samplerIndex;
  }indices{};
  const auto descriptorInfo = device_->AllocateStorageBufferDescriptor(testBuffer2.Handle());
  const auto descriptorInfo2 = device_->AllocateCombinedImageSamplerDescriptor(sampler, testSampledTexture->ImageView(), VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  indices.bufferIndex = descriptorInfo.GpuResource().index;
  indices.samplerIndex = descriptorInfo2.GpuResource().index;
  vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(indices), &indices);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->Handle());
  vkCmdDraw(commandBuffer, 6, 1, 0, 0);

  ctx.EndRendering();

  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  ctx.ImageBarrier(*testTexture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  vkCmdBlitImage2(commandBuffer, Address(VkBlitImageInfo2{
    .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
    .srcImage = testTexture->Image(),
    .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    .dstImage = swapchainImages_[swapchainImageIndex],
    .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    .regionCount = 1,
    .pRegions = Address(VkImageBlit2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
      .srcSubresource = VkImageSubresourceLayers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
      .srcOffsets = {{}, {(int)testTexture->GetCreateInfo().extent.width, (int)testTexture->GetCreateInfo().extent.height, 1}},
      .dstSubresource = VkImageSubresourceLayers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
      .dstOffsets = {{}, {(int)windowWidth, (int)windowHeight, 1}},
    }),
    .filter = VK_FILTER_NEAREST,
  }));

  // ugh
  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void FrogRenderer2::OnGui([[maybe_unused]] double dt)
{
  ImGui::Begin("test");
  ImGui::Text("%.1fHz", 1.0 / dt);
  ImGui::End();
}

void FrogRenderer2::OnPathDrop([[maybe_unused]] std::span<const char*> paths)
{
  
}
