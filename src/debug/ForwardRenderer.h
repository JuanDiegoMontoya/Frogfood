#pragma once
#include "Fvog/Buffer2.h"
#include "Fvog/Pipeline2.h"
#include "Fvog/Shader2.h"
#include "Fvog/Texture2.h"

#include <glm/mat4x4.hpp>

#include <vulkan/vulkan_core.h>

#include <optional>

namespace Fvog
{
  class Device;
}

namespace Debug
{
  class ForwardRenderer
  {
  public:
    explicit ForwardRenderer(Fvog::Device& device);

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    ForwardRenderer(ForwardRenderer&&) noexcept = default;
    ForwardRenderer& operator=(ForwardRenderer&&) noexcept = default;

    struct Drawable
    {
      VkDeviceAddress vertexBufferAddress{};
      Fvog::Buffer* indexBuffer{};
      VkDeviceSize indexBufferOffset{};
      uint32_t indexCount;
      glm::mat4 worldFromObject{};
      uint32_t materialId{};
    };

    struct ViewParams
    {
      glm::mat4 clipFromWorld; // viewProj
    };

    void PushDraw(const Drawable& draw);
    void FlushAndRender(VkCommandBuffer commandBuffer, const ViewParams& view, const Fvog::TextureView& renderTarget, Fvog::Buffer& materialBuffer);

  private:
    struct Uniforms
    {
      glm::mat4 clipFromWorld;
      glm::mat4 worldFromObject;
      VkDeviceAddress vertexBufferAddress;
      uint32_t materialId;
      uint32_t materialBufferIndex;
      uint32_t samplerIndex;
    };

    Fvog::Device* device_{};

    // Pipeline is recreated if last RT format doesn't match
    Fvog::Format lastRenderTargetFormat = Fvog::Format::UNDEFINED;
    std::optional<Fvog::GraphicsPipeline> pipeline_;
    std::optional<Fvog::Shader> vertexShader_;
    std::optional<Fvog::Shader> fragmentShader_;
    std::optional<Fvog::Texture> depthTexture_;
    std::optional<Fvog::TypedBuffer<Uniforms>> uniformBuffer_;

    std::vector<Drawable> draws_;
  };
} // namespace Debug