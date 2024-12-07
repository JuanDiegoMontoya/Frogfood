#include "DebugRenderer.h"
#include "PhysicsUtils.h"

#include <cassert>

namespace Physics
{
  void DebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
  {
    lines_.emplace_back(Debug::Line{
      .aPosition = ToGlm(inFrom),
      .aColor    = ToGlm(inColor.ToVec4()),
      .bPosition = ToGlm(inTo),
      .bColor    = ToGlm(inColor.ToVec4()),
    });
  }

  void DebugRenderer::DrawText3D([[maybe_unused]] JPH::RVec3Arg inPosition,
    [[maybe_unused]] const std::string_view& inString,
    [[maybe_unused]] JPH::ColorArg inColor,
    [[maybe_unused]] float inHeight)
  {
    assert(false);
  }
} // namespace Physics
