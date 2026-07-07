#include "damagecalculator.h"

#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/world.h"
#include "world/bullet.h"
#include "gothic.h"

// https://forum.worldofplayers.de/forum/threads/127320-Damage-System?p=2198181#post2198181
// https://strafkolonie-online.net/forum/board/thread/1895-info-erkl%C3%A4rung-der-berechnung-der-trefferchance-im-fernkampf/

static float mix(float x, float y, float a) {
  return x + (y-x)*a;
  }

static DamageCalculator::Modifier damageModifier(CollideMask bMsk) noexcept {
  if(bMsk & COLL_APPLYDOUBLEDAMAGE)
    return DamageCalculator::Modifier::Double;
  if(bMsk & COLL_APPLYHALVEDAMAGE)
    return DamageCalculator::Modifier::Half;
  if((bMsk & (COLL_APPLYDAMAGE | COLL_APPLYDOUBLEDAMAGE | COLL_APPLYHALVEDAMAGE | COLL_DOEVERYTHING))==0)
    return DamageCalculator::Modifier::Blocked;
  return DamageCalculator::Modifier::Normal;
  }

DamageCalculator::Val DamageCalculator::damageValue(Npc& src, Npc& other, const Bullet* b, bool isSpell, const DamageCalculator::Damage& splDmg, const CollideMask bMsk) {
  DamageCalculator::Val ret;
  if(b!=nullptr) {
    ret = rangeDamage(src,other,*b,bMsk);
    } else
  if(isSpell) {
    ret = rangeDamage(src,other,splDmg,bMsk);
    }
  else {
    ret = swordDamage(src,other);
    }

#if 0
  // debug
  ret.value = MinDamage;
#endif

  if(ret.hasHit && !ret.invincible && Gothic::inst().version().game==2)
    ret.value = std::max<int32_t>(ret.value,MinDamage);
  return ret;
  }

DamageCalculator::Val DamageCalculator::damageFall(Npc& npc, float speed) {
  auto  gl = npc.guild();
  auto& g  = npc.world().script().guildVal();

  float   gravity     = DynamicWorld::gravity;
  float   fallTime    = speed/gravity;
  float   height      = 0.5f*std::abs(gravity)*fallTime*fallTime;
  float   h0          = float(g.falldown_height[gl]);
  float   dmgPerMeter = float(g.falldown_damage[gl]);
  int32_t prot        = npc.protection(::PROT_FALL);

  Val ret;
  ret.invincible = (prot<0);
  ret.value      = int32_t(dmgPerMeter*(height-h0)/100.f - float(prot));
  if(ret.value<=0 || ret.invincible) {
    ret.value = 0;
    ret.kind = Kind::Fall;
    ret.fallSpeed = speed;
    return ret;
    }
  ret.hasHit = true;
  ret.kind = Kind::Fall;
  ret.fallSpeed = speed;
  return ret;
  }

DamageCalculator::Val DamageCalculator::rangeDamage(Npc& nsrc, Npc& nother, const Bullet& b, const CollideMask bMsk) {
  float dist       = b.pathLength();
  bool  noHit      = dist>float(MaxMagRange);
  bool  invincible = !checkDamageMask(nsrc,nother,&b);
  auto  dmg        = b.damage();
  Val   meta;
  meta.kind = b.isSpell() ? Kind::Magic : Kind::Ranged;
  meta.modifier = damageModifier(bMsk);
  meta.hasExplicitDamage = true;
  meta.explicitDamage = dmg;
  meta.projectileSpell = b.isSpell();
  meta.projectileDistance = dist;
  meta.projectileWeaponChance = b.hitChance();

  if(!b.isSpell()) {
    auto& script    = nsrc.world().script();
    float hitChance = float(script.rand(100))/100.f;
    float hitCh     = 0;
    bool  g2        = Gothic::inst().version().game==2;
    float refRange  = g2 ? ReferenceBowRangeG2 : ReferenceBowRangeG1;
    float maxRange  = float(MaxBowRange);
    float chance    = b.hitChance();
    meta.hasRangedRoll = true;
    meta.projectileRandomHitRoll = hitChance;

    if(dist<refRange)
      hitCh = mix(1.f, chance, (dist / refRange));
    else if(dist<maxRange)
      hitCh = mix(chance, 0.f, (dist-refRange) / (maxRange-refRange));
    else
      hitCh = 0;

    noHit = (dist>float(MaxBowRange) || hitCh<=hitChance);

    if(!g2 && !noHit && !invincible) {
      const int32_t mul        = script.criticalDamageMultiplyer();
      const int     critChance = int(script.rand(100));
      meta.projectileCriticalHit = std::lround(100.f * b.critChance())>critChance;
      if(meta.projectileCriticalHit)
        dmg *= mul;
      }
    }

  if(noHit) {
    meta.value = 0;
    meta.hasHit = false;
    meta.invincible = invincible;
    return meta;
    }

  if(invincible) {
    meta.value = 0;
    meta.hasHit = true;
    meta.invincible = true;
    return meta;
    }

  if(meta.modifier == Modifier::Blocked) {
    meta.value = 0;
    meta.hasHit = true;
    meta.invincible = true;
    return meta;
    }

  auto ret = rangeDamage(nsrc,nother,dmg,bMsk);
  ret.kind = meta.kind;
  ret.modifier = meta.modifier;
  ret.hasExplicitDamage = meta.hasExplicitDamage;
  ret.explicitDamage = meta.explicitDamage;
  ret.hasRangedRoll = meta.hasRangedRoll;
  ret.projectileSpell = meta.projectileSpell;
  ret.projectileDistance = meta.projectileDistance;
  ret.projectileWeaponChance = meta.projectileWeaponChance;
  ret.projectileRandomHitRoll = meta.projectileRandomHitRoll;
  ret.projectileCriticalHit = meta.projectileCriticalHit;
  return ret;
  }

