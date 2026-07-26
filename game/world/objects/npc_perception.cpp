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

bool Npc::isEnemy(const Npc &other) const {
  return owner.script().personAttitude(*this,other)==ATT_HOSTILE;
  }

bool Npc::isDead() const {
  if(isMmoServerReplica())
    return mmoPresentationLifeState == MmoPresentationLifeState::Dead;
  return owner.script().isDead(*this);
  }

bool Npc::isLie() const {
  return bodyStateMasked()==BS_LIE;
  }

bool Npc::isUnconscious() const {
  if(isMmoServerReplica())
    return mmoPresentationLifeState == MmoPresentationLifeState::Unconscious;
  return owner.script().isUnconscious(*this);
  }

bool Npc::isDown() const {
  return isUnconscious() || isDead();
  }

bool Npc::isAttack() const {
  return owner.script().isAttack(*this);
  }

bool Npc::isTalk() const {
  return owner.script().isTalk(*this);
  }

bool Npc::isAttackAnim() const {
  return visual.pose().isAttackAnim();
  }

bool Npc::isPrehit() const {
  return visual.pose().isPrehit(owner.tickCount());
  }

bool Npc::isImmortal() const {
  return hnpc->flags & zenkit::NpcFlag::IMMORTAL;
  }

void Npc::setPerceptionTime(uint64_t time) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Perception))
    return;
  perceptionTime = time;
  }

uint64_t Npc::perceptionTimeClampt() const {
  return std::max<uint64_t>(perceptionTime, 1);
  }

void Npc::setPerceptionEnable(PercType t, size_t fn) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Perception))
    return;
  if(t>0 && t<PERC_Count)
    perception[t].func = fn;
  }

void Npc::setPerceptionDisable(PercType t) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Perception))
    return;
  if(t>0 && t<PERC_Count)
    perception[t].func = ScriptFn();
  }

void Npc::startDialog(Npc& pl) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Dialog))
    return;
  if(pl.isDown() || pl.isInAir() || isPlayer())
    return;
  if(perceptionProcess(pl,nullptr,0,PERC_ASSESSTALK))
    setOther(&pl);
  }

bool Npc::perceptionProcess(Npc &pl) {
  if(!isPlayer() && mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Perception))
    return false;

  static bool dbg = false;
  static int  kId = -1;
  if(dbg && hnpc->id!=kId)
    return false;

  if(isPlayer())
    return true;

  bool ret=false;
  if(processPolicy()!=NpcProcessPolicy::AiNormal) {
    perceptionNextTime = owner.tickCount()+perceptionTimeClampt();
    return ret;
    }

  const float quadDist = pl.qDistTo(*this);
  if(hasPerc(PERC_ASSESSPLAYER) && canSenseNpc(pl,false)!=SensesBit::SENSE_NONE) {
    if(perceptionProcess(pl,nullptr,quadDist,PERC_ASSESSPLAYER)) {
      ret = true;
      }
    }

  Npc* enem=hasPerc(PERC_ASSESSENEMY) ? updateNearestEnemy() : nullptr;
  if(enem!=nullptr){
    float dist=qDistTo(*enem);
    if(perceptionProcess(*enem,nullptr,dist,PERC_ASSESSENEMY)){
      ret          = true;
      } else {
      nearestEnemy = nullptr;
      }
    }

  Npc* body=hasPerc(PERC_ASSESSBODY) ? updateNearestBody() : nullptr;
  if(body!=nullptr){
    float dist=qDistTo(*body);
    if(perceptionProcess(*body,nullptr,dist,PERC_ASSESSBODY)) {
      ret = true;
      }
    }

  // if(aiQueue.size()==0) // NOTE: Gothic1 fights
  perceptionNextTime = owner.tickCount()+perceptionTimeClampt();
  return ret;
  }

bool Npc::perceptionProcess(Npc &pl, Npc* victim, float quadDist, PercType perc) {
  if(!isPlayer() && mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Perception))
    return false;

  if(!aiState.started && aiState.funcIni.isValid()) {
    // avoid ugly soft-lock (ZS_MM_Attack <-> B_MM_AssessWarn) for the orks near ramp
    return false;
    }

  float r = float(world().script().percRanges().at(perc, hnpc->senses_range));
  r = r*r;

  if(quadDist>r)
    return false;

  if(hasPerc(perc)) {
    owner.script().invokeState(this,&pl,victim,perception[perc].func);
    return true;
    }
  if(perc==PERC_ASSESSMAGIC && isPlayer()) {
    auto defaultFn = owner.script().playerPercAssessMagic();
    if(defaultFn.isValid())
      owner.script().invokeState(this,&pl,victim,defaultFn);
    return true;
    }
  return false;
  }

