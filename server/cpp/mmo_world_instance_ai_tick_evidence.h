#pragma once

#include "mmo_world_instance_ai_tick.h"

#include <cstddef>
#include <limits>
#include <string>

namespace Mmo::WorldInstanceAiTick {

struct WorldInstanceAiTickEvidenceOptions final {
  std::size_t minAcceptedNpcs = 0;
  std::size_t minPlayerActors = 0;
  std::size_t minDecisions = 0;
  std::size_t maxSkippedWeakEntityKeys = std::numeric_limits<std::size_t>::max();
  std::size_t maxSkippedMissingNpcInstance = std::numeric_limits<std::size_t>::max();
  bool requireActorPair = false;
  bool requireDecision = false;
  bool requireCleanNpcIdentity = false;
  bool requireNoRecordLimitSkip = false;
};

struct WorldInstanceAiTickEvidenceReport final {
  bool accepted = true;
  std::string status = "accepted";
  std::string reason = "ok";
  std::size_t acceptedNpcs = 0;
  std::size_t playerActors = 0;
  std::size_t decisions = 0;
  std::size_t weakIdentitySkips = 0;
  std::size_t missingNpcInstanceSkips = 0;
  std::size_t recordLimitSkips = 0;
  double acceptedNpcRatio = 1.0;
};

[[nodiscard]] WorldInstanceAiTickEvidenceReport evaluateWorldInstanceAiTickEvidence(
    const WorldInstanceAiTickResult& result,
    const WorldInstanceAiTickEvidenceOptions& options);

} // namespace Mmo::WorldInstanceAiTick
