#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mmo_outbound_gameplay_delivery_state.h"
#include "mmo_server_conversation_resume_delivery_registration_boundary.h"
#include "mmo_server_conversation_resume_dispatch_envelope.h"
#include "mmo_server_conversation_session_boundary.h"

namespace Mmo::Server {

enum class ConversationResumeMutationGuardStatus : std::uint8_t {
  ReadyNoMutation = 0,
  GuardDisabled,
  DispatchEnvelopeNotEligible,
  RegistrationBoundaryNotEligible,
  DispatchRegistrationMismatch,
  MissingDeliveryState,
  MissingConversationRuntime,
  MissingObserverSession,
  MissingConversationId,
  MissingActionId,
  MissingAckKey,
  MissingEndpointText,
  MissingDatagram,
  DatagramTooLarge,
  MissingConversationSession,
  AlreadyInOutboundDelivery,
  AlreadyConversationObserverAckKey,
  AlreadyConversationObserverSession,
  RegistrationAttemptMismatch,
  RegistrationObserverMismatch,
  DuplicateGuardActionId,
  DuplicateGuardAckKey,
};

[[nodiscard]] constexpr const char* conversationResumeMutationGuardStatusName(
    ConversationResumeMutationGuardStatus status) noexcept {
  switch(status) {
    case ConversationResumeMutationGuardStatus::ReadyNoMutation:                  return "ready_no_mutation";
    case ConversationResumeMutationGuardStatus::GuardDisabled:                   return "guard_disabled";
    case ConversationResumeMutationGuardStatus::DispatchEnvelopeNotEligible:     return "dispatch_envelope_not_eligible";
    case ConversationResumeMutationGuardStatus::RegistrationBoundaryNotEligible: return "registration_boundary_not_eligible";
    case ConversationResumeMutationGuardStatus::DispatchRegistrationMismatch:    return "dispatch_registration_mismatch";
    case ConversationResumeMutationGuardStatus::MissingDeliveryState:            return "missing_delivery_state";
    case ConversationResumeMutationGuardStatus::MissingConversationRuntime:      return "missing_conversation_runtime";
    case ConversationResumeMutationGuardStatus::MissingObserverSession:          return "missing_observer_session";
    case ConversationResumeMutationGuardStatus::MissingConversationId:           return "missing_conversation_id";
    case ConversationResumeMutationGuardStatus::MissingActionId:                 return "missing_action_id";
    case ConversationResumeMutationGuardStatus::MissingAckKey:                   return "missing_ack_key";
    case ConversationResumeMutationGuardStatus::MissingEndpointText:             return "missing_endpoint_text";
    case ConversationResumeMutationGuardStatus::MissingDatagram:                 return "missing_datagram";
    case ConversationResumeMutationGuardStatus::DatagramTooLarge:                return "datagram_too_large";
    case ConversationResumeMutationGuardStatus::MissingConversationSession:      return "missing_conversation_session";
    case ConversationResumeMutationGuardStatus::AlreadyInOutboundDelivery:       return "already_in_outbound_delivery";
    case ConversationResumeMutationGuardStatus::AlreadyConversationObserverAckKey:return "already_conversation_observer_ack_key";
    case ConversationResumeMutationGuardStatus::AlreadyConversationObserverSession:return "already_conversation_observer_session";
    case ConversationResumeMutationGuardStatus::RegistrationAttemptMismatch:     return "registration_attempt_mismatch";
    case ConversationResumeMutationGuardStatus::RegistrationObserverMismatch:    return "registration_observer_mismatch";
    case ConversationResumeMutationGuardStatus::DuplicateGuardActionId:          return "duplicate_guard_action_id";
    case ConversationResumeMutationGuardStatus::DuplicateGuardAckKey:            return "duplicate_guard_ack_key";
  }
  return "unknown";
}

struct ConversationResumeMutationGuardRequest final {
  const ConversationResumeDispatchEnvelopeResult* dispatchEnvelope = nullptr;
  const ConversationResumeDeliveryRegistrationResult* registrationBoundary = nullptr;
  const OutboundGameplayDeliveryState* deliveryState = nullptr;
  const ConversationSessionRuntime* conversationRuntime = nullptr;
  bool guardEnabled = false;
  std::uint64_t nowMs = 0;
};

struct ConversationResumeMutationGuardResult final {
  ConversationResumeMutationGuardStatus status = ConversationResumeMutationGuardStatus::GuardDisabled;
  bool ready = false;
  bool duplicate = false;
  bool wouldDispatchUdp = false;
  bool wouldRegisterDelivery = false;
  bool wouldRegisterConversationObserver = false;
  bool wouldMutateRuntime = false;
  bool mutatedRuntime = false;
  bool requiresDurableSendReceipt = true;
  bool requiresDurableObserverReceipt = true;
  bool requiresSendFailureTerminalState = true;
  std::string conversationId;
  std::string sessionUuid;
  std::string characterKey;
  std::string actionId;
  std::string ackKey;
  std::string endpointText;
  std::string commitOrder;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  std::uint64_t sentAtMs = 0;
  std::uint64_t ackDeadlineMs = 0;
  std::uint64_t guardedAtMs = 0;
  std::size_t encodedBytes = 0;
  OutboundGameplayAttempt deliveryAttempt;
  ConversationObserverSent conversationObserver;
  std::vector<std::uint8_t> datagram;
  std::string reason;
};

struct ConversationResumeMutationGuardStats final {
  std::uint64_t requests = 0;
  std::uint64_t ready = 0;
  std::uint64_t disabled = 0;
  std::uint64_t dispatchNotEligible = 0;
  std::uint64_t registrationNotEligible = 0;
  std::uint64_t identityMismatch = 0;
  std::uint64_t missingRuntime = 0;
  std::uint64_t missingIdentity = 0;
  std::uint64_t missingEndpoint = 0;
  std::uint64_t missingDatagram = 0;
  std::uint64_t datagramTooLarge = 0;
  std::uint64_t missingConversationSession = 0;
  std::uint64_t alreadyDelivery = 0;
  std::uint64_t alreadyObserverAckKey = 0;
  std::uint64_t alreadyObserverSession = 0;
  std::uint64_t registrationAttemptMismatch = 0;
  std::uint64_t registrationObserverMismatch = 0;
  std::uint64_t duplicateGuardActionId = 0;
  std::uint64_t duplicateGuardAckKey = 0;
};

class ConversationResumeMutationGuard final {
  public:
    [[nodiscard]] ConversationResumeMutationGuardResult classifyNoMutation(
        const ConversationResumeMutationGuardRequest& request) {
      ++counters.requests;

      ConversationResumeMutationGuardResult out;
      out.guardedAtMs = request.nowMs;

      if(!request.guardEnabled) {
        ++counters.disabled;
        out.status = ConversationResumeMutationGuardStatus::GuardDisabled;
        out.reason = "resume_mutation_guard_disabled";
        return out;
      }

      if(request.dispatchEnvelope == nullptr || !request.dispatchEnvelope->eligible) {
        ++counters.dispatchNotEligible;
        out.status = ConversationResumeMutationGuardStatus::DispatchEnvelopeNotEligible;
        out.reason = request.dispatchEnvelope == nullptr ? "missing_resume_dispatch_envelope_result"
                                                         : request.dispatchEnvelope->reason;
        return out;
      }

      if(request.registrationBoundary == nullptr || !request.registrationBoundary->eligible) {
        ++counters.registrationNotEligible;
        out.status = ConversationResumeMutationGuardStatus::RegistrationBoundaryNotEligible;
        out.reason = request.registrationBoundary == nullptr ? "missing_resume_delivery_registration_result"
                                                             : request.registrationBoundary->reason;
        return out;
      }

      const auto& envelope = *request.dispatchEnvelope;
      const auto& registration = *request.registrationBoundary;
      out.conversationId = envelope.conversationId;
      out.sessionUuid = envelope.sessionUuid;
      out.characterKey = envelope.characterKey;
      out.actionId = envelope.actionId;
      out.ackKey = envelope.ackKey;
      out.endpointText = envelope.endpointText;
      out.packetSequence = envelope.packetSequence;
      out.localSequence = envelope.localSequence;
      out.serverTick = envelope.serverTick;
      out.sentAtMs = envelope.sentAtMs;
      out.ackDeadlineMs = envelope.ackDeadlineMs;
      out.encodedBytes = envelope.encodedBytes;
      out.wouldDispatchUdp = envelope.wouldDispatchUdp;
      out.wouldRegisterDelivery = registration.wouldRegisterDelivery;
      out.wouldRegisterConversationObserver = registration.wouldRegisterConversationObserver;

      if(std::string_view(envelope.conversationId) != std::string_view(registration.conversationId) ||
         std::string_view(envelope.sessionUuid) != std::string_view(registration.sessionUuid) ||
         std::string_view(envelope.actionId) != std::string_view(registration.actionId) ||
         std::string_view(envelope.ackKey) != std::string_view(registration.ackKey) ||
         envelope.packetSequence != registration.packetSequence) {
        ++counters.identityMismatch;
        out.status = ConversationResumeMutationGuardStatus::DispatchRegistrationMismatch;
        out.reason = "resume_dispatch_envelope_and_registration_identity_mismatch";
        return out;
      }

      if(envelope.sessionUuid.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeMutationGuardStatus::MissingObserverSession;
        out.reason = "resume_mutation_guard_requires_observer_session_uuid";
        return out;
      }
      if(envelope.conversationId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeMutationGuardStatus::MissingConversationId;
        out.reason = "resume_mutation_guard_requires_conversation_id";
        return out;
      }
      if(envelope.actionId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeMutationGuardStatus::MissingActionId;
        out.reason = "resume_mutation_guard_requires_action_id";
        return out;
      }
      if(envelope.ackKey.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeMutationGuardStatus::MissingAckKey;
        out.reason = "resume_mutation_guard_requires_ack_key";
        return out;
      }
      if(envelope.endpointText.empty()) {
        ++counters.missingEndpoint;
        out.status = ConversationResumeMutationGuardStatus::MissingEndpointText;
        out.reason = "resume_mutation_guard_requires_endpoint_text";
        return out;
      }
      if(envelope.datagram.empty() || envelope.encodedBytes == 0) {
        ++counters.missingDatagram;
        out.status = ConversationResumeMutationGuardStatus::MissingDatagram;
        out.reason = "resume_mutation_guard_requires_encoded_datagram";
        return out;
      }
      if(envelope.datagram.size() > Mmo::Net::MaxDatagramBytes || envelope.encodedBytes > Mmo::Net::MaxDatagramBytes) {
        ++counters.datagramTooLarge;
        out.status = ConversationResumeMutationGuardStatus::DatagramTooLarge;
        out.reason = "resume_mutation_guard_datagram_too_large";
        return out;
      }

      if(request.deliveryState == nullptr) {
        ++counters.missingRuntime;
        out.status = ConversationResumeMutationGuardStatus::MissingDeliveryState;
        out.reason = "resume_mutation_guard_requires_outbound_delivery_state";
        return out;
      }
      if(request.conversationRuntime == nullptr) {
        ++counters.missingRuntime;
        out.status = ConversationResumeMutationGuardStatus::MissingConversationRuntime;
        out.reason = "resume_mutation_guard_requires_conversation_runtime";
        return out;
      }
      if(!request.conversationRuntime->hasConversation(envelope.conversationId)) {
        ++counters.missingConversationSession;
        out.status = ConversationResumeMutationGuardStatus::MissingConversationSession;
        out.reason = "resume_mutation_guard_conversation_session_not_open";
        return out;
      }
      if(request.deliveryState->hasAttempt(envelope.ackKey)) {
        ++counters.alreadyDelivery;
        out.status = ConversationResumeMutationGuardStatus::AlreadyInOutboundDelivery;
        out.duplicate = true;
        out.reason = "resume_mutation_guard_ack_key_already_in_outbound_delivery";
        return out;
      }
      if(request.conversationRuntime->hasObserverAckKey(envelope.ackKey)) {
        ++counters.alreadyObserverAckKey;
        out.status = ConversationResumeMutationGuardStatus::AlreadyConversationObserverAckKey;
        out.duplicate = true;
        out.reason = "resume_mutation_guard_ack_key_already_in_conversation_runtime";
        return out;
      }
      if(request.conversationRuntime->hasObserverSession(envelope.conversationId, envelope.sessionUuid)) {
        ++counters.alreadyObserverSession;
        out.status = ConversationResumeMutationGuardStatus::AlreadyConversationObserverSession;
        out.duplicate = true;
        out.reason = "resume_mutation_guard_session_already_observes_conversation";
        return out;
      }

      const auto& attempt = registration.deliveryAttempt;
      if(std::string_view(attempt.actionId) != std::string_view(envelope.actionId) ||
         std::string_view(attempt.ackKey) != std::string_view(envelope.ackKey) ||
         std::string_view(attempt.sessionUuid) != std::string_view(envelope.sessionUuid) ||
         attempt.packetSequence != envelope.packetSequence ||
         attempt.localSequence != envelope.localSequence ||
         attempt.serverTick != envelope.serverTick ||
         attempt.ackDeadlineMs != envelope.ackDeadlineMs) {
        ++counters.registrationAttemptMismatch;
        out.status = ConversationResumeMutationGuardStatus::RegistrationAttemptMismatch;
        out.reason = "resume_mutation_guard_delivery_attempt_preview_mismatch";
        return out;
      }

      const auto& observer = registration.conversationObserver;
      if(std::string_view(observer.actionId) != std::string_view(envelope.actionId) ||
         std::string_view(observer.ackKey) != std::string_view(envelope.ackKey) ||
         std::string_view(observer.sessionUuid) != std::string_view(envelope.sessionUuid) ||
         observer.packetSequence != envelope.packetSequence ||
         observer.localSequence != envelope.localSequence ||
         observer.ackDeadlineMs != envelope.ackDeadlineMs) {
        ++counters.registrationObserverMismatch;
        out.status = ConversationResumeMutationGuardStatus::RegistrationObserverMismatch;
        out.reason = "resume_mutation_guard_conversation_observer_preview_mismatch";
        return out;
      }

      if(actionIds.contains(envelope.actionId)) {
        ++counters.duplicateGuardActionId;
        out.status = ConversationResumeMutationGuardStatus::DuplicateGuardActionId;
        out.duplicate = true;
        out.reason = "resume_action_id_already_reserved_by_mutation_guard";
        return out;
      }
      if(ackKeys.contains(envelope.ackKey)) {
        ++counters.duplicateGuardAckKey;
        out.status = ConversationResumeMutationGuardStatus::DuplicateGuardAckKey;
        out.duplicate = true;
        out.reason = "resume_ack_key_already_reserved_by_mutation_guard";
        return out;
      }

      actionIds.insert(envelope.actionId);
      ackKeys.insert(envelope.ackKey);
      ++counters.ready;

      out.status = ConversationResumeMutationGuardStatus::ReadyNoMutation;
      out.ready = true;
      out.wouldDispatchUdp = true;
      out.wouldRegisterDelivery = true;
      out.wouldRegisterConversationObserver = true;
      out.wouldMutateRuntime = true;
      out.mutatedRuntime = false;
      out.commitOrder = "record_delivery_then_record_conversation_observer_then_udp_send_then_terminalize_send_failure_if_any";
      out.deliveryAttempt = attempt;
      out.conversationObserver = observer;
      out.datagram = envelope.datagram;
      out.reason = "resume_mutation_guard_ready_no_mutation";
      return out;
    }

    [[nodiscard]] ConversationResumeMutationGuardStats stats() const noexcept {
      return counters;
    }

  private:
    std::unordered_set<std::string> actionIds;
    std::unordered_set<std::string> ackKeys;
    ConversationResumeMutationGuardStats counters;
};

} // namespace Mmo::Server

