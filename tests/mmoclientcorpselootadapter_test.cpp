#include "game/mmoclientadapterdetail.h"

#include <cassert>

namespace {

constexpr Mmo::ClientEntityHandle Corpse{
    .worldId = 17U,
    .worldGeneration = 3U,
    .id = 91U,
    .generation = 5U,
};

constexpr Mmo::ClientItemStackHandle Stack{
    .instanceId = 1'001U,
    .generation = 7U,
};

void testOpenCarriesExactAuthorityBasis() {
  const Mmo::ClientOpenCorpseLootRequest request{
      .corpse = Corpse,
      .expectedCorpseRevision = 21U,
      .expectedInventoryRevision = 34U,
  };
  const auto runtime =
      Mmo::ClientAdapterDetail::makeProtocolV2OpenCorpseLootRequest(request);
  assert(runtime.has_value());
  assert(runtime->corpse.world.id == Corpse.worldId);
  assert(runtime->corpse.world.generation == Corpse.worldGeneration);
  assert(runtime->corpse.id == Corpse.id);
  assert(runtime->corpse.generation == Corpse.generation);
  assert(runtime->expectedCorpseRevision == 21U);
  assert(runtime->expectedInventoryRevision == 34U);
}

void testTakeStackCarriesExactHandleAmountAndRevisions() {
  const Mmo::ClientTakeCorpseLootStackRequest request{
      .corpse = Corpse,
      .stack = Stack,
      .amount = 13U,
      .expectedCorpseRevision = 55U,
      .expectedInventoryRevision = 89U,
  };
  const auto runtime =
      Mmo::ClientAdapterDetail::makeProtocolV2TakeCorpseLootStackRequest(
          request);
  assert(runtime.has_value());
  assert(runtime->stack.instanceId == Stack.instanceId);
  assert(runtime->stack.generation == Stack.generation);
  assert(runtime->amount == 13U);
  assert(runtime->expectedCorpseRevision == 55U);
  assert(runtime->expectedInventoryRevision == 89U);
}

void testMalformedRequestsFailClosed() {
  auto invalidTake = Mmo::ClientTakeCorpseLootStackRequest{
      .corpse = Corpse,
      .stack = Stack,
      .amount = 0U,
      .expectedCorpseRevision = 1U,
      .expectedInventoryRevision = 1U,
  };
  assert(!Mmo::ClientAdapterDetail::makeProtocolV2TakeCorpseLootStackRequest(
              invalidTake)
              .has_value());

  auto invalidClose = Mmo::ClientCloseCorpseLootRequest{
      .corpse = Corpse,
      .expectedCorpseRevision = 0U,
      .expectedInventoryRevision = 1U,
  };
  assert(!Mmo::ClientAdapterDetail::makeProtocolV2CloseCorpseLootRequest(
              invalidClose)
              .has_value());
}

} // namespace

int main() {
  testOpenCarriesExactAuthorityBasis();
  testTakeStackCarriesExactHandleAmountAndRevisions();
  testMalformedRequestsFailClosed();
}
