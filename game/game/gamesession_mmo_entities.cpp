#include "gamesession.h"
#include "savegameheader.h"
#if OPENGOTHIC_MMO_SQLITE_TOOLING
#include "../../tools/mmo/mmoruntimesqlite.h"
#endif
#include "mmosemantichooks.h"
#include "mmoclientbridge.h"
#include "mmoclientpresentationcatalog.h"
#include "mmoserverpresentationbatchconsumer.h"
#include "mmorestoresnapshot.h"

#include <Tempest/Log>
#include <Tempest/MemReader>
#include <Tempest/MemWriter>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <chrono>
#include <charconv>
#include <thread>
#include <limits>
#include <iterator>
#include <functional>
#include <optional>
#include <utility>
#include <type_traits>

#include "utils/string_frm.h"
#include "worldstatestorage.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/objects/interactive.h"
#include "world/world.h"
#include "world/waypoint.h"
#include "sound/soundfx.h"
#include "serialize.h"
#include "camera.h"
#include "gothic.h"
#include "commandline.h"

using namespace Tempest;

namespace {

[[nodiscard]] bool isValidNpcSymbol(std::size_t symbol) noexcept {
  return symbol != std::size_t(-1) &&
         symbol <= std::numeric_limits<std::uint32_t>::max();
}

void appendMmoPresentationNumber(
    std::string& value,
    const std::uint64_t number) {
  char buffer[24]{};
  const auto [end, error] = std::to_chars(std::begin(buffer), std::end(buffer), number);
  if(error == std::errc{})
    value.append(buffer, end);
}


[[nodiscard]] std::string mmoPresentationEntityKey(
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity) {
  std::string value;
  value.reserve(96U);
  value.append("v2-entity:");
  appendMmoPresentationNumber(value, entity.handle.id);
  value.push_back(':');
  appendMmoPresentationNumber(value, entity.handle.generation);
  value.push_back(':');
  appendMmoPresentationNumber(value, entity.presentation.presentationId);
  value.push_back(':');
  appendMmoPresentationNumber(value, entity.presentation.archetypeId);
  value.push_back(':');
  appendMmoPresentationNumber(value, entity.presentation.revision);
  return value;
}

[[nodiscard]] constexpr Mmo::ClientPresentation::ServerEntityHandle
presentationRegistryHandle(
    const Mmo::ClientPresentation::ServerPresentationEntityHandle handle) noexcept {
  return {.id = handle.id, .generation = handle.generation};
}

[[nodiscard]] constexpr Mmo::ClientPresentation::ServerEntityKind
presentationRegistryKind(
    const Mmo::ClientPresentation::ServerPresentationEntityKind kind) noexcept {
  using Typed = Mmo::ClientPresentation::ServerPresentationEntityKind;
  using Legacy = Mmo::ClientPresentation::ServerEntityKind;
  switch(kind) {
    case Typed::LocalPlayer:  return Legacy::LocalPlayer;
    case Typed::RemotePlayer: return Legacy::RemotePlayer;
    case Typed::Npc:          return Legacy::Npc;
  }
  return Legacy::Npc;
}

struct MmoServerEntityMaterialization final {
  Npc* npc = nullptr;
  bool materializedByMmo = false;
};

Npc* findNpcByObjectToken(World& world, const std::uintptr_t token) noexcept {
  if(token == 0U)
    return nullptr;
  const auto count = world.npcCount();
  for(std::uint32_t id = 0; id < count; ++id) {
    auto* npc = world.npcById(id);
    if(reinterpret_cast<std::uintptr_t>(npc) == token)
      return npc;
  }
  return nullptr;
}

Npc* findNpcByPresentationIdentity(
    World& world,
    const Mmo::ClientPresentation::LocalNpcPresentationIdentity& local) noexcept {
  auto* npc = world.npcById(local.localNpcId);
  if(npc != nullptr &&
     (local.localObjectToken == 0U ||
      reinterpret_cast<std::uintptr_t>(npc) == local.localObjectToken)) {
    return npc;
  }
  return findNpcByObjectToken(world, local.localObjectToken);
}

Npc* findLocalNpcForServerEntity(
    World& world,
    const std::uint32_t instanceSymbol,
    const Tempest::Vec3 position) noexcept {
  Npc* result = nullptr;
  float bestDistanceSquared = 128.0F * 128.0F;
  for(std::uint32_t id = 0U; id < world.npcCount(); ++id) {
    auto* npc = world.npcById(id);
    if(npc == nullptr || npc->isPlayer() || npc->isMmoServerReplica() ||
       npc->instanceSymbol() != instanceSymbol) {
      continue;
    }
    const auto delta = npc->position() - position;
    const float distanceSquared = delta.x * delta.x + delta.y * delta.y +
                                  delta.z * delta.z;
    if(distanceSquared < bestDistanceSquared) {
      result = npc;
      bestDistanceSquared = distanceSquared;
    }
  }
  return result;
}

MmoServerEntityMaterialization materializeMmoServerEntityNpc(
    World& world,
    GameScript& scripts,
    const Mmo::ClientPresentation::ClientPresentationCatalogRuntime* catalog,
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity) noexcept {
  using Kind = Mmo::ClientPresentation::ServerPresentationEntityKind;
  if(entity.kind == Kind::LocalPlayer)
    return {world.player(), false};

  if(!entity.presentation.valid())
    return {};

  std::optional<std::uint32_t> catalogSymbol;
  std::string_view catalogInstanceName;
  if(catalog != nullptr && catalog->installed()) {
    auto instanceName = entity.kind == Kind::RemotePlayer
                            ? catalog->playerInstanceName(
                                  entity.presentation.archetypeId,
                                  entity.presentation.presentationId)
                            : catalog->npcInstanceName(
                                  entity.presentation.archetypeId,
                                  entity.presentation.presentationId);
    if(!instanceName.has_value() && entity.kind == Kind::RemotePlayer) {
      instanceName = catalog->npcInstanceName(
          entity.presentation.archetypeId,
          entity.presentation.presentationId);
    }
    if(instanceName.has_value()) {
      catalogInstanceName = *instanceName;
      const auto symbol = scripts.findSymbolIndex(*instanceName);
      if(symbol != size_t(-1) &&
         symbol <= std::numeric_limits<std::uint32_t>::max()) {
        catalogSymbol = static_cast<std::uint32_t>(symbol);
      }
    }
  }

  const auto* localPlayer = world.player();
  const auto localPlayerSymbol = localPlayer != nullptr
                                     ? localPlayer->instanceSymbol()
                                     : 0U;
  const auto resolvedSymbol = catalogSymbol.has_value()
                                  ? catalogSymbol
                                  : resolveServerReplicaInstanceSymbol(
                                        presentationRegistryKind(entity.kind),
                                        entity.presentation.archetypeId,
                                        localPlayerSymbol);
  if(!resolvedSymbol.has_value() || !isValidNpcSymbol(*resolvedSymbol))
    return {};
  const auto instanceSymbol = *resolvedSymbol;

  try {
    const Tempest::Vec3 position{
        static_cast<float>(entity.transform.posX),
        static_cast<float>(entity.transform.posY),
        static_cast<float>(entity.transform.posZ)};
    auto* npc = findLocalNpcForServerEntity(world, instanceSymbol, position);
    const bool materializedByMmo = npc == nullptr;
    if(npc == nullptr)
      npc = world.addMmoServerReplica(instanceSymbol, position);
    if(npc == nullptr)
      return {};
    if(!materializedByMmo)
      Log::i("MMO NPC presentation: reusing local NPC instance_symbol=",
             instanceSymbol, " position=", position.x, ",", position.y,
             ",", position.z);
    if(catalog != nullptr && catalog->installed()) {
      if(const auto visual = catalog->npcVisual(
             entity.presentation.archetypeId,
             entity.presentation.presentationId);
         visual.has_value() &&
         (!visual->bodyVisual.empty() || !visual->headVisual.empty())) {
        Log::i(
            "MMO NPC presentation: entity=", entity.handle.id,
            " instance_symbol=", instanceSymbol,
            " instance_name=", catalogInstanceName,
            " archetype=", entity.presentation.archetypeId,
            " presentation=", entity.presentation.presentationId,
            " body=", visual->bodyVisual,
            " head=", visual->headVisual,
            " armor=", visual->defaultArmorVisual,
            " body_texture=", visual->bodyTextureVariant,
            " head_texture=", visual->headTextureVariant,
            " skin=", visual->skinVariant,
            " position=", position.x, ",", position.y, ",", position.z);
        npc->setVisualBody(
            static_cast<std::int32_t>(visual->headTextureVariant),
            0,
            static_cast<std::int32_t>(visual->bodyTextureVariant),
            static_cast<std::int32_t>(visual->skinVariant),
            visual->bodyVisual,
            visual->headVisual);
        npc->setMmoDefaultArmor(visual->defaultArmorVisual);
      } else {
        Log::e(
            "MMO NPC presentation missing: entity=", entity.handle.id,
            " instance_symbol=", instanceSymbol,
            " archetype=", entity.presentation.archetypeId,
            " presentation=", entity.presentation.presentationId);
      }
    } else {
      Log::e(
          "MMO NPC presentation catalog unavailable: entity=", entity.handle.id,
          " instance_symbol=", instanceSymbol);
    }
    npc->setMmoServerReplica(true);
    return {npc, materializedByMmo};
  } catch(const std::exception& error) {
    Log::e("MMO typed entity materialization failed: ", error.what());
  } catch(...) {
    Log::e("MMO typed entity materialization failed with an unknown exception");
  }
  return {};
}

[[nodiscard]] std::optional<Mmo::ClientPresentation::LocalNpcPresentationIdentity>
localMmoPresentationIdentity(
    World& world,
    Npc* npc,
    const Mmo::ClientPresentation::ServerEntityKind kind,
    const bool materializedByMmo) noexcept {
  using namespace Mmo::ClientPresentation;
  if(npc == nullptr ||
     ((kind == ServerEntityKind::LocalPlayer) != npc->isPlayer())) {
    return std::nullopt;
  }
  const auto localNpcId = world.npcId(npc);
  if(localNpcId == InvalidLocalNpcId)
    return std::nullopt;
  return LocalNpcPresentationIdentity{
      .localNpcId = localNpcId,
      .persistentId = npc->persistentId(),
      .instanceSymbol = npc->instanceSymbol(),
      .localObjectToken = reinterpret_cast<std::uintptr_t>(npc),
      .materializedByMmo = materializedByMmo,
  };
}

Npc* resolveMmoPresentationBinding(
    World& world,
    const Mmo::ClientPresentation::ServerEntityPresentationBinding& binding) noexcept {
  auto* npc = findNpcByPresentationIdentity(world, binding.local);
  if(npc == nullptr ||
     npc->persistentId() != binding.local.persistentId ||
     npc->instanceSymbol() != binding.local.instanceSymbol ||
     ((binding.kind == Mmo::ClientPresentation::ServerEntityKind::LocalPlayer) !=
      npc->isPlayer())) {
    return nullptr;
  }
  return npc;
}

} // namespace

