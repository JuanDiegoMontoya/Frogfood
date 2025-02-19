#include "Reflection.h"

#include "imgui.h"

#include <type_traits>

namespace Core::Reflection
{
  template<typename Scalar>
  consteval ImGuiDataType ScalarToImGuiDataType()
  {
    if constexpr (std::is_same_v<Scalar, int8_t>)
    {
      return ImGuiDataType_S8;
    }
    if constexpr (std::is_same_v<Scalar, uint8_t>)
    {
      return ImGuiDataType_U8;
    }
    if constexpr (std::is_same_v<Scalar, int16_t>)
    {
      return ImGuiDataType_S16;
    }
    if constexpr (std::is_same_v<Scalar, uint16_t>)
    {
      return ImGuiDataType_U16;
    }
    if constexpr (std::is_same_v<Scalar, int32_t>)
    {
      return ImGuiDataType_S32;
    }
    if constexpr (std::is_same_v<Scalar, uint32_t>)
    {
      return ImGuiDataType_U32;
    }
    if constexpr (std::is_same_v<Scalar, int64_t>)
    {
      return ImGuiDataType_S64;
    }
    if constexpr (std::is_same_v<Scalar, uint64_t>)
    {
      return ImGuiDataType_U64;
    }
    if constexpr (std::is_same_v<Scalar, float>)
    {
      return ImGuiDataType_Float;
    }
    if constexpr (std::is_same_v<Scalar, double>)
    {
      return ImGuiDataType_Double;
    }

    throw "Error: unsupported type";
  }

  template<typename Scalar>
  consteval const char* ScalarToFormatString()
  {
    if constexpr (std::is_same_v<Scalar, bool>)
    {
      return "%d";
    }
    if constexpr (std::is_same_v<Scalar, int8_t>)
    {
      return "%d";
    }
    if constexpr (std::is_same_v<Scalar, uint8_t>)
    {
      return "%u";
    }
    if constexpr (std::is_same_v<Scalar, int16_t>)
    {
      return "%d";
    }
    if constexpr (std::is_same_v<Scalar, uint16_t>)
    {
      return "%u";
    }
    if constexpr (std::is_same_v<Scalar, int32_t>)
    {
      return "%d";
    }
    if constexpr (std::is_same_v<Scalar, uint32_t>)
    {
      return "%u";
    }
    if constexpr (std::is_same_v<Scalar, int64_t>)
    {
      return "%lld";
    }
    if constexpr (std::is_same_v<Scalar, uint64_t>)
    {
      return "%llu";
    }
    if constexpr (std::is_same_v<Scalar, float>)
    {
      return "%.3f";
    }
    if constexpr (std::is_same_v<Scalar, double>)
    {
      return "%.3f";
    }

    throw "Error: unsupported type";
  }

  static void GetEditorName(const char*& label, const PropertiesMap& properties)
  {
    if (auto it = properties.find("name"_hs); it != properties.end())
    {
      label = *it->second.try_cast<const char*>();
    }
  }

  template<typename Scalar>
  static void InitEditorScalarParams(const PropertiesMap& properties, const char*& label, Scalar& min, Scalar& max, Scalar& speed)
  {
    GetEditorName(label, properties);
    if (auto it = properties.find("min"_hs); it != properties.end())
    {
      min = *it->second.try_cast<Scalar>();
    }
    if (auto it = properties.find("max"_hs); it != properties.end())
    {
      max = *it->second.try_cast<Scalar>();
    }
    if (auto it = properties.find("speed"_hs); it != properties.end())
    {
      speed = *it->second.try_cast<Scalar>();
    }
  }

  template<typename Scalar>
  static bool EditorWriteScalar(Scalar& f, const PropertiesMap& properties)
  {
    const char* label = "float";
    Scalar min        = 0;
    Scalar max        = 1;
    Scalar speed      = 0;

    InitEditorScalarParams(properties, label, min, max, speed);

    if constexpr (std::is_same_v<Scalar, bool>)
    {
      return ImGui::Checkbox(label, &f);
    }
    else
    {
      if (speed <= 0)
      {
        return ImGui::SliderScalar(label, ScalarToImGuiDataType<Scalar>(), &f, &min, &max, ScalarToFormatString<Scalar>(), 0);
      }
      else
      {
        return ImGui::DragScalar(label, ScalarToImGuiDataType<Scalar>(), &f, static_cast<float>(speed), &min, &max, ScalarToFormatString<Scalar>(), 0);
      }
    }
  }

