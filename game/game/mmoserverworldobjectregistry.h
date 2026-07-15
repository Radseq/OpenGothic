#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mmoserverpresentationevents.h"

namespace Mmo::ClientPresentation {

struct ServerWorldObjectId final {
  std::uint64_t value = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
  [[nodiscard]] friend constexpr bool operator==(
      const ServerWorldObjectId&,
      const ServerWorldObjectId&) noexcept = default;
};

struct LocalVobToken final {
  std::uint32_t vobObjectId = std::numeric_limits<std::uint32_t>::max();
  ServerPresentationWorldObjectKind kind =
      ServerPresentationWorldObjectKind::Item;

  [[nodiscard]] constexpr bool valid() const noexcept {
    const bool knownKind = kind == ServerPresentationWorldObjectKind::Item ||
                           kind == ServerPresentationWorldObjectKind::Interactive ||
                           kind == ServerPresentationWorldObjectKind::Mover ||
                           kind == ServerPresentationWorldObjectKind::Container ||
                           kind == ServerPresentationWorldObjectKind::Trigger;
    return vobObjectId != std::numeric_limits<std::uint32_t>::max() &&
           knownKind;
  }
  [[nodiscard]] friend constexpr bool operator==(
      const LocalVobToken&,
      const LocalVobToken&) noexcept = default;
};

enum class ServerWorldObjectRegisterStatus : std::uint8_t {
  Registered,
  Duplicate,
  Invalid,
  Conflict,
  CapacityExceeded,
};

enum class ServerWorldObjectBindStatus : std::uint8_t {
  Bound,
  Duplicate,
  ReplacedGeneration,
  Invalid,
  RouteMismatch,
  MissingLocal,
  KindMismatch,
  StaleGeneration,
  Conflict,
  CapacityExceeded,
};

enum class ServerWorldObjectStateStatus : std::uint8_t {
  Apply,
  Unchanged,
  Duplicate,
  Stale,
  MissingBinding,
};

struct ServerWorldObjectBindResult final {
  constexpr ServerWorldObjectBindResult(
      const ServerWorldObjectBindStatus status =
          ServerWorldObjectBindStatus::Invalid,
      std::optional<ServerPresentationEntityHandle> released = std::nullopt)
      : status(status), released(std::move(released)) {}

  ServerWorldObjectBindStatus status = ServerWorldObjectBindStatus::Invalid;
  std::optional<ServerPresentationEntityHandle> released;

  [[nodiscard]] constexpr bool bound() const noexcept {
    return status == ServerWorldObjectBindStatus::Bound ||
           status == ServerWorldObjectBindStatus::Duplicate ||
           status == ServerWorldObjectBindStatus::ReplacedGeneration;
  }
};

struct ServerWorldObjectRegistryConfig final {
  std::size_t maxLocalObjects = 32'768U;
  std::size_t maxRuntimeBindings = 32'768U;
  std::size_t maxUnresolvedLogsPerRoute = 128U;
};

class ServerWorldObjectRegistry final {
 public:
  explicit ServerWorldObjectRegistry(
      ServerWorldObjectRegistryConfig config = {})
      : config_(config) {
    config_.maxLocalObjects =
        std::max<std::size_t>(1U, config_.maxLocalObjects);
    config_.maxRuntimeBindings =
        std::max<std::size_t>(1U, config_.maxRuntimeBindings);
    localByWorldObject_.reserve(config_.maxLocalObjects);
    worldObjectByLocal_.reserve(config_.maxLocalObjects);
    runtimeByEntity_.reserve(config_.maxRuntimeBindings);
    entityByWorldObject_.reserve(config_.maxRuntimeBindings);
    entityByLocal_.reserve(config_.maxRuntimeBindings);
    unresolved_.reserve(config_.maxUnresolvedLogsPerRoute);
  }

  [[nodiscard]] ServerWorldObjectRegisterStatus registerLocal(
      const ServerWorldObjectId worldObject,
      const LocalVobToken local) {
    if(!worldObject.valid() || !local.valid())
      return ServerWorldObjectRegisterStatus::Invalid;

    const auto localKey = keyOf(local);
    if(const auto existing = localByWorldObject_.find(worldObject.value);
       existing != localByWorldObject_.end()) {
      return existing->second == local
                 ? ServerWorldObjectRegisterStatus::Duplicate
                 : ServerWorldObjectRegisterStatus::Conflict;
    }
    if(const auto existing = worldObjectByLocal_.find(localKey);
       existing != worldObjectByLocal_.end()) {
      return existing->second == worldObject.value
                 ? ServerWorldObjectRegisterStatus::Duplicate
                 : ServerWorldObjectRegisterStatus::Conflict;
    }
    if(localByWorldObject_.size() >= config_.maxLocalObjects)
      return ServerWorldObjectRegisterStatus::CapacityExceeded;

    localByWorldObject_.emplace(worldObject.value, local);
    worldObjectByLocal_.emplace(localKey, worldObject.value);
    return ServerWorldObjectRegisterStatus::Registered;
  }

