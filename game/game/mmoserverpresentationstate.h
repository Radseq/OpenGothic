#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mmoserverpresentationevents.h"

namespace Mmo::ClientPresentation {

enum class ServerPresentationApplyStatus : std::uint8_t {
  Applied,
  Duplicate,
  Stale,
  Invalid,
  StaleRoute,
  RouteMismatch,
  BaselineInactive,
  BaselineMismatch,
  MissingEntity,
  IdentityMismatch,
  CapacityExceeded,
};

enum class ServerPresentationMutation : std::uint8_t {
  None,
  RouteReplaced,
  BootstrapInstalled,
  WorldDescriptorUpdated,
  EntitySpawned,
  EntityRebindRequired,
  EntityDespawned,
  EntityTransformed,
  MovementCorrectionQueued,
  NpcStateUpdated,
  InventorySnapshotInstalled,
  InventoryDeltaApplied,
  EquipmentSnapshotInstalled,
  EquipmentBindingUpdated,
  EquipmentSlotUpdated,
  WeaponModeUpdated,
  CombatActionStarted,
  CombatActionResolved,
  DamageApplied,
  HitReactionApplied,
  CharacterLifeStateUpdated,
  DialogStarted,
  DialogUpdated,
  DialogEnded,
  DialogBusy,
  InteractiveUpdated,
  MoverUpdated,
  WorldItemSpawned,
  WorldItemDespawned,
  WorldItemUpdated,
  ProjectileSpawned,
  ProjectileUpdated,
  ProjectileImpacted,
  ProjectileDespawned,
};

struct ServerPresentationApplyResult final {
  ServerPresentationApplyStatus status = ServerPresentationApplyStatus::Invalid;
  ServerPresentationMutation mutation = ServerPresentationMutation::None;
  std::optional<ServerPresentationEntityRecord> releasedEntity;
  bool hardSnap = false;

  constexpr ServerPresentationApplyResult() noexcept = default;
  constexpr ServerPresentationApplyResult(
      ServerPresentationApplyStatus statusValue,
      ServerPresentationMutation mutationValue = ServerPresentationMutation::None,
      std::optional<ServerPresentationEntityRecord> released = std::nullopt,
      bool hardSnapValue = false) noexcept
      : status(statusValue),
        mutation(mutationValue),
        releasedEntity(std::move(released)),
        hardSnap(hardSnapValue) {}

  [[nodiscard]] constexpr bool applied() const noexcept {
    return status == ServerPresentationApplyStatus::Applied;
  }
};

struct ServerPresentationRouteReplaceResult final {
  ServerPresentationApplyStatus status = ServerPresentationApplyStatus::Invalid;
  std::vector<ServerPresentationEntityRecord> releasedEntities;

  [[nodiscard]] constexpr bool applied() const noexcept {
    return status == ServerPresentationApplyStatus::Applied;
  }
};

struct ServerPresentationBootstrapInstallResult final {
  ServerPresentationApplyStatus status = ServerPresentationApplyStatus::Invalid;
  std::vector<ServerPresentationEntityRecord> releasedEntities;

  [[nodiscard]] constexpr bool applied() const noexcept {
    return status == ServerPresentationApplyStatus::Applied;
  }
};

struct ServerPresentationDialogBusyState final {
  ServerPresentationEntityHandle npc{};
  std::uint64_t activeSessionId = 0U;
  ServerDialogBusyReason reason = ServerDialogBusyReason::NpcUnavailable;
  std::uint32_t retryAfterMilliseconds = 0U;
  std::uint64_t dialogRevision = 0U;

  [[nodiscard]] constexpr bool active() const noexcept {
    return npc.valid() && dialogRevision != 0U;
  }
};

struct ServerPresentationDialogState final {
  std::uint64_t sessionId = 0U;
  ServerPresentationEntityHandle player{};
  ServerPresentationEntityHandle npc{};
  ServerPresentationEntityHandle speaker{};
  std::uint64_t topicId = 0U;
  std::uint64_t lineId = 0U;
  std::uint64_t choicesRevision = 0U;
  std::uint64_t dialogRevision = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] constexpr bool active() const noexcept {
    return sessionId != 0U;
  }
};

struct ServerPresentationStateConfig final {
  std::size_t maxEntities = 4096U;
  std::size_t maxWorldObjects = 16384U;
  std::size_t maxProjectiles = 4096U;
};

class ServerPresentationState final {
 public:
  explicit ServerPresentationState(
      ServerPresentationStateConfig config = {})
      : config_(config) {
    config_.maxEntities = std::max<std::size_t>(1U, config_.maxEntities);
    config_.maxWorldObjects =
        std::max<std::size_t>(1U, config_.maxWorldObjects);
    config_.maxProjectiles =
        std::max<std::size_t>(1U, config_.maxProjectiles);
    entities_.reserve(config_.maxEntities);
    npcStates_.reserve(config_.maxEntities);
    equipment_.reserve(config_.maxEntities);
    weaponModes_.reserve(config_.maxEntities);
    activeCombatActions_.reserve(config_.maxEntities);
    combatActionRevisions_.reserve(config_.maxEntities);
    damage_.reserve(config_.maxEntities);
    hitReactions_.reserve(config_.maxEntities);
    lifeStates_.reserve(config_.maxEntities);
    worldObjects_.reserve(config_.maxWorldObjects);
    interactives_.reserve(config_.maxWorldObjects);
    movers_.reserve(config_.maxWorldObjects);
    projectiles_.reserve(config_.maxProjectiles);
  }

  [[nodiscard]] ServerPresentationRouteReplaceResult replaceRoute(
      const ServerPresentationRouteIdentity route) {
    ServerPresentationRouteReplaceResult result;
    if(!route.valid())
      return result;
    if(route_.has_value() && route_->connectionId == route.connectionId) {
      if(route.routeEpoch < route_->routeEpoch ||
         (route.world.id == route_->world.id &&
          route.world.generation < route_->world.generation)) {
        result.status = ServerPresentationApplyStatus::StaleRoute;
        return result;
      }
    }
    if(route_ == route) {
      result.status = ServerPresentationApplyStatus::Duplicate;
      return result;
    }

    result.releasedEntities.reserve(entities_.size());
    for(auto& [id, entity] : entities_) {
      static_cast<void>(id);
      result.releasedEntities.push_back(std::move(entity));
    }
    clearRouteState();
    route_ = route;
    result.status = ServerPresentationApplyStatus::Applied;
    return result;
  }