  static bool EditorWriteVec3(glm::vec3& v, const PropertiesMap& properties)
  {
    const char* label = "vec3";
    float min         = 0;
    float max         = 1;
    float speed       = 0;

    InitEditorScalarParams(properties, label, min, max, speed);

    if (speed <= 0)
    {
      return ImGui::SliderFloat3(label, &v[0], min, max);
    }
    else
    {
      return ImGui::DragFloat3(label, &v[0], speed);
    }
  }

  static bool EditorWriteQuat(glm::quat& q, const PropertiesMap& properties)
  {
    const char* label = "quat";
    float min         = -180;
    float max         = 180;
    float speed       = 0;

    InitEditorScalarParams(properties, label, min, max, speed);

    auto euler = glm::degrees(glm::eulerAngles(q));

    bool changed = false;
    if (speed <= 0)
    {
      changed = ImGui::SliderFloat3(label, &euler[0], min, max);
    }
    else
    {
      changed = ImGui::DragFloat3(label, &euler[0], speed);
    }

    if (changed)
    {
      q = glm::quat(glm::radians(euler));
    }

    return changed;
  }

  template<typename Scalar>
  static void EditorReadScalar(Scalar s, const PropertiesMap& properties)
  {
    const char* label = "scalar";
    GetEditorName(label, properties);
    ImGui::Text((std::string("%s: ") + ScalarToFormatString<Scalar>()).c_str(), label, s);
  }

  static void EditorReadVec3(glm::vec3 v, const PropertiesMap& properties)
  {
    const char* label = "vec3";
    GetEditorName(label, properties);
    ImGui::Text("%s: %f, %f, %f", label, v.x, v.y, v.z);
  }

  static void EditorReadQuat(glm::quat q, const PropertiesMap& properties)
  {
    const char* label = "quat";
    GetEditorName(label, properties);
    ImGui::Text("%s: %f, %f, %f, %f", label, q.w, q.x, q.y, q.z);
  }

  static bool EditorWriteString(std::string& s, const PropertiesMap& properties)
  {
    const char* label = "string";
    GetEditorName(label, properties);
    constexpr size_t bufferSize = 256;
    char buffer[bufferSize]{};
    s.copy(buffer, bufferSize);
    if (ImGui::InputText(label, buffer, bufferSize, ImGuiInputTextFlags_EnterReturnsTrue))
    {
      s.assign(buffer, std::strlen(buffer));
      return true;
    }
    return false;
  }

  static void EditorReadString(const std::string& s, const PropertiesMap& properties)
  {
    const char* label = "string";
    GetEditorName(label, properties);
    ImGui::Text("%.*s", static_cast<int>(s.size()), s.c_str());
  }
} // namespace Core::Reflection

void Core::Reflection::InitializeReflection()
{
  entt::meta_reset();
  
#define REFLECTION(T) entt::meta<T>()
#define REFLECT_COMPONENT_NO_DEFAULT(T) entt::meta<T>()\
  .traits(Traits::COMPONENT)
#define REFLECT_COMPONENT(T) entt::meta<T>()\
  .traits(Traits::COMPONENT) \
  .func<[](entt::registry* registry, entt::entity entity) { registry->emplace<T>(entity); }>("EmplaceDefault"_hs)
#define TRAITS(Traits) .traits(Traits)
#define DATA(Type, Member, ...) \
  .data<&Type :: Member, entt::as_ref_t>(#Member##_hs) \
  .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Member} __VA_OPT__(, __VA_ARGS__)})
