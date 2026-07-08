#include "mmo_npc_perception_dialog_intent_fanout_plan.h"

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

void addIssue(NpcPerceptionDialogIntentFanoutPlan& plan, std::string issue) {
  plan.issues.push_back(std::move(issue));
}

[[nodiscard]] bool supportedActionKind(std::string_view actionKind) noexcept {
  return actionKind == "npc_greet_player" || actionKind == "npc_warn_player";
}

[[nodiscard]] bool diagnosticEncodingSafe(
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding) noexcept {
  return diagnosticEncoding.encoded && diagnosticEncoding.fitsDatagram && diagnosticEncoding.decodedRoundTrip &&
         diagnosticEncoding.decodedFieldsMatch && !diagnosticEncoding.sendExecuted &&
         !diagnosticEncoding.packetFanoutExecuted && !diagnosticEncoding.dialogUiExecuted &&
         !diagnosticEncoding.audioExecuted && !diagnosticEncoding.markAppliedExecuted;
}

} // namespace

bool supportsNpcPerceptionDialogIntentFanoutPlan(
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidence& durableEvidence) noexcept {
  return diagnosticEncodingSafe(diagnosticEncoding) && durableEvidence.built && durableEvidence.written &&
         !durableEvidence.dbMutated && !durableEvidence.liveDispatchExecuted && !durableEvidence.sendExecuted &&
         !durableEvidence.packetFanoutExecuted && !durableEvidence.dialogUiExecuted && !durableEvidence.audioExecuted &&
         !durableEvidence.markAppliedExecuted;
}

NpcPerceptionDialogIntentFanoutPlan buildNpcPerceptionDialogIntentFanoutPlan(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation,
    const NpcPerceptionTypedEffectDescriptor& typedEffect,
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacket& diagnosticPacket,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidence& durableEvidence,
    const NpcPerceptionDialogIntentFanoutPlanOptions& options) {
  NpcPerceptionDialogIntentFanoutPlan out;
  out.contractVersion = options.contractVersion;
  out.fanoutSource = options.fanoutSource;
  out.fanoutMode = options.fanoutMode;
  out.recipientKind = options.recipientKind;
  out.packetKind = diagnosticEncoding.packetKind;

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

  out.targetSessionUuid = action.sessionUuid;
  out.targetCharacterUuid = action.characterUuid;
  out.packetSequence = diagnosticEncoding.packetSequence;
  out.localSequence = diagnosticEncoding.localSequence;
  out.encodedBytes = diagnosticEncoding.encodedBytes;
  out.maxDatagramBytes = diagnosticEncoding.maxDatagramBytes;
  out.plannedDatagrams = diagnosticEncoding.encodedBytes > 0 ? 1U : 0U;
  out.fitsSingleDatagram = diagnosticEncoding.fitsDatagram;
  out.durableEvidenceWritten = durableEvidence.written;
  out.diagnosticEncodingSafe = diagnosticEncodingSafe(diagnosticEncoding);

  out.dbMutated = false;
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
  if(!diagnosticEncodingSafe(diagnosticEncoding)) {
    addIssue(out, "diagnostic_encoding_not_safe_for_fanout_plan");
  }
  if(!supportedActionKind(action.actionKind)) {
    addIssue(out, "unsupported_action_kind_for_fanout_plan");
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
  if(action.sessionUuid.empty()) {
    addIssue(out, "missing_target_session_uuid");
  }
  if(action.characterUuid.empty()) {
    addIssue(out, "missing_target_character_uuid");
  }
  if(action.targetKey.empty()) {
    addIssue(out, "missing_target_key");
  }
  if(action.idempotencyKey.empty()) {
    addIssue(out, "missing_idempotency_key");
  }
  if(durableEvidence.dbMutated || durableEvidence.liveDispatchExecuted || durableEvidence.sendExecuted ||
     durableEvidence.packetFanoutExecuted || durableEvidence.dialogUiExecuted || durableEvidence.audioExecuted ||
     durableEvidence.markAppliedExecuted) {
    addIssue(out, "durable_evidence_has_live_side_effect");
  }
  if(options.requireDurableEvidenceWritten && !durableEvidence.written) {
    addIssue(out, "durable_evidence_jsonl_not_written");
  }

  out.built = out.issues.empty();
  out.status = out.built ? "client_fanout_plan_built_no_send" : "client_fanout_plan_rejected";
  return out;
}

std::string dialogIntentFanoutPlanJson(const NpcPerceptionDialogIntentFanoutPlan& plan) {
  std::string out;
  out.reserve(1536 + plan.issues.size() * 48);
  out.push_back('{');

  appendJsonBoolField(out, "built", plan.built);
  appendComma(out);
  appendJsonField(out, "status", plan.status);
  appendComma(out);
  appendJsonField(out, "contract_version", plan.contractVersion);
  appendComma(out);
  appendJsonField(out, "fanout_source", plan.fanoutSource);
  appendComma(out);
  appendJsonField(out, "fanout_mode", plan.fanoutMode);
  appendComma(out);
  appendJsonField(out, "recipient_kind", plan.recipientKind);
  appendComma(out);
  appendJsonField(out, "packet_kind", plan.packetKind);
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
  appendJsonCountField(out, "packet_sequence", plan.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", plan.localSequence);
  appendComma(out);
  appendJsonCountField(out, "encoded_bytes", plan.encodedBytes);
  appendComma(out);
  appendJsonCountField(out, "max_datagram_bytes", plan.maxDatagramBytes);
  appendComma(out);
  appendJsonCountField(out, "planned_datagrams", plan.plannedDatagrams);
  appendComma(out);
  appendJsonBoolField(out, "fits_single_datagram", plan.fitsSingleDatagram);
  appendComma(out);
  appendJsonBoolField(out, "durable_evidence_written", plan.durableEvidenceWritten);
  appendComma(out);
  appendJsonBoolField(out, "diagnostic_encoding_safe", plan.diagnosticEncodingSafe);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", plan.dbMutated);
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
  appendJsonCountField(out, "issue_count", plan.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  bool first = true;
  for(const auto& issue : plan.issues) {
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
