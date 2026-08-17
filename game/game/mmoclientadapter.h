#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace Mmo {

enum class ClientMmoSubmitStatus : std::uint8_t {
  Disabled,
  Accepted,
  InvalidIntent,
  UnsupportedIntent,
  QueueFull,
  TransportError,
};

enum class ClientMmoCommandKind : std::uint16_t {
  Interact = 33U,
  DialogChoice = 34U,
  EquipItem = 36U,
  PickupItem = 38U,
  DropItem = 39U,
  UnequipItem = 40U,
  SplitStack = 41U,
  MergeStack = 42U,
  UseItem = 56U,
  SelectiveLoot = 70U,
};

struct ClientMmoCommandToken final {
  ClientMmoCommandKind kind = ClientMmoCommandKind::UseItem;
  std::uint64_t routeEpoch = 0U;
  std::uint64_t sequence = 0U;
  std::uint64_t idempotencyKeyHigh = 0U;
  std::uint64_t idempotencyKeyLow = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return routeEpoch != 0U && sequence != 0U &&
           (idempotencyKeyHigh != 0U || idempotencyKeyLow != 0U);
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ClientMmoCommandToken&,
      const ClientMmoCommandToken&) noexcept = default;
};

enum class ClientMmoCommandCompletionStatus : std::uint8_t {
  Applied,
  Rejected,
  CancelledByRouteChange,
  TimedOut,
  ConnectionLost,
  TransportClosed,
};

struct ClientMmoCommandCompletion final {
  ClientMmoCommandToken command{};
  ClientMmoCommandCompletionStatus status =
      ClientMmoCommandCompletionStatus::Rejected;
  std::uint16_t rejectionCode = 0U;
  std::uint64_t serverTick = 0U;
  std::uint64_t aggregateRevision = 0U;
};

struct ClientMmoSubmitResult final {
  ClientMmoSubmitStatus status = ClientMmoSubmitStatus::Disabled;
  ClientMmoCommandToken command{};
  std::uint64_t droppedCount = 0;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == ClientMmoSubmitStatus::Accepted ||
           status == ClientMmoSubmitStatus::Disabled;
  }

  [[nodiscard]] constexpr bool submitted() const noexcept {
    return status == ClientMmoSubmitStatus::Accepted && command.valid();
  }
};

struct ClientEntityHandle final {
  std::uint64_t worldId = 0;
  std::uint32_t worldGeneration = 0;
  std::uint64_t id = 0;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return worldId != 0U && worldGeneration != 0U && id != 0U &&
           generation != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ClientEntityHandle&,
      const ClientEntityHandle&) noexcept = default;
};

struct ClientItemStackHandle final {
  std::uint64_t instanceId = 0U;
  std::uint32_t generation = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return instanceId != 0U && generation != 0U;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const ClientItemStackHandle&,
      const ClientItemStackHandle&) noexcept = default;
};

enum class ClientEquipmentSlot : std::uint8_t {
  MeleeWeapon,
  RangedWeapon,
  Armor,
  Amulet,
  RingLeft,
  RingRight,
  Belt,
  Spell,
};

struct ClientPosition final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

enum class ClientMovementState : std::uint16_t {
  None = 0,
  InAir = 1U << 0U,
  Falling = 1U << 1U,
  FallingDeep = 1U << 2U,
  Sliding = 1U << 3U,
  Jumping = 1U << 4U,
  JumpingUp = 1U << 5U,
  Swimming = 1U << 6U,
  Diving = 1U << 7U,
  InWater = 1U << 8U,
};

