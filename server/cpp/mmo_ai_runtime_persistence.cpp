#include "mmo_ai_runtime_persistence.h"

#include "mmo_server_persistence.h"

#include <cctype>
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

} // namespace Mmo::AiRuntime
