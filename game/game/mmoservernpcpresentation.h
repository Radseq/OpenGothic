#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

#include "mmoserverpresentationevents.h"

namespace Mmo::ClientPresentation {

enum class ServerNpcPresentationLocomotion : std::uint8_t {
  Stand,
  Walk,
  Run,
  TurnLeft,
  TurnRight,
  Crouch,
  Knockback,
  Fall,
};

enum class ServerNpcPresentationActionKind : std::uint8_t {
  None,
  Kneel,
  Sit,
  Sleep,
  Work,
  UseObject,
  Conversation,
  Attack,
  Block,
  HitReaction,
  Death,
};

enum class ServerNpcPresentationActionPhase : std::uint8_t {
  Starting,
  Active,
  Recovering,
  Completed,
};

struct ServerNpcPresentationAction final {
  std::uint64_t actionId = 0U;
  ServerNpcPresentationActionKind kind =
      ServerNpcPresentationActionKind::None;
  ServerNpcPresentationActionPhase phase =
      ServerNpcPresentationActionPhase::Active;
  std::uint64_t startedAtServerTick = 0U;
  std::uint64_t actionRevision = 0U;
  ServerPresentationEntityHandle target{};

  [[nodiscard]] constexpr bool valid(
      const ServerWorldInstanceHandle world) const noexcept {
    if(kind == ServerNpcPresentationActionKind::None)
      return actionId == 0U && startedAtServerTick == 0U &&
             actionRevision == 0U && target.empty();
    return actionId != 0U && startedAtServerTick != 0U &&
           actionRevision != 0U &&
           (target.empty() || target.world == world);
  }
};

struct ServerNpcCompletePresentationSnapshot final {
  ServerPresentationRouteIdentity route{};
  ServerPresentationEntityRecord entity{};
  ServerPresentationNpcStateRecord state{};
  ServerNpcPresentationLocomotion locomotion =
      ServerNpcPresentationLocomotion::Stand;
  ServerNpcPresentationAction action{};
  std::uint64_t presentationRevision = 0U;

  [[nodiscard]] bool valid() const noexcept {
    return route.valid() && entity.valid() &&
           entity.kind == ServerPresentationEntityKind::Npc &&
           entity.handle.world == route.world && state.valid() &&
           state.entity == entity.handle && action.valid(route.world) &&
           presentationRevision != 0U;
  }
};

enum class ServerNpcAnimationIntent : std::uint8_t {
  Idle,
  Walk,
  Run,
  TurnLeft,
  TurnRight,
  Crouch,
  Kneel,
  Sit,
  Sleep,
  Work,
  UseObject,
  Conversation,
  Attack,
  Block,
  HitReaction,
  Death,
  Knockback,
  Fall,
};

[[nodiscard]] constexpr ServerNpcAnimationIntent mapServerNpcAnimationIntent(
    const ServerNpcCompletePresentationSnapshot& snapshot) noexcept {
  using Action = ServerNpcPresentationActionKind;
  switch(snapshot.action.kind) {
    case Action::Kneel: return ServerNpcAnimationIntent::Kneel;
    case Action::Sit: return ServerNpcAnimationIntent::Sit;
    case Action::Sleep: return ServerNpcAnimationIntent::Sleep;
    case Action::Work: return ServerNpcAnimationIntent::Work;
    case Action::UseObject: return ServerNpcAnimationIntent::UseObject;
    case Action::Conversation: return ServerNpcAnimationIntent::Conversation;
    case Action::Attack: return ServerNpcAnimationIntent::Attack;
    case Action::Block: return ServerNpcAnimationIntent::Block;
    case Action::HitReaction: return ServerNpcAnimationIntent::HitReaction;
    case Action::Death: return ServerNpcAnimationIntent::Death;
    case Action::None: break;
  }

  using Locomotion = ServerNpcPresentationLocomotion;
  switch(snapshot.locomotion) {
    case Locomotion::Stand: return ServerNpcAnimationIntent::Idle;
    case Locomotion::Walk: return ServerNpcAnimationIntent::Walk;
    case Locomotion::Run: return ServerNpcAnimationIntent::Run;
    case Locomotion::TurnLeft: return ServerNpcAnimationIntent::TurnLeft;
    case Locomotion::TurnRight: return ServerNpcAnimationIntent::TurnRight;
    case Locomotion::Crouch: return ServerNpcAnimationIntent::Crouch;
    case Locomotion::Knockback: return ServerNpcAnimationIntent::Knockback;
    case Locomotion::Fall: return ServerNpcAnimationIntent::Fall;
  }
  return ServerNpcAnimationIntent::Idle;
}

enum class ServerNpcPresentationObserveStatus : std::uint8_t {
  Applied,
  Duplicate,
  Stale,
  ReplacedGeneration,
  RouteMismatch,
  Invalid,
};

struct ServerNpcPresentationObserveResult final {
  ServerNpcPresentationObserveStatus status =
      ServerNpcPresentationObserveStatus::Invalid;
  std::optional<ServerPresentationEntityHandle> released;

