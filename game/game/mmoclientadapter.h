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

struct ClientMmoSubmitResult final {
  ClientMmoSubmitStatus status = ClientMmoSubmitStatus::Disabled;
  std::uint64_t droppedCount = 0;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == ClientMmoSubmitStatus::Accepted ||
           status == ClientMmoSubmitStatus::Disabled;
  }
};

struct ClientEntityHandle final {
  std::uint64_t id = 0;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return id != 0U && generation != 0U;
  }
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

// Engine-facing movement proposal/checkpoint. This DTO deliberately contains
// no packet kind, route header, sequence number, codec field or transport
// handle. The adapter copies it into the current sandbox facade contract
// synchronously. All string views need remain valid only for the call.
struct ClientMovementIntent final {
  ClientMovementRequestKind kind = ClientMovementRequestKind::MovementProposal;
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
  std::uint64_t clientTick = 0;
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
[[nodiscard]] ClientMmoSubmitResult submitClientWeaponState(
    const ClientWeaponStateRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientCombat(
    const ClientCombatRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientDialogChoice(
    const ClientDialogChoiceRequest& request) noexcept;

} // namespace Mmo
