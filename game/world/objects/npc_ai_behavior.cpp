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

[[nodiscard]] static constexpr WeaponState weaponStateFromAnimationEvent(
    const zenkit::MdsFightMode mode) noexcept {
  switch(mode) {
    case zenkit::MdsFightMode::NONE: return WeaponState::NoWeapon;
    case zenkit::MdsFightMode::FIST: return WeaponState::Fist;
    case zenkit::MdsFightMode::SINGLE_HANDED: return WeaponState::W1H;
    case zenkit::MdsFightMode::DUAL_HANDED: return WeaponState::W2H;
    case zenkit::MdsFightMode::BOW: return WeaponState::Bow;
    case zenkit::MdsFightMode::CROSSBOW: return WeaponState::CBow;
    case zenkit::MdsFightMode::MAGIC: return WeaponState::Mage;
    case zenkit::MdsFightMode::INVALID: return WeaponState::NoWeapon;
  }
  return WeaponState::NoWeapon;
}

int Npc::aiOutputOrderId() const {
  return aiQueue.aiOutputOrderId();
  }

bool Npc::performOutput(const AiQueue::AiAction &act) {
  if(act.target==nullptr) //FIXME: target is null after loading
    return true;
  const int order = act.target->aiOutputOrderId();
  if(order<act.i0)
    return false;
  if(aiOutputBarrier>owner.tickCount() && act.target==this && !isPlayer())
    return false;
  if(aiPolicy>=NpcProcessPolicy::AiFar)
    return true; // don't waste CPU on far-away svm-talks
  //if(act.act!=AI_OutputSvmOverlay && bodyStateMasked()!=BS_STAND)
  //  return false;
  if(act.act==AI_Output           && outputPipe->output   (*this,act.s0))
    return true;
  auto svm = owner.script().messageFromSvm(act.s0,hnpc->voice);
  if(act.act==AI_OutputSvm        && outputPipe->outputSvm(*this,svm))
    return true;
  if(act.act==AI_OutputSvmOverlay && outputPipe->outputOv(*this,svm))
    return true;
  return false;
  }

bool Npc::implPointAt(const Tempest::Vec3& to) {
  auto    dpos = to-position();
  uint8_t comb = Pose::calcAniComb(dpos,angle);

  return (setAnimAngGet(Npc::Anim::PointAt,comb)!=nullptr);
  }

bool Npc::implLookAtWp(uint64_t dt) {
  if(currentLookAt==nullptr)
    return false;
  auto dvec = currentLookAt->position();
  return implLookAt(dvec.x,dvec.y,dvec.z,dt);
  }

bool Npc::implLookAtNpc(uint64_t dt) {
  if(currentLookAtNpc==nullptr)
    return false;
  auto selfHead  = visual.mapHeadBone();
  auto otherHead = currentLookAtNpc->visual.mapHeadBone();
  auto dvec = otherHead - selfHead;
  return implLookAt(dvec.x,dvec.y,dvec.z,dt);
  }

bool Npc::implLookAt(float dx, float dy, float dz, uint64_t dt) {
  static const float rotSpeed = 200; // deg per second
  static const float maxRot   = 80; // maximum rotation
  Vec2 dst;

  dst.x = visual.viewDirection()-angleDir(dx,dz);
  while(dst.x>180)
    dst.x -= 360;
  while(dst.x<-180)
    dst.x += 360;

  dst.y = std::atan2(dy,std::sqrt(dx*dx+dz*dz));
  dst.y = dst.y*180.f/float(M_PI);

  if(dst.x<-maxRot || dst.x>maxRot) {
    dst.x = 0;
    dst.y = 0;
    }

  if(dst.y<-20)
    dst.y = -20;
  if(dst.y>20)
    dst.y = 20;

  auto rot  = visual.headRotation();
  auto drot = dst-rot;

  drot.x = std::min(std::abs(drot.x),rotSpeed*float(dt)/1000.f);
  drot.y = std::min(std::abs(drot.y),rotSpeed*float(dt)/1000.f);
  if(dst.x<rot.x)
    drot.x = -drot.x;
  if(dst.y<rot.y)
    drot.y = -drot.y;

  rot+=drot;
  visual.setHeadRotation(rot.x,rot.y);

  return false;
  }