void GameSession::releaseMmoServerPresentationBinding(
    const Mmo::ClientPresentation::ServerEntityPresentationBinding& binding) noexcept {
  if(binding.kind == Mmo::ClientPresentation::ServerEntityKind::LocalPlayer) {
    mmoMovementCorrectionBoundary.unbindLocalPlayer();
    return;
  }
  if(wrld == nullptr)
    return;
  auto* npc = findNpcByPresentationIdentity(*wrld, binding.local);
  if(npc == nullptr || npc->persistentId() != binding.local.persistentId ||
     npc->instanceSymbol() != binding.local.instanceSymbol)
    return;
  if(binding.local.materializedByMmo) {
    wrld->removeNpc(*npc);
    return;
  }
  npc->setMmoServerReplica(false);
}


void GameSession::releaseMmoServerEntity(
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity) noexcept {
  const auto handle = presentationRegistryHandle(entity.handle);
  if(auto released = mmoServerEntityPresentation.invalidate(
         handle, mmoPresentationWorldGeneration)) {
    releaseMmoServerPresentationBinding(*released);
  }
  static_cast<void>(mmoServerEntityInterpolator.erase(
      handle, mmoPresentationWorldGeneration));
  if(entity.kind ==
     Mmo::ClientPresentation::ServerPresentationEntityKind::LocalPlayer) {
    mmoMovementCorrectionBoundary.unbindLocalPlayer();
  }
}


