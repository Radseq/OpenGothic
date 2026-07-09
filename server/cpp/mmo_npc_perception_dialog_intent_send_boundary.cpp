#include "mmo_npc_perception_dialog_intent_send_boundary.h"

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

void addIssue(NpcPerceptionDialogIntentSendBoundary& boundary, std::string issue) {
  boundary.issues.push_back(std::move(issue));
}

[[nodiscard]] bool diagnosticEncodingSafe(
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding) noexcept {
  return diagnosticEncoding.encoded && diagnosticEncoding.fitsDatagram && diagnosticEncoding.decodedRoundTrip &&
         diagnosticEncoding.decodedFieldsMatch && !diagnosticEncoding.sendExecuted &&
         !diagnosticEncoding.packetFanoutExecuted && !diagnosticEncoding.dialogUiExecuted &&
         !diagnosticEncoding.audioExecuted && !diagnosticEncoding.markAppliedExecuted;
}

[[nodiscard]] bool fanoutPlanSafe(const NpcPerceptionDialogIntentFanoutPlan& fanoutPlan) noexcept {
  return fanoutPlan.built && !fanoutPlan.dbMutated && !fanoutPlan.sendExecuted &&
         !fanoutPlan.packetFanoutExecuted && !fanoutPlan.dialogUiExecuted && !fanoutPlan.audioExecuted &&
         !fanoutPlan.markAppliedExecuted && fanoutPlan.durableEvidenceWritten && fanoutPlan.diagnosticEncodingSafe;
}

} // namespace

bool supportsNpcPerceptionDialogIntentSendBoundary(
    const NpcPerceptionDialogIntentFanoutPlan& fanoutPlan,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding) noexcept {
  return fanoutPlanSafe(fanoutPlan) && diagnosticEncodingSafe(diagnosticEncoding) &&
         !diagnosticEncoding.encodedBytesBuffer.empty() && !fanoutPlan.targetSessionUuid.empty() &&
         fanoutPlan.plannedDatagrams == 1U && fanoutPlan.fitsSingleDatagram;
}

NpcPerceptionDialogIntentSendBoundary prepareNpcPerceptionDialogIntentSendBoundary(
    const NpcPerceptionDialogIntentFanoutPlan& fanoutPlan,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentSendBoundaryOptions& options) {
  NpcPerceptionDialogIntentSendBoundary out;
  out.contractVersion = options.contractVersion;
  out.sendSource = options.sendSource;
  out.sendMode = options.sendMode;
  out.transportKind = options.transportKind;
  out.packetKind = fanoutPlan.packetKind.empty() ? diagnosticEncoding.packetKind : fanoutPlan.packetKind;

  out.actionQueueUuid = fanoutPlan.actionQueueUuid;
  out.decisionUuid = fanoutPlan.decisionUuid;
  out.actionKind = fanoutPlan.actionKind;
  out.worldInstanceUuid = fanoutPlan.worldInstanceUuid;
  out.sessionUuid = fanoutPlan.sessionUuid;
  out.characterUuid = fanoutPlan.characterUuid;
  out.npcEntityKey = fanoutPlan.npcEntityKey;
  out.targetKey = fanoutPlan.targetKey;
  out.perceptionKind = fanoutPlan.perceptionKind;
  out.idempotencyKey = fanoutPlan.idempotencyKey;

  out.targetSessionUuid = fanoutPlan.targetSessionUuid;
  out.targetCharacterUuid = fanoutPlan.targetCharacterUuid;
  out.packetSequence = diagnosticEncoding.packetSequence;
  out.localSequence = diagnosticEncoding.localSequence;
  out.encodedBytes = diagnosticEncoding.encodedBytes;
  out.encodedPayloadBytes = diagnosticEncoding.encodedBytesBuffer.size();
  out.maxDatagramBytes = diagnosticEncoding.maxDatagramBytes;
  out.plannedDatagrams = fanoutPlan.plannedDatagrams;
  out.fanoutPlanBuilt = fanoutPlan.built;
  out.diagnosticEncodingSafe = diagnosticEncodingSafe(diagnosticEncoding);
  out.hasEncodedPayload = !diagnosticEncoding.encodedBytesBuffer.empty();
  out.hasTargetSession = !fanoutPlan.targetSessionUuid.empty();
  out.fitsSingleDatagram = fanoutPlan.fitsSingleDatagram && diagnosticEncoding.fitsDatagram;

  out.dbMutated = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;
  out.sendBlockedByDesign = true;

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.sendSource.empty()) {
    addIssue(out, "missing_send_source");
  }
  if(options.sendMode != "prepare_only_no_send") {
    addIssue(out, "send_mode_must_remain_prepare_only_no_send");
  }
  if(options.transportKind != "udp_server_diagnostic_packet") {
    addIssue(out, "unsupported_transport_kind");
  }
  if(options.requireFanoutPlanBuilt && !fanoutPlan.built) {
    addIssue(out, "fanout_plan_not_built");
  }
  if(!fanoutPlanSafe(fanoutPlan)) {
    addIssue(out, "fanout_plan_not_safe_for_send_boundary");
  }
  if(!diagnosticEncodingSafe(diagnosticEncoding)) {
    addIssue(out, "diagnostic_encoding_not_safe_for_send_boundary");
  }
  if(!out.hasEncodedPayload) {
    addIssue(out, "missing_encoded_payload_bytes");
  }
  if(out.encodedBytes == 0U) {
    addIssue(out, "encoded_byte_count_is_zero");
  }
  if(out.encodedPayloadBytes != out.encodedBytes) {
    addIssue(out, "encoded_payload_size_mismatch");
  }
  if(!out.hasTargetSession) {
    addIssue(out, "missing_target_session_uuid");
  }
  if(fanoutPlan.targetCharacterUuid.empty()) {
    addIssue(out, "missing_target_character_uuid");
  }
  if(fanoutPlan.actionQueueUuid.empty()) {
    addIssue(out, "missing_action_queue_uuid");
  }
  if(fanoutPlan.decisionUuid.empty()) {
    addIssue(out, "missing_decision_uuid");
  }
  if(fanoutPlan.worldInstanceUuid.empty()) {
    addIssue(out, "missing_world_instance_uuid");
  }
  if(fanoutPlan.idempotencyKey.empty()) {
    addIssue(out, "missing_idempotency_key");
  }
  if(options.requireSingleDatagram && (!out.fitsSingleDatagram || fanoutPlan.plannedDatagrams != 1U)) {
    addIssue(out, "diagnostic_packet_not_single_datagram_safe");
  }
  if(fanoutPlan.dbMutated || fanoutPlan.sendExecuted || fanoutPlan.packetFanoutExecuted || fanoutPlan.dialogUiExecuted ||
     fanoutPlan.audioExecuted || fanoutPlan.markAppliedExecuted || diagnosticEncoding.sendExecuted ||
     diagnosticEncoding.packetFanoutExecuted || diagnosticEncoding.dialogUiExecuted || diagnosticEncoding.audioExecuted ||
     diagnosticEncoding.markAppliedExecuted) {
    addIssue(out, "upstream_boundary_has_live_side_effect");
  }

  out.prepared = out.issues.empty();
  out.wouldSendIfEnabled = out.prepared;
  out.status = out.prepared ? "send_boundary_prepared_no_send" : "send_boundary_rejected";
  return out;
}