bool Npc::implTurnAway(const Npc &oth, uint64_t dt) {
  if(&oth==this)
    return true;

  // turn npc's back to oth, so calculate direction from oth to npc
  auto dx = x-oth.x;
  auto dz = z-oth.z;
  auto  gl   = guild();
  float step = float(owner.script().guildVal().turn_speed[gl]);
  return rotateTo(dx,dz,step,AnimationSolver::TurnType::Std,dt);
  }

bool Npc::implTurnToFai(const Npc& oth, uint64_t dt) {
  if(&oth==this || oth.isDown())
    return false;

  auto ws = weaponState();
  if(ws==WeaponState::NoWeapon)
    return false;

  auto  gl   = guild();
  auto& gv   = owner.script().guildVal();
  float step = float(gv.turn_speed[gl]);
  //auto  dpos = fghAlgo.distVec(*currentTarget, *this);
  auto dpos = currentTarget->collosionCenter() - collosionCenter();

  // vanilla has a bug(or quirk) apparently, for that
  // also would need to fallthru in FAI code, if no animation is performed
  bool skipAnim = gv.turn_speed[gl] >= 100;
  auto anim = skipAnim ? AnimationSolver::TurnType::None : AnimationSolver::TurnType::Std;
  if(ws==WeaponState::Bow || ws==WeaponState::CBow || ws==WeaponState::Mage) {
    anim = AnimationSolver::TurnType::None;
    }

  auto bs = bodyStateMasked();
  if(bs!=BS_HIT) {
    //NOTE: Troll rotates during the hit, but not very fast - seem to be regulat speed
    step *= 2.f; // faster in combat
    }
  return rotateTo(dpos.x,dpos.z,step,anim,dt) && !skipAnim;
  }

bool Npc::implTurnTo(const Npc &oth, uint64_t dt) {
  if(&oth==this)
    return false;
  auto dx = oth.x-x;
  auto dz = oth.z-z;
  return implTurnTo(dx,dz,AnimationSolver::TurnType::Std,dt);
  }

bool Npc::implTurnTo(const Npc& oth, AnimationSolver::TurnType anim, uint64_t dt) {
  if(&oth==this)
    return false;
  auto dx = oth.x-x;
  auto dz = oth.z-z;
  return implTurnTo(dx,dz,anim,dt);
  }

bool Npc::implTurnTo(const WayPoint* wp, AnimationSolver::TurnType anim, uint64_t dt) {
  if(wp==nullptr)
    return false;
  return implTurnTo(wp->dir.x,wp->dir.z,anim,dt);
  }

bool Npc::implTurnTo(float dx, float dz, AnimationSolver::TurnType anim, uint64_t dt) {
  auto  gl   = guild();
  float step = float(owner.script().guildVal().turn_speed[gl]);
  return rotateTo(dx,dz,step,anim,dt);
  }

bool Npc::implWhirlTo(const Npc &oth, uint64_t dt) {
  return implTurnTo(oth,AnimationSolver::TurnType::Whirl,dt);
  }

bool Npc::implGoTo(uint64_t dt) {
  float dist = 0;
  if(go2.npc) {
    dist = fghAlgo.prefferedAttackDistance(*this,*go2.npc,owner.script());
    } else {
    // use smaller threshold, to avoid edge-looping in script
    dist = MoveAlgo::closeToPointThreshold*0.5f;
    if(!mvAlgo.checkLastBounce())
      dist = MoveAlgo::closeToPointThreshold*1.5f;
    if(go2.wp!=nullptr && go2.wp->useCounter()>1)
      dist = float(MAX_AI_USE_DISTANCE);
    }
  return implGoTo(dt,dist);
  }

