#include "mmo_npc_perception_dialog_intent_receive_loop_integration.h"

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

void addIssue(NpcPerceptionDialogIntentReceiveLoopIntegrationProof& proof, std::string issue) {
  proof.issues.push_back(std::move(issue));
}

[[nodiscard]] bool receiptPreviewSafe(
    const NpcPerceptionDialogIntentClientAckReceiptPreview& preview) noexcept {
  return preview.built && preview.clientAckContractBuilt && preview.parserReady && preview.validatorReady &&
         preview.syntheticAckReceiptValidated && preview.syntheticNackReceiptValidated &&
         preview.syntheticMalformedReceiptRejected && preview.ackCorrelationReady && preview.timeoutConfigured &&
         preview.targetSessionMatched && preview.targetCharacterMatched && preview.terminalStatusDeferred &&
         !preview.dbMutated && !preview.socketReceiveExecuted && !preview.clientPacketDecoded &&
         !preview.clientAckObserved && !preview.clientNackObserved && !preview.sendExecuted &&
         !preview.packetFanoutExecuted && !preview.dialogUiExecuted && !preview.audioExecuted &&
         !preview.markAppliedExecuted;
}

[[nodiscard]] std::string makeReceiveRouteKey(
    const NpcPerceptionDialogIntentClientAckReceiptPreview& preview) {
  std::string out;
  out.reserve(preview.targetSessionUuid.size() + preview.ackCorrelationKey.size() + 16);
  out += "session:";
  out += preview.targetSessionUuid;
  out += ":ack:";
  out += preview.ackCorrelationKey;
  return out;
}

} // namespace

bool supportsNpcPerceptionDialogIntentReceiveLoopIntegrationProof(
    const NpcPerceptionDialogIntentClientAckReceiptPreview& receiptPreview) noexcept {
  return receiptPreviewSafe(receiptPreview) && !receiptPreview.expectedAckPacketKind.empty() &&
         !receiptPreview.expectedNackPacketKind.empty() && !receiptPreview.ackCorrelationKey.empty() &&
         !receiptPreview.expectedAckIdempotencyKey.empty() && !receiptPreview.targetSessionUuid.empty() &&
         !receiptPreview.targetCharacterUuid.empty();
}

