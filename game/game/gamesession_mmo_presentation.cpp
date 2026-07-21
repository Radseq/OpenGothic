#include "gamesession.h"
#include "savegameheader.h"
#if OPENGOTHIC_MMO_SQLITE_TOOLING
#include "../../tools/mmo/mmoruntimesqlite.h"
#endif
#include "mmosemantichooks.h"
#include "mmoclientbridge.h"
#include "mmoclientpresentationcatalog.h"
#include "mmoserverpresentationbatchconsumer.h"
#include "mmorestoresnapshot.h"

#include <Tempest/Log>
#include <Tempest/MemReader>
#include <Tempest/MemWriter>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <chrono>
#include <charconv>
#include <thread>
#include <limits>
#include <iterator>
#include <functional>
#include <optional>
#include <utility>
#include <type_traits>

#include "utils/string_frm.h"
#include "worldstatestorage.h"
#include "world/objects/npc.h"
#include "world/objects/item.h"
#include "world/objects/interactive.h"
#include "world/world.h"
#include "world/waypoint.h"
#include "sound/soundfx.h"
#include "serialize.h"
#include "camera.h"
#include "gothic.h"
#include "commandline.h"

using namespace Tempest;

namespace {

struct MmoPresentationWeaponShape final {
  bool meleeEquipped = false;
  bool meleeTwoHanded = false;
  bool rangedEquipped = false;
  bool rangedCrossbow = false;
};

[[nodiscard]] constexpr bool equipmentOccupied(
    const Mmo::ClientPresentation::ServerPresentationEquipmentSlotRecord*
        state) noexcept {
  return state != nullptr &&
         (state->flags &
          Mmo::ClientPresentation::ServerPresentationEquipmentOccupied) != 0U;
}

[[nodiscard]] constexpr WeaponState presentationWeaponState(
    const Mmo::ClientPresentation::ServerPresentationWeaponMode mode,
    const MmoPresentationWeaponShape shape) noexcept {
  using Mode = Mmo::ClientPresentation::ServerPresentationWeaponMode;
  switch(mode) {
    case Mode::None: return WeaponState::NoWeapon;
    case Mode::Melee:
      if(!shape.meleeEquipped)
        return WeaponState::Fist;
      return shape.meleeTwoHanded ? WeaponState::W2H : WeaponState::W1H;
    case Mode::Ranged:
      if(!shape.rangedEquipped)
        return WeaponState::NoWeapon;
      return shape.rangedCrossbow ? WeaponState::CBow : WeaponState::Bow;
    case Mode::Magic: return WeaponState::Mage;
    case Mode::Fist: return WeaponState::Fist;
  }
  return WeaponState::NoWeapon;
}

[[nodiscard]] constexpr Npc::MmoPresentationLifeState presentationLifeState(
    const Mmo::ClientPresentation::ServerPresentationNpcLifeState state) noexcept {
  using State = Mmo::ClientPresentation::ServerPresentationNpcLifeState;
  switch(state) {
    case State::Alive: return Npc::MmoPresentationLifeState::Alive;
    case State::Unconscious: return Npc::MmoPresentationLifeState::Unconscious;
    case State::Dead: return Npc::MmoPresentationLifeState::Dead;
  }
  return Npc::MmoPresentationLifeState::Alive;
}

[[nodiscard]] constexpr Npc::MmoPresentationCombatAction
presentationCombatAction(
    const Mmo::ClientPresentation::ServerPresentationCombatActionKind action) noexcept {
  using Source = Mmo::ClientPresentation::ServerPresentationCombatActionKind;
  switch(action) {
    case Source::LightAttack:
      return Npc::MmoPresentationCombatAction::LightAttack;
    case Source::HeavyAttack:
      return Npc::MmoPresentationCombatAction::HeavyAttack;
    case Source::ComboAttack:
      return Npc::MmoPresentationCombatAction::ComboAttack;
    case Source::Parry:
      return Npc::MmoPresentationCombatAction::Parry;
    case Source::Dodge:
      return Npc::MmoPresentationCombatAction::Dodge;
    case Source::CancelAction:
      return Npc::MmoPresentationCombatAction::Cancel;
  }
  return Npc::MmoPresentationCombatAction::Cancel;
}

[[nodiscard]] constexpr Npc::MmoPresentationHitReaction
presentationHitReaction(
    const Mmo::ClientPresentation::ServerPresentationHitReactionKind reaction) noexcept {
  using Source = Mmo::ClientPresentation::ServerPresentationHitReactionKind;
  switch(reaction) {
    case Source::Light: return Npc::MmoPresentationHitReaction::Light;
    case Source::Heavy: return Npc::MmoPresentationHitReaction::Heavy;
    case Source::Blocked: return Npc::MmoPresentationHitReaction::Blocked;
    case Source::Knockback: return Npc::MmoPresentationHitReaction::Knockback;
    case Source::Knockdown: return Npc::MmoPresentationHitReaction::Knockdown;
  }
  return Npc::MmoPresentationHitReaction::Light;
}

void hashCombine(std::uint64_t& seed, std::uint64_t value) noexcept {
  seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
}

std::uint64_t mmoInteractiveStateSignature(
    const Mmo::ClientPresentation::ServerPresentationInteractiveStateRecord& state) noexcept {
  std::uint64_t signature = 0x6B8B4567327B23C6ULL;
  hashCombine(signature, state.stateId);
  hashCombine(signature, state.flags);
  hashCombine(signature, state.user.world.id);
  hashCombine(signature, state.user.world.generation);
  hashCombine(signature, state.user.id);
  hashCombine(signature, state.user.generation);
  return signature;
}

std::uint64_t mmoMoverStateSignature(
    const Mmo::ClientPresentation::ServerPresentationMoverStateRecord& state) noexcept {
  std::uint64_t signature = 0x643C986966334873ULL;
  hashCombine(signature, static_cast<std::uint64_t>(state.phase));
  hashCombine(signature, state.keyframe);
  hashCombine(signature, state.normalizedProgress);
  hashCombine(signature, state.flags);
  return signature;
}

[[nodiscard]] constexpr Mmo::ClientPresentation::ServerEntityHandle
presentationRegistryHandle(
    const Mmo::ClientPresentation::ServerPresentationEntityHandle handle) noexcept {
  return {.id = handle.id, .generation = handle.generation};
}

} // namespace

