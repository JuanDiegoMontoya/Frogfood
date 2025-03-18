#include "Serialization.h"
#include "Reflection.h"
#include "TwoLevelGrid.h"
#include "Game.h"

#include "entt/meta/container.hpp"
#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"
#include "entt/meta/factory.hpp"
#include "entt/core/hashed_string.hpp"

#include "cereal/cereal.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/archives/xml.hpp"
#include "cereal/types/string.hpp"

#include "tracy/Tracy.hpp"

#include <cstdint>
#include <fstream>
#include <memory>

namespace Core::Serialization
{
  using namespace Reflection;
  using namespace entt::literals;

  template<bool Save, typename Archive>
  static void Serialize(Archive& ar, entt::meta_any value)
  {
    ZoneScoped;

    // First, check if the type already has a bespoke serialization function.
    const auto archiveHash = entt::type_id<Archive>();
    for (auto [id, func] : value.type().func())
    {
      if (func.arg(0).info() == archiveHash)
      {
        func.invoke({}, entt::forward_as_meta(ar), value.as_ref());
        return;
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
        sequence.resize(size); // Returns false for un-resizable containers such as std::array.
      }

      for (auto element : sequence)
      {
        Serialize<Save>(ar, element.as_ref());
      }

      return;
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
      return;
    }

    if (value.type().is_enum())
    {
      auto toUnderlyingFunc = value.type().func("to_underlying"_hs);
      assert(toUnderlyingFunc);
      if constexpr (Save)
      {
        Serialize<Save>(ar, toUnderlyingFunc.invoke({}, value));
      }
      else
      {
        auto underlying = toUnderlyingFunc.ret().construct();
        Serialize<Save>(ar, underlying.as_ref());
        value.assign(underlying);
      }

      return;
    }

    // Serialize data members.
    for (auto [id, data] : value.type().data())
    {
      if (data.traits<Traits>() & Traits::SERIALIZE)
      {
        Serialize<Save>(ar, data.get(value).as_ref());
      }
    }
  }


  // Ugly manual serialization to improve TwoLevelGrid serialization perf.
  namespace
  {
    template<typename Archive>
    void Serialize2(Archive& ar, TwoLevelGrid::BottomLevelBrick& blBrick)
    {
      for (auto& bits : blBrick.occupancy.bitmask)
      {
        detail::Serialize2(ar, bits);
      }

      for (auto& voxel : blBrick.voxels)
      {
        detail::Serialize2(ar, voxel);
      }
    }

    template<typename Archive>
    void Serialize2(Archive& ar, const TwoLevelGrid::BottomLevelBrick& blBrick)
    {
      for (auto& bits : blBrick.occupancy.bitmask)
      {
        detail::Serialize2(ar, bits);
      }

      for (auto& voxel : blBrick.voxels)
      {
        detail::Serialize2(ar, voxel);
      }
    }

    template<typename Archive>
    void Serialize2(Archive& ar, TwoLevelGrid::BottomLevelBrickPtr& blBrickPtr)
    {
      detail::Serialize2(ar, blBrickPtr.voxelsDoBeAllSame);
      detail::Serialize2(ar, blBrickPtr.bottomLevelBrick);
    }

    template<typename Archive>
    void Serialize2(Archive& ar, const TwoLevelGrid::BottomLevelBrickPtr& blBrickPtr)
    {
      detail::Serialize2(ar, blBrickPtr.voxelsDoBeAllSame);
      detail::Serialize2(ar, blBrickPtr.bottomLevelBrick);
    }

    template<typename Archive>
    void Serialize2(Archive& ar, TwoLevelGrid::TopLevelBrick& tlBrick)
    {
      for (auto& blBrickPtr : tlBrick.bricks)
      {
        Serialize2(ar, blBrickPtr);
      }
    }

    template<typename Archive>
    void Serialize2(Archive& ar, const TwoLevelGrid::TopLevelBrick& tlBrick)
    {
      for (auto& blBrickPtr : tlBrick.bricks)
      {
        Serialize2(ar, blBrickPtr);
      }
    }
  } // namespace

