#include "mmo_npc_perception_dialog_intent_chain_guard.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Mmo::AiRuntime {
namespace {

constexpr std::array<std::string_view, 14> ChainStages = {
    "step236_dispatch_contract",
    "step237_typed_effect_descriptor",
    "step238_dialog_intent_preview",
    "step239_diagnostic_packet_contract",
    "step240_diagnostic_binary_encoding",
    "step241_durable_evidence_jsonl",
    "step242_client_fanout_plan",
    "step243_send_boundary",
    "step244_sender_adapter_proof",
    "step245_endpoint_resolution_proof",
    "step246_client_ack_contract",
    "step247_client_ack_receipt_preview",
    "step248_receive_loop_integration",
    "step249_terminal_plan",
};

constexpr std::array<std::string_view, 12> BlockedSideEffects = {
    "db_mutation",
    "timer_scheduling",
    "timeout_observation",
    "retry_queueing",
    "dead_letter_write",
    "socket_receive",
    "live_packet_decode",
    "udp_send",
    "packet_fanout",
    "dialog_ui",
    "audio",
    "mark_applied",
};

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

void appendStringArrayJson(std::string& out, const std::vector<std::string>& values) {
  out.push_back('[');
  for(std::size_t i = 0; i < values.size(); ++i) {
    if(i != 0U) {
      out.push_back(',');
    }
    out += jsonEscape(values[i]);
  }
  out.push_back(']');
}

void addIssue(NpcPerceptionDialogIntentChainGuard& guard, std::string issue) {
  guard.issues.push_back(std::move(issue));
}

[[nodiscard]] bool hasRequiredIdentity(const NpcPerceptionDialogIntentTerminalPlan& plan) noexcept {
  return !plan.actionQueueUuid.empty() && !plan.decisionUuid.empty() && !plan.actionKind.empty() &&
         !plan.worldInstanceUuid.empty() && !plan.targetSessionUuid.empty() &&
         !plan.targetCharacterUuid.empty() && !plan.ackCorrelationKey.empty() &&
         !plan.expectedAckIdempotencyKey.empty() && !plan.receiveRouteKey.empty();
}

[[nodiscard]] bool noTerminalPlanSideEffects(const NpcPerceptionDialogIntentTerminalPlan& plan) noexcept {
  return !plan.dbMutated && !plan.timerScheduled && !plan.timeoutObserved && !plan.retryQueued &&
         !plan.deadLetterWritten && !plan.actionMarkedApplied && !plan.actionMarkedFailed &&
         !plan.socketReceiveExecuted && !plan.livePacketDecoded && !plan.clientAckObserved &&
         !plan.clientNackObserved && !plan.sendExecuted && !plan.packetFanoutExecuted &&
         !plan.dialogUiExecuted && !plan.audioExecuted && !plan.markAppliedExecuted;
}

[[nodiscard]] bool terminalPlanComplete(const NpcPerceptionDialogIntentTerminalPlan& plan) noexcept {
  return plan.planned && plan.receiveLoopIntegrationProofed && plan.pendingAckSlotShapeReady &&
         plan.ackRouteShapeReady && plan.nackRouteShapeReady && plan.malformedRouteRejected &&
         plan.timeoutConfigured && plan.terminalStatusDeferred && plan.ackApplyFinalizationPlanned &&
         plan.nackRetryOrRejectPlanned && plan.timeoutDeadLetterPlanned && plan.idempotentApplyRequired &&
         plan.idempotentFailureRequired && hasRequiredIdentity(plan) && noTerminalPlanSideEffects(plan);
}

void fillCompletedStages(NpcPerceptionDialogIntentChainGuard& guard, bool complete) {
  guard.completedStages.clear();
  if(!complete) {
    return;
  }
  guard.completedStages.reserve(ChainStages.size());
  for(const std::string_view stage : ChainStages) {
    guard.completedStages.emplace_back(stage);
  }
}

void fillBlockedSideEffects(NpcPerceptionDialogIntentChainGuard& guard) {
  guard.blockedSideEffects.clear();
  guard.blockedSideEffects.reserve(BlockedSideEffects.size());
  for(const std::string_view blocked : BlockedSideEffects) {
    guard.blockedSideEffects.emplace_back(blocked);
  }
}

} // namespace

