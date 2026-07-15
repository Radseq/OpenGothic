#include "mmoclientadapter.h"

#include "mmoclientadapterdetail.h"
#include "mmoclientbridge.h"

namespace Mmo {
namespace {

[[nodiscard]] constexpr ClientMmoSubmitResult submitResult(
    const ClientMmoSubmitStatus status) noexcept {
  return {.status = status, .command = {}, .droppedCount = 0};
}

} // namespace

ClientMmoSubmitResult submitClientMovement(
    const ClientMovementIntent& intent) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validMovementIntent(intent))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  if(!ClientAdapterDetail::makeProtocolV2MovementRequest(intent).has_value())
    return submitResult(ClientMmoSubmitStatus::UnsupportedIntent);
  return submitProtocolV2Movement(intent);
}

ClientMmoSubmitResult submitClientBootstrap(
    const ClientBootstrapRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validBootstrapRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  try {
    ClientMmoSessionRequest session;
    session.characterName = request.displayName.empty()
                                ? std::string(request.characterKey)
                                : std::string(request.displayName);
    session.createIfMissing = true;
    session.enterWorld = true;
    return beginClientMmoSession(session)
               ? submitResult(ClientMmoSubmitStatus::Accepted)
               : submitResult(ClientMmoSubmitStatus::TransportError);
  } catch(...) {
    return submitResult(ClientMmoSubmitStatus::TransportError);
  }
}

ClientMmoSubmitResult submitClientInteraction(
    const ClientInteractionRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validInteractionRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  if(!ClientAdapterDetail::makeProtocolV2InteractionRequest(request).has_value())
    return submitResult(ClientMmoSubmitStatus::UnsupportedIntent);
  return submitProtocolV2Interaction(request);
}

ClientMmoSubmitResult submitClientInventory(
    const ClientInventoryRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validInventoryRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  // Legacy inventory hooks do not carry Protocol V2 item/entity generations
  // and expected revisions, so forwarding them would be non-authoritative.
  return submitResult(ClientMmoSubmitStatus::UnsupportedIntent);
}

ClientMmoSubmitResult submitClientEquipItem(
    const ClientEquipItemRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validEquipItemRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  return submitProtocolV2EquipItem(request);
}

ClientMmoSubmitResult submitClientUnequipItem(
    const ClientUnequipItemRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validUnequipItemRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  return submitProtocolV2UnequipItem(request);
}

ClientMmoSubmitResult submitClientUseItem(
    const ClientUseItemRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validUseItemRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  return submitProtocolV2UseItem(request);
}

ClientMmoSubmitResult submitClientDropItem(
    const ClientDropItemRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validDropItemRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  return submitProtocolV2DropItem(request);
}

ClientMmoSubmitResult submitClientSplitStack(
    const ClientSplitStackRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validSplitStackRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  return submitProtocolV2SplitStack(request);
}

ClientMmoSubmitResult submitClientMergeStack(
    const ClientMergeStackRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validMergeStackRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  return submitProtocolV2MergeStack(request);
}

ClientMmoSubmitResult submitClientWeaponState(
    const ClientWeaponStateRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validWeaponStateRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  return submitProtocolV2WeaponState(request);
}

ClientMmoSubmitResult submitClientCombat(
    const ClientCombatRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validCombatRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  if(!ClientAdapterDetail::makeProtocolV2CombatRequest(request).has_value())
    return submitResult(ClientMmoSubmitStatus::UnsupportedIntent);
  return submitProtocolV2Combat(request);
}

ClientMmoSubmitResult submitClientDialogChoice(
    const ClientDialogChoiceRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};

  if(!ClientAdapterDetail::validDialogChoiceRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  if(!ClientAdapterDetail::makeProtocolV2DialogChoiceRequest(request).has_value())
    return submitResult(ClientMmoSubmitStatus::UnsupportedIntent);
  return submitProtocolV2DialogChoice(request);
}

} // namespace Mmo
