#include "mmo_npc_perception_dialog_intent_sender_adapter.h"

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

void addIssue(NpcPerceptionDialogIntentSenderAdapterProof& proof, std::string issue) {
  proof.issues.push_back(std::move(issue));
}

[[nodiscard]] bool diagnosticEncodingSafe(
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding) noexcept {
  return diagnosticEncoding.encoded && diagnosticEncoding.fitsDatagram && diagnosticEncoding.decodedRoundTrip &&
         diagnosticEncoding.decodedFieldsMatch && !diagnosticEncoding.sendExecuted &&
         !diagnosticEncoding.packetFanoutExecuted && !diagnosticEncoding.dialogUiExecuted &&
         !diagnosticEncoding.audioExecuted && !diagnosticEncoding.markAppliedExecuted &&
         !diagnosticEncoding.encodedBytesBuffer.empty();
}

[[nodiscard]] bool sendBoundarySafe(const NpcPerceptionDialogIntentSendBoundary& sendBoundary) noexcept {
  return sendBoundary.prepared && sendBoundary.sendBlockedByDesign && sendBoundary.wouldSendIfEnabled &&
         sendBoundary.fanoutPlanBuilt && sendBoundary.diagnosticEncodingSafe && sendBoundary.hasEncodedPayload &&
         sendBoundary.hasTargetSession && sendBoundary.fitsSingleDatagram && !sendBoundary.dbMutated &&
         !sendBoundary.sendExecuted && !sendBoundary.packetFanoutExecuted && !sendBoundary.dialogUiExecuted &&
         !sendBoundary.audioExecuted && !sendBoundary.markAppliedExecuted;
}

} // namespace

bool supportsNpcPerceptionDialogIntentSenderAdapterProof(
    const NpcPerceptionDialogIntentSendBoundary& sendBoundary,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding) noexcept {
  return sendBoundarySafe(sendBoundary) && diagnosticEncodingSafe(diagnosticEncoding) &&
         !sendBoundary.targetSessionUuid.empty() && !sendBoundary.targetCharacterUuid.empty() &&
         sendBoundary.encodedPayloadBytes == diagnosticEncoding.encodedBytesBuffer.size() &&
         sendBoundary.encodedBytes == diagnosticEncoding.encodedBytes &&
         sendBoundary.packetSequence == diagnosticEncoding.packetSequence &&
         sendBoundary.localSequence == diagnosticEncoding.localSequence;
}

NpcPerceptionDialogIntentSenderAdapterProof proveNpcPerceptionDialogIntentSenderAdapter(
    const NpcPerceptionDialogIntentSendBoundary& sendBoundary,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentSenderAdapterOptions& options) {
  NpcPerceptionDialogIntentSenderAdapterProof out;
  out.contractVersion = options.contractVersion;
  out.adapterSource = options.adapterSource;
  out.adapterMode = options.adapterMode;
  out.transportKind = options.transportKind;
  out.recipientRouteKind = options.recipientRouteKind;
  out.packetKind = sendBoundary.packetKind.empty() ? diagnosticEncoding.packetKind : sendBoundary.packetKind;

  out.actionQueueUuid = sendBoundary.actionQueueUuid;
  out.decisionUuid = sendBoundary.decisionUuid;
  out.actionKind = sendBoundary.actionKind;
  out.worldInstanceUuid = sendBoundary.worldInstanceUuid;
  out.sessionUuid = sendBoundary.sessionUuid;
  out.characterUuid = sendBoundary.characterUuid;
  out.npcEntityKey = sendBoundary.npcEntityKey;
  out.targetKey = sendBoundary.targetKey;
  out.perceptionKind = sendBoundary.perceptionKind;
  out.idempotencyKey = sendBoundary.idempotencyKey;

  out.targetSessionUuid = sendBoundary.targetSessionUuid;
  out.targetCharacterUuid = sendBoundary.targetCharacterUuid;
  out.packetSequence = sendBoundary.packetSequence;
  out.localSequence = sendBoundary.localSequence;
  out.encodedBytes = sendBoundary.encodedBytes;
  out.encodedPayloadBytes = sendBoundary.encodedPayloadBytes;
  out.maxDatagramBytes = sendBoundary.maxDatagramBytes;
  out.plannedDatagrams = sendBoundary.plannedDatagrams;

  out.dbMutated = false;
  out.routeLookupExecuted = false;
  out.endpointResolved = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;
  out.requiresLiveEndpointResolver = true;

  out.sendBoundaryPrepared = sendBoundary.prepared;
  out.sendBlockedByDesign = sendBoundary.sendBlockedByDesign;
  out.wouldSendIfEnabled = sendBoundary.wouldSendIfEnabled;
  out.diagnosticEncodingSafe = diagnosticEncodingSafe(diagnosticEncoding);
  out.hasEncodedPayload = sendBoundary.hasEncodedPayload && !diagnosticEncoding.encodedBytesBuffer.empty();
  out.hasTargetSession = !sendBoundary.targetSessionUuid.empty();
  out.hasTargetCharacter = !sendBoundary.targetCharacterUuid.empty();
  out.fitsSingleDatagram = sendBoundary.fitsSingleDatagram && diagnosticEncoding.fitsDatagram;
  out.payloadBytesMatch = sendBoundary.encodedPayloadBytes == diagnosticEncoding.encodedBytesBuffer.size() &&
                          sendBoundary.encodedBytes == diagnosticEncoding.encodedBytes;
  out.sequenceNumbersMatch = sendBoundary.packetSequence == diagnosticEncoding.packetSequence &&
                             sendBoundary.localSequence == diagnosticEncoding.localSequence;

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.adapterSource.empty()) {
    addIssue(out, "missing_adapter_source");
  }
  if(options.adapterMode != "proof_only_no_send") {
    addIssue(out, "adapter_mode_must_remain_proof_only_no_send");
  }
  if(options.transportKind != "asio_udp_server_diagnostic_packet") {
    addIssue(out, "unsupported_transport_kind");
  }
  if(options.recipientRouteKind != "active_session_udp_endpoint") {
    addIssue(out, "unsupported_recipient_route_kind");
  }
  if(options.requireSendBoundaryPrepared && !sendBoundary.prepared) {
    addIssue(out, "send_boundary_not_prepared");
  }
  if(options.requireBlockedByDesign && !sendBoundary.sendBlockedByDesign) {
    addIssue(out, "send_boundary_not_blocked_by_design");
  }
  if(!sendBoundarySafe(sendBoundary)) {
    addIssue(out, "send_boundary_not_safe_for_sender_adapter");
  }
  if(!diagnosticEncodingSafe(diagnosticEncoding)) {
    addIssue(out, "diagnostic_encoding_not_safe_for_sender_adapter");
  }
  if(!out.hasEncodedPayload) {
    addIssue(out, "missing_encoded_payload_bytes");
  }
  if(!out.hasTargetSession) {
    addIssue(out, "missing_target_session_uuid");
  }
  if(!out.hasTargetCharacter) {
    addIssue(out, "missing_target_character_uuid");
  }
  if(!out.payloadBytesMatch) {
    addIssue(out, "encoded_payload_size_mismatch");
  }
  if(!out.sequenceNumbersMatch) {
    addIssue(out, "diagnostic_sequence_mismatch");
  }
  if(options.requireSingleDatagram && (!out.fitsSingleDatagram || sendBoundary.plannedDatagrams != 1U)) {
    addIssue(out, "diagnostic_packet_not_single_datagram_safe");
  }
  if(sendBoundary.sendExecuted || sendBoundary.packetFanoutExecuted || sendBoundary.dialogUiExecuted ||
     sendBoundary.audioExecuted || sendBoundary.markAppliedExecuted || diagnosticEncoding.sendExecuted ||
     diagnosticEncoding.packetFanoutExecuted || diagnosticEncoding.dialogUiExecuted || diagnosticEncoding.audioExecuted ||
     diagnosticEncoding.markAppliedExecuted) {
    addIssue(out, "upstream_boundary_has_live_side_effect");
  }

  out.proofed = out.issues.empty();
  out.status = out.proofed ? "sender_adapter_proofed_no_send" : "sender_adapter_rejected";
  return out;
}