void GameSession::resetMmoServerPresentationProjection() noexcept {
  if(!mmoPresentationRouteKey.empty())
    Gothic::inst().resetTypedServerDialogPresentation();

  ++mmoPresentationWorldGeneration;
  if(mmoPresentationWorldGeneration == 0U)
    mmoPresentationWorldGeneration = 1U;

  auto released =
      mmoServerEntityPresentation.resetRoute(mmoPresentationWorldGeneration);
  for(const auto& binding : released)
    releaseMmoServerPresentationBinding(binding);

  mmoServerEntityInterpolator.resetRoute(mmoPresentationWorldGeneration);
  mmoServerProjectilePresentation.resetRoute();
  mmoMovementCorrectionBoundary.resetRoute(mmoPresentationWorldGeneration);
  mmoServerInventoryPresentation_.reset();
  resetMmoServerWorldItems();
  mmoServerWorldObjects.resetRoute({});
  mmoServerEntitySamples.clear();
  mmoServerProjectileSamples.clear();
}

void GameSession::resetMmoServerPresentationWorld() noexcept {
  resetMmoServerPresentationProjection();
  mmoTypedServerPresentation.reset();
  mmoPresentationRouteKey.clear();
}


struct GameSession::MmoServerPresentationBatchSink final {
  explicit MmoServerPresentationBatchSink(GameSession& owner) noexcept
      : owner(owner) {}

