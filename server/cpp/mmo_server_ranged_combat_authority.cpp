#include "mmo_server_ranged_combat_authority.h"

namespace Mmo::Server::RangedCombat {

static_assert(computeDamage({
                .game = GameVersion::Gothic2,
                .damageTypeMask = 1,
                .dexterity = 40,
                .weaponDamage = {20},
              })[0] == 60);

static_assert(computeDamage({
                .game = GameVersion::Gothic1,
                .damageTypeMask = 1,
                .dexterity = 40,
                .weaponDamage = {20},
              })[0] == 20);

static_assert(computeHitChance({
                .game = GameVersion::Gothic1,
                .dexterity = 35,
              }) > 0.34f);

static_assert(computeCritChance({
                .game = GameVersion::Gothic1,
                .weapon = WeaponKind::Crossbow,
                .crossbowTalent = 45,
              }) > 0.44f);

static_assert(!validateShot({
                 .weaponMode = FightMove::WeaponMode::OneHanded,
                 .hasAmmo = true,
                 .hasFocus = true,
                 .hasProjectileItem = true,
               }).legal);

} // namespace Mmo::Server::RangedCombat
