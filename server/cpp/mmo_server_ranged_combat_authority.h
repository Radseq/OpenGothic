#pragma once

#include <array>
#include <cstdint>

#include "mmo_server_fight_move_model.h"

namespace Mmo::Server::RangedCombat {

inline constexpr std::size_t MaxDamageTypes = 8;

enum class GameVersion : std::uint8_t {
  Gothic1,
  Gothic2,
};

enum class WeaponKind : std::uint8_t {
  Bow,
  Crossbow,
};

struct DamageInput final {
  GameVersion game = GameVersion::Gothic2;
  std::int32_t damageTypeMask = 0;
  std::int32_t dexterity = 0;
  std::array<std::int32_t, MaxDamageTypes> weaponDamage {};
};

struct ChanceInput final {
  GameVersion game = GameVersion::Gothic2;
  WeaponKind weapon = WeaponKind::Bow;
  std::int32_t dexterity = 0;
  std::int32_t bowTalent = 0;
  std::int32_t crossbowTalent = 0;
  std::int32_t bowHitChance = 0;
  std::int32_t crossbowHitChance = 0;
};

struct ShotValidationInput final {
  FightMove::WeaponMode weaponMode = FightMove::WeaponMode::Unknown;
  bool hasAmmo = false;
  bool hasFocus = false;
  bool hasProjectileItem = false;
};

struct ShotValidationResult final {
  bool legal = true;
  const char* reason = "ok";
};

struct ProjectileProfile final {
  std::array<std::int32_t, MaxDamageTypes> damage {};
  float hitChance = 1.f;
  float critChance = 0.f;
};

[[nodiscard]] constexpr bool isDamageTypeEnabled(std::int32_t mask, std::size_t index) noexcept {
  return index < MaxDamageTypes && (mask & (std::int32_t{1} << index)) != 0;
}

[[nodiscard]] constexpr std::array<std::int32_t, MaxDamageTypes>
computeDamage(const DamageInput& input) noexcept {
  std::array<std::int32_t, MaxDamageTypes> out {};
  const std::int32_t attributeBonus = input.game == GameVersion::Gothic2 ? input.dexterity : 0;
  for(std::size_t i = 0; i < MaxDamageTypes; ++i) {
    if(isDamageTypeEnabled(input.damageTypeMask, i))
      out[i] = attributeBonus + input.weaponDamage[i];
  }
  return out;
}

[[nodiscard]] constexpr float asChance(std::int32_t percent) noexcept {
  return static_cast<float>(percent) / 100.f;
}

[[nodiscard]] constexpr float computeHitChance(const ChanceInput& input) noexcept {
  if(input.game == GameVersion::Gothic1)
    return asChance(input.dexterity);
  return input.weapon == WeaponKind::Crossbow ? asChance(input.crossbowHitChance) : asChance(input.bowHitChance);
}

[[nodiscard]] constexpr float computeCritChance(const ChanceInput& input) noexcept {
  if(input.game != GameVersion::Gothic1)
    return 0.f;
  return input.weapon == WeaponKind::Crossbow ? asChance(input.crossbowTalent) : asChance(input.bowTalent);
}

[[nodiscard]] constexpr ShotValidationResult validateShot(const ShotValidationInput& input) noexcept {
  if(input.weaponMode != FightMove::WeaponMode::Bow && input.weaponMode != FightMove::WeaponMode::Crossbow)
    return {.legal = false, .reason = "shot_without_ranged_weapon"};
  if(!input.hasAmmo)
    return {.legal = false, .reason = "shot_without_ammo"};
  if(!input.hasProjectileItem)
    return {.legal = false, .reason = "shot_without_projectile_item"};
  if(!input.hasFocus)
    return {.legal = false, .reason = "shot_without_focus"};
  return {};
}

[[nodiscard]] constexpr ProjectileProfile buildProjectileProfile(const DamageInput& damage,
                                                                 const ChanceInput& chance) noexcept {
  return {
    .damage = computeDamage(damage),
    .hitChance = computeHitChance(chance),
    .critChance = computeCritChance(chance),
  };
}

} // namespace Mmo::Server::RangedCombat