bool Npc::implGoTo(uint64_t dt, float destDist) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementPlanning))
    return false;
  if(go2.flag==GT_No)
    return false;

  if(isInAir() || interactive()!=nullptr) {
    mvAlgo.tick(dt);
    return true;
    }

  auto target = go2.target();
  auto dpos   = target - position();

  if(go2.flag==GT_Flee) {
    // nop
    }
  else if(go2.isClose(*this, destDist)) {
    bool finished = true;
    if(go2.flag==GT_Way) {
      go2.wp = go2.wp->hasLadderConn(wayPath.first()) ? wayPath.first() : wayPath.pop();
      if(go2.wp!=nullptr) {
        attachToPoint(go2.wp);
        if(setGoToLadder()) {
          mvAlgo.tick(dt);
          return true;
          }
        finished = false;
        }
      }
    if(finished) {
      if(go2.flag==Npc::GT_NextFp && implTurnTo(go2.wp,AnimationSolver::TurnType::Std,dt))
        return true;
      clearGoTo();
      }
    }
  else {
    if(setGoToLadder()) {
      mvAlgo.tick(dt);
      return true;
      }
    if(mvAlgo.checkLastBounce() && implTurnTo(dpos.x,dpos.z,AnimationSolver::TurnType::Std,dt)) {
      mvAlgo.tick(dt);
      return true;
      }
    }

  if(!go2.empty()) {
    setAnim(AnimationSolver::Move);
    mvAlgo.tick(dt);
    return true;
    }
  return false;
  }

