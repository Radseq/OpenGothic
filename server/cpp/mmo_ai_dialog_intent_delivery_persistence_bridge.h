#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mmo_server_conversation_resume_commit_preflight.h"
#include "mmo_server_conversation_resume_delivery_registration_boundary.h"
#include "mmo_server_conversation_resume_dispatch_envelope.h"
#include "mmo_server_conversation_resume_packet_boundary.h"
#include "mmo_server_conversation_session_boundary.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryPersistencePreviewStatus : std::uint8_t {
  ReadyNoExecute = 0,
  PreviewDisabled,
  InvalidAiDatabaseName,
  MissingCommitPreflight,
  CommitPreflightNotReady,
  MissingResumePlan,
  MissingPacketBoundary,
  PacketBoundaryNotBuildable,
  MissingDeliveryRegistration,
  DeliveryRegistrationNotEligible,
  MissingDispatchEnvelope,
  DispatchEnvelopeNotEligible,
  IdentityMismatch,
  MissingRequiredIdentity,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistencePreviewStatusName(
    AiDialogIntentDeliveryPersistencePreviewStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistencePreviewStatus::ReadyNoExecute:                 return "ready_no_execute";
    case AiDialogIntentDeliveryPersistencePreviewStatus::PreviewDisabled:                return "preview_disabled";
    case AiDialogIntentDeliveryPersistencePreviewStatus::InvalidAiDatabaseName:          return "invalid_ai_database_name";
    case AiDialogIntentDeliveryPersistencePreviewStatus::MissingCommitPreflight:         return "missing_commit_preflight";
    case AiDialogIntentDeliveryPersistencePreviewStatus::CommitPreflightNotReady:        return "commit_preflight_not_ready";
    case AiDialogIntentDeliveryPersistencePreviewStatus::MissingResumePlan:              return "missing_resume_plan";
    case AiDialogIntentDeliveryPersistencePreviewStatus::MissingPacketBoundary:          return "missing_packet_boundary";
    case AiDialogIntentDeliveryPersistencePreviewStatus::PacketBoundaryNotBuildable:     return "packet_boundary_not_buildable";
    case AiDialogIntentDeliveryPersistencePreviewStatus::MissingDeliveryRegistration:    return "missing_delivery_registration";
    case AiDialogIntentDeliveryPersistencePreviewStatus::DeliveryRegistrationNotEligible:return "delivery_registration_not_eligible";
    case AiDialogIntentDeliveryPersistencePreviewStatus::MissingDispatchEnvelope:        return "missing_dispatch_envelope";
    case AiDialogIntentDeliveryPersistencePreviewStatus::DispatchEnvelopeNotEligible:    return "dispatch_envelope_not_eligible";
    case AiDialogIntentDeliveryPersistencePreviewStatus::IdentityMismatch:               return "identity_mismatch";
    case AiDialogIntentDeliveryPersistencePreviewStatus::MissingRequiredIdentity:        return "missing_required_identity";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryPersistenceStatementPreview final {
  std::string name;
  std::string sql;
  bool mutating = true;
  bool procedureCall = false;
};

struct AiDialogIntentDeliveryPersistencePreviewRequest final {
  bool enabled = false;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string worldInstanceUuid;
  std::string worldName;
  std::string contentRevisionKey;
  std::uint64_t ackTimeoutMs = 0;
  const ConversationLateObserverResumePlan* resumePlan = nullptr;
  const ConversationResumePacketBuildResult* packetBoundary = nullptr;
  const ConversationResumeDeliveryRegistrationResult* deliveryRegistration = nullptr;
  const ConversationResumeDispatchEnvelopeResult* dispatchEnvelope = nullptr;
  const ConversationResumeCommitPreflightResult* commitPreflight = nullptr;
};

struct AiDialogIntentDeliveryPersistencePreviewResult final {
  AiDialogIntentDeliveryPersistencePreviewStatus status = AiDialogIntentDeliveryPersistencePreviewStatus::PreviewDisabled;
  bool ready = false;
  bool executeMysql = false;
  bool mutatedDb = false;
  bool containsMutatingSql = true;
  bool requiresExistingStep273Schema = true;
  std::string reason;
  std::string aiDatabaseName;
  std::string conversationKey;
  std::string actionId;
  std::string ackKey;
  std::string sessionUuid;
  std::string characterKey;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  std::uint64_t ackTimeoutMs = 0;
  std::size_t payloadBytes = 0;
  std::vector<AiDialogIntentDeliveryPersistenceStatementPreview> statements;
};

namespace Detail {

[[nodiscard]] constexpr bool isIdentStart(char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

[[nodiscard]] constexpr bool isIdentContinue(char ch) noexcept {
  return isIdentStart(ch) || (ch >= '0' && ch <= '9');
}

[[nodiscard]] inline bool isSafeMysqlIdentifier(std::string_view name) noexcept {
  if(name.empty() || !isIdentStart(name.front()))
    return false;
  for(char ch : name) {
    if(!isIdentContinue(ch))
      return false;
  }
  return true;
}

[[nodiscard]] inline std::string mysqlIdentifier(std::string_view name) {
  std::string out;
  out.reserve(name.size() + 2);
  out.push_back('`');
  out.append(name);
  out.push_back('`');
  return out;
}

[[nodiscard]] inline std::string sqlLiteral(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('\'');
  for(char ch : text) {
    if(ch == '\'')
      out += "''";
    else
      out.push_back(ch);
  }
  out.push_back('\'');
  return out;
}

[[nodiscard]] inline std::string sqlBool(bool value) {
  return value ? "1" : "0";
}

[[nodiscard]] inline std::string sqlUint(std::uint64_t value) {
  return std::to_string(value);
}

[[nodiscard]] inline std::string sqlSize(std::size_t value) {
  return std::to_string(value);
}

[[nodiscard]] inline std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for(char ch : text) {
    switch(ch) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if(static_cast<unsigned char>(ch) < 0x20)
          out += " ";
        else
          out.push_back(ch);
        break;
    }
  }
  out.push_back('"');
  return out;
}

[[nodiscard]] inline std::string previewJsonPayload(const AiDialogIntentDeliveryPersistencePreviewRequest& request) {
  const auto& packet = request.packetBoundary->packet;
  std::string out;
  out.reserve(512);
  out += "{";
  out += "\"source\":\"step274_persistence_preview_bridge\"";
  out += ",\"conversation_key\":" + jsonEscape(packet.conversationId);
  out += ",\"action_id\":" + jsonEscape(packet.actionId);
  out += ",\"ack_key\":" + jsonEscape(packet.ackKey);
  out += ",\"session_uuid\":" + jsonEscape(packet.sessionUuid);
  out += ",\"character_key\":" + jsonEscape(packet.targetCharacterKey);
  out += ",\"speaker_entity_key\":" + jsonEscape(packet.speakerEntityKey);
  out += ",\"line_id\":" + jsonEscape(packet.lineId);
  out += ",\"audio_ref\":" + jsonEscape(packet.audioRef);
  out += ",\"packet_sequence\":" + sqlUint(packet.packetSequence);
  out += ",\"local_sequence\":" + sqlUint(packet.localSequence);
  out += ",\"server_tick\":" + sqlUint(packet.serverTick);
  out += ",\"duration_ms\":" + sqlUint(packet.durationMs);
  out += ",\"remaining_ms\":" + sqlUint(request.resumePlan == nullptr ? 0 : request.resumePlan->remainingMs);
  out += ",\"encoded_bytes\":" + sqlSize(request.dispatchEnvelope == nullptr ? request.packetBoundary->encodedBytes : request.dispatchEnvelope->encodedBytes);
  out += ",\"execute_mysql\":false";
  out += "}";
  return out;
}

[[nodiscard]] inline std::string endpointAuditJson(const AiDialogIntentDeliveryPersistencePreviewRequest& request) {
  std::string out;
  out.reserve(256);
  out += "{";
  out += "\"source\":\"step274_persistence_preview_bridge\"";
  out += ",\"endpoint\":" + jsonEscape(request.dispatchEnvelope == nullptr ? std::string_view{} : std::string_view(request.dispatchEnvelope->endpointText));
  out += ",\"would_dispatch_udp\":" + std::string(request.commitPreflight != nullptr && request.commitPreflight->wouldDispatchUdp ? "true" : "false");
  out += ",\"execute_mysql\":false";
  out += "}";
  return out;
}

[[nodiscard]] inline bool same(std::string_view a, std::string_view b) noexcept {
  return a == b;
}

} // namespace Detail

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceStatementNamesCsv(
    const AiDialogIntentDeliveryPersistencePreviewResult& preview) {
  std::string out;
  for(const auto& statement : preview.statements) {
    if(!out.empty())
      out.push_back(',');
    out += statement.name;
  }
  return out;
}

