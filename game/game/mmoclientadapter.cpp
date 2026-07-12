#include "mmoclientadapter.h"

#include <utility>

#include "mmoclientadapterdetail.h"
#include "mmoclientbridge.h"

namespace Mmo {
namespace {

[[nodiscard]] constexpr ClientMmoSubmitResult submitResult(
    const ClientMmoSubmitStatus status) noexcept {
  return {.status = status, .droppedCount = 0};
}

template<class Packet>
[[nodiscard]] ClientMmoSubmitResult submitCompatibilityPacket(
    std::optional<Packet> packet) noexcept {
  if(!packet.has_value())
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  try {
    return submitClientIntent(Net::ClientIntentPacket{std::move(*packet)});
  } catch(...) {
    return submitResult(ClientMmoSubmitStatus::TransportError);
  }
}

} // namespace

ClientMmoSubmitResult submitClientMovement(
    const ClientMovementIntent& intent) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  return submitCompatibilityPacket(
      ClientAdapterDetail::makeCompatibilityMovementPacket(intent));
}

ClientMmoSubmitResult submitClientBootstrap(
    const ClientBootstrapRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  return submitCompatibilityPacket(
      ClientAdapterDetail::makeCompatibilityBootstrapPacket(request));
}

ClientMmoSubmitResult submitClientInteraction(
    const ClientInteractionRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  if(!ClientAdapterDetail::validInteractionRequest(request))
    return submitResult(ClientMmoSubmitStatus::InvalidIntent);
  if(!ClientAdapterDetail::supportsCompatibilityInteraction(request.verb))
    return submitResult(ClientMmoSubmitStatus::UnsupportedIntent);
  return submitCompatibilityPacket(
      ClientAdapterDetail::makeCompatibilityInteractionPacket(request));
}

ClientMmoSubmitResult submitClientInventory(
    const ClientInventoryRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  return submitCompatibilityPacket(
      ClientAdapterDetail::makeCompatibilityInventoryPacket(request));
}

ClientMmoSubmitResult submitClientWeaponState(
    const ClientWeaponStateRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  return submitCompatibilityPacket(
      ClientAdapterDetail::makeCompatibilityWeaponStatePacket(request));
}

ClientMmoSubmitResult submitClientCombat(
    const ClientCombatRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};
  return submitCompatibilityPacket(
      ClientAdapterDetail::makeCompatibilityCombatPacket(request));
}

ClientMmoSubmitResult submitClientDialogChoice(
    const ClientDialogChoiceRequest& request) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};

  try {
    auto packet = ClientAdapterDetail::makeCompatibilityDialogChoicePacket(
        request, nextClientIntentSequence(), clientMmoSessionKey());
    if(!packet.has_value())
      return submitResult(ClientMmoSubmitStatus::InvalidIntent);
    return submitClientDialogChoicePacket(std::move(*packet));
  } catch(...) {
    return submitResult(ClientMmoSubmitStatus::TransportError);
  }
}

} // namespace Mmo