NpcPerceptionDialogIntentReceiveLoopIntegrationProof proveNpcPerceptionDialogIntentReceiveLoopIntegration(
    const NpcPerceptionDialogIntentClientAckReceiptPreview& receiptPreview,
    const NpcPerceptionDialogIntentReceiveLoopIntegrationOptions& options) {
  NpcPerceptionDialogIntentReceiveLoopIntegrationProof out;
  out.contractVersion = options.contractVersion;
  out.integrationSource = options.integrationSource;
  out.integrationMode = options.integrationMode;
  out.expectedAckPacketKind = receiptPreview.expectedAckPacketKind;
  out.expectedNackPacketKind = receiptPreview.expectedNackPacketKind;
  out.actionQueueUuid = receiptPreview.actionQueueUuid;
  out.decisionUuid = receiptPreview.decisionUuid;
  out.actionKind = receiptPreview.actionKind;
  out.worldInstanceUuid = receiptPreview.worldInstanceUuid;
  out.sessionUuid = receiptPreview.sessionUuid;
  out.characterUuid = receiptPreview.characterUuid;
  out.npcEntityKey = receiptPreview.npcEntityKey;
  out.targetKey = receiptPreview.targetKey;
  out.perceptionKind = receiptPreview.perceptionKind;
  out.idempotencyKey = receiptPreview.idempotencyKey;
  out.targetSessionUuid = receiptPreview.targetSessionUuid;
  out.targetCharacterUuid = receiptPreview.targetCharacterUuid;
  out.ackCorrelationKey = receiptPreview.ackCorrelationKey;
  out.expectedAckIdempotencyKey = receiptPreview.expectedAckIdempotencyKey;
  out.packetSequence = receiptPreview.packetSequence;
  out.localSequence = receiptPreview.localSequence;
  out.ackTimeoutMs = receiptPreview.ackTimeoutMs;
  out.receiveRouteKey = makeReceiveRouteKey(receiptPreview);

  out.dbMutated = false;
  out.receiveLoopEntered = false;
  out.socketReceiveExecuted = false;
  out.livePacketDecoded = false;
  out.clientAckObserved = false;
  out.clientNackObserved = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;

  out.receiptPreviewBuilt = receiptPreview.built;
  out.parserReady = receiptPreview.parserReady;
  out.validatorReady = receiptPreview.validatorReady;
  out.syntheticAckReceiptValidated = receiptPreview.syntheticAckReceiptValidated;
  out.syntheticNackReceiptValidated = receiptPreview.syntheticNackReceiptValidated;
  out.syntheticMalformedReceiptRejected = receiptPreview.syntheticMalformedReceiptRejected;
  out.ackCorrelationReady = receiptPreview.ackCorrelationReady;
  out.timeoutConfigured = receiptPreview.timeoutConfigured;
  out.targetSessionReady = !receiptPreview.targetSessionUuid.empty() && receiptPreview.targetSessionMatched;
  out.targetCharacterReady = !receiptPreview.targetCharacterUuid.empty() && receiptPreview.targetCharacterMatched;
  out.expectedPacketKindsRegistered = !receiptPreview.expectedAckPacketKind.empty() &&
                                      !receiptPreview.expectedNackPacketKind.empty();
  out.ackRouteShapeReady = out.expectedPacketKindsRegistered && out.ackCorrelationReady &&
                           out.targetSessionReady && receiptPreview.syntheticAckReceiptValidated;
  out.nackRouteShapeReady = out.expectedPacketKindsRegistered && out.ackCorrelationReady &&
                            out.targetSessionReady && receiptPreview.syntheticNackReceiptValidated;
  out.malformedRouteRejected = receiptPreview.syntheticMalformedReceiptRejected;
  out.pendingAckSlotShapeReady = !out.receiveRouteKey.empty() && out.timeoutConfigured &&
                                 !receiptPreview.expectedAckIdempotencyKey.empty();
  out.terminalStatusDeferred = receiptPreview.terminalStatusDeferred;
  out.wouldEnterReceiveLoopIfEnabled = out.parserReady && out.validatorReady && out.pendingAckSlotShapeReady;
  out.wouldRouteAckToReceiptParserIfEnabled = out.wouldEnterReceiveLoopIfEnabled && out.ackRouteShapeReady;
  out.wouldRouteNackToReceiptParserIfEnabled = out.wouldEnterReceiveLoopIfEnabled && out.nackRouteShapeReady;
  out.wouldRejectMalformedReceiptIfEnabled = out.wouldEnterReceiveLoopIfEnabled && out.malformedRouteRejected;
  out.wouldMarkAppliedAfterAckIfEnabled = out.wouldRouteAckToReceiptParserIfEnabled &&
                                          receiptPreview.wouldMarkAppliedAfterAckIfEnabled;
  out.wouldRejectOrRetryAfterNackIfEnabled = out.wouldRouteNackToReceiptParserIfEnabled &&
                                             receiptPreview.wouldRejectOrRetryAfterNackIfEnabled;

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.integrationSource.empty()) {
    addIssue(out, "missing_integration_source");
  }
  if(options.integrationMode != "proof_only_no_socket_receive") {
    addIssue(out, "integration_mode_must_remain_proof_only_no_socket_receive");
  }
  if(options.requireReceiptPreviewBuilt && !receiptPreview.built) {
    addIssue(out, "receipt_preview_not_built");
  }
  if(options.requireParserReady && !receiptPreview.parserReady) {
    addIssue(out, "receipt_parser_not_ready");
  }
  if(options.requireValidatorReady && !receiptPreview.validatorReady) {
    addIssue(out, "receipt_validator_not_ready");
  }
  if(!receiptPreviewSafe(receiptPreview)) {
    addIssue(out, "receipt_preview_not_safe_for_receive_loop_integration");
  }
  if(!out.expectedPacketKindsRegistered) {
    addIssue(out, "expected_packet_kinds_not_registered");
  }
  if(!out.ackRouteShapeReady) {
    addIssue(out, "ack_route_shape_not_ready");
  }
  if(!out.nackRouteShapeReady) {
    addIssue(out, "nack_route_shape_not_ready");
  }
  if(!out.malformedRouteRejected) {
    addIssue(out, "malformed_route_not_rejected");
  }
  if(!out.pendingAckSlotShapeReady) {
    addIssue(out, "pending_ack_slot_shape_not_ready");
  }
  if(!out.terminalStatusDeferred) {
    addIssue(out, "terminal_status_not_deferred");
  }
  if(options.requireNoSocketReceive && (out.receiveLoopEntered || out.socketReceiveExecuted)) {
    addIssue(out, "receive_loop_or_socket_receive_executed");
  }
  if(options.requireNoClientPacketDecoded && out.livePacketDecoded) {
    addIssue(out, "live_packet_decoded");
  }
  if(options.requireNoClientReceiptObserved && (out.clientAckObserved || out.clientNackObserved)) {
    addIssue(out, "client_ack_or_nack_observed");
  }
  if(options.requireNoSend && (out.sendExecuted || out.packetFanoutExecuted)) {
    addIssue(out, "send_or_fanout_executed");
  }
  if(options.requireNoDbMutation && out.dbMutated) {
    addIssue(out, "db_mutated");
  }

  out.proofed = out.issues.empty();
  out.status = out.proofed ? "receive_loop_integration_proofed_no_socket_receive" :
                             "receive_loop_integration_rejected";
  return out;
}

