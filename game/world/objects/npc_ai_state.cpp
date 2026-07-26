#include "npc.h"
#include "npc_transform_back.h"

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

bool Npc::prepareTurn() {
  const auto st = bodyStateMasked();
  if(interactive()==nullptr && (st==BS_WALK || st==BS_SNEAK)) {
    visual.stopWalkAnim(*this);
    setAnimRotate(0);
    return false;
    }
  if(interactive()==nullptr) {
    visual.stopWalkAnim(*this);
    visual.stopDlgAnim(*this);
    }
  return true;
  }

void Npc::nextAiAction(AiQueue& queue, uint64_t dt) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::AiQueue))
    return;
  if(isInAir())
    return;
  if(queue.size()==0)
    return;
  auto act = queue.pop();
  switch(act.act) {
    case AI_None: break;
    case AI_LookAtNpc:{
      currentLookAt=nullptr;
      currentLookAtNpc=act.target;
      break;
      }
    case AI_LookAt:{
      currentLookAtNpc=nullptr;
      currentLookAt=act.point;
      break;
      }
    case AI_TurnAway: {
      if(!prepareTurn()) {
        queue.pushFront(std::move(act));
        break;
        }
      if(act.target!=nullptr && implTurnAway(*act.target,dt)) {
        queue.pushFront(std::move(act));
        break;
        }
      break;
      }
    case AI_TurnToNpc: {
      if(!prepareTurn()) {
        queue.pushFront(std::move(act));
        break;
        }
      if(act.target!=nullptr && implTurnTo(*act.target,dt)) {
        queue.pushFront(std::move(act));
        break;
        }
      // Not looking quite correct in dialogs, when npc turns around
      // Example: Esteban dialog
      // currentLookAt    = nullptr;
      // currentLookAtNpc = nullptr;
      break;
      }
    case AI_WhirlToNpc: {
      if(!prepareTurn()) {
        queue.pushFront(std::move(act));
        break;
        }
      if(act.target!=nullptr && implWhirlTo(*act.target,dt)) {
        queue.pushFront(std::move(act));
        break;
        }
      break;
      }
    case AI_GoToNpc:
      if(!setInteraction(nullptr)) {
        queue.pushFront(std::move(act));
        break;
        }
      attachToPoint(nullptr);
      go2.set(act.target);
      wayPath.clear();
      break;
    case AI_GoToNextFp: {
      if(!setInteraction(nullptr)) {
        queue.pushFront(std::move(act));
        break;
        }
      auto fp = owner.findNextFreePoint(*this,act.s0);
      if(fp!=nullptr) {
        attachToPoint(fp);
        go2.set(fp,GoToHint::GT_NextFp);
        wayPath.clear();
        }
      break;
      }
    case AI_GoToPoint: {
      if(isInAir() || !setInteraction(nullptr)) {
        queue.pushFront(std::move(act));
        break;
        }
      if(wayPath.last()!=act.point) {
        wayPath     = owner.wayTo(*this,*act.point);
        auto wpoint = wayPath.pop();

        if(wpoint!=nullptr) {
          go2.set(wpoint);
          attachToPoint(wpoint);
          } else {
          attachToPoint(act.point);
          clearGoTo();
          }
        }
      break;
      }
    case AI_StopLookAt:
      currentLookAtNpc=nullptr;
      currentLookAt=nullptr;
      visual.setHeadRotation(0,0);
      break;
    case AI_RemoveWeapon:
      if(!isDead()) {
        if(closeWeapon(false)) {
          stopWalkAnimation();
          }
        auto ws = weaponState();
        if(ws!=WeaponState::NoWeapon){
          queue.pushFront(std::move(act));
          }
        }
      break;
    case AI_StartState:
      // NOTE: a new state can be stater within a daly routiine, such as TA_Sleep, with: ZS_GotoBed -> ZS_Sleep.
      // In such cases it's important to preserve aiState.eTime.
      if(startState(act.func,act.s0,aiState.eTime,act.i0==0)) {
        setOther(act.target);
        setVictim(act.victim);
        }
      break;
    case AI_PlayAnim:{
      owner.script().eventPlayAni(*this, act.s0);
      if(auto sq = playAnimByName(act.s0,BS_NONE)) {
        implAniWait(uint64_t(sq->totalTime()));
        implFaiWait(uint64_t(sq->totalTime()));
        } else {
        if(visual.hasAnim(act.s0))
          queue.pushFront(std::move(act));
        }
      break;
      }
    case AI_PlayAnimBs:{
      BodyState bs = BodyState(act.i0);
      if(auto sq = playAnimByName(act.s0,bs)) {
        implAniWait(uint64_t(sq->totalTime()));
        implFaiWait(uint64_t(sq->totalTime()));
        } else {
        if(visual.hasAnim(act.s0)) {
          queue.pushFront(std::move(act));
          } else {
          /* ZS_MM_Rtn_Sleep will set NPC_WALK mode and run T_STAND_2_SLEEP animation.
           * The problem is: T_STAND_2_SLEEP may not exists, in that case only NPC_WALK should be applied,
           * we will do so by playing Idle anim.
           */
          setAnim(Anim::Idle);
          }
        }
      break;
      }
    case AI_Wait:
      implAiWait(uint64_t(act.i0));
      break;
    case AI_StandUp:
    case AI_StandUpQuick: {
      const auto bs = bodyStateMasked();
      // NOTE: B_ASSESSTALK calls AI_StandUp, to make npc stand, if it's not on a chair or something
      if(interactive()!=nullptr) {
        if((interactive()->isLadder() && !isPlayer()) || !setInteraction(nullptr,false)) {
          queue.pushFront(std::move(act));
          }
        break;
        }
      else if(bs==BS_UNCONSCIOUS || bs==BS_LIE) {
        if(!setAnim(Anim::Idle))
          queue.pushFront(std::move(act)); else
          implAniWait(visual.pose().animationTotalTime());
        }
      else if(bs!=BS_DEAD) {
        visual.stopAnim(*this,"");
        setStateItem(MeshObjects::Mesh(),"");
        setAnim(Anim::Idle);
        }
      break;
      }
    case AI_EquipArmor:
      invent.equipArmor(act.i0,*this);
      break;
    case AI_EquipBestArmor:
      invent.equipBestArmor(*this);
      break;
    case AI_EquipMelee:
      invent.equipBestMeleeWeapon(*this);
      break;
    case AI_EquipRange:
      invent.equipBestRangedWeapon(*this);
      break;
    case AI_UseMob: {
      if(act.i0<0) {
        if(!setInteraction(nullptr))
          queue.pushFront(std::move(act));
        break;
        }
      /*
       * Rhademes doesn't quit talk properly
      if(owner.script().isTalk(*this)) {
        queue.pushFront(std::move(act));
        break;
        }*/

      auto inter = owner.availableMob(*this,act.s0);
      if(inter==nullptr) {
        /* in L`Hiver, version 1.3 there is a typo: "COOL" instead of "BSCOOL"
         * maybe 'scheme' need to be checked loosely, or maybe ignored.
         *
         * For now, if no mob found - discard command, to avoid npc soft-lock.
         */
        // queue.pushFront(std::move(act));
        break;
        }

      if(currentInteract!=nullptr && inter!=currentInteract) {
        setInteraction(nullptr);
        queue.pushFront(std::move(act));
        break;
        }

      if(inter!=nullptr) {
        auto pos = inter->nearestPoint(*this);
        if(currentInteract==nullptr && !MoveAlgo::isClose(*this, pos, MAX_AI_USE_DISTANCE)) { // too far
          go2.set(pos);
          // go to MOBSI and then complete AI_UseMob
          queue.pushFront(std::move(act));
          return;
          }
        if(!setInteraction(inter)) {
          // queue.pushFront(std::move(act));
          }
        }

      if(currentInteract==nullptr || currentInteract->stateId()!=act.i0) {
        queue.pushFront(std::move(act));
        return;
        }

      clearGoTo();
      break;
      }
    case AI_UseItem: {
      if(!isStanding()) {
        setAnim(Npc::Anim::Idle);
        queue.pushFront(std::move(act));
        break;
        }
      if(act.i0!=0)
        useItem(uint32_t(act.i0));
      break;
      }
    case AI_UseItemToState:
      if(act.i0!=0) {
        uint32_t itm   = uint32_t(act.i0);
        int      state = act.i1;
        if(state>0)
          visual.stopDlgAnim(*this);
        if(!invent.putState(*this,state>=0 ? itm : 0,state))
          queue.pushFront(std::move(act));
        }
      break;
    case AI_Teleport: {
      setPosition (act.point->position() );
      setDirection(act.point->direction());
      if(isPlayer()) {
        updateTransform();
        Gothic::inst().camera()->reset(this);
        }
      }
      break;
    case AI_DrawWeapon:
      if(canSwitchWeapon()) {
        if(!drawWeaponMelee() &&
           !drawWeaponBow())
          queue.pushFront(std::move(act));
        }
      break;
    case AI_DrawWeaponMelee:
      if(canSwitchWeapon()) {
        if(!drawWeaponMelee())
          queue.pushFront(std::move(act));
        }
      break;
    case AI_DrawWeaponRange:
      if(canSwitchWeapon()) {
        if(!drawWeaponBow())
          queue.pushFront(std::move(act));
        }
      break;
    case AI_DrawSpell: {
      if(canSwitchWeapon()) {
        const int32_t spell = act.i0;
        if(drawSpell(spell))
          aiExpectedInvest = act.i1; else
          queue.pushFront(std::move(act));
        }
      break;
      }
    case AI_Attack:
      if(currentTarget!=nullptr) {
        if(!fghAlgo.fetchInstructions(*this,*currentTarget,owner.script()))
          queue.pushFront(std::move(act));
        }
      break;
    case AI_Flee:
      if(!implAiFlee(dt))
        queue.pushFront(std::move(act));
      break;
    case AI_Dodge:
      if(auto sq = setAnimAngGet(Anim::MoveBack)) {
        visual.setAnimRotate(*this,0);
        implAniWait(uint64_t(sq->totalTime()));
        } else {
        queue.pushFront(std::move(act));
        }
      break;
    case AI_UnEquipWeapons:
      invent.unequipWeapons(owner.script(),*this);
      break;
    case AI_UnEquipArmor:
      invent.unequipArmor(owner.script(),*this);
      break;
    case AI_Output:
    case AI_OutputSvm:
    case AI_OutputSvmOverlay:{
      if(performOutput(act)) {
        if(aiPolicy!=NpcProcessPolicy::AiFar2) {
          uint64_t msgTime = 0;
          if(act.act==AI_Output) {
            msgTime = owner.script().messageTime(act.s0);
            } else {
            auto svm  = owner.script().messageFromSvm(act.s0,hnpc->voice);
            msgTime   = owner.script().messageTime(svm);
            }
          visual.startFaceAnim(*this,"VISEME",1,msgTime);
          }
        if(act.act!=AI_OutputSvmOverlay) {
          visual.startAnimDialog(*this);
          visual.setAnimRotate(*this,0);
          }
        } else {
        queue.pushFront(std::move(act));
        }
      break;
      }
    case AI_ProcessInfo: {
      const int PERC_DIST_DIALOG = 2000;

      if(act.target==nullptr)
        break;

      if(owner.isInDialog()) {
        queue.pushFront(std::move(act));
        break;
        }

      if(this!=act.target && act.target->isPlayer() && act.target->currentInteract!=nullptr) {
        //queue.pushFront(std::move(act));
        break;
        }

      if(act.target->qDistTo(*this)>PERC_DIST_DIALOG*PERC_DIST_DIALOG) {
        break;
        }

      if(act.target->interactive()==nullptr && !act.target->isAiBusy())
        act.target->stopWalkAnimation();
      if(interactive()==nullptr && !isAiBusy())
        stopWalkAnimation();

      if(auto p = owner.script().openDlgOuput(*this,*act.target)) {
        outputPipe = p;
        setOther(act.target);
        act.target->setOther(this);
        act.target->outputPipe = p;
        } else {
        queue.pushFront(std::move(act));
        }
      }
      break;
    case AI_StopProcessInfo:
      if(outputPipe->close()) {
        outputPipe = owner.script().openAiOuput();
        if(currentOther!=nullptr)
          currentOther->outputPipe = owner.script().openAiOuput();
        } else {
        queue.pushFront(std::move(act));
        }
      break;
    case AI_ContinueRoutine:
      resumeAiRoutine();
      break;
    case AI_AlignToWp:
    case AI_AlignToFp:{
      if(auto fp = currentFp){
        if(fp->dir.x!=0.f || fp->dir.z!=0.f){
          if(implTurnTo(fp->dir.x,fp->dir.z,AnimationSolver::TurnType::Std,dt))
            queue.pushFront(std::move(act));
          }
        }
      break;
      }
    case AI_SetNpcsToState:{
      const int32_t r = act.i0*act.i0;
      owner.detectNpc(position(),float(hnpc->senses_range),[&act,this,r](Npc& other) {
        if(&other==this)
          return;
        if(other.isDead())
          return;
        if(qDistTo(other)>float(r))
          return;
        other.aiPush(AiQueue::aiStartState(act.func,1,other.currentOther,other.currentVictim,other.hnpc->wp));
        });
      break;
      }
    case AI_SetWalkMode:{
      setWalkMode(WalkBit(act.i0));
      break;
      }
    case AI_FinishingMove:{
      if(act.target==nullptr || !act.target->isUnconscious())
        break;

      if(!fghAlgo.isInFinishRange(*this,*act.target,owner.script())){
        queue.pushFront(std::move(act));
        go2.set(act.target);
        setAnim(Npc::Anim::Move);
        implGoTo(dt,fghAlgo.attackFinishDistance(owner.script()));
        }
      else if(!isStanding()) {
        clearGoTo();
        queue.pushFront(std::move(act));
        }
      else if(!implTurnTo(*act.target,dt)) {
        queue.pushFront(std::move(act));
        }
      else if(canFinish(*act.target)){
        setTarget(act.target);
        if(!finishingMove())
          queue.pushFront(std::move(act));
        }
      break;
      }
    case AI_TakeItem:{
      if(act.item==nullptr)
        break;
      if(takeItem(*act.item)==nullptr)
        queue.pushFront(std::move(act));
      break;
      }
    case AI_GotoItem:{
      go2.set(act.item);
      break;
      }
    case AI_PointAt:{
      if(act.point==nullptr)
        break;
      if(!implPointAt(act.point->position()))
        queue.pushFront(std::move(act));
      break;
      }
    case AI_PointAtNpc:{
      if(act.target==nullptr)
        break;
      if(!implPointAt(act.target->position()))
        queue.pushFront(std::move(act));
      break;
      }
    case AI_StopPointAt:{
      visual.stopAnim(*this,"T_POINT");
      break;
      }
    case AI_PrintScreen:{
      auto  msg     = act.s0;
      auto  posx    = act.i0;
      auto  posy    = act.i1;
      int   timesec = act.i2;
      auto  font    = act.s1;

      bool complete = false;
      if(aiOutputBarrier<=owner.tickCount()) {
        if(outputPipe->printScr(*this,timesec,msg,posx,posy,font))
          complete = true;
        }

      if(!complete)
        queue.pushFront(std::move(act));
      break;
      }
    }
  }

