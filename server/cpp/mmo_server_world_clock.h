#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Mmo::Server {

inline constexpr std::int64_t WorldClockMinuteMs = 60 * 1000;
inline constexpr std::int64_t WorldClockHourMs = 60 * WorldClockMinuteMs;
inline constexpr std::int64_t WorldClockDayMs = 24 * WorldClockHourMs;

struct WorldClockTime final {
  std::int64_t worldTimeMs = 0;

  [[nodiscard]] constexpr std::int64_t normalizedDayTimeMs() const noexcept {
    const auto mod = worldTimeMs % WorldClockDayMs;
    return mod < 0 ? mod + WorldClockDayMs : mod;
  }

  [[nodiscard]] constexpr int hour() const noexcept {
    return static_cast<int>(normalizedDayTimeMs() / WorldClockHourMs);
  }

  [[nodiscard]] constexpr int minute() const noexcept {
    return static_cast<int>((normalizedDayTimeMs() % WorldClockHourMs) / WorldClockMinuteMs);
  }
};

[[nodiscard]] constexpr WorldClockTime advanceWorldClock(WorldClockTime clock,
                                                         std::int64_t deltaMs) noexcept {
  if(deltaMs <= 0)
    return clock;
  clock.worldTimeMs += deltaMs;
  return clock;
}

[[nodiscard]] std::string buildWorldClockSnapshotQuery(std::string_view sessionSql,
                                                       std::string_view fallbackWorldSql);

} // namespace Mmo::Server
