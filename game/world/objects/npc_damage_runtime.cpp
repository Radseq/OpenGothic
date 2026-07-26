#include "npc.h"

#include <algorithm>

#include <Tempest/Matrix4x4>
#include <Tempest/Log>

#include "graphics/mesh/skeleton.h"
#include "graphics/visualfx.h"
#include "game/damagecalculator.h"
#include "game/gamesession.h"
#include "game/serialize.h"
#include "game/gamescript.h"
#include "game/mmosemantichooks.h"
#include "utils/string_frm.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/world.h"
#include "utils/versioninfo.h"
#include "utils/fileext.h"
#include "utils/dbgpainter.h"
#include "camera.h"
#include "gothic.h"
#include "resources.h"

using namespace Tempest;

void Npc::commitDamage() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat))
    return;
  if(currentTarget==nullptr)
    return;
  if(!fghAlgo.isInAttackRange(*this,*currentTarget,owner.script()))
    return;
  if(!fghAlgo.isInFocusAngle(*this,*currentTarget))
    return;
  currentTarget->takeDamage(*this,nullptr);
  }

void Npc::takeDamage(Npc &other, const Bullet* b) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat))
    return;
  if(isDown())
    return;

  assert(b==nullptr || !b->isSpell());
  const auto& pose    = visual.pose();
  const bool  isJumpb = pose.isJumpBack(owner.tickCount()) && fghAlgo.isInJumpBackAngle(*this,other);
  const bool  isBlock = (!other.isMonster() || other.inventory().activeWeapon()!=nullptr) &&
                         fghAlgo.isInFocusAngle(*this,other) &&
                         pose.isDefence(owner.tickCount());

  lastHit = &other;
  if(!isPlayer())
    setOther(&other);
  owner.sendPassivePerc(*this,other,*this,PERC_ASSESSFIGHTSOUND);

  if(!(isBlock || isJumpb) || b!=nullptr) {
    takeDamage(other,b,COLL_DOEVERYTHING,0,false);
    } else {
    if(invent.activeWeapon()!=nullptr)
      visual.emitBlockEffect(*this,other);
    }
  }

void Npc::takeDamage(Npc& other, const Bullet* b, const VisualFx* vfx, int32_t splId) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat))
    return;
  if(isDown())
    return;

  lastHitSpell = splId;
  lastHit      = &other;
  if(!isPlayer())
    setOther(&other);

  CollideMask bMask = owner.script().canNpcCollideWithSpell(*this,&other,splId);
  if(bMask!=COLL_DONOTHING)
    Effect::onCollide(owner,vfx,position(),this,&other,splId);
  takeDamage(other,b,bMask,splId,true);
  }

