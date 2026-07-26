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

std::string_view Npc::Routine::wayPointName() const {
  return point!=nullptr ? point->name : fallbackName;
  }


void Npc::GoTo::save(Serialize& fout) const {
  fout.write(npc, uint8_t(flag), wp, pos);
  }

void Npc::GoTo::load(Serialize& fin) {
  fin.read(npc, reinterpret_cast<uint8_t&>(flag), wp, pos);
  //NOTE: no real need to version check
  //if(fin.version()<53) {
    if(flag==GoToHint::GT_Enemy || flag==GoToHint::GT_EnemyG)
      clear(); // not persistent flags, and should be cleared by FAI
  //  }
  }

Vec3 Npc::GoTo::target() const {
  if(npc!=nullptr)
    return npc->centerPosition();
  if(wp!=nullptr)
    return wp->position();
  return pos;
  }

bool Npc::GoTo::isClose(const Npc& self, float dist) const {
  if(flag==GT_Enemy)
    return self.fghAlgo.isInWRange(self, *npc, self.owner.script()); //need to be consistent with implAttack
  if(npc!=nullptr)
    return MoveAlgo::isClose(self, *npc, dist);
  if(wp!=nullptr)
    return MoveAlgo::isClose(self, *wp, dist);
  return MoveAlgo::isClose(self, pos, dist);
  }

bool Npc::GoTo::empty() const {
  return flag==Npc::GT_No;
  }

void Npc::GoTo::clear() {
  npc  = nullptr;
  wp   = nullptr;
  flag = Npc::GT_No;
  }

void Npc::GoTo::set(Npc* to, Npc::GoToHint hnt) {
  npc  = to;
  wp   = nullptr;
  flag = hnt;
  }

void Npc::GoTo::set(const WayPoint* to, GoToHint hnt) {
  npc  = nullptr;
  wp   = to;
  flag = hnt;
  }

void Npc::GoTo::set(const Item* to) {
  pos  = to->position();
  flag = Npc::GT_Item;
  }

void Npc::GoTo::set(const Vec3& to) {
  pos  = to;
  flag = GT_Point;
  }

void Npc::GoTo::setFlee() {
  flag = GT_Flee;
  }

bool Npc::setPosition(float ix, float iy, float iz) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementMutation))
    return false;
  if(x==ix && y==iy && z==iz)
    return false;
  x = ix;
  y = iy;
  z = iz;
  durtyTranform |= TR_Pos;
  physic.setPosition(Vec3{x,y,z});
  return true;
  }

bool Npc::setPosition(const Tempest::Vec3& pos) {
  return setPosition(pos.x,pos.y,pos.z);
  }

void Npc::setViewPosition(const Tempest::Vec3& pos) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementMutation))
    return;
  x = pos.x;
  y = pos.y;
  z = pos.z;
  durtyTranform |= TR_Pos;
  }

void Npc::setDirection(const Tempest::Vec3& pos) {
  float a = angleDir(pos.x, pos.z);
  setDirection(a);
  }

void Npc::setDirection(float rotation) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementMutation))
    return;
  durtyTranform |= TR_Rot;
  angle = rotation;
  physic.setRotation(angle);
  }

void Npc::setDirectionY(float rotation) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementMutation))
    return;
  if(rotation>90)
    rotation = 90;
  if(rotation<-90)
    rotation = -90;
  rotation = std::fmod(rotation,360.f);
  if(!mvAlgo.isDive() && !(interactive()!=nullptr && interactive()->isLadder()))
    return;
  angleY = rotation;
  durtyTranform |= TR_Rot;
  }

void Npc::setRunAngle(float angle) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementMutation))
    return;
  durtyTranform |= TR_Rot;
  runAng = angle;
  }

float Npc::angleDir(float x, float z) {
  float a = 0;
  if(x!=0.f || z!=0.f)
    a = 180.f*std::atan2(z,x)/float(M_PI);
  return a;
  }

bool Npc::resetPositionToTA() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Routine))
    return false;
  const bool g2       = owner.version().game==2;
  const bool isDragon = (g2 && guild()==GIL_DRAGON);
  const bool isDead   = this->isDead();

  if(isDead && !invent.hasMissionItems() && !isDragon)
    return false;

  invent.clearSlot(*this,"",currentInteract!=nullptr);
  if(!isPlayer())
    setInteraction(nullptr,true);

  // return monsters to their way-points
  // if(routines.empty() && !isPlayer())
  //   return currentTaPoint()!=nullptr;

  attachToPoint(nullptr);
  clearAiQueue();

  if(!isDead) {
    visual.stopAnim(*this,"");
    clearState(true);
    }

  if(isPlayer())
    return true;

  auto at = currentTaPoint();
  if(at==nullptr)
    return false;

  if(at->isLocked() && !isDead) {
    auto p = owner.findNextPoint(*at);
    if(p!=nullptr)
      at = p;
    }
  setPosition (at->position() );
  setDirection(at->direction());
  owner.script().fixNpcPosition(*this,0,0);

  if(!isDead) {
    attachToPoint(at);
    invent.autoEquipWeapons(*this);
    }

  owner.script().invokeRefreshAtInsert(*this);
  return true;
  }

void Npc::stopDlgAnim() {
  visual.stopDlgAnim(*this);
  }

void Npc::clearSpeed() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementMutation))
    return;
  mvAlgo.clearSpeed();
  }

void Npc::setProcessPolicy(NpcProcessPolicy t) {
  if(aiPolicy==t)
    return;
  if(aiPolicy==NpcProcessPolicy::Player)
    runAng = 0;
  aiPolicy=t;
  }

void Npc::setWalkMode(WalkBit m) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementMutation))
    return;
  wlkMode = m;
  }

bool Npc::isPlayer() const {
  return aiPolicy==NpcProcessPolicy::Player;
  }

bool Npc::startClimb(JumpStatus jump) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::MovementMutation))
    return false;
  setPosition(physic.position());
  visual.setAnimRotate(*this,0);
  return mvAlgo.startClimb(jump);
  }
