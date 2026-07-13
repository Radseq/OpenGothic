#pragma once

#include "mmoclientadapter.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include <gothic/mmo/client_runtime_facade.h>

namespace Mmo::ClientAdapterDetail {

inline constexpr std::size_t MaximumTextSize = 4096U;
inline constexpr std::uint16_t KnownMovementStateMask =
    static_cast<std::uint16_t>(ClientMovementState::InAir) |
    static_cast<std::uint16_t>(ClientMovementState::Falling) |
    static_cast<std::uint16_t>(ClientMovementState::FallingDeep) |
    static_cast<std::uint16_t>(ClientMovementState::Sliding) |
    static_cast<std::uint16_t>(ClientMovementState::Jumping) |
    static_cast<std::uint16_t>(ClientMovementState::JumpingUp) |
    static_cast<std::uint16_t>(ClientMovementState::Swimming) |
    static_cast<std::uint16_t>(ClientMovementState::Diving) |
    static_cast<std::uint16_t>(ClientMovementState::InWater);

[[nodiscard]] inline bool validRequiredText(
    const std::string_view value) noexcept {
  return !value.empty() && value.size() <= MaximumTextSize;
}

[[nodiscard]] inline bool validOptionalText(
    const std::string_view value) noexcept {
  return value.size() <= MaximumTextSize;
}

[[nodiscard]] inline bool finitePosition(
    const ClientPosition& position) noexcept {
  return std::isfinite(position.x) && std::isfinite(position.y) &&
         std::isfinite(position.z);
}

[[nodiscard]] constexpr bool validMovementState(
    const ClientMovementState state) noexcept {
  const auto value = static_cast<std::uint16_t>(state);
  return (value & static_cast<std::uint16_t>(~KnownMovementStateMask)) == 0U;
}

[[nodiscard]] constexpr bool knownMovementRequestKind(
    const ClientMovementRequestKind kind) noexcept {
  return kind == ClientMovementRequestKind::MovementProposal ||
         kind == ClientMovementRequestKind::CharacterCheckpoint;
}

[[nodiscard]] constexpr bool knownMovementMode(
    const ClientMovementMode mode) noexcept {
  switch(mode) {
    case ClientMovementMode::Walk:
    case ClientMovementMode::Run:
    case ClientMovementMode::Sneak:
    case ClientMovementMode::Swim:
    case ClientMovementMode::Dive:
    case ClientMovementMode::Climb:
      return true;
  }
  return false;
}

[[nodiscard]] constexpr bool normalizedAxis(const std::int16_t value) noexcept {
  return value != std::numeric_limits<std::int16_t>::min();
}

[[nodiscard]] inline bool finiteSample(
    const ClientMovementSample& sample) noexcept {
  return std::isfinite(sample.x) && std::isfinite(sample.y) &&
         std::isfinite(sample.z) && std::isfinite(sample.yaw);
}

[[nodiscard]] inline bool validMovementIntent(
    const ClientMovementIntent& intent) noexcept {
  return knownMovementRequestKind(intent.kind) &&
         knownMovementMode(intent.mode) && intent.to.tick >= intent.from.tick &&
         finiteSample(intent.from) && finiteSample(intent.to) &&
         validMovementState(intent.from.state) &&
         validMovementState(intent.to.state) &&
         std::isfinite(intent.cadence.minimumDistance) &&
         std::isfinite(intent.cadence.minimumYawDegrees) &&
         intent.cadence.minimumDistance >= 0.0 &&
         intent.cadence.minimumYawDegrees >= 0.0 &&
         normalizedAxis(intent.forward) && normalizedAxis(intent.right) &&
         normalizedAxis(intent.viewYaw) && normalizedAxis(intent.viewPitch) &&
         validRequiredText(intent.targetKey) &&
         validRequiredText(intent.actorKey) &&
         validRequiredText(intent.characterKey) &&
         validRequiredText(intent.world) && validOptionalText(intent.source) &&
         validOptionalText(intent.waypointKey) &&
         validOptionalText(intent.reason);
}

[[nodiscard]] inline bool fitsQuantizedPosition(const double value) noexcept {
  return std::isfinite(value) &&
         value >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) &&
         value <= static_cast<double>(std::numeric_limits<std::int32_t>::max());
}

