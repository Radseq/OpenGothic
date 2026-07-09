#pragma once

#include "mmo_world_instance_ai_tick_evidence.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Mmo::WorldInstanceAiTick {

struct WorldInstanceAiSchedulerPlanOptions final {
  bool enabled = false;
  std::string schedulerMode = "plan_only_no_timer_no_db";
  std::uint64_t intervalMs = 250;
  std::size_t maxWorldInstances = 1;
  bool tickOncePerWorldInstance = true;
  bool allowWrites = false;
};

struct WorldInstanceAiSchedulerPlan final {
  bool built = false;
  bool enabled = false;
  bool tickOncePerWorldInstance = false;
  bool timerScheduled = false;
  bool tickExecuted = false;
  bool dbMutated = false;
  bool writeAllowed = false;

  std::string status = "not_built";
  std::string schedulerMode;
  std::uint64_t intervalMs = 0;
  std::size_t maxWorldInstances = 0;
  std::vector<std::string> issues;
};

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

[[nodiscard]] WorldInstanceAiSchedulerPlan buildWorldInstanceAiSchedulerPlan(
    const WorldInstanceAiSchedulerPlanOptions& options);

[[nodiscard]] std::string worldInstanceAiSchedulerPlanJson(
    const WorldInstanceAiSchedulerPlan& plan);

[[nodiscard]] WorldInstanceAiSchedulerBoundaryResult runWorldInstanceAiSchedulerBoundary(
    const Server::MySqlTarget& target,
    const WorldInstanceAiSchedulerBoundaryOptions& options);

} // namespace Mmo::WorldInstanceAiTick