void Npc::takeDamage(Npc& other, const Bullet* b, const CollideMask bMask, int32_t splId, bool isSpell) {
  // The full client may still play predicted attack animations, but a
  // server-owned replica must never accept local hit resolution or mutate its
  // gameplay attributes. Typed damage/life replication remains the only owner.
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat))
    return;
  float a  = angleDir(other.x-x,other.z-z);
  float da = a-angle;
  if(std::cos(da*M_PI/180.0)<0)
    lastHitType='A'; else
    lastHitType='B';

  DamageCalculator::Damage dmg={};
  DamageCalculator::Val    hitResult;
  SpellCategory            splCat     = SpellCategory::SPELL_BAD;
  const bool               dontKill   = ((b==nullptr && splId==0) || (bMask & COLL_DONTKILL)) && (!isSwim());
  int32_t                  damageType = DamageCalculator::damageTypeMask(other);

  if(isSpell) {
    auto& spl  = owner.script().spellDesc(splId);
    splCat     = SpellCategory(spl.spell_type);
    damageType = spl.damage_type;
    for(size_t i=0; i<zenkit::DamageType::NUM; ++i)
      if((damageType&(1<<i))!=0)
        dmg[i] = spl.damage_per_level;
    }

  if(!isSpell || splCat==SpellCategory::SPELL_BAD) {
    perceptionProcess(other,this,0,PERC_ASSESSDAMAGE);
    fghAlgo.onTakeHit();
    implFaiWait(0);
    }

  hitResult = DamageCalculator::damageValue(other,*this,b,isSpell,dmg,bMask);
  if(!isSpell && !isDown() && hitResult.hasHit)
    owner.addWeaponHitEffect(other,b,*this).play();

  if(isDown()) {
    onNoHealth(dontKill,HS_NoSound);
    return;
    }

  if(hitResult.hasHit) {
    auto state = bodyStateMasked();
    if(interactive()==nullptr && ((state&BS_FLAG_INTERRUPTABLE)!=BS_NONE || state==BS_RUN || state==BS_NONE)) {
      //NONE/RUN requires for monsters like waran
      const bool noInter = (hnpc->bodystate_interruptable_override!=0);
      if(!noInter) {
        //NOTE: kepp rotation animation: this results in more accurate fight with trolls
        // visual.setAnimRotate(*this,0);
        visual.interrupt(); // TODO: put down in pipeline, at Pose and merge with setAnimAngGet
        }

      if((damageType & (1<<zenkit::DamageType::FLY))==0)
        setAnimAngGet(lastHitType=='A' ? Anim::StumbleA  : Anim::StumbleB);
      }
    }

  // throw enemy
  if(hitResult.hasHit && (damageType & (1<<zenkit::DamageType::FLY))) {
    mvAlgo.accessDamFly(x-other.x, z-other.z, lastHitType);
    }

  if(hitResult.value>0) {
    currentOther = &other;
    changeAttribute(ATR_HITPOINTS,-hitResult.value,dontKill);

    if(bMask&(COLL_APPLYVICTIMSTATE|COLL_DOEVERYTHING)) {
      owner.sendPassivePerc(*this,other,*this,PERC_ASSESSOTHERSDAMAGE);
      if(isUnconscious()){
        owner.sendPassivePerc(*this,other,*this,PERC_ASSESSDEFEAT);
        }
      else if(isDead()) {
        owner.sendPassivePerc(*this,other,*this,PERC_ASSESSMURDER);
        }
      else {
        if(owner.script().rand(2)==0) {
          emitSoundSVM("SVM_%d_AARGH");
          }
        }
      }
    }
  }

void Npc::takeFallDamage(const Vec3& fallSpeed) {
  if(bodyStateMasked()==BS_FALL) {
    if(!isFallingDeep()) {
      // small fall
      setAnim(Anim::Idle);
      } else {
      const float a  = angleDir(-fallSpeed.x,-fallSpeed.z);
      const float da = a-angle;
      if(std::cos(da*M_PI/180.0)<0 || Vec2(fallSpeed.x,fallSpeed.z).length()<0.1f)
        lastHitType='A'; else
        lastHitType='B';
      setAnim(lastHitType=='A' ? Anim::FallenA : Anim::FallenB);
      }
    }
  auto dmg = DamageCalculator::damageFall(*this,fallSpeed.length());
  if(!dmg.hasHit)
    return;
  int32_t hp = attribute(ATR_HITPOINTS);
  if(hp>dmg.value) {
    emitSoundSVM("SVM_%d_AARGH");
    clearState(true);
    }
  changeAttribute(ATR_HITPOINTS,-dmg.value,false);
  }

void Npc::takeDrownDamage() {
  changeAttribute(Attribute::ATR_HITPOINTS, -attribute(Attribute::ATR_HITPOINTSMAX), false);
  }

Npc *Npc::updateNearestEnemy() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::TargetSelection))
    return nullptr;
  if(aiPolicy!=NpcProcessPolicy::AiNormal)
    return nullptr;

  Npc*  ret  = nullptr;
  float dist = std::numeric_limits<float>::max();
  if(nearestEnemy!=nullptr &&
     (!nearestEnemy->isDown() && canSenseNpc(*nearestEnemy,true)!=SensesBit::SENSE_NONE)) {
    ret  = nearestEnemy;
    dist = qDistTo(*ret);
    }

  owner.detectNpcNear([this,&ret,&dist](Npc& n){
    if(!isEnemy(n) || n.isDown() || &n==this)
      return;

    float d = qDistTo(n);
    if(d<dist && canSenseNpc(n,true)!=SensesBit::SENSE_NONE) {
      ret  = &n;
      dist = d;
      }
    });
  nearestEnemy = ret;
  return nearestEnemy;
  }

