#include "game/mmoserverpresentationfacadeadapter.h"

#include <cassert>
#include <cstdint>
#include <utility>

namespace {

using namespace Mmo;
using namespace Mmo::ClientPresentation;
using namespace Mmo::ClientSandbox;

constexpr ClientRuntimeWorldInstanceHandle RuntimeWorld{
    .id = 11U,
    .generation = 2U,
};

constexpr ClientRuntimeEntityHandle RuntimeCorpse{
    .world = RuntimeWorld,
    .id = 91U,
    .generation = 4U,
};

constexpr ClientRuntimeReplicationMetadata runtimeMetadata(
    const std::uint64_t sequence) noexcept {
  return {
      .connectionId = 7U,
      .routeEpoch = 3U,
      .world = RuntimeWorld,
      .streamSequence = sequence,
      .serverTick = sequence,
      .aggregateRevision = sequence,
      .baseline = {.serverTick = 1U, .aggregateRevision = 1U},
  };
}

constexpr ProtocolV2::ServerReplicationHeader protocolHeader(
    const std::uint64_t sequence) noexcept {
  return {
      .protocolVersion = {},
      .connection = {.value = 7U},
      .routeEpoch = 3U,
      .world = {.id = {.value = 11U}, .generation = 2U},
      .streamSequence = sequence,
      .serverTick = sequence,
      .aggregateRevision = sequence,
      .baseline = {.serverTick = 1U, .aggregateRevision = 1U},
  };
}

constexpr ProtocolV2::EntityHandle protocolCorpse() noexcept {
  return {
      .world = {.id = {.value = 11U}, .generation = 2U},
      .id = {.value = 91U},
      .generation = 4U,
  };
}

constexpr ClientRuntimeReplicatedItemStackDescriptor runtimeStack(
    const std::uint32_t quantity,
    const std::uint64_t revision) noexcept {
  return {
      .stack = {.instanceId = 1'001U, .generation = 8U},
      .archetypeId = 41U,
      .presentationId = 52U,
      .presentationRevision = 3U,
      .quantity = quantity,
      .stackRevision = revision,
  };
}

void testFacadeMapsSelectiveLootAuthorityData() {
  ClientRuntimePresentationMailboxSnapshot source;
  source.livePresentationEvents.emplace_back(
      ProtocolV2::LootAvailabilityChanged{
          .header = protocolHeader(4U),
          .corpse = protocolCorpse(),
          .looter = {},
          .lootRevision = 5U,
          .flags = ProtocolV2::LootAvailable,
      });
  source.corpseLootSnapshots.push_back({
      .replication = runtimeMetadata(5U),
      .corpse = RuntimeCorpse,
      .corpseRevision = 5U,
      .inventoryRevision = 10U,
      .snapshotId = 77U,
      .stacks = {runtimeStack(6U, 5U)},
  });
  source.corpseLootDeltas.push_back({
      .replication = runtimeMetadata(6U),
      .corpse = RuntimeCorpse,
      .kind = ClientRuntimeCorpseLootStackDeltaKind::StackQuantityChanged,
      .stack = runtimeStack(5U, 6U),
      .corpseRevision = 6U,
      .inventoryRevision = 11U,
  });
  source.corpseLootClosed.push_back({
      .replication = runtimeMetadata(7U),
      .corpse = RuntimeCorpse,
      .reason = ClientRuntimeCorpseLootSessionCloseReason::CorpseDecayed,
      .corpseRevision = 7U,
      .inventoryRevision = 11U,
  });
  source.corpseLootResyncs.push_back({
      .corpse = RuntimeCorpse,
      .expectedCorpseRevision = 7U,
      .expectedInventoryRevision = 11U,
      .reason = ClientRuntimeCorpseLootResyncReason::RevisionGap,
      .submission = {
          .status = ClientRuntimeV2SubmitStatus::Accepted,
          .command = {
              .kind = ClientRuntimeV2CommandKind::SelectiveLoot,
              .routeEpoch = 3U,
              .sequence = 9U,
              .idempotencyKeyHigh = 1U,
              .idempotencyKeyLow = 2U,
          },
      },
  });

  auto mapped = mapClientRuntimePresentationMailbox(std::move(source));
  assert(mapped.rejectedRecords == 0U);
  assert(mapped.corpseLootAvailability.size() == 1U);
  assert(mapped.corpseLootSnapshots.size() == 1U);
  assert(mapped.corpseLootDeltas.size() == 1U);
  assert(mapped.corpseLootClosed.size() == 1U);
  assert(mapped.corpseLootResyncs.size() == 1U);

  const auto& availability = mapped.corpseLootAvailability.front();
  assert(availability.corpse.id == 91U);
  assert(availability.lootRevision == 5U);
  assert(availability.flags == ServerCorpseLootAvailable);

  const auto& snapshot = mapped.corpseLootSnapshots.front();
  assert(snapshot.corpseRevision == 5U);
  assert(snapshot.inventoryRevision == 10U);
  assert(snapshot.snapshotId == 77U);
  assert(snapshot.stacks.size() == 1U);
  assert(snapshot.stacks.front().handle.instanceId == 1'001U);
  assert(snapshot.stacks.front().quantity == 6U);

  const auto& delta = mapped.corpseLootDeltas.front();
  assert(delta.kind == ServerCorpseLootStackDeltaKind::StackQuantityChanged);
  assert(delta.corpseRevision == 6U);
  assert(delta.inventoryRevision == 11U);
  assert(delta.stack.quantity == 5U);

  const auto& closed = mapped.corpseLootClosed.front();
  assert(closed.reason == ServerCorpseLootSessionCloseReason::Decayed);
  assert(closed.corpseRevision == 7U);

  const auto& resync = mapped.corpseLootResyncs.front();
  assert(resync.valid());
  assert(resync.routeEpoch == 3U);
  assert(resync.reason == ServerCorpseLootResyncReason::RevisionGap);
  assert(resync.submissionStatus == ClientMmoSubmitStatus::Accepted);
}

} // namespace

int main() {
  testFacadeMapsSelectiveLootAuthorityData();
}