bool Npc::startState(ScriptFn id, std::string_view wp) {
  return startState(id,wp,gtime::endOfTime(),false);
  }

bool Npc::startState(ScriptFn id, std::string_view wp, gtime endTime, bool noFinalize) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Routine))
    return false;
  if(!id.isValid())
    return false;

  if(aiState.funcIni==id) {
    if(!noFinalize) {
      // NOTE: B_AssessQuietSound can cause soft-lock on npc without this
      aiState.started = false;
      }
    if(!wp.empty())
      hnpc->wp = wp;
    return true;
    }

  clearAiQueue();
  clearState(noFinalize);
  if(!wp.empty())
    hnpc->wp = wp;

  {
    // ZS_GotoBed -> ZS_Sleep relie on clean state
    for(size_t i=0; i<PERC_Count; ++i)
      setPerceptionDisable(PercType(i));
  }

  if(wp=="TOT" && aiPolicy!=NpcProcessPolicy::Player && aiPolicy!=NpcProcessPolicy::AiNormal) {
    // workaround for Pedro removal script
    auto& point = owner.deadPoint();
    attachToPoint(nullptr);
    setPosition(point.position());
    }

  auto& st = owner.script().aiState(id);
  if(isPlayer() && !isPlayerEnabledState(st)) {
    // disable for now, as it causes infinite lock in freeze state
    // https://github.com/Try/OpenGothic/issues/906
    // extra 'aiStandup' to avoid issue with B_StopMagicFreeze
    aiPush(AiQueue::aiStandup());
    return false;
    }

  aiState.started      = false;
  aiState.funcIni      = st.funcIni;
  aiState.funcLoop     = st.funcLoop;
  aiState.funcEnd      = st.funcEnd;
  aiState.sTime        = owner.tickCount();
  aiState.eTime        = endTime;
  aiState.loopNextTime = owner.tickCount();
  aiState.hint         = st.name();
  return true;
  }

