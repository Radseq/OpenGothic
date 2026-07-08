#include "mmo_npc_perception_dialog_intent_diagnostic_packet.h"

#include <cctype>
#include <string>
#include <utility>

namespace Mmo::AiRuntime {
namespace {

[[nodiscard]] std::string trimCopy(std::string_view text) {
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return std::string(text);
}

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

void appendJsonCountField(std::string& out, std::string_view key, std::size_t value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void addIssue(NpcPerceptionDialogIntentDiagnosticPacket& packet, std::string issue) {
  packet.issues.push_back(std::move(issue));
}

void copyPreviewFields(
    NpcPerceptionDialogIntentDiagnosticPacket& out,
    const NpcPerceptionDialogIntentPreview& preview) {
  out.previewKind = preview.previewKind;
  out.effectFamily = preview.effectFamily;
  out.effectKind = preview.effectKind;
  out.intentKind = preview.intentKind;
  out.actionKind = preview.actionKind;
  out.actionQueueUuid = preview.actionQueueUuid;
  out.decisionUuid = preview.decisionUuid;
  out.worldInstanceUuid = preview.worldInstanceUuid;
  out.sessionUuid = preview.sessionUuid;
  out.characterUuid = preview.characterUuid;
  out.npcEntityKey = preview.npcEntityKey;
  out.targetKey = preview.targetKey;
  out.perceptionKind = preview.perceptionKind;
  out.idempotencyKey = preview.idempotencyKey;
}

[[nodiscard]] std::string buildMessageText(const NpcPerceptionDialogIntentDiagnosticPacket& packet) {
  std::string out;
  out.reserve(512);
  out += "ai dialog intent preview";
  out += " kind=" + packet.previewKind;
  out += " action=" + packet.actionKind;
  out += " intent=" + packet.intentKind;
  out += " world_instance_uuid=" + packet.worldInstanceUuid;
  out += " npc_entity_key=" + packet.npcEntityKey;
  out += " target_key=" + packet.targetKey;
  out += " decision_uuid=" + packet.decisionUuid;
  out += " action_queue_uuid=" + packet.actionQueueUuid;
  out += " packet_fanout_executed=false";
  out += " dialog_ui_executed=false";
  out += " audio_executed=false";
  out += " mark_applied_executed=false";
  return out;
}

} // namespace

bool supportsNpcPerceptionDialogIntentDiagnosticPacket(
    const NpcPerceptionDialogIntentPreview& preview) noexcept {
  return preview.previewed && preview.effectFamily == "dialog_intent" &&
         (preview.previewKind == "dialog_greeting_preview" || preview.previewKind == "dialog_warning_preview") &&
         !preview.liveDispatchExecuted && !preview.markAppliedExecuted && !preview.packetFanoutExecuted;
}

NpcPerceptionDialogIntentDiagnosticPacket buildNpcPerceptionDialogIntentDiagnosticPacket(
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacketOptions& options) {
  NpcPerceptionDialogIntentDiagnosticPacket out;
  out.contractVersion = options.contractVersion;
  out.packetKind = options.packetKind;
  out.diagnosticAction = options.diagnosticAction;
  out.diagnosticReason = options.diagnosticReason;
  out.diagnosticSource = options.diagnosticSource;
  out.severity = options.severity;
  copyPreviewFields(out, preview);

  out.liveDispatchExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;

  if(trimCopy(out.contractVersion).empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(trimCopy(out.packetKind).empty()) {
    addIssue(out, "missing_packet_kind");
  }
  if(trimCopy(out.diagnosticAction).empty()) {
    addIssue(out, "missing_diagnostic_action");
  }
  if(trimCopy(out.diagnosticReason).empty()) {
    addIssue(out, "missing_diagnostic_reason");
  }
  if(trimCopy(out.diagnosticSource).empty()) {
    addIssue(out, "missing_diagnostic_source");
  }
  if(!preview.previewed) {
    addIssue(out, "dialog_intent_preview_not_previewed");
  }
  if(preview.effectFamily != "dialog_intent") {
    addIssue(out, "unsupported_preview_effect_family");
  }
  if(preview.previewKind != "dialog_greeting_preview" && preview.previewKind != "dialog_warning_preview") {
    addIssue(out, "unsupported_preview_kind_for_diagnostic_packet");
  }
  if(preview.liveDispatchExecuted || preview.markAppliedExecuted || preview.packetFanoutExecuted) {
    addIssue(out, "preview_already_executed_live_side_effect");
  }
  if(trimCopy(out.actionQueueUuid).empty()) {
    addIssue(out, "missing_action_queue_uuid");
  }
  if(trimCopy(out.decisionUuid).empty()) {
    addIssue(out, "missing_decision_uuid");
  }
  if(trimCopy(out.worldInstanceUuid).empty()) {
    addIssue(out, "missing_world_instance_uuid");
  }
  if(trimCopy(out.npcEntityKey).empty()) {
    addIssue(out, "missing_npc_entity_key");
  }
  if(trimCopy(out.targetKey).empty()) {
    addIssue(out, "missing_target_key");
  }
  if(trimCopy(out.perceptionKind).empty()) {
    addIssue(out, "missing_perception_kind");
  }
  if(trimCopy(out.idempotencyKey).empty()) {
    addIssue(out, "missing_idempotency_key");
  }

  out.built = out.issues.empty();
  out.status = out.built ? "diagnostic_packet_contract_only_no_fanout" : "diagnostic_packet_rejected";
  if(out.built) {
    out.messageText = buildMessageText(out);
  }
  return out;
}

std::string dialogIntentDiagnosticPacketJson(const NpcPerceptionDialogIntentDiagnosticPacket& packet) {
  std::string out;
  out.reserve(1536 + packet.issues.size() * 48 + packet.messageText.size());
  out.push_back('{');

  appendJsonBoolField(out, "built", packet.built);
  appendComma(out);
  appendJsonField(out, "status", packet.status);
  appendComma(out);
  appendJsonField(out, "contract_version", packet.contractVersion);
  appendComma(out);
  appendJsonField(out, "packet_kind", packet.packetKind);
  appendComma(out);
  appendJsonCountField(out, "severity", packet.severity);
  appendComma(out);
  appendJsonField(out, "diagnostic_action", packet.diagnosticAction);
  appendComma(out);
  appendJsonField(out, "diagnostic_reason", packet.diagnosticReason);
  appendComma(out);
  appendJsonField(out, "diagnostic_source", packet.diagnosticSource);
  appendComma(out);
  appendJsonField(out, "message_text", packet.messageText);
  appendComma(out);
  appendJsonField(out, "preview_kind", packet.previewKind);
  appendComma(out);
  appendJsonField(out, "effect_family", packet.effectFamily);
  appendComma(out);
  appendJsonField(out, "effect_kind", packet.effectKind);
  appendComma(out);
  appendJsonField(out, "intent_kind", packet.intentKind);
  appendComma(out);
  appendJsonField(out, "action_kind", packet.actionKind);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", packet.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", packet.decisionUuid);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", packet.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", packet.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", packet.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", packet.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", packet.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", packet.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", packet.idempotencyKey);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_executed", packet.liveDispatchExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", packet.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", packet.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", packet.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", packet.markAppliedExecuted);
  appendComma(out);
  appendJsonCountField(out, "issue_count", packet.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  for(std::size_t i = 0; i < packet.issues.size(); ++i) {
    if(i != 0) {
      out.push_back(',');
    }
    out += jsonEscape(packet.issues[i]);
  }
  out += "]}";
  return out;
}

} // namespace Mmo::AiRuntime
