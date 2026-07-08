#include "mmo_npc_perception_dialog_intent_preview.h"

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

void addIssue(NpcPerceptionDialogIntentPreview& preview, std::string issue) {
  preview.issues.push_back(std::move(issue));
}

void copyDescriptorFields(
    NpcPerceptionDialogIntentPreview& out,
    const NpcPerceptionTypedEffectDescriptor& descriptor) {
  out.effectFamily = descriptor.effectFamily;
  out.effectKind = descriptor.effectKind;
  out.intentKind = descriptor.intentKind;
  out.actionKind = descriptor.actionKind;
  out.actionQueueUuid = descriptor.actionQueueUuid;
  out.decisionUuid = descriptor.decisionUuid;
  out.worldInstanceUuid = descriptor.worldInstanceUuid;
  out.sessionUuid = descriptor.sessionUuid;
  out.characterUuid = descriptor.characterUuid;
  out.npcEntityKey = descriptor.npcEntityKey;
  out.targetKey = descriptor.targetKey;
  out.perceptionKind = descriptor.perceptionKind;
  out.idempotencyKey = descriptor.idempotencyKey;
  out.requiresDialogUi = descriptor.requiresDialogUi;
  out.requiresAudio = descriptor.requiresAudio;
  out.requiresNpcTurn = descriptor.requiresNpcTurn;
  out.requiresNpcMovement = descriptor.requiresNpcMovement;
  out.requiresCombat = descriptor.requiresCombat;
}

[[nodiscard]] std::string previewKindForIntent(std::string_view intentKind) {
  if(intentKind == "greet_player") {
    return "dialog_greeting_preview";
  }
  if(intentKind == "warn_player") {
    return "dialog_warning_preview";
  }
  return {};
}

[[nodiscard]] std::string buildLogLine(const NpcPerceptionDialogIntentPreview& preview) {
  std::string out;
  out.reserve(512);
  out += "npc_dialog_intent_preview";
  out += " mode=" + preview.previewMode;
  out += " kind=" + preview.previewKind;
  out += " action=" + preview.actionKind;
  out += " intent=" + preview.intentKind;
  out += " world_instance_uuid=" + preview.worldInstanceUuid;
  out += " npc_entity_key=" + preview.npcEntityKey;
  out += " target_key=" + preview.targetKey;
  out += " decision_uuid=" + preview.decisionUuid;
  out += " action_queue_uuid=" + preview.actionQueueUuid;
  out += " live_dispatch_executed=false";
  out += " mark_applied_executed=false";
  out += " packet_fanout_executed=false";
  return out;
}

} // namespace

bool supportsNpcPerceptionDialogIntentPreview(const NpcPerceptionTypedEffectDescriptor& descriptor) noexcept {
  return descriptor.described && descriptor.effectFamily == "dialog_intent" &&
         (descriptor.intentKind == "greet_player" || descriptor.intentKind == "warn_player") &&
         !descriptor.liveDispatchImplemented && !descriptor.liveDispatchAllowed;
}

NpcPerceptionDialogIntentPreview previewNpcPerceptionDialogIntent(
    const NpcPerceptionTypedEffectDescriptor& descriptor,
    const NpcPerceptionDialogIntentPreviewOptions& options) {
  NpcPerceptionDialogIntentPreview out;
  out.previewSource = options.previewSource;
  out.previewMode = options.previewMode;
  out.previewKind = previewKindForIntent(descriptor.intentKind);
  copyDescriptorFields(out, descriptor);

  out.liveDispatchExecuted = false;
  out.markAppliedExecuted = false;
  out.packetFanoutExecuted = false;

  if(trimCopy(out.previewSource).empty()) {
    addIssue(out, "missing_preview_source");
  }
  if(trimCopy(out.previewMode).empty()) {
    addIssue(out, "missing_preview_mode");
  }
  if(!descriptor.described) {
    addIssue(out, "typed_effect_not_described");
  }
  if(descriptor.effectFamily != "dialog_intent") {
    addIssue(out, "unsupported_effect_family_for_dialog_preview");
  }
  if(out.previewKind.empty()) {
    addIssue(out, "unsupported_dialog_intent_kind_for_preview");
  }
  if(descriptor.liveDispatchImplemented || descriptor.liveDispatchAllowed) {
    addIssue(out, "typed_effect_unexpectedly_live_dispatchable");
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

  out.previewed = out.issues.empty();
  out.status = out.previewed ? "preview_log_only_no_live_dispatch" : "preview_rejected";
  if(out.previewed) {
    out.logLine = buildLogLine(out);
  }
  return out;
}

std::string dialogIntentPreviewJson(const NpcPerceptionDialogIntentPreview& preview) {
  std::string out;
  out.reserve(1280 + preview.issues.size() * 48 + preview.logLine.size());
  out.push_back('{');

  appendJsonBoolField(out, "previewed", preview.previewed);
  appendComma(out);
  appendJsonField(out, "status", preview.status);
  appendComma(out);
  appendJsonField(out, "preview_source", preview.previewSource);
  appendComma(out);
  appendJsonField(out, "preview_mode", preview.previewMode);
  appendComma(out);
  appendJsonField(out, "preview_kind", preview.previewKind);
  appendComma(out);
  appendJsonField(out, "effect_family", preview.effectFamily);
  appendComma(out);
  appendJsonField(out, "effect_kind", preview.effectKind);
  appendComma(out);
  appendJsonField(out, "intent_kind", preview.intentKind);
  appendComma(out);
  appendJsonField(out, "action_kind", preview.actionKind);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", preview.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", preview.decisionUuid);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", preview.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", preview.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", preview.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", preview.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", preview.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", preview.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", preview.idempotencyKey);
  appendComma(out);
  appendJsonBoolField(out, "requires_dialog_ui", preview.requiresDialogUi);
  appendComma(out);
  appendJsonBoolField(out, "requires_audio", preview.requiresAudio);
  appendComma(out);
  appendJsonBoolField(out, "requires_npc_turn", preview.requiresNpcTurn);
  appendComma(out);
  appendJsonBoolField(out, "requires_npc_movement", preview.requiresNpcMovement);
  appendComma(out);
  appendJsonBoolField(out, "requires_combat", preview.requiresCombat);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_executed", preview.liveDispatchExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", preview.markAppliedExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", preview.packetFanoutExecuted);
  appendComma(out);
  appendJsonField(out, "log_line", preview.logLine);
  appendComma(out);
  appendJsonCountField(out, "issue_count", preview.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  for(std::size_t i = 0; i < preview.issues.size(); ++i) {
    if(i != 0) {
      out.push_back(',');
    }
    out += jsonEscape(preview.issues[i]);
  }
  out += "]}";
  return out;
}

} // namespace Mmo::AiRuntime