bool Npc::implAttack(uint64_t dt) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat))
    return false;
  if(currentTarget==nullptr || isPlayer() || isTalk())
    return false;

  if(currentTarget->isDown()){
    // NOTE: don't clear internal target, to make scripts happy
    // currentTarget=nullptr;
    fghAlgo.onClearTarget();
    return false;
    }

  if(aiQueue.size()>0) {
    // do not messup weapon change animations by MOVE intruction
    return false;
    }

  const auto ws = weaponState();
  const auto bs = bodyStateMasked();

  if(bs==BS_HIT && (ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H)) {
    //NOTE: 'storm' attack has BS_RUN state and not meant to be auto-rotated
    implTurnToFai(*currentTarget,dt);
    mvAlgo.tick(dt,MoveAlgo::FaiMove);
    return true;
    }

  if(!fghAlgo.hasInstructions())
    return false;

  if(bs==BS_LIE) {
    setAnim(Npc::Anim::Idle);
    mvAlgo.tick(dt,MoveAlgo::FaiMove);
    return true;
    }
  if(bs==BS_STUMBLE || bs==BS_FALL || isInAir()) {
    mvAlgo.tick(dt,MoveAlgo::FaiMove);
    return true;
    }

  if(faiWaitTime>=owner.tickCount() || waitTime>=owner.tickCount()) {
    implTurnToFai(*currentTarget,dt);
    mvAlgo.tick(dt,MoveAlgo::FaiMove);
    return true;
    }

  const auto act = fghAlgo.nextFromQueue(*this,*currentTarget,owner.script());

  // NOTE: in original-game, this behaviour seem to be hardcoded
  // test case: wolf jump-back quite often when close, but programmed to jump only if attacked
  // so far promoting wait to jump seem to work best
  const bool jmp = fghAlgo.isInCloseupRange(*this,*currentTarget,owner.script()) && fghAlgo.isInFocusAngle(*this,*currentTarget);

  // vanilla behavior, required for orcs in G1 orcgraveyard
  if(ws==WeaponState::NoWeapon && isAiQueueEmpty() && canSwitchWeapon()) {
    drawWeaponMelee();
    return true;
    }

  if(act==FightAlgo::MV_BLOCK) {
    if(!fghAlgo.isInFocusAngle(*this, *currentTarget)) {
      fghAlgo.consumeAction();
      return true;
      }

    switch(ws) {
      case WeaponState::Fist: {
        if(blockFist())
          fghAlgo.consumeAction();
        break;
        }
      case WeaponState::W1H:
      case WeaponState::W2H: {
        if(blockSword())
          fghAlgo.consumeAction();
        break;
        }
      default:
        fghAlgo.consumeAction();
        break;
      }
    return true;
    }

  if(act==FightAlgo::MV_ATTACK || act==FightAlgo::MV_ATTACKL || act==FightAlgo::MV_ATTACKR) {
    //NOTE: FIGHT_DIST_CANCEL in scipts is often longer, than senses_range of npc
    const auto sense = fghAlgo.isInFocusAngle(*this,*currentTarget,5.f);
    if(!sense) {
      implTurnToFai(*currentTarget,dt);
      mvAlgo.tick(dt,MoveAlgo::FaiMove);
      return true;
      }
#if 0
    fghAlgo.consumeAction(); //debug
    return true;
#endif

    static const Anim ani[4] = {Anim::Attack, Anim::AttackL, Anim::AttackR};
    if((act!=FightAlgo::MV_ATTACK && bodyState()!=BS_RUN) &&
       !fghAlgo.isInWRange(*this,*currentTarget,owner.script())) {
      fghAlgo.consumeAction();
      return true;
      }

    if(ws==WeaponState::Bow || ws==WeaponState::CBow || ws==WeaponState::Mage) {
      bool obsticle = false;
      if(currentTarget!=nullptr) {
        auto hit = owner.physic()->rayNpc(this->mapWeaponBone(),currentTarget->centerPosition(),this);
        if(hit.hasCol && hit.npcHit!=currentTarget) {
          obsticle = true;
          // if(hit.npcHit!=nullptr && owner.script().personAttitude(*this,*hit.npcHit)==ATT_HOSTILE)
          //   obsticle = false;
          if(hit.npcHit!=nullptr && hit.npcHit!=currentTarget && owner.script().isFriendlyFire(*this,*hit.npcHit))
            obsticle = false;
          }
        }
      if(auto spl = activeWeapon()) {
        if(spl->isSpell() && !spl->isSpellShoot())
          obsticle = false;
        }
      if(obsticle) {
        auto anim = (owner.script().rand(2)==0 ? Npc::Anim::MoveL : Npc::Anim::MoveR);
        if(setAnim(anim)){
          visual.setAnimRotate(*this,0);
          implFaiWait(visual.pose().animationTotalTime());
          fghAlgo.consumeAction();
          return true;
          }
        }
      }

    if(ws==WeaponState::Mage) {
      const auto cast = beginCastSpell();
      if(cast==BeginCastResult::BC_No)
        return false;
      fghAlgo.consumeAction();
      }
    else if(ws==WeaponState::Bow || ws==WeaponState::CBow) {
      if(shootBow()) {
        fghAlgo.consumeAction();
        }
      else if(!implTurnToFai(*currentTarget,dt)) {
        aimBow();
        }
      }
    else if(ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H) {
      const auto hit = owner.physic()->ray(this->collosionCenter(),currentTarget->collosionCenter());
      if(hit.hasCol) {
        // blocked by wall
        fghAlgo.consumeAction();
        return true;
        }
      const auto atkType = (ws==WeaponState::Fist) ? Anim::Attack : ani[act-FightAlgo::MV_ATTACK];
      const bool atk     = doAttack(atkType, BS_HIT);

      if(atk || mvAlgo.isSwim() || mvAlgo.isDive()) {
        uint64_t aniTime = visual.pose().atkTotalTime()+1;
        implFaiWait(aniTime);
        if(bs==BS_RUN)
          implAniWait(aniTime);
        fghAlgo.consumeAction();
        } else {
        implTurnToFai(*currentTarget,dt);
        }
      }
    else {
      // Attack action without any weapon. Can happend at weapon transition(orc shaman) - skip it.
      fghAlgo.consumeAction();
      }
    return true;
    }

  if(act==FightAlgo::MV_TURN2HIT) {
    if(!implTurnTo(*currentTarget,dt))
      fghAlgo.consumeAction();
    return true;
    }

  if(act==FightAlgo::MV_STRAFEL) {
    if(setAnim(Npc::Anim::MoveL)) {
      visual.setAnimRotate(*this,0);
      implFaiWait(visual.pose().animationTotalTime());
      fghAlgo.consumeAction();
      }
    else if(!hasAnim(Npc::Anim::MoveL)) {
      // avoid soft-locks
      visual.setAnimRotate(*this,0);
      fghAlgo.consumeAction();
      }
    return true;
    }

  if(act==FightAlgo::MV_STRAFER) {
    if(setAnim(Npc::Anim::MoveR)) {
      visual.setAnimRotate(*this,0);
      implFaiWait(visual.pose().animationTotalTime());
      fghAlgo.consumeAction();
      }
    else if(!hasAnim(Npc::Anim::MoveR)) {
      // avoid soft-locks
      visual.setAnimRotate(*this,0);
      fghAlgo.consumeAction();
      }
    return true;
    }

  if(act==FightAlgo::MV_STRAFE_E) {
    // finalize strafe
    if(!setAnim(Npc::Anim::Idle))
      return false;
    fghAlgo.consumeAction();
    return true;
    }

  if(act==FightAlgo::MV_JUMPBACK || (act==FightAlgo::MV_WAIT && jmp) || (act==FightAlgo::MV_TURN && jmp)) {
    if(isSwim()) {
      fghAlgo.consumeAction();
      return true;
      }
    if(bodyStateMasked()==BS_PARADE) {
      fghAlgo.consumeAction();
      return true;
      }
    if(!fghAlgo.isInFocusAngle(*this, *currentTarget) && !jmp) {
      //NOTE: jump-back is ultimate defence, so better to use it only if npc face player directly
      fghAlgo.consumeAction();
      aiState.loopNextTime = owner.tickCount(); // force ZS_MM_Attack_Loop call
      return true;
      }
    if(setAnim(Npc::Anim::MoveBack)) {
      implFaiWait(visual.pose().animationTotalTime());
      fghAlgo.consumeAction();
      }
    return true;
    }

  if(act==FightAlgo::MV_MOVE || act==FightAlgo::MV_TURN) {
    if(currentTarget->isDown()) {
      if(setAnim(Anim::Idle))
        fghAlgo.consumeAction();
      return true;
      }

    const bool prGRange = fghAlgo.isInGRange(*this, *currentTarget, owner.script());
    const bool prWRange = fghAlgo.isInWRange(*this, *currentTarget, owner.script());
    const auto prBs     = bs;

    const float distance = qDistTo(*currentTarget);
    const float range    = float(handle().senses_range);

    if(!prGRange && distance<range*range) {
      // if npc is reasonably far, we can switch to propper pathfinding
      const auto hit = owner.physic()->ray(this->collosionCenter(), currentTarget->collosionCenter());
      if(hit.hasCol) {
        auto near = owner.findWayPoint(currentTarget->position(), [this](const WayPoint &wp) {
          if(!currentTarget->canRayHitPoint(wp.pos))
            return false;
          return true;
          });
        if(near!=nullptr) {
          if(near!=wayPath.last()) {
            wayPath = owner.wayTo(*this,*near);
            go2.set(wayPath.first(), GT_Way);
            }
          return false;
          }
        }
      }

    if(prWRange) {
      //NOTE: bloodfly and other monsters may run to close to player otherwise
      //NOTE2: also for bloodfly we have to use 'hard-stop', to avoid trailing flight
      visual.stopWalkAnim(*this);
      //setAnim(Anim::Idle);
      implTurnToFai(*currentTarget,dt);
      } else {
      if(mvAlgo.checkLastBounce()) {
        if(implTurnToFai(*currentTarget,dt))
          return true;
        }
      setAnim(AnimationSolver::Move);
      go2.set(currentTarget, GT_Enemy);
      mvAlgo.tick(dt, MoveAlgo::FaiMove);
      go2.clear();
      wayPath.clear();
      }

    const bool isGRange = fghAlgo.isInGRange(*this, *currentTarget, owner.script());
    const bool isWRange = fghAlgo.isInWRange(*this, *currentTarget, owner.script());
    const bool isFocus  = fghAlgo.isInFocusAngle(*this, *currentTarget, 5.f);

    if((isWRange || (isGRange!=prGRange) || prBs!=bodyStateMasked()) && isFocus) {
      visual.setAnimRotate(*this, 0);
      fghAlgo.consumeAction();
      aiState.loopNextTime = owner.tickCount(); // force ZS_MM_Attack_Loop call
      implAiTick(dt);
      return true;
      }

    implAiTick(dt);
    return true;
    }

  if(act==FightAlgo::MV_WAIT) {
    implFaiWait(200);
    fghAlgo.consumeAction();
    stopWalkAnimation();
    return true;
    }

  if(act==FightAlgo::MV_WAITLONG) {
    implFaiWait(300);
    fghAlgo.consumeAction();
    stopWalkAnimation();
    return true;
    }

  if(act==FightAlgo::MV_NULL) {
    fghAlgo.consumeAction();
    stopWalkAnimation();
    return true;
    }

  return true;
  }

