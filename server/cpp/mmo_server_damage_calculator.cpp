#include "mmo_server_damage_calculator.h"

namespace Mmo::Server {

namespace {

constexpr Combat::DamageActorProfile swordAttacker() noexcept {
  Combat::DamageActorProfile out;
  out.strength = 50;
  out.damageTypeMask = std::int64_t{1} << static_cast<std::size_t>(Combat::DamageType::Edge);
  out.damage.values[static_cast<std::size_t>(Combat::DamageType::Edge)] = 30;
  return out;
}

constexpr Combat::DamageActorProfile protectedVictim() noexcept {
  Combat::DamageActorProfile out;
  out.protection.values[static_cast<std::size_t>(Combat::DamageType::Edge)] = 20;
  out.protection.values[static_cast<std::size_t>(Combat::DamageType::Point)] = 10;
  return out;
}

constexpr Combat::DamageVector arrowDamage() noexcept {
  Combat::DamageVector out;
  out.values[static_cast<std::size_t>(Combat::DamageType::Point)] = 70;
  return out;
}

} // namespace

static_assert(Combat::calculateMeleeDamageWithRoll({
                .attacker = swordAttacker(),
                .victim = protectedVictim(),
                .gothic2 = true,
                .talentChance = 30,
                .randomRoll = 80,
              }).result.value == Combat::GothicMinDamage);

static_assert(Combat::calculateMeleeDamageWithRoll({
                .attacker = swordAttacker(),
                .victim = protectedVictim(),
                .gothic2 = true,
                .talentChance = 90,
                .randomRoll = 10,
              }).result.value == 60);

static_assert(Combat::rangedHitChanceAtDistance({
                .distance = 0.0,
                .weaponChance = 0.25,
                .gothic2 = true,
              }) == 1.0);

static_assert(!Combat::rangedProjectileHits({
                 .distance = static_cast<double>(Combat::DefaultMaxBowRange) + 1.0,
                 .weaponChance = 1.0,
                 .gothic2 = true,
               },
               0.0));

static_assert(Combat::calculateRangedDamageWithRoll({
                .damage = arrowDamage(),
                .victimProtection = protectedVictim().protection,
                .gothic2 = true,
                .distance = 100.0,
                .weaponChance = 1.0,
                .randomHitRoll = 0.0,
              }).result.value == 60);

static_assert(DamageCalculator::evaluateObservedDamage({
                .kind = DamageCalculator::DamageKind::Melee,
                .proposedDamage = 60,
                .attacker = swordAttacker(),
                .victim = protectedVictim(),
                .hasAttackerProfile = true,
                .hasVictimProfile = true,
                .gothic2 = true,
                .hasMeleeRoll = true,
                .meleeTalentChance = 90,
                .meleeRandomRoll = 10,
              }).accepted);

} // namespace Mmo::Server