std::optional<GameSession::MmoServerEntityTarget>
GameSession::mmoServerEntityTarget(const Npc& npc) const noexcept {
  const auto local = Mmo::ClientPresentation::LocalNpcPresentationIdentity{
      .localNpcId = wrld != nullptr ? wrld->npcId(&npc)
                                    : Mmo::ClientPresentation::InvalidLocalNpcId,
      .persistentId = npc.persistentId(),
      .instanceSymbol = npc.instanceSymbol(),
      .localObjectToken = reinterpret_cast<std::uintptr_t>(&npc),
      .materializedByMmo = npc.isMmoServerReplica(),
  };
  const auto* binding = mmoServerEntityPresentation.findLocal(local);
  if(binding == nullptr) {
    Log::e("MMO NPC target lookup: registry_binding=0 local_npc_id=",
           local.localNpcId, " persistent_id=", local.persistentId,
           " instance_symbol=", local.instanceSymbol,
           " object=", local.localObjectToken);
    return std::nullopt;
  }

  const auto session = Mmo::clientMmoSessionSnapshot();
  const Mmo::ClientPresentation::ServerPresentationEntityHandle entity{
      .world = {
          .id = session.worldId,
          .generation = session.worldGeneration,
      },
      .id = binding->handle.id,
      .generation = binding->handle.generation,
  };
  const auto* record = mmoTypedServerPresentation.findEntity(entity);
  if(record == nullptr) {
    Log::e("MMO NPC target lookup: registry_binding=1 record=0 entity=",
           entity.id, ":", entity.generation);
    return std::nullopt;
  }
  if(!session.inWorld() || session.worldId == 0U ||
     session.worldGeneration == 0U ||
     binding->worldGeneration != mmoPresentationWorldGeneration) {
    Log::e("MMO NPC target lookup: registry_binding=1 record=1 in_world=",
           session.inWorld() ? 1 : 0, " session_world=", session.worldId,
           " session_generation=", session.worldGeneration,
           " binding_generation=", binding->worldGeneration,
           " presentation_generation=", mmoPresentationWorldGeneration);
    return std::nullopt;
  }

  return MmoServerEntityTarget{
      .handle = {
          .worldId = session.worldId,
          .worldGeneration = session.worldGeneration,
          .id = binding->handle.id,
          .generation = binding->handle.generation,
      },
      .revision = record->entityRevision,
  };
}