void Npc::clearState(bool noFinalize) {
  if(aiState.funcIni.isValid() && aiState.started) {
    if(!noFinalize)
      owner.script().invokeState(this,currentOther,currentVictim,aiState.funcEnd);  // cleanup
    aiPrevState = aiState.funcIni;
    invent.putState(*this,0,0);
    visual.stopItemStateAnim(*this);
    }
  aiState = AiState();
  }

bool Npc::isPlayerEnabledState(const ::AiState& st) const {
  // allowed player states are hard-coded
  // https://forum.worldofplayers.de/forum/threads/1533803-G1-AI_StartState-hardcoded-ZS-states-for-Player?p=26034737&viewfull=1#post26034737

  static const std::array playerEnabledStatesG1 = {
    "ZS_DEAD",   "ZS_UNCONSCIOUS", "ZS_MAGICFREEZE",
    "ZS_PYRO",   "ZS_ASSESSMAGIC", "ZS_ASSESSSTOPMAGIC",
    "ZS_ZAPPED", "ZS_SHORTZAPPED", "ZS_MAGICSLEEP",
    "ZS_MAGICFEAR"
    };
  static const std::array playerEnabledStatesG2 = {
    "ZS_DEAD",   "ZS_UNCONSCIOUS", "ZS_MAGICFREEZE",
    "ZS_PYRO",   "ZS_ASSESSMAGIC", "ZS_ASSESSSTOPMAGIC",
    "ZS_ZAPPED", "ZS_SHORTZAPPED", "ZS_MAGICSLEEP",
    "ZS_WHIRLWIND"
    };

  const auto* sym = owner.script().findSymbol(st.funcIni);
  if(sym==nullptr)
    return false;

  const auto& playerEnabledStates = (owner.version().game==2 ? playerEnabledStatesG2 : playerEnabledStatesG1);
  for(auto* pState:playerEnabledStates)
    if(sym->name()==pState)
      return true;
  return false;
  }

