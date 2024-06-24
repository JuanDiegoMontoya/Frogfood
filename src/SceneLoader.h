#pragma once
#include "Fvog/detail/Flags.h"
#include "Fvog/Texture2.h"
#include "Fvog/Device.h"

#include "shaders/Resources.h.glsl"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <string>
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
    //Fwog::TextureView texture;
    //Fwog::SamplerState sampler;

    Fvog::TextureView texture;
    //Fvog::Device::DescriptorInfo textureDescriptorInfo;
  };

  enum class MaterialFlagBit
  {
    HAS_BASE_COLOR_TEXTURE         = 1 << 0,
    HAS_METALLIC_ROUGHNESS_TEXTURE = 1 << 1,
    HAS_NORMAL_TEXTURE             = 1 << 2,
    HAS_OCCLUSION_TEXTURE          = 1 << 3,
    HAS_EMISSION_TEXTURE           = 1 << 4,
  };
  FVOG_DECLARE_FLAG_TYPE(MaterialFlags, MaterialFlagBit, uint32_t)

  struct GpuMaterial
  {
    MaterialFlags flags;
    FVOG_FLOAT alphaCutoff;
    FVOG_FLOAT metallicFactor;
    FVOG_FLOAT roughnessFactor;
    FVOG_VEC4 baseColorFactor;
    FVOG_VEC3 emissiveFactor;
    FVOG_FLOAT emissiveStrength;
    FVOG_FLOAT normalXyScale;
    FVOG_UINT32 baseColorTextureIndex;
    FVOG_UINT32 metallicRoughnessTextureIndex;
    FVOG_UINT32 normalTextureIndex;
    FVOG_UINT32 occlusionTextureIndex;
    FVOG_UINT32 emissionTextureIndex;
    FVOG_UINT32 _padding[2];
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
    //uint32_t instanceId = 0;
    float aabbMin[3] = {};
    float aabbMax[3] = {};
  };

  struct MeshletInstance
  {
    uint32_t meshletId;
    uint32_t instanceId;
    uint32_t materialId = 0;
  };

  struct ObjectUniforms
  {
    glm::mat4 modelPrevious;
    glm::mat4 modelCurrent;
  };

  struct Node
  {
    std::string name;

    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;

    glm::mat4 CalcLocalTransform() const noexcept;

    glm::mat4 globalTransform;
    glm::vec3 globalAabbMin;
    glm::vec3 globalAabbMax;

    std::vector<Node*> children;
    //std::vector<Meshlet> meshlets;

    // A list of meshlets (minus their transform ID), which are stored in the scene
    std::vector<MeshletInstance> meshletInstances;
    std::optional<GpuLight> light; // TODO: hold a light without position/direction type safety
  };

  struct SceneFlattened
  {
    //std::vector<uint32_t> instanceMeshIndices;
    std::vector<MeshletInstance> meshletInstances;
    std::vector<ObjectUniforms> transforms;
    std::vector<GpuLight> lights;
  };

  struct SceneMeshlet
  {
    SceneFlattened Flatten() const;

    std::vector<Node*> rootNodes;
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Meshlet> meshlets;
    std::vector<Vertex> vertices;
    std::vector<index_t> indices;
    std::vector<uint8_t> primitives;
    std::vector<Material> materials;
    std::vector<Fvog::Texture> images;

  private:
    mutable size_t previousMeshletsSize{};
    mutable size_t previousTransformsSize{};
    mutable size_t previousLightsSize{};
  };
  
  // TODO: maybe customizeable (not recommended though)
  inline constexpr auto maxMeshletIndices = 64u;
  inline constexpr auto maxMeshletPrimitives = 64u;
  inline constexpr auto meshletConeWeight = 0.0f;

  bool LoadModelFromFileMeshlet(Fvog::Device& device, SceneMeshlet& scene, const std::filesystem::path& fileName, glm::mat4 rootTransform);
}