[[nodiscard]] inline AiDialogIntentDeliveryPersistencePreviewResult buildAiDialogIntentDeliveryPersistencePreview(
    const AiDialogIntentDeliveryPersistencePreviewRequest& request) {
  AiDialogIntentDeliveryPersistencePreviewResult out;
  out.executeMysql = false;
  out.mutatedDb = false;
  out.aiDatabaseName = request.aiDatabaseName;
  out.ackTimeoutMs = request.ackTimeoutMs;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::PreviewDisabled;
    out.reason = "step273_persistence_preview_disabled";
    return out;
  }
  if(!Detail::isSafeMysqlIdentifier(request.aiDatabaseName)) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::InvalidAiDatabaseName;
    out.reason = "step273_persistence_preview_requires_safe_ai_database_identifier";
    return out;
  }
  if(request.commitPreflight == nullptr) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::MissingCommitPreflight;
    out.reason = "step273_persistence_preview_requires_commit_preflight_result";
    return out;
  }
  if(!request.commitPreflight->ready) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::CommitPreflightNotReady;
    out.reason = request.commitPreflight->reason.empty() ? "step273_commit_preflight_not_ready" : request.commitPreflight->reason;
    return out;
  }
  if(request.resumePlan == nullptr) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::MissingResumePlan;
    out.reason = "step273_persistence_preview_requires_resume_plan";
    return out;
  }
  if(request.packetBoundary == nullptr) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::MissingPacketBoundary;
    out.reason = "step273_persistence_preview_requires_packet_boundary";
    return out;
  }
  if(!request.packetBoundary->buildable) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::PacketBoundaryNotBuildable;
    out.reason = request.packetBoundary->reason;
    return out;
  }
  if(request.deliveryRegistration == nullptr) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::MissingDeliveryRegistration;
    out.reason = "step273_persistence_preview_requires_delivery_registration_boundary";
    return out;
  }
  if(!request.deliveryRegistration->eligible) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::DeliveryRegistrationNotEligible;
    out.reason = request.deliveryRegistration->reason;
    return out;
  }
  if(request.dispatchEnvelope == nullptr) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::MissingDispatchEnvelope;
    out.reason = "step273_persistence_preview_requires_dispatch_envelope";
    return out;
  }
  if(!request.dispatchEnvelope->eligible) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::DispatchEnvelopeNotEligible;
    out.reason = request.dispatchEnvelope->reason;
    return out;
  }

  const auto& packet = request.packetBoundary->packet;
  const auto& commit = *request.commitPreflight;
  const auto& registration = *request.deliveryRegistration;
  const auto& dispatch = *request.dispatchEnvelope;

  if(!Detail::same(commit.conversationId, packet.conversationId) ||
     !Detail::same(commit.sessionUuid, packet.sessionUuid) ||
     !Detail::same(commit.actionId, packet.actionId) ||
     !Detail::same(commit.ackKey, packet.ackKey) ||
     !Detail::same(registration.conversationId, packet.conversationId) ||
     !Detail::same(registration.sessionUuid, packet.sessionUuid) ||
     !Detail::same(registration.actionId, packet.actionId) ||
     !Detail::same(registration.ackKey, packet.ackKey) ||
     !Detail::same(dispatch.conversationId, packet.conversationId) ||
     !Detail::same(dispatch.sessionUuid, packet.sessionUuid) ||
     !Detail::same(dispatch.actionId, packet.actionId) ||
     !Detail::same(dispatch.ackKey, packet.ackKey)) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::IdentityMismatch;
    out.reason = "step273_persistence_preview_identity_mismatch";
    return out;
  }

  if(packet.conversationId.empty() || packet.sessionUuid.empty() || packet.actionId.empty() ||
     packet.ackKey.empty() || packet.packetSequence == 0 || request.worldInstanceUuid.empty()) {
    out.status = AiDialogIntentDeliveryPersistencePreviewStatus::MissingRequiredIdentity;
    out.reason = "step273_persistence_preview_missing_required_identity";
    return out;
  }

  const std::string db = Detail::mysqlIdentifier(request.aiDatabaseName);
  const std::string payloadJson = Detail::previewJsonPayload(request);
  const std::string endpointJson = Detail::endpointAuditJson(request);
  const std::string conversationKey = packet.conversationId;
  const std::string conversationSubquery = "(SELECT conversation_id FROM " + db +
      ".dialog_intent_conversation_sessions WHERE conversation_key=" + Detail::sqlLiteral(conversationKey) + " LIMIT 1)";

  out.conversationKey = conversationKey;
  out.actionId = packet.actionId;
  out.ackKey = packet.ackKey;
  out.sessionUuid = packet.sessionUuid;
  out.characterKey = packet.targetCharacterKey;
  out.packetSequence = packet.packetSequence;
  out.localSequence = packet.localSequence;
  out.serverTick = packet.serverTick;
  out.payloadBytes = dispatch.encodedBytes;

  std::string conversationSql;
  conversationSql.reserve(1400);
  conversationSql += "INSERT INTO " + db + ".dialog_intent_conversation_sessions (";
  conversationSql += "conversation_key,world_instance_uuid,world_name,content_revision_key,speaker_entity_key,";
  conversationSql += "speaker_npc_instance_uuid,line_id,audio_ref,server_tick,start_tick,duration_ms,planned_recipients,";
  conversationSql += "conversation_status,raw_payload) VALUES (";
  conversationSql += Detail::sqlLiteral(conversationKey) + ",";
  conversationSql += Detail::sqlLiteral(request.worldInstanceUuid) + ",";
  conversationSql += Detail::sqlLiteral(request.worldName.empty() ? request.resumePlan->worldName : request.worldName) + ",";
  conversationSql += Detail::sqlLiteral(request.contentRevisionKey) + ",";
  conversationSql += Detail::sqlLiteral(packet.speakerEntityKey) + ",";
  conversationSql += Detail::sqlLiteral(request.resumePlan->speakerNpcInstanceUuid) + ",";
  conversationSql += Detail::sqlLiteral(packet.lineId) + ",";
  conversationSql += Detail::sqlLiteral(packet.audioRef) + ",";
  conversationSql += Detail::sqlUint(packet.serverTick) + ",";
  conversationSql += Detail::sqlUint(packet.startTick) + ",";
  conversationSql += Detail::sqlUint(packet.durationMs) + ",";
  conversationSql += Detail::sqlSize(request.resumePlan->knownObservers + 1) + ",";
  conversationSql += "'open',CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON)) ";
  conversationSql += "ON DUPLICATE KEY UPDATE planned_recipients=GREATEST(planned_recipients,VALUES(planned_recipients)),";
  conversationSql += "raw_payload=JSON_MERGE_PATCH(raw_payload,VALUES(raw_payload)),updated_at=CURRENT_TIMESTAMP(6);";
  out.statements.push_back({"conversation_session_upsert", std::move(conversationSql), true, false});

  std::string deliverySql;
  deliverySql.reserve(1700);
  deliverySql += "SET @step273_delivery_id=NULL; SET @step273_delivery_status=NULL; CALL " + db + ".mmo_ai_record_gameplay_delivery_sent(";
  deliverySql += "NULL,NULL," + conversationSubquery + ",";
  deliverySql += Detail::sqlLiteral(request.worldInstanceUuid) + ",";
  deliverySql += "'npc_dialog_resume','late_observer_resume',";
  deliverySql += Detail::sqlLiteral(packet.actionId) + ",";
  deliverySql += Detail::sqlLiteral(packet.ackKey) + ",";
  deliverySql += Detail::sqlLiteral(packet.sessionUuid) + ",";
  deliverySql += "''," + Detail::sqlLiteral(packet.targetCharacterKey) + ",";
  deliverySql += "'ServerNpcDialogIntent',";
  deliverySql += Detail::sqlUint(packet.packetSequence) + ",";
  deliverySql += Detail::sqlUint(packet.localSequence) + ",";
  deliverySql += Detail::sqlUint(packet.serverTick) + ",";
  deliverySql += "''," + Detail::sqlSize(dispatch.encodedBytes) + ",";
  deliverySql += Detail::sqlUint(request.ackTimeoutMs) + ",";
  deliverySql += "CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON),";
  deliverySql += "CAST(" + Detail::sqlLiteral(endpointJson) + " AS JSON),";
  deliverySql += "@step273_delivery_id,@step273_delivery_status);";
  out.statements.push_back({"gameplay_delivery_sent_call", std::move(deliverySql), true, true});

  std::string observerSql;
  observerSql.reserve(1600);
  observerSql += "INSERT INTO " + db + ".dialog_intent_conversation_observers (";
  observerSql += "conversation_id,delivery_id,observer_session_uuid,observer_character_uuid,observer_character_key,";
  observerSql += "observer_kind,observer_status,observation_status,action_id,ack_key,packet_sequence,local_sequence,";
  observerSql += "distance_squared,has_position,raw_payload,sent_at,ack_deadline_at) SELECT ";
  observerSql += "conversation_id,@step273_delivery_id,";
  observerSql += Detail::sqlLiteral(packet.sessionUuid) + ",'',";
  observerSql += Detail::sqlLiteral(packet.targetCharacterKey) + ",";
  observerSql += "'late_observer','pending','none',";
  observerSql += Detail::sqlLiteral(packet.actionId) + ",";
  observerSql += Detail::sqlLiteral(packet.ackKey) + ",";
  observerSql += Detail::sqlUint(packet.packetSequence) + ",";
  observerSql += Detail::sqlUint(packet.localSequence) + ",0,0,";
  observerSql += "CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON),CURRENT_TIMESTAMP(6),";
  observerSql += "TIMESTAMPADD(MICROSECOND," + Detail::sqlUint(request.ackTimeoutMs) + "*1000,CURRENT_TIMESTAMP(6)) ";
  observerSql += "FROM " + db + ".dialog_intent_conversation_sessions WHERE conversation_key=" + Detail::sqlLiteral(conversationKey) + " LIMIT 1 ";
  observerSql += "ON DUPLICATE KEY UPDATE delivery_id=COALESCE(delivery_id,VALUES(delivery_id)),updated_at=CURRENT_TIMESTAMP(6);";
  out.statements.push_back({"conversation_observer_upsert", std::move(observerSql), true, false});

  std::string receiptSql;
  receiptSql.reserve(900);
  receiptSql += "SET @step273_receipt_delivery_id=NULL; SET @step273_receipt_kind=NULL; SET @step273_receipt_delivery_status=NULL; CALL " + db + ".mmo_ai_record_gameplay_delivery_receipt(";
  receiptSql += Detail::sqlLiteral(packet.actionId) + ",";
  receiptSql += Detail::sqlLiteral(packet.ackKey) + ",";
  receiptSql += "'acked',";
  receiptSql += Detail::sqlLiteral(packet.sessionUuid) + ",'',";
  receiptSql += Detail::sqlLiteral(packet.targetCharacterKey) + ",";
  receiptSql += "'none','future_client_ack','preview_only_no_client_receipt_yet',";
  receiptSql += "CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON),";
  receiptSql += "@step273_receipt_delivery_id,@step273_receipt_kind,@step273_receipt_delivery_status);";
  out.statements.push_back({"future_gameplay_delivery_receipt_call", std::move(receiptSql), true, true});

  std::string deadLetterSql;
  deadLetterSql.reserve(600);
  deadLetterSql += "SET @step273_dead_letter_id=NULL; CALL " + db + ".mmo_ai_record_gameplay_delivery_dead_letter(";
  deadLetterSql += "@step273_delivery_id,'future_udp_send_failure_preview',0,";
  deadLetterSql += "CAST(" + Detail::sqlLiteral(payloadJson) + " AS JSON),@step273_dead_letter_id);";
  out.statements.push_back({"future_gameplay_delivery_dead_letter_call", std::move(deadLetterSql), true, true});

  out.status = AiDialogIntentDeliveryPersistencePreviewStatus::ReadyNoExecute;
  out.ready = true;
  out.reason = "step273_persistence_preview_ready_no_execute";
  return out;
}

} // namespace Mmo::Server
