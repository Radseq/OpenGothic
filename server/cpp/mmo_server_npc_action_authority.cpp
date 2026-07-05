#include "mmo_server_npc_action_authority.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace Mmo::Server::NpcAction {

namespace {

[[nodiscard]] std::string trimAscii(std::string_view text) {
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    text.remove_prefix(1);
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.remove_suffix(1);
  return std::string(text);
}

[[nodiscard]] std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
  for(const auto value : values) {
    auto out = trimAscii(value);
    if(!out.empty())
      return out;
  }
  return {};
}

[[nodiscard]] bool validLen(std::string_view text, std::size_t maxLen) noexcept {
  return text.size() <= maxLen;
}

[[nodiscard]] bool nonEmptyKey(std::string_view key) noexcept {
  return !key.empty() && key.size() <= MaxNpcActionKeyBytes;
}

[[nodiscard]] std::string defaultSyncGroup(std::string_view actorKey, std::string_view actionKey) {
  std::string out;
  out.reserve(actorKey.size() + actionKey.size() + 1);
  out += actorKey;
  out.push_back(':');
  out += actionKey;
  return out;
}

void applyValidation(ActionCommand& out, ValidationResult validation) noexcept {
  out.accepted = validation.accepted;
  out.shouldPersist = validation.shouldPersist;
  out.reason = validation.reason;
}

void applyValidation(DialogLineCommand& out, ValidationResult validation) noexcept {
  out.accepted = validation.accepted;
  out.shouldPersist = validation.shouldPersist;
  out.reason = validation.reason;
}

[[nodiscard]] ValidationResult validateAction(const ActionCommand& command) noexcept {
  if(!nonEmptyKey(command.actorKey))
    return {true, false, "npc_action_missing_actor"};
  if(!nonEmptyKey(command.actionKey))
    return {true, false, "npc_action_missing_action"};
  if(!validLen(command.actionState, MaxNpcActionNameBytes) ||
     !validLen(command.targetKey, MaxNpcActionKeyBytes) ||
     !validLen(command.syncGroup, MaxNpcActionKeyBytes))
    return {true, false, "npc_action_too_large"};
  return {};
}

[[nodiscard]] ValidationResult validateDialogLine(const DialogLineCommand& command) noexcept {
  if(!nonEmptyKey(command.conversationKey))
    return {true, false, "npc_dialog_missing_conversation"};
  if(!nonEmptyKey(command.speakerKey))
    return {true, false, "npc_dialog_missing_speaker"};
  if(command.outputName.empty() && command.subtitleText.empty())
    return {true, false, "npc_dialog_line_empty"};
  if(!validLen(command.listenerKey, MaxNpcActionKeyBytes) ||
     !validLen(command.infoKey, MaxNpcActionKeyBytes) ||
     !validLen(command.outputName, MaxNpcActionNameBytes) ||
     !validLen(command.subtitleText, MaxNpcDialogTextBytes))
    return {true, false, "npc_dialog_line_too_large"};
  if(command.lineDurationMs > MaxNpcDialogLineMs)
    return {true, false, "npc_dialog_line_duration_invalid"};
  return {};
}

} // namespace

ActionCommand buildActionCommand(const ActionInput& input) {
  ActionCommand out;
  out.actorKey = trimAscii(input.actorKey);
  out.actionKey = firstNonEmpty({input.actionKey, "unknown"});
  out.actionState = firstNonEmpty({input.actionState, "active"});
  out.targetKey = trimAscii(input.targetKey);
  out.syncGroup = firstNonEmpty({input.syncGroup});
  if(out.syncGroup.empty())
    out.syncGroup = defaultSyncGroup(out.actorKey, out.actionKey);
  out.serverTick = input.serverTick;
  applyValidation(out, validateAction(out));
  return out;
}

DialogLineCommand buildDialogLineCommand(const DialogLineInput& input) {
  DialogLineCommand out;
  out.conversationKey = firstNonEmpty({input.conversationKey});
  out.speakerKey = trimAscii(input.speakerKey);
  out.listenerKey = trimAscii(input.listenerKey);
  out.infoKey = trimAscii(input.infoKey);
  out.outputName = trimAscii(input.outputName);
  out.subtitleText = trimAscii(input.subtitleText);
  out.lineDurationMs = input.lineDurationMs;
  out.serverTick = input.serverTick;
  if(out.conversationKey.empty())
    out.conversationKey = defaultSyncGroup(out.speakerKey, out.listenerKey.empty() ? "dialog" : out.listenerKey);
  applyValidation(out, validateDialogLine(out));
  return out;
}

} // namespace Mmo::Server::NpcAction
