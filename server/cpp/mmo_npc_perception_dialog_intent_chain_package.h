#pragma once

#include "mmo_npc_perception_dialog_intent_chain_gate.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentChainPackageOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_chain_package.v1";
  std::string packageSource = "mmo_manual_action_dispatcher_probe";
  std::string packageMode = "read_only_package_no_dispatch_no_db";
  std::string llmDbChangesLedgerPath = "docs/llm/llm_db_changes/";
  std::string stepLedgerEntry = "docs/llm/llm_db_changes/011-step253-chain-package-no-db.md";
  bool requireGateAccepted = true;
  bool requireNoLiveSideEffects = true;
  bool requireDbWorkPaused = true;
  bool requireNoDbMutation = true;
  bool requireNoSqlGeneration = true;
  bool requireLiveDispatchBlocked = true;
  bool requireDbLedgerPointer = true;
};

struct NpcPerceptionDialogIntentChainPackage final {
  bool built = false;
  bool packageReady = false;
  bool readOnly = true;
  bool proofOnly = true;
  bool reportOnly = true;
  bool ciSafe = false;
  bool dbWorkPaused = true;
  bool dbMutated = false;
  bool sqlMigrationGenerated = false;
  bool serverSqlTouched = false;
  bool liveDispatchEnabled = false;
  bool liveDispatchReady = false;
  bool canEnableLiveDispatch = false;
  bool noLiveSideEffects = true;
  bool gateAccepted = false;
  bool gatePackaged = false;
  bool dbLedgerPointerIncluded = false;
  bool reportArtifactWriteRequired = false;
  bool fileWriteExecuted = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool socketReceiveExecuted = false;
  bool livePacketDecoded = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;
  bool actionMarkedApplied = false;
  bool futureDbWorkDocumentedOnly = true;
  bool explicitDbMigrationRequiredBeforeLiveDispatch = true;

  std::string status = "not_packaged";
  std::string contractVersion;
  std::string packageSource;
  std::string packageMode;
  std::string packageName = "ai_dialog_intent_proof_chain_step253";
  std::string gateStatus;
  std::string gateCiVerdict;
  std::string actionQueueUuid;
  std::string decisionUuid;
  std::string actionKind;
  std::string worldInstanceUuid;
  std::string targetSessionUuid;
  std::string targetCharacterUuid;
  std::string ackCorrelationKey;
  std::string llmDbChangesLedgerPath;
  std::string stepLedgerEntry;
  std::string recommendedNextStep = "keep_no_db_pause_or_prepare_real_migration_from_llm_db_changes";

  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t ackTimeoutMs = 0;
  std::uint32_t maxRetryAttempts = 0;
  std::size_t completedStageCount = 0;
  std::size_t expectedStageCount = 16;
  std::size_t blockedTransitionCount = 0;
  std::size_t missingStorageSurfaceCount = 0;
  std::size_t requiredBeforeLiveDispatchCount = 0;

  std::vector<std::string> packageSections;
  std::vector<std::string> blockedTransitions;
  std::vector<std::string> missingStorageSurfaces;
  std::vector<std::string> requiredBeforeLiveDispatch;
  std::vector<std::string> dbLedgerNotes;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentChainPackage(
    const NpcPerceptionDialogIntentChainGate& gate) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentChainPackage buildNpcPerceptionDialogIntentChainPackage(
    const NpcPerceptionDialogIntentChainGate& gate,
    const NpcPerceptionDialogIntentChainPackageOptions& options = {});

[[nodiscard]] std::string dialogIntentChainPackageJson(
    const NpcPerceptionDialogIntentChainPackage& package);

} // namespace Mmo::AiRuntime