  [[nodiscard]] ServerWorldObjectBindResult bindRuntime(
      const ServerPresentationEntityHandle entity,
      const ServerWorldObjectId worldObject,
      const ServerPresentationWorldObjectKind kind) {
    if(!entity.valid() || !worldObject.valid() || !activeWorld_.valid() ||
       !knownKind(kind)) {
      return {ServerWorldObjectBindStatus::Invalid};
    }
    if(entity.world != activeWorld_)
      return {ServerWorldObjectBindStatus::RouteMismatch};

    const auto local = localByWorldObject_.find(worldObject.value);
    if(local == localByWorldObject_.end())
      return {ServerWorldObjectBindStatus::MissingLocal};
    if(local->second.kind != kind)
      return {ServerWorldObjectBindStatus::KindMismatch};

    const auto existing = runtimeByEntity_.find(entity.id);
    if(existing != runtimeByEntity_.end()) {
      if(entity.generation < existing->second.entity.generation)
        return {ServerWorldObjectBindStatus::StaleGeneration};
      if(entity.generation == existing->second.entity.generation) {
        return existing->second.entity == entity &&
                       existing->second.worldObject == worldObject &&
                       existing->second.local == local->second
                   ? ServerWorldObjectBindResult{
                         ServerWorldObjectBindStatus::Duplicate}
                   : ServerWorldObjectBindResult{
                         ServerWorldObjectBindStatus::Conflict};
      }
    }

    if(const auto owner = entityByWorldObject_.find(worldObject.value);
       owner != entityByWorldObject_.end() && owner->second != entity.id) {
      return {ServerWorldObjectBindStatus::Conflict};
    }
    const auto localKey = keyOf(local->second);
    if(const auto owner = entityByLocal_.find(localKey);
       owner != entityByLocal_.end() && owner->second != entity.id) {
      return {ServerWorldObjectBindStatus::Conflict};
    }
    if(existing == runtimeByEntity_.end() &&
       runtimeByEntity_.size() >= config_.maxRuntimeBindings) {
      return {ServerWorldObjectBindStatus::CapacityExceeded};
    }

    std::optional<ServerPresentationEntityHandle> released;
    auto status = ServerWorldObjectBindStatus::Bound;
    if(existing != runtimeByEntity_.end()) {
      released = existing->second.entity;
      eraseReverse(existing->second);
      status = ServerWorldObjectBindStatus::ReplacedGeneration;
    }

    Binding binding{
        .entity = entity,
        .worldObject = worldObject,
        .local = local->second,
    };
    runtimeByEntity_.insert_or_assign(entity.id, binding);
    entityByWorldObject_.insert_or_assign(worldObject.value, entity.id);
    entityByLocal_.insert_or_assign(localKey, entity.id);
    return {status, released};
  }

  [[nodiscard]] const LocalVobToken* find(
      const ServerPresentationEntityHandle entity) const noexcept {
    const auto found = runtimeByEntity_.find(entity.id);
    return found != runtimeByEntity_.end() && found->second.entity == entity
               ? &found->second.local
               : nullptr;
  }

  [[nodiscard]] std::optional<ServerPresentationEntityHandle> find(
      const LocalVobToken local) const noexcept {
    if(!local.valid())
      return std::nullopt;
    const auto reverse = entityByLocal_.find(keyOf(local));
    if(reverse == entityByLocal_.end())
      return std::nullopt;
    const auto binding = runtimeByEntity_.find(reverse->second);
    return binding != runtimeByEntity_.end()
               ? std::optional{binding->second.entity}
               : std::nullopt;
  }

  [[nodiscard]] std::optional<ServerWorldObjectId> worldObject(
      const ServerPresentationEntityHandle entity) const noexcept {
    const auto found = runtimeByEntity_.find(entity.id);
    return found != runtimeByEntity_.end() && found->second.entity == entity
               ? std::optional{found->second.worldObject}
               : std::nullopt;
  }

  [[nodiscard]] ServerWorldObjectStateStatus inspectState(
      const ServerPresentationEntityHandle entity,
      const std::uint64_t revision,
      const std::uint64_t signature) const noexcept {
    const auto found = runtimeByEntity_.find(entity.id);
    if(found == runtimeByEntity_.end() || found->second.entity != entity)
      return ServerWorldObjectStateStatus::MissingBinding;
    if(revision < found->second.appliedRevision)
      return ServerWorldObjectStateStatus::Stale;
    if(revision == found->second.appliedRevision)
      return ServerWorldObjectStateStatus::Duplicate;
    if(found->second.hasAppliedState &&
       signature == found->second.appliedSignature) {
      return ServerWorldObjectStateStatus::Unchanged;
    }
    return ServerWorldObjectStateStatus::Apply;
  }