  void routeApplied(
      const Mmo::ClientPresentation::ServerPresentationRouteIdentity& route,
      const Mmo::ClientPresentation::ServerPresentationRouteReplaceResult&) {
    owner.resetMmoServerPresentationProjection();
    owner.setMmoServerPresentationRoute(route);
    Mmo::recordClientMmoProcessGatePresentation(
        Mmo::ClientMmoProcessGatePresentationEvent::RouteApplied);
    projectionResetForRoute = true;
  }

  void bootstrapApplied(
      const Mmo::ClientPresentation::ServerPresentationBootstrap& bootstrap,
      const Mmo::ClientPresentation::ServerPresentationBootstrapInstallResult&) {
    owner.installMmoServerPresentationBootstrap(
        bootstrap, std::exchange(projectionResetForRoute, false));
  }

  void eventApplied(
      const Mmo::ClientPresentation::ServerPresentationEvent& event,
      const Mmo::ClientPresentation::ServerPresentationApplyResult& result) {
    owner.applyMmoServerPresentationEvent(event, result);
  }

  template<class Record, class Result>
  void rejected(const Record&, const Result& result) const {
    Log::e("MMO typed presentation state rejected record: status=",
           static_cast<unsigned>(result.status));
  }

  void sourceRejected(const std::size_t count) const {
    Log::e("MMO typed presentation facade rejected records: count=", count);
  }

  GameSession& owner;
  bool projectionResetForRoute = false;
};

void GameSession::applyMmoServerCombatAction(
    const Mmo::ClientPresentation::ServerPresentationCombatActionRecord& action) noexcept {
  auto* actor = resolveMmoServerEntity(action.entity);
  if(actor == nullptr)
    return;
  actor->setMmoServerReplica(true);
  actor->setTarget(action.target.empty()
                       ? nullptr
                       : resolveMmoServerEntity(action.target));

  const bool predictedLocally =
      (action.flags &
       Mmo::ClientPresentation::ServerPresentationCombatActionPredictedLocally) != 0U;
  if(predictedLocally && wrld != nullptr && actor == wrld->player())
    return;

  actor->applyMmoServerPresentationCombatAction(
      presentationCombatAction(action.kind), action.comboIndex,
      (action.flags &
       Mmo::ClientPresentation::ServerPresentationCombatActionLeftSide) != 0U,
      (action.flags &
       Mmo::ClientPresentation::ServerPresentationCombatActionRightSide) != 0U);
}

void GameSession::resolveMmoServerCombatAction(
    const Mmo::ClientPresentation::ServerPresentationCombatActionResolution& resolution) noexcept {
  using namespace Mmo::ClientPresentation;
  if(resolution.result == ServerPresentationCombatActionResult::Completed)
    return;
  auto* actor = resolveMmoServerEntity(resolution.entity);
  if(actor == nullptr)
    return;

  const auto* melee = mmoTypedServerPresentation.findEquipment(
      resolution.entity, ServerPresentationEquipmentSlot::MeleeWeapon);
  const auto* ranged = mmoTypedServerPresentation.findEquipment(
      resolution.entity, ServerPresentationEquipmentSlot::RangedWeapon);
  const MmoPresentationWeaponShape shape{
      .meleeEquipped = equipmentOccupied(melee),
      .meleeTwoHanded =
          melee != nullptr &&
          (melee->flags & ServerPresentationEquipmentTwoHanded) != 0U,
      .rangedEquipped = equipmentOccupied(ranged),
      .rangedCrossbow =
          ranged != nullptr &&
          (ranged->flags & ServerPresentationEquipmentCrossbow) != 0U,
  };
  auto authoritativeMode = resolution.authoritativeWeaponMode;
  if(const auto* mode =
         mmoTypedServerPresentation.findWeaponMode(resolution.entity);
     mode != nullptr) {
    authoritativeMode = mode->mode;
  }
  actor->correctMmoServerPresentationCombat(
      presentationWeaponState(authoritativeMode, shape),
      shape.meleeTwoHanded, shape.rangedCrossbow);
}

