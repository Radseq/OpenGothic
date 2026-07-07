#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mmo_server_gameplay_authority.h"
#include "mmo_server_perception_authority.h"

namespace Mmo::Server::Perception {

inline constexpr std::size_t DefaultQueueCapacity = 512;
inline constexpr std::uint64_t DefaultCoalesceWindowMs = 750;

struct EventInput final {
  std::uint8_t perceptionId = 0;
  std::string_view sourceKey;
  std::string_view otherKey;
  std::string_view victimKey;
  std::string_view itemKey;
  std::string_view reason;
  std::optional<Gameplay::Vec3> originPosition;
  std::uint64_t serverTickMs = 0;
};

struct Event final {
  std::uint64_t sequence = 0;
  std::uint8_t perceptionId = 0;
  PerceptionDef def;
  std::string sourceKey;
  std::string otherKey;
  std::string victimKey;
  std::string itemKey;
  std::string reason;
  std::optional<Gameplay::Vec3> originPosition;
  std::uint64_t firstServerTickMs = 0;
  std::uint64_t lastServerTickMs = 0;
  std::uint32_t repeats = 1;
};

struct EnqueueResult final {
  bool accepted = true;
  bool coalesced = false;
  const char* reason = "queued";
  std::uint64_t sequence = 0;
};

class Queue final {
public:
  explicit Queue(std::size_t capacity = DefaultQueueCapacity,
                 std::uint64_t coalesceWindowMs = DefaultCoalesceWindowMs);

  [[nodiscard]] EnqueueResult enqueue(const EventInput& input);
  [[nodiscard]] std::vector<Event> snapshot() const;
  [[nodiscard]] std::optional<Event> latest() const;
  [[nodiscard]] std::size_t size() const noexcept;

  void expireBefore(std::uint64_t serverTickMs);
  void clear();

private:
  [[nodiscard]] bool matchesRecent(const Event& event, const EventInput& input) const noexcept;
  void trimToCapacity();

  std::deque<Event> events_;
  std::size_t capacity_ = DefaultQueueCapacity;
  std::uint64_t coalesceWindowMs_ = DefaultCoalesceWindowMs;
  std::uint64_t nextSequence_ = 1;
};

[[nodiscard]] std::string_view domainName(Domain domain) noexcept;
[[nodiscard]] std::string_view serverNeedName(ServerNeed need) noexcept;

} // namespace Mmo::Server::Perception
