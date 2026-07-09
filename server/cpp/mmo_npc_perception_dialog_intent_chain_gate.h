#pragma once

#include "mmo_npc_perception_dialog_intent_chain_report.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentChainGateOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_chain_gate.v1";
  std::string gateSource = "mmo_manual_action_dispatcher_probe";
  std::string gateMode = "ci_readiness_gate_no_dispatch_no_db";
  bool requireReportBuilt = true;
  bool requireNoLiveSideEffects = true;
  bool requireDbWorkPaused = true;
  bool requireNoDbMutation = true;
  bool requireNoSqlGeneration = true;
  bool requireLiveDispatchBlocked = true;
  bool requireFutureStoragePrerequisitesMissing = true;
  bool requireMissingStorageSurfacesDocumented = true;
};

struct NpcPerceptionDialogIntentChainGate final {
  bool built = false;
  bool gateAccepted = false;
  bool readOnly = true;
  bool proofOnly = true;
  bool ciSafe = false;
  bool dbWorkPaused = true;
  bool dbMutated = false;
  bool sqlMigrationGenerated = false;
  bool serverSqlTouched = false;
  bool liveDispatchEnabled = false;
  bool liveDispatchReady = false;
  bool canEnableLiveDispatch = false;
  bool noLiveSideEffects = true;
  bool futureStoragePrerequisitesMissing = true;
  bool missingStorageSurfacesDocumented = false;
  bool explicitDbMigrationRequiredBeforeLiveDispatch = true;
  bool reportBuilt = false;
  bool proofChainComplete = false;
  bool transportProofComplete = false;
  bool ackProofComplete = false;
  bool timeoutPlanComplete = false;
  bool terminalPlanReady = false;

  std::string status = "not_gated";
  std::string contractVersion;
  std::string gateSource;
  std::string gateMode;
  std::string reportStatus;
  std::string actionQueueUuid;
  std::string decisionUuid;
  std::string actionKind;
  std::string worldInstanceUuid;
  std::string targetSessionUuid;
  std::string targetCharacterUuid;
  std::string ackCorrelationKey;
  std::string ciVerdict = "not_evaluated";
  std::string recommendedNextStep = "keep_no_db_pause_or_prepare_llm_db_changes_migration_design";

  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t ackTimeoutMs = 0;
  std::uint32_t maxRetryAttempts = 0;
  std::size_t completedStageCount = 0;
  std::size_t expectedStageCount = 15;
  std::size_t blockedSideEffectCount = 0;
  std::size_t missingStorageSurfaceCount = 0;

  std::vector<std::string> passedChecks;
  std::vector<std::string> blockedTransitions;
  std::vector<std::string> missingStorageSurfaces;
  std::vector<std::string> requiredBeforeLiveDispatch;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentChainGate(
    const NpcPerceptionDialogIntentChainReport& report) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentChainGate buildNpcPerceptionDialogIntentChainGate(
    const NpcPerceptionDialogIntentChainReport& report,
    const NpcPerceptionDialogIntentChainGateOptions& options = {});

[[nodiscard]] std::string dialogIntentChainGateJson(
    const NpcPerceptionDialogIntentChainGate& gate);

} // namespace Mmo::AiRuntime
