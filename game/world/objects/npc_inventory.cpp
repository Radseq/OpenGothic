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
#include "utils/nativetelemetry.h"
#include "camera.h"
#include "gothic.h"
#include "resources.h"

using namespace Tempest;

void Npc::aiPush(AiQueue::AiAction&& a) {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::AiQueue))
    return;
  if(a.act==AI_OutputSvmOverlay)
    aiQueueOverlay.pushBack(std::move(a)); else
    aiQueue.pushBack(std::move(a));
  }

void Npc::resumeAiRoutine() {
  if(mmoAuthorityGate.rejectLocalGameplay(
         Mmo::ClientPresentation::NpcLocalGameplayEntryPoint::Routine))
    return;
  clearState(false);
  auto& r = currentRoutine();
  if(r.callback.isValid()) {
    auto t = endTime(r);
    startState(r.callback,r.wayPointName(),t,false);
    }
  }

Item* Npc::addItem(const size_t item, size_t count) {
  return invent.addItem(item,count,owner);
  }

Item* Npc::addItem(std::unique_ptr<Item>&& i) {
  return invent.addItem(std::move(i));
  }

Item* Npc::takeItem(Item& item) {
  if(interactive()!=nullptr)
    return nullptr;
  if(item.isTorchBurn() && (isUsingTorch() || weaponState()!=WeaponState::NoWeapon))
    return nullptr;

  const auto sourceWorldItemPersistentId = item.persistentId();
  const auto sourceItemSymbol = item.clsId();
  const auto sourceAmount = item.count();
  const auto sourcePosition = item.position();

  auto state = bodyStateMasked();
  if(state!=BS_STAND && state!=BS_SNEAK && state!=BS_SWIM && state!=BS_DIVE) {
    return nullptr;
    }

  const auto  dpos = item.midPosition()-centerPosition();
  const auto* sq   = setAnimAngGet(Npc::Anim::ItmGet, Pose::calcAniCombVert(dpos));
  if(sq==nullptr)
    return nullptr;

  std::unique_ptr<Item> ptr = owner.takeItem(item);
  if(ptr!=nullptr && ptr->isTorchBurn()) {
   if(!toggleTorch())
     return nullptr;
    size_t torchId = owner.script().findSymbolIndex("ItLsTorch");
    if(torchId!=size_t(-1))
      return nullptr;
    ptr.reset(new Item(owner,torchId,Item::T_Inventory));
    }

  auto it = ptr.get();
  if(it==nullptr)
    return nullptr;

  it = addItem(std::move(ptr));
  if(isPlayer() && it != nullptr) {
    const auto playerPosition = position();
    NativeTelemetry::itemPickedUp(
        it->displayName(), it->clsId(), it->count(),
        playerPosition.x, playerPosition.y, playerPosition.z, rotation(),
        sourcePosition.x, sourcePosition.y, sourcePosition.z);
  }
  if(it!=nullptr)
    Mmo::Hooks::onWorldItemPickedUp(*this, *it, sourceWorldItemPersistentId,
                                    sourceItemSymbol, sourceAmount,
                                    "game/world/objects/npc.cpp:Npc::takeItem");
  if(isPlayer() && it!=nullptr)
    owner.sendPassivePerc(*this,*this,*it,PERC_ASSESSTHEFT);

  implAniWait(uint64_t(sq->totalTime()));
  return it;
  }

void Npc::onWldItemRemoved(const Item& itm) {
  aiQueue.onWldItemRemoved(itm);
  aiQueueOverlay.onWldItemRemoved(itm);
  }

void Npc::addItem(size_t id, Interactive &chest, size_t count) {
  const auto sourceItemBefore = chest.inventory().getItem(id);
  const auto sourceItemPersistentId = sourceItemBefore != nullptr ? sourceItemBefore->persistentId() : 0u;
  const auto before = invent.itemCount(id);
  Inventory::transfer(invent,chest.inventory(),nullptr,id,count,owner);
  const auto after = invent.itemCount(id);
  const auto moved = after > before ? after - before : 0u;
  Mmo::Hooks::onContainerInventoryTaken(*this, chest, id, sourceItemPersistentId, moved,
                                        "game/world/objects/npc.cpp:Npc::addItem(Interactive)");
  }

