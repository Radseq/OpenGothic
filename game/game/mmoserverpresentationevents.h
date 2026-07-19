#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "mmoserverinventoryreadmodel.h"

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

  [[nodiscard]] constexpr bool empty() const noexcept {
    return archetypeId == 0U && presentationId == 0U && revision == 0U;
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

enum class ServerPresentationWorldObjectKind : std::uint8_t {
  Item = 1U,
  Interactive = 2U,
  Mover = 3U,
  Container = 4U,
  Trigger = 5U,
};

struct ServerPresentationWorldObjectRecord final {
  ServerPresentationEntityHandle entity{};
  std::uint64_t worldObjectId = 0U;
  ServerPresentationWorldObjectKind kind =
      ServerPresentationWorldObjectKind::Item;
  ServerPresentationMapping presentation{};
  ServerPresentationTransform transform{};
  std::uint64_t stateRevision = 0U;
  std::uint32_t flags = 0U;

  [[nodiscard]] bool valid() const noexcept {
    const bool knownKind = kind == ServerPresentationWorldObjectKind::Item ||
                           kind == ServerPresentationWorldObjectKind::Interactive ||
                           kind == ServerPresentationWorldObjectKind::Mover ||
                           kind == ServerPresentationWorldObjectKind::Container ||
                           kind == ServerPresentationWorldObjectKind::Trigger;
    return entity.valid() && worldObjectId != 0U && knownKind &&
           presentation.valid() && transform.valid() && stateRevision != 0U &&
           flags == 0U;
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

struct ServerPresentationItemHandle final {
  std::uint64_t id = 0U;
  std::uint32_t generation = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return id != 0U && generation != 0U;
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return id == 0U && generation == 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ServerPresentationItemHandle&,
      const ServerPresentationItemHandle&) noexcept = default;
};

enum class ServerPresentationEquipmentSlot : std::uint8_t {
  MeleeWeapon = 1U,
  RangedWeapon = 2U,
  Armor = 3U,
  Amulet = 4U,
  RingLeft = 5U,
  RingRight = 6U,
  Belt = 7U,
  Spell = 8U,
};

inline constexpr std::size_t ServerPresentationEquipmentSlotCount = 8U;

enum ServerPresentationEquipmentStateFlag : std::uint32_t {
  ServerPresentationEquipmentOccupied = 1U << 0U,
  ServerPresentationEquipmentTwoHanded = 1U << 1U,
  ServerPresentationEquipmentCrossbow = 1U << 2U,
};
inline constexpr std::uint32_t KnownServerPresentationEquipmentStateFlags =
    ServerPresentationEquipmentOccupied |
    ServerPresentationEquipmentTwoHanded |
    ServerPresentationEquipmentCrossbow;

[[nodiscard]] constexpr bool isKnownServerPresentationEquipmentSlot(
    const ServerPresentationEquipmentSlot slot) noexcept {
  return slot >= ServerPresentationEquipmentSlot::MeleeWeapon &&
         slot <= ServerPresentationEquipmentSlot::Spell;
}

[[nodiscard]] constexpr std::size_t serverPresentationEquipmentSlotIndex(
    const ServerPresentationEquipmentSlot slot) noexcept {
  return static_cast<std::size_t>(slot) - 1U;
}

struct ServerPresentationEquipmentSlotRecord final {
  ServerPresentationEntityHandle entity{};
  ServerPresentationEquipmentSlot slot =
      ServerPresentationEquipmentSlot::MeleeWeapon;
  ServerPresentationItemHandle item{};
  ServerPresentationMapping presentation{};
  std::uint32_t flags = 0U;
  std::uint64_t equipmentRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool occupied =
        (flags & ServerPresentationEquipmentOccupied) != 0U;
    const bool twoHanded =
        (flags & ServerPresentationEquipmentTwoHanded) != 0U;
    const bool crossbow =
        (flags & ServerPresentationEquipmentCrossbow) != 0U;
    const bool payloadValid = occupied
                                  ? item.valid() && presentation.valid()
                                  : item.empty() && presentation.empty() &&
                                        !twoHanded && !crossbow;
    const bool compatibleFlags =
        (!twoHanded || slot == ServerPresentationEquipmentSlot::MeleeWeapon) &&
        (!crossbow || slot == ServerPresentationEquipmentSlot::RangedWeapon);
    return entity.valid() && isKnownServerPresentationEquipmentSlot(slot) &&
           (flags & ~KnownServerPresentationEquipmentStateFlags) == 0U &&
           payloadValid && compatibleFlags && equipmentRevision != 0U;
  }
};

enum class ServerPresentationWeaponMode : std::uint8_t {
  None = 1U,
  Melee = 2U,
  Ranged = 3U,
  Magic = 4U,
  Fist = 5U,
};

struct ServerPresentationWeaponModeRecord final {
  ServerPresentationEntityHandle entity{};
  ServerPresentationWeaponMode mode = ServerPresentationWeaponMode::None;
  std::uint64_t weaponRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool knownMode = mode == ServerPresentationWeaponMode::None ||
                           mode == ServerPresentationWeaponMode::Melee ||
                           mode == ServerPresentationWeaponMode::Ranged ||
                           mode == ServerPresentationWeaponMode::Magic ||
                           mode == ServerPresentationWeaponMode::Fist;
    return entity.valid() && knownMode && weaponRevision != 0U;
  }
};