std::string dialogIntentReceiveLoopIntegrationProofJson(
    const NpcPerceptionDialogIntentReceiveLoopIntegrationProof& proof) {
  std::string out;
  out.reserve(3072 + proof.issues.size() * 48 + proof.receiveRouteKey.size());
  out.push_back('{');

  appendJsonBoolField(out, "proofed", proof.proofed);
  appendComma(out);
  appendJsonField(out, "status", proof.status);
  appendComma(out);
  appendJsonField(out, "contract_version", proof.contractVersion);
  appendComma(out);
  appendJsonField(out, "integration_source", proof.integrationSource);
  appendComma(out);
  appendJsonField(out, "integration_mode", proof.integrationMode);
  appendComma(out);
  appendJsonField(out, "expected_ack_packet_kind", proof.expectedAckPacketKind);
  appendComma(out);
  appendJsonField(out, "expected_nack_packet_kind", proof.expectedNackPacketKind);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", proof.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", proof.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", proof.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", proof.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", proof.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", proof.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", proof.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", proof.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", proof.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", proof.idempotencyKey);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", proof.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", proof.targetCharacterUuid);
  appendComma(out);
  appendJsonField(out, "ack_correlation_key", proof.ackCorrelationKey);
  appendComma(out);
  appendJsonField(out, "expected_ack_idempotency_key", proof.expectedAckIdempotencyKey);
  appendComma(out);
  appendJsonField(out, "receive_route_key", proof.receiveRouteKey);
  appendComma(out);
  appendJsonField(out, "ack_route_description", proof.ackRouteDescription);
  appendComma(out);
  appendJsonField(out, "nack_route_description", proof.nackRouteDescription);
  appendComma(out);
  appendJsonField(out, "malformed_route_description", proof.malformedRouteDescription);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", proof.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", proof.localSequence);
  appendComma(out);
  appendJsonCountField(out, "ack_timeout_ms", proof.ackTimeoutMs);
  appendComma(out);
  appendJsonBoolField(out, "receipt_preview_built", proof.receiptPreviewBuilt);
  appendComma(out);
  appendJsonBoolField(out, "parser_ready", proof.parserReady);
  appendComma(out);
  appendJsonBoolField(out, "validator_ready", proof.validatorReady);
  appendComma(out);
  appendJsonBoolField(out, "synthetic_ack_receipt_validated", proof.syntheticAckReceiptValidated);
  appendComma(out);
  appendJsonBoolField(out, "synthetic_nack_receipt_validated", proof.syntheticNackReceiptValidated);
  appendComma(out);
  appendJsonBoolField(out, "synthetic_malformed_receipt_rejected", proof.syntheticMalformedReceiptRejected);
  appendComma(out);
  appendJsonBoolField(out, "ack_correlation_ready", proof.ackCorrelationReady);
  appendComma(out);
  appendJsonBoolField(out, "timeout_configured", proof.timeoutConfigured);
  appendComma(out);
  appendJsonBoolField(out, "target_session_ready", proof.targetSessionReady);
  appendComma(out);
  appendJsonBoolField(out, "target_character_ready", proof.targetCharacterReady);
  appendComma(out);
  appendJsonBoolField(out, "expected_packet_kinds_registered", proof.expectedPacketKindsRegistered);
  appendComma(out);
  appendJsonBoolField(out, "ack_route_shape_ready", proof.ackRouteShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "nack_route_shape_ready", proof.nackRouteShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "malformed_route_rejected", proof.malformedRouteRejected);
  appendComma(out);
  appendJsonBoolField(out, "pending_ack_slot_shape_ready", proof.pendingAckSlotShapeReady);
  appendComma(out);
  appendJsonBoolField(out, "terminal_status_deferred", proof.terminalStatusDeferred);
  appendComma(out);
  appendJsonBoolField(out, "would_enter_receive_loop_if_enabled", proof.wouldEnterReceiveLoopIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_route_ack_to_receipt_parser_if_enabled", proof.wouldRouteAckToReceiptParserIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_route_nack_to_receipt_parser_if_enabled", proof.wouldRouteNackToReceiptParserIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_reject_malformed_receipt_if_enabled", proof.wouldRejectMalformedReceiptIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_mark_applied_after_ack_if_enabled", proof.wouldMarkAppliedAfterAckIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_reject_or_retry_after_nack_if_enabled", proof.wouldRejectOrRetryAfterNackIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", proof.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "receive_loop_entered", proof.receiveLoopEntered);
  appendComma(out);
  appendJsonBoolField(out, "socket_receive_executed", proof.socketReceiveExecuted);
  appendComma(out);
  appendJsonBoolField(out, "live_packet_decoded", proof.livePacketDecoded);
  appendComma(out);
  appendJsonBoolField(out, "client_ack_observed", proof.clientAckObserved);
  appendComma(out);
  appendJsonBoolField(out, "client_nack_observed", proof.clientNackObserved);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", proof.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", proof.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", proof.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", proof.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", proof.markAppliedExecuted);
  appendComma(out);
  appendJsonCountField(out, "issue_count", proof.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  bool first = true;
  for(const auto& issue : proof.issues) {
    if(!first) {
      out.push_back(',');
    }
    first = false;
    out += jsonEscape(issue);
  }
  out += "]}";
  return out;
}

} // namespace Mmo::AiRuntime