std::optional<GameSession::MmoServerEntityTarget>
GameSession::mmoServerEntityTarget(
    const Interactive& interactive) const noexcept {
  using Kind =
      Mmo::ClientPresentation::ServerPresentationWorldObjectKind;
  std::optional<Mmo::ClientPresentation::ServerPresentationEntityHandle> entity;
  for(const auto kind : {Kind::Interactive, Kind::Container, Kind::Mover}) {
    entity = mmoServerWorldObjects.find(
        Mmo::ClientPresentation::LocalVobToken{
            .vobObjectId = interactive.getId(),
            .kind = kind,
        });
    if(entity.has_value())
      break;
  }
  if(!entity.has_value()) {
    const auto position =
        Mmo::ClientPresentation::ServerWorldObjectPosition::fromFloat(
            interactive.position().x,
            interactive.position().y,
            interactive.position().z);
    for(const auto kind : {Kind::Interactive, Kind::Container, Kind::Mover}) {
      entity = mmoServerWorldObjects.findNearest(kind, position);
      if(entity.has_value())
        break;
    }
  }
  if(!entity.has_value()) {
    Log::e("MMO interactive target lookup failed: vob=", interactive.getId(),
           " position=", interactive.position().x, ",",
           interactive.position().y, ",", interactive.position().z);
    return std::nullopt;
  }

  std::uint64_t revision = 0U;
  if(const auto* state = mmoTypedServerPresentation.findInteractive(*entity);
     state != nullptr) {
    revision = state->stateRevision;
  } else if(const auto* state = mmoTypedServerPresentation.findMover(*entity);
            state != nullptr) {
    revision = state->stateRevision;
  } else {
    return std::nullopt;
  }
  const auto session = Mmo::clientMmoSessionSnapshot();
  if(!session.inWorld() || session.worldId == 0U ||
     session.worldGeneration == 0U || !entity->valid()) {
    return std::nullopt;
  }

  return MmoServerEntityTarget{
      .handle = {
          .worldId = session.worldId,
          .worldGeneration = session.worldGeneration,
          .id = entity->id,
          .generation = entity->generation,
      },
      .revision = revision,
  };
}

std::optional<GameSession::MmoServerEntityTarget>
GameSession::mmoServerEntityTarget(const Item& item) const noexcept {
  const auto binding = std::find_if(
      mmoServerWorldItemBindings.begin(),
      mmoServerWorldItemBindings.end(),
      [&item](const auto& value) noexcept { return value.item == &item; });
  if(binding == mmoServerWorldItemBindings.end() || binding->revision == 0U)
    return std::nullopt;
  const auto session = Mmo::clientMmoSessionSnapshot();
  if(!session.inWorld() || session.worldId != binding->entity.world.id ||
     session.worldGeneration != binding->entity.world.generation)
    return std::nullopt;
  return MmoServerEntityTarget{
      .handle = {
          .worldId = binding->entity.world.id,
          .worldGeneration = binding->entity.world.generation,
          .id = binding->entity.id,
          .generation = binding->entity.generation,
      },
      .revision = binding->revision,
      .quantity = binding->quantity,
  };
}

Item* GameSession::mmoNearestServerWorldItem(
    const Tempest::Vec3& position,
    const float maximumDistance) const noexcept {
  if(maximumDistance < 0.0F)
    return nullptr;
  const auto maximumDistanceSquared = maximumDistance * maximumDistance;
  Item* nearest = nullptr;
  auto nearestDistanceSquared = maximumDistanceSquared;
  for(const auto& binding : mmoServerWorldItemBindings) {
    if(binding.item == nullptr || binding.revision == 0U)
      continue;
    const auto itemPosition = binding.item->position();
    const auto dx = itemPosition.x - position.x;
    const auto dy = itemPosition.y - position.y;
    const auto dz = itemPosition.z - position.z;
    const auto distanceSquared = dx * dx + dy * dy + dz * dz;
    if(distanceSquared <= nearestDistanceSquared) {
      nearestDistanceSquared = distanceSquared;
      nearest = binding.item;
    }
  }
  return nearest;
}