#define PROP_SPEED(Scalar) {"speed"_hs, Scalar}
#define PROP_MIN(Scalar) {"min"_hs, Scalar}
#define PROP_MAX(Scalar) {"max"_hs, Scalar}

  entt::meta<int>().func<&EditorWriteScalar<int>>("EditorWrite"_hs).func<&EditorReadScalar<int>>("EditorRead"_hs);
  entt::meta<uint32_t>().func<&EditorWriteScalar<uint32_t>>("EditorWrite"_hs).func<&EditorReadScalar<uint32_t>>("EditorRead"_hs);
  entt::meta<float>().func<&EditorWriteScalar<float>>("EditorWrite"_hs).func<&EditorReadScalar<float>>("EditorRead"_hs);
  entt::meta<glm::vec3>().func<&EditorWriteVec3>("EditorWrite"_hs).func<&EditorReadVec3>("EditorRead"_hs);
  entt::meta<glm::quat>().func<&EditorWriteQuat>("EditorWrite"_hs).func<&EditorReadQuat>("EditorRead"_hs);
  entt::meta<std::string>().func<&EditorWriteString>("EditorWrite"_hs).func<&EditorReadString>("EditorRead"_hs);
  entt::meta<bool>().func<&EditorWriteScalar<bool>>("EditorWrite"_hs).func<&EditorReadScalar<bool>>("EditorRead"_hs);

  REFLECT_COMPONENT(LocalTransform)
    DATA(LocalTransform, position, PROP_SPEED(0.20f))
    TRAITS(Traits::EDITOR)
    DATA(LocalTransform, rotation)
    TRAITS(Traits::EDITOR)
    DATA(LocalTransform, scale, PROP_SPEED(0.125f))
    TRAITS(Traits::EDITOR);
  
  REFLECT_COMPONENT(GlobalTransform)
    DATA(GlobalTransform, position)
    TRAITS(Traits::EDITOR_READ)
    DATA(GlobalTransform, rotation)
    TRAITS(Traits::EDITOR_READ)
    DATA(GlobalTransform, scale)
    TRAITS(Traits::EDITOR_READ);

  REFLECT_COMPONENT(PreviousGlobalTransform)
    DATA(PreviousGlobalTransform, position)
    TRAITS(Traits::EDITOR_READ)
    DATA(PreviousGlobalTransform, rotation)
    TRAITS(Traits::EDITOR_READ)
    DATA(PreviousGlobalTransform, scale)
    TRAITS(Traits::EDITOR_READ);

  REFLECT_COMPONENT(RenderTransform)
    DATA(RenderTransform, transform)
    TRAITS(Traits::EDITOR_READ);

  REFLECT_COMPONENT(Health)
    DATA(Health, hp, PROP_MIN(0.0f), PROP_MAX(100.0f))
    TRAITS(Traits::EDITOR)
    DATA(Health, maxHp, PROP_MIN(0.0f), PROP_MAX(100.0f))
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(ContactDamage)
    DATA(ContactDamage, damage, PROP_MIN(0.125f), PROP_MAX(100.0f))
    TRAITS(Traits::EDITOR)
    DATA(ContactDamage, knockback, PROP_MIN(0.125f), PROP_MAX(100.0f))
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(LinearVelocity)
    DATA(LinearVelocity, v, PROP_SPEED(0.0125f))
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(TeamFlags)
    DATA(TeamFlags, flags)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(Friction)
    DATA(Friction, axes, PROP_MAX(5.0f))
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(Player)
    DATA(Player, id)
    TRAITS(Traits::EDITOR_READ)
    DATA(Player, inventoryIsOpen)
    TRAITS(Traits::EDITOR_READ);

  REFLECT_COMPONENT(InputState)
    DATA(InputState, strafe)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputState, forward)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputState, elevate)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputState, jump)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputState, sprint)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputState, walk)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputState, usePrimary)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputState, useSecondary)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputState, interact)
    TRAITS(Traits::EDITOR_READ);

  REFLECT_COMPONENT(InputLookState)
    DATA(InputLookState, pitch)
    TRAITS(Traits::EDITOR_READ)
    DATA(InputLookState, yaw)
    TRAITS(Traits::EDITOR_READ);

  REFLECT_COMPONENT(Mesh)
    DATA(Mesh, name)
    TRAITS(Traits::EDITOR)
    DATA(Mesh, tint)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(NoclipCharacterController);

  REFLECT_COMPONENT(FlyingCharacterController)
    .func<[](World* w, entt::entity e) { w->GivePlayerFlyingCharacterController(e); }>("add"_hs)
    DATA(FlyingCharacterController, maxSpeed)
    TRAITS(Traits::EDITOR)
    DATA(FlyingCharacterController, acceleration)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(Physics::CharacterController)
    .func<[](World* w, entt::entity e) { w->GivePlayerCharacterController(e); }>("add"_hs);
  
  REFLECT_COMPONENT(Physics::CharacterControllerShrimple)
    .func<[](World* w, entt::entity e) { w->GivePlayerCharacterControllerShrimple(e); }>("add"_hs);

  REFLECT_COMPONENT(Name)
    DATA(Name, name)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(Physics::RigidBody);

  REFLECT_COMPONENT(DroppedItem)
    DATA(DroppedItem, item)
    TRAITS(Traits::EDITOR);

  REFLECTION(ItemState)
    DATA(ItemState, id)
    TRAITS(Traits::EDITOR)
    DATA(ItemState, count)
    TRAITS(Traits::EDITOR)
    DATA(ItemState, useAccum)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(DeferredDelete);

  REFLECT_COMPONENT(ForwardCollisionsToParent);

  REFLECT_COMPONENT(SimpleEnemyBehavior);

  REFLECT_COMPONENT(PredatoryBirdBehavior);

  REFLECT_COMPONENT(SimplePathfindingEnemyBehavior);

  REFLECT_COMPONENT(NoHashGrid);

  REFLECT_COMPONENT(WormEnemyBehavior)
    DATA(WormEnemyBehavior, maxTurnSpeedDegPerSec)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(LinearPath)
    DATA(LinearPath, frames)
    TRAITS(Traits::EDITOR)
    DATA(LinearPath, secondsElapsed)
    TRAITS(Traits::EDITOR)
    DATA(LinearPath, originalLocalTransform);

  REFLECTION(LinearPath::KeyFrame)
    TRAITS(Traits::EDITOR)
    DATA(LinearPath::KeyFrame, position)
    TRAITS(Traits::EDITOR)
    DATA(LinearPath::KeyFrame, rotation)
    TRAITS(Traits::EDITOR)
    DATA(LinearPath::KeyFrame, scale)
    TRAITS(Traits::EDITOR)
    DATA(LinearPath::KeyFrame, offsetSeconds)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(BlockHealth)
    DATA(BlockHealth, health, PROP_MIN(0.0f), PROP_MAX(100.0f))
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(Hierarchy)
    DATA(Hierarchy, parent)
    TRAITS(Traits::EDITOR_READ)
    DATA(Hierarchy, children)
    TRAITS(Traits::EDITOR_READ)
    DATA(Hierarchy, useLocalPositionAsGlobal)
    TRAITS(Traits::EDITOR_READ)
    DATA(Hierarchy, useLocalRotationAsGlobal)
    TRAITS(Traits::EDITOR_READ);

  REFLECT_COMPONENT(Lifetime)
    DATA(Lifetime, remainingSeconds)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(GhostPlayer)
    DATA(GhostPlayer, remainingSeconds)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(Invulnerability)
    DATA(Invulnerability, remainingSeconds)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(CannotDamageEntities)
    DATA(CannotDamageEntities, entities)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT(Projectile)
    DATA(Projectile, initialSpeed)
    TRAITS(Traits::EDITOR)
    DATA(Projectile, drag)
    TRAITS(Traits::EDITOR)
    DATA(Projectile, restitution)
    TRAITS(Traits::EDITOR);

  REFLECT_COMPONENT_NO_DEFAULT(Inventory)
    DATA(Inventory, activeSlotCoord)
    TRAITS(Traits::EDITOR_READ)
    DATA(Inventory, activeSlotEntity)
    TRAITS(Traits::EDITOR_READ)
    DATA(Inventory, slots)
    TRAITS(Traits::EDITOR_READ);
}
