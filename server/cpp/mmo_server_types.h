#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace Mmo::Server {

struct Options final {
  std::string bind = "127.0.0.1:29777";
  std::string mysqlUrl;
  std::string accountName = "local-import";
  std::string characterKey = "PC_HERO";
  std::string characterDisplayName = "Ja";
  std::string sessionKey = "local-dev-PC_HERO_TEST";
  std::string dbSessionUuid;
  std::string clientContentManifestHash;
  std::string runtimeReadModelPath;
  std::string contentRevisionKey;
  std::string worldInstanceKey;
  std::string worldName;
  std::uint64_t worldInstanceAiSchedulerIntervalMs = 250;
  std::string worldInstanceAiDatabaseName = "mmo_ai_runtime";
  std::string worldInstanceAiPerceptionKind = "PERC_ASSESSPLAYER";
  std::string aiDialogIntentText = "MMO server dialog intent transport probe.";
  std::string aiDialogIntentSpeakerEntityKey = "mmo.server.diagnostic_npc";
  std::string aiDialogIntentLineId = "MMO_DIAGNOSTIC_DIALOG_INTENT";
  std::string aiDialogIntentFanoutMode = "target_session";
  double worldInstanceAiMaxDistance = 1500.0;
  double aiDialogIntentAoiRadius = 1800.0;
  std::uint64_t worldInstanceAiServerTick = 0;
  std::uint64_t worldInstanceAiCooldownTicks = 250;
  std::uint64_t aiDialogIntentAckTimeoutMs = 5000;
  int worldInstanceAiPriorityValue = 100;
  std::size_t worldInstanceAiMaxNpcs = 32;
  std::size_t aiDialogIntentMaxRecipients = 8;
  std::size_t worldInstanceAiMaxPlayers = 16;
  std::size_t worldInstanceAiMinAcceptedNpcs = 0;
  std::size_t worldInstanceAiMinPlayerActors = 0;
  std::size_t worldInstanceAiMinDecisions = 0;
  std::size_t worldInstanceAiMaxSkippedWeakNpcIdentity = std::numeric_limits<std::size_t>::max();
  std::size_t worldInstanceAiMaxSkippedMissingNpcInstance = std::numeric_limits<std::size_t>::max();
  bool worldInstanceAiStartupDryRun = false;
  bool worldInstanceAiStartupFailOnEvidence = false;
  bool worldInstanceAiRepairWeakNpcEntityKeys = true;
  bool worldInstanceAiIncludeWeakNpcIdentity = false;
  bool worldInstanceAiRequireActorPair = false;
  bool worldInstanceAiRequireDecision = false;
  bool worldInstanceAiRequireCleanNpcIdentity = false;
  bool worldInstanceAiRequireNoRecordLimitSkip = false;
  bool enableAiDialogIntentSend = false;
  bool aiDialogIntentConversationSessionProbe = false;
  bool aiDialogIntentLateObserverResumePlan = false;
  bool aiDialogIntentLateObserverResumePacketBoundary = false;
  bool aiDialogIntentLateObserverResumeSendGate = false;
  bool aiDialogIntentLateObserverResumeRuntimeSend = false;
  bool aiDialogIntentLateObserverResumeDeliveryRegistrationBoundary = false;
  bool aiDialogIntentLateObserverResumeDispatchEnvelope = false;
  bool aiDialogIntentLateObserverResumeMutationGuard = false;
  bool aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard = false;
  bool aiDialogIntentLateObserverResumeCommitPreflight = false;
  bool aiDialogIntentObservationReceiptProbe = false;
  bool aiDialogIntentStep273PersistencePreview = false;
  bool aiDialogIntentStep273PersistenceExecutionBoundary = false;
  bool aiDialogIntentStep273PersistenceExecutorAdapter = false;
  bool aiDialogIntentStep273PersistenceDryRunReportGate = false;
  bool aiDialogIntentStep273PersistenceOutcomePolicy = false;
  bool aiDialogIntentStep273PersistenceOutcomeObservability = false;
  bool aiDialogIntentStep273PersistenceActivationPreflight = false;
  bool aiDialogIntentStep273PersistenceRuntimeStorage = false;
  bool aiDialogIntentStep273DurableMarkAppliedGate = false;
  bool runtimeReadModelActiveExportPlanOnly = false;
  bool worldInstanceNpcIdentityMaterializationPlanOnly = false;
  bool worldInstanceAiSchedulerPlanOnly = false;
  int outboxPriority = 100;
  int outboxMaxAttempts = 5;
  int maxPackets = 0;
  bool directDb = true;
  bool enqueueOutbox = false;
  bool forwardBootstrapOutbox = false;
  bool requireDbSaveCheckpointRestore = false;
  bool requireClientContentManifest = false;
  bool requireRuntimeReadModelContentRevisionMatch = true;
  bool startupCheckOnly = false;
};

struct BootstrapReadiness final {
  bool ready = false;
  std::uint64_t metaRows = 0;
  std::uint64_t characterRows = 0;
  std::uint64_t worldEntityRows = 0;
  std::uint64_t characterInventoryRows = 0;
  std::uint64_t questRows = 0;
  std::uint64_t knownDialogRows = 0;
  std::uint64_t scriptIntRows = 0;
  std::uint64_t waypointRows = 0;
  std::uint64_t waypointEdgeRows = 0;
  std::uint64_t worldInventoryRows = 0;
  std::uint64_t interactiveRows = 0;
  std::uint64_t worldClockRows = 0;
};

struct MySqlTarget final {
  std::string host = "127.0.0.1";
  int port = 3306;
  std::string user;
  std::string password;
  std::string database;
};

struct DirectApplyResult final {
  bool handled = false;
  bool accepted = true;
  bool ready = false;
  const char* label = "unhandled";
};

struct WorldItemIdentity final {
  std::string exact;
  std::string world;
  std::int64_t persistentId = -1;
  std::int64_t symbol = -1;
};

} // namespace Mmo::Server




