void Npc::addItem(size_t id, Npc &from, size_t count) {
  const auto sourceItemBefore = from.invent.getItem(id);
  const auto sourceItemPersistentId = sourceItemBefore != nullptr ? sourceItemBefore->persistentId() : 0u;
  const auto before = invent.itemCount(id);
  Inventory::transfer(invent,from.invent,&from,id,count,owner);
  const auto after = invent.itemCount(id);
  const auto moved = after > before ? after - before : 0u;
  Mmo::Hooks::onNpcInventoryLooted(*this, from, id, sourceItemPersistentId, moved,
                                   "game/world/objects/npc.cpp:Npc::addItem(Npc)");
  }

void Npc::moveItem(size_t id, Interactive &to, size_t count) {
  Inventory::transfer(to.inventory(),invent,this,id,count,owner);
  }

void Npc::sellItem(size_t id, Npc &to, size_t count) {
  if(id==owner.script().goldId()->index())
    return;
  int32_t price = invent.sellPriceOf(id);
  const auto itemBefore = invent.getItem(id);
  const auto itemPersistentId = itemBefore != nullptr ? itemBefore->persistentId() : 0u;
  const auto countBefore = invent.itemCount(id);
  const auto goldBefore = invent.goldCount();
  Inventory::transfer(to.invent,invent,this,id,count,owner);
  invent.addItem(owner.script().goldId()->index(),size_t(price)*count,owner);
  const auto countAfter = invent.itemCount(id);
  const auto goldAfter = invent.goldCount();
  const auto moved = countBefore > countAfter ? countBefore - countAfter : 0u;
  Mmo::Hooks::onTradeSellToNpc(*this, to, id, itemPersistentId, moved, price,
                               goldBefore, goldAfter,
                               "game/world/objects/npc.cpp:Npc::sellItem");
  }

void Npc::buyItem(size_t id, Npc &from, size_t count) {
  if(id==owner.script().goldId()->index())
    return;

  int32_t price = from.invent.priceOf(id);
  if(price>0 && size_t(price)*count>invent.goldCount()) {
    count = invent.goldCount()/size_t(price);
    }
  if(count==0) {
    owner.script().printCannotBuyError(*this);
    return;
    }

  const auto vendorItemBefore = from.invent.getItem(id);
  const auto vendorItemPersistentId = vendorItemBefore != nullptr ? vendorItemBefore->persistentId() : 0u;
  const auto countBefore = invent.itemCount(id);
  const auto goldBefore = invent.goldCount();
  Inventory::transfer(invent,from.invent,nullptr,id,count,owner);
  if(price>=0)
    invent.delItem(owner.script().goldId()->index(),size_t( price)*count,*this); else
    invent.addItem(owner.script().goldId()->index(),size_t(-price)*count,owner);
  const auto countAfter = invent.itemCount(id);
  const auto goldAfter = invent.goldCount();
  const auto moved = countAfter > countBefore ? countAfter - countBefore : count;
  Mmo::Hooks::onTradeBuyFromNpc(*this, from, id, vendorItemPersistentId, moved, price,
                                goldBefore, goldAfter,
                                "game/world/objects/npc.cpp:Npc::buyItem");
  }

void Npc::dropItem(size_t id, size_t count) {
  if(id==size_t(-1))
    return;
  size_t cnt = invent.itemCount(id);
  if(count>cnt)
    count = cnt;
  if(count<1)
    return;

  const auto inventoryItemBefore = invent.getItem(id);
  const auto sourceItemPersistentId = inventoryItemBefore != nullptr ? inventoryItemBefore->persistentId() : 0u;

  auto sk = visual.visualSkeleton();
  if(sk==nullptr)
    return;

  size_t rightHand = sk->findNode("ZS_RIGHTHAND");
  if(rightHand==size_t(-1))
    return;

  if(!setAnim(Anim::ItmDrop))
    return;

  auto mat = visual.transform();
  if(rightHand<visual.pose().boneCount())
    mat = visual.pose().bone(rightHand);

  auto it = owner.addItemDyn(id,mat,hnpc->symbol_index());
  if(it==nullptr)
    return;
  it->setCount(count);
  invent.delItem(id,count,*this);
  Mmo::Hooks::onCharacterItemDropped(*this, *it, id, sourceItemPersistentId, count,
                                     "game/world/objects/npc.cpp:Npc::dropItem");
  }

