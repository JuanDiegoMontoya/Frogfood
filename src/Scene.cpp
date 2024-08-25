#include "Scene.h"

#include "FrogRenderer2.h"
#include "SceneLoader.h"

#include <tracy/Tracy.hpp>

#include "fastgltf/util.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace Scene
{
  void SceneMeshlet::Import(FrogRenderer2& renderer, Utility::LoadModelResultA loadModelResult)
  {
    ZoneScoped;
    meshGeometryIds.reserve(meshGeometryIds.size() + loadModelResult.meshGeometries.size());

    // Every mesh node creates a new mesh instance for now.
    // Also, not every node necessarily holds a mesh, so this may over-reserve.
    meshIds.reserve(meshIds.size() + loadModelResult.nodes.size());

    // Also assume that every node holds a light. These IDs are tiny.
    lightIds.reserve(lightIds.size() + loadModelResult.nodes.size());

    if (materialIds.empty())
    {
      // First material is always default.
      constexpr auto defaultGpu = Render::GpuMaterial{
        .metallicFactor  = 0,
        .baseColorFactor = {0.5f, 0.5f, 0.5f, 0.5f},
      };
      materialIds.emplace_back(renderer.RegisterMaterial({.gpuMaterial = defaultGpu}));
    }

    materialIds.reserve(materialIds.size() + loadModelResult.materials.size());

    const auto baseMeshGeometryIndex = meshGeometryIds.size();
    const auto baseMaterialIndex = materialIds.size();

    for (const auto& meshGeometry : loadModelResult.meshGeometries)
    {
      // TODO: move arrays in
      auto info = FrogRenderer2::MeshGeometryInfo{
        .meshlets        = meshGeometry.meshlets,
        .vertices        = meshGeometry.vertices,
        .remappedIndices = meshGeometry.remappedIndices,
        .primitives      = meshGeometry.primitives,
        .originalIndices = meshGeometry.originalIndices,
      };
      meshGeometryIds.push_back(renderer.RegisterMeshGeometry(info));
    }
    
    for (auto& material : loadModelResult.materials)
    {
      materialIds.push_back(renderer.RegisterMaterial(std::move(material)));
    }

    // Convert the Utility::LoadModelNode tree into a Scene::Node tree.
    struct StackElement
    {
      const Utility::LoadModelNode* node;
      bool isRootNode = false;
      Node* parent    = nullptr;
    };
    std::stack<StackElement> nodeStack;

    for (auto* rootNode : loadModelResult.rootNodes)
    {
      nodeStack.emplace(rootNode, true, nullptr);
    }

    while (!nodeStack.empty())
    {
      auto [node, isRootNode, parent] = nodeStack.top();
      nodeStack.pop();

      auto newNode = std::make_unique<Node>(Node{
        .name              = node->name,
        .translation       = node->translation,
        .rotation          = node->rotation,
        .scale             = node->scale,
        .globalTransform   = {},
        .parent            = parent,
        .children          = {},
        .isDirty           = true,
        .isDescendantDirty = true,
      });

      if (parent)
      {
        parent->children.emplace_back(newNode.get());
      }

      for (auto& [meshIndex, materialIndex] : node->meshes)
      {
        auto meshId     = meshIds.emplace_back(renderer.SpawnMesh(meshGeometryIds[baseMeshGeometryIndex + meshIndex]));
        auto materialId = materialIds[materialIndex.has_value() ? baseMaterialIndex + *materialIndex : 0];
        newNode->meshes.emplace_back(meshId, materialId);
      }

      if (node->light)
      {
        newNode->light = node->light.value();
        newNode->lightId = renderer.SpawnLight(newNode->light);
      }

      for (const auto* childNode : node->children)
      {
        nodeStack.emplace(childNode, false, newNode.get());
      }

      if (isRootNode)
      {
        rootNodes.emplace_back(newNode.get());
      }
      nodes.emplace_back(std::move(newNode));
    }

    std::ranges::move(loadModelResult.images, std::back_inserter(images));

    {
      ZoneScopedN("Free temp nodes");
      loadModelResult.nodes.clear();
    }
    {
      ZoneScopedN("Free mesh geometries");
      ZoneTextF("Geometries: %llu", loadModelResult.meshGeometries.size());
      loadModelResult.meshGeometries.clear();
    }
    {
      ZoneScopedN("Free nodes");
      loadModelResult.nodes.clear();
    }
  }

  void SceneMeshlet::CalcUpdatedData(FrogRenderer2& renderer) const
  {
    ZoneScoped;

    struct StackElement
    {
      Node* node;
      glm::mat4 parentGlobalTransform;
    };
    std::stack<StackElement> nodeStack;

    for (auto* rootNode : rootNodes)
    {
      nodeStack.emplace(rootNode, rootNode->CalcLocalTransform());
    }

    // Traverse the scene
    while (!nodeStack.empty())
    {
      auto [node, parentGlobalTransform] = nodeStack.top();
      nodeStack.pop();

      const auto globalTransform = parentGlobalTransform * node->CalcLocalTransform();

      for (auto* childNode : node->children)
      {
        // If this node is dirty, then all its children must be dirty too.
        if (node->isDirty)
        {
          childNode->isDirty = true;
        }

        if (childNode->isDirty || childNode->isDescendantDirty)
        {
          nodeStack.emplace(childNode, globalTransform);
        }
      }

      if (node->isDirty)
      {
        for (auto [meshId, materialId] : node->meshes)
        {
          const auto uniforms = Render::ObjectUniforms{
            .modelPrevious = globalTransform,
            .modelCurrent = globalTransform,
            .materialId = renderer.GetMaterialGpuIndex(materialId),
          };
          renderer.UpdateMesh(meshId, uniforms);
        }

        if (node->lightId)
        {
          auto gpuLight = node->light;

          std::array<float, 16> globalTransformArray{};
          std::copy_n(&globalTransform[0][0], 16, globalTransformArray.data());
          std::array<float, 3> scaleArray{};
          std::array<float, 4> rotationArray{};
          std::array<float, 3> translationArray{};
          fastgltf::decomposeTransformMatrix(globalTransformArray, scaleArray, rotationArray, translationArray);

          glm::quat rotation    = {rotationArray[3], rotationArray[0], rotationArray[1], rotationArray[2]};
          glm::vec3 translation = glm::make_vec3(translationArray.data());
          // We rotate (0, 0, -1) because that is the default, un-rotated direction of spot and directional lights according to the glTF spec
          gpuLight.direction = glm::normalize(rotation) * glm::vec3(0, 0, -1);
          gpuLight.position  = translation;

          renderer.UpdateLight(node->lightId, gpuLight);
        }
      }

      node->isDirty = false;
      node->isDescendantDirty = false;
    }
  }

  glm::mat4 Node::CalcLocalTransform() const noexcept
  {
    return glm::scale(glm::translate(translation) * glm::mat4_cast(rotation), scale);
  }

  void Node::DeleteLight(FrogRenderer2& renderer)
  {
    assert(lightId);
    renderer.DeleteLight(lightId);
    lightId = {0};
  }

  void Node::MarkDirty()
  {
    isDirty = true;

    // Tell ascendants that they have a dirty descendant
    auto* curParent = parent;
    while (curParent)
    {
      curParent->isDescendantDirty = true;
      curParent = curParent->parent;
    }
    // We intentionally don't mark descendants dirty here, as that's handled in CalcUpdatedData()
  }
} // namespace Scene
