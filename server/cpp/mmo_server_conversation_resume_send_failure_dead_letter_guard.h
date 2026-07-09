#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

#include "mmo_outbound_gameplay_delivery_state.h"
#include "mmo_server_conversation_resume_mutation_guard.h"
#include "mmo_server_conversation_session_boundary.h"

namespace Mmo::Server {

enum class ConversationResumeSendFailureDeadLetterGuardStatus : std::uint8_t {
  ReadyNoMutation = 0,
  GuardDisabled,
  MutationGuardNotReady,
  MissingDeliveryState,
  MissingConversationRuntime,
  MissingConversationId,
  MissingObserverSession,
  MissingActionId,
  MissingAckKey,
  MissingEndpointText,
  MissingDatagram,
  MissingFailureReason,
  MissingConversationSession,
  AlreadyInOutboundDelivery,
  AlreadyConversationObserverAckKey,
  AlreadyConversationObserverSession,
  DuplicateDeadLetterActionId,
  DuplicateDeadLetterAckKey,
};

[[nodiscard]] constexpr const char* conversationResumeSendFailureDeadLetterGuardStatusName(
    ConversationResumeSendFailureDeadLetterGuardStatus status) noexcept {
  switch(status) {
    case ConversationResumeSendFailureDeadLetterGuardStatus::ReadyNoMutation:                   return "ready_no_mutation";
    case ConversationResumeSendFailureDeadLetterGuardStatus::GuardDisabled:                    return "guard_disabled";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MutationGuardNotReady:            return "mutation_guard_not_ready";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingDeliveryState:             return "missing_delivery_state";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingConversationRuntime:       return "missing_conversation_runtime";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingConversationId:            return "missing_conversation_id";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingObserverSession:           return "missing_observer_session";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingActionId:                  return "missing_action_id";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingAckKey:                    return "missing_ack_key";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingEndpointText:              return "missing_endpoint_text";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingDatagram:                  return "missing_datagram";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingFailureReason:             return "missing_failure_reason";
    case ConversationResumeSendFailureDeadLetterGuardStatus::MissingConversationSession:       return "missing_conversation_session";
    case ConversationResumeSendFailureDeadLetterGuardStatus::AlreadyInOutboundDelivery:        return "already_in_outbound_delivery";
    case ConversationResumeSendFailureDeadLetterGuardStatus::AlreadyConversationObserverAckKey:return "already_conversation_observer_ack_key";
    case ConversationResumeSendFailureDeadLetterGuardStatus::AlreadyConversationObserverSession:return "already_conversation_observer_session";
    case ConversationResumeSendFailureDeadLetterGuardStatus::DuplicateDeadLetterActionId:      return "duplicate_dead_letter_action_id";
    case ConversationResumeSendFailureDeadLetterGuardStatus::DuplicateDeadLetterAckKey:        return "duplicate_dead_letter_ack_key";
  }
  return "unknown";
}

struct ConversationResumeSendFailureDeadLetterGuardRequest final {
  const ConversationResumeMutationGuardResult* mutationGuard = nullptr;
  const OutboundGameplayDeliveryState* deliveryState = nullptr;
  const ConversationSessionRuntime* conversationRuntime = nullptr;
  bool guardEnabled = false;
  std::uint64_t nowMs = 0;
  std::string failureReason = "udp_send_failure_dead_letter_preview";
  std::string failureMessage;
};

struct ConversationResumeSendFailureDeadLetterPreview final {
  std::string actionId;
  std::string ackKey;
  std::string conversationId;
  std::string sessionUuid;
  std::string characterKey;
  std::string endpointText;
  std::string failureReason;
  std::string failureMessage;
  OutboundGameplayDeliveryStatus outboundTerminalStatus = OutboundGameplayDeliveryStatus::Nacked;
  ConversationObserverStatus observerTerminalStatus = ConversationObserverStatus::Nacked;
  ConversationSessionStatus expectedConversationStatusAfterTerminal = ConversationSessionStatus::MixedTerminal;
  std::uint64_t terminalAtMs = 0;
  std::uint64_t packetSequence = 0;
};

struct ConversationResumeSendFailureDeadLetterGuardResult final {
  ConversationResumeSendFailureDeadLetterGuardStatus status = ConversationResumeSendFailureDeadLetterGuardStatus::GuardDisabled;
  bool ready = false;
  bool duplicate = false;
  bool wouldDispatchUdp = false;
  bool wouldRegisterDelivery = false;
  bool wouldRegisterConversationObserver = false;
  bool wouldTerminalizeOutboundDelivery = false;
  bool wouldTerminalizeConversationObserver = false;
  bool wouldDeadLetter = false;
  bool wouldMutateRuntime = false;
  bool mutatedRuntime = false;
  bool requiresDurableSendFailureReceipt = true;
  bool requiresDurableDeadLetterState = true;
  bool requiresDurableObserverTerminalReceipt = true;
  std::string conversationId;
  std::string sessionUuid;
  std::string characterKey;
  std::string endpointText;
  std::string actionId;
  std::string ackKey;
  std::string failureReason;
  std::string failureMessage;
  std::string commitOrder;
  std::uint64_t packetSequence = 0;
  std::uint64_t sentAtMs = 0;
  std::uint64_t ackDeadlineMs = 0;
  std::uint64_t guardedAtMs = 0;
  std::size_t encodedBytes = 0;
  ConversationResumeSendFailureDeadLetterPreview preview;
  std::string reason;
};

struct ConversationResumeSendFailureDeadLetterGuardStats final {
  std::uint64_t requests = 0;
  std::uint64_t ready = 0;
  std::uint64_t disabled = 0;
  std::uint64_t mutationGuardNotReady = 0;
  std::uint64_t missingRuntime = 0;
  std::uint64_t missingIdentity = 0;
  std::uint64_t missingEndpoint = 0;
  std::uint64_t missingDatagram = 0;
  std::uint64_t missingFailureReason = 0;
  std::uint64_t missingConversationSession = 0;
  std::uint64_t alreadyDelivery = 0;
  std::uint64_t alreadyObserverAckKey = 0;
  std::uint64_t alreadyObserverSession = 0;
  std::uint64_t duplicateDeadLetterActionId = 0;
  std::uint64_t duplicateDeadLetterAckKey = 0;
};

class ConversationResumeSendFailureDeadLetterGuard final {
  public:
    [[nodiscard]] ConversationResumeSendFailureDeadLetterGuardResult classifyNoMutation(
        const ConversationResumeSendFailureDeadLetterGuardRequest& request) {
      ++counters.requests;

      ConversationResumeSendFailureDeadLetterGuardResult out;
      out.guardedAtMs = request.nowMs;
      out.failureReason = request.failureReason;
      out.failureMessage = request.failureMessage;

      if(!request.guardEnabled) {
        ++counters.disabled;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::GuardDisabled;
        out.reason = "resume_send_failure_dead_letter_guard_disabled";
        return out;
      }

      if(request.mutationGuard == nullptr || !request.mutationGuard->ready) {
        ++counters.mutationGuardNotReady;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MutationGuardNotReady;
        out.reason = request.mutationGuard == nullptr ? "missing_resume_mutation_guard_result"
                                                       : request.mutationGuard->reason;
        return out;
      }

      const auto& mutation = *request.mutationGuard;
      copyMutationIdentity(out, mutation);

      if(mutation.conversationId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingConversationId;
        out.reason = "resume_dead_letter_requires_conversation_id";
        return out;
      }
      if(mutation.sessionUuid.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingObserverSession;
        out.reason = "resume_dead_letter_requires_observer_session_uuid";
        return out;
      }
      if(mutation.actionId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingActionId;
        out.reason = "resume_dead_letter_requires_action_id";
        return out;
      }
      if(mutation.ackKey.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingAckKey;
        out.reason = "resume_dead_letter_requires_ack_key";
        return out;
      }
      if(mutation.endpointText.empty()) {
        ++counters.missingEndpoint;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingEndpointText;
        out.reason = "resume_dead_letter_requires_endpoint_text";
        return out;
      }
      if(mutation.datagram.empty() || mutation.encodedBytes == 0) {
        ++counters.missingDatagram;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingDatagram;
        out.reason = "resume_dead_letter_requires_dispatch_datagram_preview";
        return out;
      }
      if(request.failureReason.empty()) {
        ++counters.missingFailureReason;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingFailureReason;
        out.reason = "resume_dead_letter_requires_failure_reason";
        return out;
      }
      if(request.deliveryState == nullptr) {
        ++counters.missingRuntime;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingDeliveryState;
        out.reason = "resume_dead_letter_requires_outbound_delivery_state";
        return out;
      }
      if(request.conversationRuntime == nullptr) {
        ++counters.missingRuntime;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingConversationRuntime;
        out.reason = "resume_dead_letter_requires_conversation_runtime";
        return out;
      }
      if(!request.conversationRuntime->hasConversation(mutation.conversationId)) {
        ++counters.missingConversationSession;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::MissingConversationSession;
        out.reason = "resume_dead_letter_conversation_session_not_open";
        return out;
      }
      if(request.deliveryState->hasAttempt(mutation.ackKey)) {
        ++counters.alreadyDelivery;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::AlreadyInOutboundDelivery;
        out.duplicate = true;
        out.reason = "resume_dead_letter_ack_key_already_in_outbound_delivery";
        return out;
      }
      if(request.conversationRuntime->hasObserverAckKey(mutation.ackKey)) {
        ++counters.alreadyObserverAckKey;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::AlreadyConversationObserverAckKey;
        out.duplicate = true;
        out.reason = "resume_dead_letter_ack_key_already_in_conversation_runtime";
        return out;
      }
      if(request.conversationRuntime->hasObserverSession(mutation.conversationId, mutation.sessionUuid)) {
        ++counters.alreadyObserverSession;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::AlreadyConversationObserverSession;
        out.duplicate = true;
        out.reason = "resume_dead_letter_session_already_observes_conversation";
        return out;
      }
      if(actionIds.contains(mutation.actionId)) {
        ++counters.duplicateDeadLetterActionId;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::DuplicateDeadLetterActionId;
        out.duplicate = true;
        out.reason = "resume_action_id_already_reserved_by_dead_letter_guard";
        return out;
      }
      if(ackKeys.contains(mutation.ackKey)) {
        ++counters.duplicateDeadLetterAckKey;
        out.status = ConversationResumeSendFailureDeadLetterGuardStatus::DuplicateDeadLetterAckKey;
        out.duplicate = true;
        out.reason = "resume_ack_key_already_reserved_by_dead_letter_guard";
        return out;
      }

      actionIds.insert(mutation.actionId);
      ackKeys.insert(mutation.ackKey);
      ++counters.ready;

      out.status = ConversationResumeSendFailureDeadLetterGuardStatus::ReadyNoMutation;
      out.ready = true;
      out.wouldDispatchUdp = true;
      out.wouldRegisterDelivery = true;
      out.wouldRegisterConversationObserver = true;
      out.wouldTerminalizeOutboundDelivery = true;
      out.wouldTerminalizeConversationObserver = true;
      out.wouldDeadLetter = true;
      out.wouldMutateRuntime = true;
      out.mutatedRuntime = false;
      out.commitOrder = "record_delivery_then_record_conversation_observer_then_udp_send_then_on_send_failure_record_nack_and_dead_letter";
      out.preview = makePreview(out, request.nowMs);
      out.reason = "resume_send_failure_dead_letter_guard_ready_no_mutation";
      return out;
    }