void GameSession::applyMmoServerDamage(
    const Mmo::ClientPresentation::ServerPresentationDamageRecord& damage) noexcept {
  auto* target = resolveMmoServerEntity(damage.target);
  if(target == nullptr)
    return;
  Npc::PersistentStats stats;
  stats.healthCurrent = damage.health;
  stats.healthMax = damage.maximumHealth >= 0
                        ? damage.maximumHealth
                        : Npc::PersistentStats::Missing;
  target->restorePersistentStats(stats);
  target->setMmoServerReplica(true);
}

void GameSession::applyMmoServerHitReaction(
    const Mmo::ClientPresentation::ServerPresentationHitReactionRecord& reaction) noexcept {
  if(wrld == nullptr)
    return;
  auto* target = resolveMmoServerEntity(reaction.target);
  if(target == nullptr)
    return;
  target->setMmoServerReplica(true);
  target->applyMmoServerPresentationHitReaction(
      presentationHitReaction(reaction.kind));

  const Tempest::Vec3 knockback{
      reaction.knockbackX, reaction.knockbackY, reaction.knockbackZ};
  if(knockback.x != 0.f || knockback.y != 0.f || knockback.z != 0.f)
    static_cast<void>(target->setPosition(target->position()+knockback));

  const auto effectFlags =
      Mmo::ClientPresentation::ServerPresentationHitReactionVfx |
      Mmo::ClientPresentation::ServerPresentationHitReactionSfx;
  if(auto* source = reaction.source.empty()
                        ? nullptr
                        : resolveMmoServerEntity(reaction.source);
     source != nullptr && (reaction.flags & effectFlags) != 0U) {
    const bool spawnVfx =
        (reaction.flags &
         Mmo::ClientPresentation::ServerPresentationHitReactionVfx) != 0U;
    auto effect =
        wrld->addWeaponHitEffect(*source, nullptr, *target, spawnVfx);
    if((reaction.flags &
        Mmo::ClientPresentation::ServerPresentationHitReactionSfx) != 0U) {
      effect.play();
    }
  }

  if(target == wrld->player() &&
     (reaction.flags &
      Mmo::ClientPresentation::ServerPresentationHitReactionCameraShake) != 0U) {
    camera().addPresentationShake(reaction.cameraShakeStrength);
  }
}

void GameSession::applyMmoServerLifeState(
    const Mmo::ClientPresentation::ServerPresentationLifeStateRecord& state) noexcept {
  auto* npc = resolveMmoServerEntity(state.entity);
  if(npc == nullptr)
    return;
  npc->setMmoServerReplica(true);
  npc->applyMmoServerPresentationLifecycle(
      state.health, state.maximumHealth,
      presentationLifeState(state.lifeState));
}

