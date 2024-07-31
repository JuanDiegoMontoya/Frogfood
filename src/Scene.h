#pragma once
#include "Fvog/Texture2.h"
#include "Fvog/Device.h"

#include "Renderables.h"

#include <glm/vec2.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <string>
#include <optional>

class FrogRenderer2;

namespace Utility
{
  struct LoadModelResultA;
}

namespace Scene
{
  struct Node
  {
    std::string name;

    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;

    [[nodiscard]] glm::mat4 CalcLocalTransform() const noexcept;

    // Horrible interface
    void DeleteLight(FrogRenderer2& renderer);

    glm::mat4 globalTransform;
    //glm::vec3 globalAabbMin;
    //glm::vec3 globalAabbMax;

    // Relationship
    Node* parent = nullptr;
    std::vector<Node*> children;
    bool isDirty           = false; // True if transform OR light data changed
    bool isDescendantDirty = false;

    // Also tells parents that a descendant is dirty.
    void MarkDirty();
    
    std::vector<Render::MeshID> meshIds;
    Render::LightID lightId;
    Render::GpuLight light; // Only contains valid data if lightId is not null
  };

  struct SceneMeshlet
  {
    void Import(FrogRenderer2& renderer, Utility::LoadModelResultA loadModelResult);

    // Epic interface
    void CalcUpdatedData(FrogRenderer2& renderer) const;

    std::vector<Node*> rootNodes;
    std::vector<std::unique_ptr<Node>> nodes;

    std::vector<Fvog::Texture> images;
    std::vector<Render::MeshGeometryID> meshGeometryIds;
    std::vector<Render::MeshInstanceID> meshInstanceIds;
    std::vector<Render::MeshID> meshIds;
    std::vector<Render::LightID> lightIds;
    std::vector<Render::MaterialID> materialIds;
  };
}