  [[nodiscard]] ServerPresentationBootstrapInstallResult installBootstrap(
      const ServerPresentationBootstrap& bootstrap) {
    ServerPresentationBootstrapInstallResult result;
    if(!route_.has_value() || bootstrap.route != *route_) {
      result.status = classifyRoute(bootstrap.route);
      return result;
    }
    if(!bootstrap.baseline.valid() || !bootstrap.world.valid()) {
      result.status = ServerPresentationApplyStatus::Invalid;
      return result;
    }
    if(activeBaseline_.has_value()) {
      if(bootstrap.baseline == *activeBaseline_) {
        result.status = ServerPresentationApplyStatus::Duplicate;
        return result;
      }
      if(bootstrap.baseline.serverTick < activeBaseline_->serverTick ||
         bootstrap.baseline.aggregateRevision <
             activeBaseline_->aggregateRevision) {
        result.status = ServerPresentationApplyStatus::Stale;
        return result;
      }
    }
    const auto maxEquipmentRecords =
        config_.maxEntities >
                std::numeric_limits<std::size_t>::max() /
                    ServerPresentationEquipmentSlotCount
            ? std::numeric_limits<std::size_t>::max()
            : config_.maxEntities * ServerPresentationEquipmentSlotCount;
    if(bootstrap.entities.size() > config_.maxEntities ||
       bootstrap.combatEquipment.size() > maxEquipmentRecords ||
       bootstrap.weaponModes.size() > config_.maxEntities ||
       bootstrap.combatActions.size() > config_.maxEntities ||
       bootstrap.lifeStates.size() > config_.maxEntities ||
       bootstrap.worldObjects.size() > config_.maxWorldObjects ||
       bootstrap.interactives.size() > config_.maxWorldObjects ||
       bootstrap.movers.size() > config_.maxWorldObjects) {
      result.status = ServerPresentationApplyStatus::CapacityExceeded;
      return result;
    }

    EntityMap nextEntities;
    WorldObjectMap nextWorldObjects;
    NpcStateMap nextNpcStates;
    EquipmentMap nextEquipment;
    WeaponModeMap nextWeaponModes;
    CombatActionMap nextCombatActions;
    RevisionMap nextCombatActionRevisions;
    LifeStateMap nextLifeStates;
    InteractiveMap nextInteractives;
    MoverMap nextMovers;
    nextEntities.reserve(bootstrap.entities.size());
    nextWorldObjects.reserve(bootstrap.worldObjects.size());
    nextNpcStates.reserve(bootstrap.npcStates.size());
    nextEquipment.reserve(bootstrap.entities.size());
    nextWeaponModes.reserve(bootstrap.weaponModes.size());
    nextCombatActions.reserve(bootstrap.combatActions.size());
    nextCombatActionRevisions.reserve(bootstrap.combatActions.size());
    nextLifeStates.reserve(bootstrap.lifeStates.size());
    nextInteractives.reserve(bootstrap.interactives.size());
    nextMovers.reserve(bootstrap.movers.size());

    std::optional<ServerPresentationEntityHandle> nextLocalPlayer;
    if(!buildBootstrapEntities(bootstrap, nextEntities, nextLocalPlayer) ||
       !buildBootstrapWorldObjectCatalog(bootstrap, nextWorldObjects) ||
       !buildBootstrapNpcStates(bootstrap, nextEntities, nextNpcStates) ||
       !buildBootstrapCombatPresentation(
           bootstrap, nextEntities, nextLocalPlayer, nextEquipment,
           nextWeaponModes,
           nextCombatActions, nextCombatActionRevisions, nextLifeStates) ||
       !buildBootstrapWorldObjectStates(
           bootstrap, nextWorldObjects, nextInteractives, nextMovers)) {
      result.status = ServerPresentationApplyStatus::Invalid;
      return result;
    }

    result.releasedEntities.reserve(entities_.size());
    for(auto& [id, entity] : entities_) {
      static_cast<void>(id);
      result.releasedEntities.push_back(std::move(entity));
    }

    // Install world-object baselines before exposing the entity roster as live.
    worldObjects_ = std::move(nextWorldObjects);
    interactives_ = std::move(nextInteractives);
    movers_ = std::move(nextMovers);
    entities_ = std::move(nextEntities);
    npcStates_ = std::move(nextNpcStates);
    equipment_ = std::move(nextEquipment);
    weaponModes_ = std::move(nextWeaponModes);
    activeCombatActions_ = std::move(nextCombatActions);
    combatActionRevisions_ = std::move(nextCombatActionRevisions);
    lifeStates_ = std::move(nextLifeStates);
    damage_.clear();
    hitReactions_.clear();
    projectiles_.clear();
    localPlayer_ = nextLocalPlayer;
    world_ = bootstrap.world;
    activeBaseline_ = bootstrap.baseline;
    pendingCorrection_.reset();
    dialog_ = {};
    dialogBusy_ = {};
    result.status = ServerPresentationApplyStatus::Applied;
    return result;
  }

  [[nodiscard]] ServerPresentationApplyResult apply(
      const ServerPresentationEvent& event) {
    return std::visit([this](const auto& value) { return applyOne(value); }, event);
  }

  void reset() noexcept {
    route_.reset();
    clearRouteState();
  }

  [[nodiscard]] const std::optional<ServerPresentationRouteIdentity>& route()
      const noexcept {
    return route_;
  }

  [[nodiscard]] const std::optional<ServerPresentationBaseline>& activeBaseline()
      const noexcept {
    return activeBaseline_;
  }

  [[nodiscard]] const std::optional<ServerPresentationWorldDescriptor>& world()
      const noexcept {
    return world_;
  }

