#pragma once
#include "Renderables.h"

#include "Fvog/Texture2.h"

#include <glm/gtc/quaternion.hpp>

#include <filesystem>
#include <vector>

namespace Utility
{
  struct MeshGeometry
  {
    std::vector<Render::Meshlet> meshlets;
    std::vector<Render::Vertex> vertices;
    std::vector<Render::index_t> indices; // meshletIndices
    std::vector<Render::primitive_t> primitives;
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
      size_t materialIndex;
    };
    std::vector<MeshIndices> meshes;
    std::optional<Render::GpuLight> light; // TODO: hold a light without position/direction type safety
  };

  struct LoadModelResultA
  {
    // These nodes are a different type that refer to not-yet-uploaded
    // resources. These nodes contain indices into the various other
    // buffers this struct holds, and should be trivially convertible to
    // actual scene nodes.
    std::vector<LoadModelNode*> rootNodes;
    std::vector<std::unique_ptr<LoadModelNode>> nodes;

    std::vector<MeshGeometry> meshGeometries;
    std::vector<Render::Material> materials;
    std::vector<Fvog::Texture> images;
  };

  // TODO: maybe customizeable (not recommended though)
  inline constexpr auto maxMeshletIndices = 64u;
  inline constexpr auto maxMeshletPrimitives = 64u;
  inline constexpr auto meshletConeWeight = 0.0f;

  [[nodiscard]] LoadModelResultA LoadModelFromFileMeshlet(Fvog::Device& device,
    const std::filesystem::path& fileName,
    glm::mat4 rootTransform,
    bool skipMaterials = false);
}