enum class ServerPresentationCombatActionKind : std::uint8_t {
  LightAttack = 1U,
  HeavyAttack = 2U,
  ComboAttack = 3U,
  Parry = 4U,
  Dodge = 5U,
  CancelAction = 6U,
};

enum ServerPresentationCombatActionFlag : std::uint32_t {
  ServerPresentationCombatActionPredictedLocally = 1U << 0U,
  ServerPresentationCombatActionLeftSide = 1U << 1U,
  ServerPresentationCombatActionRightSide = 1U << 2U,
};
inline constexpr std::uint32_t KnownServerPresentationCombatActionFlags =
    ServerPresentationCombatActionPredictedLocally |
    ServerPresentationCombatActionLeftSide |
    ServerPresentationCombatActionRightSide;

struct ServerPresentationCombatActionRecord final {
  ServerPresentationEntityHandle entity{};
  ServerPresentationEntityHandle target{};
  std::uint64_t actionId = 0U;
  std::uint64_t clientActionSequence = 0U;
  ServerPresentationCombatActionKind kind =
      ServerPresentationCombatActionKind::LightAttack;
  std::uint16_t comboIndex = 0U;
  std::uint32_t flags = 0U;
  std::uint64_t startTick = 0U;
  std::uint64_t activeStartTick = 0U;
  std::uint64_t activeEndTick = 0U;
  std::uint64_t recoveryEndTick = 0U;
  std::uint64_t actionRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool knownKind =
        kind == ServerPresentationCombatActionKind::LightAttack ||
        kind == ServerPresentationCombatActionKind::HeavyAttack ||
        kind == ServerPresentationCombatActionKind::ComboAttack ||
        kind == ServerPresentationCombatActionKind::Parry ||
        kind == ServerPresentationCombatActionKind::Dodge ||
        kind == ServerPresentationCombatActionKind::CancelAction;
    const bool sideFlagsCompatible =
        (flags & (ServerPresentationCombatActionLeftSide |
                  ServerPresentationCombatActionRightSide)) !=
        (ServerPresentationCombatActionLeftSide |
         ServerPresentationCombatActionRightSide);
    const bool predictionMetadataValid =
        (flags & ServerPresentationCombatActionPredictedLocally) == 0U ||
        clientActionSequence != 0U;
    return entity.valid() && (target.empty() || target.valid()) &&
           actionId != 0U && knownKind &&
           (flags & ~KnownServerPresentationCombatActionFlags) == 0U &&
           sideFlagsCompatible && predictionMetadataValid && startTick != 0U &&
           startTick <= activeStartTick && activeStartTick <= activeEndTick &&
           activeEndTick <= recoveryEndTick && actionRevision != 0U;
  }
};

enum class ServerPresentationCombatActionResult : std::uint8_t {
  Completed = 1U,
  Cancelled = 2U,
  Rejected = 3U,
};