void Npc::clearInventory() {
  invent.clear(owner.script(),*this);
  }

Item* Npc::currentArmor() {
  return invent.currentArmor();
  }

Item* Npc::currentMeleeWeapon() {
  return invent.currentMeleeWeapon();
  }

Item* Npc::currentRangedWeapon() {
  return invent.currentRangedWeapon();
  }

Item* Npc::currentShield() {
  return invent.currentShield();
  }

Vec3 Npc::mapWeaponBone() const {
  return visual.mapWeaponBone();
  }

Vec3 Npc::mapHeadBone() const {
  return visual.mapHeadBone();
  }

Vec3 Npc::mapBone(std::string_view bone) const {
  if(auto sk = visual.visualSkeleton()) {
    size_t id = sk->findNode(bone);
    if(id!=size_t(-1))
      return visual.mapBone(id);
    }

  Vec3 ret = {};
  ret.y = physic.centerY()-y;
  return ret+position();
  }

bool Npc::turnTo(float dx, float dz, bool noAnim, uint64_t dt) {
  return implTurnTo(dx,dz,noAnim?AnimationSolver::TurnType::None:AnimationSolver::TurnType::Std,dt);
  }

bool Npc::rotateTo(float dx, float dz, float step, AnimationSolver::TurnType anim, uint64_t dt) {
  //step *= (float(dt)/1000.f)*60.f/100.f;
  step *= (float(dt)/1000.f);

  if(dx==0.f && dz==0.f) {
    setAnimRotate(0);
    return false;
    }

  if(!isRotationAllowed())
    return false;

  float a  = angleDir(dx,dz);
  float da = a-angle;

  if(anim == AnimationSolver::TurnType::None || std::cos(double(da)*M_PI/180.0)>0) {
    if(float(std::abs(int(da)%360))<=(step*2.f)) {
      setAnimRotate(0);
      setDirection(a);
      return false;
      }
    } else {
    visual.stopWalkAnim(*this);
    }

  const auto sgn = std::sin(double(da)*M_PI/180.0);
  if(sgn==0) {
    setAnimRotate(0);
    } else {
    const int rot = (sgn<0) ? +1 : -1;
    switch(anim) {
      case AnimationSolver::TurnType::Std:
        setAnimRotate(rot);
        break;
      case AnimationSolver::TurnType::None:
        setAnimRotate(0);
        break;
      case AnimationSolver::TurnType::Whirl:
        visual.setAnimWhirl(*this, rot);
        break;
      }
    setDirection(angle - float(rot)*step);
    }
  return true;
  }

bool Npc::isRotationAllowed() const {
  auto bs  = bodyStateMasked();
  bool air = (!isPlayer() && isInAir()) || isFallingDeep();
  return currentInteract==nullptr && !isFinishingMove() && bs!=BS_CLIMB && bs!=BS_LIE && !air;
  }

bool Npc::checkGoToNpcdistance(const Npc &other) {
  return fghAlgo.isInAttackRange(*this,other,owner.script());
  }

size_t Npc::itemCount(size_t id) const {
  return invent.itemCount(id);
  }

Item* Npc::activeWeapon() {
  return invent.activeWeapon();
  }

Item *Npc::getItem(size_t id) {
  return invent.getItem(id);
  }

void Npc::delItem(size_t item, uint32_t amount) {
  invent.delItem(item,amount,*this);
  }

void Npc::useItem(size_t item) {
  useItem(item,Item::NSLOT,false);
  }

void Npc::useItem(size_t item, uint8_t slotHint, bool force) {
  invent.use(item,*this,slotHint,force);
  }

void Npc::setCurrentItem(size_t item) {
  invent.setCurrentItem(item);
  }

void Npc::unequipItem(size_t item) {
  invent.unequip(item,*this);
  }
