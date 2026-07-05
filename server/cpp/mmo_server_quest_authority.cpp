#include "mmo_server_quest_authority.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>

namespace Mmo::Server::Quest {

namespace {

[[nodiscard]] std::string lowerAscii(std::string_view text) {
  std::string out(text);
  for(char& ch : out)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

[[nodiscard]] std::string stripQuestPrefix(std::string_view key) {
  constexpr std::string_view Prefix = "quest:";
  if(key.size() >= Prefix.size() && key.substr(0, Prefix.size()) == Prefix)
    key.remove_prefix(Prefix.size());
  return std::string(key);
}

[[nodiscard]] std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
  for(const auto value : values) {
    if(!value.empty())
      return std::string(value);
  }
  return {};
}

[[nodiscard]] bool knownStatusText(std::string_view raw) noexcept {
  if(raw.empty())
    return true;
  constexpr std::array<std::string_view, 16> Values {
    "1", "2", "3", "4",
    "run", "running", "in_progress",
    "success", "completed_success", "succeeded",
    "failed", "failure", "completed_failed",
    "obsolete", "closed",
    "cancelled",
  };
  return std::find(Values.begin(), Values.end(), raw) != Values.end();
}

[[nodiscard]] Story::ValidationResult validateTransition(Status previous,
                                                         Status next,
                                                         bool allowTerminalReopen) noexcept {
  if(previous == next)
    return {};
  if(isTerminal(previous) && next == Status::Running && !allowTerminalReopen)
    return {false, "quest_terminal_reopen_denied"};
  if(previous == Status::Obsolete && next != Status::Obsolete && !allowTerminalReopen)
    return {false, "quest_obsolete_transition_denied"};
  return {};
}

} // namespace

Status parseStatus(std::string_view raw) noexcept {
  const auto status = lowerAscii(raw);
  if(status == "2" || status == "success" || status == "completed_success" || status == "succeeded")
    return Status::Success;
  if(status == "3" || status == "failed" || status == "failure" || status == "completed_failed")
    return Status::Failed;
  if(status == "4" || status == "obsolete" || status == "closed" || status == "cancelled")
    return Status::Obsolete;
  return Status::Running;
}

UpdateCommand buildUpdateCommand(const UpdateInput& input) {
  UpdateCommand out;
  out.questKey = stripQuestPrefix(firstNonEmpty({input.questKey, input.topic, input.targetKey}));
  out.questName = firstNonEmpty({input.questName, out.questKey});
  out.entryCount = input.entryCount;

  const auto rawStatus = lowerAscii(input.rawStatus);
  if(!knownStatusText(rawStatus)) {
    out.accepted = false;
    out.reason = "quest_status_invalid";
    return out;
  }

  const Status nextStatus = parseStatus(rawStatus);
  out.status = std::string(statusName(nextStatus));

  if(!input.previousStatus.empty()) {
    const auto previousRaw = lowerAscii(input.previousStatus);
    if(!knownStatusText(previousRaw)) {
      out.accepted = false;
      out.reason = "quest_previous_status_invalid";
      return out;
    }
    const auto transition = validateTransition(parseStatus(previousRaw), nextStatus, input.allowTerminalReopen);
    if(!transition.accepted) {
      out.accepted = false;
      out.reason = transition.reason;
      return out;
    }
  }

  const auto validation = Story::validateQuestUpdate({
    .questKey = out.questKey,
    .questName = out.questName,
    .status = out.status,
    .entryCount = out.entryCount,
  });
  if(!validation.accepted) {
    out.accepted = false;
    out.reason = validation.reason;
  }
  return out;
}

} // namespace Mmo::Server::Quest