Npc* GameSession::resolveMmoServerEntity(
    const Mmo::ClientPresentation::ServerPresentationEntityHandle entity) noexcept {
  if(wrld == nullptr || !entity.valid())
    return nullptr;
  const auto* binding = mmoServerEntityPresentation.find(
      presentationRegistryHandle(entity), mmoPresentationWorldGeneration);
  return binding != nullptr ? resolveMmoPresentationBinding(*wrld, *binding)
                            : nullptr;
}

void GameSession::materializeMmoServerEntity(
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity,
    const bool snap,
    const Mmo::ClientPresentation::ServerPresentationNpcStateRecord*
        initialNpcState) noexcept {
  if(wrld == nullptr || mmoPresentationRouteKey.empty())
    return;

  using namespace Mmo::ClientPresentation;
  const auto handle = presentationRegistryHandle(entity.handle);
  const auto kind = presentationRegistryKind(entity.kind);
  auto stableKey = mmoPresentationEntityKey(entity);
  const ServerEntityTransformObservation observation{
      .route = {
          .worldGeneration = mmoPresentationWorldGeneration,
          .worldInstanceId = mmoPresentationRouteKey,
      },
      .handle = handle,
      .kind = kind,
      // This registry field is an ordering key. Typed V2 entities provide the
      // stricter per-entity revision, which also distinguishes updates emitted
      // within the same authoritative server tick.
      .serverTick = entity.entityRevision,
      .stableEntityKey = stableKey,
      .posX = entity.transform.posX,
      .posY = entity.transform.posY,
      .posZ = entity.transform.posZ,
      .yaw = entity.transform.yaw,
      .active = true,
  };

  auto observed = mmoServerEntityPresentation.observe(observation);
  if(observed.releasedBinding.has_value()) {
    releaseMmoServerPresentationBinding(*observed.releasedBinding);
    static_cast<void>(mmoServerEntityInterpolator.erase(
        observed.releasedBinding->handle,
        observed.releasedBinding->worldGeneration));
  }
  if(observed.status == ServerEntityObservationStatus::Stale ||
     observed.status == ServerEntityObservationStatus::RouteMismatch ||
     observed.status == ServerEntityObservationStatus::IdentityMismatch ||
     observed.status == ServerEntityObservationStatus::Invalid) {
    return;
  }

  const auto* binding = mmoServerEntityPresentation.find(
      handle, mmoPresentationWorldGeneration);
  Npc* npc = binding != nullptr
                 ? resolveMmoPresentationBinding(*wrld, *binding)
                 : nullptr;
  bool materializedByMmo = false;
  bool newlyMaterialized = false;
  if(npc == nullptr) {
    const auto materialized = materializeMmoServerEntityNpc(
        *wrld, *vm, mmoClientPresentationCatalog.get(), entity);
    npc = materialized.npc;
    materializedByMmo = materialized.materializedByMmo;
    const auto local = localMmoPresentationIdentity(
        *wrld, npc, kind, materializedByMmo);
    if(!local || !mmoServerEntityPresentation.bind(observation, *local)) {
      if(materializedByMmo && npc != nullptr)
        wrld->removeNpc(*npc);
      Log::e("MMO typed entity unresolved: entity=", entity.handle.id,
             " generation=", entity.handle.generation,
             " kind=", static_cast<unsigned>(entity.kind),
             " presentation=", entity.presentation.presentationId,
             " archetype=", entity.presentation.archetypeId);
      return;
    }
    newlyMaterialized = true;
  }

  applyMmoServerEntityTransform(entity, snap);
  if(initialNpcState != nullptr &&
     entity.kind == ServerPresentationEntityKind::Npc &&
     initialNpcState->entity == entity.handle) {
    applyMmoServerNpcState(*initialNpcState);
    replayMmoServerNpcPresentation(entity.handle);
  }

  if(newlyMaterialized) {
    auto event = Mmo::ClientMmoProcessGatePresentationEvent::NpcMaterialized;
    if(entity.kind == ServerPresentationEntityKind::LocalPlayer)
      event = Mmo::ClientMmoProcessGatePresentationEvent::LocalPlayerMaterialized;
    else if(entity.kind == ServerPresentationEntityKind::RemotePlayer)
      event = Mmo::ClientMmoProcessGatePresentationEvent::RemotePlayerMaterialized;
    Mmo::recordClientMmoProcessGatePresentation(event);
  }
}