  template<bool Save, typename Archive>
  static void Serialize(Archive& ar, std::conditional_t<Save, const TwoLevelGrid*, std::unique_ptr<TwoLevelGrid>&> grid)
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
      grid = std::make_unique<TwoLevelGrid>(dims);
      grid->SetMaterialArray(std::move(materials));
    }

    for (int tz = 0; tz < grid->topLevelBricksDims_.z; tz++)
    for (int ty = 0; ty < grid->topLevelBricksDims_.y; ty++)
    for (int tx = 0; tx < grid->topLevelBricksDims_.x; tx++)
    {
      const auto tlBrickIndex = grid->FlattenTopLevelBrickCoord({tx, ty, tz});
      auto& tlBrickPtr  = grid->GetTopLevelBrickPtr(tlBrickIndex);
      Serialize<Save>(ar, entt::forward_as_meta(tlBrickPtr));
      if (tlBrickPtr.voxelsDoBeAllSame)
      {
        continue;
      }

      if constexpr (!Save)
      {
        tlBrickPtr.topLevelBrick = grid->AllocateTopLevelBrick(0);
      }
      auto& tlBrick = grid->GetTopLevelBrick(tlBrickPtr.topLevelBrick);
      Serialize2(ar, tlBrick);

      // Bottom-level bricks are handled essentially the same as top-level bricks.
      for (auto& blBrickPtr : tlBrick.bricks)
      {
        Serialize2(ar, blBrickPtr);
        if (blBrickPtr.voxelsDoBeAllSame)
        {
          continue;
        }

        if constexpr (!Save)
        {
          blBrickPtr.bottomLevelBrick = grid->AllocateBottomLevelBrick(0);
        }
        auto& blBrick = grid->GetBottomLevelBrick(blBrickPtr.bottomLevelBrick);
        Serialize2(ar, blBrick);
      }
    }
  }

  void Initialize()
  {
    using namespace detail;
#define MAKE_SERIALIZERS(T)                                                     \
  entt::meta_factory<T>()                                                       \
    .func<Serialize2<cereal::XMLInputArchive, T>>("XMLInputArchive"_hs)         \
    .func<Serialize2<cereal::XMLOutputArchive, const T>>("XMLOutputArchive"_hs) \
    .func<Serialize2<cereal::BinaryInputArchive, T>>("BinaryInputArchive"_hs)   \
    .func<Serialize2<cereal::BinaryOutputArchive, const T>>("BinaryOutputArchive"_hs)
    //.func<Serialize2<cereal::JSONInputArchive, float>>("JSONInputArchive"_hs)
    //.func<Serialize2<cereal::JSONOutputArchive, float>>("JSONOutputArchive"_hs)

    MAKE_SERIALIZERS(int8_t);
    MAKE_SERIALIZERS(int16_t);
    MAKE_SERIALIZERS(int32_t);
    MAKE_SERIALIZERS(int64_t);
    MAKE_SERIALIZERS(uint8_t);
    MAKE_SERIALIZERS(uint16_t);
    MAKE_SERIALIZERS(uint32_t);
    MAKE_SERIALIZERS(uint64_t);
    MAKE_SERIALIZERS(float);
    MAKE_SERIALIZERS(double);
    MAKE_SERIALIZERS(entt::entity);
    MAKE_SERIALIZERS(bool);
    MAKE_SERIALIZERS(std::string);

  }

  void SaveRegistryToFile(const World& world, const std::filesystem::path& path)
  {
    ZoneScoped;
    const auto& registry = world.GetRegistry();
    auto file            = std::ofstream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    auto outputArchive   = cereal::BinaryOutputArchive(file);

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
  void LoadRegistryFromFile(World& world, const std::filesystem::path& path)
  {
    ZoneScoped;
    auto& registry     = world.GetRegistry();
    auto remoteToLocal = std::unordered_map<entt::entity, entt::entity>();
    registry.clear(); // Required to invoke on_destroy observers. In particular, for cleaning up physics objects.
    registry = {};
    CreateContextVariablesAndObservers(world);
    registry.ctx().get<GameState>() = GameState::PAUSED;

    auto file         = std::ifstream(path, std::ios::binary | std::ios::in);
    auto inputArchive = cereal::BinaryInputArchive(file);

    // Load relevant context variables.
    auto pGrid = std::unique_ptr<TwoLevelGrid>();
    Serialize<false>(inputArchive, pGrid);
    assert(pGrid);
    pGrid->CoalesceBricksSLOW();
    pGrid->MarkAllBricksDirty();
    registry.ctx().emplace<TwoLevelGrid>(std::move(*pGrid));

    auto numSets = uint32_t();
    Serialize<false>(inputArchive, entt::forward_as_meta(numSets));
    for (uint32_t i = 0; i < numSets; i++)
    {
      ZoneScopedN("Component");
      auto id   = entt::id_type();
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
} // namespace Core::Serialization