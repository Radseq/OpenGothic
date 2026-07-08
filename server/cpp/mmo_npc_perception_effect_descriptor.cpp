#include "mmo_npc_perception_effect_descriptor.h"

#include <cctype>
#include <optional>
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

void appendComma(std::string& out) {
  if(!out.empty() && out.back() != '{' && out.back() != '[') {
    out.push_back(',');
  }
}

[[nodiscard]] std::optional<char> unescapeJsonChar(char escaped) noexcept {
  switch(escaped) {
    case '"':
      return '"';
    case '\\':
      return '\\';
    case '/':
      return '/';
    case 'b':
      return '\b';
    case 'f':
      return '\f';
    case 'n':
      return '\n';
    case 'r':
      return '\r';
    case 't':
      return '\t';
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::string> jsonStringValue(std::string_view payload, std::string_view key) {
  if(key.empty()) {
    return std::nullopt;
  }

  std::string quotedKey;
  quotedKey.reserve(key.size() + 2);
  quotedKey.push_back('"');
  quotedKey.append(key.data(), key.size());
  quotedKey.push_back('"');

  std::size_t pos = payload.find(quotedKey);
  while(pos != std::string_view::npos) {
    std::size_t cursor = pos + quotedKey.size();
    while(cursor < payload.size() && std::isspace(static_cast<unsigned char>(payload[cursor])) != 0) {
      ++cursor;
    }
    if(cursor < payload.size() && payload[cursor] == ':') {
      ++cursor;
      while(cursor < payload.size() && std::isspace(static_cast<unsigned char>(payload[cursor])) != 0) {
        ++cursor;
      }
      if(cursor >= payload.size() || payload[cursor] != '"') {
        return std::nullopt;
      }
      ++cursor;
      std::string out;
      while(cursor < payload.size()) {
        const char ch = payload[cursor++];
        if(ch == '"') {
          return out;
        }
        if(ch == '\\') {
          if(cursor >= payload.size()) {
            return std::nullopt;
          }
          const char escaped = payload[cursor++];
          if(escaped == 'u') {
            // The Gothic MMO action payload keys used by this descriptor are
            // ASCII identifiers. Preserve escaped unicode sequences literally
            // rather than accepting a lossy partial decode in this boundary.
            out += "\\u";
            for(int i = 0; i < 4 && cursor < payload.size(); ++i) {
              out.push_back(payload[cursor++]);
            }
            continue;
          }
          const auto decoded = unescapeJsonChar(escaped);
          if(!decoded.has_value()) {
            return std::nullopt;
          }
          out.push_back(*decoded);
        } else {
          out.push_back(ch);
        }
      }
      return std::nullopt;
    }
    pos = payload.find(quotedKey, pos + quotedKey.size());
  }
  return std::nullopt;
}

void addIssue(NpcPerceptionTypedEffectDescriptor& descriptor, std::string issue) {
  descriptor.issues.push_back(std::move(issue));
}

[[nodiscard]] std::string actionIntentKind(std::string_view actionKind) {
  if(actionKind == "npc_greet_player") {
    return "greet_player";
  }
  if(actionKind == "npc_warn_player") {
    return "warn_player";
  }
  return {};
}

[[nodiscard]] std::string actionEffectKind(std::string_view actionKind) {
  if(actionKind == "npc_greet_player") {
    return "dialog_greeting_intent";
  }
  if(actionKind == "npc_warn_player") {
    return "dialog_warning_intent";
  }
  return {};
}

void copyStableFields(NpcPerceptionTypedEffectDescriptor& out, const ClaimedNpcPerceptionAction& action) {
  out.actionKind = action.actionKind;
  out.actionQueueUuid = action.actionQueueUuid;
  out.decisionUuid = action.decisionUuid;
  out.worldInstanceUuid = action.worldInstanceUuid;
  out.sessionUuid = action.sessionUuid;
  out.characterUuid = action.characterUuid;
  out.targetKey = action.targetKey;
  out.idempotencyKey = action.idempotencyKey;
}

} // namespace

bool supportsNpcPerceptionTypedEffectDescriptor(std::string_view actionKind) noexcept {
  return actionKind == "npc_greet_player" || actionKind == "npc_warn_player";
}

NpcPerceptionTypedEffectDescriptor describeNpcPerceptionTypedEffect(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation) {
  NpcPerceptionTypedEffectDescriptor out;
  copyStableFields(out, action);
  out.effectFamily = "dialog_intent";
  out.effectKind = actionEffectKind(action.actionKind);
  out.intentKind = actionIntentKind(action.actionKind);
  out.requiresDialogUi = true;
  out.requiresAudio = false;
  out.requiresNpcTurn = false;
  out.requiresNpcMovement = false;
  out.requiresCombat = false;
  out.liveDispatchImplemented = false;
  out.liveDispatchAllowed = false;

  if(!validation.accepted) {
    out.status = "invalid_dispatch_contract";
    for(const auto& issue : validation.issues) {
      addIssue(out, "dispatch_contract:" + issue);
    }
    return out;
  }

  if(!supportsNpcPerceptionTypedEffectDescriptor(action.actionKind)) {
    out.status = "unsupported_effect_family";
    out.effectFamily.clear();
    out.requiresDialogUi = false;
    addIssue(out, "unsupported_action_kind_for_typed_effect_descriptor");
    return out;
  }

  const auto npcEntityKey = jsonStringValue(action.requestPayloadJson, "npc_entity_key");
  const auto perceptionKind = jsonStringValue(action.requestPayloadJson, "perception_kind");
  const auto decisionUuid = jsonStringValue(action.requestPayloadJson, "decision_uuid");

  out.npcEntityKey = npcEntityKey.value_or("");
  out.perceptionKind = perceptionKind.value_or("");
  if(out.decisionUuid.empty() && decisionUuid.has_value()) {
    out.decisionUuid = *decisionUuid;
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

  out.described = out.issues.empty();
  out.status = out.described ? "described_contract_only_no_live_dispatch" : "descriptor_incomplete";
  return out;
}

std::string typedEffectDescriptorJson(const NpcPerceptionTypedEffectDescriptor& descriptor) {
  std::string out;
  out.reserve(1024 + descriptor.issues.size() * 48);
  out.push_back('{');

  appendJsonBoolField(out, "described", descriptor.described);
  appendComma(out);
  appendJsonField(out, "status", descriptor.status);
  appendComma(out);
  appendJsonField(out, "effect_family", descriptor.effectFamily);
  appendComma(out);
  appendJsonField(out, "effect_kind", descriptor.effectKind);
  appendComma(out);
  appendJsonField(out, "intent_kind", descriptor.intentKind);
  appendComma(out);
  appendJsonField(out, "action_kind", descriptor.actionKind);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", descriptor.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", descriptor.decisionUuid);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", descriptor.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", descriptor.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", descriptor.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", descriptor.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", descriptor.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", descriptor.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", descriptor.idempotencyKey);
  appendComma(out);
  appendJsonBoolField(out, "requires_dialog_ui", descriptor.requiresDialogUi);
  appendComma(out);
  appendJsonBoolField(out, "requires_audio", descriptor.requiresAudio);
  appendComma(out);
  appendJsonBoolField(out, "requires_npc_turn", descriptor.requiresNpcTurn);
  appendComma(out);
  appendJsonBoolField(out, "requires_npc_movement", descriptor.requiresNpcMovement);
  appendComma(out);
  appendJsonBoolField(out, "requires_combat", descriptor.requiresCombat);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_implemented", descriptor.liveDispatchImplemented);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_allowed", descriptor.liveDispatchAllowed);
  appendComma(out);
  appendJsonCountField(out, "issue_count", descriptor.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  for(std::size_t i = 0; i < descriptor.issues.size(); ++i) {
    if(i != 0) {
      out.push_back(',');
    }
    out += jsonEscape(descriptor.issues[i]);
  }
  out += "]}";
  return out;
}

} // namespace Mmo::AiRuntime
