#include "mmo_world_instance_ai_tick_evidence.h"

#include <algorithm>
#include <string>
#include <utility>

namespace Mmo::WorldInstanceAiTick {
namespace {

void reject(WorldInstanceAiTickEvidenceReport& report, std::string status, std::string reason) {
  if(!report.accepted) {
    return;
  }
  report.accepted = false;
  report.status = std::move(status);
  report.reason = std::move(reason);
}

[[nodiscard]] double ratio(std::size_t part, std::size_t whole) noexcept {
  if(whole == 0) {
    return 1.0;
  }
  return static_cast<double>(part) / static_cast<double>(whole);
}

} // namespace

WorldInstanceAiTickEvidenceReport evaluateWorldInstanceAiTickEvidence(
    const WorldInstanceAiTickResult& result,
    const WorldInstanceAiTickEvidenceOptions& options) {
  WorldInstanceAiTickEvidenceReport report;
  report.acceptedNpcs = result.npcIdentity.acceptedNpcs;
  report.playerActors = result.assessment.playerActors;
  report.decisions = result.assessment.decisions;
  report.weakIdentitySkips = result.npcIdentity.skippedWeakEntityKeys;
  report.missingNpcInstanceSkips = result.npcIdentity.skippedMissingNpcInstance;
  report.recordLimitSkips = result.skippedByRecordLimit;
  report.acceptedNpcRatio = ratio(result.npcIdentity.acceptedNpcs, result.npcIdentity.npcRowsRead);

  if(options.requireActorPair && (result.assessment.npcActors == 0 || result.assessment.playerActors == 0)) {
    reject(report, "no_actor_pair", "expected at least one accepted NPC actor and one player actor");
  }
  if(result.npcIdentity.acceptedNpcs < options.minAcceptedNpcs) {
    reject(report,
           "accepted_npc_floor_failed",
           "accepted NPC count is below required minimum " + std::to_string(options.minAcceptedNpcs));
  }
  if(result.assessment.playerActors < options.minPlayerActors) {
    reject(report,
           "player_actor_floor_failed",
           "player actor count is below required minimum " + std::to_string(options.minPlayerActors));
  }
  if(options.requireDecision && result.assessment.decisions == 0) {
    reject(report, "no_decision", "expected at least one perception decision");
  }
  if(result.assessment.decisions < options.minDecisions) {
    reject(report,
           "decision_floor_failed",
           "decision count is below required minimum " + std::to_string(options.minDecisions));
  }
  if(options.requireCleanNpcIdentity &&
     (result.npcIdentity.skippedWeakEntityKeys != 0 || result.npcIdentity.skippedMissingNpcInstance != 0)) {
    reject(report, "npc_identity_not_clean", "weak or incomplete NPC identity rows were skipped");
  }
  if(result.npcIdentity.skippedWeakEntityKeys > options.maxSkippedWeakEntityKeys) {
    reject(report,
           "weak_npc_identity_budget_exceeded",
           "weak NPC entity-key skips exceed configured budget " +
               std::to_string(options.maxSkippedWeakEntityKeys));
  }
  if(result.npcIdentity.skippedMissingNpcInstance > options.maxSkippedMissingNpcInstance) {
    reject(report,
           "missing_npc_instance_budget_exceeded",
           "missing NPC instance skips exceed configured budget " +
               std::to_string(options.maxSkippedMissingNpcInstance));
  }
  if(options.requireNoRecordLimitSkip && result.skippedByRecordLimit != 0) {
    reject(report, "record_limit_would_skip", "record limit is lower than produced decision count");
  }

  return report;
}

} // namespace Mmo::WorldInstanceAiTick
