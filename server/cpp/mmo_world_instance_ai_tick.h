#pragma once

#include "mmo_ai_runtime_persistence.h"
#include "mmo_npc_perception_runtime_source.h"
#include "mmo_server_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Mmo::WorldInstanceAiTick {

struct WorldInstanceAiTickOptions final {
  std::filesystem::path runtimeReadModelPath;
  std::string contentRevisionKey;
  std::string worldInstanceKey;
  std::string worldName;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string perceptionKind = "PERC_ASSESSPLAYER";
  double maxDistance = 1500.0;
  std::uint64_t serverTick = 0;
  std::uint64_t cooldownTicks = 250;
  int priorityValue = 100;
  std::size_t maxNpcs = 32;
  std::size_t maxPlayers = 16;
  std::size_t maxRecords = 16;
  bool repairWeakNpcEntityKeys = true;
  bool includeWeakNpcIdentity = false;
  bool enqueueAction = true;
  bool dryRun = true;
};

struct RecordedDecision final {
  NpcPerception::DecisionCandidate decision;
  AiRuntime::RecordedNpcPerceptionDecision recorded;
};

struct WorldInstanceAiTickResult final {
  NpcPerceptionRuntime::RuntimeWorldInstance world;
  NpcPerceptionRuntime::RuntimeNpcIdentityStats npcIdentity;
  NpcPerception::AssessmentStats assessment;
  std::vector<NpcPerception::DecisionCandidate> decisions;
  std::vector<RecordedDecision> recorded;
  std::size_t recordLimit = 0;
  std::size_t skippedByRecordLimit = 0;
  bool dryRun = true;
};

[[nodiscard]] WorldInstanceAiTickResult runWorldInstanceAiTick(
    const Server::MySqlTarget& target,
    const WorldInstanceAiTickOptions& options);

} // namespace Mmo::WorldInstanceAiTick