void GameSession::applyMmoServerInteractiveState(
    const Mmo::ClientPresentation::ServerPresentationInteractiveStateRecord& state) noexcept {
  using namespace Mmo::ClientPresentation;
  if(wrld == nullptr ||
     state.stateId > static_cast<std::uint64_t>(
                         std::numeric_limits<std::int32_t>::max())) {
    return;
  }

  const auto signature = mmoInteractiveStateSignature(state);
  const auto status = mmoServerWorldObjects.inspectState(
      state.entity, state.stateRevision, signature);
  if(status == ServerWorldObjectStateStatus::Duplicate ||
     status == ServerWorldObjectStateStatus::Stale)
    return;
  if(status == ServerWorldObjectStateStatus::Unchanged) {
    mmoServerWorldObjects.commitState(
        state.entity, state.stateRevision, signature);
    return;
  }

  const auto* local = mmoServerWorldObjects.find(state.entity);
  if(local == nullptr ||
     (local->kind != ServerPresentationWorldObjectKind::Interactive &&
      local->kind != ServerPresentationWorldObjectKind::Container)) {
    if(mmoServerWorldObjects.shouldLogUnresolved(
           state.entity, state.stateRevision)) {
      Log::e("MMO typed interactive unresolved: entity=", state.entity.id,
             " generation=", state.entity.generation,
             " revision=", state.stateRevision);
    }
    return;
  }

  auto* interactive = wrld->interactiveByVobId(local->vobObjectId);
  if(interactive == nullptr) {
    if(mmoServerWorldObjects.shouldLogUnresolved(
           state.entity, state.stateRevision)) {
      Log::e("MMO typed interactive local VOB unresolved: entity=",
             state.entity.id,
             " vob=", local->vobObjectId,
             " revision=", state.stateRevision);
    }
    return;
  }
  const bool locked =
      (state.flags & ServerPresentationInteractiveLocked) != 0U;
  interactive->restorePersistentState(
      static_cast<std::int32_t>(state.stateId), locked, false);
  mmoServerWorldObjects.commitState(
      state.entity, state.stateRevision, signature);
  Mmo::recordClientMmoProcessGatePresentation(
      Mmo::ClientMmoProcessGatePresentationEvent::InteractiveApplied);
}

void GameSession::applyMmoServerMoverState(
    const Mmo::ClientPresentation::ServerPresentationMoverStateRecord& state) noexcept {
  using namespace Mmo::ClientPresentation;
  if(wrld == nullptr)
    return;

  const auto signature = mmoMoverStateSignature(state);
  const auto status = mmoServerWorldObjects.inspectState(
      state.entity, state.stateRevision, signature);
  if(status == ServerWorldObjectStateStatus::Duplicate ||
     status == ServerWorldObjectStateStatus::Stale)
    return;
  if(status == ServerWorldObjectStateStatus::Unchanged) {
    mmoServerWorldObjects.commitState(
        state.entity, state.stateRevision, signature);
    return;
  }

  const auto* local = mmoServerWorldObjects.find(state.entity);
  if(local == nullptr || local->kind != ServerPresentationWorldObjectKind::Mover) {
    if(mmoServerWorldObjects.shouldLogUnresolved(
           state.entity, state.stateRevision)) {
      Log::e("MMO typed mover unresolved: entity=", state.entity.id,
             " generation=", state.entity.generation,
             " revision=", state.stateRevision);
    }
    return;
  }

  std::int32_t moverState = 0;
  switch(state.phase) {
    case ServerPresentationMoverPhase::AtStart: moverState = 0; break;
    case ServerPresentationMoverPhase::Opening: moverState = 2; break;
    case ServerPresentationMoverPhase::AtEnd: moverState = 0; break;
    case ServerPresentationMoverPhase::Closing: moverState = 4; break;
    case ServerPresentationMoverPhase::Paused: moverState = 0; break;
  }

  const auto frame = static_cast<std::int32_t>(state.keyframe);
  if(!wrld->restoreMoverState(local->vobObjectId, moverState, frame, -1)) {
    if(mmoServerWorldObjects.shouldLogUnresolved(
           state.entity, state.stateRevision)) {
      Log::e("MMO typed mover local VOB unresolved: entity=", state.entity.id,
             " vob=", local->vobObjectId,
             " phase=", static_cast<unsigned>(state.phase),
             " keyframe=", state.keyframe,
             " revision=", state.stateRevision);
    }
    return;
  }
  mmoServerWorldObjects.commitState(
      state.entity, state.stateRevision, signature);
  Mmo::recordClientMmoProcessGatePresentation(
      Mmo::ClientMmoProcessGatePresentationEvent::MoverApplied);
}

