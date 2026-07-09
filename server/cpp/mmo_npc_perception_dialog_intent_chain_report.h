#pragma once

#include "mmo_npc_perception_dialog_intent_chain_guard.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentChainReportOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_chain_report.v1";
  std::string reportSource = "mmo_manual_action_dispatcher_probe";
  std::string reportMode = "read_only_report_no_dispatch_no_db";
  bool requireGuardedChain = true;
  bool requireNoLiveSideEffects = true;
  bool requireFutureStoragePrerequisitesMissing = true;
  bool requireNoDbMutation = true;
};

struct NpcPerceptionDialogIntentChainReport final {
  bool built = false;
  bool reportOnly = true;
  bool proofOnly = true;
  bool dbMutated = false;
  bool sqlMigrationGenerated = false;
  bool serverSqlTouched = false;
  bool liveDispatchEnabled = false;
  bool liveDispatchReady = false;
  bool canEnableLiveDispatch = false;
  bool noLiveSideEffects = true;
  bool futureStoragePrerequisitesMissing = true;
  bool explicitDbMigrationRequiredBeforeLiveDispatch = true;
  bool guardAccepted = false;
  bool proofChainComplete = false;
  bool terminalPlanReady = false;
  bool transportProofComplete = false;
  bool ackProofComplete = false;
  bool timeoutPlanComplete = false;

  std::string status = "not_reported";
  std::string contractVersion;
  std::string reportSource;
  std::string reportMode;
  std::string guardStatus;
  std::string actionQueueUuid;
  std::string decisionUuid;
  std::string actionKind;
  std::string worldInstanceUuid;
  std::string targetSessionUuid;
  std::string targetCharacterUuid;
  std::string ackCorrelationKey;
  std::string recommendedNextStep = "keep_no_db_pause_or_design_storage_migration_in_llm_db_changes";

  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t ackTimeoutMs = 0;
  std::uint32_t maxRetryAttempts = 0;
  std::size_t completedStageCount = 0;
  std::size_t expectedStageCount = 14;
  std::size_t blockedSideEffectCount = 0;
  std::size_t guardIssueCount = 0;

  std::vector<std::string> completedStages;
  std::vector<std::string> blockedSideEffects;
  std::vector<std::string> missingStorageSurfaces;
  std::vector<std::string> nextChecks;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentChainReport(
    const NpcPerceptionDialogIntentChainGuard& guard) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentChainReport buildNpcPerceptionDialogIntentChainReport(
    const NpcPerceptionDialogIntentChainGuard& guard,
    const NpcPerceptionDialogIntentChainReportOptions& options = {});

[[nodiscard]] std::string dialogIntentChainReportJson(
    const NpcPerceptionDialogIntentChainReport& report);

} // namespace Mmo::AiRuntime
