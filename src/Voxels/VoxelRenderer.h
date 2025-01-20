#pragma once
#include "Fvog/Buffer2.h"
#include "Fvog/Texture2.h"
#include "PipelineManager.h"
#include "PlayerHead.h"
#include "TwoLevelGrid.h"
#include "debug/Shapes.h"
#include "techniques/denoising/spatial/Bilateral.h"

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "glm/ext/matrix_transform.hpp"

#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string>

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

  struct Voxels
  {
    FVOG_IVEC3 topLevelBricksDims;
    FVOG_UINT32 topLevelBrickPtrsBaseIndex;
    FVOG_IVEC3 dimensions;
    FVOG_UINT32 bufferIdx;
    FVOG_UINT32 materialBufferIdx;
    shared::Sampler voxelSampler;
  };

  FVOG_DECLARE_ARGUMENTS(PushConstants)
  {
    Voxels voxels;
    FVOG_UINT32 uniformBufferIndex;
    shared::Texture2D noiseTexture;
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
    Voxels voxels;
    shared::Texture2D noiseTexture;
  };

  struct BillboardInstance
  {
    glm::vec3 position;
    glm::vec2 scale;
    glm::vec4 leftColor;
    glm::vec4 rightColor;
    float middle;
  };

  struct ObjectUniforms
  {
    glm::mat4 worldFromObject;
    VkDeviceAddress vertexBuffer;
  };
} // namespace "Temp"

class VoxelRenderer
{
public:

  explicit VoxelRenderer(PlayerHead* head, World& world);
  ~VoxelRenderer();

private:

  void OnFramebufferResize(uint32_t newWidth, uint32_t newHeight);
  void OnRender(double dt, World& world, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex);
  void OnGui(DeltaTime dt, World& world, VkCommandBuffer commandBuffer);

  struct Frame
  {
    std::optional<Fvog::Texture> sceneAlbedo;
    constexpr static Fvog::Format sceneAlbedoFormat = Fvog::Format::R8G8B8A8_SRGB;
    std::optional<Fvog::Texture> sceneNormal;
    constexpr static Fvog::Format sceneNormalFormat = Fvog::Format::R16G16B16A16_SNORM; // TODO: should be oct
    std::optional<Fvog::Texture> sceneIlluminance;
    std::optional<Fvog::Texture> sceneIlluminancePingPong;
    constexpr static Fvog::Format sceneIlluminanceFormat = Fvog::Format::R16G16B16A16_SFLOAT;
    std::optional<Fvog::Texture> sceneColor;
    constexpr static Fvog::Format sceneColorFormat = Fvog::Format::R8G8B8A8_UNORM;
    std::optional<Fvog::Texture> sceneDepth;
    constexpr static Fvog::Format sceneDepthFormat = Fvog::Format::D32_SFLOAT;
  };
  Frame frame;

  Techniques::Bilateral bilateral_;
  Fvog::NDeviceBuffer<Temp::Uniforms> perFrameUniforms;
  PipelineManager::GraphicsPipelineKey testPipeline;
  PipelineManager::GraphicsPipelineKey meshPipeline;
  PipelineManager::GraphicsPipelineKey debugTexturePipeline;
  PipelineManager::GraphicsPipelineKey debugLinesPipeline;
  PipelineManager::GraphicsPipelineKey billboardsPipeline;
  std::optional<Fvog::NDeviceBuffer<Debug::Line>> lineVertexBuffer;
  std::optional<Fvog::NDeviceBuffer<Temp::BillboardInstance>> billboardInstanceBuffer;
  std::optional<Fvog::Buffer> voxelMaterialBuffer;
  std::optional<Fvog::Texture> noiseTexture;
  std::unordered_map<std::string, Fvog::Texture> stringToTexture;
  PlayerHead* head_;
};