    [[nodiscard]] ConversationResumeSendFailureDeadLetterGuardStats stats() const noexcept {
      return counters;
    }

  private:
    static void copyMutationIdentity(ConversationResumeSendFailureDeadLetterGuardResult& out,
                                     const ConversationResumeMutationGuardResult& mutation) {
      out.conversationId = mutation.conversationId;
      out.sessionUuid = mutation.sessionUuid;
      out.characterKey = mutation.characterKey;
      out.endpointText = mutation.endpointText;
      out.actionId = mutation.actionId;
      out.ackKey = mutation.ackKey;
      out.packetSequence = mutation.packetSequence;
      out.sentAtMs = mutation.sentAtMs;
      out.ackDeadlineMs = mutation.ackDeadlineMs;
      out.encodedBytes = mutation.encodedBytes;
      out.wouldDispatchUdp = mutation.wouldDispatchUdp;
      out.wouldRegisterDelivery = mutation.wouldRegisterDelivery;
      out.wouldRegisterConversationObserver = mutation.wouldRegisterConversationObserver;
    }

    [[nodiscard]] static ConversationResumeSendFailureDeadLetterPreview makePreview(
        const ConversationResumeSendFailureDeadLetterGuardResult& result,
        std::uint64_t terminalAtMs) {
      ConversationResumeSendFailureDeadLetterPreview out;
      out.actionId = result.actionId;
      out.ackKey = result.ackKey;
      out.conversationId = result.conversationId;
      out.sessionUuid = result.sessionUuid;
      out.characterKey = result.characterKey;
      out.endpointText = result.endpointText;
      out.failureReason = result.failureReason;
      out.failureMessage = result.failureMessage;
      out.terminalAtMs = terminalAtMs;
      out.packetSequence = result.packetSequence;
      return out;
    }

    std::unordered_set<std::string> actionIds;
    std::unordered_set<std::string> ackKeys;
    ConversationResumeSendFailureDeadLetterGuardStats counters;
};

} // namespace Mmo::Server
