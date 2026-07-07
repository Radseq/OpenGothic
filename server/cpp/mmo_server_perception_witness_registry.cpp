#include "mmo_server_perception_witness_registry.h"

#include <cmath>

namespace Mmo::Server::Perception {

Gameplay::Vec3 forwardFromYawRad(double yawRad) noexcept {
  if(!std::isfinite(yawRad))
    return {0.0, 0.0, 1.0};
  return {std::cos(yawRad), 0.0, std::sin(yawRad)};
}

bool WitnessRegistry::observe(const ObservationInput& input) {
  if(input.npcKey.empty() || !Gameplay::finitePosition(input.position))
    return false;

  auto& out = observations_[std::string(input.npcKey)];
  out.npcKey = std::string(input.npcKey);
  out.position = input.position;
  if(input.yawRad)
    out.forward = forwardFromYawRad(*input.yawRad);
  out.senses = input.senses;
  out.down = input.down;
  out.dead = input.dead;
  out.updatedServerTickMs = input.serverTickMs;
  return true;
}

std::optional<Observation> WitnessRegistry::observation(std::string_view npcKey) const {
  const auto it = observations_.find(std::string(npcKey));
  if(it == observations_.end())
    return std::nullopt;
  return it->second;
}

std::vector<WitnessCandidate> WitnessRegistry::candidatesFor(const Event& event,
                                                             std::uint64_t nowServerTickMs) const {
  std::vector<WitnessCandidate> out;
  out.reserve(observations_.size());
  for(const auto& [key, observation] : observations_) {
    if(nowServerTickMs > observation.updatedServerTickMs + DefaultWitnessObservationTtlMs)
      continue;
    out.push_back({
      .witnessKey = observation.npcKey,
      .position = observation.position,
      .forward = observation.forward,
      .senses = observation.senses,
      .rangeScale = 1.0,
      .down = observation.down || observation.dead,
      .sameAsSource = !event.sourceKey.empty() && observation.npcKey == event.sourceKey,
    });
  }
  return out;
}

WitnessSummary WitnessRegistry::evaluate(const Event& event,
                                         std::uint64_t nowServerTickMs) const {
  WitnessSummary out;
  out.results = evaluateWitnesses(event, candidatesFor(event, nowServerTickMs));
  for(const auto& result : out.results) {
    if(result.decision.comparable)
      ++out.comparable;
    if(result.decision.witnessed)
      ++out.witnessed;
  }
  return out;
}

void WitnessRegistry::expire(std::uint64_t nowServerTickMs) {
  for(auto it = observations_.begin(); it != observations_.end();) {
    if(nowServerTickMs > it->second.updatedServerTickMs + DefaultWitnessObservationTtlMs)
      it = observations_.erase(it);
    else
      ++it;
  }
}

void WitnessRegistry::clear() {
  observations_.clear();
}

static_assert(DefaultWitnessObservationTtlMs > 0);

} // namespace Mmo::Server::Perception
