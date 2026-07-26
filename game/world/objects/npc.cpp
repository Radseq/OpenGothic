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

Npc::Npc(World &owner, size_t instance, std::string_view waypoint, NpcProcessPolicy aiPolicy)
  :owner(owner),aiPolicy(aiPolicy),mvAlgo(*this) {
  outputPipe     = owner.script().openAiOuput();

  hnpc           = std::make_shared<zenkit::INpc>();
  hnpc->user_ptr = this;
  hnpc->id       = int32_t(instance & 0x7FFFFFFF);
  hnpc->wp       = std::string(waypoint);

  if(instance==size_t(-1))
    return;

  owner.script().initializeInstanceNpc(hnpc, instance);

  // vanilla behavior: equip best weapon and set non-zero damage type
  if(!isPlayer())
    invent.autoEquipWeapons(*this);
  if(hnpc->damage_type==0)
    hnpc->damage_type = 2;
  setTrueGuild(hnpc->guild); // https://worldofplayers.ru/threads/12446/post-878087
  setPerceptionTime(5000);   // https://github.com/Try/OpenGothic/pull/720#issuecomment-2602908614
  }

Npc::~Npc(){
  if(currentInteract)
    currentInteract->detach(*this,true);
  }

bool Npc::checkHealth(bool onChange, bool allowUnconscious) {
  if(isDead()) {
    return false;
    }
  if(isUnconscious() && allowUnconscious) {
    return false;
    }

  const int minHp = isMonster() ? 0 : 1;
  if(hnpc->attribute[ATR_HITPOINTS]<=minHp) {
    if(currentOther==nullptr || !allowUnconscious || !isHuman() ||
       owner.script().personAttitude(*this,*currentOther)==ATT_HOSTILE){
      if(hnpc->attribute[ATR_HITPOINTS]<=0)
        onNoHealth(true,HS_Dead);
      return false;
      }

    if(onChange) {
      onNoHealth(false,HS_Dead);
      return false;
      }
    }
  physic.setEnable(true);
  return true;
  }

void Npc::onNoHealth(bool death, HitSound sndMask) {
  invent.switchActiveWeapon(*this,Item::NSLOT);
  visual.dropWeapon(*this);
  visual.dropShield(*this);
  dropTorch();
  visual.setToFightMode(WeaponState::NoWeapon);
  updateWeaponSkeleton();

  setOther(lastHit);
  clearAiQueue();
  attachToPoint(nullptr);

  const char* svm   = death ? "SVM_%d_DEAD" : "SVM_%d_AARGH";
  const char* state = death ? "ZS_Dead"     : "ZS_Unconscious";

  if(!death)
    hnpc->attribute[ATR_HITPOINTS]=1;

  size_t fdead=owner.script().findSymbolIndex(state);
  startState(fdead,"",gtime::endOfTime(),true);
  // Note: clear perceptions for William in Jarkentar
  for(size_t i=0;i<PERC_Count;++i)
    setPerceptionDisable(PercType(i));
  if(hnpc->voice>0 && sndMask!=HS_NoSound && !isDive()) {
    emitSoundSVM(svm);
    }

  setInteraction(nullptr,true);
  invent.clearSlot(*this,"",false);

  if(death)
    physic.setEnable(false);

  if(death)
    setAnim(lastHitType=='A' ? Anim::DeadA        : Anim::DeadB); else
    setAnim(lastHitType=='A' ? Anim::UnconsciousA : Anim::UnconsciousB);

  Mmo::Hooks::onNpcLifecycleChanged(*this, lastHit, death, !death,
                                    "game/world/objects/npc.cpp:Npc::onNoHealth");
  }

bool Npc::hasAutoroll() const {
  auto gl = std::min<uint32_t>(guild(),GIL_MAX);
  return owner.script().guildVal().disable_autoroll[gl]==0;
  }

void Npc::stopWalkAnimation() {
  if(interactive()==nullptr)
    visual.stopWalkAnim(*this);
  setAnimRotate(0);
  }

