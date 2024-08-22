#pragma once

#include "Fvog/detail/Flags.h"
#include "Fvog/Texture2.h"

#include "shaders/Resources.h.glsl"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>

#include <optional>

namespace Render
{
  // TODO: split this into separate streams
  struct Vertex
  {
    glm::vec3 position;
    uint32_t normal;
    glm::vec2 texcoord;
  };

  using index_t = uint32_t;
  using primitive_t = uint8_t;

  struct Box3D
  {
    glm::vec3 min;
    glm::vec3 max;
  };

  struct CombinedTextureSampler
  {
    // Fwog::TextureView texture;
    // Fwog::SamplerState sampler;

    Fvog::TextureView texture;
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
    bool operator==(const GpuMaterial&) const noexcept = default;

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

  struct Material
  {
    bool operator==(const Material& other) const noexcept
    {
      return gpuMaterial == other.gpuMaterial;
    }

    GpuMaterial gpuMaterial{};
    std::optional<CombinedTextureSampler> albedoTextureSampler;
    std::optional<CombinedTextureSampler> metallicRoughnessTextureSampler;
    std::optional<CombinedTextureSampler> normalTextureSampler;
    std::optional<CombinedTextureSampler> occlusionTextureSampler;
    std::optional<CombinedTextureSampler> emissiveTextureSampler;
  };

  struct Meshlet
  {
    uint32_t vertexOffset    = 0;
    uint32_t indexOffset     = 0;
    uint32_t primitiveOffset = 0;
    uint32_t indexCount      = 0;
    uint32_t primitiveCount  = 0;
    float aabbMin[3]         = {};
    float aabbMax[3]         = {};
  };

  struct MeshletInstance
  {
    uint32_t meshletId;
    uint32_t instanceId; // For internal use only
  };

  struct ObjectUniforms
  {
    bool operator==(const ObjectUniforms&) const noexcept = default;
    glm::mat4 modelPrevious;
    glm::mat4 modelCurrent;
    uint32_t materialId = 0;
    uint32_t _padding[3];
  };

  // The ID structs below this line mainly exist in this file as a hack to prevent
  // a circular dependency between FrogRenderer2.h and Scene.h.
  struct MeshGeometryID
  {
    explicit operator bool() const noexcept
    {
      return id != 0;
    }
    uint64_t id{};
  };

  struct MaterialID
  {
    explicit operator bool() const noexcept
    {
      return id != 0;
    }
    uint64_t id{};
  };

  struct MeshID
  {
    explicit operator bool() const noexcept
    {
      return id != 0;
    }
    uint64_t id{};
  };

  struct LightID
  {
    explicit operator bool() const noexcept
    {
      return id != 0;
    }
    uint64_t id{};
  };
}