Npc* Npc::updateNearestBody() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::TargetSelection))
    return nullptr;
  if(aiPolicy!=NpcProcessPolicy::AiNormal)
    return nullptr;

  Npc*  ret  = nullptr;
  float dist = std::numeric_limits<float>::max();

  owner.detectNpcNear([this,&ret,&dist](Npc& n){
    if(!n.isDead())
      return;

    float d = qDistTo(n);
    if(d<dist && canSenseNpc(n,true)!=SensesBit::SENSE_NONE) {
      ret  = &n;
      dist = d;
      }
    });
  return ret;
  }

void Npc::tickTimedEvt(Animation::EvCount& ev) {
  if(ev.timed.empty())
    return;

  std::sort(ev.timed.begin(),ev.timed.end(),[](const Animation::EvTimed& a,const Animation::EvTimed& b){
    return a.time<b.time;
    });

  // https://auronen.cokoliv.eu/gmc/zengin/anims/events/
  for(auto& i:ev.timed) {
    switch(i.def) {
      case zenkit::MdsEventType::ITEM_CREATE: {
        if(auto it = invent.addItem(i.item,1,world())) {
          invent.putToSlot(*this,it->clsId(),i.slot[0]);
          }
        break;
        }
      case zenkit::MdsEventType::ITEM_INSERT: {
        invent.putCurrentToSlot(*this,i.slot[0]);
        break;
        }
      case zenkit::MdsEventType::ITEM_REMOVE:
      case zenkit::MdsEventType::ITEM_DESTROY: {
        invent.clearSlot(*this, "", i.def != zenkit::MdsEventType::ITEM_REMOVE);
        break;
        }
      case zenkit::MdsEventType::ITEM_PLACE: {
        if(currentInteract!=nullptr)
          Inventory::moveItem(*this, invent, *currentInteract);
        break;
        }
      case zenkit::MdsEventType::ITEM_EXCHANGE: {
        if(!invent.clearSlot(*this,i.slot[0],true)) {
          // fallback for cooking animations
          invent.putCurrentToSlot(*this,i.slot[0]);
          invent.clearSlot(*this,"",true);
          }
        if(auto it = invent.addItem(i.item,1,world())) {
          invent.putToSlot(*this,it->clsId(),i.slot[0]);
          }
        break;
        }
      case zenkit::MdsEventType::SET_FIGHT_MODE:
        break;
      case zenkit::MdsEventType::MUNITION_PLACE: {
        auto active=invent.activeWeapon();
        if(active!=nullptr) {
          const int32_t munition = active->handle().munition;
          invent.putAmmunition(*this,uint32_t(munition),i.slot[0]);
          }
        break;
        }
      case zenkit::MdsEventType::MUNITION_REMOVE: {
        invent.putAmmunition(*this,0,"");
        break;
        }
      case zenkit::MdsEventType::TORCH_DRAW:
        setTorch(true);
        break;
      case zenkit::MdsEventType::TORCH_INVENTORY:
        processDefInvTorch();
        break;
      case zenkit::MdsEventType::TORCH_DROP:
        dropTorch();
        break;
      case zenkit::MdsEventType::SOUND_DRAW:
        break;
      case zenkit::MdsEventType::SOUND_UNDRAW:
        break;
      case zenkit::MdsEventType::MESH_SWAP:
        break;
      case zenkit::MdsEventType::HIT_LIMB:
        break;
      case zenkit::MdsEventType::HIT_DIRECTION:
        break;
      case zenkit::MdsEventType::DAMAGE_MULTIPLIER:
        break;
      case zenkit::MdsEventType::PARRY_FRAME:
        break;
      case zenkit::MdsEventType::OPTIMAL_FRAME:
        break;
      case zenkit::MdsEventType::HIT_END:
        break;
      case zenkit::MdsEventType::COMBO_WINDOW:
        break;
      case zenkit::MdsEventType::UNKNOWN:
        break;
      }
    }
  }