[[nodiscard]] constexpr ClientSandbox::ClientRuntimeMovementMode
toRuntime(const ClientMovementMode mode) noexcept {
  using Runtime = ClientSandbox::ClientRuntimeMovementMode;
  switch(mode) {
    case ClientMovementMode::Walk: return Runtime::Walk;
    case ClientMovementMode::Run: return Runtime::Run;
    case ClientMovementMode::Sneak: return Runtime::Sneak;
    case ClientMovementMode::Swim: return Runtime::Swim;
    case ClientMovementMode::Dive: return Runtime::Dive;
    case ClientMovementMode::Climb: return Runtime::Climb;
  }
  return Runtime::Walk;
}

[[nodiscard]] inline std::optional<ClientSandbox::ClientRuntimeMovementRequest>
makeProtocolV2MovementRequest(const ClientMovementIntent& intent) noexcept {
  if(!validMovementIntent(intent) || !intent.hasNormalizedInput ||
     intent.kind != ClientMovementRequestKind::MovementProposal ||
     !fitsQuantizedPosition(intent.to.x) ||
     !fitsQuantizedPosition(intent.to.y) ||
     !fitsQuantizedPosition(intent.to.z)) {
    return std::nullopt;
  }
  return ClientSandbox::ClientRuntimeMovementRequest{
      .forward = intent.forward,
      .right = intent.right,
      .viewYaw = intent.viewYaw,
      .viewPitch = intent.viewPitch,
      .mode = toRuntime(intent.mode),
      .flags = intent.inputFlags,
      .predictedX = static_cast<std::int32_t>(std::llround(intent.to.x)),
      .predictedY = static_cast<std::int32_t>(std::llround(intent.to.y)),
      .predictedZ = static_cast<std::int32_t>(std::llround(intent.to.z)),
      .predictedYaw = intent.predictedYaw,
      .lastAcknowledgedServerTick = intent.lastAcknowledgedServerTick,
  };
}

[[nodiscard]] inline bool validBootstrapRequest(
    const ClientBootstrapRequest& request) noexcept {
  return validRequiredText(request.targetKey) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.characterKey) &&
         validRequiredText(request.world) &&
         validRequiredText(request.serverEndpoint) &&
         validOptionalText(request.source) &&
         validOptionalText(request.displayName) &&
         validOptionalText(request.clientContentManifestHash) &&
         validOptionalText(request.reason);
}

[[nodiscard]] constexpr bool knownInteractionVerb(
    const ClientInteractionVerb verb) noexcept {
  switch(verb) {
    case ClientInteractionVerb::Use:
    case ClientInteractionVerb::Open:
    case ClientInteractionVerb::Close:
    case ClientInteractionVerb::Activate:
    case ClientInteractionVerb::Talk:
    case ClientInteractionVerb::Loot:
    case ClientInteractionVerb::Sleep:
    case ClientInteractionVerb::Read:
    case ClientInteractionVerb::Lockpick:
      return true;
  }
  return false;
}

[[nodiscard]] inline bool validInteractionRequest(
    const ClientInteractionRequest& request) noexcept {
  return knownInteractionVerb(request.verb) &&
         finitePosition(request.actorPosition) &&
         (request.targetHandle.valid() || validRequiredText(request.targetKey)) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.characterKey) &&
         validRequiredText(request.world) &&
         validOptionalText(request.targetKey) &&
         validOptionalText(request.source) && validOptionalText(request.reason);
}

[[nodiscard]] constexpr ClientSandbox::ClientRuntimeInteractionVerb
toRuntime(const ClientInteractionVerb verb) noexcept {
  using Runtime = ClientSandbox::ClientRuntimeInteractionVerb;
  switch(verb) {
    case ClientInteractionVerb::Use: return Runtime::Use;
    case ClientInteractionVerb::Open: return Runtime::Open;
    case ClientInteractionVerb::Close: return Runtime::Close;
    case ClientInteractionVerb::Activate: return Runtime::Activate;
    case ClientInteractionVerb::Talk: return Runtime::Talk;
    case ClientInteractionVerb::Loot: return Runtime::Loot;
    case ClientInteractionVerb::Sleep: return Runtime::Sleep;
    case ClientInteractionVerb::Read: return Runtime::Read;
    case ClientInteractionVerb::Lockpick: return Runtime::Lockpick;
  }
  return Runtime::Use;
}

[[nodiscard]] constexpr ClientSandbox::ClientRuntimeEntityHandle
toRuntime(const ClientEntityHandle handle) noexcept {
  return {
      .world = {.id = handle.worldId, .generation = handle.worldGeneration},
      .id = handle.id,
      .generation = handle.generation,
  };
}

