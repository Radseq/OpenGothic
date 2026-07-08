#pragma once

#include "mmo_npc_perception_policy.h"
#include "mmo_server_types.h"

#include <string>
#include <string_view>

namespace Mmo::AiRuntime {

struct RecordNpcPerceptionDecisionOptions final {
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string worldInstanceUuid;
  std::string sessionUuid;
  std::string characterUuid;
};

struct RecordedNpcPerceptionDecision final {
  std::string decisionUuid;
  std::string decisionStatus;
  std::string actionQueueUuid;
};

[[nodiscard]] bool isSafeMysqlIdentifier(std::string_view value) noexcept;

[[nodiscard]] RecordedNpcPerceptionDecision recordNpcPerceptionDecision(
    const Server::MySqlTarget& target,
    const RecordNpcPerceptionDecisionOptions& options,
    const NpcPerception::DecisionCandidate& decision);

} // namespace Mmo::AiRuntime
