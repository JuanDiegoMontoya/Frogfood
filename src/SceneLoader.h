#pragma once
#include <Fwog/detail/Flags.h>
#include <Fwog/Texture.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

#include <list>
#include <variant>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <filesystem>

namespace Utility
{
  struct Vertex
  {
    glm::vec3 position;
    uint32_t normal;
    glm::vec2 texcoord;
  };

  using index_t = uint32_t;

  struct Box3D
  {
    glm::vec3 min;
    glm::vec3 max;
  };

  struct CombinedTextureSampler
  {
    Fwog::TextureView texture;
    Fwog::SamplerState sampler;
  };

  enum class MaterialFlagBit
  {
    HAS_BASE_COLOR_TEXTURE         = 1 << 0,
    HAS_METALLIC_ROUGHNESS_TEXTURE = 1 << 1,
    HAS_NORMAL_TEXTURE             = 1 << 2,
    HAS_OCCLUSION_TEXTURE          = 1 << 3,
    HAS_EMISSION_TEXTURE           = 1 << 4,
  };
  FWOG_DECLARE_FLAG_TYPE(MaterialFlags, MaterialFlagBit, uint32_t)

  struct GpuMaterial
  {
    MaterialFlags flags{};
    float alphaCutoff{};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    glm::vec4 baseColorFactor = {1, 1, 1, 1};
    glm::vec3 emissiveFactor = {0, 0, 0};
    float emissiveStrength = 1.0f;
    uint64_t baseColorTextureHandle{};
    float normalXyScale = 1.0f;
    uint32_t _padding[1];
  };

  enum class LightType : uint32_t
  {
    DIRECTIONAL = 0,
    POINT       = 1,
    SPOT        = 2
  };
  struct GpuLight
  {
    glm::vec3 color;
    LightType type;
    glm::vec3 direction;  // Directional and spot only
    // Point and spot lights use candela (lm/sr) while directional use lux (lm/m^2)
    float intensity;
    glm::vec3 position;   // Point and spot only
    float range;          // Point and spot only
    float innerConeAngle; // Spot only
    float outerConeAngle; // Spot only
    uint32_t _padding[2];
  };

  struct Material
  {
    GpuMaterial gpuMaterial{};
    std::optional<CombinedTextureSampler> albedoTextureSampler;
    std::optional<CombinedTextureSampler> metallicRoughnessTextureSampler;
    std::optional<CombinedTextureSampler> normalTextureSampler;
    std::optional<CombinedTextureSampler> occlusionTextureSampler;
    std::optional<CombinedTextureSampler> emissiveTextureSampler;
  };

  struct Meshlet
  {
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t primitiveOffset = 0;
    uint32_t indexCount = 0;
    uint32_t primitiveCount = 0;
    uint32_t materialId = 0;
    uint32_t instanceId = 0;
    float aabbMin[3] = {};
    float aabbMax[3] = {};
  };

  struct Node
  {
    std::string name;
    glm::mat4 localTransform;
    std::vector<Node*> children;
    std::vector<Meshlet> meshlets;
    std::optional<GpuLight> light; // TODO: hold a light without position/direction type safety
  };

  struct SceneFlattened
  {
    std::vector<Meshlet> meshlets;
    std::vector<glm::mat4> transforms;
    std::vector<GpuLight> lights;
  };

  struct SceneMeshlet
  {
    SceneFlattened Flatten() const;

    std::vector<Node*> rootNodes;
    std::list<Node> nodes;
    std::vector<Vertex> vertices;
    std::vector<index_t> indices;
    std::vector<uint8_t> primitives;
    std::vector<Material> materials;
  };
  
  // TODO: maybe customizeable (not recommended though)
  inline constexpr auto maxMeshletIndices = 64u;
  inline constexpr auto maxMeshletPrimitives = 64u;
  inline constexpr auto meshletConeWeight = 0.0f;

  bool LoadModelFromFileMeshlet(SceneMeshlet& scene, const std::filesystem::path& fileName, glm::mat4 rootTransform, bool binary);
}