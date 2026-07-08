#include "mmo_npc_perception_action_dispatcher_boundary.h"

#include <array>
#include <cctype>
#include <string>
#include <utility>

namespace Mmo::AiRuntime {
namespace {

struct StaticActionKindContract final {
  std::string_view actionKind;
  std::string_view effectKind;
  bool safeNoop = false;
  bool requiresTargetKey = true;
  bool requiresNpcEntityKey = true;
  bool requiresPerceptionKind = true;
  bool requiresDecisionUuid = true;
};

static constexpr std::array<StaticActionKindContract, 9> KnownActionContracts = {{
    {"npc_assess_player", "assessment_observation", false, true, true, true, true},
    {"npc_turn_to_player", "orientation_intent", false, true, true, true, true},
    {"npc_approach_player", "movement_intent", false, true, true, true, true},
    {"npc_greet_player", "dialog_greeting_intent", false, true, true, true, true},
    {"npc_warn_player", "dialog_warning_intent", false, true, true, true, true},
    {"npc_start_dialog", "dialog_start_intent", false, true, true, true, true},
    {"npc_attack_player", "combat_intent", false, true, true, true, true},
    {"npc_ignore_player", "noop", true, false, true, true, true},
    {"npc_noop", "noop", true, false, false, false, true},
}};

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
  out.reserve(text.size() + 2);
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

void addIssue(NpcPerceptionActionDispatchValidation& validation, std::string issue) {
  validation.issues.push_back(std::move(issue));
}

} // namespace

bool isKnownNpcPerceptionActionKind(std::string_view actionKind) noexcept {
  for(const auto& contract : KnownActionContracts) {
    if(contract.actionKind == actionKind) {
      return true;
    }
  }
  return false;
}

NpcPerceptionActionKindContract npcPerceptionActionKindContract(std::string_view actionKind) {
  for(const auto& contract : KnownActionContracts) {
    if(contract.actionKind == actionKind) {
      return NpcPerceptionActionKindContract{
          std::string(contract.actionKind),
          std::string(contract.effectKind),
          true,
          contract.safeNoop,
          false,
          contract.requiresTargetKey,
          contract.requiresNpcEntityKey,
          contract.requiresPerceptionKind,
          contract.requiresDecisionUuid,
      };
    }
  }
  return NpcPerceptionActionKindContract{
      std::string(actionKind),
      "unknown",
      false,
      false,
      false,
      true,
      true,
      true,
      true,
  };
}

bool looksLikeJsonObject(std::string_view payload) noexcept {
  while(!payload.empty() && std::isspace(static_cast<unsigned char>(payload.front())) != 0) {
    payload.remove_prefix(1);
  }
  while(!payload.empty() && std::isspace(static_cast<unsigned char>(payload.back())) != 0) {
    payload.remove_suffix(1);
  }
  return payload.size() >= 2 && payload.front() == '{' && payload.back() == '}';
}

bool jsonObjectHasKey(std::string_view payload, std::string_view key) noexcept {
  if(key.empty()) {
    return false;
  }
  std::string quoted;
  quoted.reserve(key.size() + 2);
  quoted.push_back('"');
  quoted.append(key.data(), key.size());
  quoted.push_back('"');
  return payload.find(quoted) != std::string_view::npos;
}

NpcPerceptionActionDispatchValidation validateNpcPerceptionActionDispatchContract(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidationOptions& options) {
  NpcPerceptionActionDispatchValidation validation;
  validation.actionKind = action.actionKind;

  const auto kind = npcPerceptionActionKindContract(action.actionKind);
  validation.effectKind = kind.effectKind;
  validation.liveDispatchAllowed = false;

  if(options.requireActionQueueUuid && action.actionQueueUuid.empty()) {
    addIssue(validation, "missing_action_queue_uuid");
  }
  if(options.requireDecisionUuid && action.decisionUuid.empty()) {
    addIssue(validation, "missing_decision_uuid");
  }
  if(options.requireWorldInstanceUuid && action.worldInstanceUuid.empty()) {
    addIssue(validation, "missing_world_instance_uuid");
  }
  if(options.requireTargetKey && kind.requiresTargetKey && trimCopy(action.targetKey).empty()) {
    addIssue(validation, "missing_target_key");
  }
  if(options.requireIdempotencyKey && action.idempotencyKey.empty()) {
    addIssue(validation, "missing_idempotency_key");
  }
  if(options.requireKnownActionKind && !kind.known) {
    addIssue(validation, "unknown_action_kind");
  }

  if(options.requirePayloadJsonObject && !looksLikeJsonObject(action.requestPayloadJson)) {
    addIssue(validation, "request_payload_not_json_object");
  }
  if(looksLikeJsonObject(action.requestPayloadJson)) {
    if(options.requirePayloadDecisionUuid && kind.requiresDecisionUuid &&
       !jsonObjectHasKey(action.requestPayloadJson, "decision_uuid")) {
      addIssue(validation, "payload_missing_decision_uuid");
    }
    if(options.requirePayloadNpcEntityKey && kind.requiresNpcEntityKey &&
       !jsonObjectHasKey(action.requestPayloadJson, "npc_entity_key")) {
      addIssue(validation, "payload_missing_npc_entity_key");
    }
    if(options.requirePayloadPerceptionKind && kind.requiresPerceptionKind &&
       !jsonObjectHasKey(action.requestPayloadJson, "perception_kind")) {
      addIssue(validation, "payload_missing_perception_kind");
    }
  }

  validation.accepted = validation.issues.empty();
  if(!validation.accepted) {
    validation.status = "invalid";
  } else if(kind.liveDispatchImplemented) {
    validation.status = "validated_dispatch_ready";
    validation.liveDispatchAllowed = true;
  } else if(kind.safeNoop) {
    validation.status = "validated_noop_no_live_dispatch";
  } else {
    validation.status = "validated_contract_only_no_live_dispatch";
  }
  return validation;
}

std::string validationIssuesJson(const NpcPerceptionActionDispatchValidation& validation) {
  std::string out;
  out.reserve(validation.issues.size() * 32 + 2);
  out.push_back('[');
  for(std::size_t i = 0; i < validation.issues.size(); ++i) {
    if(i != 0) {
      out.push_back(',');
    }
    out += jsonEscape(validation.issues[i]);
  }
  out.push_back(']');
  return out;
}

} // namespace Mmo::AiRuntime
