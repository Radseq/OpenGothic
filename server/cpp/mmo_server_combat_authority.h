#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace Mmo::Server::Combat {

inline constexpr std::int64_t MinDamageAmount = 0;
inline constexpr std::int64_t MaxDamageAmount = 1000000;
inline constexpr std::int64_t MinResourceDelta = -1000000;
inline constexpr std::int64_t MaxResourceDelta = 1000000;
inline constexpr std::int64_t MinResourceValue = 0;
inline constexpr std::int64_t MaxResourceValue = 1000000;
inline constexpr std::int64_t MinExperienceReward = 0;
inline constexpr std::int64_t MaxExperienceReward = 10000000;
inline constexpr std::int64_t MinLearningPointDelta = -1000;
inline constexpr std::int64_t MaxLearningPointDelta = 1000;
inline constexpr std::size_t MaxCharacterKeyBytes = 191;
inline constexpr std::size_t MaxResourceKeyBytes = 64;
inline constexpr std::size_t MaxReasonBytes = 191;

struct ValidationResult final {
  bool accepted = true;
  const char* reason = "ok";
};

struct DamageInput final {
  std::string_view targetKey;
  std::int64_t amount = 0;
  bool fatal = false;
};

struct ResourceDeltaInput final {
  std::string_view characterKey;
  std::string_view resourceKey;
  std::int64_t delta = 0;
  std::int64_t valueBefore = 0;
  std::int64_t valueAfter = 0;
};

struct ProgressionRewardInput final {
  std::int64_t experienceDelta = 0;
  std::int64_t learningPointsDelta = 0;
  std::string_view reason;
};

[[nodiscard]] constexpr bool validLen(std::string_view text, std::size_t maxLen) noexcept {
  return !text.empty() && text.size() <= maxLen;
}

[[nodiscard]] constexpr ValidationResult validateDamage(const DamageInput& input) noexcept {
  if(!validLen(input.targetKey, MaxCharacterKeyBytes * 3))
    return {false, "combat_target_missing"};
  if(input.amount < MinDamageAmount || input.amount > MaxDamageAmount)
    return {false, "combat_damage_invalid"};
  return {};
}

[[nodiscard]] constexpr ValidationResult validateResourceDelta(const ResourceDeltaInput& input) noexcept {
  if(!validLen(input.characterKey, MaxCharacterKeyBytes))
    return {false, "resource_character_missing"};
  if(!validLen(input.resourceKey, MaxResourceKeyBytes))
    return {false, "resource_key_missing"};
  if(input.delta < MinResourceDelta || input.delta > MaxResourceDelta)
    return {false, "resource_delta_invalid"};
  if(input.valueBefore < MinResourceValue || input.valueBefore > MaxResourceValue ||
     input.valueAfter < MinResourceValue || input.valueAfter > MaxResourceValue)
    return {false, "resource_value_invalid"};
  return {};
}

[[nodiscard]] constexpr ValidationResult validateProgressionReward(const ProgressionRewardInput& input) noexcept {
  if(input.experienceDelta < MinDamageAmount || input.experienceDelta > MaxExperienceReward)
    return {false, "progression_xp_invalid"};
  if(input.learningPointsDelta < MinLearningPointDelta || input.learningPointsDelta > MaxLearningPointDelta)
    return {false, "progression_lp_invalid"};
  if(input.reason.size() > MaxReasonBytes)
    return {false, "progression_reason_too_large"};
  return {};
}

} // namespace Mmo::Server::Combat
