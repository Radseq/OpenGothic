#include "mmo_npc_perception_dialog_intent_durable_evidence.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
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

void addIssue(NpcPerceptionDialogIntentDurableEvidence& evidence, std::string issue) {
  evidence.issues.push_back(std::move(issue));
}

[[nodiscard]] bool supportedActionKind(std::string_view actionKind) noexcept {
  return actionKind == "npc_greet_player" || actionKind == "npc_warn_player";
}

[[nodiscard]] std::string buildEvidenceRecordJson(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation,
    const NpcPerceptionTypedEffectDescriptor& typedEffect,
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacket& diagnosticPacket,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidence& evidence) {
  std::string out;
  out.reserve(2048 + diagnosticEncoding.message.size());
  out.push_back('{');

  appendJsonField(out, "contract_version", evidence.contractVersion);
  appendComma(out);
  appendJsonField(out, "evidence_kind", evidence.evidenceKind);
  appendComma(out);
  appendJsonField(out, "evidence_source", evidence.evidenceSource);
  appendComma(out);
  appendJsonField(out, "evidence_mode", evidence.evidenceMode);
  appendComma(out);
  appendJsonField(out, "record_status", "durable_preview_evidence_record_no_dispatch");
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", action.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", action.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", action.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", action.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", action.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", action.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", typedEffect.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", action.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", typedEffect.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", action.idempotencyKey);
  appendComma(out);
  appendJsonField(out, "dispatch_validation_status", validation.status);
  appendComma(out);
  appendJsonField(out, "typed_effect_status", typedEffect.status);
  appendComma(out);
  appendJsonField(out, "preview_status", preview.status);
  appendComma(out);
  appendJsonField(out, "diagnostic_packet_status", diagnosticPacket.status);
  appendComma(out);
  appendJsonField(out, "diagnostic_encoding_status", diagnosticEncoding.status);
  appendComma(out);
  appendJsonCountField(out, "diagnostic_packet_sequence", diagnosticEncoding.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "diagnostic_local_sequence", diagnosticEncoding.localSequence);
  appendComma(out);
  appendJsonCountField(out, "diagnostic_severity", diagnosticEncoding.severity);
  appendComma(out);
  appendJsonField(out, "mapped_action_kind", diagnosticEncoding.actionKind);
  appendComma(out);
  appendJsonField(out, "mapped_reason", diagnosticEncoding.reason);
  appendComma(out);
  appendJsonField(out, "mapped_message", diagnosticEncoding.message);
  appendComma(out);
  appendJsonCountField(out, "encoded_bytes", diagnosticEncoding.encodedBytes);
  appendComma(out);
  appendJsonBoolField(out, "fits_datagram", diagnosticEncoding.fitsDatagram);
  appendComma(out);
  appendJsonBoolField(out, "decoded_round_trip", diagnosticEncoding.decodedRoundTrip);
  appendComma(out);
  appendJsonBoolField(out, "decoded_fields_match", diagnosticEncoding.decodedFieldsMatch);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", false);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_executed", false);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", false);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", false);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", false);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", false);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", false);
  out.push_back('}');
  return out;
}

} // namespace

bool supportsNpcPerceptionDialogIntentDurableEvidence(
    const NpcPerceptionDialogIntentDiagnosticEncoding& encoding) noexcept {
  return encoding.encoded && supportedActionKind(encoding.actionKind) && encoding.fitsDatagram &&
         encoding.decodedRoundTrip && encoding.decodedFieldsMatch && !encoding.sendExecuted &&
         !encoding.packetFanoutExecuted && !encoding.dialogUiExecuted && !encoding.audioExecuted &&
         !encoding.markAppliedExecuted;
}

NpcPerceptionDialogIntentDurableEvidence buildNpcPerceptionDialogIntentDurableEvidence(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation,
    const NpcPerceptionTypedEffectDescriptor& typedEffect,
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacket& diagnosticPacket,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidenceOptions& options) {
  NpcPerceptionDialogIntentDurableEvidence out;
  out.contractVersion = options.contractVersion;
  out.evidenceKind = options.evidenceKind;
  out.evidenceSource = options.evidenceSource;
  out.evidenceMode = options.evidenceMode;
  out.jsonlPath = options.jsonlPath.generic_string();

  out.actionQueueUuid = action.actionQueueUuid;
  out.decisionUuid = action.decisionUuid;
  out.actionKind = action.actionKind;
  out.worldInstanceUuid = action.worldInstanceUuid;
  out.sessionUuid = action.sessionUuid;
  out.characterUuid = action.characterUuid;
  out.npcEntityKey = typedEffect.npcEntityKey;
  out.targetKey = action.targetKey;
  out.perceptionKind = typedEffect.perceptionKind;
  out.idempotencyKey = action.idempotencyKey;
  out.typedEffectStatus = typedEffect.status;
  out.previewStatus = preview.status;
  out.diagnosticPacketStatus = diagnosticPacket.status;
  out.diagnosticEncodingStatus = diagnosticEncoding.status;
  out.encodedBytes = diagnosticEncoding.encodedBytes;
  out.fitsDatagram = diagnosticEncoding.fitsDatagram;
  out.decodedRoundTrip = diagnosticEncoding.decodedRoundTrip;
  out.decodedFieldsMatch = diagnosticEncoding.decodedFieldsMatch;

  out.dbMutated = false;
  out.liveDispatchExecuted = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;

  if(!action.claimed) {
    addIssue(out, "action_not_claimed");
  }
  if(!validation.accepted) {
    addIssue(out, "dispatch_contract_not_accepted");
  }
  if(!typedEffect.described) {
    addIssue(out, "typed_effect_not_described");
  }
  if(!preview.previewed) {
    addIssue(out, "dialog_intent_preview_not_built");
  }
  if(!diagnosticPacket.built) {
    addIssue(out, "diagnostic_packet_contract_not_built");
  }
  if(!diagnosticEncoding.encoded) {
    addIssue(out, "diagnostic_encoding_not_built");
  }
  if(!supportedActionKind(action.actionKind)) {
    addIssue(out, "unsupported_action_kind_for_durable_preview_evidence");
  }
  if(action.actionQueueUuid.empty()) {
    addIssue(out, "missing_action_queue_uuid");
  }
  if(action.decisionUuid.empty()) {
    addIssue(out, "missing_decision_uuid");
  }
  if(action.worldInstanceUuid.empty()) {
    addIssue(out, "missing_world_instance_uuid");
  }
  if(action.targetKey.empty()) {
    addIssue(out, "missing_target_key");
  }
  if(action.idempotencyKey.empty()) {
    addIssue(out, "missing_idempotency_key");
  }
  if(!supportsNpcPerceptionDialogIntentDurableEvidence(diagnosticEncoding)) {
    addIssue(out, "diagnostic_encoding_not_safe_for_durable_preview_evidence");
  }

  if(out.issues.empty()) {
    out.jsonRecord = buildEvidenceRecordJson(action, validation, typedEffect, preview, diagnosticPacket, diagnosticEncoding, out);
    out.jsonBytes = out.jsonRecord.size();
    out.built = true;
    out.status = "built_not_written";
  } else {
    out.status = "evidence_rejected";
  }
  return out;
}

