#pragma once

#include <cmath>
#include <cstdint>
#include <variant>
#include <vector>

namespace Mmo::ClientPresentation {

struct ServerWorldInstanceHandle final {
  std::uint64_t id = 0U;
  std::uint32_t generation = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return id != 0U && generation != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerWorldInstanceHandle&,
      const ServerWorldInstanceHandle&) noexcept = default;
};

struct ServerPresentationRouteIdentity final {
  std::uint64_t connectionId = 0U;
  std::uint64_t routeEpoch = 0U;
  ServerWorldInstanceHandle world{};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return connectionId != 0U && routeEpoch != 0U && world.valid();
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerPresentationRouteIdentity&,
      const ServerPresentationRouteIdentity&) noexcept = default;
};

struct ServerPresentationBaseline final {
  std::uint64_t serverTick = 0U;
  std::uint64_t aggregateRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return serverTick != 0U && aggregateRevision != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerPresentationBaseline&,
      const ServerPresentationBaseline&) noexcept = default;
};

struct ServerPresentationEventHeader final {
  ServerPresentationRouteIdentity route{};
  std::uint64_t streamSequence = 0U;
  std::uint64_t serverTick = 0U;
  std::uint64_t aggregateRevision = 0U;
  ServerPresentationBaseline baseline{};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return route.valid() && streamSequence != 0U && serverTick != 0U &&
           aggregateRevision != 0U && baseline.valid() &&
           baseline.serverTick <= serverTick &&
           baseline.aggregateRevision <= aggregateRevision;
  }
};

struct ServerPresentationEntityHandle final {
  ServerWorldInstanceHandle world{};
  std::uint64_t id = 0U;
  std::uint32_t generation = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return world.valid() && id != 0U && generation != 0U;
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return world.id == 0U && world.generation == 0U && id == 0U &&
           generation == 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerPresentationEntityHandle&,
      const ServerPresentationEntityHandle&) noexcept = default;
};

enum class ServerPresentationEntityKind : std::uint8_t {
  LocalPlayer = 1U,
  RemotePlayer = 2U,
  Npc = 3U,
};

struct ServerPresentationMapping final {
  std::uint64_t archetypeId = 0U;
  std::uint64_t presentationId = 0U;
  std::uint64_t revision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return archetypeId != 0U && presentationId != 0U && revision != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerPresentationMapping&,
      const ServerPresentationMapping&) noexcept = default;
};

struct ServerPresentationTransform final {
  double posX = 0.0;
  double posY = 0.0;
  double posZ = 0.0;
  double yaw = 0.0;
  double pitch = 0.0;
  double roll = 0.0;
  bool grounded = false;
  bool teleport = false;
  bool dormant = false;

  [[nodiscard]] bool valid() const noexcept {
    return std::isfinite(posX) && std::isfinite(posY) &&
           std::isfinite(posZ) && std::isfinite(yaw) &&
           std::isfinite(pitch) && std::isfinite(roll);
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerPresentationTransform&,
      const ServerPresentationTransform&) noexcept = default;
};

struct ServerPresentationWorldDescriptor final {
  ServerPresentationMapping presentation{};
  std::uint64_t contentFingerprint = 0U;
  std::uint64_t storyProjectionFingerprint = 0U;
  std::uint64_t descriptorRevision = 0U;
  std::uint64_t ticksPerDay = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return presentation.valid() && contentFingerprint != 0U &&
           descriptorRevision != 0U && ticksPerDay != 0U;
  }
};

struct ServerPresentationEntityRecord final {
  ServerPresentationEntityHandle handle{};
  ServerPresentationEntityKind kind = ServerPresentationEntityKind::Npc;
  ServerPresentationMapping presentation{};
  ServerPresentationTransform transform{};
  std::uint64_t entityRevision = 0U;

  [[nodiscard]] bool valid() const noexcept {
    return handle.valid() && presentation.valid() && transform.valid() &&
           entityRevision != 0U;
  }
};

enum class ServerPresentationNpcLifeState : std::uint8_t {
  Alive = 1U,
  Unconscious = 2U,
  Dead = 3U,
};

enum class ServerPresentationNpcActivityState : std::uint8_t {
  Idle = 1U,
  Routine = 2U,
  Traversal = 3U,
  Interaction = 4U,
  Dialog = 5U,
  Combat = 6U,
};

