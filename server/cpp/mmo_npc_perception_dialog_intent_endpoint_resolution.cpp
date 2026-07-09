#include "mmo_npc_perception_dialog_intent_endpoint_resolution.h"

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

void addIssue(NpcPerceptionDialogIntentEndpointResolutionProof& proof, std::string issue) {
  proof.issues.push_back(std::move(issue));
}

[[nodiscard]] bool senderAdapterSafe(const NpcPerceptionDialogIntentSenderAdapterProof& proof) noexcept {
  return proof.proofed && proof.sendBoundaryPrepared && proof.sendBlockedByDesign && proof.wouldSendIfEnabled &&
         proof.diagnosticEncodingSafe && proof.hasEncodedPayload && proof.hasTargetSession && proof.hasTargetCharacter &&
         proof.fitsSingleDatagram && proof.payloadBytesMatch && proof.sequenceNumbersMatch &&
         proof.requiresLiveEndpointResolver && proof.recipientRouteKind == "active_session_udp_endpoint" &&
         proof.transportKind == "asio_udp_server_diagnostic_packet" && !proof.dbMutated && !proof.routeLookupExecuted &&
         !proof.endpointResolved && !proof.sendExecuted && !proof.packetFanoutExecuted && !proof.dialogUiExecuted &&
         !proof.audioExecuted && !proof.markAppliedExecuted;
}

} // namespace

bool supportsNpcPerceptionDialogIntentEndpointResolutionProof(
    const NpcPerceptionDialogIntentSenderAdapterProof& senderAdapterProof) noexcept {
  return senderAdapterSafe(senderAdapterProof) && !senderAdapterProof.targetSessionUuid.empty() &&
         !senderAdapterProof.targetCharacterUuid.empty() && senderAdapterProof.encodedPayloadBytes > 0U &&
         senderAdapterProof.plannedDatagrams == 1U;
}

NpcPerceptionDialogIntentEndpointResolutionProof proveNpcPerceptionDialogIntentEndpointResolution(
    const NpcPerceptionDialogIntentSenderAdapterProof& senderAdapterProof,
    const NpcPerceptionDialogIntentEndpointResolutionOptions& options) {
  NpcPerceptionDialogIntentEndpointResolutionProof out;
  out.contractVersion = options.contractVersion;
  out.resolutionSource = options.resolutionSource;
  out.resolutionMode = options.resolutionMode;
  out.endpointResolverKind = options.endpointResolverKind;
  out.transportKind = senderAdapterProof.transportKind;
  out.recipientRouteKind = senderAdapterProof.recipientRouteKind;
  out.packetKind = senderAdapterProof.packetKind;

  out.actionQueueUuid = senderAdapterProof.actionQueueUuid;
  out.decisionUuid = senderAdapterProof.decisionUuid;
  out.actionKind = senderAdapterProof.actionKind;
  out.worldInstanceUuid = senderAdapterProof.worldInstanceUuid;
  out.sessionUuid = senderAdapterProof.sessionUuid;
  out.characterUuid = senderAdapterProof.characterUuid;
  out.npcEntityKey = senderAdapterProof.npcEntityKey;
  out.targetKey = senderAdapterProof.targetKey;
  out.perceptionKind = senderAdapterProof.perceptionKind;
  out.idempotencyKey = senderAdapterProof.idempotencyKey;

  out.targetSessionUuid = senderAdapterProof.targetSessionUuid;
  out.targetCharacterUuid = senderAdapterProof.targetCharacterUuid;
  out.routeLookupKey = senderAdapterProof.targetSessionUuid;
  out.packetSequence = senderAdapterProof.packetSequence;
  out.localSequence = senderAdapterProof.localSequence;
  out.encodedBytes = senderAdapterProof.encodedBytes;
  out.encodedPayloadBytes = senderAdapterProof.encodedPayloadBytes;
  out.maxDatagramBytes = senderAdapterProof.maxDatagramBytes;
  out.plannedDatagrams = senderAdapterProof.plannedDatagrams;

  out.dbMutated = false;
  out.routeLookupExecuted = false;
  out.endpointResolverExecuted = false;
  out.endpointResolved = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;
  out.requiresLiveEndpointResolver = true;

  out.senderAdapterProofed = senderAdapterProof.proofed;
  out.senderAdapterSafe = senderAdapterSafe(senderAdapterProof);
  out.hasTargetSession = !senderAdapterProof.targetSessionUuid.empty();
  out.hasTargetCharacter = !senderAdapterProof.targetCharacterUuid.empty();
  out.hasEncodedPayload = senderAdapterProof.hasEncodedPayload && senderAdapterProof.encodedPayloadBytes > 0U;
  out.fitsSingleDatagram = senderAdapterProof.fitsSingleDatagram && senderAdapterProof.plannedDatagrams == 1U;
  out.payloadBytesMatch = senderAdapterProof.payloadBytesMatch;
  out.sequenceNumbersMatch = senderAdapterProof.sequenceNumbersMatch;
  out.transportKindSupported = senderAdapterProof.transportKind == options.requiredTransportKind;
  out.recipientRouteKindSupported = senderAdapterProof.recipientRouteKind == options.requiredRecipientRouteKind;
  out.endpointLookupInputReady = out.hasTargetSession && out.hasTargetCharacter && out.hasEncodedPayload &&
                                 out.fitsSingleDatagram && out.transportKindSupported && out.recipientRouteKindSupported;

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.resolutionSource.empty()) {
    addIssue(out, "missing_resolution_source");
  }
  if(options.resolutionMode != "proof_only_no_endpoint_lookup") {
    addIssue(out, "resolution_mode_must_remain_proof_only_no_endpoint_lookup");
  }
  if(options.endpointResolverKind != "active_session_udp_endpoint_map") {
    addIssue(out, "unsupported_endpoint_resolver_kind");
  }
  if(options.requireSenderAdapterProofed && !senderAdapterProof.proofed) {
    addIssue(out, "sender_adapter_not_proofed");
  }
  if(!senderAdapterSafe(senderAdapterProof)) {
    addIssue(out, "sender_adapter_not_safe_for_endpoint_resolution");
  }
  if(options.requireLiveEndpointResolver && !senderAdapterProof.requiresLiveEndpointResolver) {
    addIssue(out, "sender_adapter_does_not_require_live_endpoint_resolver");
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
  if(!out.payloadBytesMatch) {
    addIssue(out, "encoded_payload_size_mismatch");
  }
  if(!out.sequenceNumbersMatch) {
    addIssue(out, "diagnostic_sequence_mismatch");
  }
  if(options.requireSingleDatagram && !out.fitsSingleDatagram) {
    addIssue(out, "diagnostic_packet_not_single_datagram_safe");
  }
  if(!out.transportKindSupported) {
    addIssue(out, "unsupported_transport_kind");
  }
  if(!out.recipientRouteKindSupported) {
    addIssue(out, "unsupported_recipient_route_kind");
  }
  if(options.requireNoRouteLookup && senderAdapterProof.routeLookupExecuted) {
    addIssue(out, "route_lookup_already_executed_upstream");
  }
  if(options.requireNoEndpointResolution && senderAdapterProof.endpointResolved) {
    addIssue(out, "endpoint_already_resolved_upstream");
  }
  if(senderAdapterProof.dbMutated || senderAdapterProof.sendExecuted || senderAdapterProof.packetFanoutExecuted ||
     senderAdapterProof.dialogUiExecuted || senderAdapterProof.audioExecuted || senderAdapterProof.markAppliedExecuted) {
    addIssue(out, "upstream_boundary_has_live_side_effect");
  }

  out.proofed = out.issues.empty();
  out.wouldResolveEndpointIfEnabled = out.proofed;
  out.status = out.proofed ? "endpoint_resolution_proofed_no_lookup" : "endpoint_resolution_rejected";
  return out;
}

