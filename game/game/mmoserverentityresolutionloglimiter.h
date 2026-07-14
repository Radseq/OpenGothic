#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>

#include "mmoserverentitypresentationtypes.h"

namespace Mmo::ClientPresentation {

struct ServerEntityResolutionLogLimiterConfig final {
  std::uint64_t repeatIntervalMs = 5'000U;
  std::size_t maxEntities = 256U;
};

class ServerEntityResolutionLogLimiter final {
 public:
  explicit ServerEntityResolutionLogLimiter(
      ServerEntityResolutionLogLimiterConfig config = {})
      : config_(config) {
    config_.maxEntities = std::max<std::size_t>(1U, config_.maxEntities);
    entries_.reserve(config_.maxEntities);
  }

  void resetRoute(const std::uint64_t worldGeneration) noexcept {
    entries_.clear();
    worldGeneration_ = worldGeneration;
  }

  [[nodiscard]] bool shouldLog(const ServerEntityHandle handle,
                               const std::uint64_t worldGeneration,
                               const std::uint64_t nowMs) noexcept {
    if(!handle.valid() || worldGeneration == 0U ||
       worldGeneration != worldGeneration_) {
      return false;
    }

    try {
      auto found = entries_.find(handle.id);
      if(found == entries_.end()) {
        if(entries_.size() >= config_.maxEntities)
          entries_.erase(entries_.begin());
        entries_.emplace(handle.id, Entry{
            .generation = handle.generation,
            .nextLogAtMs = nextLogAt(nowMs),
        });
        return true;
      }

      auto& entry = found->second;
      if(entry.generation != handle.generation) {
        entry.generation = handle.generation;
        entry.nextLogAtMs = nextLogAt(nowMs);
        return true;
      }
      if(nowMs < entry.nextLogAtMs)
        return false;

      entry.nextLogAtMs = nextLogAt(nowMs);
      return true;
    } catch(...) {
      // Diagnostics must never terminate the noexcept presentation path. If a
      // bookkeeping allocation fails, preserve the original error visibility.
      return true;
    }
  }

  void resolved(const ServerEntityHandle handle,
                const std::uint64_t worldGeneration) noexcept {
    if(worldGeneration != worldGeneration_)
      return;
    const auto found = entries_.find(handle.id);
    if(found != entries_.end() && found->second.generation == handle.generation)
      entries_.erase(found);
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return entries_.size();
  }

 private:
  struct Entry final {
    std::uint32_t generation = 0U;
    std::uint64_t nextLogAtMs = 0U;
  };

  [[nodiscard]] std::uint64_t nextLogAt(const std::uint64_t nowMs) const noexcept {
    constexpr auto Max = std::numeric_limits<std::uint64_t>::max();
    if(config_.repeatIntervalMs > Max - nowMs)
      return Max;
    return nowMs + config_.repeatIntervalMs;
  }

  ServerEntityResolutionLogLimiterConfig config_;
  std::uint64_t worldGeneration_ = 0U;
  std::unordered_map<std::uint64_t, Entry> entries_;
};

} // namespace Mmo::ClientPresentation
