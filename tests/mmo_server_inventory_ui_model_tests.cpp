#include "ui/mmoserverinventorypagemodel.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

using Mmo::ClientEquipmentSlot;
using Mmo::ClientItemStackHandle;
using Mmo::ClientMmoCommandCompletionStatus;
using Mmo::ClientMmoCommandKind;
using Mmo::ClientMmoCommandToken;
using namespace Mmo::ClientPresentation;

[[nodiscard]] constexpr ClientItemStackHandle handle(
    const std::uint64_t id,
    const std::uint32_t generation = 1U) noexcept {
  return {.instanceId = id, .generation = generation};
}

[[nodiscard]] ServerInventoryStack stack(
    const std::uint64_t id,
    const std::uint32_t quantity,
    const std::uint64_t itemRevision = 1U) {
  return {
      .handle = handle(id),
      .archetypeId = 1000U + id,
      .presentationId = 2000U + id,
      .presentationRevision = 1U,
      .quantity = quantity,
      .flags = 0U,
      .itemRevision = itemRevision,
  };
}

[[nodiscard]] constexpr ClientMmoCommandToken command(
    const ClientMmoCommandKind kind,
    const std::uint64_t sequence) noexcept {
  return {
      .kind = kind,
      .routeEpoch = 7U,
      .sequence = sequence,
      .idempotencyKeyHigh = 0xA5U,
      .idempotencyKeyLow = sequence,
  };
}

[[nodiscard]] bool check(const bool condition,
                         const std::string_view expression,
                         const int line) {
  if(condition)
    return true;
  std::cerr << "check failed at line " << line << ": " << expression << '\n';
  return false;
}