struct ServerPresentationCombatActionResolution final {
  ServerPresentationEntityHandle entity{};
  std::uint64_t actionId = 0U;
  std::uint64_t clientActionSequence = 0U;
  ServerPresentationCombatActionResult result =
      ServerPresentationCombatActionResult::Completed;
  ServerPresentationWeaponMode authoritativeWeaponMode =
      ServerPresentationWeaponMode::None;
  std::uint64_t actionRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool knownResult =
        result == ServerPresentationCombatActionResult::Completed ||
        result == ServerPresentationCombatActionResult::Cancelled ||
        result == ServerPresentationCombatActionResult::Rejected;
    const bool knownMode =
        authoritativeWeaponMode == ServerPresentationWeaponMode::None ||
        authoritativeWeaponMode == ServerPresentationWeaponMode::Melee ||
        authoritativeWeaponMode == ServerPresentationWeaponMode::Ranged ||
        authoritativeWeaponMode == ServerPresentationWeaponMode::Magic ||
        authoritativeWeaponMode == ServerPresentationWeaponMode::Fist;
    return entity.valid() && actionId != 0U && knownResult && knownMode &&
           actionRevision != 0U;
  }
};

enum ServerPresentationDamageFlag : std::uint32_t {
  ServerPresentationDamageCritical = 1U << 0U,
  ServerPresentationDamageBlocked = 1U << 1U,
  ServerPresentationDamageLethal = 1U << 2U,
};
inline constexpr std::uint32_t KnownServerPresentationDamageFlags =
    ServerPresentationDamageCritical |
    ServerPresentationDamageBlocked |
    ServerPresentationDamageLethal;

struct ServerPresentationDamageRecord final {
  ServerPresentationEntityHandle source{};
  ServerPresentationEntityHandle target{};
  std::uint64_t actionId = 0U;
  std::int32_t amount = 0;
  std::int32_t health = 0;
  std::int32_t maximumHealth = -1;
  std::uint32_t flags = 0U;
  std::uint64_t damageRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return (source.empty() || source.valid()) && target.valid() &&
           amount >= 0 && health >= 0 &&
           (maximumHealth < 0 || maximumHealth >= health) &&
           (flags & ~KnownServerPresentationDamageFlags) == 0U &&
           damageRevision != 0U;
  }
};

enum class ServerPresentationHitReactionKind : std::uint8_t {
  Light = 1U,
  Heavy = 2U,
  Blocked = 3U,
  Knockback = 4U,
  Knockdown = 5U,
};

enum ServerPresentationHitReactionFlag : std::uint32_t {
  ServerPresentationHitReactionVfx = 1U << 0U,
  ServerPresentationHitReactionSfx = 1U << 1U,
  ServerPresentationHitReactionCameraShake = 1U << 2U,
};
inline constexpr std::uint32_t KnownServerPresentationHitReactionFlags =
    ServerPresentationHitReactionVfx |
    ServerPresentationHitReactionSfx |
    ServerPresentationHitReactionCameraShake;

struct ServerPresentationHitReactionRecord final {
  ServerPresentationEntityHandle source{};
  ServerPresentationEntityHandle target{};
  std::uint64_t actionId = 0U;
  ServerPresentationHitReactionKind kind =
      ServerPresentationHitReactionKind::Light;
  float knockbackX = 0.0F;
  float knockbackY = 0.0F;
  float knockbackZ = 0.0F;
  float cameraShakeStrength = 0.0F;
  std::uint32_t flags = 0U;
  std::uint64_t reactionRevision = 0U;

  [[nodiscard]] bool valid() const noexcept {
    const bool knownKind = kind == ServerPresentationHitReactionKind::Light ||
                           kind == ServerPresentationHitReactionKind::Heavy ||
                           kind == ServerPresentationHitReactionKind::Blocked ||
                           kind == ServerPresentationHitReactionKind::Knockback ||
                           kind == ServerPresentationHitReactionKind::Knockdown;
    return (source.empty() || source.valid()) && target.valid() &&
           knownKind && std::isfinite(knockbackX) &&
           std::isfinite(knockbackY) && std::isfinite(knockbackZ) &&
           std::isfinite(cameraShakeStrength) && cameraShakeStrength >= 0.0F &&
           (flags & ~KnownServerPresentationHitReactionFlags) == 0U &&
           reactionRevision != 0U;
  }
};

