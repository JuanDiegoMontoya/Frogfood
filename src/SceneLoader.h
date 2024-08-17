#pragma once
#include "Renderables.h"
#include "shaders/ShadeDeferredPbr.h.glsl"

#include "Fvog/Texture2.h"

#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <vector>
#include <memory_resource>

namespace Utility
{
  struct MeshGeometry
  {
    std::pmr::vector<Render::Meshlet> meshlets;
    std::pmr::vector<Render::Vertex> vertices;
    std::pmr::vector<Render::index_t> indices; // meshletIndices
    std::pmr::vector<Render::primitive_t> primitives;
  };

  struct LoadModelNode
  {
    std::string name;

    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;

    [[nodiscard]] glm::mat4 CalcLocalTransform() const noexcept;

    // Relationship
    std::vector<LoadModelNode*> children;

    // A list of meshlets (minus their transform ID), which are stored in the scene
    struct MeshIndices
    {
      size_t meshIndex;
      std::optional<size_t> materialIndex;
    };
    std::vector<MeshIndices> meshes;
    std::optional<GpuLight> light; // TODO: hold a light without position/direction type safety
  };

  struct LoadModelResultA
  {
    // These nodes are a different type that refer to not-yet-uploaded
    // resources. These nodes contain indices into the various other
    // buffers this struct holds, and should be trivially convertible to
    // actual scene nodes.
    std::pmr::vector<LoadModelNode*> rootNodes;
    std::pmr::vector<std::unique_ptr<LoadModelNode>> nodes;

    std::pmr::vector<MeshGeometry> meshGeometries;
    std::pmr::vector<Render::Material> materials;
    std::vector<Fvog::Texture> images;
  };

  // TODO: maybe customizeable (not recommended though)
  inline constexpr auto maxMeshletIndices = 64u;
  inline constexpr auto maxMeshletPrimitives = 64u;
  inline constexpr auto meshletConeWeight = 0.0f;

  [[nodiscard]] LoadModelResultA LoadModelFromFile(Fvog::Device& device,
    const std::filesystem::path& fileName,
    const glm::mat4& rootTransform,
    bool skipMaterials = false);
}