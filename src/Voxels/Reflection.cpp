#include "Reflection.h"
#include "TwoLevelGrid.h"

#include "imgui.h"
#include "entt/meta/container.hpp"
#include "entt/meta/meta.hpp"
#include "entt/meta/factory.hpp"

#include "cereal/cereal.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/archives/xml.hpp"
#include "cereal/types/string.hpp"

#include "tracy/Tracy.hpp"

#include <type_traits>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iterator>
#include <unordered_map>

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

  static bool EditorWriteEntity(entt::entity& entity, const PropertiesMap& properties)
  {
    const char* label = "entity";
    GetEditorName(label, properties);
    using T = std::underlying_type_t<entt::entity>;
    auto temp = entity;
    if (ImGui::InputScalar(label, ScalarToImGuiDataType<T>(), &temp, nullptr, nullptr, ScalarToFormatString<T>(), ImGuiInputTextFlags_EnterReturnsTrue))
    {
      // TODO: Validate new entity ID with registry.
      entity = temp;
      return true;
    }
    return false;
  }

  static void EditorReadEntity(entt::entity entity, const PropertiesMap& properties)
  {
    const char* label = "entity";
    GetEditorName(label, properties);
    using T = std::underlying_type_t<entt::entity>;
    if (entity == entt::null)
    {
      ImGui::Text("%s: null", label);
    }
    else
    {
      ImGui::Text("%s: %u, v%u", label, (uint32_t)entt::to_entity(entity), (uint32_t)entt::to_version(entity));
    }
  }

  // Tentative, unsure if something like this is necessary.
  static void EditorUpdateTransform(entt::handle handle)
  {
    UpdateLocalTransform(handle);
  }

  static void EditorUpdateLinearPath(entt::handle handle)
  {
    // Path component treats 0 as the "reset transform value", and we don't want to trigger that.
    auto& path = handle.get<LinearPath>();
    if (path.secondsElapsed == 0)
    {
      path.secondsElapsed = 1e-5f;
    }
  }

  template<bool Save, typename Archive>
  void Serialize(Archive& ar, entt::meta_any value)
  {
    ZoneScoped;
    const auto archiveHash = entt::type_id<Archive>();
    for (auto [id, func] : value.type().func())
    {
      if (func.arg(0).info() == archiveHash)
      {
        func.invoke({}, entt::forward_as_meta(ar), value.as_ref());
        return;
      }
    }

    for (auto [id, data] : value.type().data())
    {
      if (data.traits<Traits>() & Traits::SERIALIZE)
      {
        Serialize<Save>(ar, data.get(value).as_ref());
      }
    }

    if (value.type().is_sequence_container())
    {
      auto sequence = value.as_sequence_container();

      if constexpr (Save)
      {
        auto size = (uint32_t)sequence.size();
        Serialize<Save>(ar, entt::forward_as_meta(size));
      }
      else
      {
        auto size = uint32_t();
        Serialize<Save>(ar, entt::forward_as_meta(size));
        sequence.resize(size);
      }

      for (auto element : sequence)
      {
        Serialize<Save>(ar, element.as_ref());
      }
    }

    if (value.type().traits<Traits>() & Traits::VARIANT)
    {
      if constexpr (Save)
      {
        auto idFunc = value.type().func("type_hash"_hs);
        assert(idFunc);
        Serialize<Save>(ar, idFunc.invoke({}, value));
        auto valueFunc = value.type().func("const_value"_hs);
        assert(valueFunc);
        Serialize<Save>(ar, valueFunc.invoke({}, value.as_ref()));
      }
      else
      {
        auto id = entt::id_type();
        Serialize<Save>(ar, entt::forward_as_meta(id));
        auto variantMeta = entt::resolve(id);
        assert(variantMeta);
        auto variantTypeInstance = variantMeta.construct();
        Serialize<Save>(ar, variantTypeInstance.as_ref());
        [[maybe_unused]] auto succ = value.assign(value.type().construct(variantTypeInstance));
        assert(succ);
      }
    }
  }

  template<typename Archive, typename T>
  void Serialize2(Archive& ar, T& value)
  {
    ar(value);
  }

  template<bool Save, typename Archive>
  void Serialize(Archive& ar, std::conditional_t<Save, const TwoLevelGrid*&, TwoLevelGrid*&> grid)
  {
    ZoneScoped;
    if constexpr (Save)
    {
      Serialize<Save>(ar, grid->materials_);
      Serialize<Save>(ar, grid->topLevelBricksDims_);
    }
    else
    {
      auto materials = std::vector<TwoLevelGrid::Material>();
      Serialize<Save>(ar, entt::forward_as_meta(materials));
      auto dims = glm::ivec3();
      Serialize<Save>(ar, entt::forward_as_meta(dims));
      grid = new TwoLevelGrid(dims);
      grid->SetMaterialArray(std::move(materials));
    }
    // TODO: Ridiculously inefficient way to serialize grid.
    for (int z = 0; z < grid->dimensions_.z; z++)
    for (int y = 0; y < grid->dimensions_.y; y++)
    for (int x = 0; x < grid->dimensions_.x; x++)
    {
      if constexpr (Save)
      {
        auto voxel = grid->GetVoxelAt({x, y, z});
        Serialize2(ar, voxel);
      }
      else
      {
        auto voxel = TwoLevelGrid::voxel_t();
        Serialize2(ar, voxel);
        grid->SetVoxelAt({x, y, z}, voxel);
      }
    }
  }
} // namespace Core::Reflection