void GameSession::applyMmoServerMovementCorrection() noexcept {
  if(wrld == nullptr || mmoPresentationRouteKey.empty())
    return;
  const auto typed = mmoTypedServerPresentation.takePendingCorrection();
  if(!typed.has_value())
    return;

  using namespace Mmo::ClientPresentation;
  const bool hardSnap = typed->transform.teleport ||
                        typed->reason == ServerMovementCorrectionReason::Teleport ||
                        typed->reason == ServerMovementCorrectionReason::Resync;
  const ServerMovementCorrection correction{
      .route = {
          .worldGeneration = mmoPresentationWorldGeneration,
          .worldInstanceId = mmoPresentationRouteKey,
      },
      .handle = presentationRegistryHandle(typed->entity),
      .serverTick = typed->header.serverTick,
      .posX = typed->transform.posX,
      .posY = typed->transform.posY,
      .posZ = typed->transform.posZ,
      .yaw = typed->transform.yaw,
      .hardSnap = hardSnap,
  };
  if(mmoMovementCorrectionBoundary.observe(correction) !=
     ServerMovementCorrectionStatus::Accepted) {
    return;
  }
  const auto accepted = mmoMovementCorrectionBoundary.takePending();
  auto* hero = wrld->player();
  if(!accepted.has_value() || hero == nullptr)
    return;

  if(hero->setPosition(static_cast<float>(accepted->posX),
                       static_cast<float>(accepted->posY),
                       static_cast<float>(accepted->posZ))) {
    hero->setDirection(static_cast<float>(accepted->yaw));
    if(accepted->hardSnap)
      hero->clearSpeed();
    Mmo::recordClientMmoProcessGatePresentation(
        Mmo::ClientMmoProcessGatePresentationEvent::MovementCorrectionApplied);
  }
}