DamageCalculator::Val DamageCalculator::rangeDamage(Npc&, Npc& nother, Damage dmg, const CollideMask bMsk) {
  auto& other = nother.handle();
  const auto rawDamage = dmg;

  if(bMsk & COLL_APPLYDOUBLEDAMAGE)
    dmg*=2;
  if(bMsk & COLL_APPLYHALVEDAMAGE)
    dmg/=2;

  int  value = 0;
  bool invincible = true;
  for(unsigned int i=0; i<zenkit::DamageType::NUM; ++i) {
    if(dmg[size_t(i)]==0)
      continue;
    int vd = std::max(dmg[size_t(i)] - other.protection[i],0);
    if(other.protection[i]>=0) { // Filter immune
      value     += vd;
      invincible = false;
      }
    }

  Val ret(value,true,invincible);
  ret.kind = Kind::Magic;
  ret.modifier = damageModifier(bMsk);
  ret.hasExplicitDamage = true;
  ret.explicitDamage = rawDamage;
  return ret;
  }

DamageCalculator::Val DamageCalculator::swordDamage(Npc& nsrc, Npc& nother) {
  if(!checkDamageMask(nsrc,nother,nullptr))
    return Val(0,true,true);

  auto& script = nsrc.world().script();
  auto& src    = nsrc.handle();
  auto& other  = nother.handle();

  // Swords/Fists
  const int dtype      = damageTypeMask(nsrc);
  Talent    tal        = TALENT_UNKNOWN;
  int       str        = nsrc.attribute(Attribute::ATR_STRENGTH);
  int       critChance = int(script.rand(100));

  int value = 0;

  if(auto w = nsrc.inventory().activeWeapon()) {
    if(w->is2H())
      tal = TALENT_2H; else
      tal = TALENT_1H;
    }

  if(Gothic::inst().version().game==2) {
    if(nsrc.isMonster() && tal==TALENT_UNKNOWN) {
      // regular monsters always do critical damage
      critChance = -1;
      }

    bool invincible = true;
    for(unsigned int i=0; i<zenkit::DamageType::NUM; ++i) {
      if((dtype & (1<<i))==0)
        continue;
      int vd = std::max(str + src.damage[i] - other.protection[i],0);
      if(src.hitchance[tal]<=critChance)
        vd = (vd-1)/10;
      if(other.protection[i]>=0) { // Filter immune
        value += vd;
        invincible = false;
        }
      }

    Val ret(value,true,invincible);
    ret.kind = Kind::Melee;
    ret.hasMeleeRoll = true;
    ret.meleeTalentChance = src.hitchance[tal];
    ret.meleeRandomRoll = critChance;
    ret.criticalHit = src.hitchance[tal] > critChance;
    return ret;
    } else {
    bool invincible = true;
    const int32_t mul = script.criticalDamageMultiplyer();
    for(unsigned int i=0; i<zenkit::DamageType::NUM; ++i) {
      if((dtype & (1<<i))==0)
        continue;
      int vd = 0;
      if(nsrc.talentValue(tal)<=critChance)
        vd = std::max(str +     src.damage[i] - other.protection[i],0); else
        vd = std::max(str + mul*src.damage[i] - other.protection[i],0);
      if(other.protection[i]>=0) { // Filter immune
        value += vd;
        invincible = false;
        }
      }

    Val ret(value,true,invincible);
    ret.kind = Kind::Melee;
    ret.hasMeleeRoll = true;
    ret.meleeTalentChance = nsrc.talentValue(tal);
    ret.meleeRandomRoll = critChance;
    ret.criticalHit = nsrc.talentValue(tal) > critChance;
    return ret;
    }
  }

int32_t DamageCalculator::damageTypeMask(Npc& npc) {
  if(auto w = npc.inventory().activeWeapon())
    return w->handle().damage_type;
  return npc.handle().damage_type;
  }

bool DamageCalculator::checkDamageMask(Npc& nsrc, Npc& nother, const Bullet* b) {
  auto& other = nother.handle();

  if(b!=nullptr) {
    auto dmg = b->damage();
    for(unsigned int i=0;i<zenkit::DamageType::NUM;++i) {
      if(dmg[size_t(i)]>0 && other.protection[i]>=0)
        return true;
      }
    } else {
    const int dtype = damageTypeMask(nsrc);
    for(unsigned int i=0;i<zenkit::DamageType::NUM;++i){
      if((dtype & (1<<i))==0)
        continue;
      return true;
      }
    }

  return false;
  }

DamageCalculator::Damage DamageCalculator::rangeDamageValue(Npc& src) {
  const int dtype = damageTypeMask(src);
  int d = Gothic::inst().version().game==2 ? src.attribute(Attribute::ATR_DEXTERITY) : 0;
  Damage ret={};
  for(unsigned int i=0;i<zenkit::DamageType::NUM;++i){
    if((dtype & (1<<i))==0)
      continue;
    ret[size_t(i)] = d + src.handle().damage[i];
    }
  return ret;
  }


