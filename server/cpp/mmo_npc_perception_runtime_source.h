#pragma once

#include "mmo_npc_perception_policy.h"
#include "mmo_server_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::NpcPerceptionRuntime {

struct RuntimeWorldInstance final {
  std::string worldInstanceUuid;
  std::string worldInstanceKey;
  std::string worldName;
  std::uint64_t serverTick = 0;
};

struct RuntimeActorQueryOptions final {
  std::string worldInstanceKey;
  std::string worldName;
  std::size_t maxNpcs = 32;
  std::size_t maxPlayers = 16;
  bool repairWeakNpcEntityKeys = true;
  bool includeWeakNpcIdentity = false;
};

struct RuntimeNpcIdentityStats final {
  std::size_t npcRowsRead = 0;
  std::size_t acceptedNpcs = 0;
  std::size_t repairedEntityKeys = 0;
  std::size_t skippedWeakEntityKeys = 0;
  std::size_t skippedMissingNpcInstance = 0;
};

struct RuntimeActorSnapshot final {
  RuntimeWorldInstance world;
  std::vector<NpcPerception::NpcActor> npcs;
  std::vector<NpcPerception::PlayerActor> players;
  RuntimeNpcIdentityStats npcIdentity;
};

[[nodiscard]] RuntimeWorldInstance resolveRuntimeWorldInstance(
    const Server::MySqlTarget& target,
    std::string_view worldInstanceKey,
    std::string_view worldName);

[[nodiscard]] RuntimeActorSnapshot loadRuntimeActors(
    const Server::MySqlTarget& target,
    const RuntimeActorQueryOptions& options);

} // namespace Mmo::NpcPerceptionRuntime


