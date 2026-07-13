#pragma once

#ifndef OPENGOTHIC_MMO_SANDBOX_FACADE
#define OPENGOTHIC_MMO_SANDBOX_FACADE 0
#endif

#if OPENGOTHIC_MMO_SANDBOX_FACADE

#ifndef OPENGOTHIC_MMO_CLIENT_RUNTIME_FACADE_HEADER
#define OPENGOTHIC_MMO_CLIENT_RUNTIME_FACADE_HEADER <gothic/mmo/client_runtime_facade.h>
#endif
#include OPENGOTHIC_MMO_CLIENT_RUNTIME_FACADE_HEADER
#include "mmoserverpresentationmailbox.h"

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace Mmo::ClientPresentation {

struct ClientRuntimePresentationMailboxSnapshot final {
  std::optional<ClientSandbox::ClientRuntimeProtocolV2State> protocolState;
  std::vector<ClientSandbox::ClientRuntimeWorldDescriptor> worldDescriptors;
  std::vector<ClientSandbox::ClientRuntimeMovementCorrection> movementCorrections;
  std::vector<ClientSandbox::ClientRuntimeEntitySpawn> entitySpawns;
  std::vector<ClientSandbox::ClientRuntimeEntityDespawn> entityDespawns;
  std::vector<ClientSandbox::ClientRuntimeEntityTransform> entityTransforms;
  std::vector<ClientSandbox::ClientRuntimeNpcState> npcStates;
  std::vector<ClientSandbox::ClientRuntimeDialogEvent> dialogEvents;
  std::vector<ClientSandbox::ClientRuntimeInteractiveState> interactiveStates;
  std::vector<ClientSandbox::ClientRuntimeMoverState> moverStates;
  std::vector<ClientSandbox::ClientRuntimeCompletedBootstrap> bootstraps;
};

namespace Detail {

using namespace ClientSandbox;

[[nodiscard]] constexpr bool sameWorld(
    const ClientRuntimeWorldInstanceHandle lhs,
    const ClientRuntimeWorldInstanceHandle rhs) noexcept {
  return lhs.id == rhs.id && lhs.generation == rhs.generation;
}

[[nodiscard]] constexpr ServerWorldInstanceHandle mapWorld(
    const ClientRuntimeWorldInstanceHandle value) noexcept {
  return {.id = value.id, .generation = value.generation};
}

[[nodiscard]] constexpr ServerPresentationRouteIdentity mapRoute(
    const std::uint64_t connectionId,
    const std::uint64_t routeEpoch,
    const ClientRuntimeWorldInstanceHandle world) noexcept {
  return {
      .connectionId = connectionId,
      .routeEpoch = routeEpoch,
      .world = mapWorld(world),
  };
}

[[nodiscard]] constexpr ServerPresentationBaseline mapBaseline(
    const ClientRuntimeReplicationBaseline value) noexcept {
  return {
      .serverTick = value.serverTick,
      .aggregateRevision = value.aggregateRevision,
  };
}

[[nodiscard]] constexpr ServerPresentationEventHeader mapHeader(
    const ClientRuntimeReplicationMetadata& value) noexcept {
  return {
      .route = mapRoute(value.connectionId, value.routeEpoch, value.world),
      .streamSequence = value.streamSequence,
      .serverTick = value.serverTick,
      .aggregateRevision = value.aggregateRevision,
      .baseline = mapBaseline(value.baseline),
  };
}

[[nodiscard]] constexpr ServerPresentationEntityHandle mapHandle(
    const ClientRuntimeEntityHandle value) noexcept {
  return {
      .world = mapWorld(value.world),
      .id = value.id,
      .generation = value.generation,
  };
}

[[nodiscard]] constexpr ServerPresentationEntityHandle mapHandle(
    const std::optional<ClientRuntimeEntityHandle>& value) noexcept {
  return value.has_value() ? mapHandle(*value)
                           : ServerPresentationEntityHandle{};
}

[[nodiscard]] constexpr ServerPresentationMapping mapPresentation(
    const ClientRuntimeEntityPresentation value) noexcept {
  return {
      .archetypeId = value.archetypeId,
      .presentationId = value.presentationId,
      .revision = value.revision,
  };
}

[[nodiscard]] constexpr bool validTransformFlags(
    const std::uint16_t flags) noexcept {
  constexpr std::uint16_t known = 0x1U | 0x2U | 0x4U;
  return (flags & static_cast<std::uint16_t>(~known)) == 0U;
}

[[nodiscard]] constexpr std::optional<ServerPresentationTransform> mapTransform(
    const ClientRuntimeQuantizedTransform value) noexcept {
  if(!validTransformFlags(value.flags))
    return std::nullopt;
  return ServerPresentationTransform{
      .posX = static_cast<double>(value.positionX),
      .posY = static_cast<double>(value.positionY),
      .posZ = static_cast<double>(value.positionZ),
      .yaw = static_cast<double>(value.yaw),
      .pitch = static_cast<double>(value.pitch),
      .roll = static_cast<double>(value.roll),
      .grounded = (value.flags & 0x1U) != 0U,
      .teleport = (value.flags & 0x2U) != 0U,
      .dormant = (value.flags & 0x4U) != 0U,
  };
}

template<class Destination, class Source>
[[nodiscard]] constexpr std::optional<Destination> mapEnum(
    const Source value) noexcept {
  using D = Destination;
  using S = Source;
  if constexpr(std::is_same_v<S, ClientRuntimeEntityKind>) {
    switch(value) {
      case S::LocalPlayer: return D::LocalPlayer;
      case S::RemotePlayer: return D::RemotePlayer;
      case S::Npc: return D::Npc;
    }
  } else if constexpr(std::is_same_v<S, ClientRuntimeMovementCorrectionReason>) {
    switch(value) {
      case S::Reconciliation: return D::Reconciliation;
      case S::Collision: return D::Collision;
      case S::Teleport: return D::Teleport;
      case S::InvalidInput: return D::InvalidInput;
      case S::Resync: return D::Resync;
    }
  } else if constexpr(std::is_same_v<S, ClientRuntimeEntityDespawnReason>) {
    switch(value) {
      case S::LeftInterest: return D::LeftInterest;
      case S::Destroyed: return D::Destroyed;
      case S::WorldTransition: return D::WorldTransition;
      case S::ReplacedGeneration: return D::ReplacedGeneration;
    }
  } else if constexpr(std::is_same_v<S, ClientRuntimeNpcLifeState>) {
    switch(value) {
      case S::Alive: return D::Alive;
      case S::Unconscious: return D::Unconscious;
      case S::Dead: return D::Dead;
    }
  } else if constexpr(std::is_same_v<S, ClientRuntimeNpcActivityState>) {
    switch(value) {
      case S::Idle: return D::Idle;
      case S::Routine: return D::Routine;
      case S::Traversal: return D::Traversal;
      case S::Interaction: return D::Interaction;
      case S::Dialog: return D::Dialog;
      case S::Combat: return D::Combat;
    }
  } else if constexpr(std::is_same_v<S, ClientRuntimeDialogEndReason>) {
    switch(value) {
      case S::Completed: return D::Completed;
      case S::Cancelled: return D::Cancelled;
      case S::ParticipantUnavailable: return D::ParticipantUnavailable;
      case S::RouteChanged: return D::RouteChanged;
    }
  } else if constexpr(std::is_same_v<S, ClientRuntimeDialogBusyReason>) {
    switch(value) {
      case S::NpcInAnotherDialog: return D::NpcInAnotherDialog;
      case S::NpcUnavailable: return D::NpcUnavailable;
      case S::DialogCooldown: return D::DialogCooldown;
    }
  } else if constexpr(std::is_same_v<S, ClientRuntimeMoverPhase>) {
    switch(value) {
      case S::AtStart: return D::AtStart;
      case S::Opening: return D::Opening;
      case S::AtEnd: return D::AtEnd;
      case S::Closing: return D::Closing;
      case S::Paused: return D::Paused;
    }
  }
  return std::nullopt;
}

[[nodiscard]] inline std::optional<ServerPresentationEntityRecord> mapEntity(
    const ClientRuntimeBootstrapEntity& value) noexcept {
  const auto kind = mapEnum<ServerPresentationEntityKind>(value.kind);
  const auto transform = mapTransform(value.transform);
  if(!kind || !transform)
    return std::nullopt;
  ServerPresentationEntityRecord out{
      .handle = mapHandle(value.entity),
      .kind = *kind,
      .presentation = mapPresentation(value.presentation),
      .transform = *transform,
      .entityRevision = value.entityRevision,
  };
  return out.valid() ? std::optional{out} : std::nullopt;
}

[[nodiscard]] inline std::optional<ServerPresentationEntityRecord> mapEntity(
    const ClientRuntimeEntitySpawn& value) noexcept {
  const auto kind = mapEnum<ServerPresentationEntityKind>(value.kind);
  const auto transform = mapTransform(value.transform);
  if(!kind || !transform)
    return std::nullopt;
  ServerPresentationEntityRecord out{
      .handle = mapHandle(value.entity),
      .kind = *kind,
      .presentation = mapPresentation(value.presentation),
      .transform = *transform,
      .entityRevision = value.entityRevision,
  };
  return out.valid() ? std::optional{out} : std::nullopt;
}

[[nodiscard]] inline std::optional<ServerPresentationNpcStateRecord> mapNpc(
    const ClientRuntimeBootstrapNpcState& value) noexcept {
  const auto life = mapEnum<ServerPresentationNpcLifeState>(value.lifeState);
  const auto activity =
      mapEnum<ServerPresentationNpcActivityState>(value.activityState);
  if(!life || !activity)
    return std::nullopt;
  ServerPresentationNpcStateRecord out{
      .entity = mapHandle(value.entity),
      .lifeState = *life,
      .activityState = *activity,
      .weaponMode = value.weaponMode,
      .flags = value.flags,
      .target = mapHandle(value.target),
      .health = value.health,
      .maximumHealth = value.maximumHealth,
      .mana = value.mana,
      .maximumMana = value.maximumMana,
      .stateRevision = value.stateRevision,
  };
  return out.valid() ? std::optional{out} : std::nullopt;
}

[[nodiscard]] inline std::optional<ServerPresentationNpcStateRecord> mapNpc(
    const ClientRuntimeNpcState& value) noexcept {
  const ClientRuntimeBootstrapNpcState source{
      .entity = value.entity,
      .lifeState = value.lifeState,
      .activityState = value.activityState,
      .weaponMode = value.weaponMode,
      .flags = value.flags,
      .target = value.target,
      .health = value.health,
      .maximumHealth = value.maximumHealth,
      .mana = value.mana,
      .maximumMana = value.maximumMana,
      .stateRevision = value.stateRevision,
  };
  return mapNpc(source);
}

[[nodiscard]] inline std::optional<ServerPresentationInteractiveStateRecord>
mapInteractive(const ClientRuntimeBootstrapInteractiveState& value) noexcept {
  ServerPresentationInteractiveStateRecord out{
      .entity = mapHandle(value.entity),
      .user = mapHandle(value.user),
      .stateId = value.stateId,
      .flags = value.flags,
      .stateRevision = value.stateRevision,
  };
  return out.valid() ? std::optional{out} : std::nullopt;
}

[[nodiscard]] inline std::optional<ServerPresentationInteractiveStateRecord>
mapInteractive(const ClientRuntimeInteractiveState& value) noexcept {
  return mapInteractive(ClientRuntimeBootstrapInteractiveState{
      .entity = value.entity,
      .user = value.user,
      .stateId = value.stateId,
      .flags = value.flags,
      .stateRevision = value.stateRevision,
  });
}

[[nodiscard]] inline std::optional<ServerPresentationMoverStateRecord> mapMover(
    const ClientRuntimeBootstrapMoverState& value) noexcept {
  const auto phase = mapEnum<ServerPresentationMoverPhase>(value.phase);
  if(!phase)
    return std::nullopt;
  ServerPresentationMoverStateRecord out{
      .entity = mapHandle(value.entity),
      .phase = *phase,
      .keyframe = value.keyframe,
      .normalizedProgress = value.normalizedProgress,
      .flags = value.flags,
      .stateRevision = value.stateRevision,
  };
  return out.valid() ? std::optional{out} : std::nullopt;
}

[[nodiscard]] inline std::optional<ServerPresentationMoverStateRecord> mapMover(
    const ClientRuntimeMoverState& value) noexcept {
  return mapMover(ClientRuntimeBootstrapMoverState{
      .entity = value.entity,
      .phase = value.phase,
      .keyframe = value.keyframe,
      .normalizedProgress = value.normalizedProgress,
      .flags = value.flags,
      .stateRevision = value.stateRevision,
  });
}

[[nodiscard]] inline std::optional<ServerPresentationBootstrap> mapBootstrap(
    const ClientRuntimeCompletedBootstrap& value) {
  if(!value.worldDescriptor.has_value() ||
     !sameWorld(value.world, value.worldDescriptor->world) ||
     value.baseline.serverTick != value.serverTick ||
     value.baseline.aggregateRevision != value.aggregateRevision ||
     value.worldDescriptor->baseline.serverTick != value.baseline.serverTick ||
     value.worldDescriptor->baseline.aggregateRevision !=
         value.baseline.aggregateRevision ||
     value.contentFingerprint != value.worldDescriptor->contentFingerprint ||
     value.storyProjectionFingerprint !=
         value.worldDescriptor->storyProjectionFingerprint) {
    return std::nullopt;
  }

  const auto& descriptor = *value.worldDescriptor;
  ServerPresentationBootstrap out;
  out.route = mapRoute(value.connectionId, value.routeEpoch, value.world);
  out.baseline = mapBaseline(value.baseline);
  out.world = {
      .presentation = {
          .archetypeId = descriptor.worldArchetypeId,
          .presentationId = descriptor.worldPresentationId,
          .revision = descriptor.presentationRevision,
      },
      .contentFingerprint = descriptor.contentFingerprint,
      .storyProjectionFingerprint = descriptor.storyProjectionFingerprint,
      .descriptorRevision = descriptor.descriptorRevision,
      .ticksPerDay = descriptor.ticksPerDay,
  };
  if(!out.route.valid() || !out.baseline.valid() || !out.world.valid())
    return std::nullopt;

  out.entities.reserve(value.entities.size());
  for(const auto& source : value.entities) {
    auto mapped = mapEntity(source);
    if(!mapped)
      return std::nullopt;
    out.entities.push_back(std::move(*mapped));
  }
  out.npcStates.reserve(value.npcStates.size());
  for(const auto& source : value.npcStates) {
    auto mapped = mapNpc(source);
    if(!mapped)
      return std::nullopt;
    out.npcStates.push_back(std::move(*mapped));
  }
  out.interactives.reserve(value.interactiveStates.size());
  for(const auto& source : value.interactiveStates) {
    auto mapped = mapInteractive(source);
    if(!mapped)
      return std::nullopt;
    out.interactives.push_back(std::move(*mapped));
  }
  out.movers.reserve(value.moverStates.size());
  for(const auto& source : value.moverStates) {
    auto mapped = mapMover(source);
    if(!mapped)
      return std::nullopt;
    out.movers.push_back(std::move(*mapped));
  }
  return out;
}

inline void observeRoute(ServerPresentationMailboxBatch& out,
                         const ServerPresentationRouteIdentity candidate) {
  if(!candidate.valid())
    return;
  if(!out.route.has_value() ||
     candidate.connectionId != out.route->connectionId ||
     candidate.routeEpoch > out.route->routeEpoch ||
     (candidate.routeEpoch == out.route->routeEpoch &&
      candidate.world.id == out.route->world.id &&
      candidate.world.generation > out.route->world.generation)) {
    out.route = candidate;
  }
}

template<class Event>
inline void appendEvent(ServerPresentationMailboxBatch& out,
                        std::optional<Event> mapped) {
  if(!mapped) {
    ++out.rejectedRecords;
    return;
  }
  observeRoute(out, mapped->header.route);
  out.events.emplace_back(std::move(*mapped));
}

} // namespace Detail

[[nodiscard]] inline ServerPresentationMailboxBatch
mapClientRuntimePresentationMailbox(
    ClientRuntimePresentationMailboxSnapshot source) {
  using namespace ClientSandbox;
  using namespace Detail;

  ServerPresentationMailboxBatch out;
  const auto eventCount = source.worldDescriptors.size() +
                          source.movementCorrections.size() +
                          source.entitySpawns.size() +
                          source.entityDespawns.size() +
                          source.entityTransforms.size() +
                          source.npcStates.size() + source.dialogEvents.size() +
                          source.interactiveStates.size() +
                          source.moverStates.size();
  out.events.reserve(eventCount);
  out.bootstraps.reserve(source.bootstraps.size());

  if(source.protocolState.has_value() && source.protocolState->routeBound) {
    observeRoute(out, mapRoute(source.protocolState->connectionId,
                               source.protocolState->routeEpoch,
                               source.protocolState->world));
  }

  for(const auto& value : source.bootstraps) {
    auto mapped = mapBootstrap(value);
    if(!mapped) {
      ++out.rejectedRecords;
      continue;
    }
    observeRoute(out, mapped->route);
    out.bootstraps.push_back(std::move(*mapped));
  }

  for(const auto& value : source.worldDescriptors) {
    ServerWorldDescriptorEvent event{
        .header = mapHeader(value.replication),
        .descriptor = {
            .presentation = {
                .archetypeId = value.worldArchetypeId,
                .presentationId = value.worldPresentationId,
                .revision = value.presentationRevision,
            },
            .contentFingerprint = value.contentFingerprint,
            .storyProjectionFingerprint = value.storyProjectionFingerprint,
            .descriptorRevision = value.descriptorRevision,
            .ticksPerDay = value.ticksPerDay,
        },
    };
    appendEvent(out, event.header.valid() && event.descriptor.valid()
                         ? std::optional{event}
                         : std::nullopt);
  }

  for(const auto& value : source.entitySpawns) {
    auto entity = mapEntity(value);
    ServerEntitySpawnEvent event{.header = mapHeader(value.replication)};
    if(entity)
      event.entity = std::move(*entity);
    appendEvent(out, entity && event.header.valid() ? std::optional{event}
                                                    : std::nullopt);
  }

  for(const auto& value : source.entityDespawns) {
    const auto reason = mapEnum<ServerEntityDespawnReason>(value.reason);
    ServerEntityDespawnEvent event{
        .header = mapHeader(value.replication),
        .entity = mapHandle(value.entity),
        .entityRevision = value.entityRevision,
    };
    if(reason)
      event.reason = *reason;
    appendEvent(out, reason && event.header.valid() && event.entity.valid() &&
                             event.entityRevision != 0U
                         ? std::optional{event}
                         : std::nullopt);
  }

  for(const auto& value : source.entityTransforms) {
    const auto transform = mapTransform(value.transform);
    ServerEntityTransformEvent event{
        .header = mapHeader(value.replication),
        .entity = mapHandle(value.entity),
        .entityRevision = value.entityRevision,
    };
    if(transform)
      event.transform = *transform;
    appendEvent(out, transform && event.header.valid() && event.entity.valid() &&
                             event.entityRevision != 0U
                         ? std::optional{event}
                         : std::nullopt);
  }

  for(const auto& value : source.movementCorrections) {
    const auto transform = mapTransform(value.transform);
    const auto reason =
        mapEnum<ServerMovementCorrectionReason>(value.reason);
    ServerMovementCorrectionEvent event{
        .header = mapHeader(value.replication),
        .entity = mapHandle(value.entity),
        .movementRevision = value.movementRevision,
    };
    if(transform)
      event.transform = *transform;
    if(reason)
      event.reason = *reason;
    appendEvent(out, transform && reason && event.header.valid() &&
                             event.entity.valid() &&
                             event.movementRevision != 0U
                         ? std::optional{event}
                         : std::nullopt);
  }

  for(const auto& value : source.npcStates) {
    auto state = mapNpc(value);
    ServerNpcStateEvent event{.header = mapHeader(value.replication)};
    if(state)
      event.state = std::move(*state);
    appendEvent(out, state && event.header.valid() ? std::optional{event}
                                                   : std::nullopt);
  }

  for(const auto& value : source.dialogEvents) {
    const auto header = mapHeader(value.replication);
    std::optional<ServerPresentationEvent> mapped;
    if(header.valid()) {
      switch(value.kind) {
        case ClientRuntimeDialogEventKind::Start: {
          ServerDialogStartEvent event{
              .header = header,
              .sessionId = value.sessionId,
              .player = mapHandle(value.player),
              .npc = mapHandle(value.npc),
              .topicId = value.topicId,
              .dialogRevision = value.dialogRevision,
          };
          if(event.sessionId != 0U && event.player.valid() &&
             event.npc.valid() && event.dialogRevision != 0U)
            mapped = event;
          break;
        }
        case ClientRuntimeDialogEventKind::Update: {
          ServerDialogUpdateEvent event{
              .header = header,
              .sessionId = value.sessionId,
              .speaker = mapHandle(value.speaker),
              .lineId = value.lineId,
              .choicesRevision = value.choicesRevision,
              .dialogRevision = value.dialogRevision,
              .flags = value.flags,
          };
          if(event.sessionId != 0U && event.speaker.valid() &&
             event.dialogRevision != 0U &&
             (event.flags & ~KnownServerDialogUpdateFlags) == 0U)
            mapped = event;
          break;
        }
        case ClientRuntimeDialogEventKind::End: {
          const auto reason = mapEnum<ServerDialogEndReason>(value.endReason);
          if(reason && value.sessionId != 0U && value.dialogRevision != 0U) {
            mapped = ServerDialogEndEvent{
                .header = header,
                .sessionId = value.sessionId,
                .reason = *reason,
                .dialogRevision = value.dialogRevision,
            };
          }
          break;
        }
        case ClientRuntimeDialogEventKind::Busy: {
          const auto reason = mapEnum<ServerDialogBusyReason>(value.busyReason);
          const auto npc = mapHandle(value.npc);
          if(reason && npc.valid() && value.dialogRevision != 0U) {
            mapped = ServerDialogBusyEvent{
                .header = header,
                .npc = npc,
                .activeSessionId = value.sessionId,
                .reason = *reason,
                .retryAfterMilliseconds = value.retryAfterMilliseconds,
                .dialogRevision = value.dialogRevision,
            };
          }
          break;
        }
      }
    }
    if(!mapped) {
      ++out.rejectedRecords;
      continue;
    }
    observeRoute(out, serverPresentationEventHeader(*mapped).route);
    out.events.push_back(std::move(*mapped));
  }

  for(const auto& value : source.interactiveStates) {
    auto state = mapInteractive(value);
    ServerInteractiveStateEvent event{.header = mapHeader(value.replication)};
    if(state)
      event.state = std::move(*state);
    appendEvent(out, state && event.header.valid() ? std::optional{event}
                                                   : std::nullopt);
  }

  for(const auto& value : source.moverStates) {
    auto state = mapMover(value);
    ServerMoverStateEvent event{.header = mapHeader(value.replication)};
    if(state)
      event.state = std::move(*state);
    appendEvent(out, state && event.header.valid() ? std::optional{event}
                                                   : std::nullopt);
  }

  std::stable_sort(out.events.begin(), out.events.end(),
                   [](const ServerPresentationEvent& lhs,
                      const ServerPresentationEvent& rhs) noexcept {
                     return serverPresentationEventHeader(lhs).streamSequence <
                            serverPresentationEventHeader(rhs).streamSequence;
                   });
  return out;
}

[[nodiscard]] inline ClientRuntimePresentationMailboxSnapshot
drainClientRuntimePresentationMailbox(
    ClientSandbox::ClientRuntimeFacade& facade) {
  ClientRuntimePresentationMailboxSnapshot source;
  source.protocolState = facade.protocolV2State();
  source.worldDescriptors = facade.drainWorldDescriptors();
  source.movementCorrections = facade.drainMovementCorrections();
  source.entitySpawns = facade.drainEntitySpawns();
  source.entityDespawns = facade.drainEntityDespawns();
  source.entityTransforms = facade.drainEntityTransforms();
  source.npcStates = facade.drainNpcStates();
  source.dialogEvents = facade.drainDialogEvents();
  source.interactiveStates = facade.drainInteractiveStates();
  source.moverStates = facade.drainMoverStates();
  source.bootstraps = facade.drainCompletedBootstraps();
  return source;
}

[[nodiscard]] inline ServerPresentationMailboxBatch
drainServerPresentationMailbox(ClientSandbox::ClientRuntimeFacade& facade) {
  return mapClientRuntimePresentationMailbox(
      drainClientRuntimePresentationMailbox(facade));
}

} // namespace Mmo::ClientPresentation

#endif // OPENGOTHIC_MMO_SANDBOX_FACADE
