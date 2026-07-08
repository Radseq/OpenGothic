#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mmo_server_types.h"

namespace Mmo::Server {

// Current MySQL schema/procedure layer is a bridge, not the final MMO database.
// Keep DB-specific formatting and versioning behind this namespace so the
// eventual persistence rewrite does not leak through gameplay authority code.
inline constexpr int DbBridgeVersion = 7;
inline constexpr std::string_view MysqlSessionPreamble =
    "SET SESSION group_concat_max_len=104857600; "
    "SET SESSION max_execution_time=0; ";

struct ContentManifestValidationError final : std::runtime_error {
  ContentManifestValidationError(std::string decision,
                                 std::string clientHash,
                                 std::string serverHash,
                                 std::string revisionKey,
                                 std::string phase)
    : std::runtime_error("client content manifest rejected: " + decision),
      decision(std::move(decision)),
      clientHash(std::move(clientHash)),
      serverHash(std::move(serverHash)),
      revisionKey(std::move(revisionKey)),
      phase(std::move(phase)) {
  }

  std::string decision;
  std::string clientHash;
  std::string serverHash;
  std::string revisionKey;
  std::string phase;
};

struct ContentManifestRejectAuditRecord final {
  std::string_view sessionUuid;
  std::string_view remoteEndpoint;
  std::string_view packetSessionKey;
  std::string_view targetKey;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::string_view phase;
  std::string_view reason;
  std::string_view clientManifestHash;
  std::string_view serverManifestHash;
  std::string_view contentRevisionKey;
  std::string_view message;
  std::string_view payloadJson;
};

[[nodiscard]] std::string sqlLiteral(std::string_view text);
[[nodiscard]] std::string sqlJson(std::string_view json);
[[nodiscard]] const char* sqlBool(bool value) noexcept;
[[nodiscard]] MySqlTarget parseMysqlUrl(std::string_view url);
[[nodiscard]] std::string runMysql(const MySqlTarget& target, std::string_view sql);
[[nodiscard]] std::vector<std::string> splitMysqlLastRow(std::string_view raw);
[[nodiscard]] std::string mysqlSingleField(const MySqlTarget& target, std::string_view sql);
[[nodiscard]] std::string mysqlSingleFieldWithDiagnostic(const MySqlTarget& target,
                                                         std::string_view sql,
                                                         std::string_view label);
[[nodiscard]] std::string mysqlJsonOr(const MySqlTarget& target,
                                      std::string_view sql,
                                      std::string_view fallbackJson);
[[nodiscard]] std::string mysqlJsonOrWithDiagnostic(const MySqlTarget& target,
                                                    std::string_view sql,
                                                    std::string_view fallbackJson,
                                                    std::string_view label);
[[nodiscard]] std::string concatenateJsonArrays(std::initializer_list<std::string_view> arrays);
[[nodiscard]] std::string dbLogin(const MySqlTarget& target, const Options& opt);
void validateClientContentManifestForSession(const MySqlTarget& target,
                                             std::string_view sessionUuid,
                                             const Options& opt,
                                             std::string_view reason);
void recordContentManifestRejectAudit(const MySqlTarget& target,
                                      const ContentManifestRejectAuditRecord& record);
[[nodiscard]] bool isActiveDbSession(const MySqlTarget& target, std::string_view sessionUuid);
[[nodiscard]] bool ensureActiveDbSession(const MySqlTarget& target,
                                         const Options& opt,
                                         std::string& sessionUuid,
                                         std::string_view reason);
[[nodiscard]] BootstrapReadiness readBootstrapReadinessWithFallback(const MySqlTarget& target,
                                                                    std::string_view characterKey,
                                                                    std::string_view worldName,
                                                                    std::string_view sessionUuid,
                                                                    std::string& selectedWorldName);

struct NpcAuthoritySnapshotSlices final {
  std::string routineStateJson = "[]";
  std::string aiStateJson = "[]";
  std::string pathStateJson = "[]";
  std::string fightStateJson = "[]";
};

struct CharacterBootstrapSnapshotSlices final {
  std::string characterListJson = "[]";
  std::string characterJson = "{}";
  std::string inventoryJson = "[]";
  std::string equipmentJson = "[]";
  std::string knownDialogsJson = "[]";
  std::string questsJson = "[]";
  std::string scriptStateJson = "[]";
};

struct WorldBootstrapSnapshotSlices final {
  std::string worldDeltasJson = "[]";
  std::string worldClockJson = "{}";
  std::string interactiveStateJson = "[]";
  std::string npcLifecycleJson = "[]";
  std::string recentEventsJson = "[]";
  std::string moverStateJson = "[]";
  std::string triggerQueueJson = "[]";
  std::string worldTransitionStateJson = "[]";
  std::string clientCorrectionsJson = "[]";
  std::string checkpointManifestJson = "{}";
};

struct PositionedBootstrapSnapshotSlices final {
  std::string activeWorldItemsJson = "[]";
  std::string nearbyNpcsJson = "[]";
  std::string nearbyNpcKnownDialogsJson = "[]";
  std::string nearbyWaypointsJson = "[]";
};

[[nodiscard]] CharacterBootstrapSnapshotSlices readCharacterBootstrapSnapshotSlices(const MySqlTarget& target,
                                                                                    std::string_view sessionUuid,
                                                                                    std::string_view characterKey,
                                                                                    std::string_view worldName);
[[nodiscard]] WorldBootstrapSnapshotSlices readWorldBootstrapSnapshotSlices(const MySqlTarget& target,
                                                                            std::string_view sessionUuid,
                                                                            std::string_view worldName);
[[nodiscard]] PositionedBootstrapSnapshotSlices readPositionedBootstrapSnapshotSlices(const MySqlTarget& target,
                                                                                      std::string_view sessionUuid,
                                                                                      std::string_view worldName);
[[nodiscard]] NpcAuthoritySnapshotSlices readNpcAuthoritySnapshotSlices(const MySqlTarget& target,
                                                                        std::string_view sessionUuid,
                                                                        std::string_view diagnosticPrefix);
[[nodiscard]] std::string buildSaveCheckpointBootstrapSnapshotJson(const MySqlTarget& target,
                                                                   std::string_view sessionUuid);

struct OutboxActionRecord final {
  std::string_view sessionUuid;
  std::string_view actionName;
  std::string_view targetKey;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
  int priority = 100;
  int maxAttempts = 5;
};

struct CharacterCheckpointRecord final {
  std::string_view sessionUuid;
  std::uint64_t serverTick = 0;
  double posX = 0.0;
  double posY = 0.0;
  double posZ = 0.0;
  double rotationYaw = 0.0;
  std::string_view waypoint;
  std::int64_t level = 0;
  std::int64_t experience = 0;
  std::int64_t experienceNext = 500;
  std::int64_t learningPoints = 0;
  std::int64_t healthCurrent = 0;
  std::int64_t healthMax = 0;
  std::int64_t manaCurrent = 0;
  std::int64_t manaMax = 0;
  std::int64_t strength = 0;
  std::int64_t dexterity = 0;
  std::int64_t guild = 0;
  std::int64_t trueGuild = 0;
  std::int64_t permanentAttitude = 0;
  std::int64_t temporaryAttitude = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct SaveCheckpointManifestRecord final {
  std::string_view sessionUuid;
  std::string_view manifestKey;
  std::string_view checkpointKind;
  std::string_view reason;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct ClientActionCorrectionRecord final {
  std::string_view sessionUuid;
  std::string_view actionName;
  std::uint64_t localSequence = 0;
  std::string_view correctionKind;
  std::string_view reason;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct ScriptIntRecord final {
  std::string_view sessionUuid;
  std::string_view scriptKey;
  std::int64_t symbolIndex = 0;
  std::int64_t valueIndex = 0;
  std::int64_t valueAfter = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct QuestUpdateRecord final {
  std::string_view sessionUuid;
  std::string_view questKey;
  std::string_view questName;
  std::string_view status;
  std::int64_t entryCount = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct KnownDialogRecord final {
  std::string_view sessionUuid;
  std::string_view npcKey;
  std::string_view infoKey;
  bool known = true;
  bool permanent = false;
  std::string_view availability;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct ProgressionAdjustmentRecord final {
  std::string_view sessionUuid;
  std::int64_t experienceDelta = 0;
  std::int64_t learningPointsDelta = 0;
  std::string_view reason;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct ExperienceRewardRecord final {
  std::string_view sessionUuid;
  std::int64_t experienceDelta = 0;
  std::string_view reason;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct CharacterDamageRecord final {
  std::string_view sessionUuid;
  std::string_view characterKey;
  std::int64_t damage = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct WorldEntityDamageRecord final {
  std::string_view sessionUuid;
  std::string_view entityKey;
  std::int64_t damage = 0;
  bool fatal = false;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct MarkNpcDeadRecord final {
  std::string_view sessionUuid;
  std::string_view entityKey;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct CharacterResourceDeltaRecord final {
  std::string_view sessionUuid;
  std::string_view characterKey;
  std::string_view resourceKey;
  std::int64_t delta = 0;
  std::int64_t valueBefore = 0;
  std::int64_t valueAfter = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct TriggerEventRecord final {
  std::string_view sessionUuid;
  std::string_view triggerKey;
  std::string_view eventTypeName;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct MoverStateRecord final {
  std::string_view sessionUuid;
  std::string_view moverKey;
  std::int64_t stateBefore = 0;
  std::int64_t stateAfter = 0;
  std::string_view stateAfterName;
  std::int64_t frame = 0;
  std::int64_t targetFrame = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct NpcRoutineStateRecord final {
  std::string_view sessionUuid;
  std::string_view npcKey;
  std::string_view routineState;
  std::string_view scheduleKey;
  std::string_view currentWaypoint;
  std::string_view targetWaypoint;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct NpcAiStateRecord final {
  std::string_view sessionUuid;
  std::string_view npcKey;
  std::string_view aiState;
  std::string_view aiIntent;
  std::string_view targetEntity;
  std::string_view perceptionState;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct NpcPathStateRecord final {
  std::string_view sessionUuid;
  std::string_view npcKey;
  std::string_view pathState;
  std::string_view routeKey;
  std::string_view currentWaypoint;
  std::string_view nextWaypoint;
  std::string_view targetWaypoint;
  std::optional<double> posX;
  std::optional<double> posY;
  std::optional<double> posZ;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct NpcFightStateRecord final {
  std::string_view sessionUuid;
  std::string_view npcKey;
  std::string_view opponentKey;
  std::string_view fightState;
  std::string_view attackState;
  std::int64_t comboIndex = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct TriggerQueueStateRecord final {
  std::string_view sessionUuid;
  std::string_view triggerKey;
  std::string_view queueState;
  std::string_view eventTypeName;
  std::int64_t scheduledServerTick = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct WorldTransitionStateRecord final {
  std::string_view sessionUuid;
  std::string_view fromWorld;
  std::string_view toWorld;
  std::string_view transitionState;
  std::string_view chapterKey;
  bool visited = true;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct ClientCorrectionAckRecord final {
  std::string_view sessionUuid;
  std::string_view actionKind;
  std::int64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct InteractiveUseRecord final {
  std::string_view sessionUuid;
  std::string_view interactiveKey;
  std::int64_t stateAfter = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct InteractiveStateUpdateRecord final {
  std::string_view sessionUuid;
  std::string_view interactiveKey;
  std::int64_t stateAfter = 0;
  std::int64_t stateCount = 0;
  std::int64_t stateMask = 0;
  bool locked = false;
  bool cracked = false;
  std::string_view lifecycle;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct NpcWeaponStateRecord final {
  std::string_view sessionUuid;
  std::string_view actorKey;
  std::string_view weaponState;
  bool ready = false;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct TransferCharacterItemRecord final {
  std::string_view sessionUuid;
  std::string_view itemUuid;
  std::string_view targetCharacterKey;
  std::int64_t amount = 1;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct LootWorldInventoryRecord final {
  std::string_view sessionUuid;
  std::string_view sourceEntityKey;
  std::string_view itemUuid;
  std::int64_t amount = 1;
  std::int64_t bagIndex = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct GrantCharacterItemBySymbolRecord final {
  std::string_view sessionUuid;
  std::int64_t itemSymbol = -1;
  std::int64_t amount = 1;
  std::int64_t bagIndex = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct PickupWorldItemRecord final {
  std::string_view sessionUuid;
  std::string_view entityKey;
  std::int64_t amount = 1;
  std::int64_t bagIndex = 0;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct RemoveWorldItemRecord final {
  std::string_view sessionUuid;
  std::string_view entityKey;
  std::string_view reason;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct EquipCharacterItemRecord final {
  std::string_view sessionUuid;
  std::string_view itemUuid;
  std::string_view equipmentSlot;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct UnequipCharacterItemRecord final {
  std::string_view sessionUuid;
  std::string_view equipmentSlot;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct ConsumeCharacterItemRecord final {
  std::string_view sessionUuid;
  std::string_view itemUuid;
  std::int64_t amount = 1;
  std::string_view reason;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct TradeSellToNpcRecord final {
  std::string_view sessionUuid;
  std::string_view npcKey;
  std::string_view itemUuid;
  std::int64_t priceTotal = 0;
  std::string_view currencyKey;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct TradeBuyFromNpcRecord final {
  std::string_view sessionUuid;
  std::string_view npcKey;
  std::string_view itemUuid;
  std::int64_t priceTotal = 0;
  std::string_view currencyKey;
  std::int64_t bagIndex = -1;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

struct DropCharacterItemRecord final {
  std::string_view sessionUuid;
  std::string_view itemUuid;
  std::int64_t amount = 1;
  std::string_view entityKey;
  std::optional<double> posX;
  std::optional<double> posY;
  std::optional<double> posZ;
  std::uint64_t serverTick = 0;
  std::string_view dbPayload;
  std::string_view idempotencyKey;
};

void enqueueOutboxAction(const MySqlTarget& target, const OutboxActionRecord& record);
void recordCharacterCheckpoint(const MySqlTarget& target, const CharacterCheckpointRecord& record);
void createSaveCheckpointManifest(const MySqlTarget& target, const SaveCheckpointManifestRecord& record);
void recordClientActionCorrection(const MySqlTarget& target, const ClientActionCorrectionRecord& record);
void setCharacterScriptInt(const MySqlTarget& target, const ScriptIntRecord& record);
void updateCharacterQuest(const MySqlTarget& target, const QuestUpdateRecord& record);
void setCharacterKnownDialog(const MySqlTarget& target, const KnownDialogRecord& record);
void adjustCharacterProgression(const MySqlTarget& target, const ProgressionAdjustmentRecord& record);
void applyCharacterExperienceReward(const MySqlTarget& target, const ExperienceRewardRecord& record);
void applyCharacterDamage(const MySqlTarget& target, const CharacterDamageRecord& record);
void applyWorldEntityDamage(const MySqlTarget& target, const WorldEntityDamageRecord& record);
void markNpcDead(const MySqlTarget& target, const MarkNpcDeadRecord& record);
void recordCharacterResourceDelta(const MySqlTarget& target, const CharacterResourceDeltaRecord& record);
void recordTriggerEvent(const MySqlTarget& target, const TriggerEventRecord& record);
void recordMoverState(const MySqlTarget& target, const MoverStateRecord& record);
void recordNpcRoutineState(const MySqlTarget& target, const NpcRoutineStateRecord& record);
void recordNpcAiState(const MySqlTarget& target, const NpcAiStateRecord& record);
void recordNpcPathState(const MySqlTarget& target, const NpcPathStateRecord& record);
void recordNpcFightState(const MySqlTarget& target, const NpcFightStateRecord& record);
void recordTriggerQueueState(const MySqlTarget& target, const TriggerQueueStateRecord& record);
void recordWorldTransitionState(const MySqlTarget& target, const WorldTransitionStateRecord& record);
void ackClientActionCorrection(const MySqlTarget& target, const ClientCorrectionAckRecord& record);
void recordInteractiveUse(const MySqlTarget& target, const InteractiveUseRecord& record);
void updateInteractiveState(const MySqlTarget& target, const InteractiveStateUpdateRecord& record);
void recordNpcWeaponState(const MySqlTarget& target, const NpcWeaponStateRecord& record);
void transferCharacterItem(const MySqlTarget& target, const TransferCharacterItemRecord& record);
void lootWorldInventoryItem(const MySqlTarget& target, const LootWorldInventoryRecord& record);
void grantCharacterItemBySymbol(const MySqlTarget& target, const GrantCharacterItemBySymbolRecord& record);
void pickupWorldItem(const MySqlTarget& target, const PickupWorldItemRecord& record);
void removeWorldItem(const MySqlTarget& target, const RemoveWorldItemRecord& record);
void equipCharacterItem(const MySqlTarget& target, const EquipCharacterItemRecord& record);
void unequipCharacterItem(const MySqlTarget& target, const UnequipCharacterItemRecord& record);
void consumeCharacterItem(const MySqlTarget& target, const ConsumeCharacterItemRecord& record);
void tradeSellToNpc(const MySqlTarget& target, const TradeSellToNpcRecord& record);
void tradeBuyFromNpc(const MySqlTarget& target, const TradeBuyFromNpcRecord& record);
void dropCharacterItem(const MySqlTarget& target, const DropCharacterItemRecord& record);

} // namespace Mmo::Server








