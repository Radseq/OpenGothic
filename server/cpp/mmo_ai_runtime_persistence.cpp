#include "mmo_ai_runtime_persistence.h"

#include "mmo_server_persistence.h"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace Mmo::AiRuntime {
namespace {

[[nodiscard]] std::string quoteIdentifier(std::string_view value) {
  if(!isSafeMysqlIdentifier(value)) {
    throw std::runtime_error("unsafe MySQL identifier: " + std::string(value));
  }
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('`');
  out.append(value.data(), value.size());
  out.push_back('`');
  return out;
}


[[nodiscard]] std::size_t parseSizeOrZero(std::string_view value) noexcept {
  if(value.empty() || value == "NULL") {
    return 0;
  }
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if(result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    return 0;
  }
  return static_cast<std::size_t>(parsed);
}

[[nodiscard]] std::string optionalWorldFilter(std::string_view worldInstanceUuid, std::string_view tableAlias) {
  if(worldInstanceUuid.empty()) {
    return {};
  }
  std::string out;
  out += " AND ";
  if(!tableAlias.empty()) {
    out += tableAlias;
    out.push_back('.');
  }
  out += "world_instance_uuid=";
  out += Server::sqlLiteral(worldInstanceUuid);
  return out;
}


[[nodiscard]] std::string knownNpcPerceptionActionKindPredicate(std::string_view columnName) {
  std::string out;
  out.reserve(columnName.size() + 256);
  out += columnName;
  out += " IN ('npc_assess_player','npc_turn_to_player','npc_approach_player','npc_greet_player',";
  out += "'npc_warn_player','npc_start_dialog','npc_attack_player','npc_ignore_player','npc_noop')";
  return out;
}

[[nodiscard]] std::string duePendingPredicate(std::string_view tableAlias) {
  std::string prefix;
  if(!tableAlias.empty()) {
    prefix += tableAlias;
    prefix.push_back('.');
  }
  std::string out;
  out += prefix + "action_status='pending'";
  out += " AND " + prefix + "attempt_count<" + prefix + "max_attempts";
  out += " AND (" + prefix + "next_attempt_at IS NULL OR " + prefix + "next_attempt_at<=CURRENT_TIMESTAMP(6))";
  return out;
}

[[nodiscard]] std::string payloadHasPathPredicate(std::string_view columnName, std::string_view path) {
  std::string out;
  out += "JSON_CONTAINS_PATH(";
  out += columnName;
  out += ",'one','";
  out += path;
  out += "')=1";
  return out;
}

[[nodiscard]] std::string validDispatchShapePredicate(std::string_view tableAlias) {
  std::string prefix;
  if(!tableAlias.empty()) {
    prefix += tableAlias;
    prefix.push_back('.');
  }
  const std::string payloadColumn = prefix + "request_payload";
  std::string out;
  out += knownNpcPerceptionActionKindPredicate(prefix + "action_kind");
  out += " AND COALESCE(" + prefix + "world_instance_uuid,'')<>''";
  out += " AND COALESCE(" + prefix + "target_key,'')<>''";
  out += " AND JSON_TYPE(" + payloadColumn + ")='OBJECT'";
  out += " AND " + payloadHasPathPredicate(payloadColumn, "$.decision_uuid");
  out += " AND " + payloadHasPathPredicate(payloadColumn, "$.npc_entity_key");
  out += " AND " + payloadHasPathPredicate(payloadColumn, "$.perception_kind");
  return out;
}

[[nodiscard]] std::string effectiveSessionUuid(
    const RecordNpcPerceptionDecisionOptions& options,
    const NpcPerception::DecisionCandidate& decision) {
  if(!options.sessionUuid.empty()) {
    return options.sessionUuid;
  }
  return decision.sessionUuid;
}

[[nodiscard]] std::string effectiveCharacterUuid(
    const RecordNpcPerceptionDecisionOptions& options,
    const NpcPerception::DecisionCandidate& decision) {
  if(!options.characterUuid.empty()) {
    return options.characterUuid;
  }
  return decision.characterUuid;
}

} // namespace

bool isSafeMysqlIdentifier(std::string_view value) noexcept {
  if(value.empty()) {
    return false;
  }
  for(const unsigned char c : value) {
    if(std::isalnum(c) == 0 && c != '_') {
      return false;
    }
  }
  return true;
}