void GameSession::applyMmoServerPresentationEvent(
    const Mmo::ClientPresentation::ServerPresentationEvent& event,
    const Mmo::ClientPresentation::ServerPresentationApplyResult& result) noexcept {
  using namespace Mmo::ClientPresentation;
  Mmo::Hooks::ScopedCaptureSuppression suppressCapture;
  std::visit(
      [this, &event, &result](const auto& value) {
        using Event = std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<Event, ServerEntitySpawnEvent>) {
          if(result.releasedEntity.has_value())
            releaseMmoServerEntity(*result.releasedEntity);
          materializeMmoServerEntity(value.entity, true);
        } else if constexpr(std::is_same_v<Event, ServerEntityDespawnEvent>) {
          if(result.releasedEntity.has_value()) {
            releaseMmoServerEntity(*result.releasedEntity);
            Mmo::recordClientMmoProcessGatePresentation(
                Mmo::ClientMmoProcessGatePresentationEvent::EntityDespawnApplied);
          }
        } else if constexpr(std::is_same_v<Event, ServerEntityTransformEvent>) {
          const auto* entity = mmoTypedServerPresentation.findEntity(value.entity);
          if(entity != nullptr) {
            applyMmoServerEntityTransform(*entity,
                                          value.transform.teleport);
            Mmo::recordClientMmoProcessGatePresentation(
                Mmo::ClientMmoProcessGatePresentationEvent::TransformApplied);
          }
        } else if constexpr(std::is_same_v<Event, ServerMovementCorrectionEvent>) {
          applyMmoServerMovementCorrection();
        } else if constexpr(std::is_same_v<Event, ServerNpcStateEvent>) {
          applyMmoServerNpcState(value.state);
        } else if constexpr(std::is_same_v<Event, ServerWorldItemSpawnEvent>) {
          applyMmoServerWorldItemSpawn(value);
        } else if constexpr(std::is_same_v<Event, ServerWorldItemDespawnEvent>) {
          applyMmoServerWorldItemDespawn(value);
        } else if constexpr(std::is_same_v<Event, ServerWorldItemStateChangedEvent>) {
          applyMmoServerWorldItemStateChanged(value);
        } else if constexpr(std::is_same_v<Event, ServerInventorySnapshotEvent>) {
          applyMmoServerInventorySnapshot(value);
        } else if constexpr(std::is_same_v<Event, ServerInventoryDeltaEvent>) {
          applyMmoServerInventoryDelta(value);
        } else if constexpr(std::is_same_v<Event, ServerEquipmentSnapshotEvent>) {
          applyMmoServerEquipmentSnapshot(value);
        } else if constexpr(std::is_same_v<Event, ServerEquipmentBindingChangedEvent>) {
          applyMmoServerEquipmentBinding(value);
        } else if constexpr(std::is_same_v<Event, ServerEquipmentSlotChangedEvent>) {
          applyMmoServerEquipmentSlot(value.state);
        } else if constexpr(std::is_same_v<Event, ServerWeaponModeChangedEvent>) {
          applyMmoServerWeaponMode(value.state, true);
        } else if constexpr(std::is_same_v<Event, ServerCombatActionStartedEvent>) {
          applyMmoServerCombatAction(value.action);
        } else if constexpr(std::is_same_v<Event, ServerCombatActionResolvedEvent>) {
          resolveMmoServerCombatAction(value.resolution);
        } else if constexpr(std::is_same_v<Event, ServerDamageAppliedEvent>) {
          applyMmoServerDamage(value.damage);
        } else if constexpr(std::is_same_v<Event, ServerHitReactionEvent>) {
          applyMmoServerHitReaction(value.reaction);
        } else if constexpr(std::is_same_v<Event, ServerCharacterDeathStateChangedEvent>) {
          applyMmoServerLifeState(value.state);
        } else if constexpr(std::is_same_v<Event, ServerInteractiveStateEvent>) {
          applyMmoServerInteractiveState(value.state);
        } else if constexpr(std::is_same_v<Event, ServerMoverStateEvent>) {
          applyMmoServerMoverState(value.state);
        } else if constexpr(std::is_same_v<Event, ServerDialogStartEvent>) {
          auto* player = resolveMmoServerEntity(value.player);
          auto* npc = resolveMmoServerEntity(value.npc);
          Gothic::inst().presentTypedServerDialog(player, npc, nullptr, event);
          Mmo::recordClientMmoProcessGatePresentation(
              Mmo::ClientMmoProcessGatePresentationEvent::DialogApplied);
        } else if constexpr(std::is_same_v<Event, ServerDialogChoiceEvent>) {
          const auto& dialog = mmoTypedServerPresentation.dialog();
          auto* player = resolveMmoServerEntity(dialog.player);
          auto* npc = resolveMmoServerEntity(dialog.npc);
          Gothic::inst().presentTypedServerDialog(player, npc, nullptr, event);
        } else if constexpr(std::is_same_v<Event, ServerDialogUpdateEvent>) {
          const auto& dialog = mmoTypedServerPresentation.dialog();
          auto* player = resolveMmoServerEntity(dialog.player);
          auto* npc = resolveMmoServerEntity(dialog.npc);
          auto* speaker = resolveMmoServerEntity(value.speaker);
          Gothic::inst().presentTypedServerDialog(player, npc, speaker, event);
          Mmo::recordClientMmoProcessGatePresentation(
              Mmo::ClientMmoProcessGatePresentationEvent::DialogApplied);
        } else if constexpr(std::is_same_v<Event, ServerDialogEndEvent>) {
          Gothic::inst().presentTypedServerDialog(nullptr, nullptr, nullptr, event);
          Mmo::recordClientMmoProcessGatePresentation(
              Mmo::ClientMmoProcessGatePresentationEvent::DialogApplied);
        } else if constexpr(std::is_same_v<Event, ServerDialogBusyEvent>) {
          auto* npc = resolveMmoServerEntity(value.npc);
          Gothic::inst().presentTypedServerDialog(nullptr, npc, nullptr, event);
          Mmo::recordClientMmoProcessGatePresentation(
              Mmo::ClientMmoProcessGatePresentationEvent::DialogApplied);
        } else if constexpr(std::is_same_v<Event, ServerProjectileSpawnEvent>) {
          const auto status = mmoServerProjectilePresentation.observe(
              value.projectile, ticks);
          if(status != ServerProjectilePresentationStatus::Applied &&
             status != ServerProjectilePresentationStatus::Duplicate) {
            Log::e("MMO projectile spawn presentation rejected: status=",
                   static_cast<unsigned>(status),
                   " projectile=", value.projectile.projectileId);
          }
        } else if constexpr(std::is_same_v<Event, ServerProjectileStateEvent>) {
          const auto status = mmoServerProjectilePresentation.observe(
              value.projectile, ticks);
          if(status != ServerProjectilePresentationStatus::Applied &&
             status != ServerProjectilePresentationStatus::Duplicate &&
             status != ServerProjectilePresentationStatus::Stale) {
            Log::e("MMO projectile state presentation rejected: status=",
                   static_cast<unsigned>(status),
                   " projectile=", value.projectile.projectileId);
          }
        } else if constexpr(std::is_same_v<Event, ServerProjectileImpactEvent>) {
          const auto status = mmoServerProjectilePresentation.impact(
              value.impact, ticks);
          if(status != ServerProjectilePresentationStatus::Applied &&
             status != ServerProjectilePresentationStatus::Stale) {
            Log::e("MMO projectile impact presentation rejected: status=",
                   static_cast<unsigned>(status),
                   " projectile=", value.impact.projectileId);
          }
        } else if constexpr(std::is_same_v<Event, ServerProjectileDespawnEvent>) {
          const auto status = mmoServerProjectilePresentation.despawn(value);
          if(status != ServerProjectilePresentationStatus::Applied &&
             status != ServerProjectilePresentationStatus::Missing &&
             status != ServerProjectilePresentationStatus::Stale) {
            Log::e("MMO projectile despawn presentation rejected: status=",
                   static_cast<unsigned>(status),
                   " projectile=", value.projectileId);
          }
        } else if constexpr(std::is_same_v<Event, ServerWorldDescriptorEvent>) {
          Log::i("MMO typed world descriptor updated: revision=",
                 value.descriptor.descriptorRevision,
                 " content=", value.descriptor.contentFingerprint,
                 " story=", value.descriptor.storyProjectionFingerprint);
        }
      },
      event);
}

