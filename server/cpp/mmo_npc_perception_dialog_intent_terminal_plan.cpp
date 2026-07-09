#include "mmo_npc_perception_dialog_intent_terminal_plan.h"

#include <string>
#include <string_view>
#include <utility>

namespace Mmo::AiRuntime {
namespace {

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(const unsigned char c : text) {
    switch(c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if(c < 0x20U) {
          static constexpr char Hex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(Hex[(c >> 4U) & 0xFU]);
          out.push_back(Hex[c & 0xFU]);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

void appendComma(std::string& out) {
  if(!out.empty() && out.back() != '{' && out.back() != '[') {
    out.push_back(',');
  }
}

void appendJsonField(std::string& out, std::string_view key, std::string_view value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += jsonEscape(value);
}

void appendJsonBoolField(std::string& out, std::string_view key, bool value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += value ? "true" : "false";
}

void appendJsonCountField(std::string& out, std::string_view key, std::uint64_t value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void addIssue(NpcPerceptionDialogIntentTerminalPlan& plan, std::string issue) {
  plan.issues.push_back(std::move(issue));
}

[[nodiscard]] bool receiveLoopProofSafe(
    const NpcPerceptionDialogIntentReceiveLoopIntegrationProof& proof) noexcept {
  return proof.proofed && proof.receiptPreviewBuilt && proof.parserReady && proof.validatorReady &&
         proof.syntheticAckReceiptValidated && proof.syntheticNackReceiptValidated &&
         proof.syntheticMalformedReceiptRejected && proof.ackCorrelationReady && proof.timeoutConfigured &&
         proof.targetSessionReady && proof.targetCharacterReady && proof.expectedPacketKindsRegistered &&
         proof.ackRouteShapeReady && proof.nackRouteShapeReady && proof.malformedRouteRejected &&
         proof.pendingAckSlotShapeReady && proof.terminalStatusDeferred && !proof.dbMutated &&
         !proof.receiveLoopEntered && !proof.socketReceiveExecuted && !proof.livePacketDecoded &&
         !proof.clientAckObserved && !proof.clientNackObserved && !proof.sendExecuted &&
         !proof.packetFanoutExecuted && !proof.dialogUiExecuted && !proof.audioExecuted &&
         !proof.markAppliedExecuted;
}

void appendIssuesJson(std::string& out, const std::vector<std::string>& issues) {
  out.push_back('[');
  for(std::size_t i = 0; i < issues.size(); ++i) {
    if(i != 0U) {
      out.push_back(',');
    }
    out += jsonEscape(issues[i]);
  }
  out.push_back(']');
}

} // namespace

bool supportsNpcPerceptionDialogIntentTerminalPlan(
    const NpcPerceptionDialogIntentReceiveLoopIntegrationProof& receiveLoopProof) noexcept {
  return receiveLoopProofSafe(receiveLoopProof) && !receiveLoopProof.actionQueueUuid.empty() &&
         !receiveLoopProof.decisionUuid.empty() && !receiveLoopProof.targetSessionUuid.empty() &&
         !receiveLoopProof.targetCharacterUuid.empty() && !receiveLoopProof.ackCorrelationKey.empty() &&
         !receiveLoopProof.expectedAckIdempotencyKey.empty() && !receiveLoopProof.receiveRouteKey.empty();
}

NpcPerceptionDialogIntentTerminalPlan buildNpcPerceptionDialogIntentTerminalPlan(
    const NpcPerceptionDialogIntentReceiveLoopIntegrationProof& receiveLoopProof,
    const NpcPerceptionDialogIntentTerminalPlanOptions& options) {
  NpcPerceptionDialogIntentTerminalPlan out;
  out.contractVersion = options.contractVersion;
  out.planningSource = options.planningSource;
  out.planningMode = options.planningMode;
  out.actionQueueUuid = receiveLoopProof.actionQueueUuid;
  out.decisionUuid = receiveLoopProof.decisionUuid;
  out.actionKind = receiveLoopProof.actionKind;
  out.worldInstanceUuid = receiveLoopProof.worldInstanceUuid;
  out.sessionUuid = receiveLoopProof.sessionUuid;
  out.characterUuid = receiveLoopProof.characterUuid;
  out.npcEntityKey = receiveLoopProof.npcEntityKey;
  out.targetKey = receiveLoopProof.targetKey;
  out.perceptionKind = receiveLoopProof.perceptionKind;
  out.idempotencyKey = receiveLoopProof.idempotencyKey;
  out.targetSessionUuid = receiveLoopProof.targetSessionUuid;
  out.targetCharacterUuid = receiveLoopProof.targetCharacterUuid;
  out.ackCorrelationKey = receiveLoopProof.ackCorrelationKey;
  out.expectedAckIdempotencyKey = receiveLoopProof.expectedAckIdempotencyKey;
  out.receiveRouteKey = receiveLoopProof.receiveRouteKey;
  out.ackApplyStatus = options.ackApplyStatus;
  out.nackStatus = options.nackStatus;
  out.timeoutStatus = options.timeoutStatus;
  out.deadLetterReason = options.deadLetterReason;
  out.packetSequence = receiveLoopProof.packetSequence;
  out.localSequence = receiveLoopProof.localSequence;
  out.ackTimeoutMs = receiveLoopProof.ackTimeoutMs;
  out.maxRetryAttempts = options.maxRetryAttempts;

  out.dbMutated = false;
  out.timerScheduled = false;
  out.timeoutObserved = false;
  out.retryQueued = false;
  out.deadLetterWritten = false;
  out.actionMarkedApplied = false;
  out.actionMarkedFailed = false;
  out.socketReceiveExecuted = false;
  out.livePacketDecoded = false;
  out.clientAckObserved = false;
  out.clientNackObserved = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;

  out.receiveLoopIntegrationProofed = receiveLoopProof.proofed;
  out.pendingAckSlotShapeReady = receiveLoopProof.pendingAckSlotShapeReady;
  out.ackRouteShapeReady = receiveLoopProof.ackRouteShapeReady;
  out.nackRouteShapeReady = receiveLoopProof.nackRouteShapeReady;
  out.malformedRouteRejected = receiveLoopProof.malformedRouteRejected;
  out.timeoutConfigured = receiveLoopProof.timeoutConfigured && receiveLoopProof.ackTimeoutMs > 0U;
  out.terminalStatusDeferred = receiveLoopProof.terminalStatusDeferred;
  out.ackApplyFinalizationPlanned = receiveLoopProof.wouldMarkAppliedAfterAckIfEnabled &&
                                    !options.ackApplyStatus.empty();
  out.nackRetryOrRejectPlanned = receiveLoopProof.wouldRejectOrRetryAfterNackIfEnabled &&
                                 !options.nackStatus.empty();
  out.retryPolicyPlanned = options.maxRetryAttempts > 0U;
  out.deadLetterPolicyPlanned = !options.timeoutStatus.empty() && !options.deadLetterReason.empty();
  out.timeoutDeadLetterPlanned = out.timeoutConfigured && out.pendingAckSlotShapeReady &&
                                 out.deadLetterPolicyPlanned;
  out.idempotentApplyRequired = true;
  out.idempotentFailureRequired = true;
  out.wouldScheduleTimerIfEnabled = out.timeoutConfigured && out.pendingAckSlotShapeReady;
  out.wouldMarkAppliedAfterAckIfEnabled = out.ackApplyFinalizationPlanned;
  out.wouldRejectOrRetryAfterNackIfEnabled = out.nackRetryOrRejectPlanned;
  out.wouldDeadLetterAfterTimeoutIfEnabled = out.timeoutDeadLetterPlanned;
  out.wouldRecordTerminalReceiptIfEnabled = out.ackApplyFinalizationPlanned ||
                                            out.nackRetryOrRejectPlanned ||
                                            out.timeoutDeadLetterPlanned;

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.planningSource.empty()) {
    addIssue(out, "missing_planning_source");
  }
  if(options.planningMode != "plan_only_no_timer_no_db") {
    addIssue(out, "planning_mode_must_remain_plan_only_no_timer_no_db");
  }
  if(options.requireReceiveLoopIntegrationProofed && !receiveLoopProof.proofed) {
    addIssue(out, "receive_loop_integration_not_proofed");
  }
  if(!receiveLoopProofSafe(receiveLoopProof)) {
    addIssue(out, "receive_loop_proof_not_safe_for_terminal_plan");
  }
  if(options.requirePendingAckSlot && !out.pendingAckSlotShapeReady) {
    addIssue(out, "pending_ack_slot_shape_not_ready");
  }
  if(options.requireTimeoutConfigured && !out.timeoutConfigured) {
    addIssue(out, "ack_timeout_not_configured");
  }
  if(options.requireTerminalStatusDeferred && !out.terminalStatusDeferred) {
    addIssue(out, "terminal_status_not_deferred");
  }
  if(!out.ackApplyFinalizationPlanned) {
    addIssue(out, "ack_apply_finalization_not_planned");
  }
  if(!out.nackRetryOrRejectPlanned) {
    addIssue(out, "nack_retry_or_reject_not_planned");
  }
  if(!out.timeoutDeadLetterPlanned) {
    addIssue(out, "timeout_dead_letter_not_planned");
  }
  if(options.requireNoTimer && (out.timerScheduled || out.timeoutObserved || out.retryQueued)) {
    addIssue(out, "timer_timeout_or_retry_executed");
  }
  if(options.requireNoSocketReceive && (out.socketReceiveExecuted || out.livePacketDecoded)) {
    addIssue(out, "socket_receive_or_live_decode_executed");
  }
  if(options.requireNoClientReceiptObserved && (out.clientAckObserved || out.clientNackObserved)) {
    addIssue(out, "client_ack_or_nack_observed");
  }
  if(options.requireNoSend && (out.sendExecuted || out.packetFanoutExecuted)) {
    addIssue(out, "send_or_fanout_executed");
  }
  if(options.requireNoDbMutation && (out.dbMutated || out.deadLetterWritten || out.actionMarkedFailed)) {
    addIssue(out, "db_mutated_or_dead_letter_written");
  }
  if(options.requireNoMarkApplied && (out.markAppliedExecuted || out.actionMarkedApplied)) {
    addIssue(out, "mark_applied_executed");
  }

  out.planned = out.issues.empty();
  out.status = out.planned ? "terminal_plan_ready_no_timer_no_db" : "terminal_plan_rejected";
  return out;
}

std::string dialogIntentTerminalPlanJson(const NpcPerceptionDialogIntentTerminalPlan& plan) {
  std::string out;
  out.reserve(4096);
  out.push_back('{');
  appendJsonBoolField(out, "planned", plan.planned);
  appendComma(out);
  appendJsonField(out, "status", plan.status);
  appendComma(out);
  appendJsonField(out, "contract_version", plan.contractVersion);
  appendComma(out);
  appendJsonField(out, "planning_source", plan.planningSource);
  appendComma(out);
  appendJsonField(out, "planning_mode", plan.planningMode);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", plan.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "timer_scheduled", plan.timerScheduled);
  appendComma(out);
  appendJsonBoolField(out, "timeout_observed", plan.timeoutObserved);
  appendComma(out);
  appendJsonBoolField(out, "retry_queued", plan.retryQueued);
  appendComma(out);
  appendJsonBoolField(out, "dead_letter_written", plan.deadLetterWritten);
  appendComma(out);
  appendJsonBoolField(out, "action_marked_applied", plan.actionMarkedApplied);
  appendComma(out);
  appendJsonBoolField(out, "action_marked_failed", plan.actionMarkedFailed);
  appendComma(out);
  appendJsonBoolField(out, "socket_receive_executed", plan.socketReceiveExecuted);
  appendComma(out);
  appendJsonBoolField(out, "live_packet_decoded", plan.livePacketDecoded);
  appendComma(out);
  appendJsonBoolField(out, "client_ack_observed", plan.clientAckObserved);
  appendComma(out);
  appendJsonBoolField(out, "client_nack_observed", plan.clientNackObserved);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", plan.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", plan.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", plan.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", plan.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", plan.markAppliedExecuted);
  appendComma(out);
  appendJsonBoolField(out, "receive_loop_integration_proofed", plan.receiveLoopIntegrationProofed);
  appendComma(out);
  appendJsonBoolField(out, "pending_ack_slot_shape_ready", plan.pendingAckSlotShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "ack_route_shape_ready", plan.ackRouteShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "nack_route_shape_ready", plan.nackRouteShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "malformed_route_rejected", plan.malformedRouteRejected);
  appendComma(out);
  appendJsonBoolField(out, "timeout_configured", plan.timeoutConfigured);
  appendComma(out);
  appendJsonBoolField(out, "terminal_status_deferred", plan.terminalStatusDeferred);
  appendComma(out);
  appendJsonBoolField(out, "ack_apply_finalization_planned", plan.ackApplyFinalizationPlanned);
  appendComma(out);
  appendJsonBoolField(out, "nack_retry_or_reject_planned", plan.nackRetryOrRejectPlanned);
  appendComma(out);
  appendJsonBoolField(out, "timeout_dead_letter_planned", plan.timeoutDeadLetterPlanned);
  appendComma(out);
  appendJsonBoolField(out, "retry_policy_planned", plan.retryPolicyPlanned);
  appendComma(out);
  appendJsonBoolField(out, "dead_letter_policy_planned", plan.deadLetterPolicyPlanned);
  appendComma(out);
  appendJsonBoolField(out, "idempotent_apply_required", plan.idempotentApplyRequired);
  appendComma(out);
  appendJsonBoolField(out, "idempotent_failure_required", plan.idempotentFailureRequired);
  appendComma(out);
  appendJsonBoolField(out, "would_schedule_timer_if_enabled", plan.wouldScheduleTimerIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_mark_applied_after_ack_if_enabled", plan.wouldMarkAppliedAfterAckIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_reject_or_retry_after_nack_if_enabled", plan.wouldRejectOrRetryAfterNackIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_dead_letter_after_timeout_if_enabled", plan.wouldDeadLetterAfterTimeoutIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_record_terminal_receipt_if_enabled", plan.wouldRecordTerminalReceiptIfEnabled);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", plan.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", plan.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", plan.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", plan.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", plan.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", plan.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", plan.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", plan.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", plan.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", plan.idempotencyKey);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", plan.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", plan.targetCharacterUuid);
  appendComma(out);
  appendJsonField(out, "ack_correlation_key", plan.ackCorrelationKey);
  appendComma(out);
  appendJsonField(out, "expected_ack_idempotency_key", plan.expectedAckIdempotencyKey);
  appendComma(out);
  appendJsonField(out, "receive_route_key", plan.receiveRouteKey);
  appendComma(out);
  appendJsonField(out, "ack_apply_status", plan.ackApplyStatus);
  appendComma(out);
  appendJsonField(out, "nack_status", plan.nackStatus);
  appendComma(out);
  appendJsonField(out, "timeout_status", plan.timeoutStatus);
  appendComma(out);
  appendJsonField(out, "dead_letter_reason", plan.deadLetterReason);
  appendComma(out);
  appendJsonField(out, "ack_finalization_description", plan.ackFinalizationDescription);
  appendComma(out);
  appendJsonField(out, "nack_finalization_description", plan.nackFinalizationDescription);
  appendComma(out);
  appendJsonField(out, "timeout_finalization_description", plan.timeoutFinalizationDescription);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", plan.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", plan.localSequence);
  appendComma(out);
  appendJsonCountField(out, "ack_timeout_ms", plan.ackTimeoutMs);
  appendComma(out);
  appendJsonCountField(out, "max_retry_attempts", plan.maxRetryAttempts);
  appendComma(out);
  appendJsonCountField(out, "issue_count", plan.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out.push_back(':');
  appendIssuesJson(out, plan.issues);
  out.push_back('}');
  return out;
}

} // namespace Mmo::AiRuntime