RecordedNpcPerceptionDecision recordNpcPerceptionDecision(
    const Server::MySqlTarget& target,
    const RecordNpcPerceptionDecisionOptions& options,
    const NpcPerception::DecisionCandidate& decision) {
  if(options.worldInstanceUuid.empty()) {
    throw std::runtime_error("world_instance_uuid is required to record NPC perception decision");
  }
  if(decision.npcEntityKey.empty()) {
    throw std::runtime_error("npc_entity_key is required to record NPC perception decision");
  }
  if(decision.targetKey.empty()) {
    throw std::runtime_error("target_key is required to record NPC perception decision");
  }
  if(decision.perceptionKind.empty()) {
    throw std::runtime_error("perception_kind is required to record NPC perception decision");
  }
  if(decision.idempotencyKey.empty()) {
    throw std::runtime_error("idempotency_key is required to record NPC perception decision");
  }

  const std::string db = quoteIdentifier(options.aiDatabaseName.empty() ? "mmo_ai_runtime" : options.aiDatabaseName);
  const std::string payload = NpcPerception::decisionPayloadJson(decision);
  const std::string sessionUuid = effectiveSessionUuid(options, decision);
  const std::string characterUuid = effectiveCharacterUuid(options, decision);

  std::string sql;
  sql += "SET @mmo_ai_decision_id=NULL;";
  sql += "SET @mmo_ai_decision_status=NULL;";
  sql += "SET @mmo_ai_action_queue_id=NULL;";
  sql += "CALL " + db + ".mmo_ai_record_npc_perception_decision(";
  sql += Server::sqlLiteral(options.worldInstanceUuid) + ",";
  sql += Server::sqlLiteral(sessionUuid) + ",";
  sql += Server::sqlLiteral(characterUuid) + ",";
  sql += Server::sqlLiteral(decision.characterKey) + ",";
  sql += Server::sqlLiteral(decision.npcEntityKey) + ",";
  sql += Server::sqlLiteral(decision.targetKey) + ",";
  sql += Server::sqlLiteral(decision.contentRevisionKey) + ",";
  sql += Server::sqlLiteral(decision.ruleKey) + ",";
  sql += Server::sqlLiteral(decision.perceptionKind) + ",";
  sql += Server::sqlLiteral(decision.decisionKind) + ",";
  sql += std::to_string(decision.priorityValue) + ",";
  sql += std::to_string(decision.serverTick) + ",";
  sql += std::to_string(decision.cooldownTicks) + ",";
  sql += Server::sqlBool(decision.enqueueAction);
  sql += ",";
  sql += Server::sqlJson(payload);
  sql += ",";
  sql += Server::sqlLiteral(decision.idempotencyKey);
  sql += ",@mmo_ai_decision_id,@mmo_ai_decision_status,@mmo_ai_action_queue_id);";
  sql += "SELECT COALESCE(BIN_TO_UUID(@mmo_ai_decision_id,1),''),";
  sql += "COALESCE(@mmo_ai_decision_status,''),";
  sql += "COALESCE(BIN_TO_UUID(@mmo_ai_action_queue_id,1),'');";

  const auto parts = Server::splitMysqlLastRow(Server::runMysql(target, sql));
  RecordedNpcPerceptionDecision out;
  if(!parts.empty() && parts[0] != "NULL") {
    out.decisionUuid = parts[0];
  }
  if(parts.size() > 1 && parts[1] != "NULL") {
    out.decisionStatus = parts[1];
  }
  if(parts.size() > 2 && parts[2] != "NULL") {
    out.actionQueueUuid = parts[2];
  }
  return out;
}


