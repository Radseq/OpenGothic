#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "mmo_outbound_gameplay_delivery_state.h"
#include "mmo_server_conversation_resume_mutation_guard.h"
#include "mmo_server_conversation_resume_send_failure_dead_letter_guard.h"
#include "mmo_server_conversation_session_boundary.h"

namespace Mmo::Server {

enum class ConversationResumeCommitPreflightStatus : std::uint8_t {
  ReadyNoMutation = 0,
  PreflightDisabled,
  MutationGuardNotReady,
  SendFailureGuardNotReady,
  MutationFailureGuardMismatch,
  MissingDeliveryState,
  MissingConversationRuntime,
  MissingConversationId,
  MissingObserverSession,
  MissingActionId,
  MissingAckKey,
  MissingEndpointText,
  MissingDatagram,
  MissingConversationSession,
  AlreadyInOutboundDelivery,
  AlreadyConversationObserverAckKey,
  AlreadyConversationObserverSession,
  MissingCommitPlanStep,
  DuplicatePreflightActionId,
  DuplicatePreflightAckKey,
};

[[nodiscard]] constexpr const char* conversationResumeCommitPreflightStatusName(
    ConversationResumeCommitPreflightStatus status) noexcept {
  switch(status) {
    case ConversationResumeCommitPreflightStatus::ReadyNoMutation:                 return "ready_no_mutation";
    case ConversationResumeCommitPreflightStatus::PreflightDisabled:              return "preflight_disabled";
    case ConversationResumeCommitPreflightStatus::MutationGuardNotReady:          return "mutation_guard_not_ready";
    case ConversationResumeCommitPreflightStatus::SendFailureGuardNotReady:       return "send_failure_guard_not_ready";
    case ConversationResumeCommitPreflightStatus::MutationFailureGuardMismatch:   return "mutation_failure_guard_mismatch";
    case ConversationResumeCommitPreflightStatus::MissingDeliveryState:           return "missing_delivery_state";
    case ConversationResumeCommitPreflightStatus::MissingConversationRuntime:     return "missing_conversation_runtime";
    case ConversationResumeCommitPreflightStatus::MissingConversationId:          return "missing_conversation_id";
    case ConversationResumeCommitPreflightStatus::MissingObserverSession:         return "missing_observer_session";
    case ConversationResumeCommitPreflightStatus::MissingActionId:                return "missing_action_id";
    case ConversationResumeCommitPreflightStatus::MissingAckKey:                  return "missing_ack_key";
    case ConversationResumeCommitPreflightStatus::MissingEndpointText:            return "missing_endpoint_text";
    case ConversationResumeCommitPreflightStatus::MissingDatagram:                return "missing_datagram";
    case ConversationResumeCommitPreflightStatus::MissingConversationSession:     return "missing_conversation_session";
    case ConversationResumeCommitPreflightStatus::AlreadyInOutboundDelivery:      return "already_in_outbound_delivery";
    case ConversationResumeCommitPreflightStatus::AlreadyConversationObserverAckKey:
      return "already_conversation_observer_ack_key";
    case ConversationResumeCommitPreflightStatus::AlreadyConversationObserverSession:
      return "already_conversation_observer_session";
    case ConversationResumeCommitPreflightStatus::MissingCommitPlanStep:          return "missing_commit_plan_step";
    case ConversationResumeCommitPreflightStatus::DuplicatePreflightActionId:     return "duplicate_preflight_action_id";
    case ConversationResumeCommitPreflightStatus::DuplicatePreflightAckKey:       return "duplicate_preflight_ack_key";
  }
  return "unknown";
}

enum class ConversationResumeCommitPlanStepKind : std::uint8_t {
  RecordOutboundDeliveryPending = 0,
  RecordConversationObserverPending,
  DispatchUdpDatagram,
  AwaitClientTransportAck,
  OnSendFailureTerminalizeOutboundDelivery,
  OnSendFailureTerminalizeConversationObserver,
  OnSendFailureDeadLetter,
};

[[nodiscard]] constexpr const char* conversationResumeCommitPlanStepKindName(
    ConversationResumeCommitPlanStepKind kind) noexcept {
  switch(kind) {
    case ConversationResumeCommitPlanStepKind::RecordOutboundDeliveryPending:
      return "record_outbound_delivery_pending";
    case ConversationResumeCommitPlanStepKind::RecordConversationObserverPending:
      return "record_conversation_observer_pending";
    case ConversationResumeCommitPlanStepKind::DispatchUdpDatagram:
      return "dispatch_udp_datagram";
    case ConversationResumeCommitPlanStepKind::AwaitClientTransportAck:
      return "await_client_transport_ack";
    case ConversationResumeCommitPlanStepKind::OnSendFailureTerminalizeOutboundDelivery:
      return "on_send_failure_terminalize_outbound_delivery";
    case ConversationResumeCommitPlanStepKind::OnSendFailureTerminalizeConversationObserver:
      return "on_send_failure_terminalize_conversation_observer";
    case ConversationResumeCommitPlanStepKind::OnSendFailureDeadLetter:
      return "on_send_failure_dead_letter";
  }
  return "unknown";
}

struct ConversationResumeCommitPlanStep final {
  ConversationResumeCommitPlanStepKind kind = ConversationResumeCommitPlanStepKind::RecordOutboundDeliveryPending;
  bool runtimeMutation = false;
  bool durableRequired = false;
  bool sendSideEffect = false;
  bool failureBranch = false;
  std::string idempotencyKey;
};

struct ConversationResumeCommitPreflightRequest final {
  const ConversationResumeMutationGuardResult* mutationGuard = nullptr;
  const ConversationResumeSendFailureDeadLetterGuardResult* sendFailureGuard = nullptr;
  const OutboundGameplayDeliveryState* deliveryState = nullptr;
  const ConversationSessionRuntime* conversationRuntime = nullptr;
  bool preflightEnabled = false;
  std::uint64_t nowMs = 0;
};

struct ConversationResumeCommitPreflightResult final {
  ConversationResumeCommitPreflightStatus status = ConversationResumeCommitPreflightStatus::PreflightDisabled;
  bool ready = false;
  bool duplicate = false;
  bool wouldRegisterDelivery = false;
  bool wouldRegisterConversationObserver = false;
  bool wouldDispatchUdp = false;
  bool wouldAwaitClientTransportAck = false;
  bool wouldHandleSendFailure = false;
  bool wouldTerminalizeOutboundDeliveryOnSendFailure = false;
  bool wouldTerminalizeConversationObserverOnSendFailure = false;
  bool wouldDeadLetterOnSendFailure = false;
  bool wouldMutateRuntime = false;
  bool mutatedRuntime = false;
  bool wouldUseSingleAtomicRuntimeTransaction = true;
  bool requiresDurableSendReceipt = true;
  bool requiresDurableObserverReceipt = true;
  bool requiresDurableAckReceipt = true;
  bool requiresDurableSendFailureReceipt = true;
  bool requiresDurableDeadLetterState = true;
  std::string conversationId;
  std::string sessionUuid;
  std::string characterKey;
  std::string endpointText;
  std::string actionId;
  std::string ackKey;
  std::string successCommitOrder;
  std::string failureCommitOrder;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t serverTick = 0;
  std::uint64_t sentAtMs = 0;
  std::uint64_t ackDeadlineMs = 0;
  std::uint64_t preflightAtMs = 0;
  std::size_t encodedBytes = 0;
  std::vector<ConversationResumeCommitPlanStep> planSteps;
  std::string reason;
};

struct ConversationResumeCommitPreflightStats final {
  std::uint64_t requests = 0;
  std::uint64_t ready = 0;
  std::uint64_t disabled = 0;
  std::uint64_t mutationGuardNotReady = 0;
  std::uint64_t sendFailureGuardNotReady = 0;
  std::uint64_t identityMismatch = 0;
  std::uint64_t missingRuntime = 0;
  std::uint64_t missingIdentity = 0;
  std::uint64_t missingEndpoint = 0;
  std::uint64_t missingDatagram = 0;
  std::uint64_t missingConversationSession = 0;
  std::uint64_t alreadyDelivery = 0;
  std::uint64_t alreadyObserverAckKey = 0;
  std::uint64_t alreadyObserverSession = 0;
  std::uint64_t missingCommitPlanStep = 0;
  std::uint64_t duplicatePreflightActionId = 0;
  std::uint64_t duplicatePreflightAckKey = 0;
};

class ConversationResumeCommitPreflight final {
  public:
    [[nodiscard]] ConversationResumeCommitPreflightResult classifyNoMutation(
        const ConversationResumeCommitPreflightRequest& request) {
      ++counters.requests;

      ConversationResumeCommitPreflightResult out;
      out.preflightAtMs = request.nowMs;

      if(!request.preflightEnabled) {
        ++counters.disabled;
        out.status = ConversationResumeCommitPreflightStatus::PreflightDisabled;
        out.reason = "resume_commit_preflight_disabled";
        return out;
      }

      if(request.mutationGuard == nullptr || !request.mutationGuard->ready) {
        ++counters.mutationGuardNotReady;
        out.status = ConversationResumeCommitPreflightStatus::MutationGuardNotReady;
        out.reason = request.mutationGuard == nullptr ? "missing_resume_mutation_guard_result"
                                                       : request.mutationGuard->reason;
        return out;
      }

      if(request.sendFailureGuard == nullptr || !request.sendFailureGuard->ready) {
        ++counters.sendFailureGuardNotReady;
        out.status = ConversationResumeCommitPreflightStatus::SendFailureGuardNotReady;
        out.reason = request.sendFailureGuard == nullptr ? "missing_resume_send_failure_guard_result"
                                                         : request.sendFailureGuard->reason;
        return out;
      }

      const auto& mutation = *request.mutationGuard;
      const auto& failure = *request.sendFailureGuard;
      copyMutationIdentity(out, mutation);

      if(std::string_view(mutation.conversationId) != std::string_view(failure.conversationId) ||
         std::string_view(mutation.sessionUuid) != std::string_view(failure.sessionUuid) ||
         std::string_view(mutation.actionId) != std::string_view(failure.actionId) ||
         std::string_view(mutation.ackKey) != std::string_view(failure.ackKey) ||
         mutation.packetSequence != failure.packetSequence) {
        ++counters.identityMismatch;
        out.status = ConversationResumeCommitPreflightStatus::MutationFailureGuardMismatch;
        out.reason = "resume_commit_preflight_mutation_and_failure_guard_identity_mismatch";
        return out;
      }

      if(mutation.conversationId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeCommitPreflightStatus::MissingConversationId;
        out.reason = "resume_commit_preflight_requires_conversation_id";
        return out;
      }
      if(mutation.sessionUuid.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeCommitPreflightStatus::MissingObserverSession;
        out.reason = "resume_commit_preflight_requires_observer_session_uuid";
        return out;
      }
      if(mutation.actionId.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeCommitPreflightStatus::MissingActionId;
        out.reason = "resume_commit_preflight_requires_action_id";
        return out;
      }
      if(mutation.ackKey.empty()) {
        ++counters.missingIdentity;
        out.status = ConversationResumeCommitPreflightStatus::MissingAckKey;
        out.reason = "resume_commit_preflight_requires_ack_key";
        return out;
      }
      if(mutation.endpointText.empty()) {
        ++counters.missingEndpoint;
        out.status = ConversationResumeCommitPreflightStatus::MissingEndpointText;
        out.reason = "resume_commit_preflight_requires_endpoint_text";
        return out;
      }
      if(mutation.datagram.empty() || mutation.encodedBytes == 0) {
        ++counters.missingDatagram;
        out.status = ConversationResumeCommitPreflightStatus::MissingDatagram;
        out.reason = "resume_commit_preflight_requires_encoded_datagram";
        return out;
      }
      if(request.deliveryState == nullptr) {
        ++counters.missingRuntime;
        out.status = ConversationResumeCommitPreflightStatus::MissingDeliveryState;
        out.reason = "resume_commit_preflight_requires_outbound_delivery_state";
        return out;
      }
      if(request.conversationRuntime == nullptr) {
        ++counters.missingRuntime;
        out.status = ConversationResumeCommitPreflightStatus::MissingConversationRuntime;
        out.reason = "resume_commit_preflight_requires_conversation_runtime";
        return out;
      }
      if(!request.conversationRuntime->hasConversation(mutation.conversationId)) {
        ++counters.missingConversationSession;
        out.status = ConversationResumeCommitPreflightStatus::MissingConversationSession;
        out.reason = "resume_commit_preflight_conversation_session_not_open";
        return out;
      }
      if(request.deliveryState->hasAttempt(mutation.ackKey)) {
        ++counters.alreadyDelivery;
        out.status = ConversationResumeCommitPreflightStatus::AlreadyInOutboundDelivery;
        out.duplicate = true;
        out.reason = "resume_commit_preflight_ack_key_already_in_outbound_delivery";
        return out;
      }
      if(request.conversationRuntime->hasObserverAckKey(mutation.ackKey)) {
        ++counters.alreadyObserverAckKey;
        out.status = ConversationResumeCommitPreflightStatus::AlreadyConversationObserverAckKey;
        out.duplicate = true;
        out.reason = "resume_commit_preflight_ack_key_already_in_conversation_runtime";
        return out;
      }
      if(request.conversationRuntime->hasObserverSession(mutation.conversationId, mutation.sessionUuid)) {
        ++counters.alreadyObserverSession;
        out.status = ConversationResumeCommitPreflightStatus::AlreadyConversationObserverSession;
        out.duplicate = true;
        out.reason = "resume_commit_preflight_session_already_observes_conversation";
        return out;
      }

      out.planSteps = buildPlanSteps(mutation.ackKey);
      if(out.planSteps.size() != ExpectedCommitPlanStepCount) {
        ++counters.missingCommitPlanStep;
        out.status = ConversationResumeCommitPreflightStatus::MissingCommitPlanStep;
        out.reason = "resume_commit_preflight_internal_plan_step_count_mismatch";
        return out;
      }
      if(actionIds.contains(mutation.actionId)) {
        ++counters.duplicatePreflightActionId;
        out.status = ConversationResumeCommitPreflightStatus::DuplicatePreflightActionId;
        out.duplicate = true;
        out.reason = "resume_action_id_already_reserved_by_commit_preflight";
        return out;
      }
      if(ackKeys.contains(mutation.ackKey)) {
        ++counters.duplicatePreflightAckKey;
        out.status = ConversationResumeCommitPreflightStatus::DuplicatePreflightAckKey;
        out.duplicate = true;
        out.reason = "resume_ack_key_already_reserved_by_commit_preflight";
        return out;
      }

      actionIds.insert(mutation.actionId);
      ackKeys.insert(mutation.ackKey);
      ++counters.ready;

      out.status = ConversationResumeCommitPreflightStatus::ReadyNoMutation;
      out.ready = true;
      out.wouldRegisterDelivery = true;
      out.wouldRegisterConversationObserver = true;
      out.wouldDispatchUdp = true;
      out.wouldAwaitClientTransportAck = true;
      out.wouldHandleSendFailure = true;
      out.wouldTerminalizeOutboundDeliveryOnSendFailure = true;
      out.wouldTerminalizeConversationObserverOnSendFailure = true;
      out.wouldDeadLetterOnSendFailure = true;
      out.wouldMutateRuntime = true;
      out.mutatedRuntime = false;
      out.successCommitOrder = "record_delivery_pending_then_record_conversation_observer_pending_then_udp_send_then_wait_for_client_ack";
      out.failureCommitOrder = "record_delivery_pending_then_record_conversation_observer_pending_then_udp_send_failure_then_terminalize_delivery_then_terminalize_observer_then_dead_letter";
      out.reason = "resume_commit_preflight_ready_no_mutation";
      return out;
    }