bool Npc::hasPerc(PercType perc) const {
  return perception[perc].func.isValid();
  }

uint64_t Npc::percNextTime() const {
  return perceptionNextTime;
  }

bool Npc::setInteraction(Interactive *id, bool quick) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Interaction))
    return false;
  if(currentInteract==id)
    return true;

  if(currentInteract!=nullptr) {
    return currentInteract->detach(*this,quick);
    }

  if(id==nullptr)
    return (currentInteract==nullptr);

  if(id->attach(*this)) {
    currentInteract = id;
    attachToPoint(nullptr); //NOTE: Fajeth campfire
    if(!quick) {
      visual.stopAnim(*this,"");
      setAnimRotate(0);
      }
    return true;
    }

  return false;
  }

void Npc::quitInteraction() {
  if(currentInteract==nullptr)
    return;
  if(invTorch)
    processDefInvTorch();
  setDirectionY(0);
  currentInteract=nullptr;
  }

void Npc::processDefInvTorch() {
  if(invTorch || isUsingTorch()) {
    visual.setTorch(invTorch,owner);
    invTorch = !invTorch;
    }
  }

void Npc::setDetectedMob(Interactive* id) {
  moveMob         = id;
  moveMobCacheKey = position();
  }

Interactive* Npc::detectedMob() const {
  if(currentInteract!=nullptr)
    return currentInteract;
  if((moveMobCacheKey-position()).quadLength()<10.f*10.f)
    return moveMob;
  return nullptr;
  }

bool Npc::isInState(ScriptFn stateFn) const {
  return aiState.funcIni==stateFn;
  }

bool Npc::isInRoutine(ScriptFn stateFn) const {
  auto& rout = currentRoutine();
  return rout.callback==stateFn && aiState.funcIni==stateFn;
  }

bool Npc::wasInState(ScriptFn stateFn) const {
  return aiPrevState==stateFn;
  }

size_t Npc::currentAiStateFunction() const {
  return aiState.funcIni.ptr;
  }

std::string_view Npc::currentAiStateName() const {
  if(aiState.hint!=nullptr && aiState.hint[0]!='\0')
    return aiState.hint;
  return "";
  }

uint64_t Npc::stateTime() const {
  return owner.tickCount()-aiState.sTime;
  }

void Npc::setStateTime(int64_t time) {
  aiState.sTime = owner.tickCount()-uint64_t(time);
  }

void Npc::addRoutine(gtime s, gtime e, uint32_t callback, std::string_view point) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Routine))
    return;
  auto wp = world().findPoint(point,false);

  Routine r;
  r.start    = s;
  r.end      = e;
  r.callback = callback;
  r.point    = wp;
  if(wp==nullptr)
    r.fallbackName = point;
  routines.push_back(r);

  std::stable_sort(routines.begin(), routines.end(), [](const Routine& l, const Npc::Routine& r) {
    return l.start < r.start;
    });
  }

void Npc::excRoutine(size_t callback) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Routine))
    return;
  routines.clear();
  owner.script().invokeState(this,currentOther,currentVictim,callback);
  // aiState.eTime = gtime();
  }

bool Npc::canSeeNpc(const Npc &oth, bool freeLos) const {
  const auto mid = oth.physic.center();
  if(canRayHitPoint(mid,freeLos)) {
    // mid of dead npc may endedup inside a wall; extra check for physical center
    return true;
    }
  if(oth.visual.visualSkeleton()==nullptr)
    return false;
  if(oth.visual.visualSkeleton()->BIP01_HEAD==size_t(-1))
    return false;
  auto head = oth.visual.mapHeadBone();
  if(canRayHitPoint(head,freeLos))
    return true;
  return false;
  }

bool Npc::canSeeSource() const {
  const auto head = visual.mapHeadBone();
  const bool ret  = owner.sound()->canSeeSource(head);
  if(ret)
    return ret;
  if(currentLookAtNpc!=nullptr)
    return canSeeNpc(*currentLookAtNpc, false);
  return false;
  }