void Npc::tickRoutine() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Routine))
    return;
  if(!aiState.funcIni.isValid() && !isPlayer()) {
    auto r = currentRoutine();
    if(r.callback.isValid()) {
      auto t = endTime(r);
      startState(r.callback, r.wayPointName(), t, false);
      }
    else if(hnpc->start_aistate!=0) {
      auto endTime = owner.time();
      endTime.addMilis(uint64_t(gtime(4, 0).toInt()));
      startState(uint32_t(hnpc->start_aistate), "", endTime, false);
      }
    }

  if(!aiState.funcIni.isValid())
    return;

  auto& sc = owner.script();
  if(!aiState.started) {
    aiState.started      = true;
    aiState.loopNextTime = owner.tickCount();
    // WA: for gothic1 dialogs
    perceptionNextTime   = owner.tickCount();
    sc.invokeState(this,currentOther,currentVictim,aiState.funcIni);
    return;
    }

  const bool fastPath = (aiPolicy==NpcProcessPolicy::AiFar2 && routines.empty()); //HACK: don't process far away Npc
  if(aiState.loopNextTime<=owner.tickCount()) {
    aiState.loopNextTime = owner.tickCount() + perceptionTimeClampt();
    int loop = LOOP_CONTINUE;
    if(aiState.funcLoop.isValid()) {
      static const float MAX_DIST = 300;
      if(fastPath && currentFp!=nullptr && qDistTo(currentFp) < MAX_DIST*MAX_DIST) {
        loop = LOOP_CONTINUE;
        }
      else if(fastPath && currentFp!=nullptr) {
        // for debugging
        loop = sc.invokeState(this,currentOther,currentVictim,aiState.funcLoop);
        }
      else {
        loop = sc.invokeState(this,currentOther,currentVictim,aiState.funcLoop);
        }
      } else {
      // ZS_DEATH   have no loop-function, in G1, G2-classic
      // ZS_GETMEAT have no loop-function, in G2-notr
      loop = owner.version().hasZSStateLoop() ? 1 : 0;
      }

    if(aiState.eTime<=owner.time()) {
      // Avoid interruption of ZS_TALK/ZS_ATTACK
      if(currentTarget==nullptr && outputPipe->isFinished())
        loop = LOOP_END;
      }

    if(loop!=LOOP_CONTINUE) {
      clearState(false);
      currentOther  = nullptr;
      currentVictim = nullptr;
      }
    }
  }

