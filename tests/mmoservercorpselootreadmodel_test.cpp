#include "game/mmoservercorpselootreadmodel.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

using namespace Mmo;
using namespace Mmo::ClientPresentation;

constexpr ServerPresentationRouteIdentity Route{
    .connectionId = 7U,
    .routeEpoch = 3U,
    .world = {.id = 11U, .generation = 2U},
};

constexpr ServerPresentationEventHeader header(const std::uint64_t sequence) {
  return {
      .route = Route,
      .streamSequence = sequence,
      .serverTick = sequence,
      .aggregateRevision = sequence,
      .baseline = {.serverTick = 1U, .aggregateRevision = 1U},
  };
}

constexpr ClientEntityHandle Corpse{
    .worldId = 11U,
    .worldGeneration = 2U,
    .id = 91U,
    .generation = 4U,
};

constexpr ClientItemStackHandle Stack{
    .instanceId = 1001U,
    .generation = 8U,
};

constexpr ServerInventoryStack stack(const std::uint32_t quantity,
                                     const std::uint64_t revision) {
  return {
      .handle = Stack,
      .archetypeId = 41U,
      .presentationId = 52U,
      .presentationRevision = 3U,
      .quantity = quantity,
      .flags = 0U,
      .itemRevision = revision,
  };
}

constexpr ClientMmoCommandToken token(const std::uint64_t sequence) {
  return {
      .kind = ClientMmoCommandKind::SelectiveLoot,
      .routeEpoch = Route.routeEpoch,
      .sequence = sequence,
      .idempotencyKeyHigh = 0xAAU,
      .idempotencyKeyLow = sequence,
  };
}

ServerCorpseLootSnapshot snapshot(const std::uint64_t corpseRevision,
                                  const std::uint64_t inventoryRevision,
                                  const std::uint32_t quantity) {
  return {
      .header = header(corpseRevision),
      .corpse = Corpse,
      .corpseRevision = corpseRevision,
      .inventoryRevision = inventoryRevision,
      .snapshotId = corpseRevision,
      .stacks = {stack(quantity, corpseRevision)},
  };
}

void makeLootable(ServerCorpseLootPresentationState& state,
                  const std::uint64_t revision) {
  state.observeAvailability({
      .header = header(revision),
      .corpse = Corpse,
      .looter = {},
      .lootRevision = revision,
      .flags = ServerCorpseLootAvailable,
  });
  assert(!state.canOpen(Corpse));
  state.observeDeath(Corpse, true, revision);
  assert(state.replicatedDead(Corpse));
  assert(state.canOpen(Corpse));
}

void installOpenedSnapshot(ServerCorpseLootPresentationState& state,
                           const std::uint64_t corpseRevision,
                           const std::uint64_t inventoryRevision,
                           const std::uint32_t quantity,
                           const std::uint64_t commandSequence) {
  const auto open = state.openRequest(Corpse, inventoryRevision);
  assert(open.has_value());
  assert(state.markPending({
      .command = token(commandSequence),
      .kind = ServerCorpseLootPendingKind::Open,
      .corpse = Corpse,
      .expectedCorpseRevision = open->expectedCorpseRevision,
      .expectedInventoryRevision = open->expectedInventoryRevision,
  }));
  state.installSnapshot(
      snapshot(corpseRevision, inventoryRevision, quantity));
  state.complete({
      .command = token(commandSequence),
      .status = ClientMmoCommandCompletionStatus::Applied,
  });
  assert(state.ready());
  assert(!state.pending());
}

void testBootstrapDeathSeedAllowsAvailabilityGate() {
  ServerCorpseLootPresentationState state;
  state.seedReplicatedDeath(Corpse, true);
  assert(state.replicatedDead(Corpse));
  assert(!state.canOpen(Corpse));
  state.observeAvailability({
      .header = header(4U),
      .corpse = Corpse,
      .looter = {},
      .lootRevision = 4U,
      .flags = ServerCorpseLootAvailable,
  });
  assert(state.canOpen(Corpse));

  // A projection cannot activate UI ownership without an exact Open command.
  state.installSnapshot(snapshot(4U, 10U, 2U));
  assert(!state.active());

  state.observeDeath(Corpse, false, 1U);
  assert(!state.replicatedDead(Corpse));
  assert(!state.canOpen(Corpse));
}


