#include "mmoserverentitypresentationregistry.h"

#include <utility>

namespace Mmo::ClientPresentation {

ServerEntityObservationStatus ServerEntityPresentationRegistry::observe(
    const Net::ServerEntityTransformDeltaPacket& transform) {
  auto found = entries_.find(transform.entityId);
  const bool active =
      (transform.flags & Net::ServerEntityTransformActive) != 0U;

  if(!active) {
    if(found == entries_.end())
      return ServerEntityObservationStatus::IgnoredInactive;
    if(transform.generation < found->second.generation ||
       (transform.generation == found->second.generation &&
        transform.serverTick < found->second.lastServerTick)) {
      return ServerEntityObservationStatus::Stale;
    }
    if(transform.generation != found->second.generation)
      return ServerEntityObservationStatus::IgnoredInactive;
    if(transform.stableEntityKey != found->second.stableEntityKey)
      return ServerEntityObservationStatus::IdentityMismatch;
    entries_.erase(found);
    return ServerEntityObservationStatus::Removed;
  }

  if(found == entries_.end())
    return ServerEntityObservationStatus::NeedsLocalBinding;

  const auto& binding = found->second;
  if(transform.generation < binding.generation ||
     (transform.generation == binding.generation &&
      transform.serverTick < binding.lastServerTick)) {
    return ServerEntityObservationStatus::Stale;
  }
  if(transform.generation == binding.generation &&
     transform.stableEntityKey != binding.stableEntityKey) {
    return ServerEntityObservationStatus::IdentityMismatch;
  }
  if(transform.generation > binding.generation) {
    entries_.erase(found);
    return ServerEntityObservationStatus::NeedsLocalBinding;
  }
  return ServerEntityObservationStatus::ExistingBinding;
}

bool ServerEntityPresentationRegistry::bind(
    const Net::ServerEntityTransformDeltaPacket& transform,
    LocalNpcPresentationIdentity local) {
  if(transform.entityId == 0 || transform.generation == 0 ||
     transform.stableEntityKey.empty() || !local.valid() ||
     (transform.flags & Net::ServerEntityTransformActive) == 0U) {
    return false;
  }

  const auto found = entries_.find(transform.entityId);
  if(found != entries_.end()) {
    const auto& current = found->second;
    if(transform.generation < current.generation ||
       (transform.generation == current.generation &&
        (transform.serverTick < current.lastServerTick ||
         transform.stableEntityKey != current.stableEntityKey))) {
      return false;
    }
  }

  ServerEntityPresentationBinding binding;
  binding.generation = transform.generation;
  binding.lastServerTick = transform.serverTick;
  binding.local = local;
  binding.stableEntityKey = transform.stableEntityKey;
  entries_.insert_or_assign(transform.entityId, std::move(binding));
  return true;
}

const ServerEntityPresentationBinding* ServerEntityPresentationRegistry::find(
    std::uint64_t entityId) const noexcept {
  const auto found = entries_.find(entityId);
  return found == entries_.end() ? nullptr : &found->second;
}

void ServerEntityPresentationRegistry::touch(
    const Net::ServerEntityTransformDeltaPacket& transform) noexcept {
  auto found = entries_.find(transform.entityId);
  if(found == entries_.end())
    return;
  auto& binding = found->second;
  if(binding.generation != transform.generation ||
     binding.stableEntityKey != transform.stableEntityKey ||
     transform.serverTick < binding.lastServerTick) {
    return;
  }
  binding.lastServerTick = transform.serverTick;
}

void ServerEntityPresentationRegistry::invalidate(
    std::uint64_t entityId) noexcept {
  entries_.erase(entityId);
}

void ServerEntityPresentationRegistry::clear() noexcept {
  entries_.clear();
}

std::size_t ServerEntityPresentationRegistry::size() const noexcept {
  return entries_.size();
}

} // namespace Mmo::ClientPresentation
