#pragma once

#include <cstdint>

#include <zenkit/addon/daedalus.hh>

#include "game/constants.h"

class Npc;
class Bullet;

class DamageCalculator {
  public:
    enum {
      MinDamage = 5, //TODO: NPC_MINIMAL_DAMAGE?
      };

    struct Damage final {
      int32_t  val[zenkit::DamageType::NUM] = {};
      int32_t& operator[](size_t i) { return val[i]; }
      const int32_t& operator[](size_t i) const { return val[i]; }
      void     operator *= (int32_t v) { for(auto& i:val) i*=v; }
      void     operator /= (int32_t v) { for(auto& i:val) i/=v; }
      };

    enum class Kind : uint8_t {
      Unknown,
      Melee,
      Ranged,
      Magic,
      Fall,
      };

    enum class Modifier : uint8_t {
      Normal,
      Double,
      Half,
      Blocked,
      };

    struct Val final {
      Val()=default;
      Val(int32_t v,bool b):value(v),hasHit(b){}
      Val(int32_t v,bool b,bool i):value(v),hasHit(b),invincible(i){}

      int32_t  value      = 0;
      bool     hasHit     = false;
      bool     invincible = false;
      Kind     kind       = Kind::Unknown;
      Modifier modifier   = Modifier::Normal;
      bool     criticalHit = false;
      bool     hasMeleeRoll = false;
      int32_t  meleeTalentChance = 0;
      int32_t  meleeRandomRoll = 0;
      bool     hasRangedRoll = false;
      bool     projectileSpell = false;
      float    projectileDistance = 0.f;
      float    projectileWeaponChance = 0.f;
      float    projectileRandomHitRoll = 0.f;
      bool     projectileCriticalHit = false;
      bool     hasExplicitDamage = false;
      Damage   explicitDamage = {};
      float    fallSpeed = 0.f;
      };

    static Val     damageValue(Npc& src, Npc& other, const Bullet* b, bool isSpell, const DamageCalculator::Damage& splDmg, const CollideMask bMsk);
    static Val     damageFall(Npc& src, float speed);
    static auto    rangeDamageValue(Npc& src) -> Damage;
    static int32_t damageTypeMask(Npc& npc);

  private:
    static bool    checkDamageMask(Npc& src, Npc& other, const Bullet* b);

    static Val     rangeDamage(Npc& src, Npc& other, const Bullet& b, const CollideMask bMsk);
    static Val     rangeDamage(Npc& src, Npc& other, Damage dmg, const CollideMask bMsk);
    static Val     swordDamage(Npc& src, Npc& other);
  };



