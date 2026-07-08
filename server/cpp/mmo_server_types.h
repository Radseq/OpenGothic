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
  std::string worldInstanceAiDatabaseName = "mmo_ai_runtime";
  std::string worldInstanceAiPerceptionKind = "PERC_ASSESSPLAYER";
  double worldInstanceAiMaxDistance = 1500.0;
  std::uint64_t worldInstanceAiServerTick = 0;
  std::uint64_t worldInstanceAiCooldownTicks = 250;
  int worldInstanceAiPriorityValue = 100;
  std::size_t worldInstanceAiMaxNpcs = 32;
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






