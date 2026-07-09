#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "mmo_outbound_gameplay_delivery_state.h"
#include "mmo_server_conversation_resume_send_gate.h"
#include "mmo_server_conversation_session_boundary.h"

namespace Mmo::Server {

enum class ConversationResumeDeliveryRegistrationStatus : std::uint8_t {
  EligibleNoRegister = 0,
  BoundaryDisabled,
  SendGateNotEligible,
  PacketNotBuildable,
  MissingConversationRuntime,
  MissingDeliveryState,
  MissingObserverSession,
  MissingConversationId,
  MissingActionId,
  MissingAckKey,
  MissingPacketSequence,
  GatePacketMismatch,
  MissingConversationSession,
  AlreadyInOutboundDelivery,
  AlreadyConversationObserverAckKey,
  AlreadyConversationObserverSession,
  DeadlineOverflow,
  DuplicateBoundaryActionId,
  DuplicateBoundaryAckKey,
};

[[nodiscard]] constexpr const char* conversationResumeDeliveryRegistrationStatusName(
    ConversationResumeDeliveryRegistrationStatus status) noexcept {
  switch(status) {
    case ConversationResumeDeliveryRegistrationStatus::EligibleNoRegister:             return "eligible_no_register";
    case ConversationResumeDeliveryRegistrationStatus::BoundaryDisabled:               return "boundary_disabled";
    case ConversationResumeDeliveryRegistrationStatus::SendGateNotEligible:            return "send_gate_not_eligible";
    case ConversationResumeDeliveryRegistrationStatus::PacketNotBuildable:             return "packet_not_buildable";
    case ConversationResumeDeliveryRegistrationStatus::MissingConversationRuntime:     return "missing_conversation_runtime";
    case ConversationResumeDeliveryRegistrationStatus::MissingDeliveryState:           return "missing_delivery_state";
    case ConversationResumeDeliveryRegistrationStatus::MissingObserverSession:         return "missing_observer_session";
    case ConversationResumeDeliveryRegistrationStatus::MissingConversationId:          return "missing_conversation_id";
    case ConversationResumeDeliveryRegistrationStatus::MissingActionId:                return "missing_action_id";
    case ConversationResumeDeliveryRegistrationStatus::MissingAckKey:                  return "missing_ack_key";
    case ConversationResumeDeliveryRegistrationStatus::MissingPacketSequence:          return "missing_packet_sequence";
    case ConversationResumeDeliveryRegistrationStatus::GatePacketMismatch:             return "gate_packet_mismatch";
    case ConversationResumeDeliveryRegistrationStatus::MissingConversationSession:     return "missing_conversation_session";
    case ConversationResumeDeliveryRegistrationStatus::AlreadyInOutboundDelivery:      return "already_in_outbound_delivery";
    case ConversationResumeDeliveryRegistrationStatus::AlreadyConversationObserverAckKey:
      return "already_conversation_observer_ack_key";
    case ConversationResumeDeliveryRegistrationStatus::AlreadyConversationObserverSession:
      return "already_conversation_observer_session";
    case ConversationResumeDeliveryRegistrationStatus::DeadlineOverflow:               return "deadline_overflow";
    case ConversationResumeDeliveryRegistrationStatus::DuplicateBoundaryActionId:      return "duplicate_boundary_action_id";
    case ConversationResumeDeliveryRegistrationStatus::DuplicateBoundaryAckKey:        return "duplicate_boundary_ack_key";
  }
  return "unknown";
}

struct ConversationResumeDeliveryRegistrationRequest final {
  const ConversationResumeSendGateResult* sendGate = nullptr;
  const ConversationResumePacketBuildResult* packetBoundary = nullptr;
  const OutboundGameplayDeliveryState* deliveryState = nullptr;
  const ConversationSessionRuntime* conversationRuntime = nullptr;
  bool boundaryEnabled = false;
  std::uint64_t nowMs = 0;
  std::uint64_t ackTimeoutMs = 0;
};

struct ConversationResumeDeliveryRegistrationResult final {
  ConversationResumeDeliveryRegistrationStatus status = ConversationResumeDeliveryRegistrationStatus::BoundaryDisabled;
  bool eligible = false;
  bool duplicate = false;
  bool wouldRegisterDelivery = false;
  bool wouldRegisterConversationObserver = false;
  bool wouldMutateRuntime = false;
  std::string conversationId;
  std::string sessionUuid;
  std::string characterKey;
  std::string actionId;
  std::string ackKey;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  std::uint64_t sentAtMs = 0;
  std::uint64_t ackDeadlineMs = 0;
  OutboundGameplayAttempt deliveryAttempt;
  ConversationObserverSent conversationObserver;
  std::string reason;
};

struct ConversationResumeDeliveryRegistrationStats final {
  std::uint64_t requests = 0;
  std::uint64_t eligible = 0;
  std::uint64_t disabled = 0;
  std::uint64_t sendGateNotEligible = 0;
  std::uint64_t packetNotBuildable = 0;
  std::uint64_t missingRuntime = 0;
  std::uint64_t missingIdentity = 0;
  std::uint64_t gatePacketMismatch = 0;
  std::uint64_t missingConversationSession = 0;
  std::uint64_t alreadyDelivery = 0;
  std::uint64_t alreadyObserverAckKey = 0;
  std::uint64_t alreadyObserverSession = 0;
  std::uint64_t deadlineOverflow = 0;
  std::uint64_t duplicateBoundaryActionId = 0;
  std::uint64_t duplicateBoundaryAckKey = 0;
};

class ConversationResumeDeliveryRegistrationBoundary final {
  public:
    [[nodiscard]] ConversationResumeDeliveryRegistrationResult classifyNoRegister(
        const ConversationResumeDeliveryRegistrationRequest& request) {
      ++counters.requests;

      ConversationResumeDeliveryRegistrationResult out;
      out.sentAtMs = request.nowMs;

      if(!request.boundaryEnabled) {
        ++counters.disabled;
        out.status = ConversationResumeDeliveryRegistrationStatus::BoundaryDisabled;
        out.reason = "resume_delivery_registration_boundary_disabled";
        return out;
      }

      if(request.sendGate == nullptr || !request.sendGate->eligible) {
        ++counters.sendGateNotEligible;
        out.status = ConversationResumeDeliveryRegistrationStatus::SendGateNotEligible;
        out.reason = request.sendGate == nullptr ? "missing_resume_send_gate_result" : request.sendGate->reason;
        return out;
      }

      if(request.packetBoundary == nullptr || !request.packetBoundary->buildable) {
        ++counters.packetNotBuildable;
        out.status = ConversationResumeDeliveryRegistrationStatus::PacketNotBuildable;
        out.reason = request.packetBoundary == nullptr ? "missing_resume_packet_boundary_result"
                                                       : request.packetBoundary->reason;
        return out;
      }

      const auto& packet = request.packetBoundary->packet;
      out.conversationId = packet.conversationId;
      out.sessionUuid = packet.sessionUuid;
      out.characterKey = packet.targetCharacterKey;
      out.actionId = packet.actionId;
      out.ackKey = packet.ackKey;
      out.packetSequence = packet.packetSequence;
      out.localSequence = packet.localSequence;
      out.serverTick = packet.serverTick;

      if(packet.sessionUuid.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDeliveryRegistrationStatus::MissingObserverSession;
        out.reason = "resume_packet_has_no_observer_session_uuid";
        return out;
      }
      if(packet.conversationId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDeliveryRegistrationStatus::MissingConversationId;
        out.reason = "resume_packet_has_no_conversation_id";
        return out;
      }
      if(packet.actionId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDeliveryRegistrationStatus::MissingActionId;
        out.reason = "resume_packet_has_no_action_id";
        return out;
      }
      if(packet.ackKey.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDeliveryRegistrationStatus::MissingAckKey;
        out.reason = "resume_packet_has_no_ack_key";
        return out;
      }
      if(packet.packetSequence == 0) {
        ++counters.missingIdentity;
        out.status = ConversationResumeDeliveryRegistrationStatus::MissingPacketSequence;
        out.reason = "resume_packet_has_no_packet_sequence";
        return out;
      }

      if(std::string_view(request.sendGate->sessionUuid) != std::string_view(packet.sessionUuid) ||
         std::string_view(request.sendGate->conversationId) != std::string_view(packet.conversationId) ||
         std::string_view(request.sendGate->actionId) != std::string_view(packet.actionId) ||
         std::string_view(request.sendGate->ackKey) != std::string_view(packet.ackKey)) {
        ++counters.gatePacketMismatch;
        out.status = ConversationResumeDeliveryRegistrationStatus::GatePacketMismatch;
        out.reason = "resume_send_gate_and_packet_boundary_identity_mismatch";
        return out;
      }

      if(request.deliveryState == nullptr) {
        ++counters.missingRuntime;
        out.status = ConversationResumeDeliveryRegistrationStatus::MissingDeliveryState;
        out.reason = "resume_delivery_registration_requires_outbound_delivery_state";
        return out;
      }
      if(request.conversationRuntime == nullptr) {
        ++counters.missingRuntime;
        out.status = ConversationResumeDeliveryRegistrationStatus::MissingConversationRuntime;
        out.reason = "resume_delivery_registration_requires_conversation_runtime";
        return out;
      }
      if(!request.conversationRuntime->hasConversation(packet.conversationId)) {
        ++counters.missingConversationSession;
        out.status = ConversationResumeDeliveryRegistrationStatus::MissingConversationSession;
        out.reason = "resume_conversation_session_not_open_in_runtime";
        return out;
      }
      if(request.deliveryState->hasAttempt(packet.ackKey)) {
        ++counters.alreadyDelivery;
        out.status = ConversationResumeDeliveryRegistrationStatus::AlreadyInOutboundDelivery;
        out.duplicate = true;
        out.reason = "resume_ack_key_already_registered_in_outbound_delivery";
        return out;
      }
      if(request.conversationRuntime->hasObserverAckKey(packet.ackKey)) {
        ++counters.alreadyObserverAckKey;
        out.status = ConversationResumeDeliveryRegistrationStatus::AlreadyConversationObserverAckKey;
        out.duplicate = true;
        out.reason = "resume_ack_key_already_registered_as_conversation_observer";
        return out;
      }
      if(request.conversationRuntime->hasObserverSession(packet.conversationId, packet.sessionUuid)) {
        ++counters.alreadyObserverSession;
        out.status = ConversationResumeDeliveryRegistrationStatus::AlreadyConversationObserverSession;
        out.duplicate = true;
        out.reason = "resume_session_already_registered_as_conversation_observer";
        return out;
      }
      if(actionIds.contains(packet.actionId)) {
        ++counters.duplicateBoundaryActionId;
        out.status = ConversationResumeDeliveryRegistrationStatus::DuplicateBoundaryActionId;
        out.duplicate = true;
        out.reason = "resume_action_id_already_reserved_by_delivery_registration_boundary";
        return out;
      }
      if(ackKeys.contains(packet.ackKey)) {
        ++counters.duplicateBoundaryAckKey;
        out.status = ConversationResumeDeliveryRegistrationStatus::DuplicateBoundaryAckKey;
        out.duplicate = true;
        out.reason = "resume_ack_key_already_reserved_by_delivery_registration_boundary";
        return out;
      }
      if(request.ackTimeoutMs > std::numeric_limits<std::uint64_t>::max() - request.nowMs) {
        ++counters.deadlineOverflow;
        out.status = ConversationResumeDeliveryRegistrationStatus::DeadlineOverflow;
        out.reason = "resume_ack_deadline_overflow";
        return out;
      }

      out.ackDeadlineMs = request.nowMs + request.ackTimeoutMs;

      OutboundGameplayAttempt attempt;
      attempt.actionId = packet.actionId;
      attempt.ackKey = packet.ackKey;
      attempt.sessionUuid = packet.sessionUuid;
      attempt.characterKey = packet.targetCharacterKey;
      attempt.gameplayKind = "npc_dialog_intent_resume";
      attempt.packetSequence = packet.packetSequence;
      attempt.localSequence = packet.localSequence;
      attempt.serverTick = packet.serverTick;
      attempt.sentAtMs = request.nowMs;
      attempt.ackDeadlineMs = out.ackDeadlineMs;
      attempt.sendAttempts = 1;

      ConversationObserverSent observer;
      observer.sessionUuid = packet.sessionUuid;
      observer.characterKey = packet.targetCharacterKey;
      observer.actionId = packet.actionId;
      observer.ackKey = packet.ackKey;
      observer.packetSequence = packet.packetSequence;
      observer.localSequence = packet.localSequence;
      observer.sentAtMs = request.nowMs;
      observer.ackDeadlineMs = out.ackDeadlineMs;
      observer.distanceSquared = 0.0;
      observer.target = false;
      observer.hasPosition = false;

      actionIds.insert(packet.actionId);
      ackKeys.insert(packet.ackKey);
      ++counters.eligible;

      out.status = ConversationResumeDeliveryRegistrationStatus::EligibleNoRegister;
      out.eligible = true;
      out.wouldRegisterDelivery = true;
      out.wouldRegisterConversationObserver = true;
      out.wouldMutateRuntime = false;
      out.deliveryAttempt = std::move(attempt);
      out.conversationObserver = std::move(observer);
      out.reason = "resume_delivery_registration_eligible_no_register";
      return out;
    }

    [[nodiscard]] ConversationResumeDeliveryRegistrationStats stats() const noexcept {
      return counters;
    }

  private:
    std::unordered_set<std::string> actionIds;
    std::unordered_set<std::string> ackKeys;
    ConversationResumeDeliveryRegistrationStats counters;
};

} // namespace Mmo::Server