  explicit ServerNpcPresentationObserveResult(
      const ServerNpcPresentationObserveStatus value,
      std::optional<ServerPresentationEntityHandle> old = std::nullopt) noexcept
      : status(value), released(std::move(old)) {}
};

enum class ServerNpcLocalAnimationCompletionStatus : std::uint8_t {
  IgnoredServerOwnedAction,
  Missing,
  IdentityMismatch,
};

// Authority-neutral projection of IC-04/IC-08. The track never derives a
// gameplay action from a local animation callback and never advances the
// authoritative action phase. It only retains the latest complete snapshot.
class ServerNpcPresentationTrack final {
  public:
    [[nodiscard]] bool replaceRoute(
        const ServerPresentationRouteIdentity route) noexcept {
      if(!route.valid())
        return false;
      if(route_ == route)
        return true;
      route_ = route;
      snapshot_.reset();
      return true;
    }

    [[nodiscard]] ServerNpcPresentationObserveResult observe(
        ServerNpcCompletePresentationSnapshot snapshot) noexcept {
      if(!snapshot.valid())
        return ServerNpcPresentationObserveResult{ServerNpcPresentationObserveStatus::Invalid};
      if(snapshot.route != route_)
        return ServerNpcPresentationObserveResult{
            ServerNpcPresentationObserveStatus::RouteMismatch};

      std::optional<ServerPresentationEntityHandle> released;
      if(snapshot_.has_value()) {
        const auto& current = *snapshot_;
        if(snapshot.entity.handle.id != current.entity.handle.id)
          return ServerNpcPresentationObserveResult{ServerNpcPresentationObserveStatus::Invalid};
        if(snapshot.entity.handle.generation <
           current.entity.handle.generation) {
          return ServerNpcPresentationObserveResult{ServerNpcPresentationObserveStatus::Stale};
        }
        if(snapshot.entity.handle.generation ==
           current.entity.handle.generation) {
          if(snapshot.presentationRevision < current.presentationRevision)
            return ServerNpcPresentationObserveResult{ServerNpcPresentationObserveStatus::Stale};
          if(snapshot.presentationRevision == current.presentationRevision)
            return ServerNpcPresentationObserveResult{
                ServerNpcPresentationObserveStatus::Duplicate};
        } else {
          released = current.entity.handle;
        }
      }

      snapshot_ = std::move(snapshot);
      return ServerNpcPresentationObserveResult{
          released.has_value()
              ? ServerNpcPresentationObserveStatus::ReplacedGeneration
              : ServerNpcPresentationObserveStatus::Applied,
          std::move(released)};
    }

    [[nodiscard]] bool despawn(
        const ServerPresentationEntityHandle entity) noexcept {
      if(!snapshot_.has_value() || snapshot_->entity.handle != entity)
        return false;
      snapshot_.reset();
      return true;
    }

    [[nodiscard]] ServerNpcLocalAnimationCompletionStatus
    localAnimationCompleted(
        const ServerPresentationEntityHandle entity,
        const std::uint64_t actionId) const noexcept {
      if(!snapshot_.has_value())
        return ServerNpcLocalAnimationCompletionStatus::Missing;
      if(snapshot_->entity.handle != entity ||
         snapshot_->action.actionId != actionId) {
        return ServerNpcLocalAnimationCompletionStatus::IdentityMismatch;
      }
      return ServerNpcLocalAnimationCompletionStatus::IgnoredServerOwnedAction;
    }