bool Npc::canRayHitPoint(const Tempest::Vec3 pos, bool freeLos, float extRange) const {
  float ang = freeLos ? 180.f : -1;
  return canRayHitPoint(pos, ang, extRange);
  }

bool Npc::canRayHitPoint(const Tempest::Vec3 pos, float angOverride, float extRange) const {
  const float range = float(hnpc->senses_range) + extRange;
  if(qDistTo(pos)>range*range)
    return false;
  // npc eyesight height by default
  return canRayHitPoint(visual.mapHeadBone(), pos, angOverride, extRange);
  }

bool Npc::canRayHitPoint(const Tempest::Vec3 self, const Tempest::Vec3 pos, float angOverride, float extRange) const {
  const float range = float(hnpc->senses_range) + extRange;
  if(qDistTo(pos)>range*range)
    return false;

  static const double ref = std::cos(100*M_PI/180.0); // spec requires +-100 view angle range
  const DynamicWorld* w   = owner.physic();
  bool freeLos = angOverride>=180.f;
  if(freeLos) {
    return !w->ray(self, pos).hasCol;
    }

  float dx  = self.x-pos.x, dz=self.z-pos.z;
  float dir = angleDir(dx,dz);
  float da  = float(M_PI)*(visual.viewDirection()-dir)/180.f;
  auto  ca  = angOverride > 0 ? std::cos(angOverride*M_PI/180.0) : ref;
  if(double(std::cos(da))<=ca) {
    if(!w->ray(self, pos).hasCol)
      return true;
    }
  return false;
  }

SensesBit Npc::canSenseNpc(const Npc &oth, bool freeLos, float extRange) const {
  // NOTE1: https://github.com/Try/OpenGothic/pull/589#issuecomment-2045897394
  // NOTE2: interacting with chest(lockpicking) or some MOBSI should not produce 'noise'
  // NOTE3: seem npc can't hear player in general case, and hearing relevant only for sendImmediatePerc cases
  const bool isNoisy = false;
  const auto mid     = oth.centerPosition();
  return canSenseNpc(mid,freeLos,isNoisy,extRange);
  }

SensesBit Npc::canSenseNpc(const Tempest::Vec3 pos, bool freeLos, bool isNoisy, float extRange) const {
  const float range = float(hnpc->senses_range)+extRange;
  if(qDistTo(pos)>range*range)
    return SensesBit::SENSE_NONE;

  SensesBit ret = SensesBit::SENSE_SMELL;

  if(isNoisy) {
    // no need to be in same room: https://github.com/Try/OpenGothic/issues/420
    ret = ret | SensesBit::SENSE_HEAR;
    }

  if((hnpc->senses & int32_t(SensesBit::SENSE_SEE))!=0 && canRayHitPoint(pos, freeLos, extRange)) {
    ret = ret | SensesBit::SENSE_SEE;
    }

  return ret & SensesBit(hnpc->senses);
  }

bool Npc::canSeeItem(const Item& it, bool freeLos) const {
  static const double ref = std::cos(100*M_PI/180.0); // spec requires +-100 view angle range

  const auto  itMid = it.midPosition();
  const auto  cen   = visual.mapHeadBone();
  const auto  dir   = itMid - cen;
  const float range = float(hnpc->senses_range);

  if(dir.quadLength()>range*range)
    return false;

  if(!freeLos) {
    float dx  = dir.x, dz = dir.z;
    float dir = angleDir(dx,dz);
    float da  = float(M_PI)*(visual.viewDirection()-dir)/180.f;
    if(double(std::cos(da))>ref)
      return false;
    }

  if(auto bbox = it.bBox()) {
    // npc eyesight height
    auto  at     = it.midPosition();
    auto  tMax   = (at - cen).length();
    auto  dir    = (at - cen)/tMax;
    float tHit   = DynamicWorld::rayBox(cen, dir, tMax, it.transform(), bbox[0], bbox[1]);

    const auto r = owner.physic()->ray(cen, cen+dir*tHit);
    if(r.hasCol)
      return false;
    } else {
    const auto r = owner.physic()->ray(cen, itMid);
    if(r.hasCol)
      return false;
    }

  return true;
  }