void GameSession::replayMmoServerNpcPresentation(
    const Mmo::ClientPresentation::ServerPresentationEntityHandle entity) noexcept {
  using namespace Mmo::ClientPresentation;
  constexpr ServerPresentationEquipmentSlot slots[] = {
      ServerPresentationEquipmentSlot::MeleeWeapon,
      ServerPresentationEquipmentSlot::RangedWeapon,
      ServerPresentationEquipmentSlot::Armor,
      ServerPresentationEquipmentSlot::Amulet,
      ServerPresentationEquipmentSlot::RingLeft,
      ServerPresentationEquipmentSlot::RingRight,
      ServerPresentationEquipmentSlot::Belt,
      ServerPresentationEquipmentSlot::Spell,
  };
  for(const auto slot : slots) {
    if(const auto* equipment =
           mmoTypedServerPresentation.findEquipment(entity, slot)) {
      applyMmoServerEquipmentSlot(*equipment);
    }
  }
  if(const auto* weapon = mmoTypedServerPresentation.findWeaponMode(entity))
    applyMmoServerWeaponMode(*weapon, false);
  if(const auto* life = mmoTypedServerPresentation.findLifeState(entity))
    applyMmoServerLifeState(*life);
  if(const auto* action =
         mmoTypedServerPresentation.findActiveCombatAction(entity)) {
    applyMmoServerCombatAction(*action);
  }
}

void GameSession::applyMmoServerEntityTransform(
    const Mmo::ClientPresentation::ServerPresentationEntityRecord& entity,
    const bool snap) noexcept {
  if(wrld == nullptr || mmoPresentationRouteKey.empty())
    return;

  using namespace Mmo::ClientPresentation;
  const auto handle = presentationRegistryHandle(entity.handle);
  const auto kind = presentationRegistryKind(entity.kind);
  const auto* binding = mmoServerEntityPresentation.find(
      handle, mmoPresentationWorldGeneration);
  if(binding == nullptr) {
    materializeMmoServerEntity(entity, snap);
    return;
  }
  auto* npc = resolveMmoPresentationBinding(*wrld, *binding);
  if(npc == nullptr) {
    if(auto released = mmoServerEntityPresentation.invalidate(
           handle, mmoPresentationWorldGeneration)) {
      releaseMmoServerPresentationBinding(*released);
    }
    static_cast<void>(mmoServerEntityInterpolator.erase(
        handle, mmoPresentationWorldGeneration));
    materializeMmoServerEntity(entity, snap);
    return;
  }

  const ServerEntityTransformObservation observation{
      .route = {
          .worldGeneration = mmoPresentationWorldGeneration,
          .worldInstanceId = mmoPresentationRouteKey,
      },
      .handle = handle,
      .kind = kind,
      // See materialization above: per-entity revision is the monotonic order
      // key; receive time remains the interpolator's temporal input.
      .serverTick = entity.entityRevision,
      .stableEntityKey = binding->stableEntityKey,
      .posX = entity.transform.posX,
      .posY = entity.transform.posY,
      .posZ = entity.transform.posZ,
      .yaw = entity.transform.yaw,
      .active = true,
  };

  if(kind == ServerEntityKind::LocalPlayer) {
    if(!mmoMovementCorrectionBoundary.bindLocalPlayer(handle, observation.route))
      return;
    if(snap || entity.transform.teleport) {
      if(npc->setPosition(static_cast<float>(entity.transform.posX),
                          static_cast<float>(entity.transform.posY),
                          static_cast<float>(entity.transform.posZ))) {
        npc->setDirection(static_cast<float>(entity.transform.yaw));
        npc->clearSpeed();
      }
    }
    mmoServerEntityPresentation.touch(observation);
    return;
  }

  npc->setMmoServerReplica(true);
  const bool hardSnap = snap || entity.transform.teleport;
  if(hardSnap) {
    static_cast<void>(mmoServerEntityInterpolator.erase(
        handle, mmoPresentationWorldGeneration));
    static_cast<void>(npc->applyMmoServerPresentationTransform(
        {static_cast<float>(entity.transform.posX),
         static_cast<float>(entity.transform.posY),
         static_cast<float>(entity.transform.posZ)},
        static_cast<float>(entity.transform.yaw), true));
    mmoServerEntityPresentation.touch(observation);
    return;
  }

  if(mmoServerEntityInterpolator.ingest(observation, ticks) ==
     ServerEntityInterpolationIngestStatus::Accepted) {
    mmoServerEntityPresentation.touch(observation);
  }
}

