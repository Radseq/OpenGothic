#include "mmo_server_persistence.h"
#include "mmo_server_persistence_sql.h"

namespace Mmo::Server {

void enqueueOutboxAction(const MySqlTarget& target, const OutboxActionRecord& record) {
  std::string sql;
  sql += "SET @action_id = NULL;";
  sql += "SET @status = NULL;";
  sql += "CALL mmo_enqueue_server_action(";
  sql += "UUID_TO_BIN(";
  sql += sqlLiteral(record.sessionUuid);
  sql += ", 1),";
  sql += sqlLiteral(record.actionName) + ",";
  sql += sqlLiteral(record.targetKey) + ",";
  sql += sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",";
  sql += std::to_string(record.priority) + ",";
  sql += std::to_string(record.maxAttempts) + ",";
  sql += "@action_id,@status);";
  sql += "SELECT CONCAT(BIN_TO_UUID(@action_id, 1), '\\t', @status);";
  (void)runMysql(target, sql);
}

void recordCharacterCheckpoint(const MySqlTarget& target, const CharacterCheckpointRecord& record) {
  std::string sql;
  sql += "SET @event_id = NULL;";
  sql += "CALL mmo_checkpoint_character_state(";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ", 1),";
  sql += std::to_string(record.serverTick) + ",";
  sql += std::to_string(record.posX) + ",";
  sql += std::to_string(record.posY) + ",";
  sql += std::to_string(record.posZ) + ",";
  sql += std::to_string(record.rotationYaw) + ",";
  sql += sqlLiteral(record.waypoint) + ",";
  sql += std::to_string(record.level) + ",";
  sql += std::to_string(record.experience) + ",";
  sql += std::to_string(record.experienceNext) + ",";
  sql += std::to_string(record.learningPoints) + ",";
  sql += std::to_string(record.healthCurrent) + ",";
  sql += std::to_string(record.healthMax) + ",";
  sql += std::to_string(record.manaCurrent) + ",";
  sql += std::to_string(record.manaMax) + ",";
  sql += std::to_string(record.strength) + ",";
  sql += std::to_string(record.dexterity) + ",";
  sql += std::to_string(record.guild) + ",";
  sql += std::to_string(record.trueGuild) + ",";
  sql += std::to_string(record.permanentAttitude) + ",";
  sql += std::to_string(record.temporaryAttitude) + ",";
  sql += sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",";
  sql += "@event_id);";
  sql += "SELECT BIN_TO_UUID(@event_id, 1);";
  (void)runMysql(target, sql);
}

void createSaveCheckpointManifest(const MySqlTarget& target, const SaveCheckpointManifestRecord& record) {
  std::string sql;
  sql += "SET @manifest_id=NULL; SET @event_id=NULL; SET @row_version_after=NULL;";
  sql += "CALL mmo_create_db_save_checkpoint_v1(";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.manifestKey) + ",";
  sql += sqlLiteral(record.checkpointKind) + ",";
  sql += sqlLiteral(record.reason) + ",";
  sql += std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",";
  sql += "@manifest_id,@event_id,@row_version_after);";
  sql += "SELECT CONCAT(BIN_TO_UUID(@manifest_id,1),'\\t',BIN_TO_UUID(@event_id,1),'\\t',@row_version_after);";
  (void)runMysql(target, sql);
}

void recordClientActionCorrection(const MySqlTarget& target, const ClientActionCorrectionRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @correction_id=NULL;";
  sql += "CALL mmo_record_client_action_correction(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.actionName) + ",";
  sql += std::to_string(record.localSequence) + ",";
  sql += sqlLiteral(record.correctionKind) + ",";
  sql += sqlLiteral(record.reason) + ",";
  sql += std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",";
  sql += "@event_id,@correction_id);";
  sql += "SELECT BIN_TO_UUID(@correction_id,1);";
  (void)runMysql(target, sql);
}

void setCharacterScriptInt(const MySqlTarget& target, const ScriptIntRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @value_after=NULL;";
  sql += "CALL mmo_set_character_script_int(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.scriptKey) + "," + std::to_string(record.symbolIndex) + ",";
  sql += std::to_string(record.valueIndex) + "," + std::to_string(record.valueAfter) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@value_after);";
  (void)runMysql(target, sql);
}

void updateCharacterQuest(const MySqlTarget& target, const QuestUpdateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL;";
  sql += "CALL mmo_update_character_quest(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.questKey) + "," + sqlLiteral(record.questName) + ",";
  sql += sqlLiteral(record.status) + "," + std::to_string(record.entryCount) + ",JSON_ARRAY(),";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id);";
  (void)runMysql(target, sql);
}

void setCharacterKnownDialog(const MySqlTarget& target, const KnownDialogRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL;";
  sql += "CALL mmo_set_character_known_dialog(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.infoKey) + ",";
  sql += sqlBool(record.known);
  sql += ",";
  sql += sqlBool(record.permanent);
  sql += "," + sqlLiteral(record.availability) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id);";
  (void)runMysql(target, sql);
}

void adjustCharacterProgression(const MySqlTarget& target, const ProgressionAdjustmentRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @experience_after=NULL; SET @learning_points_after=NULL;";
  sql += "CALL mmo_adjust_character_progression(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += std::to_string(record.experienceDelta) + "," + std::to_string(record.learningPointsDelta) + ",";
  sql += sqlLiteral(record.reason) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey);
  sql += ",@event_id,@experience_after,@learning_points_after);";
  (void)runMysql(target, sql);
}