[[nodiscard]] inline std::optional<ClientSandbox::ClientRuntimeInteractRequest>
makeProtocolV2InteractionRequest(
    const ClientInteractionRequest& request) noexcept {
  if(!validInteractionRequest(request) || !request.targetHandle.valid())
    return std::nullopt;
  return ClientSandbox::ClientRuntimeInteractRequest{
      .verb = toRuntime(request.verb),
      .target = toRuntime(request.targetHandle),
      .expectedTargetRevision = request.expectedTargetRevision,
      .argumentId = request.argumentId,
  };
}

[[nodiscard]] constexpr bool knownInventoryAction(
    const ClientInventoryAction action) noexcept {
  switch(action) {
    case ClientInventoryAction::PickupWorldItem:
    case ClientInventoryAction::EquipCharacterItem:
    case ClientInventoryAction::UnequipCharacterItem:
    case ClientInventoryAction::TakeContainerItem:
    case ClientInventoryAction::LootNpcInventory:
    case ClientInventoryAction::DropCharacterItem:
    case ClientInventoryAction::TradeBuyFromNpc:
    case ClientInventoryAction::TradeSellToNpc:
    case ClientInventoryAction::ConsumeItem:
      return true;
  }
  return false;
}

[[nodiscard]] inline bool validInventoryActionFields(
    const ClientInventoryRequest& request) noexcept {
  const auto hasItem = request.itemSymbol.has_value();
  const auto hasSourceItem = request.sourceItemPersistentId.has_value();
  switch(request.action) {
    case ClientInventoryAction::PickupWorldItem:
      return hasItem && request.inventoryItemSymbol.has_value() &&
             request.sourceWorldItemPersistentId.has_value();
    case ClientInventoryAction::EquipCharacterItem:
    case ClientInventoryAction::UnequipCharacterItem:
      return hasItem && request.itemPersistentId.has_value() &&
             validRequiredText(request.equipmentSlot);
    case ClientInventoryAction::TakeContainerItem:
      return hasItem && hasSourceItem &&
             validRequiredText(request.sourceEntityKey) &&
             validRequiredText(request.sourceContainerKey) &&
             validRequiredText(request.containerKey);
    case ClientInventoryAction::LootNpcInventory:
      return hasItem && hasSourceItem &&
             validRequiredText(request.sourceNpcKey) &&
             validRequiredText(request.sourceEntityKey);
    case ClientInventoryAction::DropCharacterItem:
      return hasItem && request.itemPersistentId.has_value() &&
             request.worldItemPersistentId.has_value();
    case ClientInventoryAction::TradeBuyFromNpc:
      return hasItem && request.vendorItemPersistentId.has_value() &&
             validRequiredText(request.npcKey) &&
             validRequiredText(request.targetNpcEntityKey);
    case ClientInventoryAction::TradeSellToNpc:
      return hasItem && request.sellerItemPersistentId.has_value() &&
             validRequiredText(request.npcKey) &&
             validRequiredText(request.targetNpcEntityKey);
    case ClientInventoryAction::ConsumeItem:
      return hasItem && request.itemPersistentId.has_value();
  }
  return false;
}

[[nodiscard]] inline bool validInventoryRequest(
    const ClientInventoryRequest& request) noexcept {
  return knownInventoryAction(request.action) && request.amount != 0U &&
         request.amount <= std::numeric_limits<std::uint32_t>::max() &&
         finitePosition(request.actorPosition) &&
         validRequiredText(request.targetKey) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.itemTemplateKey) &&
         validRequiredText(request.world) && validOptionalText(request.source) &&
         validOptionalText(request.equipmentSlot) &&
         validOptionalText(request.sourceEntityKey) &&
         validOptionalText(request.sourceContainerKey) &&
         validOptionalText(request.containerKey) &&
         validOptionalText(request.sourceNpcKey) &&
         validOptionalText(request.targetNpcEntityKey) &&
         validOptionalText(request.npcKey) && validOptionalText(request.reason) &&
         validInventoryActionFields(request);
}

[[nodiscard]] constexpr bool knownWeaponStateIntent(
    const ClientWeaponStateIntent intent) noexcept {
  return intent == ClientWeaponStateIntent::Ready ||
         intent == ClientWeaponStateIntent::Holster;
}

[[nodiscard]] inline bool validWeaponStateRequest(
    const ClientWeaponStateRequest& request) noexcept {
  return knownWeaponStateIntent(request.intent) &&
         finitePosition(request.actorPosition) &&
         validRequiredText(request.targetKey) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.characterKey) &&
         validRequiredText(request.world) && validOptionalText(request.source) &&
         validOptionalText(request.reason);
}