#define CHECK(expression) \
  do { \
    if(!check(static_cast<bool>(expression), #expression, __LINE__)) \
      return false; \
  } while(false)

[[nodiscard]] bool installReadyState(ServerInventoryPresentationState& state) {
  return state.install(
             {.revision = 10U, .stacks = {stack(3U, 5U), stack(9U, 1U)}},
             {.revision = 20U,
              .equipped = {{.slot = ClientEquipmentSlot::MeleeWeapon,
                            .item = handle(9U),
                            .itemRevision = 1U}}}) ==
         ServerInventoryApplyStatus::Applied;
}

[[nodiscard]] bool pendingAcceptedAndAuthoritativeVisibility() {
  ServerInventoryPresentationState state;
  CHECK(installReadyState(state));

  ServerInventoryPageModel initial(state);
  CHECK(initial.ready());
  CHECK(initial.actionsEnabled());
  CHECK(initial.feedbackStatus() == ServerInventoryFeedbackStatus::Ready);
  CHECK(initial.stacks().size() == 2U);
  const auto initialFingerprint = initial.fingerprint();

  const auto token = command(ClientMmoCommandKind::DropItem, 1U);
  CHECK(state.markPending({
      .command = token,
      .primary = handle(3U),
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = 10U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));

  ServerInventoryPageModel submitted(state);
  CHECK(submitted.feedbackStatus() == ServerInventoryFeedbackStatus::Pending);
  CHECK(submitted.pendingPhase(handle(3U)) ==
        ServerInventoryPendingPhase::Submitted);
  const auto submittedFingerprint = submitted.fingerprint();
  CHECK(submittedFingerprint.feedbackRevision !=
        initialFingerprint.feedbackRevision);
  CHECK(state.inventory().find(handle(3U))->quantity == 5U);

  state.complete({
      .command = token,
      .status = ClientMmoCommandCompletionStatus::Applied,
  });
  ServerInventoryPageModel accepted(state);
  CHECK(accepted.feedbackStatus() == ServerInventoryFeedbackStatus::Accepted);
  CHECK(accepted.pendingPhase(handle(3U)) ==
        ServerInventoryPendingPhase::AwaitingAuthoritativeDelta);
  CHECK(accepted.fingerprint().feedbackRevision !=
        submittedFingerprint.feedbackRevision);
  CHECK(state.inventory().find(handle(3U))->quantity == 5U);

  CHECK(state.apply({
            .previousRevision = 10U,
            .revision = 11U,
            .upserted = {stack(3U, 4U, 2U), stack(12U, 2U)},
            .removed = {},
        }) == ServerInventoryApplyStatus::Applied);
  ServerInventoryPageModel authoritative(state);
  CHECK(authoritative.feedbackStatus() == ServerInventoryFeedbackStatus::Ready);
  CHECK(!authoritative.pendingPhase(handle(3U)).has_value());
  CHECK(authoritative.stackAt(2U) != nullptr);
  CHECK(authoritative.stackAt(2U)->handle == handle(12U));
  CHECK(authoritative.stackAt(2U)->quantity == 2U);
  return true;
}

[[nodiscard]] bool authoritativeLootAcquisitionsBecomeVisible() {
  ServerInventoryPresentationState state;
  CHECK(installReadyState(state));

  CHECK(state.apply({
            .previousRevision = 10U,
            .revision = 11U,
            .upserted = {stack(12U, 1U)},
            .removed = {},
        }) == ServerInventoryApplyStatus::Applied);
  ServerInventoryPageModel afterPickup(state);
  CHECK(afterPickup.stacks().size() == 3U);
  CHECK(afterPickup.stackAt(2U) != nullptr);
  CHECK(afterPickup.stackAt(2U)->handle == handle(12U));

  CHECK(state.apply({
            .previousRevision = 11U,
            .revision = 12U,
            .upserted = {stack(13U, 4U)},
            .removed = {},
        }) == ServerInventoryApplyStatus::Applied);
  ServerInventoryPageModel afterQuickLoot(state);
  CHECK(afterQuickLoot.stacks().size() == 4U);
  CHECK(afterQuickLoot.stackAt(3U) != nullptr);
  CHECK(afterQuickLoot.stackAt(3U)->handle == handle(13U));
  CHECK(afterQuickLoot.stackAt(3U)->quantity == 4U);
  return true;
}

[[nodiscard]] bool rejectedAndResyncRequiredAreDistinct() {
  using Code = Mmo::ProtocolV2::CommandRejectionCode;

  ServerInventoryPresentationState state;
  CHECK(installReadyState(state));

  const auto rejectedToken = command(ClientMmoCommandKind::UseItem, 2U);
  CHECK(state.markPending({
      .command = rejectedToken,
      .primary = handle(3U),
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = 10U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));
  state.complete({
      .command = rejectedToken,
      .status = ClientMmoCommandCompletionStatus::Rejected,
      .rejectionCode = static_cast<std::uint16_t>(Code::DomainRejected),
  });
  ServerInventoryPageModel rejected(state);
  CHECK(rejected.feedbackStatus() == ServerInventoryFeedbackStatus::Rejected);
  CHECK(rejected.actionsEnabled());
  CHECK(!rejected.rejectionMessage().empty());

  state.rejectSubmission(Mmo::ClientMmoSubmitStatus::QueueFull);
  ServerInventoryPageModel localRejection(state);
  CHECK(localRejection.feedbackStatus() ==
        ServerInventoryFeedbackStatus::Rejected);
  CHECK(localRejection.rejectionMessage().find("queue") != std::string::npos);

  const auto staleToken = command(ClientMmoCommandKind::SplitStack, 3U);
  CHECK(state.markPending({
      .command = staleToken,
      .primary = handle(3U),
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = 10U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));
  state.complete({
      .command = staleToken,
      .status = ClientMmoCommandCompletionStatus::Rejected,
      .rejectionCode =
          static_cast<std::uint16_t>(Code::AggregateRevisionMismatch),
  });
  ServerInventoryPageModel stale(state);
  CHECK(stale.feedbackStatus() ==
        ServerInventoryFeedbackStatus::ResyncRequired);
  CHECK(!stale.actionsEnabled());
  CHECK(!state.markPending({
      .command = command(ClientMmoCommandKind::UseItem, 4U),
      .primary = handle(9U),
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = 10U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));

  CHECK(state.installInventory(
            {.revision = 10U,
             .stacks = {stack(3U, 5U), stack(9U, 1U)}}) ==
        ServerInventoryApplyStatus::Duplicate);
  CHECK(ServerInventoryPageModel(state).feedbackStatus() ==
        ServerInventoryFeedbackStatus::ResyncRequired);
  CHECK(!ServerInventoryPageModel(state).actionsEnabled());

  CHECK(state.apply({
            .previousRevision = 10U,
            .revision = 11U,
            .upserted = {stack(3U, 5U, 2U)},
            .removed = {},
        }) == ServerInventoryApplyStatus::Applied);
  ServerInventoryPageModel resynchronized(state);
  CHECK(resynchronized.feedbackStatus() ==
        ServerInventoryFeedbackStatus::Ready);
  CHECK(resynchronized.actionsEnabled());
  return true;
}


[[nodiscard]] bool actionRequestsUseExactAuthoritativeRevisions() {
  ServerInventoryPresentationState state;
  CHECK(installReadyState(state));
  const ServerInventoryPageModel page(state);

  const auto use = page.useRequest(handle(3U));
  CHECK(use.has_value());
  CHECK(use->item == handle(3U));
  CHECK(use->expectedInventoryRevision == 10U);
  CHECK(!use->target.has_value());

  const auto unequip = page.unequipRequest(handle(9U));
  CHECK(unequip.has_value());
  CHECK(unequip->slot == ClientEquipmentSlot::MeleeWeapon);
  CHECK(unequip->expectedInventoryRevision == 10U);
  CHECK(unequip->expectedEquipmentRevision == 20U);

  const auto equip = page.equipRequest(handle(3U), ClientEquipmentSlot::Armor);
  CHECK(equip.has_value());
  CHECK(equip->item == handle(3U));
  CHECK(equip->slot == ClientEquipmentSlot::Armor);
  CHECK(equip->expectedInventoryRevision == 10U);
  CHECK(equip->expectedEquipmentRevision == 20U);

  const Mmo::ClientPosition position{.x = 1.0, .y = 2.0, .z = 3.0};
  const auto drop = page.dropRequest(handle(3U), 2U, position);
  CHECK(drop.has_value());
  CHECK(drop->item == handle(3U));
  CHECK(drop->amount == 2U);
  CHECK(drop->proposedPosition.x == 1.0);
  CHECK(drop->proposedPosition.y == 2.0);
  CHECK(drop->proposedPosition.z == 3.0);
  CHECK(drop->expectedInventoryRevision == 10U);

  const auto split = page.splitRequest(handle(3U), 2U);
  CHECK(split.has_value());
  CHECK(split->item == handle(3U));
  CHECK(split->amount == 2U);
  CHECK(split->expectedInventoryRevision == 10U);

  const auto merge = page.mergeRequest(handle(3U), handle(9U), 5U);
  CHECK(merge.has_value());
  CHECK(merge->source == handle(3U));
  CHECK(merge->destination == handle(9U));
  CHECK(merge->amount == 5U);
  CHECK(merge->expectedInventoryRevision == 10U);
  return true;
}

[[nodiscard]] bool exactCommandIdentityAndRevisionsArePreserved() {
  ServerInventoryPendingState pending;
  CHECK(pending.add({
      .command = command(ClientMmoCommandKind::EquipItem, 10U),
      .primary = handle(1U),
      .secondary = handle(2U),
      .slot = ClientEquipmentSlot::Armor,
      .expectedInventoryRevision = 101U,
      .expectedEquipmentRevision = 201U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));
  CHECK(pending.add({
      .command = command(ClientMmoCommandKind::UnequipItem, 11U),
      .primary = handle(2U),
      .secondary = {},
      .slot = ClientEquipmentSlot::Armor,
      .expectedInventoryRevision = 102U,
      .expectedEquipmentRevision = 202U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));
  CHECK(pending.add({
      .command = command(ClientMmoCommandKind::UseItem, 12U),
      .primary = handle(3U),
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = 103U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));
  CHECK(pending.add({
      .command = command(ClientMmoCommandKind::DropItem, 13U),
      .primary = handle(4U),
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = 104U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));
  CHECK(pending.add({
      .command = command(ClientMmoCommandKind::SplitStack, 14U),
      .primary = handle(5U),
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = 105U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));
  CHECK(pending.add({
      .command = command(ClientMmoCommandKind::MergeStack, 15U),
      .primary = handle(6U),
      .secondary = handle(7U),
      .slot = std::nullopt,
      .expectedInventoryRevision = 106U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));

  const auto commands = pending.commands();
  CHECK(commands.size() == 6U);
  CHECK(commands[0].primary == handle(1U));
  CHECK(commands[0].secondary == handle(2U));
  CHECK(commands[0].slot == ClientEquipmentSlot::Armor);
  CHECK(commands[0].expectedInventoryRevision == 101U);
  CHECK(commands[0].expectedEquipmentRevision == 201U);
  CHECK(commands[1].expectedInventoryRevision == 102U);
  CHECK(commands[1].expectedEquipmentRevision == 202U);
  CHECK(commands[2].expectedInventoryRevision == 103U);
  CHECK(commands[3].expectedInventoryRevision == 104U);
  CHECK(commands[4].expectedInventoryRevision == 105U);
  CHECK(commands[5].primary == handle(6U));
  CHECK(commands[5].secondary == handle(7U));
  CHECK(commands[5].expectedInventoryRevision == 106U);
  return true;
}

[[nodiscard]] bool disconnectAndReplacementResetThePage() {
  ServerInventoryPresentationState state;
  CHECK(installReadyState(state));
  const auto initialReplacementRevision =
      ServerInventoryPageModel(state).fingerprint().replacementRevision;
  const auto token = command(ClientMmoCommandKind::UseItem, 20U);
  CHECK(state.markPending({
      .command = token,
      .primary = handle(3U),
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = 10U,
      .expectedEquipmentRevision = 0U,
      .phase = ServerInventoryPendingPhase::Submitted,
  }));
  state.complete({
      .command = token,
      .status = ClientMmoCommandCompletionStatus::ConnectionLost,
  });
  CHECK(!ServerInventoryPageModel(state).ready());
  CHECK(state.pending().commands().empty());
  CHECK(ServerInventoryPageModel(state).fingerprint().replacementRevision !=
        initialReplacementRevision);

  CHECK(installReadyState(state));
  const auto reconnectReplacementRevision =
      ServerInventoryPageModel(state).fingerprint().replacementRevision;
  state.reset();
  CHECK(!ServerInventoryPageModel(state).ready());
  CHECK(ServerInventoryPageModel(state).fingerprint().replacementRevision !=
        reconnectReplacementRevision);
  CHECK(state.inventory().stacks().empty());
  CHECK(state.equipment().bindings().size() ==
        ServerEquipmentReadModel::SlotCount);
  return true;
}

} // namespace

int main() {
  const bool ok = pendingAcceptedAndAuthoritativeVisibility() &&
                  authoritativeLootAcquisitionsBecomeVisible() &&
                  rejectedAndResyncRequiredAreDistinct() &&
                  actionRequestsUseExactAuthoritativeRevisions() &&
                  exactCommandIdentityAndRevisionsArePreserved() &&
                  disconnectAndReplacementResetThePage();
  return ok ? 0 : 1;
}
