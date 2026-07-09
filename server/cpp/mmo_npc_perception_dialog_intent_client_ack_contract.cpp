#include "mmo_npc_perception_dialog_intent_client_ack_contract.h"

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

void addIssue(NpcPerceptionDialogIntentClientAckContract& contract, std::string issue) {
  contract.issues.push_back(std::move(issue));
}

[[nodiscard]] bool endpointResolutionSafe(
    const NpcPerceptionDialogIntentEndpointResolutionProof& proof) noexcept {
  return proof.proofed && proof.senderAdapterProofed && proof.senderAdapterSafe && proof.endpointLookupInputReady &&
         proof.wouldResolveEndpointIfEnabled && proof.hasTargetSession && proof.hasTargetCharacter &&
         proof.hasEncodedPayload && proof.fitsSingleDatagram && proof.payloadBytesMatch &&
         proof.sequenceNumbersMatch && proof.transportKindSupported && proof.recipientRouteKindSupported &&
         proof.recipientRouteKind == "active_session_udp_endpoint" && !proof.dbMutated &&
         !proof.routeLookupExecuted && !proof.endpointResolverExecuted && !proof.endpointResolved &&
         !proof.sendExecuted && !proof.packetFanoutExecuted && !proof.dialogUiExecuted && !proof.audioExecuted &&
         !proof.markAppliedExecuted;
}

[[nodiscard]] std::string makeAckCorrelationKey(
    const NpcPerceptionDialogIntentEndpointResolutionProof& proof) {
  if(proof.actionQueueUuid.empty() || proof.decisionUuid.empty() || proof.idempotencyKey.empty()) {
    return {};
  }
  return proof.actionQueueUuid + ":" + proof.decisionUuid + ":" + std::to_string(proof.packetSequence) + ":" +
         proof.idempotencyKey;
}

} // namespace

bool supportsNpcPerceptionDialogIntentClientAckContract(
    const NpcPerceptionDialogIntentEndpointResolutionProof& endpointResolutionProof) noexcept {
  return endpointResolutionSafe(endpointResolutionProof) && !endpointResolutionProof.actionQueueUuid.empty() &&
         !endpointResolutionProof.decisionUuid.empty() && !endpointResolutionProof.idempotencyKey.empty() &&
         !endpointResolutionProof.routeLookupKey.empty() && endpointResolutionProof.plannedDatagrams == 1U;
}