    [[nodiscard]] const ServerNpcCompletePresentationSnapshot* current()
        const noexcept {
      return snapshot_ ? &*snapshot_ : nullptr;
    }

  private:
    ServerPresentationRouteIdentity route_{};
    std::optional<ServerNpcCompletePresentationSnapshot> snapshot_;
};

enum class ServerNpcSpawnGateStatus : std::uint8_t {
  Staged,
  Updated,
  ReplacedGeneration,
  Duplicate,
  Stale,
  CapacityExceeded,
  Invalid,
};

struct ServerNpcSpawnGateResult final {
  ServerNpcSpawnGateStatus status = ServerNpcSpawnGateStatus::Invalid;
  std::optional<ServerPresentationEntityRecord> released;

  explicit ServerNpcSpawnGateResult(
      const ServerNpcSpawnGateStatus value,
      std::optional<ServerPresentationEntityRecord> old = std::nullopt) noexcept
      : status(value), released(std::move(old)) {}
};

// Prevents a server-owned NPC from becoming visible before its complete
// authoritative state is available. Transform updates may refresh a staged
// record, but only an exact-generation NPC state opens the gate.
class ServerNpcSpawnGate final {
  public:
    explicit ServerNpcSpawnGate(const std::size_t capacity = 4096U)
        : capacity_(capacity) {
      pending_.reserve(capacity_);
    }

    [[nodiscard]] ServerNpcSpawnGateResult stage(
        ServerPresentationEntityRecord entity) {
      if(!entity.valid() || entity.kind != ServerPresentationEntityKind::Npc)
        return ServerNpcSpawnGateResult{ServerNpcSpawnGateStatus::Invalid};

      auto found = pending_.find(entity.handle.id);
      if(found == pending_.end()) {
        if(pending_.size() >= capacity_)
          return ServerNpcSpawnGateResult{ServerNpcSpawnGateStatus::CapacityExceeded};
        pending_.emplace(entity.handle.id, std::move(entity));
        return ServerNpcSpawnGateResult{ServerNpcSpawnGateStatus::Staged};
      }

      auto& current = found->second;
      if(entity.handle.generation < current.handle.generation)
        return ServerNpcSpawnGateResult{ServerNpcSpawnGateStatus::Stale};
      if(entity.handle.generation == current.handle.generation) {
        if(entity.entityRevision < current.entityRevision)
          return ServerNpcSpawnGateResult{ServerNpcSpawnGateStatus::Stale};
        if(entity.entityRevision == current.entityRevision)
          return ServerNpcSpawnGateResult{ServerNpcSpawnGateStatus::Duplicate};
        current = std::move(entity);
        return ServerNpcSpawnGateResult{ServerNpcSpawnGateStatus::Updated};
      }

      auto released = current;
      current = std::move(entity);
      return ServerNpcSpawnGateResult{
          ServerNpcSpawnGateStatus::ReplacedGeneration, std::move(released)};
    }

    [[nodiscard]] std::optional<ServerPresentationEntityRecord> takeReady(
        const ServerPresentationNpcStateRecord& state) {
      if(!state.valid())
        return std::nullopt;
      const auto found = pending_.find(state.entity.id);
      if(found == pending_.end() || found->second.handle != state.entity)
        return std::nullopt;
      auto ready = std::move(found->second);
      pending_.erase(found);
      return ready;
    }

    [[nodiscard]] bool erase(
        const ServerPresentationEntityHandle entity) noexcept {
      const auto found = pending_.find(entity.id);
      if(found == pending_.end() || found->second.handle != entity)
        return false;
      pending_.erase(found);
      return true;
    }

    [[nodiscard]] bool contains(
        const ServerPresentationEntityHandle entity) const noexcept {
      const auto found = pending_.find(entity.id);
      return found != pending_.end() && found->second.handle == entity;
    }

    void clear() noexcept { pending_.clear(); }
    [[nodiscard]] std::size_t size() const noexcept { return pending_.size(); }

  private:
    std::size_t capacity_ = 4096U;
    std::unordered_map<std::uint64_t, ServerPresentationEntityRecord> pending_;
};

} // namespace Mmo::ClientPresentation
