#include "mmo_server_perception_sensor.h"

#include <cmath>

namespace Mmo::Server::Perception {

WitnessDecision evaluateWitness(const Event& event, const WitnessCandidate& candidate) noexcept {
  if(!event.originPosition)
    return {.reason = "missing_event_position"};
  if(candidate.witnessKey.empty())
    return {.reason = "missing_witness"};
  if(candidate.down)
    return {.comparable = true, .reason = "witness_down"};
  if(candidate.sameAsSource)
    return {.comparable = true, .reason = "witness_is_source"};
  if(!Gameplay::finitePosition(*event.originPosition) || !Gameplay::finitePosition(candidate.position))
    return {.reason = "invalid_position"};

  const auto domain = event.def.domain;
  if(requiresHearing(domain) && !hasSense(candidate.senses, Sense::Hear))
    return {.comparable = true, .reason = "missing_hearing"};
  if(requiresSight(domain) && !hasSense(candidate.senses, Sense::See))
    return {.comparable = true, .reason = "missing_sight"};

  const double range = std::max(0.0, defaultRangeFor(domain) * candidate.rangeScale);
  const double distanceSq = Gameplay::distanceSq3d(candidate.position, *event.originPosition);
  if(distanceSq > range * range)
    return {.comparable = true, .reason = "out_of_range", .distanceSq = distanceSq};

  if(std::abs(candidate.position.y - event.originPosition->y) > DefaultVerticalWitnessRange)
    return {.comparable = true, .reason = "vertical_out_of_range", .distanceSq = distanceSq};

  if(requiresFocus(domain)) {
    const bool focused = Gameplay::canPerceive({
      .observer = candidate.position,
      .target = *event.originPosition,
      .observerForward = candidate.forward,
      .range = range,
      .verticalRange = DefaultVerticalWitnessRange,
      .minForwardDot = DefaultPeripheralFocusCos,
      .requiresFocus = true,
    });
    if(!focused)
      return {.comparable = true, .reason = "outside_focus", .distanceSq = distanceSq};
  }

  return {.comparable = true, .witnessed = true, .reason = "witnessed", .distanceSq = distanceSq};
}

std::vector<WitnessResult> evaluateWitnesses(const Event& event,
                                             const std::vector<WitnessCandidate>& candidates) {
  std::vector<WitnessResult> out;
  out.reserve(candidates.size());
  for(const auto& candidate : candidates) {
    out.push_back({
      .witnessKey = std::string(candidate.witnessKey),
      .decision = evaluateWitness(event, candidate),
    });
  }
  return out;
}

static_assert(defaultRangeFor(Domain::Crime) == DefaultCrimeWitnessRange);
static_assert(requiresSight(Domain::Crime));
static_assert(requiresHearing(Domain::Sound));
static_assert(!requiresFocus(Domain::Sound));

} // namespace Mmo::Server::Perception
