#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mmo_ai_dialog_intent_delivery_persistence_outcome_policy.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus : std::uint8_t {
  ReadyNoExecute = 0,
  ObservabilityDisabled,
  MissingOutcomePolicy,
  OutcomePolicyNotReady,
  OutcomeRequestedMysql,
  OutcomeWouldCallRunMysql,
  OutcomeAlreadyMutatedDb,
  ReplayUdpSendEnabled,
  MarkAppliedEnabled,
  MissingPolicySteps,
  MissingDeliverySentStep,
  MissingClientAckWaitStep,
  MissingTerminalReceiptStep,
  MissingObservationReceiptStep,
  MissingTimeoutDeadLetterStep,
  MissingActionApplyBlockStep,
  UnsafeStepPermitsMysql,
  UnsafeStepPermitsReplayUdpSend,
  UnsafeStepPermitsMarkApplied,
  MissingDurableTerminalReadiness,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceOutcomeObservabilityStatusName(
    AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::ReadyNoExecute:
      return "ready_no_execute";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::ObservabilityDisabled:
      return "observability_disabled";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingOutcomePolicy:
      return "missing_outcome_policy";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::OutcomePolicyNotReady:
      return "outcome_policy_not_ready";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::OutcomeRequestedMysql:
      return "outcome_requested_mysql";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::OutcomeWouldCallRunMysql:
      return "outcome_would_call_runmysql";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::OutcomeAlreadyMutatedDb:
      return "outcome_already_mutated_db";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::ReplayUdpSendEnabled:
      return "replay_udp_send_enabled";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MarkAppliedEnabled:
      return "mark_applied_enabled";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingPolicySteps:
      return "missing_policy_steps";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingDeliverySentStep:
      return "missing_delivery_sent_step";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingClientAckWaitStep:
      return "missing_client_ack_wait_step";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingTerminalReceiptStep:
      return "missing_terminal_receipt_step";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingObservationReceiptStep:
      return "missing_observation_receipt_step";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingTimeoutDeadLetterStep:
      return "missing_timeout_dead_letter_step";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingActionApplyBlockStep:
      return "missing_action_apply_block_step";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::UnsafeStepPermitsMysql:
      return "unsafe_step_permits_mysql";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::UnsafeStepPermitsReplayUdpSend:
      return "unsafe_step_permits_replay_udp_send";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::UnsafeStepPermitsMarkApplied:
      return "unsafe_step_permits_mark_applied";
    case AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingDurableTerminalReadiness:
      return "missing_durable_terminal_readiness";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryPersistenceOutcomeObservabilityRequest final {
  bool enabled = false;
  const AiDialogIntentDeliveryPersistenceOutcomePolicyResult* outcome = nullptr;
  std::size_t minimumPolicySteps = 6;
};

struct AiDialogIntentDeliveryPersistenceOutcomeObservabilityResult final {
  AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus status =
      AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::ObservabilityDisabled;
  bool ready = false;
  bool executeMysql = false;
  bool wouldOpenMysqlConnection = false;
  bool wouldCallRunMysql = false;
  bool wouldMutateDb = false;
  bool mutatedDb = false;
  bool replayUdpSendEnabled = false;
  bool replayUdpSendDisabled = true;
  bool markAppliedEnabled = false;
  bool markAppliedDisabled = true;
  bool allNoExecuteInvariantsHold = false;
  bool deliverySentObserved = false;
  bool clientAckWaitObserved = false;
  bool terminalReceiptObserved = false;
  bool observationReceiptObserved = false;
  bool timeoutDeadLetterObserved = false;
  bool actionApplyBlockObserved = false;
  bool durableTerminalPolicyObserved = false;
  bool requiresStep273Schema = true;
  bool requiresClientAckOrObservation = true;
  std::size_t policyStepCount = 0;
  std::size_t durableStorageStepCount = 0;
  std::size_t clientReceiptStepCount = 0;
  std::size_t deadLetterStepCount = 0;
  std::size_t unsafeMysqlStepCount = 0;
  std::size_t unsafeReplayUdpStepCount = 0;
  std::size_t unsafeMarkAppliedStepCount = 0;
  std::string reason;
  std::string actionId;
  std::string ackKey;
  std::string conversationKey;
  std::string sessionUuid;
  std::string characterKey;
};

namespace PersistenceOutcomeObservabilityDetail {

[[nodiscard]] constexpr bool isKind(
    AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind actual,
    AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind expected) noexcept {
  return actual == expected;
}

inline void observeStep(AiDialogIntentDeliveryPersistenceOutcomeObservabilityResult& out,
                        const AiDialogIntentDeliveryPersistenceOutcomePolicyStep& step) noexcept {
  out.deliverySentObserved = out.deliverySentObserved ||
      isKind(step.kind, AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistDeliverySent);
  out.clientAckWaitObserved = out.clientAckWaitObserved ||
      isKind(step.kind, AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::AwaitClientAckOrNack);
  out.terminalReceiptObserved = out.terminalReceiptObserved ||
      isKind(step.kind, AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistClientTerminalReceipt);
  out.observationReceiptObserved = out.observationReceiptObserved ||
      isKind(step.kind, AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistClientObservationReceipt);
  out.timeoutDeadLetterObserved = out.timeoutDeadLetterObserved ||
      isKind(step.kind, AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistTimeoutOrDeadLetter);
  out.actionApplyBlockObserved = out.actionApplyBlockObserved ||
      isKind(step.kind, AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::KeepActionApplyBlocked);

  out.durableStorageStepCount += step.requiresDurableStorage ? 1U : 0U;
  out.clientReceiptStepCount += step.requiresClientReceipt ? 1U : 0U;
  out.deadLetterStepCount += step.requiresDeadLetterStorage ? 1U : 0U;
  out.unsafeMysqlStepCount += step.permitsMysqlExecution ? 1U : 0U;
  out.unsafeReplayUdpStepCount += step.permitsReplayUdpSend ? 1U : 0U;
  out.unsafeMarkAppliedStepCount += step.permitsMarkApplied ? 1U : 0U;
}

} // namespace PersistenceOutcomeObservabilityDetail

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceOutcomeObservabilityResult
buildAiDialogIntentDeliveryPersistenceOutcomeObservability(
    const AiDialogIntentDeliveryPersistenceOutcomeObservabilityRequest& request) {
  AiDialogIntentDeliveryPersistenceOutcomeObservabilityResult out;
  out.executeMysql = false;
  out.wouldCallRunMysql = false;
  out.mutatedDb = false;
  out.replayUdpSendEnabled = false;
  out.replayUdpSendDisabled = true;
  out.markAppliedEnabled = false;
  out.markAppliedDisabled = true;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::ObservabilityDisabled;
    out.reason = "step279_outcome_observability_disabled";
    return out;
  }
  if(request.outcome == nullptr) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingOutcomePolicy;
    out.reason = "step279_outcome_observability_requires_step278_policy";
    return out;
  }

  const auto& outcome = *request.outcome;
  out.actionId = outcome.actionId;
  out.ackKey = outcome.ackKey;
  out.conversationKey = outcome.conversationKey;
  out.sessionUuid = outcome.sessionUuid;
  out.characterKey = outcome.characterKey;
  out.executeMysql = outcome.executeMysql;
  out.wouldOpenMysqlConnection = outcome.wouldOpenMysqlConnection;
  out.wouldCallRunMysql = outcome.wouldCallRunMysql;
  out.wouldMutateDb = outcome.wouldMutateDb;
  out.mutatedDb = outcome.mutatedDb;
  out.replayUdpSendEnabled = outcome.replayUdpSendEnabled;
  out.replayUdpSendDisabled = outcome.replayUdpSendDisabled;
  out.markAppliedEnabled = outcome.markAppliedEnabled;
  out.markAppliedDisabled = outcome.markAppliedDisabled;
  out.requiresStep273Schema = outcome.requiresStep273Schema;
  out.requiresClientAckOrObservation = outcome.requiresClientAckOrObservation;
  out.policyStepCount = outcome.policyStepCount;

  for(const auto& step : outcome.steps)
    PersistenceOutcomeObservabilityDetail::observeStep(out, step);

  out.durableTerminalPolicyObserved = outcome.deliverySentPolicyReady &&
      outcome.terminalReceiptPolicyReady &&
      outcome.observationReceiptPolicyReady &&
      outcome.timeoutDeadLetterPolicyReady &&
      outcome.actionApplyBlockedUntilDurableTerminal &&
      outcome.durableTerminalStateRequired;

  if(!outcome.ready) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::OutcomePolicyNotReady;
    out.reason = outcome.reason.empty() ? "step278_outcome_policy_not_ready" : outcome.reason;
    return out;
  }
  if(outcome.executeMysql) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::OutcomeRequestedMysql;
    out.reason = "step279_outcome_observability_requires_execute_mysql_off";
    return out;
  }
  if(outcome.wouldCallRunMysql) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::OutcomeWouldCallRunMysql;
    out.reason = "step279_outcome_observability_requires_runmysql_off";
    return out;
  }
  if(outcome.mutatedDb) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::OutcomeAlreadyMutatedDb;
    out.reason = "step279_outcome_observability_requires_unmutated_outcome_policy";
    return out;
  }
  if(outcome.replayUdpSendEnabled || !outcome.replayUdpSendDisabled) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::ReplayUdpSendEnabled;
    out.reason = "step279_outcome_observability_requires_replay_udp_send_off";
    return out;
  }
  if(outcome.markAppliedEnabled || !outcome.markAppliedDisabled) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MarkAppliedEnabled;
    out.reason = "step279_outcome_observability_requires_mark_applied_off";
    return out;
  }
  if(outcome.steps.size() < request.minimumPolicySteps || out.policyStepCount < request.minimumPolicySteps) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingPolicySteps;
    out.reason = "step279_outcome_observability_requires_full_policy_step_set";
    return out;
  }
  if(!out.deliverySentObserved) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingDeliverySentStep;
    out.reason = "step279_outcome_observability_requires_delivery_sent_step";
    return out;
  }
  if(!out.clientAckWaitObserved) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingClientAckWaitStep;
    out.reason = "step279_outcome_observability_requires_client_ack_wait_step";
    return out;
  }
  if(!out.terminalReceiptObserved) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingTerminalReceiptStep;
    out.reason = "step279_outcome_observability_requires_terminal_receipt_step";
    return out;
  }
  if(!out.observationReceiptObserved) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingObservationReceiptStep;
    out.reason = "step279_outcome_observability_requires_observation_receipt_step";
    return out;
  }
  if(!out.timeoutDeadLetterObserved) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingTimeoutDeadLetterStep;
    out.reason = "step279_outcome_observability_requires_timeout_dead_letter_step";
    return out;
  }
  if(!out.actionApplyBlockObserved) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingActionApplyBlockStep;
    out.reason = "step279_outcome_observability_requires_action_apply_block_step";
    return out;
  }
  if(out.unsafeMysqlStepCount != 0) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::UnsafeStepPermitsMysql;
    out.reason = "step279_outcome_observability_forbids_mysql_step_permission";
    return out;
  }
  if(out.unsafeReplayUdpStepCount != 0) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::UnsafeStepPermitsReplayUdpSend;
    out.reason = "step279_outcome_observability_forbids_replay_udp_step_permission";
    return out;
  }
  if(out.unsafeMarkAppliedStepCount != 0) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::UnsafeStepPermitsMarkApplied;
    out.reason = "step279_outcome_observability_forbids_mark_applied_step_permission";
    return out;
  }
  if(!out.durableTerminalPolicyObserved) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::MissingDurableTerminalReadiness;
    out.reason = "step279_outcome_observability_requires_durable_terminal_policy";
    return out;
  }

  out.status = AiDialogIntentDeliveryPersistenceOutcomeObservabilityStatus::ReadyNoExecute;
  out.ready = true;
  out.allNoExecuteInvariantsHold = true;
  out.reason = "step279_outcome_observability_ready_no_execute";
  return out;
}

} // namespace Mmo::Server