struct ServerPresentationLifeStateRecord final {
  ServerPresentationEntityHandle entity{};
  ServerPresentationNpcLifeState lifeState =
      ServerPresentationNpcLifeState::Alive;
  std::int32_t health = -1;
  std::int32_t maximumHealth = -1;
  std::uint64_t lifeRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool knownState = lifeState == ServerPresentationNpcLifeState::Alive ||
                            lifeState == ServerPresentationNpcLifeState::Unconscious ||
                            lifeState == ServerPresentationNpcLifeState::Dead;
    const bool healthKnown = health >= 0;
    const bool maximumKnown = maximumHealth >= 0;
    const bool healthValid = healthKnown == maximumKnown &&
                             (!healthKnown || maximumHealth >= health);
    return entity.valid() && knownState && healthValid && lifeRevision != 0U;
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
  ServerInventorySnapshot inventory;
  ServerEquipmentSnapshot equipment;
  std::vector<ServerPresentationEntityRecord> entities;
  std::vector<ServerPresentationWorldObjectRecord> worldObjects;
  std::vector<ServerPresentationNpcStateRecord> npcStates;
  std::vector<ServerPresentationEquipmentSlotRecord> combatEquipment;
  std::vector<ServerPresentationWeaponModeRecord> weaponModes;
  std::vector<ServerPresentationCombatActionRecord> combatActions;
  std::vector<ServerPresentationLifeStateRecord> lifeStates;
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


struct ServerWorldItemSpawnEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle entity{};
  std::uint64_t worldObjectId = 0U;
  ServerPresentationMapping presentation{};
  ServerPresentationTransform transform{};
  std::uint32_t quantity = 0U;
  std::uint32_t flags = 0U;
  std::uint64_t stateRevision = 0U;

  [[nodiscard]] bool valid() const noexcept {
    return header.valid() && entity.valid() && worldObjectId != 0U &&
           presentation.valid() && transform.valid() && quantity != 0U &&
           stateRevision != 0U;
  }
};

struct ServerWorldItemDespawnEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle entity{};
  std::uint8_t reason = 0U;
  std::uint64_t stateRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return header.valid() && entity.valid() && reason != 0U &&
           stateRevision != 0U;
  }
};

struct ServerWorldItemStateChangedEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle entity{};
  std::uint32_t quantity = 0U;
  std::uint32_t flags = 0U;
  std::uint64_t stateRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return header.valid() && entity.valid() && stateRevision != 0U;
  }
};

struct ServerInventorySnapshotEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle owner{};
  ServerInventorySnapshot snapshot{};
};

enum class ServerInventoryLiveMutationKind : std::uint8_t {
  StackAdded = 1U,
  StackRemoved = 2U,
  StackQuantityChanged = 3U,
};

struct ServerInventoryLiveMutation final {
  ServerInventoryLiveMutationKind kind =
      ServerInventoryLiveMutationKind::StackAdded;
  ServerInventoryStack item{};
  ClientItemStackHandle stack{};
  std::uint32_t quantity = 0U;
  std::uint64_t itemRevision = 0U;
  std::uint64_t inventoryRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    if(inventoryRevision == 0U)
      return false;
    switch(kind) {
      case ServerInventoryLiveMutationKind::StackAdded:
        return item.valid() && stack == item.handle &&
               quantity == item.quantity && itemRevision == item.itemRevision;
      case ServerInventoryLiveMutationKind::StackRemoved:
        return stack.valid() && !item.valid() && quantity == 0U &&
               itemRevision != 0U;
      case ServerInventoryLiveMutationKind::StackQuantityChanged:
        return stack.valid() && !item.valid() && quantity != 0U &&
               itemRevision != 0U;
    }
    return false;
  }
};

struct ServerInventoryDeltaEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle owner{};
  ServerInventoryLiveMutation mutation{};
};

struct ServerEquipmentSnapshotEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle owner{};
  ServerEquipmentSnapshot snapshot{};
};

struct ServerEquipmentBindingChangedEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEntityHandle owner{};
  ServerEquipmentSlotChanged change{};
};

struct ServerEquipmentSlotChangedEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationEquipmentSlotRecord state{};
};

struct ServerWeaponModeChangedEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationWeaponModeRecord state{};
};

struct ServerCombatActionStartedEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationCombatActionRecord action{};
};

struct ServerCombatActionResolvedEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationCombatActionResolution resolution{};
};

struct ServerDamageAppliedEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationDamageRecord damage{};
};

struct ServerHitReactionEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationHitReactionRecord reaction{};
};

struct ServerCharacterDeathStateChangedEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationLifeStateRecord state{};
};

struct ServerInteractiveStateEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationInteractiveStateRecord state{};
};

