#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mmo_server_gameplay_authority.h"
#include "mmo_server_perception_queue.h"

namespace Mmo::Server::Perception {

inline constexpr double DefaultCrimeWitnessRange = 2200.0;
inline constexpr double DefaultCombatWitnessRange = 3200.0;
inline constexpr double DefaultSoundWitnessRange = 3600.0;
inline constexpr double DefaultMagicWitnessRange = 3000.0;
inline constexpr double DefaultVerticalWitnessRange = Gameplay::DefaultNpcPerceptionVerticalRange;
inline constexpr double DefaultPeripheralFocusCos = 0.15;

enum class Sense : std::uint8_t {
  None = 0,
  See = 1 << 0,
  Hear = 1 << 1,
  Smell = 1 << 2,
};

[[nodiscard]] constexpr Sense operator|(Sense lhs, Sense rhs) noexcept {
  return static_cast<Sense>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool hasSense(Sense mask, Sense value) noexcept {
  return (static_cast<std::uint8_t>(mask) & static_cast<std::uint8_t>(value)) != 0;
}

struct WitnessCandidate final {
  std::string_view witnessKey;
  Gameplay::Vec3 position;
  Gameplay::Vec3 forward {0.0, 0.0, 1.0};
  Sense senses = Sense::See | Sense::Hear;
  double rangeScale = 1.0;
  bool down = false;
  bool sameAsSource = false;
};

struct WitnessDecision final {
  bool comparable = false;
  bool witnessed = false;
  const char* reason = "not_comparable";
  double distanceSq = 0.0;
};

struct WitnessResult final {
  std::string witnessKey;
  WitnessDecision decision;
};

[[nodiscard]] constexpr double defaultRangeFor(Domain domain) noexcept {
  switch(domain) {
    case Domain::Combat: return DefaultCombatWitnessRange;
    case Domain::Crime: return DefaultCrimeWitnessRange;
    case Domain::Sound: return DefaultSoundWitnessRange;
    case Domain::Magic: return DefaultMagicWitnessRange;
    case Domain::Room: return DefaultCrimeWitnessRange;
    case Domain::Social:
    case Domain::Movement:
    case Domain::Item:
    case Domain::Command:
      return Gameplay::DefaultNpcPerceptionRange;
  }
  return Gameplay::DefaultNpcPerceptionRange;
}

[[nodiscard]] constexpr bool requiresHearing(Domain domain) noexcept {
  return domain == Domain::Sound;
}

[[nodiscard]] constexpr bool requiresSight(Domain domain) noexcept {
  return domain == Domain::Crime || domain == Domain::Combat ||
         domain == Domain::Magic || domain == Domain::Room ||
         domain == Domain::Item || domain == Domain::Movement;
}

[[nodiscard]] constexpr bool requiresFocus(Domain domain) noexcept {
  return domain == Domain::Crime || domain == Domain::Room ||
         domain == Domain::Item || domain == Domain::Movement;
}

[[nodiscard]] WitnessDecision evaluateWitness(const Event& event,
                                              const WitnessCandidate& candidate) noexcept;
[[nodiscard]] std::vector<WitnessResult> evaluateWitnesses(const Event& event,
                                                           const std::vector<WitnessCandidate>& candidates);

} // namespace Mmo::Server::Perception
