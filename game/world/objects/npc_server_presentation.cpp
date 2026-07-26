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

void Npc::setMmoServerReplica(const bool value) noexcept {
  if(isMmoServerReplica() == value)
    return;
  mmoAuthorityGate.setMode(
      value ? Mmo::ClientPresentation::NpcAuthorityMode::ServerReplica
            : Mmo::ClientPresentation::NpcAuthorityMode::NativeSinglePlayer);
  if(value) {
    auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
    clearAiQueue();
    clearState(true);
    currentTarget = nullptr;
    nearestEnemy = nullptr;
    currentOther = nullptr;
    currentVictim = nullptr;
    return;
  }
  mmoAuthorityGate.clearDiagnostics();
  mmoPresentationLifeState = MmoPresentationLifeState::Alive;
  mmoPresentationWeaponMode = WeaponState::NoWeapon;
  mmoPresentationWeaponTransitionFrom = WeaponState::NoWeapon;
  mmoPresentationWeaponTransitionPending = false;
  mmoPresentationMeleeTwoHanded = false;
  mmoPresentationRangedCrossbow = false;
  physic.setEnable(hnpc->attribute[ATR_HITPOINTS] > 0);
}

void Npc::applyMmoServerPresentationStats(
    const PersistentStats& state) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  restorePersistentStats(state);
}

void Npc::applyMmoServerPresentationTarget(Npc* const target) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  setTarget(target);
}

bool Npc::applyMmoServerPresentationTransform(
    const Tempest::Vec3& position,
    const float yaw,
    const bool clearVelocity) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  const bool changed = setPosition(position);
  setDirection(yaw);
  if(clearVelocity)
    clearSpeed();
  return changed;
}

bool Npc::applyMmoServerPresentationTranslation(
    const Tempest::Vec3& delta) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  return setPosition(position() + delta);
}

void Npc::applyMmoServerPresentationLocomotion(
    const Anim animation) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  static_cast<void>(setAnim(animation));
}

void Npc::applyMmoServerPresentationLifecycle(
    const int32_t healthCurrent,
    const int32_t healthMax,
    const MmoPresentationLifeState lifeState) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  const auto previous = mmoPresentationLifeState;
  mmoPresentationLifeState = lifeState;

  if(healthMax >= 0)
    hnpc->attribute[ATR_HITPOINTSMAX] = std::max(0, healthMax);
  if(healthCurrent >= 0) {
    const auto maximum = std::max(0, hnpc->attribute[ATR_HITPOINTSMAX]);
    hnpc->attribute[ATR_HITPOINTS] = maximum > 0
        ? std::clamp(healthCurrent, 0, maximum)
        : std::max(0, healthCurrent);
  }

  physic.setEnable(lifeState != MmoPresentationLifeState::Dead);
  if(previous == lifeState)
    return;

  switch(lifeState) {
    case MmoPresentationLifeState::Alive:
      static_cast<void>(setAnim(Anim::Idle));
      break;
    case MmoPresentationLifeState::Unconscious:
      static_cast<void>(setAnim(Anim::UnconsciousA));
      break;
    case MmoPresentationLifeState::Dead:
      static_cast<void>(setAnim(Anim::DeadA));
      break;
  }
}

void Npc::applyMmoServerPresentationWeaponMode(
    const WeaponState mode,
    const bool animate,
    const bool meleeTwoHanded,
    const bool rangedCrossbow) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  mmoPresentationMeleeTwoHanded = meleeTwoHanded;
  mmoPresentationRangedCrossbow = rangedCrossbow;
  const auto previousMode = visual.fightMode();
  const bool animationStarted =
      animate && previousMode!=mode && visual.startAnim(*this,mode);
  mmoPresentationWeaponMode = mode;
  mmoPresentationWeaponTransitionFrom = previousMode;
  mmoPresentationWeaponTransitionPending = animationStarted;
  static_cast<void>(visual.setToFightMode(mode));
  if(!animationStarted) {
    visual.updateWeaponSkeletonPresentation(
        mmoPresentationMeleeTwoHanded,mmoPresentationRangedCrossbow);
  }

  switch(mode) {
    case WeaponState::NoWeapon: hnpc->weapon = 0; break;
    case WeaponState::Fist:     hnpc->weapon = 1; break;
    case WeaponState::W1H:      hnpc->weapon = 3; break;
    case WeaponState::W2H:      hnpc->weapon = 4; break;
    case WeaponState::Bow:      hnpc->weapon = 5; break;
    case WeaponState::CBow:     hnpc->weapon = 6; break;
    case WeaponState::Mage:     hnpc->weapon = 7; break;
    }
}

void Npc::applyMmoServerPresentationCombatAction(
    const MmoPresentationCombatAction action,
    const uint16_t comboIndex,
    const bool leftSide,
    const bool rightSide) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  Anim animation = Anim::Idle;
  switch(action) {
    case MmoPresentationCombatAction::LightAttack:
    case MmoPresentationCombatAction::HeavyAttack:
      animation = Anim::Attack;
      break;
    case MmoPresentationCombatAction::ComboAttack:
      if(leftSide)
        animation = Anim::AttackL;
      else if(rightSide)
        animation = Anim::AttackR;
      else
        animation = (comboIndex%2U)==0U ? Anim::AttackL : Anim::AttackR;
      break;
    case MmoPresentationCombatAction::Parry:
      animation = Anim::AttackBlock;
      break;
    case MmoPresentationCombatAction::Dodge:
      if(leftSide)
        animation = Anim::MoveL;
      else if(rightSide)
        animation = Anim::MoveR;
      else
        animation = Anim::MoveBack;
      break;
    case MmoPresentationCombatAction::Cancel:
      animation = Anim::Idle;
      break;
    }

  if(setAnim(animation))
    return;
  visual.interrupt();
  static_cast<void>(setAnim(animation));
}

void Npc::correctMmoServerPresentationCombat(
    const WeaponState authoritativeMode,
    const bool meleeTwoHanded,
    const bool rangedCrossbow) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  visual.interrupt();
  applyMmoServerPresentationWeaponMode(
      authoritativeMode,false,meleeTwoHanded,rangedCrossbow);
  static_cast<void>(setAnim(Anim::Idle));
}

void Npc::applyMmoServerPresentationHitReaction(
    const MmoPresentationHitReaction reaction) {
  auto serverPresentation = mmoAuthorityGate.serverPresentationScope();
  Anim animation = Anim::StumbleA;
  switch(reaction) {
    case MmoPresentationHitReaction::Light:
      animation = Anim::StumbleA;
      break;
    case MmoPresentationHitReaction::Heavy:
    case MmoPresentationHitReaction::Knockback:
      animation = Anim::StumbleB;
      break;
    case MmoPresentationHitReaction::Blocked:
      animation = Anim::AttackBlock;
      break;
    case MmoPresentationHitReaction::Knockdown:
      animation = Anim::FallenA;
      break;
    }
  visual.interrupt();
  static_cast<void>(setAnim(animation));
}
