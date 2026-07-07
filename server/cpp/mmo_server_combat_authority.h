#pragma once

#include <algorithm>
#include <array>
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
inline constexpr std::int64_t GothicMinDamage = 5;
inline constexpr std::int64_t DefaultCriticalDamageMultiplier = 2;
inline constexpr std::int64_t DefaultReferenceBowRangeG1 = 2000;
inline constexpr std::int64_t DefaultReferenceBowRangeG2 = 1500;
inline constexpr std::int64_t DefaultMaxBowRange = 4500;
inline constexpr std::int64_t DefaultMaxMagicRange = 3500;
inline constexpr std::size_t DamageTypeCount = 8;
inline constexpr std::size_t MaxCharacterKeyBytes = 191;
inline constexpr std::size_t MaxResourceKeyBytes = 64;
inline constexpr std::size_t MaxReasonBytes = 191;

enum class DamageType : std::size_t {
  Barrier = 0,
  Blunt = 1,
  Edge = 2,
  Fire = 3,
  Fly = 4,
  Magic = 5,
  Point = 6,
  Fall = 7,
};

enum class DamageModifier : std::uint8_t {
  Normal,
  Double,
  Half,
  Blocked,
};

enum class DamageOutcomeKind : std::uint8_t {
  NoHit,
  Hit,
  CriticalHit,
  Invincible,
};

struct ValidationResult final {
  bool accepted = true;
  const char* reason = "ok";
};

struct DamageInput final {
  std::string_view targetKey;
  std::int64_t amount = 0;
  bool fatal = false;
};

struct DamageVector final {
  std::array<std::int64_t, DamageTypeCount> values = {};

  [[nodiscard]] constexpr std::int64_t operator[](std::size_t index) const noexcept {
    return index < values.size() ? values[index] : 0;
  }
};

struct DamageActorProfile final {
  std::int64_t strength = 0;
  std::int64_t dexterity = 0;
  std::int64_t damageTypeMask = 0;
  DamageVector damage;
  DamageVector protection;
};

struct DamageResult final {
  std::int64_t value = 0;
  bool hasHit = false;
  bool invincible = false;
};

struct DamageOutcome final {
  DamageResult result;
  DamageOutcomeKind kind = DamageOutcomeKind::NoHit;
};

struct DamageBounds final {
  DamageResult minimum;
  DamageResult maximum;
};

struct DamageProposalInput final {
  std::int64_t proposedDamage = 0;
  DamageBounds bounds;
  std::int64_t tolerance = 0;
};

struct MeleeDamageInput final {
  DamageActorProfile attacker;
  DamageActorProfile victim;
  bool gothic2 = true;
  bool criticalHit = false;
  bool monsterWithoutWeapon = false;
  std::int64_t criticalMultiplier = DefaultCriticalDamageMultiplier;
};

struct MeleeRollInput final {
  DamageActorProfile attacker;
  DamageActorProfile victim;
  bool gothic2 = true;
  bool monsterWithoutWeapon = false;
  std::int64_t talentChance = 0;
  std::int64_t randomRoll = 0;
  std::int64_t criticalMultiplier = DefaultCriticalDamageMultiplier;
};

struct VectorDamageInput final {
  DamageVector damage;
  DamageVector victimProtection;
  DamageModifier modifier = DamageModifier::Normal;
  bool gothic2 = true;
};

struct RangedDamageValueInput final {
  DamageActorProfile attacker;
  bool gothic2 = true;
};

struct FallDamageInput final {
  double speed = 0.0;
  double gravity = 0.000981;
  std::int64_t fallHeightThreshold = 0;
  std::int64_t damagePerMeter = 0;
  std::int64_t fallProtection = 0;
};

struct RangedHitChanceInput final {
  double distance = 0.0;
  double weaponChance = 0.0;
  bool gothic2 = true;
  double referenceRangeG1 = static_cast<double>(DefaultReferenceBowRangeG1);
  double referenceRangeG2 = static_cast<double>(DefaultReferenceBowRangeG2);
  double maxRange = static_cast<double>(DefaultMaxBowRange);
};

struct RangedDamageInput final {
  DamageVector damage;
  DamageVector victimProtection;
  DamageModifier modifier = DamageModifier::Normal;
  bool gothic2 = true;
  bool projectileSpell = false;
  double distance = 0.0;
  double weaponChance = 0.0;
  double randomHitRoll = 0.0;
  bool gothic1CriticalHit = false;
  std::int64_t criticalMultiplier = DefaultCriticalDamageMultiplier;
};

struct ResourceDeltaInput final {
  std::string_view characterKey;
  std::string_view resourceKey;
  std::int64_t delta = 0;
  std::int64_t valueBefore = 0;
  std::int64_t valueAfter = 0;
};

