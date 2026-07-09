#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "mmo_ai_dialog_intent_delivery_persistence_bridge.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryPersistenceRuntimeStorageStatus : std::uint8_t {
  Ready = 0,
  Disabled,
  InvalidAiDatabaseName,
  MissingMysqlExecutionPermission,
  MissingRequiredIdentity,
  MissingWorldInstanceUuid,
  UnsupportedReceiptKind,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceRuntimeStorageStatusName(
    AiDialogIntentDeliveryPersistenceRuntimeStorageStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Ready: return "ready";
    case AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Disabled: return "disabled";
    case AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::InvalidAiDatabaseName: return "invalid_ai_database_name";
    case AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingMysqlExecutionPermission: return "missing_mysql_execution_permission";
    case AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingRequiredIdentity: return "missing_required_identity";
    case AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingWorldInstanceUuid: return "missing_world_instance_uuid";
    case AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::UnsupportedReceiptKind: return "unsupported_receipt_kind";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryPersistenceSentStorageRequest final {
  bool enabled = false;
  bool allowMysqlExecution = false;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string source = "step281_runtime_storage";
  std::string deliveryKind = "initial";
  std::string worldInstanceUuid;
  std::string worldName;
  std::string contentRevisionKey;
  std::string conversationId;
  std::string speakerEntityKey;
  std::string speakerNpcInstanceUuid;
  std::string lineId;
  std::string audioRef;
  std::string text;
  std::string reason;
  std::string actionId;
  std::string ackKey;
  std::string targetSessionUuid;
  std::string targetCharacterUuid;
  std::string targetCharacterKey;
  std::string endpointText;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  std::uint64_t startTick = 0;
  std::uint32_t durationMs = 0;
  std::size_t plannedRecipients = 0;
  std::size_t payloadBytes = 0;
  std::uint64_t ackTimeoutMs = 0;
  double distanceSquared = 0.0;
  bool recipientIsTarget = true;
  bool recipientHasPosition = false;
};

struct AiDialogIntentDeliveryPersistenceReceiptStorageRequest final {
  bool enabled = false;
  bool allowMysqlExecution = false;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string source = "step281_runtime_storage";
  std::string actionId;
  std::string ackKey;
  std::string receiptKind;
  std::string sessionUuid;
  std::string characterUuid;
  std::string characterKey;
  std::string clientObservationStatus;
  std::string reason;
  std::string messageText;
  std::uint32_t flags = 0;
  bool uiApplied = false;
  bool audioApplied = false;
};

struct AiDialogIntentDeliveryPersistenceTimeoutStorageRequest final {
  bool enabled = false;
  bool allowMysqlExecution = false;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string source = "step281_runtime_storage";
};

struct AiDialogIntentDeliveryPersistenceRuntimeStorageSql final {
  AiDialogIntentDeliveryPersistenceRuntimeStorageStatus status =
      AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Disabled;
  bool ready = false;
  bool executeMysql = false;
  bool wouldCallRunMysql = false;
  bool wouldMutateDb = false;
  std::string reason;
  std::string actionId;
  std::string ackKey;
  std::string sql;
};

namespace PersistenceRuntimeStorageDetail {

[[nodiscard]] inline std::string boolJson(bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] inline std::string runtimeSentPayloadJson(
    const AiDialogIntentDeliveryPersistenceSentStorageRequest& request) {
  std::string out;
  out.reserve(768);
  out += "{";
  out += "\"source\":" + Detail::jsonEscape(request.source.empty() ? std::string_view("step281_runtime_storage") : std::string_view(request.source));
  out += ",\"conversation_key\":" + Detail::jsonEscape(request.conversationId);
  out += ",\"action_id\":" + Detail::jsonEscape(request.actionId);
  out += ",\"ack_key\":" + Detail::jsonEscape(request.ackKey);
  out += ",\"session_uuid\":" + Detail::jsonEscape(request.targetSessionUuid);
  out += ",\"character_key\":" + Detail::jsonEscape(request.targetCharacterKey);
  out += ",\"speaker_entity_key\":" + Detail::jsonEscape(request.speakerEntityKey);
  out += ",\"line_id\":" + Detail::jsonEscape(request.lineId);
  out += ",\"audio_ref\":" + Detail::jsonEscape(request.audioRef);
  out += ",\"text\":" + Detail::jsonEscape(request.text);
  out += ",\"reason\":" + Detail::jsonEscape(request.reason);
  out += ",\"packet_sequence\":" + Detail::sqlUint(request.packetSequence);
  out += ",\"local_sequence\":" + Detail::sqlUint(request.localSequence);
  out += ",\"server_tick\":" + Detail::sqlUint(request.serverTick);
  out += ",\"start_tick\":" + Detail::sqlUint(request.startTick);
  out += ",\"duration_ms\":" + Detail::sqlUint(request.durationMs);
  out += ",\"encoded_bytes\":" + Detail::sqlSize(request.payloadBytes);
  out += ",\"recipient_is_target\":" + boolJson(request.recipientIsTarget);
  out += ",\"recipient_has_position\":" + boolJson(request.recipientHasPosition);
  out += "}";
  return out;
}

[[nodiscard]] inline std::string runtimeEndpointAuditJson(
    const AiDialogIntentDeliveryPersistenceSentStorageRequest& request) {
  std::string out;
  out.reserve(256);
  out += "{";
  out += "\"source\":" + Detail::jsonEscape(request.source.empty() ? std::string_view("step281_runtime_storage") : std::string_view(request.source));
  out += ",\"endpoint\":" + Detail::jsonEscape(request.endpointText);
  out += ",\"udp_send_success\":true";
  out += ",\"mysql_execution\":true";
  out += "}";
  return out;
}

[[nodiscard]] inline std::string runtimeReceiptPayloadJson(
    const AiDialogIntentDeliveryPersistenceReceiptStorageRequest& request) {
  std::string out;
  out.reserve(512);
  out += "{";
  out += "\"source\":" + Detail::jsonEscape(request.source.empty() ? std::string_view("step281_runtime_storage") : std::string_view(request.source));
  out += ",\"action_id\":" + Detail::jsonEscape(request.actionId);
  out += ",\"ack_key\":" + Detail::jsonEscape(request.ackKey);
  out += ",\"receipt_kind\":" + Detail::jsonEscape(request.receiptKind);
  out += ",\"session_uuid\":" + Detail::jsonEscape(request.sessionUuid);
  out += ",\"character_key\":" + Detail::jsonEscape(request.characterKey);
  out += ",\"client_observation_status\":" + Detail::jsonEscape(request.clientObservationStatus);
  out += ",\"flags\":" + Detail::sqlUint(request.flags);
  out += ",\"ui_applied\":" + boolJson(request.uiApplied);
  out += ",\"audio_applied\":" + boolJson(request.audioApplied);
  out += "}";
  return out;
}

[[nodiscard]] inline bool isSupportedReceiptKind(std::string_view kind) noexcept {
  return kind == "acked" || kind == "nacked" || kind == "observed" || kind == "skipped";
}

[[nodiscard]] inline std::string optionalCharacterUuidExpression(std::string_view uuid) {
  return uuid.empty() ? std::string("''") : Detail::sqlLiteral(uuid);
}

} // namespace PersistenceRuntimeStorageDetail

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceRuntimeStorageSql
buildAiDialogIntentDeliveryPersistenceSentStorageSql(
    const AiDialogIntentDeliveryPersistenceSentStorageRequest& request) {
  AiDialogIntentDeliveryPersistenceRuntimeStorageSql out;
  out.actionId = request.actionId;
  out.ackKey = request.ackKey;
  out.executeMysql = request.enabled && request.allowMysqlExecution;
  out.wouldCallRunMysql = out.executeMysql;
  out.wouldMutateDb = request.enabled;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Disabled;
    out.reason = "step281_runtime_storage_disabled";
    return out;
  }
  if(!request.allowMysqlExecution) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingMysqlExecutionPermission;
    out.reason = "step281_runtime_storage_requires_explicit_mysql_execution_permission";
    return out;
  }
  if(!Detail::isSafeMysqlIdentifier(request.aiDatabaseName)) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::InvalidAiDatabaseName;
    out.reason = "step281_runtime_storage_requires_safe_ai_database_identifier";
    return out;
  }
  if(request.worldInstanceUuid.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingWorldInstanceUuid;
    out.reason = "step281_runtime_storage_requires_world_instance_uuid";
    return out;
  }
  if(request.conversationId.empty() || request.actionId.empty() || request.ackKey.empty() ||
     request.targetSessionUuid.empty() || request.targetCharacterKey.empty() || request.packetSequence == 0) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingRequiredIdentity;
    out.reason = "step281_runtime_storage_requires_conversation_action_ack_session_character_and_packet_sequence";
    return out;
  }

  const std::string db = Detail::mysqlIdentifier(request.aiDatabaseName);
  const std::string payloadJson = PersistenceRuntimeStorageDetail::runtimeSentPayloadJson(request);
  const std::string endpointJson = PersistenceRuntimeStorageDetail::runtimeEndpointAuditJson(request);
  const std::string conversationSubquery = "(SELECT conversation_id FROM " + db +
      ".dialog_intent_conversation_sessions WHERE conversation_key=" + Detail::sqlLiteral(request.conversationId) + " LIMIT 1)";

  std::string sql;
  sql.reserve(4600);
  sql += "START TRANSACTION;";
  sql += "INSERT INTO " + db + ".dialog_intent_conversation_sessions (";
  sql += "conversation_key,world_instance_uuid,world_name,content_revision_key,speaker_entity_key,";
  sql += "speaker_npc_instance_uuid,line_id,audio_ref,server_tick,start_tick,duration_ms,planned_recipients,";
  sql += "conversation_status,raw_payload) VALUES (";
  sql += Detail::sqlLiteral(request.conversationId) + ",";
  sql += Detail::sqlLiteral(request.worldInstanceUuid) + ",";
  sql += Detail::sqlLiteral(request.worldName) + ",";
  sql += Detail::sqlLiteral(request.contentRevisionKey) + ",";
  sql += Detail::sqlLiteral(request.speakerEntityKey) + ",";
  sql += Detail::sqlLiteral(request.speakerNpcInstanceUuid) + ",";
  sql += Detail::sqlLiteral(request.lineId) + ",";
  sql += Detail::sqlLiteral(request.audioRef) + ",";
  sql += Detail::sqlUint(request.serverTick) + ",";
  sql += Detail::sqlUint(request.startTick) + ",";
  sql += Detail::sqlUint(request.durationMs) + ",";
  sql += Detail::sqlSize(request.plannedRecipients) + ",";
  sql += "'open',CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON)) ";
  sql += "ON DUPLICATE KEY UPDATE planned_recipients=GREATEST(planned_recipients,VALUES(planned_recipients)),";
  sql += "raw_payload=JSON_MERGE_PATCH(raw_payload,VALUES(raw_payload)),updated_at=CURRENT_TIMESTAMP(6);";

  sql += "SET @step281_delivery_id=NULL; SET @step281_delivery_status=NULL;";
  sql += "CALL " + db + ".mmo_ai_record_gameplay_delivery_sent(";
  sql += "NULL,NULL," + conversationSubquery + ",";
  sql += Detail::sqlLiteral(request.worldInstanceUuid) + ",";
  sql += "'npc_dialog_intent'," + Detail::sqlLiteral(request.deliveryKind.empty() ? std::string_view("initial") : std::string_view(request.deliveryKind)) + ",";
  sql += Detail::sqlLiteral(request.actionId) + ",";
  sql += Detail::sqlLiteral(request.ackKey) + ",";
  sql += Detail::sqlLiteral(request.targetSessionUuid) + ",";
  sql += PersistenceRuntimeStorageDetail::optionalCharacterUuidExpression(request.targetCharacterUuid) + ",";
  sql += Detail::sqlLiteral(request.targetCharacterKey) + ",";
  sql += "'ServerNpcDialogIntent',";
  sql += Detail::sqlUint(request.packetSequence) + ",";
  sql += Detail::sqlUint(request.localSequence) + ",";
  sql += Detail::sqlUint(request.serverTick) + ",";
  sql += "''," + Detail::sqlSize(request.payloadBytes) + ",";
  sql += Detail::sqlUint(request.ackTimeoutMs) + ",";
  sql += "CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON),";
  sql += "CAST(" + Detail::sqlLiteral(endpointJson) + " AS JSON),";
  sql += "@step281_delivery_id,@step281_delivery_status);";

  sql += "INSERT INTO " + db + ".dialog_intent_conversation_observers (";
  sql += "conversation_id,delivery_id,observer_session_uuid,observer_character_uuid,observer_character_key,";
  sql += "observer_kind,observer_status,observation_status,action_id,ack_key,packet_sequence,local_sequence,";
  sql += "distance_squared,has_position,raw_payload,sent_at,ack_deadline_at) SELECT ";
  sql += "conversation_id,@step281_delivery_id,";
  sql += Detail::sqlLiteral(request.targetSessionUuid) + ",";
  sql += PersistenceRuntimeStorageDetail::optionalCharacterUuidExpression(request.targetCharacterUuid) + ",";
  sql += Detail::sqlLiteral(request.targetCharacterKey) + ",";
  sql += (request.recipientIsTarget ? "'target_session'" : "'aoi'");
  sql += ",'pending','none',";
  sql += Detail::sqlLiteral(request.actionId) + ",";
  sql += Detail::sqlLiteral(request.ackKey) + ",";
  sql += Detail::sqlUint(request.packetSequence) + ",";
  sql += Detail::sqlUint(request.localSequence) + ",";
  sql += std::to_string(request.distanceSquared) + ",";
  sql += (request.recipientHasPosition ? "1" : "0");
  sql += ",CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON),CURRENT_TIMESTAMP(6),";
  sql += "TIMESTAMPADD(MICROSECOND," + Detail::sqlUint(request.ackTimeoutMs) + "*1000,CURRENT_TIMESTAMP(6)) ";
  sql += "FROM " + db + ".dialog_intent_conversation_sessions WHERE conversation_key=" + Detail::sqlLiteral(request.conversationId) + " LIMIT 1 ";
  sql += "ON DUPLICATE KEY UPDATE delivery_id=COALESCE(delivery_id,VALUES(delivery_id)),";
  sql += "sent_at=COALESCE(sent_at,VALUES(sent_at)),ack_deadline_at=COALESCE(ack_deadline_at,VALUES(ack_deadline_at)),";
  sql += "updated_at=CURRENT_TIMESTAMP(6);";

  sql += "COMMIT;";
  sql += "SELECT CONCAT(COALESCE(BIN_TO_UUID(@step281_delivery_id,1),''),'\\t',COALESCE(@step281_delivery_status,''));";

  out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Ready;
  out.ready = true;
  out.reason = "step281_runtime_storage_sent_sql_ready";
  out.sql = std::move(sql);
  return out;
}

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceRuntimeStorageSql
buildAiDialogIntentDeliveryPersistenceReceiptStorageSql(
    const AiDialogIntentDeliveryPersistenceReceiptStorageRequest& request) {
  AiDialogIntentDeliveryPersistenceRuntimeStorageSql out;
  out.actionId = request.actionId;
  out.ackKey = request.ackKey;
  out.executeMysql = request.enabled && request.allowMysqlExecution;
  out.wouldCallRunMysql = out.executeMysql;
  out.wouldMutateDb = request.enabled;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Disabled;
    out.reason = "step281_runtime_receipt_storage_disabled";
    return out;
  }
  if(!request.allowMysqlExecution) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingMysqlExecutionPermission;
    out.reason = "step281_runtime_receipt_storage_requires_explicit_mysql_execution_permission";
    return out;
  }
  if(!Detail::isSafeMysqlIdentifier(request.aiDatabaseName)) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::InvalidAiDatabaseName;
    out.reason = "step281_runtime_receipt_storage_requires_safe_ai_database_identifier";
    return out;
  }
  if(!PersistenceRuntimeStorageDetail::isSupportedReceiptKind(request.receiptKind)) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::UnsupportedReceiptKind;
    out.reason = "step281_runtime_receipt_storage_unsupported_receipt_kind";
    return out;
  }
  if(request.actionId.empty() || request.ackKey.empty() || request.sessionUuid.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingRequiredIdentity;
    out.reason = "step281_runtime_receipt_storage_requires_action_ack_and_session_identity";
    return out;
  }

  const std::string db = Detail::mysqlIdentifier(request.aiDatabaseName);
  const std::string payloadJson = PersistenceRuntimeStorageDetail::runtimeReceiptPayloadJson(request);
  std::string sql;
  sql.reserve(1600);
  sql += "START TRANSACTION;";
  sql += "SET @step281_receipt_delivery_id=NULL; SET @step281_receipt_kind=NULL; SET @step281_receipt_delivery_status=NULL;";
  sql += "CALL " + db + ".mmo_ai_record_gameplay_delivery_receipt(";
  sql += Detail::sqlLiteral(request.actionId) + ",";
  sql += Detail::sqlLiteral(request.ackKey) + ",";
  sql += Detail::sqlLiteral(request.receiptKind) + ",";
  sql += Detail::sqlLiteral(request.sessionUuid) + ",";
  sql += PersistenceRuntimeStorageDetail::optionalCharacterUuidExpression(request.characterUuid) + ",";
  sql += Detail::sqlLiteral(request.characterKey) + ",";
  sql += Detail::sqlLiteral(request.clientObservationStatus) + ",";
  sql += Detail::sqlLiteral(request.reason) + ",";
  sql += Detail::sqlLiteral(request.messageText) + ",";
  sql += "CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON),";
  sql += "@step281_receipt_delivery_id,@step281_receipt_kind,@step281_receipt_delivery_status);";
  sql += "COMMIT;";
  sql += "SELECT CONCAT(COALESCE(BIN_TO_UUID(@step281_receipt_delivery_id,1),''),'\\t',COALESCE(@step281_receipt_kind,''),'\\t',COALESCE(@step281_receipt_delivery_status,''));";

  out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Ready;
  out.ready = true;
  out.reason = "step281_runtime_storage_receipt_sql_ready";
  out.sql = std::move(sql);
  return out;
}

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceRuntimeStorageSql
buildAiDialogIntentDeliveryPersistenceTimeoutStorageSql(
    const AiDialogIntentDeliveryPersistenceTimeoutStorageRequest& request) {
  AiDialogIntentDeliveryPersistenceRuntimeStorageSql out;
  out.executeMysql = request.enabled && request.allowMysqlExecution;
  out.wouldCallRunMysql = out.executeMysql;
  out.wouldMutateDb = request.enabled;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Disabled;
    out.reason = "step281_runtime_timeout_storage_disabled";
    return out;
  }
  if(!request.allowMysqlExecution) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::MissingMysqlExecutionPermission;
    out.reason = "step281_runtime_timeout_storage_requires_explicit_mysql_execution_permission";
    return out;
  }
  if(!Detail::isSafeMysqlIdentifier(request.aiDatabaseName)) {
    out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::InvalidAiDatabaseName;
    out.reason = "step281_runtime_timeout_storage_requires_safe_ai_database_identifier";
    return out;
  }

  const std::string db = Detail::mysqlIdentifier(request.aiDatabaseName);
  std::string sql;
  sql.reserve(320);
  const std::string source = request.source.empty() ? std::string("step281_runtime_storage") : request.source;
  sql += "SET @step281_timed_out_count=0;";
  if(source == "step282_late_observer_replay_send") {
    sql += "CALL " + db + ".mmo_ai_mark_step282_late_observer_replay_timed_out(@step281_timed_out_count);";
  } else {
    sql += "CALL " + db + ".mmo_ai_mark_step281_runtime_storage_timed_out(@step281_timed_out_count);";
  }
  sql += "SELECT COALESCE(@step281_timed_out_count,0);";

  out.status = AiDialogIntentDeliveryPersistenceRuntimeStorageStatus::Ready;
  out.ready = true;
  out.reason = "step281_runtime_storage_timeout_sql_ready";
  out.sql = std::move(sql);
  return out;
}

} // namespace Mmo::Server