    [[nodiscard]] ConversationResumeCommitPreflightStats stats() const noexcept {
      return counters;
    }

  private:
    static constexpr std::size_t ExpectedCommitPlanStepCount = 7;

    static void copyMutationIdentity(ConversationResumeCommitPreflightResult& out,
                                     const ConversationResumeMutationGuardResult& mutation) {
      out.conversationId = mutation.conversationId;
      out.sessionUuid = mutation.sessionUuid;
      out.characterKey = mutation.characterKey;
      out.endpointText = mutation.endpointText;
      out.actionId = mutation.actionId;
      out.ackKey = mutation.ackKey;
      out.packetSequence = mutation.packetSequence;
      out.localSequence = mutation.localSequence;
      out.serverTick = mutation.serverTick;
      out.sentAtMs = mutation.sentAtMs;
      out.ackDeadlineMs = mutation.ackDeadlineMs;
      out.encodedBytes = mutation.encodedBytes;
    }

    [[nodiscard]] static std::vector<ConversationResumeCommitPlanStep> buildPlanSteps(std::string_view ackKey) {
      std::vector<ConversationResumeCommitPlanStep> steps;
      steps.reserve(ExpectedCommitPlanStepCount);
      steps.push_back({ConversationResumeCommitPlanStepKind::RecordOutboundDeliveryPending,
                       true,
                       true,
                       false,
                       false,
                       std::string(ackKey)});
      steps.push_back({ConversationResumeCommitPlanStepKind::RecordConversationObserverPending,
                       true,
                       true,
                       false,
                       false,
                       std::string(ackKey)});
      steps.push_back({ConversationResumeCommitPlanStepKind::DispatchUdpDatagram,
                       false,
                       true,
                       true,
                       false,
                       std::string(ackKey)});
      steps.push_back({ConversationResumeCommitPlanStepKind::AwaitClientTransportAck,
                       false,
                       true,
                       false,
                       false,
                       std::string(ackKey)});
      steps.push_back({ConversationResumeCommitPlanStepKind::OnSendFailureTerminalizeOutboundDelivery,
                       true,
                       true,
                       false,
                       true,
                       std::string(ackKey)});
      steps.push_back({ConversationResumeCommitPlanStepKind::OnSendFailureTerminalizeConversationObserver,
                       true,
                       true,
                       false,
                       true,
                       std::string(ackKey)});
      steps.push_back({ConversationResumeCommitPlanStepKind::OnSendFailureDeadLetter,
                       true,
                       true,
                       false,
                       true,
                       std::string(ackKey)});
      return steps;
    }

    std::unordered_set<std::string> actionIds;
    std::unordered_set<std::string> ackKeys;
    ConversationResumeCommitPreflightStats counters;
};

} // namespace Mmo::Server
