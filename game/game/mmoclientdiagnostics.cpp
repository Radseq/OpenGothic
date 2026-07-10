#include "mmoclientdiagnostics.h"

#include <string>
#include <string_view>

#include "../../../shared/game/mmo/mmosemanticevents.h"

namespace Mmo::ClientDiagnostics {

const char* liveDeltaDomainName(Net::ServerLiveDeltaKind kind) noexcept {
  switch(kind) {
    case Net::ServerLiveDeltaKind::MovementCorrection: return "movement";
    case Net::ServerLiveDeltaKind::CharacterStats:     return "character";
    case Net::ServerLiveDeltaKind::Inventory:          return "inventory";
    case Net::ServerLiveDeltaKind::Equipment:          return "equipment";
    case Net::ServerLiveDeltaKind::WorldItem:          return "world_item";
    case Net::ServerLiveDeltaKind::InteractiveState:   return "interactive";
    case Net::ServerLiveDeltaKind::Combat:             return "combat";
    case Net::ServerLiveDeltaKind::Story:              return "story";
    case Net::ServerLiveDeltaKind::PerceptionReaction: return "npc_perception";
    case Net::ServerLiveDeltaKind::Generic:            return "generic";
  }
  return "generic";
}

std::string liveDeltaJson(const Net::ServerLiveDeltaPacket& delta) {
  std::string out;
  out.reserve(delta.debugJson.size() + delta.actionKind.size() + 512);
  out += "{\"schema\":\"mmo.client_live_delta_debug.v1\"";
  out += ",\"kind\":";
  out += std::to_string(static_cast<unsigned>(delta.kind));
  out += ",\"domain\":";
  out += jsonEscape(liveDeltaDomainName(delta.kind));
  out += ",\"action_kind\":";
  out += jsonEscape(delta.actionKind);
  out += ",\"packet_sequence\":";
  out += std::to_string(delta.packetSequence);
  out += ",\"local_sequence\":";
  out += std::to_string(delta.localSequence);
  out += ",\"server_tick\":";
  out += std::to_string(delta.serverTick);
  out += ",\"flags\":";
  out += std::to_string(delta.flags);
  out += ",\"has_position\":";
  out += ((delta.flags & Net::ServerLiveDeltaHasPosition) != 0) ? "true" : "false";
  out += ",\"has_stats\":";
  out += ((delta.flags & Net::ServerLiveDeltaHasStats) != 0) ? "true" : "false";
  out += ",\"requires_snapshot_refresh\":";
  out += ((delta.flags & Net::ServerLiveDeltaRequiresSnapshotRefresh) != 0) ? "true" : "false";
  out += ",\"position\":{\"x\":";
  out += std::to_string(delta.posX);
  out += ",\"y\":";
  out += std::to_string(delta.posY);
  out += ",\"z\":";
  out += std::to_string(delta.posZ);
  out += ",\"yaw\":";
  out += std::to_string(delta.yaw);
  out += "},\"stats\":{\"level\":";
  out += std::to_string(delta.level);
  out += ",\"experience\":";
  out += std::to_string(delta.experience);
  out += ",\"experience_next\":";
  out += std::to_string(delta.experienceNext);
  out += ",\"learning_points\":";
  out += std::to_string(delta.learningPoints);
  out += ",\"health_current\":";
  out += std::to_string(delta.healthCurrent);
  out += ",\"health_max\":";
  out += std::to_string(delta.healthMax);
  out += ",\"mana_current\":";
  out += std::to_string(delta.manaCurrent);
  out += ",\"mana_max\":";
  out += std::to_string(delta.manaMax);
  out += ",\"strength\":";
  out += std::to_string(delta.strength);
  out += ",\"dexterity\":";
  out += std::to_string(delta.dexterity);
  out += ",\"guild\":";
  out += std::to_string(delta.guild);
  out += ",\"true_guild\":";
  out += std::to_string(delta.trueGuild);
  out += "}";
  if(!delta.debugJson.empty()) {
    out += ",\"debug_payload\":";
    out += delta.debugJson;
  }
  out += "}";
  return out;
}


std::string dialogPresentationDecisionJson(const ServerDialogPresentationDecision& decision);

std::string npcDialogIntentJson(const Net::ServerNpcDialogIntentPacket& intent,
                                     bool duplicateDelivery,
                                     const ServerDialogPresentationDecision& decision) {
  std::string out;
  out.reserve(intent.text.size() + intent.audioRef.size() + intent.reason.size() + 1024);
  out += "{\"schema\":\"mmo.client_server_npc_dialog_intent_received.v1\"";
  out += ",\"packet_sequence\":";
  out += std::to_string(intent.packetSequence);
  out += ",\"local_sequence\":";
  out += std::to_string(intent.localSequence);
  out += ",\"server_tick\":";
  out += std::to_string(intent.serverTick);
  out += ",\"start_tick\":";
  out += std::to_string(intent.startTick);
  out += ",\"duration_ms\":";
  out += std::to_string(intent.durationMs);
  out += ",\"flags\":";
  out += std::to_string(intent.flags);
  out += ",\"session_uuid\":";
  out += jsonEscape(intent.sessionUuid);
  out += ",\"target_character_key\":";
  out += jsonEscape(intent.targetCharacterKey);
  out += ",\"action_id\":";
  out += jsonEscape(intent.actionId);
  out += ",\"ack_key\":";
  out += jsonEscape(intent.ackKey);
  out += ",\"conversation_id\":";
  out += jsonEscape(intent.conversationId);
  out += ",\"world_instance_uuid\":";
  out += jsonEscape(intent.worldInstanceUuid);
  out += ",\"speaker_entity_key\":";
  out += jsonEscape(intent.speakerEntityKey);
  out += ",\"speaker_npc_instance_uuid\":";
  out += jsonEscape(intent.speakerNpcInstanceUuid);
  out += ",\"line_id\":";
  out += jsonEscape(intent.lineId);
  out += ",\"text\":";
  out += jsonEscape(intent.text);
  out += ",\"audio_ref\":";
  out += jsonEscape(intent.audioRef);
  out += ",\"reason\":";
  out += jsonEscape(intent.reason);
  out += ",\"client_dialog_ui_applied\":";
  out += decision.uiApplied ? "true" : "false";
  out += ",\"client_audio_applied\":";
  out += decision.audioApplied ? "true" : "false";
  out += ",\"client_ack_sent_by_transport\":true";
  out += ",\"client_ack_status\":";
  out += jsonEscape(decision.accepted() ? "ack" : "nack");
  out += ",\"client_ack_reason\":";
  out += jsonEscape(decision.reason);
  out += ",\"client_dialog_presentation_status\":";
  out += jsonEscape(serverDialogPresentationStatusName(decision.presentationStatus));
  out += ",\"client_dialog_presentation_validation_enabled\":";
  out += decision.validationEnabled ? "true" : "false";
  out += ",\"client_dialog_presentation_decision\":";
  out += dialogPresentationDecisionJson(decision);
  out += ",\"client_duplicate_delivery\":";
  out += duplicateDelivery ? "true" : "false";
  out += "}";
  return out;
}

std::string dialogPresentationDecisionJson(const ServerDialogPresentationDecision& decision) {
  std::string out;
  out.reserve(decision.reason.size() + decision.message.size() + 256);
  out += "{\"status\":";
  out += jsonEscape(serverDialogPresentationStatusName(decision.presentationStatus));
  out += ",\"ack_status\":";
  out += jsonEscape(decision.accepted() ? "ack" : "nack");
  out += ",\"reason\":";
  out += jsonEscape(decision.reason);
  out += ",\"message\":";
  out += jsonEscape(decision.message);
  out += ",\"validation_enabled\":";
  out += decision.validationEnabled ? "true" : "false";
  out += ",\"ui_applied\":";
  out += decision.uiApplied ? "true" : "false";
  out += ",\"audio_applied\":";
  out += decision.audioApplied ? "true" : "false";
  out += "}";
  return out;
}

} // namespace Mmo::ClientDiagnostics