void GameSession::applyMmoServerNpcState(
    const Mmo::ClientPresentation::ServerPresentationNpcStateRecord& state) noexcept {
  if(wrld == nullptr)
    return;
  using namespace Mmo::ClientPresentation;
  const auto* binding = mmoServerEntityPresentation.find(
      presentationRegistryHandle(state.entity), mmoPresentationWorldGeneration);
  if(binding == nullptr || binding->kind != ServerEntityKind::Npc)
    return;
  auto* npc = resolveMmoPresentationBinding(*wrld, *binding);
  if(npc == nullptr)
    return;

  Npc::PersistentStats stats;
  stats.healthCurrent = state.health;
  stats.healthMax = state.maximumHealth;
  stats.manaCurrent = state.mana;
  stats.manaMax = state.maximumMana;
  npc->setMmoServerReplica(true);
  npc->applyMmoServerPresentationStats(stats);

  Npc::MmoPresentationLifeState lifeState =
      Npc::MmoPresentationLifeState::Alive;
  switch(state.lifeState) {
    case ServerPresentationNpcLifeState::Alive:
      lifeState = Npc::MmoPresentationLifeState::Alive;
      break;
    case ServerPresentationNpcLifeState::Unconscious:
      lifeState = Npc::MmoPresentationLifeState::Unconscious;
      break;
    case ServerPresentationNpcLifeState::Dead:
      lifeState = Npc::MmoPresentationLifeState::Dead;
      break;
  }
  npc->applyMmoServerPresentationLifecycle(
      state.health, state.maximumHealth, lifeState);

  if(lifeState == Npc::MmoPresentationLifeState::Alive) {
    switch(state.activityState) {
      case ServerPresentationNpcActivityState::Idle:
      case ServerPresentationNpcActivityState::Routine:
      case ServerPresentationNpcActivityState::Dialog:
        npc->applyMmoServerPresentationLocomotion(Npc::Anim::Idle);
        break;
      case ServerPresentationNpcActivityState::Traversal:
        npc->applyMmoServerPresentationLocomotion(Npc::Anim::Move);
        break;
      case ServerPresentationNpcActivityState::Interaction:
      case ServerPresentationNpcActivityState::Combat:
        break;
    }
  }

  Npc* target = nullptr;
  if((state.flags & ServerPresentationNpcTargetPresent) != 0U)
    target = resolveMmoServerEntity(state.target);
  npc->applyMmoServerPresentationTarget(target);

  if(state.activityState == ServerPresentationNpcActivityState::Dialog ||
     state.activityState == ServerPresentationNpcActivityState::Interaction) {
    npc->setAiOutputBarrier(250U, true);
  }
  Mmo::recordClientMmoProcessGatePresentation(
      Mmo::ClientMmoProcessGatePresentationEvent::NpcStateApplied);
}


void GameSession::sampleMmoServerEntityTransforms() noexcept {
  if(wrld == nullptr || mmoPresentationWorldGeneration == 0U)
    return;
  mmoServerEntityInterpolator.sample(ticks, mmoServerEntitySamples);
  for(const auto& sampled : mmoServerEntitySamples) {
    const auto* binding = mmoServerEntityPresentation.find(
        sampled.handle, sampled.worldGeneration);
    if(binding == nullptr || binding->kind != sampled.kind)
      continue;
    auto* npc = resolveMmoPresentationBinding(*wrld, *binding);
    if(npc == nullptr) {
      if(auto released = mmoServerEntityPresentation.invalidate(
             sampled.handle, sampled.worldGeneration)) {
        releaseMmoServerPresentationBinding(*released);
      }
      static_cast<void>(mmoServerEntityInterpolator.erase(
          sampled.handle, sampled.worldGeneration));
      continue;
    }
    static_cast<void>(npc->applyMmoServerPresentationTransform(
        {static_cast<float>(sampled.posX),
         static_cast<float>(sampled.posY),
         static_cast<float>(sampled.posZ)},
        static_cast<float>(sampled.yaw), false));
  }
}

void GameSession::resetMmoServerWorldItems() noexcept {
  if(wrld != nullptr) {
    for(auto& binding : mmoServerWorldItemBindings) {
      if(binding.item != nullptr)
        wrld->removeItem(*binding.item);
    }
  }
  mmoServerWorldItemBindings.clear();
}