World& Npc::world() {
  return owner;
  }

Vec3 Npc::position() const {
  return {x,y,z};
  }

Matrix4x4 Npc::transform() const {
  return visual.transform();
  }

Vec3 Npc::cameraBone(bool isFirstPerson) const {
  const size_t head = visual.pose().findNode("BIP01 HEAD");

  Vec3 r = {};
  if(isFirstPerson && head!=size_t(-1)) {
    r = visual.mapBone(head);
    } else {
    auto mt = visual.pose().rootBone();
    mt.project(r);
    }

  return r;
  }

Matrix4x4 Npc::cameraMatrix(bool isFirstPerson) const {
  const size_t head = visual.pose().findNode("BIP01 HEAD");
  if(isFirstPerson && head!=size_t(-1)) {
    return visual.pose().bone(head);
    }
  return visual.pose().rootBone();
  }

float Npc::rotation() const {
  return angle;
  }

float Npc::rotationRad() const {
  return angle*float(M_PI)/180.f;
  }

float Npc::rotationY() const {
  return angleY;
  }

float Npc::rotationYRad() const {
  return angleY*float(M_PI)/180.f;
  }

Bounds Npc::bounds() const {
  return visual.bounds();
  }

auto Npc::bBoxCol() const -> const Vec3* {
  if(visual.visualSkeleton()==nullptr)
    return nullptr;
  return visual.visualSkeleton()->bboxCol;
  }

auto Npc::bBox() const -> const Vec3* {
  if(visual.visualSkeleton()==nullptr)
    return nullptr;
  return visual.visualSkeleton()->bbox;
  }

Vec3 Npc::centerPosition() const {
  auto p = position();
  // p.y += 15; // seem to be off by ~15 centimeters, according to comparations vanilla testing
  p.y += visual.pose().translateY();
  return p;
  }

Vec3 Npc::collosionCenter() const {
  auto p = position();
  p += physic.centerAsym();
  return p;
  }

Npc* Npc::lookAtTarget() const {
  return currentLookAtNpc;
  }

std::string_view Npc::portalName() {
  return mvAlgo.portalName();
  }

std::string_view Npc::formerPortalName() {
  return mvAlgo.formerPortalName();
  }

float Npc::qDistTo(const Vec3 pos) const {
  auto dp = pos - centerPosition();
  return dp.quadLength();
  }

float Npc::qDistTo(const WayPoint *f) const {
  if(f==nullptr)
    return 0.f;
  return qDistTo(f->position());
  }

float Npc::qDistTo(const Npc &p) const {
  return qDistTo(p.centerPosition());
  }

float Npc::qDistTo(const Interactive &p) const {
  auto pos = p.nearestPoint(*this);
  return qDistTo(pos);
  }

float Npc::qDistTo(const Item& p) const {
  auto pos = p.midPosition();
  return qDistTo(pos);
  }

Tempest::Vec3 Npc::fightDistanceTo(const Npc& tg) const {
  //NOTE: game script decribe comabt distance as distance between BIP01
  // however, in practice, it's easier and more relieble to use rootTr
  Vec3 cen, tgCen;
  if(auto sk = visual.visualSkeleton()) {
    cen = sk->rootTr;
    transform().project(cen);
    }
  cen += position();

  if(auto sk = tg.visual.visualSkeleton()) {
    tgCen = sk->rootTr;
    transform().project(tgCen);
    }
  tgCen += tg.position();
  return (cen-tgCen);
  }

uint8_t Npc::calcAniComb() const {
  if(currentTarget==nullptr)
    return 0;
  auto dpos = currentTarget->position() - position();
  return Pose::calcAniComb(dpos,angle);
  }

std::string_view Npc::displayName() const {
  return hnpc->name[0];
  }

Tempest::Vec3 Npc::displayPosition() const {
  auto p = visual.displayPosition();
  return p+position();
  }