NpcPerceptionDialogIntentDurableEvidence writeNpcPerceptionDialogIntentDurableEvidenceJsonl(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation,
    const NpcPerceptionTypedEffectDescriptor& typedEffect,
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacket& diagnosticPacket,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidenceOptions& options) {
  auto out = buildNpcPerceptionDialogIntentDurableEvidence(
      action,
      validation,
      typedEffect,
      preview,
      diagnosticPacket,
      diagnosticEncoding,
      options);

  if(!out.built) {
    return out;
  }
  if(options.jsonlPath.empty()) {
    addIssue(out, "missing_evidence_jsonl_path");
    out.status = "evidence_rejected";
    out.built = false;
    return out;
  }
  if(!options.allowFileWrite) {
    addIssue(out, "evidence_file_write_not_explicitly_allowed");
    out.status = "evidence_rejected";
    out.built = false;
    return out;
  }

  if(options.jsonlPath.has_parent_path()) {
    std::filesystem::create_directories(options.jsonlPath.parent_path());
  }
  std::ofstream file(options.jsonlPath, std::ios::binary | std::ios::app);
  if(!file) {
    addIssue(out, "failed_to_open_evidence_jsonl_path");
    out.status = "evidence_write_failed";
    return out;
  }
  file << out.jsonRecord << '\n';
  if(!file) {
    addIssue(out, "failed_to_write_evidence_jsonl_record");
    out.status = "evidence_write_failed";
    return out;
  }
  out.written = true;
  out.writtenBytes = out.jsonRecord.size() + 1U;
  out.status = "written_jsonl_no_dispatch";
  return out;
}

std::string dialogIntentDurableEvidenceJson(
    const NpcPerceptionDialogIntentDurableEvidence& evidence) {
  std::string out;
  out.reserve(1536 + evidence.issues.size() * 48 + evidence.jsonlPath.size());
  out.push_back('{');

  appendJsonBoolField(out, "built", evidence.built);
  appendComma(out);
  appendJsonBoolField(out, "written", evidence.written);
  appendComma(out);
  appendJsonField(out, "status", evidence.status);
  appendComma(out);
  appendJsonField(out, "contract_version", evidence.contractVersion);
  appendComma(out);
  appendJsonField(out, "evidence_kind", evidence.evidenceKind);
  appendComma(out);
  appendJsonField(out, "evidence_source", evidence.evidenceSource);
  appendComma(out);
  appendJsonField(out, "evidence_mode", evidence.evidenceMode);
  appendComma(out);
  appendJsonField(out, "jsonl_path", evidence.jsonlPath);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", evidence.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", evidence.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", evidence.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", evidence.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", evidence.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", evidence.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", evidence.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", evidence.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", evidence.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", evidence.idempotencyKey);
  appendComma(out);
  appendJsonField(out, "typed_effect_status", evidence.typedEffectStatus);
  appendComma(out);
  appendJsonField(out, "preview_status", evidence.previewStatus);
  appendComma(out);
  appendJsonField(out, "diagnostic_packet_status", evidence.diagnosticPacketStatus);
  appendComma(out);
  appendJsonField(out, "diagnostic_encoding_status", evidence.diagnosticEncodingStatus);
  appendComma(out);
  appendJsonCountField(out, "encoded_bytes", evidence.encodedBytes);
  appendComma(out);
  appendJsonBoolField(out, "fits_datagram", evidence.fitsDatagram);
  appendComma(out);
  appendJsonBoolField(out, "decoded_round_trip", evidence.decodedRoundTrip);
  appendComma(out);
  appendJsonBoolField(out, "decoded_fields_match", evidence.decodedFieldsMatch);
  appendComma(out);
  appendJsonCountField(out, "json_bytes", evidence.jsonBytes);
  appendComma(out);
  appendJsonCountField(out, "written_bytes", evidence.writtenBytes);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", evidence.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_executed", evidence.liveDispatchExecuted);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", evidence.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", evidence.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", evidence.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", evidence.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", evidence.markAppliedExecuted);
  appendComma(out);
  appendJsonCountField(out, "issue_count", evidence.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  bool first = true;
  for(const auto& issue : evidence.issues) {
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