void GameSession::pollMmoServerPresentationMailbox() noexcept {
  const auto& cmd = CommandLine::inst();
  if(!cmd.mmoClientUsesServer() || wrld == nullptr)
    return;

  for(const auto& completion : Mmo::drainClientMmoCommandCompletions())
    mmoServerInventoryPresentation_.complete(completion);

  auto batch = Mmo::drainTypedServerPresentationMailbox();
  if(!batch.empty() || batch.rejectedRecords != 0U) {
    MmoServerPresentationBatchSink sink{*this};
    const auto stats = Mmo::ClientPresentation::consumeServerPresentationBatch(
        mmoTypedServerPresentation, batch, sink);
    if(stats.routesApplied != 0U || stats.bootstrapsApplied != 0U ||
       stats.eventsApplied != 0U || stats.stateRejected != 0U ||
       stats.sourceRejected != 0U) {
      Log::i("MMO typed presentation batch consumed",
             " routes=", stats.routesApplied,
             " bootstraps=", stats.bootstrapsApplied,
             " events=", stats.eventsApplied,
             " state_rejected=", stats.stateRejected,
             " source_rejected=", stats.sourceRejected,
             " bindings=", mmoServerEntityPresentation.size(),
             " interpolation_tracks=", mmoServerEntityInterpolator.size());
    }
  }
  sampleMmoServerEntityTransforms();
  sampleMmoServerProjectiles();
}

void GameSession::sampleMmoServerProjectiles() noexcept {
  mmoServerProjectilePresentation.sample(ticks, mmoServerProjectileSamples);
}