bool Npc::implAiTick(uint64_t dt) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::AiQueue))
    return false;
  // Note AI-action queue takes priority, test case: Vatras pray at night
  if(aiQueue.size()==0) {
    tickRoutine();
    if(aiQueue.size()>0)
      nextAiAction(aiQueue,dt);
    return false;
    }
  nextAiAction(aiQueue,dt);
  return true;
  }

void Npc::implAiWait(uint64_t dt) {
  auto w = owner.tickCount()+dt;
  if(w>waitTime)
    waitTime = w;
  }

void Npc::implAniWait(uint64_t dt) {
  auto w = owner.tickCount()+dt;
  if(w>aniWaitTime)
    aniWaitTime = w;
  }

void Npc::implFaiWait(uint64_t dt) {
  faiWaitTime          = owner.tickCount()+dt;
  aiState.loopNextTime = faiWaitTime;
  }

void Npc::implSetFightMode(const Animation::EvCount& ev) {
  if(ev.weaponCh==zenkit::MdsFightMode::INVALID)
    return;
  const auto eventMode = weaponStateFromAnimationEvent(ev.weaponCh);
  WeaponState ws = visual.fightMode();
  if(isMmoServerReplica()) {
    if(eventMode != mmoPresentationWeaponMode)
      return;
    if(mmoPresentationWeaponTransitionPending)
      ws = mmoPresentationWeaponTransitionFrom;
    static_cast<void>(visual.setToFightMode(mmoPresentationWeaponMode));
    mmoPresentationWeaponTransitionPending = false;
  } else if(!visual.setFightMode(ev.weaponCh)) {
    return;
  }

  if(ev.weaponCh==zenkit::MdsFightMode::NONE && (ws==WeaponState::W1H || ws==WeaponState::W2H)) {
    if(auto melee = invent.currentMeleeWeapon()) {
      auto at = centerPosition();
      if(melee->handle().material==ItemMaterial::MAT_METAL)
        sfxWeapon = ::Sound(owner,::Sound::T_Regular,"UNDRAWSOUND_ME.WAV",at,2500,false); else
        sfxWeapon = ::Sound(owner,::Sound::T_Regular,"UNDRAWSOUND_WO.WAV",at,2500,false);
      sfxWeapon.play();
      } else if(isMmoServerReplica()) {
      auto at = centerPosition();
      sfxWeapon = ::Sound(owner,::Sound::T_Regular,"UNDRAWSOUND_ME.WAV",at,2500,false);
      sfxWeapon.play();
      }
    }
  else if(ev.weaponCh==zenkit::MdsFightMode::SINGLE_HANDED || ev.weaponCh==zenkit::MdsFightMode::DUAL_HANDED) {
    if(auto melee = invent.currentMeleeWeapon()) {
      auto at = centerPosition();
      if(melee->handle().material==ItemMaterial::MAT_METAL)
        sfxWeapon = ::Sound(owner,::Sound::T_Regular,"DRAWSOUND_ME.WAV",at,2500,false); else
        sfxWeapon = ::Sound(owner,::Sound::T_Regular,"DRAWSOUND_WO.WAV",at,2500,false);
      sfxWeapon.play();
      } else if(isMmoServerReplica()) {
      auto at = centerPosition();
      sfxWeapon = ::Sound(owner,::Sound::T_Regular,"DRAWSOUND_ME.WAV",at,2500,false);
      sfxWeapon.play();
      }
    }
  else if(ev.weaponCh==zenkit::MdsFightMode::BOW || ev.weaponCh==zenkit::MdsFightMode::CROSSBOW) {
    auto at = centerPosition();
    sfxWeapon = ::Sound(owner,::Sound::T_Regular,"DRAWSOUND_BOW",at,2500,false);
    sfxWeapon.play();
    }
  if(!isMmoServerReplica())
    dropTorch();
  visual.stopDlgAnim(*this);
  if(isMmoServerReplica()) {
    visual.updateWeaponSkeletonPresentation(
        mmoPresentationMeleeTwoHanded,mmoPresentationRangedCrossbow);
  } else {
    updateWeaponSkeleton();
  }
  }

