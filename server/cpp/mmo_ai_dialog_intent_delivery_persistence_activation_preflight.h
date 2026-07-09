#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mmo_ai_dialog_intent_delivery_persistence_outcome_observability.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryPersistenceActivationPreflightStatus : std::uint8_t {
  ReadyNoExecute = 0,
  PreflightDisabled,
  MissingOutcomeObservability,
  OutcomeObservabilityNotReady,
  UnsafeRuntimeActivationRequested,
  NoExecuteInvariantMissing,
  MysqlExecutionEnabled,
  MysqlConnectionWouldOpen,
  RunMysqlWouldBeCalled,
  DbMutationWouldHappen,
  DbAlreadyMutated,
  ReplayUdpSendEnabled,
  MarkAppliedEnabled,
  Step273SchemaRequirementMissing,
  ClientReceiptRequirementMissing,
  DurableTerminalPolicyMissing,
  PolicyStepCoverageMissing,
  DurableStorageCoverageMissing,
  ClientReceiptCoverageMissing,
  DeadLetterCoverageMissing,
  IdentityMissing,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceActivationPreflightStatusName(
    AiDialogIntentDeliveryPersistenceActivationPreflightStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::ReadyNoExecute:
      return "ready_no_execute";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::PreflightDisabled:
      return "preflight_disabled";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::MissingOutcomeObservability:
      return "missing_outcome_observability";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::OutcomeObservabilityNotReady:
      return "outcome_observability_not_ready";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::UnsafeRuntimeActivationRequested:
      return "unsafe_runtime_activation_requested";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::NoExecuteInvariantMissing:
      return "no_execute_invariant_missing";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::MysqlExecutionEnabled:
      return "mysql_execution_enabled";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::MysqlConnectionWouldOpen:
      return "mysql_connection_would_open";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::RunMysqlWouldBeCalled:
      return "runmysql_would_be_called";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DbMutationWouldHappen:
      return "db_mutation_would_happen";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DbAlreadyMutated:
      return "db_already_mutated";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::ReplayUdpSendEnabled:
      return "replay_udp_send_enabled";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::MarkAppliedEnabled:
      return "mark_applied_enabled";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::Step273SchemaRequirementMissing:
      return "step273_schema_requirement_missing";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::ClientReceiptRequirementMissing:
      return "client_receipt_requirement_missing";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DurableTerminalPolicyMissing:
      return "durable_terminal_policy_missing";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::PolicyStepCoverageMissing:
      return "policy_step_coverage_missing";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DurableStorageCoverageMissing:
      return "durable_storage_coverage_missing";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::ClientReceiptCoverageMissing:
      return "client_receipt_coverage_missing";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DeadLetterCoverageMissing:
      return "dead_letter_coverage_missing";
    case AiDialogIntentDeliveryPersistenceActivationPreflightStatus::IdentityMissing:
      return "identity_missing";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryPersistenceActivationPreflightRequest final {
  bool enabled = false;
  bool allowRuntimeActivation = false;
  bool requireRuntimeIdentity = true;
  const AiDialogIntentDeliveryPersistenceOutcomeObservabilityResult* observability = nullptr;
  std::size_t minimumPolicySteps = 6;
  std::size_t minimumDurableStorageSteps = 4;
  std::size_t minimumClientReceiptSteps = 3;
  std::size_t minimumDeadLetterSteps = 1;
};

struct AiDialogIntentDeliveryPersistenceActivationPreflightResult final {
  AiDialogIntentDeliveryPersistenceActivationPreflightStatus status =
      AiDialogIntentDeliveryPersistenceActivationPreflightStatus::PreflightDisabled;
  bool ready = false;
  bool activationAllowed = false;
  bool activationBlockedUntilFinalBatch = true;
  bool futureDbBatchRequired = true;
  bool futureToolsBatchRequired = false;
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
  bool requiresStep273Schema = true;
  bool requiresClientAckOrObservation = true;
  bool durableTerminalPolicyObserved = false;
  bool runtimeIdentityPresent = false;
  bool policyCoverageReady = false;
  bool durableStorageCoverageReady = false;
  bool clientReceiptCoverageReady = false;
  bool deadLetterCoverageReady = false;
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

namespace PersistenceActivationPreflightDetail {

[[nodiscard]] inline bool hasRuntimeIdentity(
    const AiDialogIntentDeliveryPersistenceOutcomeObservabilityResult& observability) noexcept {
  return !observability.actionId.empty() && !observability.ackKey.empty() &&
      !observability.sessionUuid.empty() && !observability.characterKey.empty();
}

inline void copyNoExecuteEnvelope(
    AiDialogIntentDeliveryPersistenceActivationPreflightResult& out,
    const AiDialogIntentDeliveryPersistenceOutcomeObservabilityResult& observability) {
  out.actionId = observability.actionId;
  out.ackKey = observability.ackKey;
  out.conversationKey = observability.conversationKey;
  out.sessionUuid = observability.sessionUuid;
  out.characterKey = observability.characterKey;
  out.executeMysql = observability.executeMysql;
  out.wouldOpenMysqlConnection = observability.wouldOpenMysqlConnection;
  out.wouldCallRunMysql = observability.wouldCallRunMysql;
  out.wouldMutateDb = observability.wouldMutateDb;
  out.mutatedDb = observability.mutatedDb;
  out.replayUdpSendEnabled = observability.replayUdpSendEnabled;
  out.replayUdpSendDisabled = observability.replayUdpSendDisabled;
  out.markAppliedEnabled = observability.markAppliedEnabled;
  out.markAppliedDisabled = observability.markAppliedDisabled;
  out.allNoExecuteInvariantsHold = observability.allNoExecuteInvariantsHold;
  out.requiresStep273Schema = observability.requiresStep273Schema;
  out.requiresClientAckOrObservation = observability.requiresClientAckOrObservation;
  out.durableTerminalPolicyObserved = observability.durableTerminalPolicyObserved;
  out.policyStepCount = observability.policyStepCount;
  out.durableStorageStepCount = observability.durableStorageStepCount;
  out.clientReceiptStepCount = observability.clientReceiptStepCount;
  out.deadLetterStepCount = observability.deadLetterStepCount;
  out.unsafeMysqlStepCount = observability.unsafeMysqlStepCount;
  out.unsafeReplayUdpStepCount = observability.unsafeReplayUdpStepCount;
  out.unsafeMarkAppliedStepCount = observability.unsafeMarkAppliedStepCount;
  out.runtimeIdentityPresent = hasRuntimeIdentity(observability);
}

} // namespace PersistenceActivationPreflightDetail

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceActivationPreflightResult
buildAiDialogIntentDeliveryPersistenceActivationPreflight(
    const AiDialogIntentDeliveryPersistenceActivationPreflightRequest& request) {
  AiDialogIntentDeliveryPersistenceActivationPreflightResult out;
  out.executeMysql = false;
  out.wouldOpenMysqlConnection = false;
  out.wouldCallRunMysql = false;
  out.wouldMutateDb = false;
  out.mutatedDb = false;
  out.replayUdpSendEnabled = false;
  out.replayUdpSendDisabled = true;
  out.markAppliedEnabled = false;
  out.markAppliedDisabled = true;
  out.activationAllowed = false;
  out.activationBlockedUntilFinalBatch = true;
  out.futureDbBatchRequired = true;
  out.futureToolsBatchRequired = false;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::PreflightDisabled;
    out.reason = "step280_activation_preflight_disabled";
    return out;
  }
  if(request.allowRuntimeActivation) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::UnsafeRuntimeActivationRequested;
    out.reason = "step280_activation_preflight_forbids_runtime_activation";
    return out;
  }
  if(request.observability == nullptr) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::MissingOutcomeObservability;
    out.reason = "step280_activation_preflight_requires_step279_observability";
    return out;
  }

  const auto& observability = *request.observability;
  PersistenceActivationPreflightDetail::copyNoExecuteEnvelope(out, observability);
  out.policyCoverageReady = out.policyStepCount >= request.minimumPolicySteps;
  out.durableStorageCoverageReady = out.durableStorageStepCount >= request.minimumDurableStorageSteps;
  out.clientReceiptCoverageReady = out.clientReceiptStepCount >= request.minimumClientReceiptSteps;
  out.deadLetterCoverageReady = out.deadLetterStepCount >= request.minimumDeadLetterSteps;

  if(!observability.ready) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::OutcomeObservabilityNotReady;
    out.reason = observability.reason.empty() ? "step279_observability_not_ready" : observability.reason;
    return out;
  }
  if(!observability.allNoExecuteInvariantsHold) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::NoExecuteInvariantMissing;
    out.reason = "step280_activation_preflight_requires_no_execute_invariants";
    return out;
  }
  if(observability.executeMysql) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::MysqlExecutionEnabled;
    out.reason = "step280_activation_preflight_requires_execute_mysql_off";
    return out;
  }
  if(observability.wouldCallRunMysql) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::RunMysqlWouldBeCalled;
    out.reason = "step280_activation_preflight_requires_runmysql_off";
    return out;
  }
  if(observability.mutatedDb) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DbAlreadyMutated;
    out.reason = "step280_activation_preflight_requires_db_unmutated";
    return out;
  }
  if(observability.replayUdpSendEnabled || !observability.replayUdpSendDisabled) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::ReplayUdpSendEnabled;
    out.reason = "step280_activation_preflight_requires_replay_udp_send_off";
    return out;
  }
  if(observability.markAppliedEnabled || !observability.markAppliedDisabled) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::MarkAppliedEnabled;
    out.reason = "step280_activation_preflight_requires_mark_applied_off";
    return out;
  }
  if(!observability.requiresStep273Schema) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::Step273SchemaRequirementMissing;
    out.reason = "step280_activation_preflight_requires_step273_schema_contract";
    return out;
  }
  if(!observability.requiresClientAckOrObservation) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::ClientReceiptRequirementMissing;
    out.reason = "step280_activation_preflight_requires_client_ack_or_observation";
    return out;
  }
  if(!observability.durableTerminalPolicyObserved) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DurableTerminalPolicyMissing;
    out.reason = "step280_activation_preflight_requires_durable_terminal_policy";
    return out;
  }
  if(!out.policyCoverageReady) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::PolicyStepCoverageMissing;
    out.reason = "step280_activation_preflight_requires_policy_step_coverage";
    return out;
  }
  if(!out.durableStorageCoverageReady) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DurableStorageCoverageMissing;
    out.reason = "step280_activation_preflight_requires_durable_storage_coverage";
    return out;
  }
  if(!out.clientReceiptCoverageReady) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::ClientReceiptCoverageMissing;
    out.reason = "step280_activation_preflight_requires_client_receipt_coverage";
    return out;
  }
  if(!out.deadLetterCoverageReady) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::DeadLetterCoverageMissing;
    out.reason = "step280_activation_preflight_requires_dead_letter_coverage";
    return out;
  }
  if(request.requireRuntimeIdentity && !out.runtimeIdentityPresent) {
    out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::IdentityMissing;
    out.reason = "step280_activation_preflight_requires_runtime_identity";
    return out;
  }

  out.status = AiDialogIntentDeliveryPersistenceActivationPreflightStatus::ReadyNoExecute;
  out.ready = true;
  out.activationAllowed = false;
  out.activationBlockedUntilFinalBatch = true;
  out.futureDbBatchRequired = true;
  out.futureToolsBatchRequired = false;
  out.reason = "step280_activation_preflight_ready_no_execute";
  return out;
}

} // namespace Mmo::Server