void Npc::setTarget(Npc *t) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::TargetSelection))
    return;
  if(currentTarget==t)
    return;

  currentTarget = t;
  if(!go2.empty() && !isPlayer())
    clearGoTo();
  }

Npc *Npc::target() const {
  return currentTarget;
  }

void Npc::clearNearestEnemy() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::TargetSelection))
    return;
  nearestEnemy = nullptr;
  }

void Npc::setOther(Npc *ot) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::TargetSelection))
    return;
  if(isTalk() && ot && !ot->isPlayer())
    Log::e("unxepected perc acton");
  currentOther = ot;
  }

void Npc::setVictim(Npc* ot) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::TargetSelection))
    return;
  currentVictim = ot;
  }

bool Npc::haveOutput() const {
  if(owner.tickCount()<aiOutputBarrier)
    return true;
  return aiOutputOrderId()!=std::numeric_limits<int>::max();
  }

void Npc::setAiOutputBarrier(uint64_t dt, bool overlay) {
  aiOutputBarrier = owner.tickCount()+dt;
  if(!overlay)
    outWaitTime = aiOutputBarrier;
  }

void Npc::emitSoundEffect(std::string_view sound, float range, bool freeSlot) {
  auto sfx = ::Sound(owner,::Sound::T_Regular,sound,centerPosition(),range,freeSlot);
  sfx.play();
  }