void Core::Reflection::InitializeReflection()
{
  entt::meta_reset();

//#define MAKE_IDENTIFIER(T) [[maybe_unused]] bool reflection_for_ ## T
#define MAKE_IDENTIFIER(T)
#define REFLECT_TYPE(T) MAKE_IDENTIFIER(T); entt::meta_factory<T>{}
#define REFLECT_COMPONENT_NO_DEFAULT(T) \
  MAKE_IDENTIFIER(T);                   \
  entt::meta_factory<T>{}\
  .traits(Traits::COMPONENT)
#define REFLECT_COMPONENT(T) \
  MAKE_IDENTIFIER(T);        \
  entt::meta_factory<T>{}    \
  .traits(Traits::COMPONENT) \
  .func<[](entt::registry* registry, entt::entity entity) { registry->emplace<T>(entity); }>("EmplaceDefault"_hs) \
  .func<[](entt::registry* registry, entt::entity entity, T& value) { registry->emplace_or_replace<T>(entity, std::move(value)); }>("EmplaceMove"_hs)
#define TRAITS(TraitsV) .traits(TraitsV)
#define DATA(Type, Member, ...) \
  .data<&Type :: Member, entt::as_ref_t>(#Member##_hs) \
  .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Member} __VA_OPT__(, __VA_ARGS__)})
#define PROP_SPEED(Scalar) {"speed"_hs, Scalar}
#define PROP_MIN(Scalar) {"min"_hs, Scalar}
#define PROP_MAX(Scalar) {"max"_hs, Scalar}
#define REFLECT_ENUM(T) entt::meta_factory<T>{}
#define ENUMERATOR(E, Member, ...) \
  .data<E :: Member>(#Member##_hs) \
  .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Member} __VA_OPT__(, __VA_ARGS__)})
#define SERIALIZERS(T) \
  func<Serialize2<cereal::XMLInputArchive, T>>("XMLInputArchive"_hs)   \
  .func<Serialize2<cereal::XMLOutputArchive, const T>>("XMLOutputArchive"_hs) \
  .func<Serialize2<cereal::BinaryInputArchive, T>>("BinaryInputArchive"_hs)   \
  .func<Serialize2<cereal::BinaryOutputArchive, const T>>("BinaryOutputArchive"_hs)
  //.func<Serialize2<cereal::JSONInputArchive, float>>("JSONInputArchive"_hs)
  //.func<Serialize2<cereal::JSONOutputArchive, float>>("JSONOutputArchive"_hs)
