#include "mmo_server_dialog_authority.h"

#include "mmo_server_story_authority.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>

namespace Mmo::Server::Dialog {

namespace {

[[nodiscard]] std::string lowerAscii(std::string_view text) {
  std::string out(text);
  for(char& ch : out)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

[[nodiscard]] std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
  for(const auto value : values) {
    if(!value.empty())
      return std::string(value);
  }
  return {};
}

[[nodiscard]] std::string stripKnownPrefix(std::string_view key, std::string_view prefix) {
  if(key.size() >= prefix.size() && key.substr(0, prefix.size()) == prefix)
    key.remove_prefix(prefix.size());
  return std::string(key);
}

[[nodiscard]] std::string normalizeAvailability(std::string_view raw) {
  const auto state = lowerAscii(raw);
  if(state == "repeatable" || state == "repeatable_known")
    return "repeatable_known";
  if(state == "consumed" || state == "consumed_hidden")
    return "consumed_hidden";
  if(state == "not_known" || state == "removed" || state == "hidden")
    return "hidden";
  if(state == "available" || state == "visible")
    return "visible";
  if(state == "unknown")
    return "unknown";
  return {};
}

[[nodiscard]] bool hasKnownAvailabilityAlias(std::string_view raw) {
  if(raw.empty())
    return true;
  constexpr std::array<std::string_view, 10> Values {
    "repeatable", "repeatable_known",
    "consumed", "consumed_hidden",
    "not_known", "removed", "hidden",
    "available", "visible",
    "unknown",
  };
  const auto state = lowerAscii(raw);
  return std::find(Values.begin(), Values.end(), state) != Values.end();
}

[[nodiscard]] std::string defaultAvailability(bool known, bool permanent) {
  return std::string(Story::defaultDialogAvailability(known, permanent));
}

} // namespace

KnownDialogCommand buildKnownDialogCommand(const KnownDialogInput& input) {
  KnownDialogCommand out;
  out.npcKey = stripKnownPrefix(firstNonEmpty({input.npcKey, input.npcSymbolName}), "npc-symbol:");
  out.infoKey = stripKnownPrefix(firstNonEmpty({input.infoKey, input.infoSymbolName, input.targetKey}), "dialog-info:");

  out.known = input.known.value_or(!input.removed.value_or(false));
  out.permanent = input.permanent.value_or(input.repeatable.value_or(false));
  if(!input.permanent && !input.repeatable && input.removed)
    out.permanent = !*input.removed;

  if(!hasKnownAvailabilityAlias(input.availabilityState)) {
    out.accepted = false;
    out.reason = "dialog_availability_invalid";
    return out;
  }

  out.availability = normalizeAvailability(input.availabilityState);
  if(out.availability.empty())
    out.availability = defaultAvailability(out.known, out.permanent);
  if(!out.known)
    out.availability = "hidden";

  const auto validation = Story::validateKnownDialog({
    .npcKey = out.npcKey,
    .infoKey = out.infoKey,
    .availabilityState = out.availability,
  });
  if(!validation.accepted) {
    out.accepted = false;
    out.reason = validation.reason;
  }
  return out;
}

} // namespace Mmo::Server::Dialog