void Npc::emitSoundGround(std::string_view sound, float range, bool freeSlot) {
  auto mat = mvAlgo.groundMaterial();
  string_frm buf(sound,"_",MaterialGroupNames[uint8_t(mat)]);
  auto sfx = ::Sound(owner,::Sound::T_Regular,buf,{x,y,z},range,freeSlot);
  sfx.play();
  }

void Npc::emitSoundSVM(std::string_view svm) {
  if(hnpc->voice==0)
    return;
  char frm [32]={};
  std::snprintf(frm,sizeof(frm),"%.*s",int(svm.size()),svm.data());

  char name[32]={};
  std::snprintf(name,sizeof(name),frm,int(hnpc->voice));
  emitSoundEffect(name,2500,true);
  }

void Npc::startEffect(Npc& to, const VisualFx& vfx) {
  Effect e(vfx,owner,*this,SpellFxKey::Cast);
  e.setActive(true);
  e.setTarget(&to);
  visual.startEffect(owner, std::move(e), 0, true);
  }

void Npc::stopEffect(const VisualFx& vfx) {
  visual.stopEffect(vfx);
  }

void Npc::runEffect(Effect&& e) {
  visual.startEffect(owner, std::move(e), 0, true);
  }

bool Npc::isTargetableBySpell(TargetType t) const {
  if(bool(t&(TARGET_TYPE_ALL|TARGET_TYPE_NPCS)))
    return true;

  const Guild gil = Guild(trueGuild());

  const bool g2 = owner.version().game==2;
  const auto SEPERATOR_ORC = g2 ? GIL_SEPERATOR_ORC : GIL_G1_SEPERATOR_ORC;
  const auto G1_UNDEAD = (gil == GIL_G1_ZOMBIE || gil == GIL_G1_UNDEADORC || gil == GIL_G1_SKELETON);
  const auto G2_UNDEAD = (gil == GIL_GOBBO_SKELETON ||
    gil == GIL_SUMMONED_GOBBO_SKELETON || gil == GIL_SKELETON      ||
    gil == GIL_SUMMONED_SKELETON       || gil == GIL_SKELETON_MAGE ||
    gil == GIL_SHADOWBEAST_SKELETON    || gil == GIL_ZOMBIE);

  if(bool(t&TARGET_TYPE_HUMANS) && isHuman())
    return true;
  if(bool(t&TARGET_TYPE_ORCS) && gil>SEPERATOR_ORC)
    return true;
  if(bool(t&TARGET_TYPE_UNDEAD) && g2 && G2_UNDEAD)
    return true;
  if(bool(t&TARGET_TYPE_UNDEAD) && !g2 && G1_UNDEAD)
    return true;

  return false;
  }

