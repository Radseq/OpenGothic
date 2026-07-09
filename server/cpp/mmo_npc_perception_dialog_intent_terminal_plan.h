#pragma once

#include "mmo_npc_perception_dialog_intent_receive_loop_integration.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentTerminalPlanOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_terminal_plan.v1";
  std::string planningSource = "mmo_manual_action_dispatcher_probe";
  std::string planningMode = "plan_only_no_timer_no_db";
  std::string ackApplyStatus = "apply_after_valid_client_ack_deferred";
  std::string nackStatus = "client_nack_retry_or_reject_deferred";
  std::string timeoutStatus = "client_ack_timeout_dead_letter_deferred";
  std::string deadLetterReason = "client_ack_timeout_without_live_observation";
  std::uint32_t maxRetryAttempts = 0;
  bool requireReceiveLoopIntegrationProofed = true;
  bool requireTimeoutConfigured = true;
  bool requirePendingAckSlot = true;
  bool requireTerminalStatusDeferred = true;
  bool requireNoTimer = true;
  bool requireNoSocketReceive = true;
  bool requireNoClientReceiptObserved = true;
  bool requireNoSend = true;
  bool requireNoDbMutation = true;
  bool requireNoMarkApplied = true;
};

struct NpcPerceptionDialogIntentTerminalPlan final {
  bool planned = false;
  bool dbMutated = false;
  bool timerScheduled = false;
  bool timeoutObserved = false;
  bool retryQueued = false;
  bool deadLetterWritten = false;
  bool actionMarkedApplied = false;
  bool actionMarkedFailed = false;
  bool socketReceiveExecuted = false;
  bool livePacketDecoded = false;
  bool clientAckObserved = false;
  bool clientNackObserved = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

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
  bool retryPolicyPlanned = false;
  bool deadLetterPolicyPlanned = false;
  bool idempotentApplyRequired = true;
  bool idempotentFailureRequired = true;
  bool wouldScheduleTimerIfEnabled = false;
  bool wouldMarkAppliedAfterAckIfEnabled = false;
  bool wouldRejectOrRetryAfterNackIfEnabled = false;
  bool wouldDeadLetterAfterTimeoutIfEnabled = false;
  bool wouldRecordTerminalReceiptIfEnabled = false;

  std::string status = "not_planned";
  std::string contractVersion;
  std::string planningSource;
  std::string planningMode;
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
  std::string ackApplyStatus;
  std::string nackStatus;
  std::string timeoutStatus;
  std::string deadLetterReason;
  std::string ackFinalizationDescription = "future_validated_ack_marks_action_applied_idempotently";
  std::string nackFinalizationDescription = "future_validated_nack_rejects_or_retries_without_apply";
  std::string timeoutFinalizationDescription = "future_timeout_moves_pending_ack_to_dead_letter_or_retry";

  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t ackTimeoutMs = 0;
  std::uint32_t maxRetryAttempts = 0;

  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentTerminalPlan(
    const NpcPerceptionDialogIntentReceiveLoopIntegrationProof& receiveLoopProof) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentTerminalPlan buildNpcPerceptionDialogIntentTerminalPlan(
    const NpcPerceptionDialogIntentReceiveLoopIntegrationProof& receiveLoopProof,
    const NpcPerceptionDialogIntentTerminalPlanOptions& options = {});

[[nodiscard]] std::string dialogIntentTerminalPlanJson(
    const NpcPerceptionDialogIntentTerminalPlan& plan);

} // namespace Mmo::AiRuntime