enum ServerPresentationNpcStateFlag : std::uint32_t {
  ServerPresentationNpcTargetPresent = 1U << 0U,
  ServerPresentationNpcWeaponDrawn = 1U << 1U,
  ServerPresentationNpcInvulnerable = 1U << 2U,
};
inline constexpr std::uint32_t KnownServerPresentationNpcStateFlags =
    ServerPresentationNpcTargetPresent |
    ServerPresentationNpcWeaponDrawn |
    ServerPresentationNpcInvulnerable;

struct ServerPresentationNpcStateRecord final {
  ServerPresentationEntityHandle entity{};
  ServerPresentationNpcLifeState lifeState =
      ServerPresentationNpcLifeState::Alive;
  ServerPresentationNpcActivityState activityState =
      ServerPresentationNpcActivityState::Idle;
  std::uint8_t weaponMode = 0U;
  std::uint32_t flags = 0U;
  ServerPresentationEntityHandle target{};
  std::int32_t health = 0;
  std::int32_t maximumHealth = 0;
  std::int32_t mana = 0;
  std::int32_t maximumMana = 0;
  std::uint64_t stateRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool knownLifeState =
        lifeState == ServerPresentationNpcLifeState::Alive ||
        lifeState == ServerPresentationNpcLifeState::Unconscious ||
        lifeState == ServerPresentationNpcLifeState::Dead;
    const bool knownActivityState =
        activityState == ServerPresentationNpcActivityState::Idle ||
        activityState == ServerPresentationNpcActivityState::Routine ||
        activityState == ServerPresentationNpcActivityState::Traversal ||
        activityState == ServerPresentationNpcActivityState::Interaction ||
        activityState == ServerPresentationNpcActivityState::Dialog ||
        activityState == ServerPresentationNpcActivityState::Combat;
    const bool targetPresent =
        (flags & ServerPresentationNpcTargetPresent) != 0U;
    const bool validTarget = targetPresent ? target.valid() : target.empty();
    return entity.valid() && knownLifeState && knownActivityState &&
           (flags & ~KnownServerPresentationNpcStateFlags) == 0U &&
           validTarget && health >= 0 && maximumHealth >= health &&
           mana >= 0 && maximumMana >= mana && stateRevision != 0U;
  }
};

enum ServerPresentationInteractiveStateFlag : std::uint32_t {
  ServerPresentationInteractiveEnabled = 1U << 0U,
  ServerPresentationInteractiveLocked = 1U << 1U,
  ServerPresentationInteractiveOccupied = 1U << 2U,
};
inline constexpr std::uint32_t KnownServerPresentationInteractiveStateFlags =
    ServerPresentationInteractiveEnabled |
    ServerPresentationInteractiveLocked |
    ServerPresentationInteractiveOccupied;

struct ServerPresentationInteractiveStateRecord final {
  ServerPresentationEntityHandle entity{};
  ServerPresentationEntityHandle user{};
  std::uint64_t stateId = 0U;
  std::uint32_t flags = 0U;
  std::uint64_t stateRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool occupied =
        (flags & ServerPresentationInteractiveOccupied) != 0U;
    const bool validUser = occupied ? user.valid() : user.empty();
    return entity.valid() &&
           (flags & ~KnownServerPresentationInteractiveStateFlags) == 0U &&
           validUser && stateId != 0U && stateRevision != 0U;
  }
};

enum class ServerPresentationMoverPhase : std::uint8_t {
  AtStart = 1U,
  Opening = 2U,
  AtEnd = 3U,
  Closing = 4U,
  Paused = 5U,
};

enum ServerPresentationMoverStateFlag : std::uint32_t {
  ServerPresentationMoverLocked = 1U << 0U,
  ServerPresentationMoverLooping = 1U << 1U,
  ServerPresentationMoverBlocked = 1U << 2U,
};
inline constexpr std::uint32_t KnownServerPresentationMoverStateFlags =
    ServerPresentationMoverLocked |
    ServerPresentationMoverLooping |
    ServerPresentationMoverBlocked;

struct ServerPresentationMoverStateRecord final {
  ServerPresentationEntityHandle entity{};
  ServerPresentationMoverPhase phase = ServerPresentationMoverPhase::AtStart;
  std::uint16_t keyframe = 0U;
  std::uint16_t normalizedProgress = 0U;
  std::uint32_t flags = 0U;
  std::uint64_t stateRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool knownPhase = phase == ServerPresentationMoverPhase::AtStart ||
                            phase == ServerPresentationMoverPhase::Opening ||
                            phase == ServerPresentationMoverPhase::AtEnd ||
                            phase == ServerPresentationMoverPhase::Closing ||
                            phase == ServerPresentationMoverPhase::Paused;
    return entity.valid() && knownPhase &&
           (flags & ~KnownServerPresentationMoverStateFlags) == 0U &&
           stateRevision != 0U;
  }
};

