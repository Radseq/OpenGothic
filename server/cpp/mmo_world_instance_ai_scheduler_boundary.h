#pragma once

#include "mmo_world_instance_ai_tick_evidence.h"

namespace Mmo::WorldInstanceAiTick {

struct WorldInstanceAiSchedulerBoundaryOptions final {
  bool enabled = false;
  WorldInstanceAiTickOptions tick;
  WorldInstanceAiTickEvidenceOptions evidence;
};

struct WorldInstanceAiSchedulerBoundaryResult final {
  bool enabled = false;
  bool executed = false;
  bool writeExecuted = false;
  WorldInstanceAiTickResult dryRunTick;
  WorldInstanceAiTickResult writeTick;
  WorldInstanceAiTickEvidenceReport evidence;
};

[[nodiscard]] WorldInstanceAiSchedulerBoundaryResult runWorldInstanceAiSchedulerBoundary(
    const Server::MySqlTarget& target,
    const WorldInstanceAiSchedulerBoundaryOptions& options);

} // namespace Mmo::WorldInstanceAiTick