bool Npc::implAiFlee(uint64_t dt) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementPlanning))
    return false;
  if(currentTarget==nullptr)
    return true;

  if(isFalling())
    return true;

  auto& oth = *currentTarget;

  const WayPoint* wp      = nullptr;
  const float     maxDist = 5*100; // 5 meters

  owner.findWayPoint(position(),[&](const WayPoint& p) {
    if(p.useCounter()>0 || qDistTo(&p)>maxDist*maxDist)
      return false;
    if(p.underWater)
      return false;
    if(!canRayHitPoint(p.position() + Vec3(0,10,0),true))
      return false;
    if(wp==nullptr || oth.qDistTo(&p)>oth.qDistTo(wp))
      wp = &p;
    return false;
    });

  if(go2.flag!=GT_Flee && go2.flag!=GT_No) {
    clearGoTo();
    }

  auto anim = (go2.flag!=GT_No)?AnimationSolver::TurnType::None:AnimationSolver::TurnType::Std;
  if(wp==nullptr || oth.qDistTo(wp)<oth.qDistTo(*this)) {
    auto  dx  = oth.x-x;
    auto  dz  = oth.z-z;
    if(implTurnTo(-dx,-dz,anim,dt))
      return (go2.flag==GT_Flee);
    } else {
    auto  dx  = wp->pos.x-x;
    auto  dz  = wp->pos.z-z;
    if(implTurnTo(dx,dz,anim,dt))
      return (go2.flag==GT_Flee);
    }

  go2.setFlee();
  setAnim(Anim::Move);
  return true;
  }

bool Npc::setGoToLadder() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementPlanning))
    return false;
  if(go2.wp==nullptr || go2.wp!=wayPath.first())
    return false;
  auto inter = go2.wp->ladder;
  if(inter==nullptr)
    return false;
  auto pos   = inter->nearestPoint(*this);
  if(MoveAlgo::isClose(*this,pos,MAX_AI_USE_DISTANCE)) {
    if(!inter->isAvailable())
      setAnim(AnimationSolver::Idle);
    else if(setInteraction(inter))
      wayPath.pop();
    return true;
    }
  return false;
  }