NpcPerceptionDialogIntentClientAckContract buildNpcPerceptionDialogIntentClientAckContract(
    const NpcPerceptionDialogIntentEndpointResolutionProof& endpointResolutionProof,
    const NpcPerceptionDialogIntentClientAckContractOptions& options) {
  NpcPerceptionDialogIntentClientAckContract out;
  out.contractVersion = options.contractVersion;
  out.ackSource = options.ackSource;
  out.ackMode = options.ackMode;
  out.expectedAckPacketKind = options.expectedAckPacketKind;
  out.expectedNackPacketKind = options.expectedNackPacketKind;
  out.ackRouteKind = options.ackRouteKind;
  out.packetKind = endpointResolutionProof.packetKind;

  out.actionQueueUuid = endpointResolutionProof.actionQueueUuid;
  out.decisionUuid = endpointResolutionProof.decisionUuid;
  out.actionKind = endpointResolutionProof.actionKind;
  out.worldInstanceUuid = endpointResolutionProof.worldInstanceUuid;
  out.sessionUuid = endpointResolutionProof.sessionUuid;
  out.characterUuid = endpointResolutionProof.characterUuid;
  out.npcEntityKey = endpointResolutionProof.npcEntityKey;
  out.targetKey = endpointResolutionProof.targetKey;
  out.perceptionKind = endpointResolutionProof.perceptionKind;
  out.idempotencyKey = endpointResolutionProof.idempotencyKey;

  out.targetSessionUuid = endpointResolutionProof.targetSessionUuid;
  out.targetCharacterUuid = endpointResolutionProof.targetCharacterUuid;
  out.routeLookupKey = endpointResolutionProof.routeLookupKey;
  out.packetSequence = endpointResolutionProof.packetSequence;
  out.localSequence = endpointResolutionProof.localSequence;
  out.ackTimeoutMs = options.ackTimeoutMs;
  out.encodedBytes = endpointResolutionProof.encodedBytes;
  out.encodedPayloadBytes = endpointResolutionProof.encodedPayloadBytes;
  out.plannedDatagrams = endpointResolutionProof.plannedDatagrams;
  out.ackCorrelationKey = makeAckCorrelationKey(endpointResolutionProof);
  out.expectedAckIdempotencyKey = out.ackCorrelationKey.empty() ? std::string() : "ack:" + out.ackCorrelationKey;

  out.dbMutated = false;
  out.routeLookupExecuted = false;
  out.endpointResolverExecuted = false;
  out.endpointResolved = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.clientAckObserved = false;
  out.clientNackObserved = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;
  out.terminalStatusDeferred = true;

  out.endpointResolutionProofed = endpointResolutionProof.proofed;
  out.endpointLookupInputReady = endpointResolutionProof.endpointLookupInputReady;
  out.hasTargetSession = !endpointResolutionProof.targetSessionUuid.empty();
  out.hasTargetCharacter = !endpointResolutionProof.targetCharacterUuid.empty();
  out.hasEncodedPayload = endpointResolutionProof.hasEncodedPayload && endpointResolutionProof.encodedPayloadBytes > 0U;
  out.fitsSingleDatagram = endpointResolutionProof.fitsSingleDatagram && endpointResolutionProof.plannedDatagrams == 1U;
  out.ackExpectedIfSent = endpointResolutionSafe(endpointResolutionProof);
  out.nackExpectedOnClientReject = out.ackExpectedIfSent;
  out.ackCorrelationReady = !out.ackCorrelationKey.empty() && !out.expectedAckIdempotencyKey.empty();
  out.timeoutConfigured = options.ackTimeoutMs > 0U;
  out.wouldWaitForClientAckIfEnabled = out.ackExpectedIfSent && out.ackCorrelationReady && out.timeoutConfigured;
  out.wouldMarkAppliedOnlyAfterAck = out.wouldWaitForClientAckIfEnabled;

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.ackSource.empty()) {
    addIssue(out, "missing_ack_source");
  }
  if(options.ackMode != "preview_only_no_client_ack") {
    addIssue(out, "ack_mode_must_remain_preview_only_no_client_ack");
  }
  if(options.expectedAckPacketKind.empty()) {
    addIssue(out, "missing_expected_ack_packet_kind");
  }
  if(options.expectedNackPacketKind.empty()) {
    addIssue(out, "missing_expected_nack_packet_kind");
  }
  if(options.ackRouteKind != "active_session_udp_endpoint") {
    addIssue(out, "unsupported_ack_route_kind");
  }
  if(options.requireEndpointResolutionProofed && !endpointResolutionProof.proofed) {
    addIssue(out, "endpoint_resolution_not_proofed");
  }
  if(!endpointResolutionSafe(endpointResolutionProof)) {
    addIssue(out, "endpoint_resolution_not_safe_for_client_ack_contract");
  }
  if(!out.endpointLookupInputReady) {
    addIssue(out, "endpoint_lookup_input_not_ready");
  }
  if(!out.hasTargetSession) {
    addIssue(out, "missing_target_session_uuid");
  }
  if(!out.hasTargetCharacter) {
    addIssue(out, "missing_target_character_uuid");
  }
  if(out.routeLookupKey.empty()) {
    addIssue(out, "missing_route_lookup_key");
  }
  if(!out.hasEncodedPayload) {
    addIssue(out, "missing_encoded_payload_bytes");
  }
  if(out.actionQueueUuid.empty()) {
    addIssue(out, "missing_action_queue_uuid");
  }
  if(out.decisionUuid.empty()) {
    addIssue(out, "missing_decision_uuid");
  }
  if(out.idempotencyKey.empty()) {
    addIssue(out, "missing_idempotency_key");
  }
  if(!out.ackCorrelationReady) {
    addIssue(out, "ack_correlation_key_not_ready");
  }
  if(!out.timeoutConfigured) {
    addIssue(out, "ack_timeout_not_configured");
  }
  if(options.requireSingleDatagram && !out.fitsSingleDatagram) {
    addIssue(out, "diagnostic_packet_not_single_datagram_safe");
  }
  if(options.requireNoEndpointLookup &&
     (endpointResolutionProof.routeLookupExecuted || endpointResolutionProof.endpointResolverExecuted ||
      endpointResolutionProof.endpointResolved)) {
    addIssue(out, "endpoint_lookup_or_resolution_already_executed_upstream");
  }
  if(options.requireNoSend && (endpointResolutionProof.sendExecuted || endpointResolutionProof.packetFanoutExecuted)) {
    addIssue(out, "send_or_fanout_already_executed_upstream");
  }
  if(options.requireNoAckObserved && (out.clientAckObserved || out.clientNackObserved)) {
    addIssue(out, "client_ack_or_nack_already_observed");
  }
  if(endpointResolutionProof.dbMutated || endpointResolutionProof.dialogUiExecuted || endpointResolutionProof.audioExecuted ||
     endpointResolutionProof.markAppliedExecuted) {
    addIssue(out, "upstream_boundary_has_live_side_effect");
  }

  out.built = out.issues.empty();
  out.status = out.built ? "client_ack_contract_previewed_no_client_ack" : "client_ack_contract_rejected";
  return out;
}