struct ResourceSpendInput final {
  std::string_view characterKey;
  std::string_view resourceKey;
  std::int64_t currentValue = 0;
  std::int64_t amount = 0;
};

struct ResolvedResourceDelta final {
  ValidationResult validation;
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

[[nodiscard]] constexpr bool damageTypeEnabled(std::int64_t mask, std::size_t index) noexcept {
  return index < 63 && (mask & (std::int64_t{1} << index)) != 0;
}

[[nodiscard]] constexpr DamageResult applyGothic2MinDamage(DamageResult result, bool gothic2) noexcept {
  if(result.hasHit && !result.invincible && gothic2)
    result.value = std::max(result.value, GothicMinDamage);
  return result;
}

[[nodiscard]] constexpr DamageVector modifiedDamageVector(DamageVector damage,
                                                          DamageModifier modifier) noexcept {
  for(auto& value : damage.values) {
    if(modifier == DamageModifier::Double)
      value *= 2;
    else if(modifier == DamageModifier::Half)
      value /= 2;
    else if(modifier == DamageModifier::Blocked)
      value = 0;
  }
  return damage;
}

[[nodiscard]] constexpr DamageResult calculateVectorDamage(const VectorDamageInput& input) noexcept {
  if(input.modifier == DamageModifier::Blocked)
    return {0, true, true};

  const auto damage = modifiedDamageVector(input.damage, input.modifier);
  std::int64_t value = 0;
  bool invincible = true;
  bool anyDamage = false;

  for(std::size_t i = 0; i < DamageTypeCount; ++i) {
    if(damage[i] == 0)
      continue;
    anyDamage = true;
    if(input.victimProtection[i] >= 0) {
      value += std::max<std::int64_t>(damage[i] - input.victimProtection[i], 0);
      invincible = false;
    }
  }

  return applyGothic2MinDamage({value, anyDamage, invincible}, input.gothic2);
}

[[nodiscard]] constexpr DamageResult calculateMeleeDamage(const MeleeDamageInput& input) noexcept {
  const auto& attacker = input.attacker;
  const auto& victim = input.victim;
  const bool critical = input.criticalHit || (input.gothic2 && input.monsterWithoutWeapon);
  std::int64_t value = 0;
  bool hasMask = false;
  bool invincible = true;

  for(std::size_t i = 0; i < DamageTypeCount; ++i) {
    if(!damageTypeEnabled(attacker.damageTypeMask, i))
      continue;
    hasMask = true;
    if(victim.protection[i] < 0)
      continue;

    invincible = false;
    auto typedDamage = std::max<std::int64_t>(
      attacker.strength + attacker.damage[i] - victim.protection[i],
      0);

    if(input.gothic2) {
      if(!critical)
        typedDamage = (typedDamage - 1) / 10;
    } else if(critical) {
      typedDamage = std::max<std::int64_t>(
        attacker.strength + input.criticalMultiplier * attacker.damage[i] - victim.protection[i],
        0);
    }

    value += typedDamage;
  }

  return applyGothic2MinDamage({value, hasMask, invincible}, input.gothic2);
}

[[nodiscard]] constexpr bool isCriticalMeleeRoll(const MeleeRollInput& input) noexcept {
  if(input.gothic2 && input.monsterWithoutWeapon)
    return true;
  return input.talentChance > input.randomRoll;
}

[[nodiscard]] constexpr DamageOutcome calculateMeleeDamageWithRoll(const MeleeRollInput& input) noexcept {
  const bool critical = isCriticalMeleeRoll(input);
  const auto result = calculateMeleeDamage({
    .attacker = input.attacker,
    .victim = input.victim,
    .gothic2 = input.gothic2,
    .criticalHit = critical,
    .monsterWithoutWeapon = input.monsterWithoutWeapon,
    .criticalMultiplier = input.criticalMultiplier,
  });
  if(result.invincible)
    return {.result = result, .kind = DamageOutcomeKind::Invincible};
  return {.result = result, .kind = critical ? DamageOutcomeKind::CriticalHit : DamageOutcomeKind::Hit};
}

[[nodiscard]] constexpr DamageBounds calculateMeleeDamageBounds(MeleeDamageInput input) noexcept {
  input.criticalHit = false;
  const auto minimum = calculateMeleeDamage(input);
  input.criticalHit = true;
  const auto maximum = calculateMeleeDamage(input);
  return {minimum, maximum};
}

[[nodiscard]] constexpr DamageVector calculateRangedDamageValue(const RangedDamageValueInput& input) noexcept {
  DamageVector out;
  const auto dexterityBonus = input.gothic2 ? input.attacker.dexterity : 0;
  for(std::size_t i = 0; i < DamageTypeCount; ++i) {
    if(damageTypeEnabled(input.attacker.damageTypeMask, i))
      out.values[i] = dexterityBonus + input.attacker.damage[i];
  }
  return out;
}

[[nodiscard]] constexpr DamageResult calculateFallDamage(const FallDamageInput& input) noexcept {
  if(input.speed <= 0.0 || input.gravity <= 0.0)
    return {};

  const double height = (input.speed * input.speed) / (2.0 * input.gravity);
  const auto raw = static_cast<std::int64_t>(
    static_cast<double>(input.damagePerMeter) *
    (height - static_cast<double>(input.fallHeightThreshold)) / 100.0 -
    static_cast<double>(input.fallProtection));

  const bool invincible = input.fallProtection < 0;
  if(raw <= 0 || invincible)
    return {0, false, invincible};
  return {raw, true, false};
}

[[nodiscard]] constexpr double mix(double from, double to, double alpha) noexcept {
  return from + (to - from) * alpha;
}

[[nodiscard]] constexpr double rangedHitChanceAtDistance(const RangedHitChanceInput& input) noexcept {
  if(input.distance < 0.0 || input.weaponChance <= 0.0)
    return 0.0;
  const auto referenceRange = input.gothic2 ? input.referenceRangeG2 : input.referenceRangeG1;
  if(input.distance < referenceRange)
    return mix(1.0, input.weaponChance, input.distance / referenceRange);
  if(input.distance < input.maxRange)
    return mix(input.weaponChance, 0.0, (input.distance - referenceRange) / (input.maxRange - referenceRange));
  return 0.0;
}

[[nodiscard]] constexpr bool rangedProjectileHits(const RangedHitChanceInput& input,
                                                  double randomHitRoll) noexcept {
  return input.distance <= input.maxRange && rangedHitChanceAtDistance(input) > randomHitRoll;
}

[[nodiscard]] constexpr DamageOutcome calculateRangedDamageWithRoll(const RangedDamageInput& input) noexcept {
  if(!input.projectileSpell) {
    const auto hitChance = RangedHitChanceInput {
      .distance = input.distance,
      .weaponChance = input.weaponChance,
      .gothic2 = input.gothic2,
    };
    if(!rangedProjectileHits(hitChance, input.randomHitRoll))
      return {};
  } else if(input.distance > static_cast<double>(DefaultMaxMagicRange)) {
    return {};
  }

  auto damage = input.damage;
  if(!input.gothic2 && input.gothic1CriticalHit)
    for(auto& value : damage.values)
      value *= input.criticalMultiplier;

  const auto result = calculateVectorDamage({
    .damage = damage,
    .victimProtection = input.victimProtection,
    .modifier = input.modifier,
    .gothic2 = input.gothic2,
  });
  if(result.invincible)
    return {.result = result, .kind = DamageOutcomeKind::Invincible};
  return {.result = result, .kind = input.gothic1CriticalHit ? DamageOutcomeKind::CriticalHit : DamageOutcomeKind::Hit};
}

[[nodiscard]] constexpr ValidationResult validateDamageProposal(const DamageProposalInput& input) noexcept {
  if(input.proposedDamage < MinDamageAmount || input.proposedDamage > MaxDamageAmount)
    return {false, "combat_damage_invalid"};
  if(input.tolerance < 0 || input.tolerance > MaxDamageAmount)
    return {false, "combat_damage_tolerance_invalid"};

  const auto lower = std::max<std::int64_t>(0, input.bounds.minimum.value - input.tolerance);
  const auto upper = std::min<std::int64_t>(MaxDamageAmount, input.bounds.maximum.value + input.tolerance);
  if(input.proposedDamage < lower || input.proposedDamage > upper)
    return {false, "combat_damage_proposal_out_of_bounds"};
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

[[nodiscard]] constexpr ResolvedResourceDelta resolveResourceSpend(const ResourceSpendInput& input) noexcept {
  if(!validLen(input.characterKey, MaxCharacterKeyBytes))
    return {{false, "resource_character_missing"}, 0, input.currentValue, input.currentValue};
  if(!validLen(input.resourceKey, MaxResourceKeyBytes))
    return {{false, "resource_key_missing"}, 0, input.currentValue, input.currentValue};
  if(input.currentValue < MinResourceValue || input.currentValue > MaxResourceValue)
    return {{false, "resource_value_invalid"}, 0, input.currentValue, input.currentValue};
  if(input.amount <= 0 || input.amount > MaxResourceDelta)
    return {{false, "resource_spend_amount_invalid"}, 0, input.currentValue, input.currentValue};
  if(input.currentValue < input.amount)
    return {{false, "resource_spend_insufficient"}, 0, input.currentValue, input.currentValue};

  const auto after = input.currentValue - input.amount;
  return {validateResourceDelta({
            .characterKey = input.characterKey,
            .resourceKey = input.resourceKey,
            .delta = -input.amount,
            .valueBefore = input.currentValue,
            .valueAfter = after,
          }),
          -input.amount,
          input.currentValue,
          after};
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