struct ServerPresentationBootstrap final {
  ServerPresentationRouteIdentity route{};
  ServerPresentationBaseline baseline{};
  ServerPresentationWorldDescriptor world{};
  std::vector<ServerPresentationEntityRecord> entities;
  std::vector<ServerPresentationNpcStateRecord> npcStates;
  std::vector<ServerPresentationInteractiveStateRecord> interactives;
  std::vector<ServerPresentationMoverStateRecord> movers;
};

struct ServerWorldDescriptorEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationWorldDescriptor descriptor{};
};

struct ServerEntitySpawnEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityRecord entity{};
};

enum class ServerEntityDespawnReason : std::uint8_t {
  LeftInterest = 1U,
  Destroyed = 2U,
  WorldTransition = 3U,
  ReplacedGeneration = 4U,
};

struct ServerEntityDespawnEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle entity{};
  ServerEntityDespawnReason reason = ServerEntityDespawnReason::LeftInterest;
  std::uint64_t entityRevision = 0U;
};

struct ServerEntityTransformEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle entity{};
  ServerPresentationTransform transform{};
  std::uint64_t entityRevision = 0U;
};

enum class ServerMovementCorrectionReason : std::uint8_t {
  Reconciliation = 1U,
  Collision = 2U,
  Teleport = 3U,
  InvalidInput = 4U,
  Resync = 5U,
};

struct ServerMovementCorrectionEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle entity{};
  ServerPresentationTransform transform{};
  ServerMovementCorrectionReason reason =
      ServerMovementCorrectionReason::Reconciliation;
  std::uint64_t movementRevision = 0U;
};

struct ServerNpcStateEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationNpcStateRecord state{};
};

struct ServerDialogStartEvent final {
  ServerPresentationEventHeader header{};
  std::uint64_t sessionId = 0U;
  ServerPresentationEntityHandle player{};
  ServerPresentationEntityHandle npc{};
  std::uint64_t topicId = 0U;
  std::uint64_t dialogRevision = 0U;
};

enum ServerDialogUpdateFlag : std::uint32_t {
  ServerDialogAwaitingChoice = 1U << 0U,
  ServerDialogSkippable = 1U << 1U,
  ServerDialogSpeakerIsPlayer = 1U << 2U,
};
inline constexpr std::uint32_t KnownServerDialogUpdateFlags =
    ServerDialogAwaitingChoice | ServerDialogSkippable |
    ServerDialogSpeakerIsPlayer;

struct ServerDialogUpdateEvent final {
  ServerPresentationEventHeader header{};
  std::uint64_t sessionId = 0U;
  ServerPresentationEntityHandle speaker{};
  std::uint64_t lineId = 0U;
  std::uint64_t choicesRevision = 0U;
  std::uint64_t dialogRevision = 0U;
  std::uint32_t flags = 0U;
};

enum class ServerDialogEndReason : std::uint8_t {
  Completed = 1U,
  Cancelled = 2U,
  ParticipantUnavailable = 3U,
  RouteChanged = 4U,
};

enum class ServerDialogBusyReason : std::uint8_t {
  NpcInAnotherDialog = 1U,
  NpcUnavailable = 2U,
  DialogCooldown = 3U,
};

struct ServerDialogEndEvent final {
  ServerPresentationEventHeader header{};
  std::uint64_t sessionId = 0U;
  ServerDialogEndReason reason = ServerDialogEndReason::Completed;
  std::uint64_t dialogRevision = 0U;
};

struct ServerDialogBusyEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle npc{};
  std::uint64_t activeSessionId = 0U;
  ServerDialogBusyReason reason = ServerDialogBusyReason::NpcUnavailable;
  std::uint32_t retryAfterMilliseconds = 0U;
  std::uint64_t dialogRevision = 0U;
};

struct ServerInteractiveStateEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationInteractiveStateRecord state{};
};

struct ServerMoverStateEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationMoverStateRecord state{};
};

using ServerPresentationEvent = std::variant<
    ServerWorldDescriptorEvent,
    ServerEntitySpawnEvent,
    ServerEntityDespawnEvent,
    ServerEntityTransformEvent,
    ServerMovementCorrectionEvent,
    ServerNpcStateEvent,
    ServerDialogStartEvent,
    ServerDialogUpdateEvent,
    ServerDialogEndEvent,
    ServerDialogBusyEvent,
    ServerInteractiveStateEvent,
    ServerMoverStateEvent>;

} // namespace Mmo::ClientPresentation