std::string dialogIntentClientAckContractJson(
    const NpcPerceptionDialogIntentClientAckContract& contract) {
  std::string out;
  out.reserve(2304 + contract.issues.size() * 48);
  out.push_back('{');

  appendJsonBoolField(out, "built", contract.built);
  appendComma(out);
  appendJsonField(out, "status", contract.status);
  appendComma(out);
  appendJsonField(out, "contract_version", contract.contractVersion);
  appendComma(out);
  appendJsonField(out, "ack_source", contract.ackSource);
  appendComma(out);
  appendJsonField(out, "ack_mode", contract.ackMode);
  appendComma(out);
  appendJsonField(out, "expected_ack_packet_kind", contract.expectedAckPacketKind);
  appendComma(out);
  appendJsonField(out, "expected_nack_packet_kind", contract.expectedNackPacketKind);
  appendComma(out);
  appendJsonField(out, "ack_route_kind", contract.ackRouteKind);
  appendComma(out);
  appendJsonField(out, "packet_kind", contract.packetKind);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", contract.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", contract.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", contract.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", contract.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", contract.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", contract.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", contract.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", contract.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", contract.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", contract.idempotencyKey);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", contract.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", contract.targetCharacterUuid);
  appendComma(out);
  appendJsonField(out, "route_lookup_key", contract.routeLookupKey);
  appendComma(out);
  appendJsonField(out, "ack_correlation_key", contract.ackCorrelationKey);
  appendComma(out);
  appendJsonField(out, "expected_ack_idempotency_key", contract.expectedAckIdempotencyKey);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", contract.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", contract.localSequence);
  appendComma(out);
  appendJsonCountField(out, "ack_timeout_ms", contract.ackTimeoutMs);
  appendComma(out);
  appendJsonCountField(out, "encoded_bytes", contract.encodedBytes);
  appendComma(out);
  appendJsonCountField(out, "encoded_payload_bytes", contract.encodedPayloadBytes);
  appendComma(out);
  appendJsonCountField(out, "planned_datagrams", contract.plannedDatagrams);
  appendComma(out);
  appendJsonBoolField(out, "endpoint_resolution_proofed", contract.endpointResolutionProofed);
  appendComma(out);
  appendJsonBoolField(out, "endpoint_lookup_input_ready", contract.endpointLookupInputReady);
  appendComma(out);
  appendJsonBoolField(out, "has_target_session", contract.hasTargetSession);
  appendComma(out);
  appendJsonBoolField(out, "has_target_character", contract.hasTargetCharacter);
  appendComma(out);
  appendJsonBoolField(out, "has_encoded_payload", contract.hasEncodedPayload);
  appendComma(out);
  appendJsonBoolField(out, "fits_single_datagram", contract.fitsSingleDatagram);
  appendComma(out);
  appendJsonBoolField(out, "ack_expected_if_sent", contract.ackExpectedIfSent);
  appendComma(out);
  appendJsonBoolField(out, "nack_expected_on_client_reject", contract.nackExpectedOnClientReject);
  appendComma(out);
  appendJsonBoolField(out, "ack_correlation_ready", contract.ackCorrelationReady);
  appendComma(out);
  appendJsonBoolField(out, "timeout_configured", contract.timeoutConfigured);
  appendComma(out);
  appendJsonBoolField(out, "terminal_status_deferred", contract.terminalStatusDeferred);
  appendComma(out);
  appendJsonBoolField(out, "would_wait_for_client_ack_if_enabled", contract.wouldWaitForClientAckIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_mark_applied_only_after_ack", contract.wouldMarkAppliedOnlyAfterAck);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", contract.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "route_lookup_executed", contract.routeLookupExecuted);
  appendComma(out);
  appendJsonBoolField(out, "endpoint_resolver_executed", contract.endpointResolverExecuted);
  appendComma(out);
  appendJsonBoolField(out, "endpoint_resolved", contract.endpointResolved);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", contract.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", contract.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "client_ack_observed", contract.clientAckObserved);
  appendComma(out);
  appendJsonBoolField(out, "client_nack_observed", contract.clientNackObserved);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", contract.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", contract.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", contract.markAppliedExecuted);
  appendComma(out);
  appendJsonCountField(out, "issue_count", contract.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  bool first = true;
  for(const auto& issue : contract.issues) {
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