std::string dialogIntentEndpointResolutionProofJson(
    const NpcPerceptionDialogIntentEndpointResolutionProof& proof) {
  std::string out;
  out.reserve(2048 + proof.issues.size() * 48);
  out.push_back('{');

  appendJsonBoolField(out, "proofed", proof.proofed);
  appendComma(out);
  appendJsonField(out, "status", proof.status);
  appendComma(out);
  appendJsonField(out, "contract_version", proof.contractVersion);
  appendComma(out);
  appendJsonField(out, "resolution_source", proof.resolutionSource);
  appendComma(out);
  appendJsonField(out, "resolution_mode", proof.resolutionMode);
  appendComma(out);
  appendJsonField(out, "endpoint_resolver_kind", proof.endpointResolverKind);
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
  appendJsonField(out, "route_lookup_key", proof.routeLookupKey);
  appendComma(out);
  appendJsonField(out, "endpoint_address", proof.endpointAddress);
  appendComma(out);
  appendJsonCountField(out, "endpoint_port", proof.endpointPort);
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
  appendJsonBoolField(out, "sender_adapter_proofed", proof.senderAdapterProofed);
  appendComma(out);
  appendJsonBoolField(out, "sender_adapter_safe", proof.senderAdapterSafe);
  appendComma(out);
  appendJsonBoolField(out, "requires_live_endpoint_resolver", proof.requiresLiveEndpointResolver);
  appendComma(out);
  appendJsonBoolField(out, "has_target_session", proof.hasTargetSession);
  appendComma(out);
  appendJsonBoolField(out, "has_target_character", proof.hasTargetCharacter);
  appendComma(out);
  appendJsonBoolField(out, "has_encoded_payload", proof.hasEncodedPayload);
  appendComma(out);
  appendJsonBoolField(out, "fits_single_datagram", proof.fitsSingleDatagram);
  appendComma(out);
  appendJsonBoolField(out, "payload_bytes_match", proof.payloadBytesMatch);
  appendComma(out);
  appendJsonBoolField(out, "sequence_numbers_match", proof.sequenceNumbersMatch);
  appendComma(out);
  appendJsonBoolField(out, "transport_kind_supported", proof.transportKindSupported);
  appendComma(out);
  appendJsonBoolField(out, "recipient_route_kind_supported", proof.recipientRouteKindSupported);
  appendComma(out);
  appendJsonBoolField(out, "endpoint_lookup_input_ready", proof.endpointLookupInputReady);
  appendComma(out);
  appendJsonBoolField(out, "would_resolve_endpoint_if_enabled", proof.wouldResolveEndpointIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", proof.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "route_lookup_executed", proof.routeLookupExecuted);
  appendComma(out);
  appendJsonBoolField(out, "endpoint_resolver_executed", proof.endpointResolverExecuted);
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