NpcPerceptionActionQueueInspection inspectNpcPerceptionActionQueue(
    const Server::MySqlTarget& target,
    const NpcPerceptionActionQueueInspectOptions& options) {
  const std::string db = quoteIdentifier(options.aiDatabaseName.empty() ? "mmo_ai_runtime" : options.aiDatabaseName);
  const std::string queueTable = db + ".`npc_perception_action_queue`";
  const std::string logTable = db + ".`npc_perception_action_dispatch_log`";
  const std::string worldFilter = optionalWorldFilter(options.worldInstanceUuid, "q");
  const std::string unaliasedWorldFilter = optionalWorldFilter(options.worldInstanceUuid, "");

  std::string sql;
  sql += "SELECT ";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE 1=1" + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE action_status='pending'" + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE action_status='pending' AND attempt_count<max_attempts AND (next_attempt_at IS NULL OR next_attempt_at<=CURRENT_TIMESTAMP(6))" + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE action_status='pending' AND next_attempt_at IS NOT NULL AND next_attempt_at>CURRENT_TIMESTAMP(6)" + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE action_status='claimed'" + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE action_status='applied'" + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE action_status='failed'" + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE action_status='skipped'" + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + logTable + " l JOIN " + queueTable + " q ON q.action_queue_id=l.action_queue_id WHERE 1=1" + worldFilter + "),0),";
  sql += "COALESCE((SELECT JSON_ARRAYAGG(JSON_OBJECT(";
  sql += "'action_queue_uuid',BIN_TO_UUID(p.action_queue_id,1),";
  sql += "'decision_uuid',BIN_TO_UUID(p.decision_id,1),";
  sql += "'world_instance_uuid',p.world_instance_uuid,";
  sql += "'session_uuid',p.session_uuid,";
  sql += "'character_uuid',p.character_uuid,";
  sql += "'action_kind',p.action_kind,";
  sql += "'target_key',p.target_key,";
  sql += "'priority_value',p.priority_value,";
  sql += "'attempt_count',p.attempt_count,";
  sql += "'max_attempts',p.max_attempts,";
  sql += "'idempotency_key',p.idempotency_key,";
  sql += "'request_payload',p.request_payload,";
  sql += "'created_at',DATE_FORMAT(p.created_at,'%Y-%m-%dT%H:%i:%s.%fZ')";
  sql += ")) FROM (SELECT * FROM " + queueTable;
  sql += " WHERE action_status='pending' AND attempt_count<max_attempts AND (next_attempt_at IS NULL OR next_attempt_at<=CURRENT_TIMESTAMP(6))";
  sql += unaliasedWorldFilter;
  sql += " ORDER BY priority_value ASC,created_at ASC,action_queue_id ASC LIMIT ";
  sql += std::to_string(options.maxPendingRows);
  sql += ") p),JSON_ARRAY());";

  const auto parts = Server::splitMysqlLastRow(Server::runMysql(target, sql));
  NpcPerceptionActionQueueInspection out;
  if(parts.size() > 0) {
    out.totalCount = parseSizeOrZero(parts[0]);
  }
  if(parts.size() > 1) {
    out.pendingCount = parseSizeOrZero(parts[1]);
  }
  if(parts.size() > 2) {
    out.duePendingCount = parseSizeOrZero(parts[2]);
  }
  if(parts.size() > 3) {
    out.delayedRetryCount = parseSizeOrZero(parts[3]);
  }
  if(parts.size() > 4) {
    out.claimedCount = parseSizeOrZero(parts[4]);
  }
  if(parts.size() > 5) {
    out.appliedCount = parseSizeOrZero(parts[5]);
  }
  if(parts.size() > 6) {
    out.failedCount = parseSizeOrZero(parts[6]);
  }
  if(parts.size() > 7) {
    out.skippedCount = parseSizeOrZero(parts[7]);
  }
  if(parts.size() > 8) {
    out.dispatchLogCount = parseSizeOrZero(parts[8]);
  }
  if(parts.size() > 9 && parts[9] != "NULL" && !parts[9].empty()) {
    out.pendingActionsJson = parts[9];
  }
  return out;
}