bool supportsNpcPerceptionDialogIntentChainGuard(
    const NpcPerceptionDialogIntentTerminalPlan& terminalPlan) noexcept {
  return terminalPlanComplete(terminalPlan);
}

NpcPerceptionDialogIntentChainGuard buildNpcPerceptionDialogIntentChainGuard(
    const NpcPerceptionDialogIntentTerminalPlan& terminalPlan,
    const NpcPerceptionDialogIntentChainGuardOptions& options) {
  NpcPerceptionDialogIntentChainGuard out;
  out.contractVersion = options.contractVersion;
  out.guardSource = options.guardSource;
  out.guardMode = options.guardMode;
  out.actionQueueUuid = terminalPlan.actionQueueUuid;
  out.decisionUuid = terminalPlan.decisionUuid;
  out.actionKind = terminalPlan.actionKind;
  out.worldInstanceUuid = terminalPlan.worldInstanceUuid;
  out.sessionUuid = terminalPlan.sessionUuid;
  out.characterUuid = terminalPlan.characterUuid;
  out.npcEntityKey = terminalPlan.npcEntityKey;
  out.targetKey = terminalPlan.targetKey;
  out.perceptionKind = terminalPlan.perceptionKind;
  out.idempotencyKey = terminalPlan.idempotencyKey;
  out.targetSessionUuid = terminalPlan.targetSessionUuid;
  out.targetCharacterUuid = terminalPlan.targetCharacterUuid;
  out.ackCorrelationKey = terminalPlan.ackCorrelationKey;
  out.expectedAckIdempotencyKey = terminalPlan.expectedAckIdempotencyKey;
  out.receiveRouteKey = terminalPlan.receiveRouteKey;
  out.packetSequence = terminalPlan.packetSequence;
  out.localSequence = terminalPlan.localSequence;
  out.ackTimeoutMs = terminalPlan.ackTimeoutMs;
  out.maxRetryAttempts = terminalPlan.maxRetryAttempts;

  out.dbMutated = terminalPlan.dbMutated;
  out.timerScheduled = terminalPlan.timerScheduled;
  out.timeoutObserved = terminalPlan.timeoutObserved;
  out.retryQueued = terminalPlan.retryQueued;
  out.deadLetterWritten = terminalPlan.deadLetterWritten;
  out.actionMarkedApplied = terminalPlan.actionMarkedApplied;
  out.actionMarkedFailed = terminalPlan.actionMarkedFailed;
  out.socketReceiveExecuted = terminalPlan.socketReceiveExecuted;
  out.livePacketDecoded = terminalPlan.livePacketDecoded;
  out.clientAckObserved = terminalPlan.clientAckObserved;
  out.clientNackObserved = terminalPlan.clientNackObserved;
  out.sendExecuted = terminalPlan.sendExecuted;
  out.packetFanoutExecuted = terminalPlan.packetFanoutExecuted;
  out.dialogUiExecuted = terminalPlan.dialogUiExecuted;
  out.audioExecuted = terminalPlan.audioExecuted;
  out.markAppliedExecuted = terminalPlan.markAppliedExecuted;

  out.terminalPlanReady = terminalPlan.planned;
  out.receiveLoopIntegrationProofed = terminalPlan.receiveLoopIntegrationProofed;
  out.pendingAckSlotShapeReady = terminalPlan.pendingAckSlotShapeReady;
  out.ackRouteShapeReady = terminalPlan.ackRouteShapeReady;
  out.nackRouteShapeReady = terminalPlan.nackRouteShapeReady;
  out.malformedRouteRejected = terminalPlan.malformedRouteRejected;
  out.timeoutConfigured = terminalPlan.timeoutConfigured;
  out.terminalStatusDeferred = terminalPlan.terminalStatusDeferred;
  out.ackApplyFinalizationPlanned = terminalPlan.ackApplyFinalizationPlanned;
  out.nackRetryOrRejectPlanned = terminalPlan.nackRetryOrRejectPlanned;
  out.timeoutDeadLetterPlanned = terminalPlan.timeoutDeadLetterPlanned;
  out.noLiveSideEffects = noTerminalPlanSideEffects(terminalPlan);
  out.proofChainComplete = terminalPlanComplete(terminalPlan);
  out.futureDispatcherPrerequisitesReady = out.proofChainComplete;
  out.futureStoragePrerequisitesMissing = true;
  fillCompletedStages(out, out.proofChainComplete);
  fillBlockedSideEffects(out);

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.guardSource.empty()) {
    addIssue(out, "missing_guard_source");
  }
  if(options.guardMode != "guard_only_no_dispatch_no_db") {
    addIssue(out, "guard_mode_must_remain_guard_only_no_dispatch_no_db");
  }
  if(options.requireTerminalPlan && !terminalPlan.planned) {
    addIssue(out, "terminal_plan_not_ready");
  }
  if(!hasRequiredIdentity(terminalPlan)) {
    addIssue(out, "terminal_plan_identity_or_correlation_incomplete");
  }
  if(!terminalPlan.receiveLoopIntegrationProofed) {
    addIssue(out, "receive_loop_integration_not_proofed");
  }
  if(!terminalPlan.pendingAckSlotShapeReady) {
    addIssue(out, "pending_ack_slot_shape_not_ready");
  }
  if(!terminalPlan.ackRouteShapeReady || !terminalPlan.nackRouteShapeReady || !terminalPlan.malformedRouteRejected) {
    addIssue(out, "ack_nack_route_shapes_not_ready");
  }
  if(!terminalPlan.timeoutConfigured) {
    addIssue(out, "timeout_not_configured");
  }
  if(!terminalPlan.terminalStatusDeferred) {
    addIssue(out, "terminal_status_not_deferred");
  }
  if(options.requireAckFinalizationPlan && !terminalPlan.ackApplyFinalizationPlanned) {
    addIssue(out, "ack_apply_finalization_not_planned");
  }
  if(options.requireNackFinalizationPlan && !terminalPlan.nackRetryOrRejectPlanned) {
    addIssue(out, "nack_retry_or_reject_not_planned");
  }
  if(options.requireTimeoutDeadLetterPlan && !terminalPlan.timeoutDeadLetterPlanned) {
    addIssue(out, "timeout_dead_letter_not_planned");
  }
  if(options.requireNoDbMutation && (out.dbMutated || out.deadLetterWritten || out.actionMarkedFailed)) {
    addIssue(out, "db_mutated_or_terminal_db_effect_written");
  }
  if(options.requireNoTimer && (out.timerScheduled || out.timeoutObserved || out.retryQueued)) {
    addIssue(out, "timer_timeout_or_retry_executed");
  }
  if(options.requireNoSocketReceive && (out.socketReceiveExecuted || out.livePacketDecoded ||
                                        out.clientAckObserved || out.clientNackObserved)) {
    addIssue(out, "socket_receive_or_live_ack_observation_executed");
  }
  if(options.requireNoSend && (out.sendExecuted || out.packetFanoutExecuted)) {
    addIssue(out, "send_or_packet_fanout_executed");
  }
  if(options.requireNoDialogUiAudio && (out.dialogUiExecuted || out.audioExecuted)) {
    addIssue(out, "dialog_ui_or_audio_executed");
  }
  if(options.requireNoMarkApplied && (out.markAppliedExecuted || out.actionMarkedApplied)) {
    addIssue(out, "mark_applied_executed");
  }

  out.guarded = out.issues.empty();
  out.status = out.guarded ? "proof_chain_guarded_no_live_side_effects" : "proof_chain_guard_rejected";
  return out;
}