#define VARIANT_FUNCS(T)                                                      \
  func<[](const T& ps)                                                        \
    {                                                                         \
      auto info = entt::id_type();                                            \
      std::visit([&](auto&& x) { info = entt::type_id<decltype(x)>().hash(); }, ps); \
      return info;                                                            \
    }>("type_hash"_hs)                                                        \
  .func<[](const T& ps)                                                       \
    {                                                                         \
      auto value = entt::meta_any();                                          \
      std::visit([&](auto&& x) { value = entt::forward_as_meta(x); }, ps);    \
      return value;                                                           \
    }>("const_value"_hs)                                                      \
  .func<[](T& ps)                                                             \
    {                                                                         \
      auto value = entt::meta_any();                                          \
      std::visit([&](auto&& x) { value = entt::forward_as_meta(x); }, ps);    \
      return value;                                                           \
    }>("value"_hs)

  entt::meta_factory<int>().func<&EditorWriteScalar<int>>("EditorWrite"_hs).func<&EditorReadScalar<int>>("EditorRead"_hs).SERIALIZERS(int);
  entt::meta_factory<uint32_t>().func<&EditorWriteScalar<uint32_t>>("EditorWrite"_hs).func<&EditorReadScalar<uint32_t>>("EditorRead"_hs).SERIALIZERS(uint32_t);
  entt::meta_factory<uint16_t>().func<&EditorWriteScalar<uint16_t>>("EditorWrite"_hs).func<&EditorReadScalar<uint16_t>>("EditorRead"_hs).SERIALIZERS(uint16_t);
  entt::meta_factory<uint8_t>().func<&EditorWriteScalar<uint8_t>>("EditorWrite"_hs).func<&EditorReadScalar<uint8_t>>("EditorRead"_hs).SERIALIZERS(uint8_t);
  entt::meta_factory<float>().func<&EditorWriteScalar<float>>("EditorWrite"_hs).func<&EditorReadScalar<float>>("EditorRead"_hs).SERIALIZERS(float);
  entt::meta_factory<glm::vec3>().func<&EditorWriteVec3>("EditorWrite"_hs).func<&EditorReadVec3>("EditorRead"_hs)
    DATA(glm::vec3, x)
    TRAITS(EDITOR | SERIALIZE)
    DATA(glm::vec3, y)
    TRAITS(EDITOR | SERIALIZE)
    DATA(glm::vec3, z)
  TRAITS(EDITOR | SERIALIZE);
  entt::meta_factory<glm::ivec3>()
    DATA(glm::ivec3, x)
    TRAITS(EDITOR | SERIALIZE)
    DATA(glm::ivec3, y)
    TRAITS(EDITOR | SERIALIZE)
    DATA(glm::ivec3, z)
    TRAITS(EDITOR | SERIALIZE);
  entt::meta_factory<glm::quat>().func<&EditorWriteQuat>("EditorWrite"_hs).func<&EditorReadQuat>("EditorRead"_hs)
    DATA(glm::quat, w)
    TRAITS(EDITOR | SERIALIZE)
    DATA(glm::quat, x)
    TRAITS(EDITOR | SERIALIZE)
    DATA(glm::quat, y)
    TRAITS(EDITOR | SERIALIZE)
    DATA(glm::quat, z)
    TRAITS(EDITOR | SERIALIZE);
  entt::meta_factory<std::string>().func<&EditorWriteString>("EditorWrite"_hs).func<&EditorReadString>("EditorRead"_hs).SERIALIZERS(std::string);
  entt::meta_factory<bool>().func<&EditorWriteScalar<bool>>("EditorWrite"_hs).func<&EditorReadScalar<bool>>("EditorRead"_hs).SERIALIZERS(bool);
  entt::meta_factory<entt::entity>().func<&EditorWriteEntity>("EditorWrite"_hs).func<&EditorReadEntity>("EditorRead"_hs).SERIALIZERS(entt::entity);
  
  REFLECT_COMPONENT(LocalTransform)
    .func<&EditorUpdateTransform>("OnUpdate"_hs)
    DATA(LocalTransform, position, PROP_SPEED(0.20f))
    TRAITS(EDITOR | SERIALIZE)
    DATA(LocalTransform, rotation)
    TRAITS(EDITOR | SERIALIZE)
    DATA(LocalTransform, scale, PROP_SPEED(0.0125f))
    TRAITS(EDITOR | SERIALIZE);
  
  REFLECT_COMPONENT(GlobalTransform)
    DATA(GlobalTransform, position)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(GlobalTransform, rotation)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(GlobalTransform, scale)
    TRAITS(SERIALIZE | EDITOR_READ);

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
    TRAITS(SERIALIZE | EDITOR)
    DATA(Health, maxHp, PROP_MIN(0.0f), PROP_MAX(100.0f))
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(ContactDamage)
    DATA(ContactDamage, damage, PROP_MIN(0.125f), PROP_MAX(100.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(ContactDamage, knockback, PROP_MIN(0.125f), PROP_MAX(100.0f))
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(LinearVelocity)
    DATA(LinearVelocity, v, PROP_SPEED(0.0125f))
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(TeamFlags)
    DATA(TeamFlags, flags)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Friction)
    DATA(Friction, axes, PROP_MAX(5.0f))
    TRAITS(SERIALIZE | EDITOR);

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
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(InputLookState, yaw)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_COMPONENT(Mesh)
    DATA(Mesh, name)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(NoclipCharacterController);

  REFLECT_COMPONENT(FlyingCharacterController)
    .func<[](World* w, entt::entity e) { w->GivePlayerFlyingCharacterController(e); }>("add"_hs)
    DATA(FlyingCharacterController, maxSpeed, PROP_MAX(50.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(FlyingCharacterController, acceleration, PROP_MAX(50.0f))
    TRAITS(SERIALIZE | EDITOR);

  using namespace Physics;
  entt::meta_factory<CharacterController>{}
    .traits(COMPONENT)
    .func<[](World* w, entt::entity e) { w->GivePlayerCharacterController(e); }>("add"_hs);
  
  entt::meta_factory<CharacterControllerShrimple>{}
    .traits(COMPONENT)
    .func<[](World* w, entt::entity e) { w->GivePlayerCharacterControllerShrimple(e); }>("add"_hs);

  REFLECT_COMPONENT(Name)
    DATA(Name, name)
    TRAITS(SERIALIZE | EDITOR);

  //REFLECT_COMPONENT(RigidBody);
  REFLECT_COMPONENT(CharacterControllerSettings)
    DATA(CharacterControllerSettings, shape)
    TRAITS(SERIALIZE | EDITOR_READ);
  
  REFLECT_COMPONENT(CharacterControllerShrimpleSettings)
    DATA(CharacterControllerShrimpleSettings, shape)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_COMPONENT(RigidBodySettings)
    DATA(RigidBodySettings, shape)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(RigidBodySettings, activate)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(RigidBodySettings, isSensor)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(RigidBodySettings, gravityFactor)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(RigidBodySettings, motionType)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(RigidBodySettings, motionQuality)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(RigidBodySettings, layer)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(RigidBodySettings, degreesOfFreedom)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_TYPE(std::monostate);
  REFLECT_TYPE(UseTwoLevelGrid);

  REFLECT_TYPE(Sphere)
    DATA(Sphere, radius)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_TYPE(Capsule)
    DATA(Capsule, radius)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(Capsule, cylinderHalfHeight)
    TRAITS(SERIALIZE | EDITOR_READ);
  
  REFLECT_TYPE(Box)
    DATA(Box, halfExtent)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_TYPE(Plane)
    DATA(Plane, normal)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(Plane, constant)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_TYPE(PolyShape)
    .ctor<std::monostate>()
    .ctor<Sphere>()
    .ctor<Capsule>()
    .ctor<Box>()
    .ctor<Plane>()
    .ctor<UseTwoLevelGrid>()
    .traits<Traits>(VARIANT)
    .VARIANT_FUNCS(PolyShape);

  REFLECT_TYPE(ShapeSettings)
    DATA(ShapeSettings, shape)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(ShapeSettings, density)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(ShapeSettings, translation)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(ShapeSettings, rotation)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_ENUM(JPH::EMotionType)
    .SERIALIZERS(JPH::EMotionType)
    ENUMERATOR(JPH::EMotionType, Static)
    ENUMERATOR(JPH::EMotionType, Kinematic)
    ENUMERATOR(JPH::EMotionType, Dynamic);

  REFLECT_ENUM(JPH::EMotionQuality)
    .SERIALIZERS(JPH::EMotionQuality)
    ENUMERATOR(JPH::EMotionQuality, Discrete)
    ENUMERATOR(JPH::EMotionQuality, LinearCast);

  REFLECT_ENUM(JPH::EAllowedDOFs)
    .SERIALIZERS(JPH::EAllowedDOFs)
    ENUMERATOR(JPH::EAllowedDOFs, None)
    ENUMERATOR(JPH::EAllowedDOFs, All)
    ENUMERATOR(JPH::EAllowedDOFs, TranslationX)
    ENUMERATOR(JPH::EAllowedDOFs, TranslationY)
    ENUMERATOR(JPH::EAllowedDOFs, TranslationZ)
    ENUMERATOR(JPH::EAllowedDOFs, RotationX)
    ENUMERATOR(JPH::EAllowedDOFs, RotationY)
    ENUMERATOR(JPH::EAllowedDOFs, RotationZ)
    ENUMERATOR(JPH::EAllowedDOFs, Plane2D)

  REFLECT_COMPONENT(DroppedItem)
    DATA(DroppedItem, item)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_TYPE(ItemState)
    DATA(ItemState, id)
    TRAITS(SERIALIZE | EDITOR)
    DATA(ItemState, count)
    TRAITS(SERIALIZE | EDITOR)
    DATA(ItemState, useAccum)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_TYPE(TwoLevelGrid::Material)
    DATA(TwoLevelGrid::Material, isVisible)
    TRAITS(SERIALIZE | EDITOR)
    DATA(TwoLevelGrid::Material, isSolid)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(DeferredDelete);

  REFLECT_COMPONENT(ForwardCollisionsToParent);

  REFLECT_COMPONENT(SimpleEnemyBehavior);

  REFLECT_COMPONENT(PredatoryBirdBehavior)
    DATA(PredatoryBirdBehavior, state)
    TRAITS(SERIALIZE | EDITOR)
    DATA(PredatoryBirdBehavior, accum)
    TRAITS(SERIALIZE | EDITOR)
    DATA(PredatoryBirdBehavior, target)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(PredatoryBirdBehavior, idlePosition)
    TRAITS(SERIALIZE | EDITOR)
    DATA(PredatoryBirdBehavior, lineOfSightDuration)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_ENUM(PredatoryBirdBehavior::State)
    ENUMERATOR(PredatoryBirdBehavior::State, IDLE)
    ENUMERATOR(PredatoryBirdBehavior::State, CIRCLING)
    ENUMERATOR(PredatoryBirdBehavior::State, SWOOPING);

  REFLECT_COMPONENT(SimplePathfindingEnemyBehavior);

  REFLECT_COMPONENT(NoHashGrid);

  REFLECT_COMPONENT(WormEnemyBehavior)
    DATA(WormEnemyBehavior, maxTurnSpeedDegPerSec)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(LinearPath)
    .func<&EditorUpdateLinearPath>("OnUpdate"_hs)
    DATA(LinearPath, frames)
    TRAITS(EDITOR | SERIALIZE)
    DATA(LinearPath, secondsElapsed)
    TRAITS(EDITOR | SERIALIZE)
    DATA(LinearPath, originalLocalTransform)
    TRAITS(SERIALIZE);

  //using LinearPath::KeyFrame;
  REFLECT_TYPE(LinearPath::KeyFrame)
    TRAITS(SERIALIZE | EDITOR)
    DATA(LinearPath::KeyFrame, position)
    TRAITS(SERIALIZE | EDITOR)
    DATA(LinearPath::KeyFrame, rotation)
    TRAITS(SERIALIZE | EDITOR)
    DATA(LinearPath::KeyFrame, scale)
    TRAITS(SERIALIZE | EDITOR)
    DATA(LinearPath::KeyFrame, offsetSeconds)
    TRAITS(SERIALIZE | EDITOR)
    DATA(LinearPath::KeyFrame, easing)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(BlockHealth)
    DATA(BlockHealth, health, PROP_MIN(0.0f), PROP_MAX(100.0f))
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Hierarchy)
    DATA(Hierarchy, parent)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(Hierarchy, children)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(Hierarchy, useLocalPositionAsGlobal)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(Hierarchy, useLocalRotationAsGlobal)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_COMPONENT(Lifetime)
    DATA(Lifetime, remainingSeconds)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(GhostPlayer)
    DATA(GhostPlayer, remainingSeconds)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Invulnerability)
    DATA(Invulnerability, remainingSeconds, PROP_MAX(1000.0f))
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(CannotDamageEntities)
    DATA(CannotDamageEntities, entities)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Projectile)
    DATA(Projectile, initialSpeed, PROP_MAX(500.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(Projectile, drag)
    TRAITS(SERIALIZE | EDITOR)
    DATA(Projectile, restitution)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Inventory)
    DATA(Inventory, activeSlotCoord)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(Inventory, activeSlotEntity)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(Inventory, slots)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_COMPONENT(Billboard)
    DATA(Billboard, name)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(GpuLight)
    DATA(GpuLight, color)
    TRAITS(SERIALIZE | EDITOR)
    DATA(GpuLight, type)
    TRAITS(SERIALIZE | EDITOR)
    DATA(GpuLight, direction, PROP_MIN(-1.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(GpuLight, intensity, PROP_MAX(50.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(GpuLight, position)
    TRAITS(SERIALIZE | EDITOR_READ)
    DATA(GpuLight, range, PROP_MAX(200.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(GpuLight, innerConeAngle, PROP_MAX(6.28f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(GpuLight, outerConeAngle, PROP_MAX(6.28f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(GpuLight, colorSpace)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(BlockEntity);

  REFLECT_COMPONENT(DespawnWhenFarFromPlayer)
    DATA(DespawnWhenFarFromPlayer, maxDistance)
    TRAITS(SERIALIZE | EDITOR)
    DATA(DespawnWhenFarFromPlayer, gracePeriod)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Loot)
    DATA(Loot, name)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Enemy);

  REFLECT_COMPONENT(AiWanderBehavior)
    DATA(AiWanderBehavior, minWanderDistance, PROP_MAX(10.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(AiWanderBehavior, maxWanderDistance, PROP_MAX(10.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(AiWanderBehavior, timeBetweenMoves, PROP_MAX(10.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(AiWanderBehavior, accumulator)
    TRAITS(SERIALIZE | EDITOR)
    DATA(AiWanderBehavior, targetCanBeFloating)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(AiTarget)
    DATA(AiTarget, currentTarget)
    TRAITS(SERIALIZE | EDITOR_READ);

  REFLECT_COMPONENT(AiVision)
    DATA(AiVision, coneAngleRad, PROP_MAX(glm::two_pi<float>()))
    TRAITS(SERIALIZE | EDITOR)
    DATA(AiVision, distance, PROP_MAX(50.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(AiVision, invAcuity, PROP_MAX(5.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(AiVision, accumulator)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(AiHearing)
    DATA(AiHearing, distance, PROP_MAX(50.0f))
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(KnockbackMultiplier)
    DATA(KnockbackMultiplier, factor, PROP_MAX(10.0f))
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Tint)
    DATA(Tint, color)
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(WalkingMovementAttributes)
    DATA(WalkingMovementAttributes, runBaseSpeed, PROP_MAX(20.0f))
    TRAITS(SERIALIZE | EDITOR)
    DATA(WalkingMovementAttributes, walkModifier)
    TRAITS(SERIALIZE | EDITOR)
    DATA(WalkingMovementAttributes, runMaxSpeed, PROP_MAX(20.0f))
    TRAITS(SERIALIZE | EDITOR);

  REFLECT_COMPONENT(Voxels);

  REFLECT_ENUM(Math::Easing)
    ENUMERATOR(Math::Easing, LINEAR)
    ENUMERATOR(Math::Easing, EASE_IN_SINE)
    ENUMERATOR(Math::Easing, EASE_OUT_SINE)
    ENUMERATOR(Math::Easing, EASE_IN_OUT_BACK)
    ENUMERATOR(Math::Easing, EASE_IN_CUBIC)
    ENUMERATOR(Math::Easing, EASE_OUT_CUBIC);

  auto stream = std::stringstream();
  glm::ivec3 shape = {1, 2, 3};
  {
    cereal::XMLOutputArchive archive(stream);
    Serialize<true>(archive, shape);
  }

  shape = {};
  {
    cereal::XMLInputArchive archive(stream);
    Serialize<false>(archive, entt::forward_as_meta(shape));
  }
  {
    cereal::XMLOutputArchive archive(std::cout);
    Serialize<true>(archive, shape);
  }

  // TODO: TEMP
  REFLECT_COMPONENT(LocalPlayer);
}

void Core::Reflection::SaveRegistryToFile(const World& world, const std::filesystem::path& path)
{
  ZoneScoped;
  const auto& registry = world.GetRegistry();
  auto file = std::ofstream(path, std::ios::binary | std::ios::out | std::ios::trunc);
  auto outputArchive = cereal::BinaryOutputArchive(file);

  // Save relevant context variables.
  auto* pGrid = &registry.ctx().get<TwoLevelGrid>();
  Serialize<true>(outputArchive, pGrid);

  const auto numSets = (uint32_t)std::ranges::count_if(registry.storage(), [](const auto& p) { return entt::resolve(p.first).traits<Traits>() & Traits::COMPONENT; });
  Serialize<true>(outputArchive, numSets);
  for (auto [id, set] : registry.storage())
  {
    if (auto meta = entt::resolve(id))
    {
      if (meta.traits<Traits>() & Traits::COMPONENT)
      {
        ZoneScopedN("Component");
        ZoneText(meta.info().name().data(), meta.info().name().size());
        Serialize<true>(outputArchive, id);
        Serialize<true>(outputArchive, (uint32_t)set.size());
        for (auto entity : set)
        {
          Serialize<true>(outputArchive, entity);
          Serialize<true>(outputArchive, meta.from_void(set.value(entity)));
        }
      }
    }
  }
}

// TODO: ctx.PCG
// TODO: ctx.NpcSpawnDirector
void Core::Reflection::LoadRegistryFromFile(World& world, const std::filesystem::path& path)
{
  ZoneScoped;
  auto& registry     = world.GetRegistry();
  auto remoteToLocal = std::unordered_map<entt::entity, entt::entity>();
  registry.clear();
  registry = {};
  CreateContextVariablesAndObservers(world);
  registry.ctx().get<GameState>() = GameState::GAME;
  auto file         = std::ifstream(path, std::ios::binary | std::ios::in);
  auto inputArchive = cereal::BinaryInputArchive(file);

  // Load relevant context variables.
  TwoLevelGrid* pGrid{};
  Serialize<false>(inputArchive, pGrid);
  assert(pGrid);
  registry.ctx().emplace<TwoLevelGrid>(std::move(*pGrid));
  delete pGrid;

  auto numSets      = uint32_t();
  Serialize<false>(inputArchive, entt::forward_as_meta(numSets));
  for (uint32_t i = 0; i < numSets; i++)
  {
    ZoneScopedN("Component");
    auto id = entt::id_type();
    auto size = uint32_t();
    Serialize<false>(inputArchive, entt::forward_as_meta(id));
    Serialize<false>(inputArchive, entt::forward_as_meta(size));
    auto meta = entt::resolve(id);
    assert(meta);
    assert(meta.traits<Traits>() & Traits::COMPONENT);
    ZoneText(meta.info().name().data(), meta.info().name().size());
    for (uint32_t j = 0; j < size; j++)
    {
      auto remoteEntity = entt::entity();
      Serialize<false>(inputArchive, entt::forward_as_meta(remoteEntity));
      assert(remoteEntity != entt::null);
      auto value = meta.construct();
      assert(value && "Type is missing default constructor");
      Serialize<false>(inputArchive, value.as_ref());
      auto localEntity = entt::entity();
      if (auto it = remoteToLocal.find(remoteEntity); it != remoteToLocal.end())
      {
        localEntity = it->second;
      }
      else
      {
        localEntity = registry.create(remoteEntity);
        remoteToLocal.emplace(remoteEntity, localEntity);
      }

      if (auto emplaceFunc = meta.func("EmplaceMove"_hs))
      {
        emplaceFunc.invoke({}, &registry, localEntity, value.as_ref());
      }
    }
  }
}
