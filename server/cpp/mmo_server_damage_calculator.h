#pragma once

#include <algorithm>
#include <cstdint>

#include "mmo_server_combat_authority.h"

namespace Mmo::Server::DamageCalculator {

inline constexpr std::int64_t DefaultObservedDamageTolerance = Combat::GothicMinDamage;

enum class DamageKind : std::uint8_t {
  Unknown,
  Melee,
  Ranged,
  Magic,
  Fall,
};

struct ObservedDamageInput final {
  DamageKind kind = DamageKind::Unknown;
  std::int64_t proposedDamage = 0;
  Combat::DamageActorProfile attacker;
  Combat::DamageActorProfile victim;
  Combat::DamageVector explicitDamage;
  bool hasAttackerProfile = false;
  bool hasVictimProfile = false;
  bool hasExplicitDamage = false;
  bool gothic2 = true;
  bool criticalHit = false;
  bool monsterWithoutWeapon = false;
  std::int64_t criticalMultiplier = Combat::DefaultCriticalDamageMultiplier;
  bool hasMeleeRoll = false;
  std::int64_t meleeTalentChance = 0;
  std::int64_t meleeRandomRoll = 0;
  Combat::DamageModifier modifier = Combat::DamageModifier::Normal;
  bool hasRangedRoll = false;
  bool projectileSpell = false;
  double projectileDistance = 0.0;
  double projectileWeaponChance = 0.0;
  double projectileRandomHitRoll = 0.0;
  bool projectileCriticalHit = false;
  Combat::FallDamageInput fall;
  bool hasFallInput = false;
  std::int64_t tolerance = DefaultObservedDamageTolerance;
};

struct Evaluation final {
  bool comparable = false;
  bool accepted = true;
  const char* reason = "damage_not_comparable";
  Combat::DamageResult minimum;
  Combat::DamageResult maximum;
};

[[nodiscard]] constexpr bool hasAnyDamage(const Combat::DamageVector& damage) noexcept {
  for(const auto value : damage.values) {
    if(value != 0)
      return true;
  }
  return false;
}

[[nodiscard]] constexpr bool validProfile(const Combat::DamageActorProfile& profile) noexcept {
  return profile.damageTypeMask != 0 || hasAnyDamage(profile.damage);
}

[[nodiscard]] constexpr Evaluation compareSingle(std::int64_t proposed,
                                                 Combat::DamageResult expected,
                                                 std::int64_t tolerance,
                                                 const char* acceptedReason) noexcept {
  const auto clampedTolerance = std::clamp<std::int64_t>(tolerance, 0, Combat::MaxDamageAmount);
  const auto lower = std::max<std::int64_t>(0, expected.value - clampedTolerance);
  const auto upper = std::min<std::int64_t>(Combat::MaxDamageAmount, expected.value + clampedTolerance);
  if(proposed < lower || proposed > upper)
    return {
      .comparable = true,
      .accepted = false,
      .reason = "damage_calculator_value_mismatch",
      .minimum = expected,
      .maximum = expected,
    };
  return {
    .comparable = true,
    .accepted = true,
    .reason = acceptedReason,
    .minimum = expected,
    .maximum = expected,
  };
}

[[nodiscard]] constexpr Evaluation compareBounds(std::int64_t proposed,
                                                 Combat::DamageBounds bounds,
                                                 std::int64_t tolerance,
                                                 const char* acceptedReason) noexcept {
  const auto clampedTolerance = std::clamp<std::int64_t>(tolerance, 0, Combat::MaxDamageAmount);
  const auto lower = std::max<std::int64_t>(0, bounds.minimum.value - clampedTolerance);
  const auto upper = std::min<std::int64_t>(Combat::MaxDamageAmount, bounds.maximum.value + clampedTolerance);
  if(proposed < lower || proposed > upper)
    return {
      .comparable = true,
      .accepted = false,
      .reason = "damage_calculator_bounds_mismatch",
      .minimum = bounds.minimum,
      .maximum = bounds.maximum,
    };
  return {
    .comparable = true,
    .accepted = true,
    .reason = acceptedReason,
    .minimum = bounds.minimum,
    .maximum = bounds.maximum,
  };
}

[[nodiscard]] constexpr Evaluation evaluateObservedDamage(const ObservedDamageInput& input) noexcept {
  if(input.proposedDamage < Combat::MinDamageAmount || input.proposedDamage > Combat::MaxDamageAmount)
    return {.comparable = true, .accepted = false, .reason = "combat_damage_invalid"};

  switch(input.kind) {
    case DamageKind::Melee: {
      if(!input.hasAttackerProfile || !input.hasVictimProfile ||
         !validProfile(input.attacker) || input.attacker.damageTypeMask == 0)
        return {};
      if(input.hasMeleeRoll) {
        const auto outcome = Combat::calculateMeleeDamageWithRoll({
          .attacker = input.attacker,
          .victim = input.victim,
          .gothic2 = input.gothic2,
          .monsterWithoutWeapon = input.monsterWithoutWeapon,
          .talentChance = input.meleeTalentChance,
          .randomRoll = input.meleeRandomRoll,
          .criticalMultiplier = input.criticalMultiplier,
        });
        return compareSingle(input.proposedDamage,
                             outcome.result,
                             input.tolerance,
                             "melee_damage_matches_gothic_roll");
      }
      auto melee = Combat::MeleeDamageInput {
        .attacker = input.attacker,
        .victim = input.victim,
        .gothic2 = input.gothic2,
        .criticalHit = input.criticalHit,
        .monsterWithoutWeapon = input.monsterWithoutWeapon,
        .criticalMultiplier = input.criticalMultiplier,
      };
      return compareBounds(input.proposedDamage,
                           Combat::calculateMeleeDamageBounds(melee),
                           input.tolerance,
                           "melee_damage_matches_gothic_bounds");
    }
    case DamageKind::Ranged: {
      if(!input.hasAttackerProfile || !input.hasVictimProfile ||
         !validProfile(input.attacker) || input.attacker.damageTypeMask == 0)
        return {};
      const auto damage = Combat::calculateRangedDamageValue({
        .attacker = input.attacker,
        .gothic2 = input.gothic2,
      });
      if(input.hasRangedRoll) {
        const auto outcome = Combat::calculateRangedDamageWithRoll({
          .damage = damage,
          .victimProtection = input.victim.protection,
          .modifier = input.modifier,
          .gothic2 = input.gothic2,
          .projectileSpell = input.projectileSpell,
          .distance = input.projectileDistance,
          .weaponChance = input.projectileWeaponChance,
          .randomHitRoll = input.projectileRandomHitRoll,
          .gothic1CriticalHit = input.projectileCriticalHit,
          .criticalMultiplier = input.criticalMultiplier,
        });
        return compareSingle(input.proposedDamage,
                             outcome.result,
                             input.tolerance,
                             "ranged_damage_matches_gothic_roll");
      }
      const auto expected = Combat::calculateVectorDamage({
        .damage = damage,
        .victimProtection = input.victim.protection,
        .modifier = input.modifier,
        .gothic2 = input.gothic2,
      });
      return compareSingle(input.proposedDamage, expected, input.tolerance, "ranged_damage_matches_gothic_value");
    }
    case DamageKind::Magic: {
      if(!input.hasExplicitDamage || !input.hasVictimProfile)
        return {};
      if(input.hasRangedRoll) {
        const auto outcome = Combat::calculateRangedDamageWithRoll({
          .damage = input.explicitDamage,
          .victimProtection = input.victim.protection,
          .modifier = input.modifier,
          .gothic2 = input.gothic2,
          .projectileSpell = true,
          .distance = input.projectileDistance,
          .weaponChance = input.projectileWeaponChance,
          .randomHitRoll = input.projectileRandomHitRoll,
          .gothic1CriticalHit = false,
          .criticalMultiplier = input.criticalMultiplier,
        });
        return compareSingle(input.proposedDamage,
                             outcome.result,
                             input.tolerance,
                             "magic_damage_matches_gothic_roll");
      }
      const auto expected = Combat::calculateVectorDamage({
        .damage = input.explicitDamage,
        .victimProtection = input.victim.protection,
        .modifier = input.modifier,
        .gothic2 = input.gothic2,
      });
      return compareSingle(input.proposedDamage, expected, input.tolerance, "magic_damage_matches_gothic_value");
    }
    case DamageKind::Fall: {
      if(!input.hasFallInput)
        return {};
      return compareSingle(input.proposedDamage,
                           Combat::calculateFallDamage(input.fall),
                           input.tolerance,
                           "fall_damage_matches_gothic_value");
    }
    case DamageKind::Unknown:
      return {};
  }
  return {};
}

} // namespace Mmo::Server::DamageCalculator
