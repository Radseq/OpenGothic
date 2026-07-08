#pragma once

#include "mmo_world_instance_content_cache.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::NpcPerception {

struct Vec3 final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct NpcActor final {
  std::string entityKey;
  std::string npcInstance;
  Vec3 position;
  bool active = true;
};

struct PlayerActor final {
  std::string targetKey;
  std::string characterKey;
  Vec3 position;
  bool active = true;
  std::string sessionUuid;
  std::string characterUuid;
};

struct AssessmentOptions final {
  std::string perceptionKind = "PERC_ASSESSPLAYER";
  double maxDistance = 1500.0;
  std::uint64_t serverTick = 0;
  std::uint64_t cooldownTicks = 250;
  int priorityValue = 100;
  bool enqueueAction = true;
};

struct DecisionCandidate final {
  std::string npcEntityKey;
  std::string npcInstance;
  std::string targetKey;
  std::string characterKey;
  std::string sessionUuid;
  std::string characterUuid;
  std::string contentRevisionKey;
  std::string worldInstanceKey;
  std::string worldName;
  std::string ruleKey;
  std::string perceptionKind;
  std::string functionSymbol;
  std::string decisionKind;
  std::string idempotencyKey;
  double distance = 0.0;
  std::uint64_t serverTick = 0;
  std::uint64_t cooldownTicks = 0;
  int priorityValue = 100;
  bool enqueueAction = false;
};

struct AssessmentStats final {
  std::size_t npcActors = 0;
  std::size_t playerActors = 0;
  std::size_t evaluatedPairs = 0;
  std::size_t inactivePairsSkipped = 0;
  std::size_t distancePairsSkipped = 0;
  std::size_t missingPerceptionBindingPairs = 0;
  std::size_t decisions = 0;
};

struct AssessmentResult final {
  AssessmentStats stats;
  std::vector<DecisionCandidate> decisions;
};

[[nodiscard]] double distanceSquared(Vec3 lhs, Vec3 rhs) noexcept;
[[nodiscard]] std::string decisionKindForPerception(std::string_view perceptionKind);
[[nodiscard]] AssessmentResult assessNpcPerception(
    const WorldInstanceContent::WorldInstanceContentCache& cache,
    const AssessmentOptions& options,
    const std::vector<NpcActor>& npcs,
    const std::vector<PlayerActor>& players);
[[nodiscard]] std::string decisionPayloadJson(const DecisionCandidate& decision);

} // namespace Mmo::NpcPerception