void Npc::commitSpell() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat))
    return;
  auto active = invent.getItem(currentSpellCast);
  if(active==nullptr || !active->isSpellOrRune())
    return;

  const int32_t splId = active->spellId();
  const auto&   spl   = owner.script().spellDesc(splId);

  if(owner.version().game==2)
    owner.script().invokeSpell(*this,currentTarget,*active);

  if(active->isSpellShoot()) {
    const int lvl = (castLevel-CS_Emit_0)+1;
    DamageCalculator::Damage dmg={};
    for(size_t i=0; i<zenkit::DamageType::NUM; ++i)
      if((spl.damage_type&(1<<i))!=0) {
        dmg[i] = spl.damage_per_level*lvl;
        }

    auto& b = owner.shootSpell(*active, *this, currentTarget);
    b.setDamage(dmg);
    b.setOrigin(this);
    b.setTarget(nullptr);
    visual.setMagicWeaponKey(owner,SpellFxKey::Init);
    } else {
    // NOTE: use pfx_ppsIsLoopingChg ?
    const VisualFx* vfx = owner.script().spellVfx(splId);
    if(vfx!=nullptr) {
      auto e = Effect(*vfx,owner,Vec3(x,y,z),SpellFxKey::Cast);
      e.setOrigin(this);
      e.setTarget((currentTarget==nullptr) ? this : currentTarget);
      e.setSpellId(splId,owner);
      e.setActive(true);
      visual.startEffect(owner,std::move(e),0,true);
      }
    visual.setMagicWeaponKey(owner,SpellFxKey::Init);
    if(currentTarget!=nullptr) {
      currentTarget->lastHitSpell = splId;
      currentTarget->perceptionProcess(*this,nullptr,0,PERC_ASSESSMAGIC);
      }
    }

  if(active->isSpell()) {
    size_t cnt = active->count();
    invent.delItem(active->clsId(),1,*this);
    if(cnt<=1) {
      Item* spl = nullptr;
      for(uint8_t i=0;i<8;++i) {
        if(auto s = invent.currentSpell(i)) {
          spl = s;
          break;
          }
        }
      if(spl==nullptr) {
        if(spellInfo==0)
          aiPush(AiQueue::aiRemoveWeapon());
        } else {
        drawSpell(spl->spellId());
        }
      }
    }

  if(spellInfo!=0 && transformSpl==nullptr) {
    transformSpl.reset(new TransformBack(*this));
    invent.updateView(*this);
    visual.clearOverlays();

    owner.script().initializeInstanceNpc(hnpc, size_t(spellInfo));
    spellInfo  = 0;
    hnpc->level = transformSpl->hnpc->level;
    }
  }

const Npc::Routine& Npc::currentRoutine(bool assertWp) const {
  // find routine for current time
  // if there is no such routine search counter clock-wise until one is found
  auto time = owner.time().timeInDay();
  for(auto& i:routines) {
    if(assertWp && i.point==nullptr)
      continue;
    if(i.end<i.start && (time<i.end || i.start<=time))
      return i;
    if(i.start<=time && time<i.end)
      return i;
    }

  const auto     day   = gtime(24,0).toInt();
  const Routine* rtn   = nullptr;
  int64_t        delta = std::numeric_limits<int64_t>::max();
  for(auto& i:routines) {
    if(assertWp && i.point==nullptr)
      continue;
    int64_t d = time.toInt() - i.end.toInt();
    if(d<0)
      d += day;
    // take the last one if multiple with same end time exist
    if(d<=delta) {
      rtn   = &i;
      delta = d;
      }
    }

  if(rtn!=nullptr)
    return *rtn;
  static Routine r;
  return r;
  }

const WayPoint* Npc::currentTaPoint() const {
  if(routines.empty())
    return owner.findPoint(hnpc->wp,false);
  return currentRoutine(true).point;
  }

auto Npc::routineSnapshot() const -> std::vector<RoutineSnapshot> {
  std::vector<RoutineSnapshot> ret;
  ret.reserve(routines.size());
  const Routine* active = &currentRoutine(true);
  for(auto& r:routines) {
    RoutineSnapshot row;
    row.start    = r.start;
    row.end      = r.end;
    row.callback = r.callback;
    row.point    = r.point;
    row.waypoint = r.wayPointName();
    row.active   = active==&r;
    ret.push_back(row);
    }
  return ret;
  }