NpcPerceptionActionDispatchContractInspection inspectNpcPerceptionActionDispatchContracts(
    const Server::MySqlTarget& target,
    const NpcPerceptionActionDispatchContractInspectOptions& options) {
  const std::string db = quoteIdentifier(options.aiDatabaseName.empty() ? "mmo_ai_runtime" : options.aiDatabaseName);
  const std::string queueTable = db + ".`npc_perception_action_queue`";
  const std::string worldFilter = optionalWorldFilter(options.worldInstanceUuid, "q");
  const std::string unaliasedWorldFilter = optionalWorldFilter(options.worldInstanceUuid, "");
  const std::string dueAliased = duePendingPredicate("q");
  const std::string dueUnaliased = duePendingPredicate("");
  const std::string knownAliased = knownNpcPerceptionActionKindPredicate("q.action_kind");
  const std::string validAliased = validDispatchShapePredicate("q");
  const std::string validUnaliased = validDispatchShapePredicate("");

  std::string sql;
  sql += "SELECT ";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND " + knownAliased + "),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND NOT (" + knownAliased + ")),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND COALESCE(q.world_instance_uuid,'')=''),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND COALESCE(q.target_key,'')=''),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND JSON_TYPE(q.request_payload)<>'OBJECT'),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND JSON_CONTAINS_PATH(q.request_payload,'one','$.decision_uuid')<>1),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND JSON_CONTAINS_PATH(q.request_payload,'one','$.npc_entity_key')<>1),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND JSON_CONTAINS_PATH(q.request_payload,'one','$.perception_kind')<>1),0),";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND " + validAliased + "),0),";
  sql += "0,";
  sql += "COALESCE((SELECT COUNT(*) FROM " + queueTable + " q WHERE " + dueAliased + worldFilter + " AND " + validAliased + "),0),";
  sql += "COALESCE((SELECT JSON_ARRAYAGG(JSON_OBJECT(";
  sql += "'action_queue_uuid',BIN_TO_UUID(p.action_queue_id,1),";
  sql += "'decision_uuid',BIN_TO_UUID(p.decision_id,1),";
  sql += "'world_instance_uuid',p.world_instance_uuid,";
  sql += "'action_kind',p.action_kind,";
  sql += "'target_key',p.target_key,";
  sql += "'idempotency_key',p.idempotency_key,";
  sql += "'known_action_kind',IF(" + knownNpcPerceptionActionKindPredicate("p.action_kind") + ",true,false),";
  sql += "'has_world_instance_uuid',IF(COALESCE(p.world_instance_uuid,'')<>'',true,false),";
  sql += "'has_target_key',IF(COALESCE(p.target_key,'')<>'',true,false),";
  sql += "'payload_is_object',IF(JSON_TYPE(p.request_payload)='OBJECT',true,false),";
  sql += "'payload_has_decision_uuid',IF(JSON_CONTAINS_PATH(p.request_payload,'one','$.decision_uuid')=1,true,false),";
  sql += "'payload_has_npc_entity_key',IF(JSON_CONTAINS_PATH(p.request_payload,'one','$.npc_entity_key')=1,true,false),";
  sql += "'payload_has_perception_kind',IF(JSON_CONTAINS_PATH(p.request_payload,'one','$.perception_kind')=1,true,false)";
  sql += ")) FROM (SELECT * FROM " + queueTable;
  sql += " WHERE " + dueUnaliased;
  sql += unaliasedWorldFilter;
  sql += " AND NOT (" + validUnaliased + ")";
  sql += " ORDER BY priority_value ASC,created_at ASC,action_queue_id ASC LIMIT ";
  sql += std::to_string(options.maxInvalidRows);
  sql += ") p),JSON_ARRAY());";

  const auto parts = Server::splitMysqlLastRow(Server::runMysql(target, sql));
  NpcPerceptionActionDispatchContractInspection out;
  if(parts.size() > 0) { out.duePendingCount = parseSizeOrZero(parts[0]); }
  if(parts.size() > 1) { out.knownActionKindCount = parseSizeOrZero(parts[1]); }
  if(parts.size() > 2) { out.unknownActionKindCount = parseSizeOrZero(parts[2]); }
  if(parts.size() > 3) { out.missingWorldInstanceUuidCount = parseSizeOrZero(parts[3]); }
  if(parts.size() > 4) { out.missingTargetKeyCount = parseSizeOrZero(parts[4]); }
  if(parts.size() > 5) { out.missingPayloadObjectCount = parseSizeOrZero(parts[5]); }
  if(parts.size() > 6) { out.missingPayloadDecisionUuidCount = parseSizeOrZero(parts[6]); }
  if(parts.size() > 7) { out.missingPayloadNpcEntityKeyCount = parseSizeOrZero(parts[7]); }
  if(parts.size() > 8) { out.missingPayloadPerceptionKindCount = parseSizeOrZero(parts[8]); }
  if(parts.size() > 9) { out.validShapeCount = parseSizeOrZero(parts[9]); }
  if(parts.size() > 10) { out.liveDispatchImplementedCount = parseSizeOrZero(parts[10]); }
  if(parts.size() > 11) { out.liveDispatchBlockedCount = parseSizeOrZero(parts[11]); }
  if(parts.size() > 12 && parts[12] != "NULL" && !parts[12].empty()) {
    out.invalidActionsJson = parts[12];
  }
  return out;
}