void Npc::tickRegen(int32_t& v, const int32_t max, const int32_t chg, const uint64_t dt) {
  uint64_t tick = owner.tickCount();
  if(tick<dt || chg==0)
    return;
  int32_t time0 = int32_t(tick%1000);
  int32_t time1 = time0+int32_t(dt);

  int32_t val0 = (time0*chg)/1000;
  int32_t val1 = (time1*chg)/1000;

  int32_t nextV = std::max(0,std::min(v+val1-val0,max));
  if(v!=nextV) {
    v = nextV;
    // check health, in case of negative chg
    checkHealth(true,false);
    }
  }

void Npc::tickAnimationTags() {
  Animation::EvCount ev;
  const bool hasEvents = visual.processEvents(owner,lastEventTime,ev);
  visual.processLayers(owner);
  visual.setNpcEffect(owner,*this,hnpc->effect,hnpc->flags);
  if(!hasEvents)
    return;

  for(auto& i:ev.morph)
    visual.startMMAnim(*this,i.anim,i.node);
  if(ev.groundSounds>0 && isPlayer() && bodyStateMasked()!=BodyState::BS_SNEAK)
    world().sendImmediatePerc(*this,*this,*this,PERC_ASSESSQUIETSOUND);
  if(isMmoServerReplica() && !isPlayer()) {
    if(ev.def_opt_frame>0) {
      static_cast<void>(mmoAuthorityGate.rejectLocalGameplay(
          Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat));
    }
    if(!ev.timed.empty()) {
      static_cast<void>(mmoAuthorityGate.rejectLocalGameplay(
          Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::
              AnimationGameplayEvent));
    }
    implSetFightMode(ev);
    return;
  }
  if(ev.def_opt_frame>0)
    commitDamage();
  implSetFightMode(ev);
  tickTimedEvt(ev);
  }

void Npc::tick(uint64_t dt) {
  static bool dbg = false;
  static int  kId = 432;
  if(dbg && !isPlayer() && hnpc->id!=kId)
    return;

  assert(go2.flag!=GoToHint::GT_Enemy && go2.flag!=GoToHint::GT_EnemyG);

  tickAnimationTags();

  if(!visual.pose().hasAnim())
    setAnim(AnimationSolver::Idle);

  // A replicated NPC is a presentation proxy for server-owned gameplay. Keep
  // animation event processing client-side, but never run autonomous routines,
  // regeneration, perception or combat transitions for that entity.
  if(isMmoServerReplica() && !isPlayer())
    return;

  if(isDive()) {
    uint32_t gl = guild();
    int32_t  v  = world().script().guildVal().dive_time[gl]*1000;
    int32_t  t  = diveTime();
    if(v>=0 && t>v+int(dt)) {
      int tickSz = world().script().npcDamDiveTime();
      if(tickSz>0) {
        t-=v;
        int dmg = t/tickSz - (t-int(dt))/tickSz;
        if(dmg>0) {
          lastHit = nullptr;
          changeAttribute(ATR_HITPOINTS,-dmg,false);
          }
        }
      }
    }

  nextAiAction(aiQueueOverlay,dt);

  if(tickCast(dt))
    return;

  if(!isDead()) {
    tickRegen(hnpc->attribute[ATR_HITPOINTS],hnpc->attribute[ATR_HITPOINTSMAX],
              hnpc->attribute[ATR_REGENERATEHP],dt);
    tickRegen(hnpc->attribute[ATR_MANA],hnpc->attribute[ATR_MANAMAX],
              hnpc->attribute[ATR_REGENERATEMANA],dt);
    }

  if(waitTime>=owner.tickCount() || aniWaitTime>=owner.tickCount() || outWaitTime>owner.tickCount()) {
    if(!isPlayer() && go2.flag!=GT_Flee && faiWaitTime<owner.tickCount() && currentTarget!=nullptr) {
      implTurnToFai(*currentTarget,dt);
      }
    mvAlgo.tick(dt,MoveAlgo::WaitMove);
    return;
    }

  if(!isDown()) {
    implLookAtNpc(dt);
    implLookAtWp(dt);

    if(implAttack(dt))
      return;

    if(implGoTo(dt)) {
      if(go2.flag==GT_Flee)
        implAiTick(dt);
      return;
      }
    }

  mvAlgo.tick(dt);
  implAiTick(dt);
  }