std::string dialogIntentSendBoundaryJson(const NpcPerceptionDialogIntentSendBoundary& boundary) {
  std::string out;
  out.reserve(1536 + boundary.issues.size() * 48);
  out.push_back('{');

  appendJsonBoolField(out, "prepared", boundary.prepared);
  appendComma(out);
  appendJsonField(out, "status", boundary.status);
  appendComma(out);
  appendJsonField(out, "contract_version", boundary.contractVersion);
  appendComma(out);
  appendJsonField(out, "send_source", boundary.sendSource);
  appendComma(out);
  appendJsonField(out, "send_mode", boundary.sendMode);
  appendComma(out);
  appendJsonField(out, "transport_kind", boundary.transportKind);
  appendComma(out);
  appendJsonField(out, "packet_kind", boundary.packetKind);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", boundary.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", boundary.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", boundary.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", boundary.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", boundary.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", boundary.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", boundary.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", boundary.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", boundary.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", boundary.idempotencyKey);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", boundary.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", boundary.targetCharacterUuid);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", boundary.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", boundary.localSequence);
  appendComma(out);
  appendJsonCountField(out, "encoded_bytes", boundary.encodedBytes);
  appendComma(out);
  appendJsonCountField(out, "encoded_payload_bytes", boundary.encodedPayloadBytes);
  appendComma(out);
  appendJsonCountField(out, "max_datagram_bytes", boundary.maxDatagramBytes);
  appendComma(out);
  appendJsonCountField(out, "planned_datagrams", boundary.plannedDatagrams);
  appendComma(out);
  appendJsonBoolField(out, "fanout_plan_built", boundary.fanoutPlanBuilt);
  appendComma(out);
  appendJsonBoolField(out, "diagnostic_encoding_safe", boundary.diagnosticEncodingSafe);
  appendComma(out);
  appendJsonBoolField(out, "has_encoded_payload", boundary.hasEncodedPayload);
  appendComma(out);
  appendJsonBoolField(out, "has_target_session", boundary.hasTargetSession);
  appendComma(out);
  appendJsonBoolField(out, "fits_single_datagram", boundary.fitsSingleDatagram);
  appendComma(out);
  appendJsonBoolField(out, "send_blocked_by_design", boundary.sendBlockedByDesign);
  appendComma(out);
  appendJsonBoolField(out, "would_send_if_enabled", boundary.wouldSendIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", boundary.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", boundary.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", boundary.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", boundary.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", boundary.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", boundary.markAppliedExecuted);
  appendComma(out);
  appendJsonCountField(out, "issue_count", boundary.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  bool first = true;
  for(const auto& issue : boundary.issues) {
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