[[nodiscard]] inline std::optional<ClientSandbox::ClientRuntimeCombatActionRequest>
makeProtocolV2WeaponStateRequest(
    const ClientWeaponStateRequest& request) noexcept {
  if(!validWeaponStateRequest(request))
    return std::nullopt;
  return ClientSandbox::ClientRuntimeCombatActionRequest{
      .action = request.intent == ClientWeaponStateIntent::Holster
                    ? ClientSandbox::ClientRuntimeCombatAction::HolsterWeapon
                    : ClientSandbox::ClientRuntimeCombatAction::DrawWeapon,
      .flags = 0U,
      .aim = {},
      .target = std::nullopt,
      .selectedItem = std::nullopt,
      .comboIndex = 0U,
      .lastAcknowledgedServerTick = 0U,
      .expectedTargetRevision = 0U,
  };
}

[[nodiscard]] inline bool validCombatRequest(
    const ClientCombatRequest& request) noexcept {
  return finitePosition(request.actorPosition) &&
         validRequiredText(request.targetKey) &&
         validRequiredText(request.actorKey) &&
         validRequiredText(request.npcEntityKey) &&
         validRequiredText(request.world) &&
         validRequiredText(request.combatAction) &&
         validRequiredText(request.intentState) &&
         validOptionalText(request.source) && validOptionalText(request.reason) &&
         validOptionalText(request.targetNpcEntityKey) &&
         normalizedAxis(request.aimX) && normalizedAxis(request.aimY) &&
         normalizedAxis(request.aimZ);
}

[[nodiscard]] constexpr ClientSandbox::ClientRuntimeCombatAction
toRuntime(const ClientCombatRequest::Action action) noexcept {
  using Runtime = ClientSandbox::ClientRuntimeCombatAction;
  switch(action) {
    case ClientCombatRequest::Action::DrawWeapon: return Runtime::DrawWeapon;
    case ClientCombatRequest::Action::HolsterWeapon: return Runtime::HolsterWeapon;
    case ClientCombatRequest::Action::PrimaryAttack: return Runtime::PrimaryAttack;
    case ClientCombatRequest::Action::SecondaryAttack: return Runtime::SecondaryAttack;
    case ClientCombatRequest::Action::Parry: return Runtime::Parry;
    case ClientCombatRequest::Action::Dodge: return Runtime::Dodge;
    case ClientCombatRequest::Action::Cancel: return Runtime::Cancel;
  }
  return Runtime::Cancel;
}

[[nodiscard]] inline std::optional<ClientSandbox::ClientRuntimeCombatActionRequest>
makeProtocolV2CombatRequest(const ClientCombatRequest& request) noexcept {
  if(!validCombatRequest(request) || !request.protocolAction.has_value() ||
     (request.targetHandle.has_value() && !request.targetHandle->valid())) {
    return std::nullopt;
  }
  ClientSandbox::ClientRuntimeCombatActionRequest out{
      .action = toRuntime(*request.protocolAction),
      .flags = request.flags,
      .aim = {.x = request.aimX, .y = request.aimY, .z = request.aimZ},
      .target = std::nullopt,
      .selectedItem = std::nullopt,
      .comboIndex = request.comboIndex,
      .lastAcknowledgedServerTick = request.lastAcknowledgedServerTick,
      .expectedTargetRevision = request.expectedTargetRevision,
  };
  if(request.targetHandle.has_value())
    out.target = toRuntime(*request.targetHandle);
  return out;
}

[[nodiscard]] inline bool validDialogChoiceRequest(
    const ClientDialogChoiceRequest& request) noexcept {
  return request.expectedRevision != 0U &&
         request.clientChoiceSequence != 0U &&
         validRequiredText(request.sessionUuid) &&
         validRequiredText(request.characterKey) &&
         validRequiredText(request.conversationId) &&
         validRequiredText(request.choiceId);
}

[[nodiscard]] inline std::optional<ClientSandbox::ClientRuntimeDialogChoiceRequest>
makeProtocolV2DialogChoiceRequest(
    const ClientDialogChoiceRequest& request) noexcept {
  if(!validDialogChoiceRequest(request) ||
     request.protocolDialogSessionId == 0U || request.protocolChoiceId == 0U) {
    return std::nullopt;
  }
  return ClientSandbox::ClientRuntimeDialogChoiceRequest{
      .dialogSessionId = request.protocolDialogSessionId,
      .choiceId = request.protocolChoiceId,
      .expectedDialogRevision = request.expectedRevision,
  };
}

} // namespace Mmo::ClientAdapterDetail