gtime Npc::endTime(const Npc::Routine &r) const {
  auto wtime = owner.time();
  auto time  = wtime.timeInDay();

  //NOTE: should we consider time extension for invalid routine sequences?
  if(r.end<r.start) {
    if(time<r.end)
      return gtime(wtime.day(),r.end.hour(),r.end.minute());
    return gtime(wtime.day()+1,r.end.hour(),r.end.minute());
    }
  if(r.start<r.end) {
    if(r.end.hour()==0 || r.end<time)
      return gtime(wtime.day()+1,r.end.hour(),r.end.minute()); else
      return gtime(wtime.day(),r.end.hour(),r.end.minute());
    }
  if(r.start==r.end && r.end.toInt()==0) {
    // for example Rtn_Start_1081 in NTR is filled with zeros
    return gtime(wtime.day()+1,r.end.hour(),r.end.minute());
    }
  // error - routine is not active now
  return wtime;
  }

BodyState Npc::bodyState() const {
  if(isDead())
    return BS_DEAD;
  if(isUnconscious())
    return BS_UNCONSCIOUS;
  if(isFalling())
    return BS_FALL;

  uint32_t s = visual.pose().bodyState();
  if(auto i = interactive())
    s = i->stateMask();
  return BodyState(s);
  }

BodyState Npc::bodyStateMasked() const {
  BodyState bs = bodyState();
  return BodyState(bs & (BS_MAX | BS_FLAG_MASK));
  }

bool Npc::hasState(BodyState s) const {
  if(visual.pose().hasState(s))
    return true;
  if(auto i = interactive())
    return s==i->stateMask();
  return false;
  }

bool Npc::hasStateFlag(BodyState flg) const {
  if(visual.pose().hasStateFlag(flg))
    return true;
  if(auto i = interactive())
    return flg==(i->stateMask() & (BS_FLAG_MASK|BS_MOD_MASK));
  return false;
  }

void Npc::setToFightMode(const size_t item) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat))
    return;
  if(invent.itemCount(item)==0)
    addItem(item,1);

  invent.equip(item,*this,true);
  invent.switchActiveWeapon(*this,1);

  auto w = invent.currentMeleeWeapon();
  if(w==nullptr || w->clsId()!=item)
    return;

  const auto previousWeaponState = weaponState();
  auto weaponSt = WeaponState::W1H;
  if(w->is2H()) {
    weaponSt = WeaponState::W2H;
    } else {
    weaponSt = WeaponState::W1H;
    }

  if(visual.setToFightMode(weaponSt))
    updateWeaponSkeleton();

  auto& weapon = *currentMeleeWeapon();
  auto  st     = weapon.is2H() ? WeaponState::W2H : WeaponState::W1H;
  hnpc->weapon  = (st==WeaponState::W1H ? 3:4);
  Mmo::Hooks::onWeaponStateChanged(*this, previousWeaponState, st,
                                   "game/world/objects/npc.cpp:Npc::setToFightMode",
                                   "weapon_ready_script_set_fight_mode");
  }

void Npc::setToFistMode() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Combat))
    return;
  auto weaponSt=weaponState();
  if(weaponSt==WeaponState::Fist)
    return;
  invent.switchActiveWeaponFist();
  if(visual.setToFightMode(WeaponState::Fist))
    updateWeaponSkeleton();
  hnpc->weapon  = 1;
  Mmo::Hooks::onWeaponStateChanged(*this, weaponSt, WeaponState::Fist,
                                   "game/world/objects/npc.cpp:Npc::setToFistMode",
                                   "weapon_ready_script_set_fist_mode");
  }

std::vector<GameScript::DlgChoice> Npc::dialogChoices(Npc& player,const std::vector<uint32_t> &except,bool includeImp) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Dialog))
    return {};
  auto ret = owner.script().dialogChoices(player.hnpc,this->hnpc,except,includeImp);
  owner.gameSession().recordDialogChoices(player, *this, ret, includeImp ? "prestart" : "choices", includeImp);
  return ret;
  }

bool Npc::isAiQueueEmpty() const {
  return aiQueue.size()==0 &&
         go2.empty() &&
         waitTime<owner.tickCount();
  }

bool Npc::isAiBusy() const {
  return !isAiQueueEmpty() ||
         aniWaitTime>=owner.tickCount() ||
         outWaitTime>=owner.tickCount();
  }

void Npc::clearAiQueue() {
  currentLookAt    = nullptr;
  currentLookAtNpc = nullptr;
  visual.setHeadRotation(0,0);

  aiQueue.clear();
  aiQueueOverlay.clear();
  aniWaitTime = 0;
  waitTime    = 0;
  faiWaitTime = 0;
  fghAlgo.onClearTarget();
  wayPath.clear();
  clearGoTo();
  }
