#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "mmo_ai_dialog_intent_delivery_persistence_bridge.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryMarkAppliedGateStatus : std::uint8_t {
  Ready = 0,
  Disabled,
  InvalidAiDatabaseName,
  MissingMysqlExecutionPermission,
  MissingRequiredIdentity,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryMarkAppliedGateStatusName(
    AiDialogIntentDeliveryMarkAppliedGateStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryMarkAppliedGateStatus::Ready:                           return "ready";
    case AiDialogIntentDeliveryMarkAppliedGateStatus::Disabled:                        return "disabled";
    case AiDialogIntentDeliveryMarkAppliedGateStatus::InvalidAiDatabaseName:           return "invalid_ai_database_name";
    case AiDialogIntentDeliveryMarkAppliedGateStatus::MissingMysqlExecutionPermission: return "missing_mysql_execution_permission";
    case AiDialogIntentDeliveryMarkAppliedGateStatus::MissingRequiredIdentity:         return "missing_required_identity";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryMarkAppliedGateRequest final {
  bool enabled = false;
  bool allowMysqlExecution = false;
  bool requireObservation = true;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string actionId;
  std::string ackKey;
  std::string workerId = "mmo_udp_server_step283";
  std::string trigger;
  std::string source;
};

struct AiDialogIntentDeliveryMarkAppliedGateSql final {
  AiDialogIntentDeliveryMarkAppliedGateStatus status = AiDialogIntentDeliveryMarkAppliedGateStatus::Disabled;
  bool ready = false;
  bool executeMysql = false;
  bool wouldCallRunMysql = false;
  bool wouldMutateDb = false;
  std::string reason;
  std::string actionId;
  std::string ackKey;
  std::string sql;
};

namespace MarkAppliedGateDetail {

[[nodiscard]] inline std::string gatePayloadJson(const AiDialogIntentDeliveryMarkAppliedGateRequest& request) {
  std::string out;
  out.reserve(384);
  out += "{";
  out += "\"source\":\"step283_durable_mark_applied_gate\"";
  out += ",\"request_source\":" + Detail::jsonEscape(request.source);
  out += ",\"trigger\":" + Detail::jsonEscape(request.trigger);
  out += ",\"action_id\":" + Detail::jsonEscape(request.actionId);
  out += ",\"ack_key\":" + Detail::jsonEscape(request.ackKey);
  out += ",\"require_observation\":" + std::string(request.requireObservation ? "true" : "false");
  out += "}";
  return out;
}

} // namespace MarkAppliedGateDetail

[[nodiscard]] inline AiDialogIntentDeliveryMarkAppliedGateSql buildAiDialogIntentDeliveryMarkAppliedGateSql(
    const AiDialogIntentDeliveryMarkAppliedGateRequest& request) {
  AiDialogIntentDeliveryMarkAppliedGateSql out;
  out.actionId = request.actionId;
  out.ackKey = request.ackKey;
  out.executeMysql = request.enabled && request.allowMysqlExecution;
  out.wouldCallRunMysql = out.executeMysql;
  out.wouldMutateDb = request.enabled;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryMarkAppliedGateStatus::Disabled;
    out.reason = "step283_mark_applied_gate_disabled";
    return out;
  }
  if(!request.allowMysqlExecution) {
    out.status = AiDialogIntentDeliveryMarkAppliedGateStatus::MissingMysqlExecutionPermission;
    out.reason = "step283_mark_applied_gate_requires_explicit_mysql_execution_permission";
    return out;
  }
  if(!Detail::isSafeMysqlIdentifier(request.aiDatabaseName)) {
    out.status = AiDialogIntentDeliveryMarkAppliedGateStatus::InvalidAiDatabaseName;
    out.reason = "step283_mark_applied_gate_requires_safe_ai_database_identifier";
    return out;
  }
  if(request.actionId.empty() || request.ackKey.empty()) {
    out.status = AiDialogIntentDeliveryMarkAppliedGateStatus::MissingRequiredIdentity;
    out.reason = "step283_mark_applied_gate_requires_action_and_ack_identity";
    return out;
  }

  const std::string db = Detail::mysqlIdentifier(request.aiDatabaseName);
  const std::string payloadJson = MarkAppliedGateDetail::gatePayloadJson(request);
  std::string sql;
  sql.reserve(1100);
  sql += "SET @step283_gate_status=NULL;";
  sql += "SET @step283_action_status=NULL;";
  sql += "SET @step283_action_queue_uuid=NULL;";
  sql += "CALL " + db + ".mmo_ai_mark_dialog_intent_action_applied_if_terminal(";
  sql += Detail::sqlLiteral(request.actionId) + ",";
  sql += Detail::sqlLiteral(request.ackKey) + ",";
  sql += Detail::sqlLiteral(request.workerId.empty() ? std::string_view("mmo_udp_server_step283") : std::string_view(request.workerId)) + ",";
  sql += (request.requireObservation ? "1" : "0");
  sql += ",CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON),";
  sql += "@step283_gate_status,@step283_action_status,@step283_action_queue_uuid);";
  sql += "SELECT CONCAT(COALESCE(@step283_gate_status,''),'\\t',COALESCE(@step283_action_status,''),'\\t',COALESCE(@step283_action_queue_uuid,''));";

  out.status = AiDialogIntentDeliveryMarkAppliedGateStatus::Ready;
  out.ready = true;
  out.reason = "step283_mark_applied_gate_sql_ready";
  out.sql = std::move(sql);
  return out;
}

} // namespace Mmo::Server

