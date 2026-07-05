#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace Mmo::Server::Story {

inline constexpr std::size_t MaxScriptKeyBytes = 255;
inline constexpr std::size_t MaxQuestKeyBytes = 191;
inline constexpr std::size_t MaxQuestNameBytes = 255;
inline constexpr std::size_t MaxDialogKeyBytes = 255;
inline constexpr std::size_t MaxStatusBytes = 64;
inline constexpr std::int64_t MinScriptSymbolIndex = -1;
inline constexpr std::int64_t MaxScriptSymbolIndex = 10000000;
inline constexpr std::int64_t MinScriptValueIndex = 0;
inline constexpr std::int64_t MaxScriptValueIndex = 10000000;
inline constexpr std::int64_t MinQuestEntryCount = 0;
inline constexpr std::int64_t MaxQuestEntryCount = 100000;

struct ValidationResult final {
  bool accepted = true;
  const char* reason = "ok";
};

struct ScriptIntInput final {
  std::string_view scriptKey;
  std::int64_t symbolIndex = 0;
  std::int64_t valueIndex = 0;
  std::int64_t valueAfter = 0;
};

struct QuestUpdateInput final {
  std::string_view questKey;
  std::string_view questName;
  std::string_view status;
  std::int64_t entryCount = 0;
};

struct KnownDialogInput final {
  std::string_view npcKey;
  std::string_view infoKey;
  std::string_view availabilityState;
};

[[nodiscard]] constexpr bool validLen(std::string_view text, std::size_t maxLen) noexcept {
  return !text.empty() && text.size() <= maxLen;
}

[[nodiscard]] constexpr bool fitsInt32(std::int64_t value) noexcept {
  return value >= std::numeric_limits<std::int32_t>::min() &&
         value <= std::numeric_limits<std::int32_t>::max();
}

template<std::size_t N>
[[nodiscard]] constexpr bool contains(const std::array<std::string_view, N>& values,
                                      std::string_view value) noexcept {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] constexpr std::string_view normalizeQuestStatus(std::string_view status) noexcept {
  if(status == "1" || status == "run" || status == "in_progress" || status.empty())
    return "running";
  if(status == "2" || status == "completed_success" || status == "succeeded")
    return "success";
  if(status == "3" || status == "failure" || status == "completed_failed")
    return "failed";
  if(status == "4" || status == "closed")
    return "obsolete";
  if(status == "running" || status == "success" || status == "failed" || status == "obsolete")
    return status;
  return "running";
}

[[nodiscard]] constexpr std::string_view defaultDialogAvailability(bool known,
                                                                   bool permanent) noexcept {
  if(!known)
    return "hidden";
  return permanent ? "repeatable_known" : "consumed_hidden";
}

[[nodiscard]] constexpr bool knownQuestStatus(std::string_view status) noexcept {
  constexpr std::array<std::string_view, 4> Values {
    "running", "success", "failed", "obsolete"
  };
  return contains(Values, status);
}

[[nodiscard]] constexpr bool knownDialogAvailability(std::string_view state) noexcept {
  constexpr std::array<std::string_view, 5> Values {
    "unknown", "visible", "hidden", "consumed_hidden", "repeatable_known"
  };
  return contains(Values, state);
}

[[nodiscard]] constexpr ValidationResult validateScriptInt(const ScriptIntInput& input) noexcept {
  if(!validLen(input.scriptKey, MaxScriptKeyBytes))
    return {false, "script_key_missing"};
  if(input.symbolIndex < MinScriptSymbolIndex || input.symbolIndex > MaxScriptSymbolIndex)
    return {false, "script_symbol_index_invalid"};
  if(input.valueIndex < MinScriptValueIndex || input.valueIndex > MaxScriptValueIndex)
    return {false, "script_value_index_invalid"};
  if(!fitsInt32(input.valueAfter))
    return {false, "script_value_after_invalid"};
  return {};
}

[[nodiscard]] constexpr ValidationResult validateQuestUpdate(const QuestUpdateInput& input) noexcept {
  if(!validLen(input.questKey, MaxQuestKeyBytes))
    return {false, "quest_key_missing"};
  if(input.questName.size() > MaxQuestNameBytes)
    return {false, "quest_name_too_large"};
  if(input.status.size() > MaxStatusBytes || !knownQuestStatus(input.status))
    return {false, "quest_status_invalid"};
  if(input.entryCount < MinQuestEntryCount || input.entryCount > MaxQuestEntryCount)
    return {false, "quest_entry_count_invalid"};
  return {};
}

[[nodiscard]] constexpr ValidationResult validateKnownDialog(const KnownDialogInput& input) noexcept {
  if(!validLen(input.npcKey, MaxDialogKeyBytes) || !validLen(input.infoKey, MaxDialogKeyBytes))
    return {false, "dialog_key_missing"};
  if(!knownDialogAvailability(input.availabilityState))
    return {false, "dialog_availability_invalid"};
  return {};
}

} // namespace Mmo::Server::Story