void applyCharacterExperienceReward(const MySqlTarget& target, const ExperienceRewardRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @experience_after=NULL;";
  sql += "CALL mmo_apply_character_experience_reward(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += std::to_string(record.experienceDelta) + "," + sqlLiteral(record.reason) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@experience_after);";
  (void)runMysql(target, sql);
}

void applyCharacterDamage(const MySqlTarget& target, const CharacterDamageRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @health_after=NULL;";
  sql += "CALL mmo_apply_character_damage(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.characterKey) + "," + std::to_string(record.damage) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@health_after);";
  (void)runMysql(target, sql);
}

void applyWorldEntityDamage(const MySqlTarget& target, const WorldEntityDamageRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @health_after=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_apply_world_entity_damage(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.entityKey) + "," + std::to_string(record.damage) + ",";
  sql += sqlBool(record.fatal);
  sql += "," + std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@health_after,@row_after);";
  (void)runMysql(target, sql);
}

void markNpcDead(const MySqlTarget& target, const MarkNpcDeadRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_mark_npc_dead(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.entityKey) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordCharacterResourceDelta(const MySqlTarget& target, const CharacterResourceDeltaRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_character_resource_delta(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.characterKey) + "," + sqlLiteral(record.resourceKey) + ",";
  sql += std::to_string(record.delta) + "," + std::to_string(record.valueBefore) + ",";
  sql += std::to_string(record.valueAfter) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordTriggerEvent(const MySqlTarget& target, const TriggerEventRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL;";
  sql += "CALL mmo_record_trigger_event(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.triggerKey) + "," + sqlLiteral(record.eventTypeName) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id);";
  (void)runMysql(target, sql);
}

void recordMoverState(const MySqlTarget& target, const MoverStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_mover_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.moverKey) + "," + std::to_string(record.stateBefore) + ",";
  sql += std::to_string(record.stateAfter) + "," + sqlLiteral(record.stateAfterName) + ",";
  sql += std::to_string(record.frame) + "," + std::to_string(record.targetFrame) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordNpcRoutineState(const MySqlTarget& target, const NpcRoutineStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_routine_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.routineState) + ",";
  sql += sqlLiteral(record.scheduleKey) + "," + sqlLiteral(record.currentWaypoint) + ",";
  sql += sqlLiteral(record.targetWaypoint) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordNpcAiState(const MySqlTarget& target, const NpcAiStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_ai_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.aiState) + ",";
  sql += sqlLiteral(record.aiIntent) + "," + sqlLiteral(record.targetEntity) + ",";
  sql += sqlLiteral(record.perceptionState) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordNpcPathState(const MySqlTarget& target, const NpcPathStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_path_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.pathState) + ",";
  sql += sqlLiteral(record.routeKey) + "," + sqlLiteral(record.currentWaypoint) + ",";
  sql += sqlLiteral(record.nextWaypoint) + "," + sqlLiteral(record.targetWaypoint) + ",";
  sql += PersistenceSql::nullableDouble(record.posX) + "," + PersistenceSql::nullableDouble(record.posY) + "," +
         PersistenceSql::nullableDouble(record.posZ) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordNpcFightState(const MySqlTarget& target, const NpcFightStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_fight_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.opponentKey) + ",";
  sql += sqlLiteral(record.fightState) + "," + sqlLiteral(record.attackState) + ",";
  sql += std::to_string(record.comboIndex) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordTriggerQueueState(const MySqlTarget& target, const TriggerQueueStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_trigger_queue_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.triggerKey) + "," + sqlLiteral(record.queueState) + ",";
  sql += sqlLiteral(record.eventTypeName) + "," + std::to_string(record.scheduledServerTick) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordWorldTransitionState(const MySqlTarget& target, const WorldTransitionStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_world_transition_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.fromWorld) + "," + sqlLiteral(record.toWorld) + ",";
  sql += sqlLiteral(record.transitionState) + "," + sqlLiteral(record.chapterKey) + ",";
  sql += sqlBool(record.visited);
  sql += "," + std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void ackClientActionCorrection(const MySqlTarget& target, const ClientCorrectionAckRecord& record) {
  std::string sql;
  sql += "SET @row_after=NULL;";
  sql += "CALL mmo_ack_client_action_correction(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.actionKind) + "," + std::to_string(record.localSequence) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@row_after);";
  (void)runMysql(target, sql);
}

void recordInteractiveUse(const MySqlTarget& target, const InteractiveUseRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_interactive_use(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.interactiveKey) + "," + std::to_string(record.stateAfter) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void updateInteractiveState(const MySqlTarget& target, const InteractiveStateUpdateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_version_after=NULL;";
  sql += "CALL mmo_update_interactive_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.interactiveKey) + "," + std::to_string(record.stateAfter) + ",";
  sql += std::to_string(record.stateCount) + "," + std::to_string(record.stateMask) + ",";
  sql += sqlBool(record.locked);
  sql += ",";
  sql += sqlBool(record.cracked);
  sql += "," + sqlLiteral(record.lifecycle) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_version_after);";
  (void)runMysql(target, sql);
}

void recordNpcWeaponState(const MySqlTarget& target, const NpcWeaponStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_weapon_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.actorKey) + "," + sqlLiteral(record.weaponState) + ",";
  sql += sqlBool(record.ready);
  sql += "," + std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

} // namespace Mmo::Server
