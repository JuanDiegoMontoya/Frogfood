#pragma once
#ifdef JPH_DEBUG_RENDERER
  #include "debug/Shapes.h"

  #include "Jolt/Jolt.h"
  #include "Jolt/Renderer/DebugRendererSimple.h"

  #include <vector>

namespace Fvog
{
  class Texture;
}

namespace Physics
{
  class DebugRenderer : public JPH::DebugRendererSimple
  {
  public:
    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;

    void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight) override;

    const auto& GetLines() const
    {
      return lines_;
    }

    void ClearPrimitives()
    {
      lines_.clear();
    }

  private:
    std::vector<Debug::Line> lines_;
  };
} // namespace Physics
#endif