  void commitState(const ServerPresentationEntityHandle entity,
                   const std::uint64_t revision,
                   const std::uint64_t signature) noexcept {
    const auto found = runtimeByEntity_.find(entity.id);
    if(found == runtimeByEntity_.end() || found->second.entity != entity ||
       revision < found->second.appliedRevision) {
      return;
    }
    found->second.appliedRevision = revision;
    found->second.appliedSignature = signature;
    found->second.hasAppliedState = true;
  }

  [[nodiscard]] bool shouldLogUnresolved(
      const ServerPresentationEntityHandle entity,
      const std::uint64_t revision) noexcept {
    if(!entity.valid() || revision == 0U || !activeWorld_.valid() ||
       entity.world != activeWorld_ ||
       unresolvedLogCount_ >= config_.maxUnresolvedLogsPerRoute) {
      return false;
    }
    const auto found = std::find_if(
        unresolved_.begin(), unresolved_.end(),
        [&](const UnresolvedRecord& record) {
          return record.entity == entity && record.revision == revision;
        });
    if(found != unresolved_.end())
      return false;

    unresolved_.push_back(
        UnresolvedRecord{.entity = entity, .revision = revision});
    ++unresolvedLogCount_;
    return true;
  }

  void unbind(const ServerPresentationEntityHandle entity) noexcept {
    const auto found = runtimeByEntity_.find(entity.id);
    if(found == runtimeByEntity_.end() || found->second.entity != entity)
      return;
    eraseReverse(found->second);
    runtimeByEntity_.erase(found);
  }

  void resetRoute(const ServerWorldInstanceHandle world) noexcept {
    runtimeByEntity_.clear();
    entityByWorldObject_.clear();
    entityByLocal_.clear();
    unresolved_.clear();
    unresolvedLogCount_ = 0U;
    activeWorld_ = world;
  }

  void resetLocalCatalog() noexcept {
    resetRoute({});
    localByWorldObject_.clear();
    worldObjectByLocal_.clear();
  }

  [[nodiscard]] std::size_t localCount() const noexcept {
    return localByWorldObject_.size();
  }
  [[nodiscard]] std::size_t boundCount() const noexcept {
    return runtimeByEntity_.size();
  }
  [[nodiscard]] std::size_t unresolvedLogCount() const noexcept {
    return unresolvedLogCount_;
  }

 private:
  struct Binding final {
    ServerPresentationEntityHandle entity{};
    ServerWorldObjectId worldObject{};
    LocalVobToken local{};
    std::uint64_t appliedRevision = 0U;
    std::uint64_t appliedSignature = 0U;
    bool hasAppliedState = false;
  };

  struct UnresolvedRecord final {
    ServerPresentationEntityHandle entity{};
    std::uint64_t revision = 0U;
  };

  [[nodiscard]] static constexpr bool knownKind(
      const ServerPresentationWorldObjectKind kind) noexcept {
    return kind == ServerPresentationWorldObjectKind::Item ||
           kind == ServerPresentationWorldObjectKind::Interactive ||
           kind == ServerPresentationWorldObjectKind::Mover ||
           kind == ServerPresentationWorldObjectKind::Container ||
           kind == ServerPresentationWorldObjectKind::Trigger;
  }

  [[nodiscard]] static constexpr std::uint64_t keyOf(
      const LocalVobToken local) noexcept {
    return (static_cast<std::uint64_t>(local.kind) << 32U) |
           static_cast<std::uint64_t>(local.vobObjectId);
  }

  void eraseReverse(const Binding& binding) noexcept {
    entityByWorldObject_.erase(binding.worldObject.value);
    entityByLocal_.erase(keyOf(binding.local));
  }

  ServerWorldObjectRegistryConfig config_{};
  ServerWorldInstanceHandle activeWorld_{};
  std::unordered_map<std::uint64_t, LocalVobToken> localByWorldObject_;
  std::unordered_map<std::uint64_t, std::uint64_t> worldObjectByLocal_;
  std::unordered_map<std::uint64_t, Binding> runtimeByEntity_;
  std::unordered_map<std::uint64_t, std::uint64_t> entityByWorldObject_;
  std::unordered_map<std::uint64_t, std::uint64_t> entityByLocal_;
  std::vector<UnresolvedRecord> unresolved_;
  std::size_t unresolvedLogCount_ = 0U;
};

} // namespace Mmo::ClientPresentation
