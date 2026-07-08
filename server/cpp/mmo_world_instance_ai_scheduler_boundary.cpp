#include "mmo_world_instance_ai_scheduler_boundary.h"

namespace Mmo::WorldInstanceAiTick {

WorldInstanceAiSchedulerBoundaryResult runWorldInstanceAiSchedulerBoundary(
    const Server::MySqlTarget& target,
    const WorldInstanceAiSchedulerBoundaryOptions& options) {
  WorldInstanceAiSchedulerBoundaryResult result;
  result.enabled = options.enabled;
  result.evidence.accepted = false;
  result.evidence.status = "scheduler_disabled";
  result.evidence.reason = "world_instance AI scheduler boundary is disabled";

  if(!options.enabled) {
    return result;
  }

  WorldInstanceAiTickOptions dryRunOptions = options.tick;
  dryRunOptions.dryRun = true;
  result.dryRunTick = runWorldInstanceAiTick(target, dryRunOptions);
  result.executed = true;
  result.evidence = evaluateWorldInstanceAiTickEvidence(result.dryRunTick, options.evidence);

  if(!result.evidence.accepted || options.tick.dryRun) {
    return result;
  }

  result.writeTick = runWorldInstanceAiTick(target, options.tick);
  result.writeExecuted = true;
  return result;
}

} // namespace Mmo::WorldInstanceAiTick