std::string dialogIntentSenderAdapterProofJson(const NpcPerceptionDialogIntentSenderAdapterProof& proof) {
  std::string out;
  out.reserve(1792 + proof.issues.size() * 48);
  out.push_back('{');

  appendJsonBoolField(out, "proofed", proof.proofed);
  appendComma(out);
  appendJsonField(out, "status", proof.status);
  appendComma(out);
  appendJsonField(out, "contract_version", proof.contractVersion);
  appendComma(out);
  appendJsonField(out, "adapter_source", proof.adapterSource);
  appendComma(out);
  appendJsonField(out, "adapter_mode", proof.adapterMode);
  appendComma(out);
  appendJsonField(out, "transport_kind", proof.transportKind);
  appendComma(out);
  appendJsonField(out, "recipient_route_kind", proof.recipientRouteKind);
  appendComma(out);
  appendJsonField(out, "packet_kind", proof.packetKind);
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
  appendJsonCountField(out, "packet_sequence", proof.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", proof.localSequence);
  appendComma(out);
  appendJsonCountField(out, "encoded_bytes", proof.encodedBytes);
  appendComma(out);
  appendJsonCountField(out, "encoded_payload_bytes", proof.encodedPayloadBytes);
  appendComma(out);
  appendJsonCountField(out, "max_datagram_bytes", proof.maxDatagramBytes);
  appendComma(out);
  appendJsonCountField(out, "planned_datagrams", proof.plannedDatagrams);
  appendComma(out);
  appendJsonBoolField(out, "send_boundary_prepared", proof.sendBoundaryPrepared);
  appendComma(out);
  appendJsonBoolField(out, "send_blocked_by_design", proof.sendBlockedByDesign);
  appendComma(out);
  appendJsonBoolField(out, "would_send_if_enabled", proof.wouldSendIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "diagnostic_encoding_safe", proof.diagnosticEncodingSafe);
  appendComma(out);
  appendJsonBoolField(out, "has_encoded_payload", proof.hasEncodedPayload);
  appendComma(out);
  appendJsonBoolField(out, "has_target_session", proof.hasTargetSession);
  appendComma(out);
  appendJsonBoolField(out, "has_target_character", proof.hasTargetCharacter);
  appendComma(out);
  appendJsonBoolField(out, "fits_single_datagram", proof.fitsSingleDatagram);
  appendComma(out);
  appendJsonBoolField(out, "payload_bytes_match", proof.payloadBytesMatch);
  appendComma(out);
  appendJsonBoolField(out, "sequence_numbers_match", proof.sequenceNumbersMatch);
  appendComma(out);
  appendJsonBoolField(out, "requires_live_endpoint_resolver", proof.requiresLiveEndpointResolver);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", proof.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "route_lookup_executed", proof.routeLookupExecuted);
  appendComma(out);
  appendJsonBoolField(out, "endpoint_resolved", proof.endpointResolved);
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