void testAvailabilityLossClosesPendingOpen() {
  ServerCorpseLootPresentationState state;
  makeLootable(state, 12U);
  assert(state.markPending({
      .command = token(8U),
      .kind = ServerCorpseLootPendingKind::Open,
      .corpse = Corpse,
      .expectedCorpseRevision = 12U,
      .expectedInventoryRevision = 20U,
  }));
  state.observeAvailability({
      .header = header(13U),
      .corpse = Corpse,
      .looter = {},
      .lootRevision = 13U,
      .flags = 0U,
  });
  assert(!state.active());
  assert(!state.pending());
  assert(state.feedback() == ServerCorpseLootFeedback::Empty);

  state.complete({
      .command = token(8U),
      .status = ClientMmoCommandCompletionStatus::Applied,
  });
  assert(state.feedback() == ServerCorpseLootFeedback::Empty);
}

void testExactOpenAndNoOptimisticMutation() {
  ServerCorpseLootPresentationState state;
  makeLootable(state, 5U);

  const auto open = state.openRequest(Corpse, 10U);
  assert(open.has_value());
  assert(open->corpse == Corpse);
  assert(open->expectedCorpseRevision == 5U);
  assert(open->expectedInventoryRevision == 10U);
  assert(state.markPending({
      .command = token(1U),
      .kind = ServerCorpseLootPendingKind::Open,
      .corpse = Corpse,
      .expectedCorpseRevision = 5U,
      .expectedInventoryRevision = 10U,
  }));
  const auto closeWhileOpening = state.closeRequest();
  assert(closeWhileOpening.has_value());
  assert(closeWhileOpening->corpse == Corpse);
  assert(closeWhileOpening->expectedCorpseRevision == 5U);
  assert(closeWhileOpening->expectedInventoryRevision == 10U);

  state.complete({
      .command = token(1U),
      .status = ClientMmoCommandCompletionStatus::Applied,
  });
  assert(state.feedback() == ServerCorpseLootFeedback::Accepted);

  state.installSnapshot(snapshot(5U, 10U, 5U));
  assert(state.ready());
  assert(!state.pending());
  assert(state.stackAt(0U)->quantity == 5U);

  const auto take = state.takeRequest(Stack, 1U);
  assert(take.has_value());
  assert(take->expectedCorpseRevision == 5U);
  assert(take->expectedInventoryRevision == 10U);
  assert(state.markPending({
      .command = token(2U),
      .kind = ServerCorpseLootPendingKind::TakeStack,
      .corpse = Corpse,
      .stack = Stack,
      .amount = 1U,
      .expectedCorpseRevision = 5U,
      .expectedInventoryRevision = 10U,
  }));

  // Submission and even an applied receipt never mutate the projected stack.
  assert(state.stackAt(0U)->quantity == 5U);
  state.complete({
      .command = token(2U),
      .status = ClientMmoCommandCompletionStatus::Applied,
  });
  assert(state.stackAt(0U)->quantity == 5U);

  state.applyDelta({
      .header = header(6U),
      .corpse = Corpse,
      .kind = ServerCorpseLootStackDeltaKind::StackQuantityChanged,
      .stack = stack(4U, 6U),
      .corpseRevision = 6U,
      .inventoryRevision = 11U,
  });
  assert(state.stackAt(0U)->quantity == 4U);
  assert(!state.pending());
}

void testTypedFailuresPreserveStacks() {
  ServerCorpseLootPresentationState state;
  makeLootable(state, 20U);
  installOpenedSnapshot(state, 20U, 30U, 7U, 9U);

  const auto reject = [&](const std::uint64_t sequence,
                          const Mmo::ProtocolV2::CommandRejectionCode code,
                          const ServerCorpseLootFeedback expected) {
    const auto request = state.takeRequest(Stack, 1U);
    assert(request.has_value());
    assert(state.markPending({
        .command = token(sequence),
        .kind = ServerCorpseLootPendingKind::TakeStack,
        .corpse = Corpse,
        .stack = Stack,
        .amount = 1U,
        .expectedCorpseRevision = state.corpseRevision(),
        .expectedInventoryRevision = state.inventoryRevision(),
    }));
    state.complete({
        .command = token(sequence),
        .status = ClientMmoCommandCompletionStatus::Rejected,
        .rejectionCode = static_cast<std::uint16_t>(code),
    });
    assert(state.feedback() == expected);
    assert(state.stackAt(0U)->quantity == 7U);
  };

  reject(10U, Mmo::ProtocolV2::CommandRejectionCode::Busy,
         ServerCorpseLootFeedback::Busy);
  reject(11U, Mmo::ProtocolV2::CommandRejectionCode::ActionNotAllowed,
         ServerCorpseLootFeedback::AccessDenied);
  reject(12U, Mmo::ProtocolV2::CommandRejectionCode::OutOfRange,
         ServerCorpseLootFeedback::OutOfRange);
  reject(13U, Mmo::ProtocolV2::CommandRejectionCode::AdmissionCapacityExceeded,
         ServerCorpseLootFeedback::FullInventory);
  reject(14U, Mmo::ProtocolV2::CommandRejectionCode::TargetRevisionMismatch,
         ServerCorpseLootFeedback::StaleRevision);
  assert(!state.actionsEnabled());
  state.installSnapshot(snapshot(21U, 31U, 7U));
  assert(state.actionsEnabled());
  reject(15U, Mmo::ProtocolV2::CommandRejectionCode::ResourceNotFound,
         ServerCorpseLootFeedback::Empty);
}