[[nodiscard]] constexpr ClientMovementState operator|(
    const ClientMovementState lhs,
    const ClientMovementState rhs) noexcept {
  return static_cast<ClientMovementState>(
      static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
}

constexpr ClientMovementState& operator|=(
    ClientMovementState& lhs,
    const ClientMovementState rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool hasMovementState(
    const ClientMovementState value,
    const ClientMovementState flag) noexcept {
  return (static_cast<std::uint16_t>(value) &
          static_cast<std::uint16_t>(flag)) != 0U;
}

struct ClientMovementSample final {
  std::uint64_t tick = 0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double yaw = 0.0;
  ClientMovementState state = ClientMovementState::None;
};

struct ClientMovementCadence final {
  std::uint64_t intervalMs = 0;
  double minimumDistance = 0.0;
  double minimumYawDegrees = 0.0;
};

enum class ClientMovementRequestKind : std::uint8_t {
  MovementProposal,
  CharacterCheckpoint,
};

enum class ClientMovementMode : std::uint8_t {
  Walk,
  Run,
  Sneak,
  Swim,
  Dive,
  Climb,
};

// Engine-facing movement proposal/checkpoint. This DTO deliberately contains
// no packet kind, route header, sequence number, codec field or transport
// handle. The adapter copies it into the current sandbox facade contract
// synchronously. All string views need remain valid only for the call.
struct ClientMovementIntent final {
  ClientMovementRequestKind kind = ClientMovementRequestKind::MovementProposal;
  // Protocol V2 transports normalized input, never a completed movement
  // result. Historical transform-only observations leave this false and are
  // rejected instead of being translated back into Protocol V1 packets.
  bool hasNormalizedInput = false;
  std::int16_t forward = 0;
  std::int16_t right = 0;
  std::int16_t viewYaw = 0;
  std::int16_t viewPitch = 0;
  ClientMovementMode mode = ClientMovementMode::Walk;
  std::uint8_t inputFlags = 0;
  std::uint16_t predictedYaw = 0;
  std::uint64_t lastAcknowledgedServerTick = 0;
  ClientMovementSample from;
  ClientMovementSample to;
  ClientMovementCadence cadence;
  std::uint64_t checkpointForceIntervalMs = 0;
  std::string_view targetKey;
  std::string_view source;
  std::string_view actorKey;
  std::string_view characterKey;
  std::string_view world;
  std::string_view waypointKey;
  std::string_view reason;
};

struct ClientBootstrapRequest final {
  std::uint64_t clientTick = 0;
  std::string_view targetKey;
  std::string_view source;
  std::string_view actorKey;
  std::string_view characterKey;
  std::string_view displayName;
  std::string_view world;
  std::string_view serverEndpoint;
  std::string_view clientContentManifestHash;
  std::string_view reason;
};

enum class ClientInteractionVerb : std::uint8_t {
  Use,
  Open,
  Close,
  Activate,
  Talk,
  Loot,
  Sleep,
  Read,
  Lockpick,
};

struct ClientInteractionRequest final {
  std::uint64_t clientTick = 0;
  ClientInteractionVerb verb = ClientInteractionVerb::Use;
  ClientEntityHandle targetHandle;
  std::uint64_t expectedTargetRevision = 0;
  std::uint64_t argumentId = 0;
  ClientPosition actorPosition;
  std::int64_t localSlotId = -1;
  std::int64_t localVobId = -1;
  std::string_view targetKey;
  std::string_view source;
  std::string_view actorKey;
  std::string_view characterKey;
  std::string_view world;
  std::string_view reason;
};

enum class ClientInventoryAction : std::uint8_t {
  PickupWorldItem,
  EquipCharacterItem,
  UnequipCharacterItem,
  TakeContainerItem,
  LootNpcInventory,
  DropCharacterItem,
  TradeBuyFromNpc,
  TradeSellToNpc,
  ConsumeItem,
};

struct ClientEquipItemRequest final {
  ClientItemStackHandle item{};
  ClientEquipmentSlot slot = ClientEquipmentSlot::MeleeWeapon;
  std::uint64_t expectedInventoryRevision = 0U;
  std::uint64_t expectedEquipmentRevision = 0U;
};

struct ClientUnequipItemRequest final {
  ClientEquipmentSlot slot = ClientEquipmentSlot::MeleeWeapon;
  std::uint64_t expectedEquipmentRevision = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientUseItemRequest final {
  ClientItemStackHandle item{};
  std::optional<ClientEntityHandle> target;
  std::uint64_t expectedInventoryRevision = 0U;
  std::uint64_t expectedTargetRevision = 0U;
};

struct ClientPickupItemRequest final {
  ClientEntityHandle worldItem{};
  std::uint32_t amount = 0U;
  std::uint64_t expectedWorldItemRevision = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientDropItemRequest final {
  ClientItemStackHandle item{};
  std::uint32_t amount = 0U;
  ClientPosition proposedPosition{};
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientSplitStackRequest final {
  ClientItemStackHandle item{};
  std::uint32_t amount = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientMergeStackRequest final {
  ClientItemStackHandle source{};
  ClientItemStackHandle destination{};
  std::uint32_t amount = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientOpenCorpseLootRequest final {
  ClientEntityHandle corpse{};
  std::uint64_t expectedCorpseRevision = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientTakeCorpseLootStackRequest final {
  ClientEntityHandle corpse{};
  ClientItemStackHandle stack{};
  std::uint32_t amount = 0U;
  std::uint64_t expectedCorpseRevision = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientTakeAllCorpseLootRequest final {
  ClientEntityHandle corpse{};
  std::uint64_t expectedCorpseRevision = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientCloseCorpseLootRequest final {
  ClientEntityHandle corpse{};
  std::uint64_t expectedCorpseRevision = 0U;
  std::uint64_t expectedInventoryRevision = 0U;
};

struct ClientInventoryRequest final {
  std::uint64_t clientTick = 0;
  ClientInventoryAction action = ClientInventoryAction::PickupWorldItem;
  std::optional<std::uint64_t> itemSymbol;
  std::optional<std::uint64_t> inventoryItemSymbol;
  std::optional<std::uint64_t> itemPersistentId;
  std::optional<std::uint64_t> sourceItemPersistentId;
  std::optional<std::uint64_t> sourceWorldItemPersistentId;
  std::optional<std::uint64_t> worldItemPersistentId;
  std::optional<std::uint64_t> vendorItemPersistentId;
  std::optional<std::uint64_t> sellerItemPersistentId;
  std::uint64_t amount = 1;
  std::int64_t equipmentSlotId = 0;
  ClientPosition actorPosition;
  std::string_view targetKey;
  std::string_view source;
  std::string_view actorKey;
  std::string_view itemTemplateKey;
  std::string_view equipmentSlot;
  std::string_view sourceEntityKey;
  std::string_view sourceContainerKey;
  std::string_view containerKey;
  std::string_view sourceNpcKey;
  std::string_view targetNpcEntityKey;
  std::string_view npcKey;
  std::string_view world;
  std::string_view reason;
};

enum class ClientWeaponStateIntent : std::uint8_t {
  Ready,
  Holster,
};

struct ClientWeaponStateRequest final {
  std::uint64_t clientTick = 0;
  ClientWeaponStateIntent intent = ClientWeaponStateIntent::Ready;
  ClientPosition actorPosition;
  std::string_view targetKey;
  std::string_view source;
  std::string_view actorKey;
  std::string_view characterKey;
  std::string_view world;
  std::string_view reason;
};

struct ClientCombatRequest final {
  enum class Action : std::uint8_t {
    DrawWeapon,
    HolsterWeapon,
    PrimaryAttack,
    SecondaryAttack,
    Parry,
    Dodge,
    Cancel,
  };

  std::uint64_t clientTick = 0;
  std::optional<Action> protocolAction;
  std::optional<ClientEntityHandle> targetHandle;
  std::int16_t aimX = 0;
  std::int16_t aimY = 0;
  std::int16_t aimZ = 0;
  std::uint16_t flags = 0;
  std::uint16_t comboIndex = 0;
  std::uint64_t lastAcknowledgedServerTick = 0;
  std::uint64_t expectedTargetRevision = 0;
  ClientPosition actorPosition;
  std::string_view targetKey;
  std::string_view source;
  std::string_view reason;
  std::string_view actorKey;
  std::string_view npcEntityKey;
  std::string_view targetNpcEntityKey;
  std::string_view world;
  std::string_view combatAction;
  std::string_view intentState;
};

struct ClientDialogChoiceRequest final {
  std::uint64_t clientTick = 0;
  std::uint64_t expectedRevision = 0;
  std::uint64_t clientChoiceSequence = 0;
  std::uint64_t protocolDialogSessionId = 0;
  std::uint64_t protocolChoiceId = 0;
  std::string_view sessionUuid;
  std::string_view characterKey;
  std::string_view conversationId;
  std::string_view choiceId;
};

[[nodiscard]] ClientMmoSubmitResult submitClientMovement(
    const ClientMovementIntent& intent) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientBootstrap(
    const ClientBootstrapRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientInteraction(
    const ClientInteractionRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientInventory(
    const ClientInventoryRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientEquipItem(
    const ClientEquipItemRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientUnequipItem(
    const ClientUnequipItemRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientUseItem(
    const ClientUseItemRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientPickupItem(
    const ClientPickupItemRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientDropItem(
    const ClientDropItemRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientSplitStack(
    const ClientSplitStackRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientMergeStack(
    const ClientMergeStackRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientOpenCorpseLoot(
    const ClientOpenCorpseLootRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientTakeCorpseLootStack(
    const ClientTakeCorpseLootStackRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientTakeAllCorpseLoot(
    const ClientTakeAllCorpseLootRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientCloseCorpseLoot(
    const ClientCloseCorpseLootRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientWeaponState(
    const ClientWeaponStateRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientCombat(
    const ClientCombatRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientDialogChoice(
    const ClientDialogChoiceRequest& request) noexcept;

} // namespace Mmo
