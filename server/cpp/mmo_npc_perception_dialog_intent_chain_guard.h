#pragma once

#include "mmo_npc_perception_dialog_intent_terminal_plan.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentChainGuardOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_chain_guard.v1";
  std::string guardSource = "mmo_manual_action_dispatcher_probe";
  std::string guardMode = "guard_only_no_dispatch_no_db";
  bool requireTerminalPlan = true;
  bool requireAckFinalizationPlan = true;
  bool requireNackFinalizationPlan = true;
  bool requireTimeoutDeadLetterPlan = true;
  bool requireNoDbMutation = true;
  bool requireNoTimer = true;
  bool requireNoSocketReceive = true;
  bool requireNoSend = true;
  bool requireNoDialogUiAudio = true;
  bool requireNoMarkApplied = true;
};

struct NpcPerceptionDialogIntentChainGuard final {
  bool guarded = false;
  bool dbMutated = false;
  bool timerScheduled = false;
  bool timeoutObserved = false;
  bool socketReceiveExecuted = false;
  bool livePacketDecoded = false;
  bool clientAckObserved = false;
  bool clientNackObserved = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;
  bool actionMarkedApplied = false;
  bool actionMarkedFailed = false;
  bool deadLetterWritten = false;
  bool retryQueued = false;

  bool proofChainComplete = false;
  bool terminalPlanReady = false;
  bool receiveLoopIntegrationProofed = false;
  bool pendingAckSlotShapeReady = false;
  bool ackRouteShapeReady = false;
  bool nackRouteShapeReady = false;
  bool malformedRouteRejected = false;
  bool timeoutConfigured = false;
  bool terminalStatusDeferred = true;
  bool ackApplyFinalizationPlanned = false;
  bool nackRetryOrRejectPlanned = false;
  bool timeoutDeadLetterPlanned = false;
  bool noLiveSideEffects = true;
  bool futureDispatcherPrerequisitesReady = false;
  bool futureStoragePrerequisitesMissing = true;

  std::string status = "not_guarded";
  std::string contractVersion;
  std::string guardSource;
  std::string guardMode;
  std::string actionQueueUuid;
  std::string decisionUuid;
  std::string actionKind;
  std::string worldInstanceUuid;
  std::string sessionUuid;
  std::string characterUuid;
  std::string npcEntityKey;
  std::string targetKey;
  std::string perceptionKind;
  std::string idempotencyKey;
  std::string targetSessionUuid;
  std::string targetCharacterUuid;
  std::string ackCorrelationKey;
  std::string expectedAckIdempotencyKey;
  std::string receiveRouteKey;
  std::string nextRequiredApproval = "explicit_db_storage_migration_before_live_dispatch";

  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t ackTimeoutMs = 0;
  std::uint32_t maxRetryAttempts = 0;

  std::vector<std::string> completedStages;
  std::vector<std::string> blockedSideEffects;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentChainGuard(
    const NpcPerceptionDialogIntentTerminalPlan& terminalPlan) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentChainGuard buildNpcPerceptionDialogIntentChainGuard(
    const NpcPerceptionDialogIntentTerminalPlan& terminalPlan,
    const NpcPerceptionDialogIntentChainGuardOptions& options = {});

[[nodiscard]] std::string dialogIntentChainGuardJson(
    const NpcPerceptionDialogIntentChainGuard& guard);

} // namespace Mmo::AiRuntime
