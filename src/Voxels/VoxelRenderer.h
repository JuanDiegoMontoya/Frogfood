#pragma once
#include "Fvog/Buffer2.h"
#include "Fvog/Texture2.h"
#include "PipelineManager.h"
#include "PlayerHead.h"
#include "TwoLevelGrid.h"

#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "glm/ext/matrix_transform.hpp"

#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace Temp
{
  struct Uniforms
  {
    glm::mat4 viewProj;
    glm::mat4 oldViewProjUnjittered;
    glm::mat4 viewProjUnjittered;
    glm::mat4 invViewProj;
    glm::mat4 proj;
    glm::mat4 invProj;
    glm::vec4 cameraPos;
    glm::uint meshletCount;
    glm::uint maxIndices;
    float bindlessSamplerLodBias;
    glm::uint flags;
    float alphaHashScale;
  };

  FVOG_DECLARE_ARGUMENTS(PushConstants)
  {
    FVOG_IVEC3 topLevelBricksDims;
    FVOG_UINT32 topLevelBrickPtrsBaseIndex;
    FVOG_IVEC3 dimensions;
    FVOG_UINT32 bufferIdx;
    FVOG_UINT32 uniformBufferIndex;
    shared::Image2D outputImage;
  };

  FVOG_DECLARE_ARGUMENTS(DebugTextureArguments)
  {
    FVOG_UINT32 textureIndex;
    FVOG_UINT32 samplerIndex;
  };

  FVOG_DECLARE_ARGUMENTS(MeshArgs)
  {
    VkDeviceAddress objects;
    VkDeviceAddress frame;
  };

  struct ObjectUniforms
  {
    glm::mat4 worldFromObject;
    VkDeviceAddress vertexBuffer;
  };

} // namespace Temp

class VoxelRenderer
{
public:

  explicit VoxelRenderer(PlayerHead* head, World& world);
  ~VoxelRenderer();

private:

  void OnFramebufferResize(uint32_t newWidth, uint32_t newHeight);
  void OnRender(double dt, World& world, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex);
  void OnGui(double dt, World& world, VkCommandBuffer commandBuffer);

  struct Frame
  {
    std::optional<Fvog::Texture> sceneColor;
    constexpr static Fvog::Format sceneColorFormat = Fvog::Format::R8G8B8A8_UNORM;
    std::optional<Fvog::Texture> sceneDepth;
    constexpr static Fvog::Format sceneDepthFormat = Fvog::Format::D32_SFLOAT;
  };
  Frame frame;

  TwoLevelGrid grid{{2, 2, 2}};
  
  Fvog::NDeviceBuffer<Temp::Uniforms> perFrameUniforms;
  PipelineManager::GraphicsPipelineKey testPipeline;
  PipelineManager::GraphicsPipelineKey meshPipeline;
  PipelineManager::GraphicsPipelineKey debugTexturePipeline;
  PlayerHead* head_;
};