void GameSession::applyMmoServerWorldItemSpawn(
    const Mmo::ClientPresentation::ServerWorldItemSpawnEvent& event) noexcept {
  if(wrld == nullptr || vm == nullptr || !event.valid())
    return;
  auto existing = std::find_if(
      mmoServerWorldItemBindings.begin(),
      mmoServerWorldItemBindings.end(),
      [&event](const auto& value) noexcept {
        return value.entity.id == event.entity.id;
      });
  if(existing != mmoServerWorldItemBindings.end()) {
    if(existing->entity.generation == event.entity.generation) {
      if(event.stateRevision <= existing->revision)
        return;
      existing->revision = event.stateRevision;
      existing->quantity = event.quantity;
      if(existing->item != nullptr) {
        existing->item->setPosition(
            static_cast<float>(event.transform.posX),
            static_cast<float>(event.transform.posY),
            static_cast<float>(event.transform.posZ));
        existing->item->setCount(event.quantity);
      }
      return;
    }
    if(existing->item != nullptr)
      wrld->removeItem(*existing->item);
    mmoServerWorldItemBindings.erase(existing);
  }

  std::optional<std::size_t> itemInstance;
  const auto catalogInstance = mmoItemInstanceName(
      event.presentation.archetypeId, event.presentation.presentationId);
  if(catalogInstance.has_value()) {
    const auto symbol = vm->findSymbolIndex(*catalogInstance);
    if(symbol != size_t(-1))
      itemInstance = symbol;
  }
  if(!itemInstance.has_value()) {
    if(const auto* local = mmoServerWorldObjects.find(event.entity);
       local != nullptr) {
      if(const auto fallback = mmoLocalItemInstanceSymbols.find(
             local->vobObjectId);
         fallback != mmoLocalItemInstanceSymbols.end()) {
        itemInstance = fallback->second;
        Log::i(
            "MMO world-item local presentation fallback: entity=",
            event.entity.id,
            " vob=", local->vobObjectId,
            " symbol=", *itemInstance,
            " catalog_instance=", catalogInstance.has_value() ? 1 : 0);
      }
    }
  }
  if(!itemInstance.has_value()) {
    Log::e(
        "MMO world-item presentation unresolved: entity=", event.entity.id,
        " generation=", event.entity.generation,
        " world_object=", event.worldObjectId,
        " archetype=", event.presentation.archetypeId,
        " presentation=", event.presentation.presentationId,
        " catalog_installed=", mmoClientPresentationCatalog != nullptr &&
                                    mmoClientPresentationCatalog->installed()
                                    ? 1
                                    : 0,
        " catalog_manifest=", mmoClientPresentationCatalog != nullptr
                                  ? mmoClientPresentationCatalog->manifest().value
                                  : 0U,
        " position=", event.transform.posX, ",", event.transform.posY, ",",
        event.transform.posZ);
    return;
  }
  auto* item = wrld->addItem(
      *itemInstance,
      Tempest::Vec3{
          static_cast<float>(event.transform.posX),
          static_cast<float>(event.transform.posY),
          static_cast<float>(event.transform.posZ)});
  if(item == nullptr)
    return;
  item->setCount(event.quantity);
  Log::i(
      "MMO world-item materialize: entity=", event.entity.id,
      " symbol=", *itemInstance,
      " quantity=", event.quantity,
      " display_name=", item->displayName(),
      " mesh=", item->bBox() != nullptr ? 1 : 0,
      " position=", event.transform.posX, ",", event.transform.posY, ",",
      event.transform.posZ);
  mmoServerWorldItemBindings.push_back({
      .entity = event.entity,
      .item = item,
      .worldObjectId = event.worldObjectId,
      .archetypeId = event.presentation.archetypeId,
      .presentationId = event.presentation.presentationId,
      .quantity = event.quantity,
      .revision = event.stateRevision,
  });
}

void GameSession::applyMmoServerWorldItemDespawn(
    const Mmo::ClientPresentation::ServerWorldItemDespawnEvent& event) noexcept {
  if(wrld == nullptr || !event.valid())
    return;
  const auto binding = std::find_if(
      mmoServerWorldItemBindings.begin(),
      mmoServerWorldItemBindings.end(),
      [&event](const auto& value) noexcept { return value.entity == event.entity; });
  if(binding == mmoServerWorldItemBindings.end() ||
     event.stateRevision < binding->revision)
    return;
  if(binding->item != nullptr)
    wrld->removeItem(*binding->item);
  mmoServerWorldItemBindings.erase(binding);
}

void GameSession::applyMmoServerWorldItemStateChanged(
    const Mmo::ClientPresentation::ServerWorldItemStateChangedEvent& event) noexcept {
  if(!event.valid())
    return;
  const auto binding = std::find_if(
      mmoServerWorldItemBindings.begin(),
      mmoServerWorldItemBindings.end(),
      [&event](const auto& value) noexcept { return value.entity == event.entity; });
  if(binding == mmoServerWorldItemBindings.end() ||
     event.stateRevision <= binding->revision)
    return;
  binding->revision = event.stateRevision;
  binding->quantity = event.quantity;
  if(binding->item != nullptr)
    binding->item->setCount(event.quantity);
}
