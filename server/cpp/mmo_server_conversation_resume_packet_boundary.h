#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "../../game/game/mmonetprotocol.h"
#include "mmo_server_conversation_session_boundary.h"

namespace Mmo::Server {

enum class ConversationResumePacketBoundaryStatus : std::uint8_t {
  Buildable = 0,
  PlanNotResumable,
  MissingObserverSession,
  MissingTargetCharacter,
  MissingConversationId,
  MissingSpeakerIdentity,
  MissingLineIdentity,
  MissingRemainingDuration,
  MissingPacketSequence,
  DurationOverflow,
  EncodeFailed,
};

[[nodiscard]] constexpr const char* conversationResumePacketBoundaryStatusName(
    ConversationResumePacketBoundaryStatus status) noexcept {
  switch(status) {
    case ConversationResumePacketBoundaryStatus::Buildable:                return "buildable";
    case ConversationResumePacketBoundaryStatus::PlanNotResumable:         return "plan_not_resumable";
    case ConversationResumePacketBoundaryStatus::MissingObserverSession:   return "missing_observer_session";
    case ConversationResumePacketBoundaryStatus::MissingTargetCharacter:   return "missing_target_character";
    case ConversationResumePacketBoundaryStatus::MissingConversationId:    return "missing_conversation_id";
    case ConversationResumePacketBoundaryStatus::MissingSpeakerIdentity:   return "missing_speaker_identity";
    case ConversationResumePacketBoundaryStatus::MissingLineIdentity:      return "missing_line_identity";
    case ConversationResumePacketBoundaryStatus::MissingRemainingDuration: return "missing_remaining_duration";
    case ConversationResumePacketBoundaryStatus::MissingPacketSequence:    return "missing_packet_sequence";
    case ConversationResumePacketBoundaryStatus::DurationOverflow:         return "duration_overflow";
    case ConversationResumePacketBoundaryStatus::EncodeFailed:             return "encode_failed";
  }
  return "unknown";
}

struct ConversationResumePacketBuildRequest final {
  ConversationLateObserverResumePlan resume;
  std::string targetCharacterKey;
  std::string worldInstanceUuid;
  std::string text;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  bool diagnostic = true;
};

struct ConversationResumePacketBuildResult final {
  ConversationResumePacketBoundaryStatus status = ConversationResumePacketBoundaryStatus::PlanNotResumable;
  bool buildable = false;
  Mmo::Net::ServerNpcDialogIntentPacket packet;
  std::size_t encodedBytes = 0;
  std::string reason;
};

[[nodiscard]] inline std::string makeConversationResumeActionId(std::string_view sessionUuid,
                                                                std::uint64_t packetSequence) {
  std::string out;
  out.reserve(sessionUuid.size() + 48);
  out.append("server-npc-dialog-resume-intent:");
  out.append(sessionUuid);
  out.push_back(':');
  out.append(std::to_string(packetSequence));
  return out;
}

[[nodiscard]] inline std::string makeConversationResumeReason(const ConversationLateObserverResumePlan& plan) {
  std::string out;
  out.reserve(192 + plan.conversationId.size());
  out.append("late_observer_resume_no_send");
  out.append(";conversation_id=").append(plan.conversationId);
  out.append(";line_start_tick=").append(std::to_string(plan.lineStartTick));
  out.append(";observer_server_tick=").append(std::to_string(plan.observerServerTick));
  out.append(";elapsed_ms=").append(std::to_string(plan.elapsedMs));
  out.append(";remaining_ms=").append(std::to_string(plan.remainingMs));
  out.append(";original_duration_ms=").append(std::to_string(plan.durationMs));
  return out;
}

[[nodiscard]] inline ConversationResumePacketBuildResult buildConversationResumeDialogIntentPacketNoSend(
    ConversationResumePacketBuildRequest request) {
  ConversationResumePacketBuildResult out;

  const auto& plan = request.resume;
  if(!plan.canResume || plan.status != ConversationLateObserverResumeStatus::Planned) {
    out.status = ConversationResumePacketBoundaryStatus::PlanNotResumable;
    out.reason = "late_observer_resume_plan_is_not_resumable";
    return out;
  }
  if(plan.sessionUuid.empty()) {
    out.status = ConversationResumePacketBoundaryStatus::MissingObserverSession;
    out.reason = "resume_plan_has_no_observer_session_uuid";
    return out;
  }
  if(request.targetCharacterKey.empty() && plan.characterKey.empty()) {
    out.status = ConversationResumePacketBoundaryStatus::MissingTargetCharacter;
    out.reason = "resume_packet_requires_target_character_key";
    return out;
  }
  if(plan.conversationId.empty()) {
    out.status = ConversationResumePacketBoundaryStatus::MissingConversationId;
    out.reason = "resume_plan_has_no_conversation_id";
    return out;
  }
  if(plan.speakerEntityKey.empty() && plan.speakerNpcInstanceUuid.empty()) {
    out.status = ConversationResumePacketBoundaryStatus::MissingSpeakerIdentity;
    out.reason = "resume_packet_requires_stable_speaker_identity";
    return out;
  }
  if(plan.lineId.empty() && plan.audioRef.empty() && request.text.empty()) {
    out.status = ConversationResumePacketBoundaryStatus::MissingLineIdentity;
    out.reason = "resume_packet_requires_line_id_audio_ref_or_text";
    return out;
  }
  if(plan.remainingMs == 0 || plan.durationMs == 0) {
    out.status = ConversationResumePacketBoundaryStatus::MissingRemainingDuration;
    out.reason = "resume_packet_requires_positive_remaining_duration";
    return out;
  }
  if(request.packetSequence == 0) {
    out.status = ConversationResumePacketBoundaryStatus::MissingPacketSequence;
    out.reason = "resume_packet_requires_non_zero_packet_sequence";
    return out;
  }
  if(plan.remainingMs > std::numeric_limits<std::uint32_t>::max()) {
    out.status = ConversationResumePacketBoundaryStatus::DurationOverflow;
    out.reason = "remaining_duration_does_not_fit_protocol_duration_ms";
    return out;
  }

  auto actionId = makeConversationResumeActionId(plan.sessionUuid, request.packetSequence);
  Mmo::Net::ServerNpcDialogIntentPacket packet;
  packet.packetSequence = request.packetSequence;
  packet.localSequence = request.localSequence;
  packet.serverTick = request.serverTick == 0 ? plan.observerServerTick : request.serverTick;
  packet.startTick = packet.serverTick;
  packet.durationMs = static_cast<std::uint32_t>(plan.remainingMs);
  packet.flags = Mmo::Net::ServerNpcDialogIntentLateObserverResume |
                 (request.diagnostic ? Mmo::Net::ServerNpcDialogIntentDiagnostic : 0u);
  packet.sessionUuid = plan.sessionUuid;
  packet.targetCharacterKey = request.targetCharacterKey.empty() ? plan.characterKey : std::move(request.targetCharacterKey);
  packet.actionId = std::move(actionId);
  packet.ackKey = packet.actionId + ":client-ack";
  packet.conversationId = plan.conversationId;
  packet.worldInstanceUuid = std::move(request.worldInstanceUuid);
  packet.speakerEntityKey = plan.speakerEntityKey;
  packet.speakerNpcInstanceUuid = plan.speakerNpcInstanceUuid;
  packet.lineId = plan.lineId;
  packet.text = std::move(request.text);
  packet.audioRef = plan.audioRef;
  packet.reason = makeConversationResumeReason(plan);

  const auto encoded = Mmo::Net::encodeServerNpcDialogIntentPacket(packet);
  if(encoded.empty()) {
    out.status = ConversationResumePacketBoundaryStatus::EncodeFailed;
    out.reason = "resume_packet_encode_failed_or_datagram_too_large";
    return out;
  }

  out.status = ConversationResumePacketBoundaryStatus::Buildable;
  out.buildable = true;
  out.encodedBytes = encoded.size();
  out.reason = "resume_packet_buildable_no_send";
  out.packet = std::move(packet);
  return out;
}

} // namespace Mmo::Server
