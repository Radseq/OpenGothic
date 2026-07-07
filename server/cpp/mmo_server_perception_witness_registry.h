#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mmo_server_perception_sensor.h"

namespace Mmo::Server::Perception {

inline constexpr std::uint64_t DefaultWitnessObservationTtlMs = 10000;

struct ObservationInput final {
  std::string_view npcKey;
  Gameplay::Vec3 position;
  std::optional<double> yawRad;
  Sense senses = Sense::See | Sense::Hear;
  bool down = false;
  bool dead = false;
  std::uint64_t serverTickMs = 0;
};

struct Observation final {
  std::string npcKey;
  Gameplay::Vec3 position;
  Gameplay::Vec3 forward {0.0, 0.0, 1.0};
  Sense senses = Sense::See | Sense::Hear;
  bool down = false;
  bool dead = false;
  std::uint64_t updatedServerTickMs = 0;
};

struct WitnessSummary final {
  std::uint32_t comparable = 0;
  std::uint32_t witnessed = 0;
  std::vector<WitnessResult> results;
};

class WitnessRegistry final {
public:
  [[nodiscard]] bool observe(const ObservationInput& input);
  [[nodiscard]] std::optional<Observation> observation(std::string_view npcKey) const;
  [[nodiscard]] std::vector<WitnessCandidate> candidatesFor(const Event& event,
                                                            std::uint64_t nowServerTickMs) const;
  [[nodiscard]] WitnessSummary evaluate(const Event& event,
                                        std::uint64_t nowServerTickMs) const;

  void expire(std::uint64_t nowServerTickMs);
  void clear();

private:
  std::unordered_map<std::string, Observation> observations_;
};

[[nodiscard]] Gameplay::Vec3 forwardFromYawRad(double yawRad) noexcept;

} // namespace Mmo::Server::Perception