ClaimedNpcPerceptionAction claimNextNpcPerceptionAction(
    const Server::MySqlTarget& target,
    const ClaimNpcPerceptionActionOptions& options) {
  if(options.workerId.empty()) {
    throw std::runtime_error("worker_id is required to claim NPC perception action");
  }
  const std::string db = quoteIdentifier(options.aiDatabaseName.empty() ? "mmo_ai_runtime" : options.aiDatabaseName);

  std::string sql;
  sql += "SET @mmo_ai_action_queue_id=NULL;";
  sql += "SET @mmo_ai_decision_id=NULL;";
  sql += "SET @mmo_ai_action_kind=NULL;";
  sql += "SET @mmo_ai_world_instance_uuid=NULL;";
  sql += "SET @mmo_ai_session_uuid=NULL;";
  sql += "SET @mmo_ai_character_uuid=NULL;";
  sql += "SET @mmo_ai_target_key=NULL;";
  sql += "SET @mmo_ai_idempotency_key=NULL;";
  sql += "SET @mmo_ai_request_payload=NULL;";
  sql += "CALL " + db + ".mmo_ai_claim_next_npc_perception_action(";
  sql += Server::sqlLiteral(options.workerId);
  sql += ",@mmo_ai_action_queue_id,@mmo_ai_decision_id,@mmo_ai_action_kind,";
  sql += "@mmo_ai_world_instance_uuid,@mmo_ai_session_uuid,@mmo_ai_character_uuid,";
  sql += "@mmo_ai_target_key,@mmo_ai_idempotency_key,@mmo_ai_request_payload);";
  sql += "SELECT COALESCE(BIN_TO_UUID(@mmo_ai_action_queue_id,1),''),";
  sql += "COALESCE(BIN_TO_UUID(@mmo_ai_decision_id,1),''),";
  sql += "COALESCE(@mmo_ai_action_kind,''),";
  sql += "COALESCE(@mmo_ai_world_instance_uuid,''),";
  sql += "COALESCE(@mmo_ai_session_uuid,''),";
  sql += "COALESCE(@mmo_ai_character_uuid,''),";
  sql += "COALESCE(@mmo_ai_target_key,''),";
  sql += "COALESCE(@mmo_ai_idempotency_key,''),";
  sql += "COALESCE(CAST(@mmo_ai_request_payload AS CHAR),'{}');";

  const auto parts = Server::splitMysqlLastRow(Server::runMysql(target, sql));
  ClaimedNpcPerceptionAction out;
  if(!parts.empty() && parts[0] != "NULL" && !parts[0].empty()) {
    out.claimed = true;
    out.actionQueueUuid = parts[0];
  }
  if(parts.size() > 1 && parts[1] != "NULL") {
    out.decisionUuid = parts[1];
  }
  if(parts.size() > 2 && parts[2] != "NULL") {
    out.actionKind = parts[2];
  }
  if(parts.size() > 3 && parts[3] != "NULL") {
    out.worldInstanceUuid = parts[3];
  }
  if(parts.size() > 4 && parts[4] != "NULL") {
    out.sessionUuid = parts[4];
  }
  if(parts.size() > 5 && parts[5] != "NULL") {
    out.characterUuid = parts[5];
  }
  if(parts.size() > 6 && parts[6] != "NULL") {
    out.targetKey = parts[6];
  }
  if(parts.size() > 7 && parts[7] != "NULL") {
    out.idempotencyKey = parts[7];
  }
  if(parts.size() > 8 && parts[8] != "NULL" && !parts[8].empty()) {
    out.requestPayloadJson = parts[8];
  }
  return out;
}

SkippedNpcPerceptionAction skipNpcPerceptionAction(
    const Server::MySqlTarget& target,
    const SkipNpcPerceptionActionOptions& options) {
  if(options.actionQueueUuid.empty()) {
    throw std::runtime_error("action_queue_uuid is required to skip NPC perception action");
  }
  if(options.workerId.empty()) {
    throw std::runtime_error("worker_id is required to skip NPC perception action");
  }
  const std::string db = quoteIdentifier(options.aiDatabaseName.empty() ? "mmo_ai_runtime" : options.aiDatabaseName);

  std::string sql;
  sql += "SET @mmo_ai_action_status=NULL;";
  sql += "CALL " + db + ".mmo_ai_skip_npc_perception_action(";
  sql += "UUID_TO_BIN(" + Server::sqlLiteral(options.actionQueueUuid) + ",1),";
  sql += Server::sqlLiteral(options.workerId) + ",";
  sql += Server::sqlLiteral(options.reason) + ",";
  sql += "@mmo_ai_action_status);";
  sql += "SELECT COALESCE(@mmo_ai_action_status,'');";

  const auto parts = Server::splitMysqlLastRow(Server::runMysql(target, sql));
  SkippedNpcPerceptionAction out;
  if(!parts.empty() && parts[0] != "NULL") {
    out.actionStatus = parts[0];
  }
  return out;
}


} // namespace Mmo::AiRuntime