void testProjectionBeforeCompletionDoesNotLeavePendingCommand() {
  ServerCorpseLootPresentationState state;
  makeLootable(state, 100U);
  assert(state.markPending({
      .command = token(20U),
      .kind = ServerCorpseLootPendingKind::Open,
      .corpse = Corpse,
      .expectedCorpseRevision = 100U,
      .expectedInventoryRevision = 110U,
  }));

  state.installSnapshot(snapshot(100U, 110U, 6U));
  assert(state.pending());
  state.complete({
      .command = token(20U),
      .status = ClientMmoCommandCompletionStatus::Applied,
  });
  assert(!state.pending());
  assert(state.feedback() == ServerCorpseLootFeedback::Ready);
  assert(state.stackAt(0U)->quantity == 6U);

  assert(state.markPending({
      .command = token(21U),
      .kind = ServerCorpseLootPendingKind::TakeStack,
      .corpse = Corpse,
      .stack = Stack,
      .amount = 1U,
      .expectedCorpseRevision = 100U,
      .expectedInventoryRevision = 110U,
  }));
  state.applyDelta({
      .header = header(101U),
      .corpse = Corpse,
      .kind = ServerCorpseLootStackDeltaKind::StackQuantityChanged,
      .stack = stack(5U, 101U),
      .corpseRevision = 101U,
      .inventoryRevision = 111U,
  });
  assert(state.pending());
  state.complete({
      .command = token(21U),
      .status = ClientMmoCommandCompletionStatus::Applied,
  });
  assert(!state.pending());
  assert(state.feedback() == ServerCorpseLootFeedback::Ready);
  assert(state.stackAt(0U)->quantity == 5U);
}

void testTakeAllAndTerminalClosure() {
  ServerCorpseLootPresentationState state;
  makeLootable(state, 40U);
  installOpenedSnapshot(state, 40U, 50U, 3U, 30U);

  const auto takeAll = state.takeAllRequest();
  assert(takeAll.has_value());
  assert(takeAll->corpse == Corpse);
  assert(takeAll->expectedCorpseRevision == 40U);
  assert(takeAll->expectedInventoryRevision == 50U);

  state.observeAvailability({
      .header = header(41U),
      .corpse = Corpse,
      .looter = {},
      .lootRevision = 41U,
      .flags = 0U,
  });
  assert(!state.active());
  assert(state.feedback() == ServerCorpseLootFeedback::Empty);

  // A precise terminal event may follow the availability update in the same
  // mailbox cut. It must refine the visible reason even though the page is
  // already locally closed.
  state.sessionClosed({
      .header = header(42U),
      .corpse = Corpse,
      .reason = ServerCorpseLootSessionCloseReason::Decayed,
      .corpseRevision = 41U,
      .inventoryRevision = 50U,
  });
  assert(!state.active());
  assert(state.feedback() == ServerCorpseLootFeedback::Decayed);

  // A stale duplicate cannot overwrite the newer terminal outcome.
  state.sessionClosed({
      .header = header(43U),
      .corpse = Corpse,
      .reason = ServerCorpseLootSessionCloseReason::Empty,
      .corpseRevision = 40U,
      .inventoryRevision = 49U,
  });
  assert(state.feedback() == ServerCorpseLootFeedback::Decayed);

  state.reset();
  makeLootable(state, 60U);
  installOpenedSnapshot(state, 60U, 70U, 2U, 31U);
  state.observeDespawn(Corpse);
  assert(!state.active());
  assert(state.feedback() == ServerCorpseLootFeedback::OutOfRange);

  state.reset();
  makeLootable(state, 80U);
  installOpenedSnapshot(state, 80U, 90U, 2U, 32U);
  state.reset(ServerCorpseLootFeedback::Disconnected);
  assert(!state.active());
  assert(state.feedback() == ServerCorpseLootFeedback::Disconnected);
}

} // namespace

int main() {
  testBootstrapDeathSeedAllowsAvailabilityGate();
  testAvailabilityLossClosesPendingOpen();
  testExactOpenAndNoOptimisticMutation();
  testTypedFailuresPreserveStacks();
  testProjectionBeforeCompletionDoesNotLeavePendingCommand();
  testTakeAllAndTerminalClosure();
}