std::string dialogIntentChainGuardJson(const NpcPerceptionDialogIntentChainGuard& guard) {
  std::string out;
  out.reserve(4096 + guard.completedStages.size() * 48 + guard.blockedSideEffects.size() * 32 +
              guard.issues.size() * 48);
  out.push_back('{');
  appendJsonBoolField(out, "guarded", guard.guarded);
  appendComma(out);
  appendJsonField(out, "status", guard.status);
  appendComma(out);
  appendJsonField(out, "contract_version", guard.contractVersion);
  appendComma(out);
  appendJsonField(out, "guard_source", guard.guardSource);
  appendComma(out);
  appendJsonField(out, "guard_mode", guard.guardMode);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", guard.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "timer_scheduled", guard.timerScheduled);
  appendComma(out);
  appendJsonBoolField(out, "timeout_observed", guard.timeoutObserved);
  appendComma(out);
  appendJsonBoolField(out, "socket_receive_executed", guard.socketReceiveExecuted);
  appendComma(out);
  appendJsonBoolField(out, "live_packet_decoded", guard.livePacketDecoded);
  appendComma(out);
  appendJsonBoolField(out, "client_ack_observed", guard.clientAckObserved);
  appendComma(out);
  appendJsonBoolField(out, "client_nack_observed", guard.clientNackObserved);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", guard.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", guard.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", guard.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", guard.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", guard.markAppliedExecuted);
  appendComma(out);
  appendJsonBoolField(out, "action_marked_applied", guard.actionMarkedApplied);
  appendComma(out);
  appendJsonBoolField(out, "action_marked_failed", guard.actionMarkedFailed);
  appendComma(out);
  appendJsonBoolField(out, "dead_letter_written", guard.deadLetterWritten);
  appendComma(out);
  appendJsonBoolField(out, "retry_queued", guard.retryQueued);
  appendComma(out);
  appendJsonBoolField(out, "proof_chain_complete", guard.proofChainComplete);
  appendComma(out);
  appendJsonBoolField(out, "terminal_plan_ready", guard.terminalPlanReady);
  appendComma(out);
  appendJsonBoolField(out, "receive_loop_integration_proofed", guard.receiveLoopIntegrationProofed);
  appendComma(out);
  appendJsonBoolField(out, "pending_ack_slot_shape_ready", guard.pendingAckSlotShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "ack_route_shape_ready", guard.ackRouteShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "nack_route_shape_ready", guard.nackRouteShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "malformed_route_rejected", guard.malformedRouteRejected);
  appendComma(out);
  appendJsonBoolField(out, "timeout_configured", guard.timeoutConfigured);
  appendComma(out);
  appendJsonBoolField(out, "terminal_status_deferred", guard.terminalStatusDeferred);
  appendComma(out);
  appendJsonBoolField(out, "ack_apply_finalization_planned", guard.ackApplyFinalizationPlanned);
  appendComma(out);
  appendJsonBoolField(out, "nack_retry_or_reject_planned", guard.nackRetryOrRejectPlanned);
  appendComma(out);
  appendJsonBoolField(out, "timeout_dead_letter_planned", guard.timeoutDeadLetterPlanned);
  appendComma(out);
  appendJsonBoolField(out, "no_live_side_effects", guard.noLiveSideEffects);
  appendComma(out);
  appendJsonBoolField(out, "future_dispatcher_prerequisites_ready", guard.futureDispatcherPrerequisitesReady);
  appendComma(out);
  appendJsonBoolField(out, "future_storage_prerequisites_missing", guard.futureStoragePrerequisitesMissing);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", guard.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", guard.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", guard.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", guard.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", guard.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", guard.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", guard.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", guard.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", guard.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", guard.idempotencyKey);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", guard.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", guard.targetCharacterUuid);
  appendComma(out);
  appendJsonField(out, "ack_correlation_key", guard.ackCorrelationKey);
  appendComma(out);
  appendJsonField(out, "expected_ack_idempotency_key", guard.expectedAckIdempotencyKey);
  appendComma(out);
  appendJsonField(out, "receive_route_key", guard.receiveRouteKey);
  appendComma(out);
  appendJsonField(out, "next_required_approval", guard.nextRequiredApproval);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", guard.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", guard.localSequence);
  appendComma(out);
  appendJsonCountField(out, "ack_timeout_ms", guard.ackTimeoutMs);
  appendComma(out);
  appendJsonCountField(out, "max_retry_attempts", guard.maxRetryAttempts);
  appendComma(out);
  appendJsonCountField(out, "completed_stage_count", guard.completedStages.size());
  appendComma(out);
  out += jsonEscape("completed_stages");
  out.push_back(':');
  appendStringArrayJson(out, guard.completedStages);
  appendComma(out);
  appendJsonCountField(out, "blocked_side_effect_count", guard.blockedSideEffects.size());
  appendComma(out);
  out += jsonEscape("blocked_side_effects");
  out.push_back(':');
  appendStringArrayJson(out, guard.blockedSideEffects);
  appendComma(out);
  appendJsonCountField(out, "issue_count", guard.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out.push_back(':');
  appendStringArrayJson(out, guard.issues);
  out.push_back('}');
  return out;
}

} // namespace Mmo::AiRuntime