struct ServerMoverStateEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationMoverStateRecord state{};
};


enum class ServerProjectileImpactKind : std::uint8_t {
  World = 1U,
  Actor = 2U,
};

enum class ServerProjectileDespawnReason : std::uint8_t {
  Impacted = 1U,
  Expired = 2U,
  LeftInterest = 3U,
  ReplacedByResync = 4U,
};

struct ServerPresentationProjectileSnapshot final {
  std::uint64_t projectileId = 0U;
  ServerPresentationEntityHandle owner{};
  ServerPresentationEntityHandle target{};
  std::uint64_t launcherArchetypeId = 0U;
  std::uint64_t projectileArchetypeId = 0U;
  std::uint64_t actionId = 0U;
  std::uint64_t actionSequence = 0U;
  std::uint64_t contentRevision = 0U;
  std::uint64_t rulesetId = 0U;
  std::uint64_t actionProfileId = 0U;
  std::int64_t positionXMicrometers = 0;
  std::int64_t positionYMicrometers = 0;
  std::int64_t positionZMicrometers = 0;
  std::int64_t velocityXMicrometersPerSecond = 0;
  std::int64_t velocityYMicrometersPerSecond = 0;
  std::int64_t velocityZMicrometersPerSecond = 0;
  std::uint64_t ageMicroseconds = 0U;
  std::uint64_t spawnTick = 0U;
  std::uint32_t radiusMillimeters = 0U;
  std::uint32_t flags = 0U;
  std::uint64_t stateRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return projectileId != 0U && owner.valid() &&
           launcherArchetypeId != 0U && projectileArchetypeId != 0U &&
           actionId != 0U && actionSequence != 0U && contentRevision != 0U &&
           rulesetId != 0U && actionProfileId != 0U && spawnTick != 0U &&
           radiusMillimeters != 0U && stateRevision != 0U &&
           (target.empty() || target.world == owner.world);
  }
};

struct ServerPresentationProjectileImpact final {
  std::uint64_t projectileId = 0U;
  ServerProjectileImpactKind kind = ServerProjectileImpactKind::World;
  ServerPresentationEntityHandle actor{};
  std::uint64_t worldObjectId = 0U;
  std::int64_t positionXMicrometers = 0;
  std::int64_t positionYMicrometers = 0;
  std::int64_t positionZMicrometers = 0;
  std::uint64_t impactTick = 0U;
  std::uint64_t actionId = 0U;
  std::uint64_t stateRevision = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    if(projectileId == 0U || impactTick == 0U || actionId == 0U ||
       stateRevision == 0U)
      return false;
    if(kind == ServerProjectileImpactKind::Actor)
      return actor.valid() && worldObjectId == 0U;
    return actor.empty();
  }
};

struct ServerProjectileSpawnEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationProjectileSnapshot projectile{};
};

struct ServerProjectileStateEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationProjectileSnapshot projectile{};
};

struct ServerProjectileImpactEvent final {
  ServerPresentationEventHeader header{};
  ServerPresentationProjectileImpact impact{};
};

struct ServerProjectileDespawnEvent final {
  ServerPresentationEventHeader header{};
  std::uint64_t projectileId = 0U;
  ServerProjectileDespawnReason reason =
      ServerProjectileDespawnReason::Impacted;
  std::uint64_t despawnTick = 0U;
  std::uint64_t stateRevision = 0U;
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
    ServerWorldItemSpawnEvent,
    ServerWorldItemDespawnEvent,
    ServerWorldItemStateChangedEvent,
    ServerInventorySnapshotEvent,
    ServerInventoryDeltaEvent,
    ServerEquipmentSnapshotEvent,
    ServerEquipmentBindingChangedEvent,
    ServerEquipmentSlotChangedEvent,
    ServerWeaponModeChangedEvent,
    ServerCombatActionStartedEvent,
    ServerCombatActionResolvedEvent,
    ServerDamageAppliedEvent,
    ServerHitReactionEvent,
    ServerCharacterDeathStateChangedEvent,
    ServerInteractiveStateEvent,
    ServerMoverStateEvent,
    ServerProjectileSpawnEvent,
    ServerProjectileStateEvent,
    ServerProjectileImpactEvent,
    ServerProjectileDespawnEvent>;

} // namespace Mmo::ClientPresentation
