#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
  DialogStarted,
  DialogUpdated,
  DialogEnded,
  DialogBusy,
  InteractiveUpdated,
  MoverUpdated,
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
};

class ServerPresentationState final {
 public:
  explicit ServerPresentationState(
      ServerPresentationStateConfig config = {})
      : config_(config) {
    config_.maxEntities = std::max<std::size_t>(1U, config_.maxEntities);
    config_.maxWorldObjects =
        std::max<std::size_t>(1U, config_.maxWorldObjects);
    entities_.reserve(config_.maxEntities);
    npcStates_.reserve(config_.maxEntities);
    interactives_.reserve(config_.maxWorldObjects);
    movers_.reserve(config_.maxWorldObjects);
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
    if(bootstrap.entities.size() > config_.maxEntities ||
       bootstrap.interactives.size() > config_.maxWorldObjects ||
       bootstrap.movers.size() > config_.maxWorldObjects) {
      result.status = ServerPresentationApplyStatus::CapacityExceeded;
      return result;
    }

    EntityMap nextEntities;
    NpcStateMap nextNpcStates;
    InteractiveMap nextInteractives;
    MoverMap nextMovers;
    nextEntities.reserve(bootstrap.entities.size());
    nextNpcStates.reserve(bootstrap.npcStates.size());
    nextInteractives.reserve(bootstrap.interactives.size());
    nextMovers.reserve(bootstrap.movers.size());

    std::optional<ServerPresentationEntityHandle> nextLocalPlayer;
    if(!buildBootstrapEntities(bootstrap, nextEntities, nextLocalPlayer) ||
       !buildBootstrapNpcStates(bootstrap, nextEntities, nextNpcStates) ||
       !buildBootstrapWorldObjects(bootstrap, nextInteractives, nextMovers)) {
      result.status = ServerPresentationApplyStatus::Invalid;
      return result;
    }

    result.releasedEntities.reserve(entities_.size());
    for(auto& [id, entity] : entities_) {
      static_cast<void>(id);
      result.releasedEntities.push_back(std::move(entity));
    }

    // Install world-object baselines before exposing the entity roster as live.
    interactives_ = std::move(nextInteractives);
    movers_ = std::move(nextMovers);
    entities_ = std::move(nextEntities);
    npcStates_ = std::move(nextNpcStates);
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

  [[nodiscard]] const ServerPresentationInteractiveStateRecord* findInteractive(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = interactives_.find(handle.id);
    return found != interactives_.end() && found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationInteractiveStateRecord*
  findInteractiveById(const std::uint64_t entityId) const noexcept {
    const auto found = interactives_.find(entityId);
    return found != interactives_.end() ? &found->second : nullptr;
  }

  [[nodiscard]] const ServerPresentationMoverStateRecord* findMover(
      const ServerPresentationEntityHandle handle) const noexcept {
    const auto found = movers_.find(handle.id);
    return found != movers_.end() && found->second.entity == handle
               ? &found->second
               : nullptr;
  }

  [[nodiscard]] const ServerPresentationMoverStateRecord* findMoverById(
      const std::uint64_t entityId) const noexcept {
    const auto found = movers_.find(entityId);
    return found != movers_.end() ? &found->second : nullptr;
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

  [[nodiscard]] std::size_t entityCount() const noexcept {
    return entities_.size();
  }

 private:
  using EntityMap =
      std::unordered_map<std::uint64_t, ServerPresentationEntityRecord>;
  using NpcStateMap =
      std::unordered_map<std::uint64_t, ServerPresentationNpcStateRecord>;
  using InteractiveMap = std::unordered_map<
      std::uint64_t, ServerPresentationInteractiveStateRecord>;
  using MoverMap =
      std::unordered_map<std::uint64_t, ServerPresentationMoverStateRecord>;

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

  [[nodiscard]] static bool buildBootstrapWorldObjects(
      const ServerPresentationBootstrap& bootstrap,
      InteractiveMap& interactives,
      MoverMap& movers) {
    for(const auto& state : bootstrap.interactives) {
      const bool userMatchesRoute =
          state.user.id == 0U || state.user.world == bootstrap.route.world;
      if(!state.valid() || !userMatchesRoute ||
         state.entity.world != bootstrap.route.world ||
         !interactives.emplace(state.entity.id, state).second) {
        return false;
      }
    }
    for(const auto& state : bootstrap.movers) {
      if(!state.valid() || state.entity.world != bootstrap.route.world ||
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
    npcStates_.erase(oldHandle.id);
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
    npcStates_.erase(event.entity.id);
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
      const ServerInteractiveStateEvent& event) {
    const auto status = validateHeader(event.header);
    if(status != ServerPresentationApplyStatus::Applied)
      return {status};
    if(!event.state.valid() || !belongsToRoute(event.state.entity) ||
       (event.state.user.id != 0U && !belongsToRoute(event.state.user))) {
      return {ServerPresentationApplyStatus::Invalid};
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

  void clearRouteState() noexcept {
    activeBaseline_.reset();
    world_.reset();
    entities_.clear();
    npcStates_.clear();
    interactives_.clear();
    movers_.clear();
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
  InteractiveMap interactives_;
  MoverMap movers_;
  std::optional<ServerPresentationEntityHandle> localPlayer_;
  std::optional<ServerMovementCorrectionEvent> pendingCorrection_;
  ServerPresentationDialogState dialog_{};
  ServerPresentationDialogBusyState dialogBusy_{};
};

} // namespace Mmo::ClientPresentation