  [[nodiscard]] const ServerPresentationEntityRecord* findEntity(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = entities_.find(handle.id);
    return found != entities_.end() && found->second.handle == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationNpcStateRecord* findNpcState(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = npcStates_.find(handle.id);
    return found != npcStates_.end() && found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationEquipmentSlotRecord* findEquipment(
      const ServerPresentationEntityHandle handle,
      const ServerPresentationEquipmentSlot slot) const noexcept {
    if(!isKnownServerPresentationEquipmentSlot(slot))
      return nullptr;
    const auto found = equipment_.find(handle.id);
    if(found == equipment_.end())
      return nullptr;
    const auto& state =
        found->second[serverPresentationEquipmentSlotIndex(slot)];
    return state.has_value() && state->entity == handle
               ? &*state
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationWeaponModeRecord* findWeaponMode(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = weaponModes_.find(handle.id);
    return found != weaponModes_.end() && found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationCombatActionRecord*
  findActiveCombatAction(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = activeCombatActions_.find(handle.id);
    return found != activeCombatActions_.end() &&
                   found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationDamageRecord* findDamage(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = damage_.find(handle.id);
    return found != damage_.end() && found->second.target == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationHitReactionRecord* findHitReaction(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = hitReactions_.find(handle.id);
    return found != hitReactions_.end() && found->second.target == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationLifeStateRecord* findLifeState(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = lifeStates_.find(handle.id);
    return found != lifeStates_.end() && found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationWorldObjectRecord* findWorldObject(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = worldObjects_.find(handle.id);
    return found != worldObjects_.end() && found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationInteractiveStateRecord* findInteractive(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = interactives_.find(handle.id);
    return found != interactives_.end() && found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationMoverStateRecord* findMover(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = movers_.find(handle.id);
    return found != movers_.end() && found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationDialogState& dialog() const noexcept {
    return dialog_;
  }

  [[nodiscard]] const ServerPresentationDialogBusyState& dialogBusy()
      const noexcept {
    return dialogBusy_;
  }

  [[nodiscard]] std::optional<ServerMovementCorrectionEvent>
  takePendingCorrection() noexcept {
    auto result = std::move(pendingCorrection_);
    pendingCorrection_.reset();
    return result;
  }

  [[nodiscard]] const ServerPresentationProjectileSnapshot* findProjectile(
      const std::uint64_t projectileId) const noexcept {
    const auto found = projectiles_.find(projectileId);
    return found != projectiles_.end() ? &found->second : nullptr;
  }

  [[nodiscard]] std::size_t projectileCount() const noexcept {
    return projectiles_.size();
  }

  [[nodiscard]] std::size_t entityCount() const noexcept {
    return entities_.size();
  }

 private:
  using EntityMap =
      std::unordered_map<std::uint64_t, ServerPresentationEntityRecord>;
  using NpcStateMap =
      std::unordered_map<std::uint64_t, ServerPresentationNpcStateRecord>;
  using EquipmentSlots = std::array<
      std::optional<ServerPresentationEquipmentSlotRecord>,
      ServerPresentationEquipmentSlotCount>;
  using EquipmentMap = std::unordered_map<std::uint64_t, EquipmentSlots>;
  using WeaponModeMap = std::unordered_map<
      std::uint64_t, ServerPresentationWeaponModeRecord>;
  using CombatActionMap = std::unordered_map<
      std::uint64_t, ServerPresentationCombatActionRecord>;
  using RevisionMap = std::unordered_map<std::uint64_t, std::uint64_t>;
  using DamageMap =
      std::unordered_map<std::uint64_t, ServerPresentationDamageRecord>;
  using HitReactionMap = std::unordered_map<
      std::uint64_t, ServerPresentationHitReactionRecord>;
  using LifeStateMap = std::unordered_map<
      std::uint64_t, ServerPresentationLifeStateRecord>;
  using WorldObjectMap = std::unordered_map<
      std::uint64_t, ServerPresentationWorldObjectRecord>;
  using InteractiveMap = std::unordered_map<
      std::uint64_t, ServerPresentationInteractiveStateRecord>;
  using MoverMap =
      std::unordered_map<std::uint64_t, ServerPresentationMoverStateRecord>;
  using ProjectileMap = std::unordered_map<
      std::uint64_t, ServerPresentationProjectileSnapshot>;

  [[nodiscard]] static constexpr bool isKnownKind(
      const ServerPresentationEntityKind kind) noexcept {
    return kind == ServerPresentationEntityKind::LocalPlayer ||
           kind == ServerPresentationEntityKind::RemotePlayer ||
           kind == ServerPresentationEntityKind::Npc;
  }

  [[nodiscard]] bool belongsToRoute(
      const ServerPresentationEntityHandle handle) const noexcept {
    return route_.has_value() && handle.valid() && handle.world == route_->world;
  }

  [[nodiscard]] ServerPresentationApplyStatus validateEntity(
      const ServerPresentationEntityHandle handle) const noexcept {
    if(!belongsToRoute(handle))
      return ServerPresentationApplyStatus::Invalid;
    const auto found = entities_.find(handle.id);
    if(found == entities_.end())
      return ServerPresentationApplyStatus::MissingEntity;
    return found->second.handle == handle
               ? ServerPresentationApplyStatus::Applied
               : ServerPresentationApplyStatus::IdentityMismatch;
  }

  [[nodiscard]] ServerPresentationApplyStatus validateOptionalEntity(
      const ServerPresentationEntityHandle handle) const noexcept {
    return handle.empty() ? ServerPresentationApplyStatus::Applied
                          : validateEntity(handle);
  }

  void eraseEntityPresentation(const std::uint64_t entityId) noexcept {
    npcStates_.erase(entityId);
    equipment_.erase(entityId);
    weaponModes_.erase(entityId);
    activeCombatActions_.erase(entityId);
    combatActionRevisions_.erase(entityId);
    damage_.erase(entityId);
    hitReactions_.erase(entityId);
    lifeStates_.erase(entityId);
    std::erase_if(activeCombatActions_, [entityId](const auto& entry) {
      return entry.second.target.id == entityId;
    });
    std::erase_if(damage_, [entityId](const auto& entry) {
      return entry.second.source.id == entityId ||
             entry.second.target.id == entityId;
    });
    std::erase_if(hitReactions_, [entityId](const auto& entry) {
      return entry.second.source.id == entityId ||
             entry.second.target.id == entityId;
    });
  }

  [[nodiscard]] ServerPresentationApplyStatus classifyRoute(
      const ServerPresentationRouteIdentity& candidate) const noexcept {
    if(!candidate.valid())
      return ServerPresentationApplyStatus::Invalid;
    if(!route_.has_value())
      return ServerPresentationApplyStatus::RouteMismatch;
    if(candidate.connectionId == route_->connectionId &&
       (candidate.routeEpoch < route_->routeEpoch ||
        (candidate.world.id == route_->world.id &&
         candidate.world.generation < route_->world.generation))) {
      return ServerPresentationApplyStatus::StaleRoute;
    }
    return ServerPresentationApplyStatus::RouteMismatch;
  }

  [[nodiscard]] bool buildBootstrapEntities(
      const ServerPresentationBootstrap& bootstrap,
      EntityMap& out,
      std::optional<ServerPresentationEntityHandle>& localPlayer) const {
    for(const auto& entity : bootstrap.entities) {
      if(!entity.valid() || !isKnownKind(entity.kind) ||
         entity.handle.world != bootstrap.route.world ||
         !out.emplace(entity.handle.id, entity).second) {
        return false;
      }
      if(entity.kind == ServerPresentationEntityKind::LocalPlayer) {
        if(localPlayer.has_value())
          return false;
        localPlayer = entity.handle;
      }
    }
    return localPlayer.has_value();
  }

  [[nodiscard]] static bool buildBootstrapNpcStates(
      const ServerPresentationBootstrap& bootstrap,
      const EntityMap& entities,
      NpcStateMap& out) {
    for(const auto& state : bootstrap.npcStates) {
      const auto entity = entities.find(state.entity.id);
      const bool targetMatchesRoute =
          state.target.id == 0U || state.target.world == bootstrap.route.world;
      if(!state.valid() || !targetMatchesRoute || entity == entities.end() ||
         entity->second.handle != state.entity ||
         entity->second.kind != ServerPresentationEntityKind::Npc ||
         !out.emplace(state.entity.id, state).second) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static bool bootstrapEntityMatches(
      const EntityMap& entities,
      const ServerPresentationEntityHandle handle) noexcept {
    const auto found = entities.find(handle.id);
    return found != entities.end() && found->second.handle == handle;
  }

  [[nodiscard]] static bool bootstrapOptionalEntityMatches(
      const EntityMap& entities,
      const ServerPresentationEntityHandle handle) noexcept {
    return handle.empty() || bootstrapEntityMatches(entities, handle);
  }

  [[nodiscard]] static bool buildBootstrapCombatPresentation(
      const ServerPresentationBootstrap& bootstrap,
      const EntityMap& entities,
      const std::optional<ServerPresentationEntityHandle>& localPlayer,
      EquipmentMap& equipment,
      WeaponModeMap& weaponModes,
      CombatActionMap& combatActions,
      RevisionMap& combatActionRevisions,
      LifeStateMap& lifeStates) {
    for(const auto& state : bootstrap.combatEquipment) {
      if(!state.valid() || !bootstrapEntityMatches(entities, state.entity))
        return false;
      auto& slots = equipment[state.entity.id];
      auto& slot = slots[serverPresentationEquipmentSlotIndex(state.slot)];
      if(slot.has_value())
        return false;
      slot = state;
    }
    for(const auto& state : bootstrap.weaponModes) {
      if(!state.valid() || !bootstrapEntityMatches(entities, state.entity) ||
         !weaponModes.emplace(state.entity.id, state).second) {
        return false;
      }
    }
    for(const auto& state : bootstrap.lifeStates) {
      if(!state.valid() || !bootstrapEntityMatches(entities, state.entity) ||
         !lifeStates.emplace(state.entity.id, state).second) {
        return false;
      }
    }
    for(const auto& action : bootstrap.combatActions) {
      const auto life = lifeStates.find(action.entity.id);
      if(!action.valid() ||
         !bootstrapEntityMatches(entities, action.entity) ||
         !bootstrapOptionalEntityMatches(entities, action.target) ||
         (life != lifeStates.end() &&
          life->second.lifeState != ServerPresentationNpcLifeState::Alive) ||
         (((action.flags &
            ServerPresentationCombatActionPredictedLocally) != 0U) &&
          (!localPlayer.has_value() || action.entity != *localPlayer)) ||
         !combatActions.emplace(action.entity.id, action).second ||
         !combatActionRevisions
              .emplace(action.entity.id, action.actionRevision)
              .second) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static bool buildBootstrapWorldObjectCatalog(
      const ServerPresentationBootstrap& bootstrap,
      WorldObjectMap& worldObjects) {
    std::unordered_map<std::uint64_t, std::uint64_t> entityByWorldObject;
    entityByWorldObject.reserve(bootstrap.worldObjects.size());
    for(const auto& object : bootstrap.worldObjects) {
      if(!object.valid() || object.entity.world != bootstrap.route.world ||
         !worldObjects.emplace(object.entity.id, object).second ||
         !entityByWorldObject.emplace(
             object.worldObjectId, object.entity.id).second) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static bool buildBootstrapWorldObjectStates(
      const ServerPresentationBootstrap& bootstrap,
      const WorldObjectMap& worldObjects,
      InteractiveMap& interactives,
      MoverMap& movers) {
    for(const auto& state : bootstrap.interactives) {
      const bool userMatchesRoute =
          state.user.id == 0U || state.user.world == bootstrap.route.world;
      const auto object = worldObjects.find(state.entity.id);
      if(!state.valid() || !userMatchesRoute ||
         state.entity.world != bootstrap.route.world ||
         object == worldObjects.end() || object->second.entity != state.entity ||
         (object->second.kind !=
              ServerPresentationWorldObjectKind::Interactive &&
          object->second.kind != ServerPresentationWorldObjectKind::Container) ||
         !interactives.emplace(state.entity.id, state).second) {
        return false;
      }
    }
    for(const auto& state : bootstrap.movers) {
      const auto object = worldObjects.find(state.entity.id);
      if(!state.valid() || state.entity.world != bootstrap.route.world ||
         object == worldObjects.end() || object->second.entity != state.entity ||
         object->second.kind != ServerPresentationWorldObjectKind::Mover ||
         !movers.emplace(state.entity.id, state).second) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] ServerPresentationApplyStatus validateHeader(
      const ServerPresentationEventHeader& header) const noexcept {
    if(!header.valid())
      return ServerPresentationApplyStatus::Invalid;
    if(!route_.has_value() || header.route != *route_)
      return classifyRoute(header.route);
    if(!activeBaseline_.has_value())
      return ServerPresentationApplyStatus::BaselineInactive;
    if(header.baseline != *activeBaseline_)
      return ServerPresentationApplyStatus::BaselineMismatch;
    return ServerPresentationApplyStatus::Applied;
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerWorldDescriptorEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.descriptor.valid())
      return {ServerPresentationApplyStatus::Invalid};
    if(world_.has_value() &&
       event.descriptor.descriptorRevision <= world_->descriptorRevision) {
      return {event.descriptor.descriptorRevision == world_->descriptorRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    world_ = event.descriptor;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::WorldDescriptorUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerEntitySpawnEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.entity.valid() || !isKnownKind(event.entity.kind) ||
       !belongsToRoute(event.entity.handle)) {
      return {ServerPresentationApplyStatus::Invalid};
    }

    auto found = entities_.find(event.entity.handle.id);
    if(found == entities_.end()) {
      if(entities_.size() >= config_.maxEntities)
        return {ServerPresentationApplyStatus::CapacityExceeded};
      if(event.entity.kind == ServerPresentationEntityKind::LocalPlayer &&
         localPlayer_.has_value()) {
        return {ServerPresentationApplyStatus::IdentityMismatch};
      }
      entities_.emplace(event.entity.handle.id, event.entity);
      if(event.entity.kind == ServerPresentationEntityKind::LocalPlayer)
        localPlayer_ = event.entity.handle;
      return {ServerPresentationApplyStatus::Applied,
              ServerPresentationMutation::EntitySpawned};
    }

    const auto& current = found->second;
    if(event.entity.handle.generation < current.handle.generation)
      return {ServerPresentationApplyStatus::Stale};
    if(event.entity.handle == current.handle &&
       event.entity.kind != current.kind) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    if((event.entity.kind == ServerPresentationEntityKind::LocalPlayer) !=
       (current.kind == ServerPresentationEntityKind::LocalPlayer)) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    if(event.entity.handle == current.handle &&
       event.entity.entityRevision <= current.entityRevision) {
      return {event.entity.entityRevision == current.entityRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }

    if(event.entity.kind == ServerPresentationEntityKind::LocalPlayer &&
       localPlayer_.has_value() && *localPlayer_ != current.handle) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }

    ServerPresentationApplyResult result{
        ServerPresentationApplyStatus::Applied,
        ServerPresentationMutation::EntityRebindRequired,
        current};
    const auto oldHandle = current.handle;
    const auto oldKind = current.kind;
    eraseEntityPresentation(oldHandle.id);
    found->second = event.entity;
    if(oldKind == ServerPresentationEntityKind::LocalPlayer)
      localPlayer_.reset();
    if(event.entity.kind == ServerPresentationEntityKind::LocalPlayer)
      localPlayer_ = event.entity.handle;
    return result;
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerEntityDespawnEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    const bool knownReason =
        event.reason == ServerEntityDespawnReason::LeftInterest ||
        event.reason == ServerEntityDespawnReason::Destroyed ||
        event.reason == ServerEntityDespawnReason::WorldTransition ||
        event.reason == ServerEntityDespawnReason::ReplacedGeneration;
    if(!event.entity.valid() || !belongsToRoute(event.entity) ||
       !knownReason || event.entityRevision == 0U) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    auto found = entities_.find(event.entity.id);
    if(found == entities_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(found->second.handle != event.entity) {
      return {event.entity.generation < found->second.handle.generation
                  ? ServerPresentationApplyStatus::Stale
                  : ServerPresentationApplyStatus::IdentityMismatch};
    }
    if(event.entityRevision < found->second.entityRevision)
      return {ServerPresentationApplyStatus::Stale};

    ServerPresentationApplyResult result{
        ServerPresentationApplyStatus::Applied,
        ServerPresentationMutation::EntityDespawned,
        found->second};
    if(localPlayer_ == event.entity) {
      localPlayer_.reset();
      pendingCorrection_.reset();
    }
    eraseEntityPresentation(event.entity.id);
    entities_.erase(found);
    return result;
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerEntityTransformEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.entity.valid() || !belongsToRoute(event.entity) ||
       !event.transform.valid() || event.entityRevision == 0U) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    auto found = entities_.find(event.entity.id);
    if(found == entities_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(found->second.handle != event.entity)
      return {ServerPresentationApplyStatus::IdentityMismatch};
    if(event.entityRevision <= found->second.entityRevision) {
      return {event.entityRevision == found->second.entityRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    found->second.transform = event.transform;
    found->second.entityRevision = event.entityRevision;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::EntityTransformed};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerMovementCorrectionEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    const bool knownReason =
        event.reason == ServerMovementCorrectionReason::Reconciliation ||
        event.reason == ServerMovementCorrectionReason::Collision ||
        event.reason == ServerMovementCorrectionReason::Teleport ||
        event.reason == ServerMovementCorrectionReason::InvalidInput ||
        event.reason == ServerMovementCorrectionReason::Resync;
    if(!event.entity.valid() || !belongsToRoute(event.entity) ||
       !event.transform.valid() || !knownReason ||
       event.movementRevision == 0U) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    if(!localPlayer_.has_value())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(event.entity != *localPlayer_)
      return {ServerPresentationApplyStatus::IdentityMismatch};
    if(pendingCorrection_.has_value() &&
       event.movementRevision <= pendingCorrection_->movementRevision) {
      return {event.movementRevision == pendingCorrection_->movementRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    pendingCorrection_ = event;
    const bool hardSnap = event.transform.teleport ||
                          event.reason == ServerMovementCorrectionReason::Teleport ||
                          event.reason == ServerMovementCorrectionReason::Resync;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::MovementCorrectionQueued,
            std::nullopt,
            hardSnap};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerNpcStateEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.state.valid() || !belongsToRoute(event.state.entity) ||
       (event.state.target.id != 0U &&
        !belongsToRoute(event.state.target))) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    const auto entity = entities_.find(event.state.entity.id);
    if(entity == entities_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(entity->second.handle != event.state.entity ||
       entity->second.kind != ServerPresentationEntityKind::Npc) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    auto found = npcStates_.find(event.state.entity.id);
    if(found != npcStates_.end() &&
       event.state.stateRevision <= found->second.stateRevision) {
      return {event.state.stateRevision == found->second.stateRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    npcStates_.insert_or_assign(event.state.entity.id, event.state);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::NpcStateUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerDialogStartEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    const auto* player = findEntity(event.player);
    const auto* npc = findEntity(event.npc);
    if(event.sessionId == 0U || event.topicId == 0U ||
       event.dialogRevision == 0U || player == nullptr || npc == nullptr ||
       player->kind != ServerPresentationEntityKind::LocalPlayer ||
       npc->kind != ServerPresentationEntityKind::Npc ||
       event.player == event.npc) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    if(dialog_.active() && event.dialogRevision <= dialog_.dialogRevision) {
      return {event.dialogRevision == dialog_.dialogRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    dialogBusy_ = {};
    dialog_ = {
        .sessionId = event.sessionId,
        .player = event.player,
        .npc = event.npc,
        .topicId = event.topicId,
        .dialogRevision = event.dialogRevision,
    };
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::DialogStarted};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerDialogUpdateEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!dialog_.active() || event.sessionId != dialog_.sessionId ||
       event.lineId == 0U || event.dialogRevision == 0U ||
       (event.flags & ~KnownServerDialogUpdateFlags) != 0U ||
       (event.speaker != dialog_.player && event.speaker != dialog_.npc) ||
       findEntity(event.speaker) == nullptr) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    if(event.dialogRevision <= dialog_.dialogRevision) {
      return {event.dialogRevision == dialog_.dialogRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    dialog_.speaker = event.speaker;
    dialog_.lineId = event.lineId;
    dialog_.choicesRevision = event.choicesRevision;
    dialog_.dialogRevision = event.dialogRevision;
    dialog_.flags = event.flags;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::DialogUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerDialogEndEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    const bool knownReason =
        event.reason == ServerDialogEndReason::Completed ||
        event.reason == ServerDialogEndReason::Cancelled ||
        event.reason == ServerDialogEndReason::ParticipantUnavailable ||
        event.reason == ServerDialogEndReason::RouteChanged;
    if(!knownReason)
      return {ServerPresentationApplyStatus::Invalid};
    if(!dialog_.active() || event.sessionId != dialog_.sessionId)
      return {ServerPresentationApplyStatus::IdentityMismatch};
    if(event.dialogRevision < dialog_.dialogRevision)
      return {ServerPresentationApplyStatus::Stale};
    dialog_ = {};
    dialogBusy_ = {};
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::DialogEnded};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerDialogBusyEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    const bool knownReason =
        event.reason == ServerDialogBusyReason::NpcInAnotherDialog ||
        event.reason == ServerDialogBusyReason::NpcUnavailable ||
        event.reason == ServerDialogBusyReason::DialogCooldown;
    const auto* npc = findEntity(event.npc);
    if(!knownReason || event.dialogRevision == 0U || npc == nullptr ||
       npc->kind != ServerPresentationEntityKind::Npc) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    if(dialogBusy_.active() &&
       event.dialogRevision <= dialogBusy_.dialogRevision) {
      return {event.dialogRevision == dialogBusy_.dialogRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    dialogBusy_ = {
        .npc = event.npc,
        .activeSessionId = event.activeSessionId,
        .reason = event.reason,
        .retryAfterMilliseconds = event.retryAfterMilliseconds,
        .dialogRevision = event.dialogRevision,
    };
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::DialogBusy};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerWorldItemSpawnEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.valid() || !belongsToRoute(event.entity) ||
       (event.flags & ~0x7U) != 0U) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    auto found = worldObjects_.find(event.entity.id);
    if(found == worldObjects_.end()) {
      if(worldObjects_.size() >= config_.maxWorldObjects)
        return {ServerPresentationApplyStatus::CapacityExceeded};
      worldObjects_.emplace(
          event.entity.id,
          ServerPresentationWorldObjectRecord{
              .entity = event.entity,
              .worldObjectId = event.worldObjectId,
              .kind = ServerPresentationWorldObjectKind::Item,
              .presentation = event.presentation,
              .transform = event.transform,
              .stateRevision = event.stateRevision,
              .flags = 0U,
          });
      return {ServerPresentationApplyStatus::Applied,
              ServerPresentationMutation::WorldItemSpawned};
    }
    if(found->second.entity != event.entity) {
      return {event.entity.generation < found->second.entity.generation
                  ? ServerPresentationApplyStatus::Stale
                  : ServerPresentationApplyStatus::IdentityMismatch};
    }
    if(found->second.kind != ServerPresentationWorldObjectKind::Item ||
       found->second.worldObjectId != event.worldObjectId) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    if(event.stateRevision <= found->second.stateRevision) {
      return {event.stateRevision == found->second.stateRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    found->second.presentation = event.presentation;
    found->second.transform = event.transform;
    found->second.stateRevision = event.stateRevision;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::WorldItemUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerWorldItemDespawnEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.valid() || !belongsToRoute(event.entity))
      return {ServerPresentationApplyStatus::Invalid};
    const auto found = worldObjects_.find(event.entity.id);
    if(found == worldObjects_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(found->second.entity != event.entity) {
      return {event.entity.generation < found->second.entity.generation
                  ? ServerPresentationApplyStatus::Stale
                  : ServerPresentationApplyStatus::IdentityMismatch};
    }
    if(found->second.kind != ServerPresentationWorldObjectKind::Item)
      return {ServerPresentationApplyStatus::IdentityMismatch};
    if(event.stateRevision < found->second.stateRevision)
      return {ServerPresentationApplyStatus::Stale};
    worldObjects_.erase(found);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::WorldItemDespawned};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerWorldItemStateChangedEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.valid() || !belongsToRoute(event.entity) ||
       (event.flags & ~0x7U) != 0U) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    const auto found = worldObjects_.find(event.entity.id);
    if(found == worldObjects_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(found->second.entity != event.entity ||
       found->second.kind != ServerPresentationWorldObjectKind::Item) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    if(event.stateRevision <= found->second.stateRevision) {
      return {event.stateRevision == found->second.stateRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    found->second.stateRevision = event.stateRevision;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::WorldItemUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerInventorySnapshotEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(validateEntity(event.owner) != ServerPresentationApplyStatus::Applied ||
       event.snapshot.revision == 0U) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::InventorySnapshotInstalled};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerInventoryDeltaEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(validateEntity(event.owner) != ServerPresentationApplyStatus::Applied ||
       !event.mutation.valid()) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::InventoryDeltaApplied};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerEquipmentSnapshotEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(validateEntity(event.owner) != ServerPresentationApplyStatus::Applied ||
       event.snapshot.revision == 0U) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::EquipmentSnapshotInstalled};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerEquipmentBindingChangedEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(validateEntity(event.owner) != ServerPresentationApplyStatus::Applied ||
       event.change.revision == 0U ||
       !ServerEquipmentReadModel::knownSlot(event.change.slot)) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::EquipmentBindingUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerEquipmentSlotChangedEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.state.valid())
      return {ServerPresentationApplyStatus::Invalid};
    const auto entityStatus = validateEntity(event.state.entity);
    if(entityStatus != ServerPresentationApplyStatus::Applied)
      return {entityStatus};

    auto& slots = equipment_[event.state.entity.id];
    auto& current =
        slots[serverPresentationEquipmentSlotIndex(event.state.slot)];
    if(current.has_value() &&
       event.state.equipmentRevision <= current->equipmentRevision) {
      return {event.state.equipmentRevision == current->equipmentRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    current = event.state;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::EquipmentSlotUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerWeaponModeChangedEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.state.valid())
      return {ServerPresentationApplyStatus::Invalid};
    const auto entityStatus = validateEntity(event.state.entity);
    if(entityStatus != ServerPresentationApplyStatus::Applied)
      return {entityStatus};

    const auto found = weaponModes_.find(event.state.entity.id);
    if(found != weaponModes_.end() &&
       event.state.weaponRevision <= found->second.weaponRevision) {
      return {event.state.weaponRevision == found->second.weaponRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    weaponModes_.insert_or_assign(event.state.entity.id, event.state);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::WeaponModeUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerCombatActionStartedEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.action.valid())
      return {ServerPresentationApplyStatus::Invalid};
    const auto entityStatus = validateEntity(event.action.entity);
    if(entityStatus != ServerPresentationApplyStatus::Applied)
      return {entityStatus};
    const auto targetStatus = validateOptionalEntity(event.action.target);
    if(targetStatus != ServerPresentationApplyStatus::Applied)
      return {targetStatus};
    if((event.action.flags &
        ServerPresentationCombatActionPredictedLocally) != 0U &&
       (!localPlayer_.has_value() || event.action.entity != *localPlayer_)) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    const auto life = lifeStates_.find(event.action.entity.id);
    if(life != lifeStates_.end() &&
       life->second.lifeState != ServerPresentationNpcLifeState::Alive) {
      return {ServerPresentationApplyStatus::Invalid};
    }

    const auto revision = combatActionRevisions_.find(event.action.entity.id);
    if(revision != combatActionRevisions_.end() &&
       event.action.actionRevision <= revision->second) {
      return {event.action.actionRevision == revision->second
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    activeCombatActions_.insert_or_assign(event.action.entity.id,
                                          event.action);
    combatActionRevisions_.insert_or_assign(event.action.entity.id,
                                            event.action.actionRevision);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::CombatActionStarted};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerCombatActionResolvedEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.resolution.valid())
      return {ServerPresentationApplyStatus::Invalid};
    const auto entityStatus = validateEntity(event.resolution.entity);
    if(entityStatus != ServerPresentationApplyStatus::Applied)
      return {entityStatus};

    const auto revision =
        combatActionRevisions_.find(event.resolution.entity.id);
    if(revision != combatActionRevisions_.end() &&
       event.resolution.actionRevision <= revision->second) {
      return {event.resolution.actionRevision == revision->second
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    const auto active =
        activeCombatActions_.find(event.resolution.entity.id);
    if(active != activeCombatActions_.end() &&
       active->second.actionId != event.resolution.actionId) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    if(active != activeCombatActions_.end() &&
       active->second.clientActionSequence != 0U &&
       event.resolution.clientActionSequence !=
           active->second.clientActionSequence) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    activeCombatActions_.erase(event.resolution.entity.id);
    combatActionRevisions_.insert_or_assign(
        event.resolution.entity.id, event.resolution.actionRevision);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::CombatActionResolved};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerDamageAppliedEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.damage.valid())
      return {ServerPresentationApplyStatus::Invalid};
    const auto sourceStatus = validateOptionalEntity(event.damage.source);
    if(sourceStatus != ServerPresentationApplyStatus::Applied)
      return {sourceStatus};
    const auto targetStatus = validateEntity(event.damage.target);
    if(targetStatus != ServerPresentationApplyStatus::Applied)
      return {targetStatus};

    const auto found = damage_.find(event.damage.target.id);
    if(found != damage_.end() &&
       event.damage.damageRevision <= found->second.damageRevision) {
      return {event.damage.damageRevision == found->second.damageRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    damage_.insert_or_assign(event.damage.target.id, event.damage);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::DamageApplied};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerHitReactionEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.reaction.valid())
      return {ServerPresentationApplyStatus::Invalid};
    const auto sourceStatus = validateOptionalEntity(event.reaction.source);
    if(sourceStatus != ServerPresentationApplyStatus::Applied)
      return {sourceStatus};
    const auto targetStatus = validateEntity(event.reaction.target);
    if(targetStatus != ServerPresentationApplyStatus::Applied)
      return {targetStatus};

    const auto found = hitReactions_.find(event.reaction.target.id);
    if(found != hitReactions_.end() &&
       event.reaction.reactionRevision <= found->second.reactionRevision) {
      return {event.reaction.reactionRevision == found->second.reactionRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    hitReactions_.insert_or_assign(event.reaction.target.id, event.reaction);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::HitReactionApplied};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerCharacterDeathStateChangedEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.state.valid())
      return {ServerPresentationApplyStatus::Invalid};
    const auto entityStatus = validateEntity(event.state.entity);
    if(entityStatus != ServerPresentationApplyStatus::Applied)
      return {entityStatus};

    const auto found = lifeStates_.find(event.state.entity.id);
    if(found != lifeStates_.end() &&
       event.state.lifeRevision <= found->second.lifeRevision) {
      return {event.state.lifeRevision == found->second.lifeRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    lifeStates_.insert_or_assign(event.state.entity.id, event.state);
    if(event.state.lifeState != ServerPresentationNpcLifeState::Alive)
      activeCombatActions_.erase(event.state.entity.id);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::CharacterLifeStateUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerInteractiveStateEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.state.valid() || !belongsToRoute(event.state.entity) ||
       (event.state.user.id != 0U && !belongsToRoute(event.state.user))) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    const auto object = worldObjects_.find(event.state.entity.id);
    if(object == worldObjects_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(object->second.entity != event.state.entity ||
       (object->second.kind !=
            ServerPresentationWorldObjectKind::Interactive &&
        object->second.kind != ServerPresentationWorldObjectKind::Container)) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    auto found = interactives_.find(event.state.entity.id);
    if(found != interactives_.end() &&
       event.state.stateRevision <= found->second.stateRevision) {
      return {event.state.stateRevision == found->second.stateRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    if(found == interactives_.end() &&
       interactives_.size() >= config_.maxWorldObjects) {
      return {ServerPresentationApplyStatus::CapacityExceeded};
    }
    interactives_.insert_or_assign(event.state.entity.id, event.state);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::InteractiveUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerMoverStateEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.state.valid() || !belongsToRoute(event.state.entity))
      return {ServerPresentationApplyStatus::Invalid};
    const auto object = worldObjects_.find(event.state.entity.id);
    if(object == worldObjects_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(object->second.entity != event.state.entity ||
       object->second.kind != ServerPresentationWorldObjectKind::Mover) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    auto found = movers_.find(event.state.entity.id);
    if(found != movers_.end() &&
       event.state.stateRevision <= found->second.stateRevision) {
      return {event.state.stateRevision == found->second.stateRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    if(found == movers_.end() && movers_.size() >= config_.maxWorldObjects)
      return {ServerPresentationApplyStatus::CapacityExceeded};
    movers_.insert_or_assign(event.state.entity.id, event.state);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::MoverUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyProjectileSnapshot(
      const ServerPresentationEventHeader& header,
      const ServerPresentationProjectileSnapshot& projectile,
      const ServerPresentationMutation insertedMutation) {
    const auto status = validateHeader(header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!projectile.valid() || !belongsToRoute(projectile.owner) ||
       (!projectile.target.empty() && !belongsToRoute(projectile.target))) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    const auto found = projectiles_.find(projectile.projectileId);
    if(found == projectiles_.end()) {
      if(projectiles_.size() >= config_.maxProjectiles)
        return {ServerPresentationApplyStatus::CapacityExceeded};
      projectiles_.emplace(projectile.projectileId, projectile);
      return {ServerPresentationApplyStatus::Applied, insertedMutation};
    }
    if(projectile.stateRevision <= found->second.stateRevision) {
      return {projectile.stateRevision == found->second.stateRevision
                  ? ServerPresentationApplyStatus::Duplicate
                  : ServerPresentationApplyStatus::Stale};
    }
    if(found->second.owner != projectile.owner ||
       found->second.target != projectile.target ||
       found->second.launcherArchetypeId != projectile.launcherArchetypeId ||
       found->second.projectileArchetypeId !=
           projectile.projectileArchetypeId ||
       found->second.actionId != projectile.actionId ||
       found->second.actionSequence != projectile.actionSequence ||
       found->second.contentRevision != projectile.contentRevision ||
       found->second.rulesetId != projectile.rulesetId ||
       found->second.actionProfileId != projectile.actionProfileId ||
       found->second.spawnTick != projectile.spawnTick) {
      return {ServerPresentationApplyStatus::IdentityMismatch};
    }
    found->second = projectile;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::ProjectileUpdated};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerProjectileSpawnEvent& event) {
    return applyProjectileSnapshot(
        event.header, event.projectile,
        ServerPresentationMutation::ProjectileSpawned);
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerProjectileStateEvent& event) {
    return applyProjectileSnapshot(
        event.header, event.projectile,
        ServerPresentationMutation::ProjectileSpawned);
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerProjectileImpactEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.impact.valid())
      return {ServerPresentationApplyStatus::Invalid};
    const auto found = projectiles_.find(event.impact.projectileId);
    if(found == projectiles_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(event.impact.stateRevision < found->second.stateRevision)
      return {ServerPresentationApplyStatus::Stale};
    if(event.impact.actionId != found->second.actionId)
      return {ServerPresentationApplyStatus::IdentityMismatch};
    if(event.impact.kind == ServerProjectileImpactKind::Actor &&
       !belongsToRoute(event.impact.actor)) {
      return {ServerPresentationApplyStatus::Invalid};
    }
    found->second.positionXMicrometers = event.impact.positionXMicrometers;
    found->second.positionYMicrometers = event.impact.positionYMicrometers;
    found->second.positionZMicrometers = event.impact.positionZMicrometers;
    found->second.stateRevision = event.impact.stateRevision;
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::ProjectileImpacted};
  }

  [[nodiscard]] ServerPresentationApplyResult applyOne(
      const ServerProjectileDespawnEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(event.projectileId == 0U || event.despawnTick == 0U ||
       event.stateRevision == 0U)
      return {ServerPresentationApplyStatus::Invalid};
    const auto found = projectiles_.find(event.projectileId);
    if(found == projectiles_.end())
      return {ServerPresentationApplyStatus::MissingEntity};
    if(event.stateRevision < found->second.stateRevision)
      return {ServerPresentationApplyStatus::Stale};
    projectiles_.erase(found);
    return {ServerPresentationApplyStatus::Applied,
            ServerPresentationMutation::ProjectileDespawned};
  }

  void clearRouteState() noexcept {
    activeBaseline_.reset();
    world_.reset();
    entities_.clear();
    npcStates_.clear();
    equipment_.clear();
    weaponModes_.clear();
    activeCombatActions_.clear();
    combatActionRevisions_.clear();
    damage_.clear();
    hitReactions_.clear();
    lifeStates_.clear();
    worldObjects_.clear();
    interactives_.clear();
    movers_.clear();
    projectiles_.clear();
    localPlayer_.reset();
    pendingCorrection_.reset();
    dialog_ = {};
    dialogBusy_ = {};
  }

  ServerPresentationStateConfig config_{};
  std::optional<ServerPresentationRouteIdentity> route_;
  std::optional<ServerPresentationBaseline> activeBaseline_;
  std::optional<ServerPresentationWorldDescriptor> world_;
  EntityMap entities_;
  NpcStateMap npcStates_;
  EquipmentMap equipment_;
  WeaponModeMap weaponModes_;
  CombatActionMap activeCombatActions_;
  RevisionMap combatActionRevisions_;
  DamageMap damage_;
  HitReactionMap hitReactions_;
  LifeStateMap lifeStates_;
  WorldObjectMap worldObjects_;
  InteractiveMap interactives_;
  MoverMap movers_;
  ProjectileMap projectiles_;
  std::optional<ServerPresentationEntityHandle> localPlayer_;
  std::optional<ServerMovementCorrectionEvent> pendingCorrection_;
  ServerPresentationDialogState dialog_{};
  ServerPresentationDialogBusyState dialogBusy_{};
};

} // namespace Mmo::ClientPresentation
