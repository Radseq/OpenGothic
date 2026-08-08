#include "mmoserverentitypresentationregistry.h"

#include <algorithm>
#include <utility>

namespace Mmo::ClientPresentation {
namespace {

[[nodiscard]] constexpr std::uintptr_t localIdentityKey(
    const LocalNpcPresentationIdentity& local) noexcept {
  if(local.localObjectToken != 0U)
    return local.localObjectToken;
  return (static_cast<std::uintptr_t>(local.localNpcId) << 1U) | 1U;
}

} // namespace

ServerEntityPresentationRegistry::ServerEntityPresentationRegistry(
    const std::size_t maxBindings)
    : maxBindings_(std::max<std::size_t>(1U, maxBindings)) {
  entries_.reserve(maxBindings_);
  localToEntity_.reserve(maxBindings_);
}

std::vector<ServerEntityPresentationBinding>
ServerEntityPresentationRegistry::resetRoute(
    const std::uint64_t worldGeneration) {
  auto released = releaseAll();
  worldGeneration_ = worldGeneration;
  worldInstanceId_.clear();
  return released;
}

bool ServerEntityPresentationRegistry::acceptRoute(
    const ServerPresentationRouteView& route) {
  if(!route.valid() || route.worldGeneration != worldGeneration_)
    return false;
  if(worldInstanceId_.empty()) {
    worldInstanceId_.assign(route.worldInstanceId);
    return true;
  }
  return worldInstanceId_ == route.worldInstanceId;
}

bool ServerEntityPresentationRegistry::routeMatches(
    const ServerPresentationRouteView& route) const noexcept {
  return route.valid() && route.worldGeneration == worldGeneration_ &&
         !worldInstanceId_.empty() && worldInstanceId_ == route.worldInstanceId;
}

ServerEntityObservationResult ServerEntityPresentationRegistry::observe(
    const ServerEntityTransformObservation& transform) {
  if(!transform.valid())
    return {ServerEntityObservationStatus::Invalid};
  if(!acceptRoute(transform.route))
    return {ServerEntityObservationStatus::RouteMismatch};

  auto found = entries_.find(transform.handle.id);
  if(!transform.active) {
    if(found == entries_.end())
      return {ServerEntityObservationStatus::IgnoredInactive};

    const auto& current = found->second;
    if(transform.handle.generation < current.handle.generation ||
       (transform.handle == current.handle &&
        transform.serverTick < current.lastServerTick)) {
      return {ServerEntityObservationStatus::Stale};
    }
    if(transform.handle.generation == current.handle.generation &&
       (transform.kind != current.kind ||
        transform.stableEntityKey != current.stableEntityKey)) {
      return {ServerEntityObservationStatus::IdentityMismatch};
    }

    auto released = std::move(found->second);
    localToEntity_.erase(localIdentityKey(released.local));
    entries_.erase(found);
    return {ServerEntityObservationStatus::Removed, std::move(released)};
  }

  if(found == entries_.end())
    return {ServerEntityObservationStatus::NeedsLocalBinding};

  const auto& current = found->second;
  if(transform.handle.generation < current.handle.generation ||
     (transform.handle == current.handle &&
      transform.serverTick < current.lastServerTick)) {
    return {ServerEntityObservationStatus::Stale};
  }
  if(transform.handle == current.handle &&
     (transform.kind != current.kind ||
      transform.stableEntityKey != current.stableEntityKey)) {
    return {ServerEntityObservationStatus::IdentityMismatch};
  }
  if(transform.handle.generation > current.handle.generation) {
    auto released = std::move(found->second);
    localToEntity_.erase(localIdentityKey(released.local));
    entries_.erase(found);
    return {ServerEntityObservationStatus::NeedsLocalBinding,
            std::move(released)};
  }
  return {ServerEntityObservationStatus::ExistingBinding};
}

bool ServerEntityPresentationRegistry::bind(
    const ServerEntityTransformObservation& transform,
    const LocalNpcPresentationIdentity local) {
  if(!transform.valid() || !transform.active || !local.valid() ||
     !acceptRoute(transform.route)) {
    return false;
  }

  const auto localKey = localIdentityKey(local);
  const auto localBinding = localToEntity_.find(localKey);
  if(localBinding != localToEntity_.end() &&
     localBinding->second != transform.handle.id) {
    return false;
  }

  const auto found = entries_.find(transform.handle.id);
  if(found != entries_.end()) {
    auto& current = found->second;
    if(current.handle != transform.handle || current.kind != transform.kind ||
       current.worldGeneration != transform.route.worldGeneration ||
       current.worldInstanceId != transform.route.worldInstanceId ||
       current.stableEntityKey != transform.stableEntityKey ||
       transform.serverTick < current.lastServerTick) {
      return false;
    }
    if(current.local != local)
      return false;
    current.lastServerTick = transform.serverTick;
    localToEntity_.insert_or_assign(localKey, transform.handle.id);
    return true;
  }
  if(entries_.size() >= maxBindings_)
    return false;

  ServerEntityPresentationBinding binding;
  binding.handle = transform.handle;
  binding.kind = transform.kind;
  binding.worldGeneration = transform.route.worldGeneration;
  binding.lastServerTick = transform.serverTick;
  binding.local = local;
  binding.worldInstanceId.assign(transform.route.worldInstanceId);
  binding.stableEntityKey.assign(transform.stableEntityKey);
  entries_.emplace(transform.handle.id, std::move(binding));
  localToEntity_.emplace(localKey, transform.handle.id);
  return true;
}

const ServerEntityPresentationBinding* ServerEntityPresentationRegistry::find(
    const ServerEntityHandle handle,
    const std::uint64_t worldGeneration) const noexcept {
  const auto found = entries_.find(handle.id);
  if(found == entries_.end())
    return nullptr;
  const auto& binding = found->second;
  return binding.handle == handle && binding.worldGeneration == worldGeneration
             ? &binding
             : nullptr;
}

const ServerEntityPresentationBinding*
ServerEntityPresentationRegistry::findLocal(
    const std::uintptr_t localObjectToken) const noexcept {
  if(localObjectToken == 0U)
    return nullptr;
  const auto local = localToEntity_.find(localObjectToken);
  if(local == localToEntity_.end())
    return nullptr;
  const auto entity = entries_.find(local->second);
  if(entity == entries_.end() ||
     entity->second.local.localObjectToken != localObjectToken) {
    return nullptr;
  }
  return &entity->second;
}

const ServerEntityPresentationBinding*
ServerEntityPresentationRegistry::findLocal(
    const LocalNpcPresentationIdentity& local) const noexcept {
  if(local.localObjectToken != 0U) {
    if(const auto* binding = findLocal(local.localObjectToken);
       binding != nullptr) {
      return binding;
    }
  }
  if(!local.valid())
    return nullptr;

  for(const auto& [unused, binding] : entries_) {
    static_cast<void>(unused);
    if(binding.local.localNpcId == local.localNpcId &&
       binding.local.persistentId == local.persistentId &&
       binding.local.instanceSymbol == local.instanceSymbol) {
      return &binding;
    }
  }
  return nullptr;
}

void ServerEntityPresentationRegistry::touch(
    const ServerEntityTransformObservation& transform) noexcept {
  if(!routeMatches(transform.route))
    return;
  auto found = entries_.find(transform.handle.id);
  if(found == entries_.end())
    return;
  auto& binding = found->second;
  if(binding.handle != transform.handle || binding.kind != transform.kind ||
     binding.stableEntityKey != transform.stableEntityKey ||
     transform.serverTick < binding.lastServerTick) {
    return;
  }
  binding.lastServerTick = transform.serverTick;
}

std::optional<ServerEntityPresentationBinding>
ServerEntityPresentationRegistry::invalidate(
    const ServerEntityHandle handle,
    const std::uint64_t worldGeneration) noexcept {
  auto found = entries_.find(handle.id);
  if(found == entries_.end() || found->second.handle != handle ||
     found->second.worldGeneration != worldGeneration) {
    return std::nullopt;
  }
  auto released = std::move(found->second);
  localToEntity_.erase(localIdentityKey(released.local));
  entries_.erase(found);
  return released;
}

std::vector<ServerEntityPresentationBinding>
ServerEntityPresentationRegistry::releaseAll() {
  std::vector<ServerEntityPresentationBinding> released;
  released.reserve(entries_.size());
  for(auto& [id, binding] : entries_) {
    static_cast<void>(id);
    released.push_back(std::move(binding));
  }
  entries_.clear();
  localToEntity_.clear();
  return released;
}

void ServerEntityPresentationRegistry::clear() noexcept {
  entries_.clear();
  localToEntity_.clear();
  worldGeneration_ = 0;
  worldInstanceId_.clear();
}

std::uint64_t ServerEntityPresentationRegistry::worldGeneration() const noexcept {
  return worldGeneration_;
}

const std::string& ServerEntityPresentationRegistry::worldInstanceId() const noexcept {
  return worldInstanceId_;
}

std::size_t ServerEntityPresentationRegistry::size() const noexcept {
  return entries_.size();
}

} // namespace Mmo::ClientPresentation
