#pragma once

#include "mmo_npc_perception_policy.h"
#include "mmo_server_types.h"

#include <cstddef>
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

struct NpcPerceptionActionQueueInspectOptions final {
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string worldInstanceUuid;
  std::size_t maxPendingRows = 16;
};

struct NpcPerceptionActionQueueInspection final {
  std::size_t totalCount = 0;
  std::size_t pendingCount = 0;
  std::size_t duePendingCount = 0;
  std::size_t delayedRetryCount = 0;
  std::size_t claimedCount = 0;
  std::size_t appliedCount = 0;
  std::size_t failedCount = 0;
  std::size_t skippedCount = 0;
  std::size_t dispatchLogCount = 0;
  std::string pendingActionsJson = "[]";
};


struct NpcPerceptionActionDispatchContractInspectOptions final {
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string worldInstanceUuid;
  std::size_t maxInvalidRows = 16;
};

struct NpcPerceptionActionDispatchContractInspection final {
  std::size_t duePendingCount = 0;
  std::size_t knownActionKindCount = 0;
  std::size_t unknownActionKindCount = 0;
  std::size_t missingWorldInstanceUuidCount = 0;
  std::size_t missingTargetKeyCount = 0;
  std::size_t missingPayloadObjectCount = 0;
  std::size_t missingPayloadDecisionUuidCount = 0;
  std::size_t missingPayloadNpcEntityKeyCount = 0;
  std::size_t missingPayloadPerceptionKindCount = 0;
  std::size_t validShapeCount = 0;
  std::size_t liveDispatchImplementedCount = 0;
  std::size_t liveDispatchBlockedCount = 0;
  std::string invalidActionsJson = "[]";
};

struct ClaimNpcPerceptionActionOptions final {
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string workerId = "mmo_manual_action_queue_probe";
};

struct ClaimedNpcPerceptionAction final {
  bool claimed = false;
  std::string actionQueueUuid;
  std::string decisionUuid;
  std::string actionKind;
  std::string worldInstanceUuid;
  std::string sessionUuid;
  std::string characterUuid;
  std::string targetKey;
  std::string idempotencyKey;
  std::string requestPayloadJson = "{}";
};

struct SkipNpcPerceptionActionOptions final {
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string actionQueueUuid;
  std::string workerId = "mmo_manual_action_queue_probe";
  std::string reason = "manual_probe_skip";
};

struct SkippedNpcPerceptionAction final {
  std::string actionStatus;
};

[[nodiscard]] bool isSafeMysqlIdentifier(std::string_view value) noexcept;

[[nodiscard]] RecordedNpcPerceptionDecision recordNpcPerceptionDecision(
    const Server::MySqlTarget& target,
    const RecordNpcPerceptionDecisionOptions& options,
    const NpcPerception::DecisionCandidate& decision);

[[nodiscard]] NpcPerceptionActionQueueInspection inspectNpcPerceptionActionQueue(
    const Server::MySqlTarget& target,
    const NpcPerceptionActionQueueInspectOptions& options);

[[nodiscard]] NpcPerceptionActionDispatchContractInspection inspectNpcPerceptionActionDispatchContracts(
    const Server::MySqlTarget& target,
    const NpcPerceptionActionDispatchContractInspectOptions& options);

[[nodiscard]] ClaimedNpcPerceptionAction claimNextNpcPerceptionAction(
    const Server::MySqlTarget& target,
    const ClaimNpcPerceptionActionOptions& options);

[[nodiscard]] SkippedNpcPerceptionAction skipNpcPerceptionAction(
    const Server::MySqlTarget& target,
    const SkipNpcPerceptionActionOptions& options);

} // namespace Mmo::AiRuntime


