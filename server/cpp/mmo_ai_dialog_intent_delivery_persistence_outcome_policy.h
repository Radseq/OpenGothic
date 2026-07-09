#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mmo_ai_dialog_intent_delivery_persistence_dry_run_report_gate.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryPersistenceOutcomePolicyStatus : std::uint8_t {
  ReadyNoExecute = 0,
  PolicyDisabled,
  MissingDryRunReport,
  DryRunReportNotReady,
  DryRunRequestedMysql,
  DryRunWouldCallRunMysql,
  DryRunAlreadyMutatedDb,
  UnsafeMysqlExecutionAllowed,
  UnsafeReplayUdpSendAllowed,
  UnsafeMarkAppliedAllowed,
  MissingTransactionReadiness,
  MissingOutputParserPolicy,
  MissingRollbackPolicy,
  MissingDeadLetterPolicy,
  MissingDurableTerminalPolicy,
  TooManyPolicySteps,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceOutcomePolicyStatusName(
    AiDialogIntentDeliveryPersistenceOutcomePolicyStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::ReadyNoExecute:
      return "ready_no_execute";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::PolicyDisabled:
      return "policy_disabled";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingDryRunReport:
      return "missing_dry_run_report";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::DryRunReportNotReady:
      return "dry_run_report_not_ready";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::DryRunRequestedMysql:
      return "dry_run_requested_mysql";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::DryRunWouldCallRunMysql:
      return "dry_run_would_call_runmysql";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::DryRunAlreadyMutatedDb:
      return "dry_run_already_mutated_db";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::UnsafeMysqlExecutionAllowed:
      return "unsafe_mysql_execution_allowed";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::UnsafeReplayUdpSendAllowed:
      return "unsafe_replay_udp_send_allowed";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::UnsafeMarkAppliedAllowed:
      return "unsafe_mark_applied_allowed";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingTransactionReadiness:
      return "missing_transaction_readiness";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingOutputParserPolicy:
      return "missing_output_parser_policy";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingRollbackPolicy:
      return "missing_rollback_policy";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingDeadLetterPolicy:
      return "missing_dead_letter_policy";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingDurableTerminalPolicy:
      return "missing_durable_terminal_policy";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::TooManyPolicySteps:
      return "too_many_policy_steps";
  }
  return "unknown";
}

enum class AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind : std::uint8_t {
  PersistDeliverySent = 0,
  AwaitClientAckOrNack,
  PersistClientTerminalReceipt,
  PersistClientObservationReceipt,
  PersistTimeoutOrDeadLetter,
  KeepActionApplyBlocked,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceOutcomePolicyStepKindName(
    AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind kind) noexcept {
  switch(kind) {
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistDeliverySent:
      return "persist_delivery_sent";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::AwaitClientAckOrNack:
      return "await_client_ack_or_nack";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistClientTerminalReceipt:
      return "persist_client_terminal_receipt";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistClientObservationReceipt:
      return "persist_client_observation_receipt";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistTimeoutOrDeadLetter:
      return "persist_timeout_or_dead_letter";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::KeepActionApplyBlocked:
      return "keep_action_apply_blocked";
  }
  return "unknown";
}

enum class AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus : std::uint8_t {
  PlannedNoExecute = 0,
  RequiresStep273Storage,
  RequiresClientReceipt,
  RequiresDeadLetterStorage,
  BlockedUntilDurableTerminal,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceOutcomePolicyStepStatusName(
    AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::PlannedNoExecute:
      return "planned_no_execute";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::RequiresStep273Storage:
      return "requires_step273_storage";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::RequiresClientReceipt:
      return "requires_client_receipt";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::RequiresDeadLetterStorage:
      return "requires_dead_letter_storage";
    case AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::BlockedUntilDurableTerminal:
      return "blocked_until_durable_terminal";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryPersistenceOutcomePolicyStep final {
  AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind kind =
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistDeliverySent;
  AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus status =
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::PlannedNoExecute;
  std::string name;
  bool requiresStep273Schema = true;
  bool requiresDurableStorage = true;
  bool requiresClientReceipt = false;
  bool requiresDeadLetterStorage = false;
  bool permitsMysqlExecution = false;
  bool permitsReplayUdpSend = false;
  bool permitsMarkApplied = false;
};

struct AiDialogIntentDeliveryPersistenceOutcomePolicyRequest final {
  bool enabled = false;
  bool allowMysqlExecution = false;
  bool allowReplayUdpSend = false;
  bool allowMarkApplied = false;
  const AiDialogIntentDeliveryPersistenceDryRunReportResult* dryRun = nullptr;
  std::size_t maxPolicySteps = 8;
};

struct AiDialogIntentDeliveryPersistenceOutcomePolicyResult final {
  AiDialogIntentDeliveryPersistenceOutcomePolicyStatus status =
      AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::PolicyDisabled;
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
  bool deliverySentPolicyReady = false;
  bool terminalReceiptPolicyReady = false;
  bool observationReceiptPolicyReady = false;
  bool timeoutDeadLetterPolicyReady = false;
  bool actionApplyBlockedUntilDurableTerminal = true;
  bool durableTerminalStateRequired = true;
  bool requiresStep273Schema = true;
  bool requiresClientAckOrObservation = true;
  std::string reason;
  std::string actionId;
  std::string ackKey;
  std::string conversationKey;
  std::string sessionUuid;
  std::string characterKey;
  std::size_t statementCount = 0;
  std::size_t procedureCallCount = 0;
  std::size_t dryRunStepCount = 0;
  std::size_t policyStepCount = 0;
  std::vector<AiDialogIntentDeliveryPersistenceOutcomePolicyStep> steps;
  std::vector<AiDialogIntentDeliveryPersistenceFailureClass> rollbackFailureClasses;
  std::vector<AiDialogIntentDeliveryPersistenceFailureClass> deadLetterFailureClasses;
};

namespace PersistenceOutcomePolicyDetail {

[[nodiscard]] inline bool hasFailureClass(
    const std::vector<AiDialogIntentDeliveryPersistenceFailureClass>& classes,
    AiDialogIntentDeliveryPersistenceFailureClass expected) noexcept {
  for(const auto failureClass : classes) {
    if(failureClass == expected)
      return true;
  }
  return false;
}

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceOutcomePolicyStep makeStep(
    AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind kind,
    AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus status,
    std::string_view name) {
  AiDialogIntentDeliveryPersistenceOutcomePolicyStep step;
  step.kind = kind;
  step.status = status;
  step.name = std::string(name);
  step.requiresClientReceipt =
      kind == AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::AwaitClientAckOrNack ||
      kind == AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistClientTerminalReceipt ||
      kind == AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistClientObservationReceipt;
  step.requiresDeadLetterStorage = kind == AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistTimeoutOrDeadLetter;
  step.permitsMysqlExecution = false;
  step.permitsReplayUdpSend = false;
  step.permitsMarkApplied = false;
  return step;
}

[[nodiscard]] inline std::string joinFailureClassNames(
    const std::vector<AiDialogIntentDeliveryPersistenceFailureClass>& classes) {
  std::string out;
  for(const auto failureClass : classes) {
    if(!out.empty())
      out.push_back(',');
    out += aiDialogIntentDeliveryPersistenceFailureClassName(failureClass);
  }
  return out;
}

} // namespace PersistenceOutcomePolicyDetail

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceOutcomePolicyStepKindsCsv(
    const AiDialogIntentDeliveryPersistenceOutcomePolicyResult& result) {
  std::string out;
  for(const auto& step : result.steps) {
    if(!out.empty())
      out.push_back(',');
    out += aiDialogIntentDeliveryPersistenceOutcomePolicyStepKindName(step.kind);
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceOutcomePolicyStepStatusesCsv(
    const AiDialogIntentDeliveryPersistenceOutcomePolicyResult& result) {
  std::string out;
  for(const auto& step : result.steps) {
    if(!out.empty())
      out.push_back(',');
    out += aiDialogIntentDeliveryPersistenceOutcomePolicyStepStatusName(step.status);
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceOutcomePolicyStepNamesCsv(
    const AiDialogIntentDeliveryPersistenceOutcomePolicyResult& result) {
  std::string out;
  for(const auto& step : result.steps) {
    if(step.name.empty())
      continue;
    if(!out.empty())
      out.push_back(',');
    out += step.name;
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceOutcomePolicyRollbackFailureClassesCsv(
    const AiDialogIntentDeliveryPersistenceOutcomePolicyResult& result) {
  return PersistenceOutcomePolicyDetail::joinFailureClassNames(result.rollbackFailureClasses);
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceOutcomePolicyDeadLetterFailureClassesCsv(
    const AiDialogIntentDeliveryPersistenceOutcomePolicyResult& result) {
  return PersistenceOutcomePolicyDetail::joinFailureClassNames(result.deadLetterFailureClasses);
}

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceOutcomePolicyResult
buildAiDialogIntentDeliveryPersistenceOutcomePolicy(
    const AiDialogIntentDeliveryPersistenceOutcomePolicyRequest& request) {
  AiDialogIntentDeliveryPersistenceOutcomePolicyResult out;
  out.executeMysql = false;
  out.wouldCallRunMysql = false;
  out.mutatedDb = false;
  out.replayUdpSendEnabled = false;
  out.replayUdpSendDisabled = true;
  out.markAppliedEnabled = false;
  out.markAppliedDisabled = true;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::PolicyDisabled;
    out.reason = "step278_persistence_outcome_policy_disabled";
    return out;
  }
  if(request.allowMysqlExecution) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::UnsafeMysqlExecutionAllowed;
    out.reason = "step278_outcome_policy_forbids_mysql_execution";
    return out;
  }
  if(request.allowReplayUdpSend) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::UnsafeReplayUdpSendAllowed;
    out.reason = "step278_outcome_policy_forbids_replay_udp_send";
    return out;
  }
  if(request.allowMarkApplied) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::UnsafeMarkAppliedAllowed;
    out.reason = "step278_outcome_policy_forbids_mark_applied";
    return out;
  }
  if(request.dryRun == nullptr) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingDryRunReport;
    out.reason = "step278_outcome_policy_requires_step277_dry_run_report";
    return out;
  }

  const auto& dryRun = *request.dryRun;
  out.actionId = dryRun.actionId;
  out.ackKey = dryRun.ackKey;
  out.conversationKey = dryRun.conversationKey;
  out.sessionUuid = dryRun.sessionUuid;
  out.characterKey = dryRun.characterKey;
  out.statementCount = dryRun.statementCount;
  out.procedureCallCount = dryRun.procedureCallCount;
  out.dryRunStepCount = dryRun.dryRunStepCount;
  out.wouldOpenMysqlConnection = dryRun.wouldOpenMysqlConnection;
  out.wouldMutateDb = dryRun.wouldMutateDb;
  out.rollbackFailureClasses = dryRun.rollbackFailureClasses;
  out.deadLetterFailureClasses = dryRun.deadLetterFailureClasses;

  if(!dryRun.ready) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::DryRunReportNotReady;
    out.reason = dryRun.reason.empty() ? "step277_dry_run_report_not_ready" : dryRun.reason;
    return out;
  }
  if(dryRun.executeMysql) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::DryRunRequestedMysql;
    out.reason = "step278_outcome_policy_requires_execute_mysql_off";
    return out;
  }
  if(dryRun.wouldCallRunMysql) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::DryRunWouldCallRunMysql;
    out.reason = "step278_outcome_policy_requires_runmysql_off";
    return out;
  }
  if(dryRun.mutatedDb) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::DryRunAlreadyMutatedDb;
    out.reason = "step278_outcome_policy_requires_unmutated_dry_run";
    return out;
  }
  if(!dryRun.transactionDryRunReady) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingTransactionReadiness;
    out.reason = "step278_outcome_policy_requires_transaction_dry_run_ready";
    return out;
  }
  if(!dryRun.outputParserDryRunReady || dryRun.outputParserStepCount == 0) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingOutputParserPolicy;
    out.reason = "step278_outcome_policy_requires_output_parser_dry_run_ready";
    return out;
  }
  if(!dryRun.rollbackReportReady ||
     !PersistenceOutcomePolicyDetail::hasFailureClass(dryRun.rollbackFailureClasses,
                                                      AiDialogIntentDeliveryPersistenceFailureClass::TransactionRollbackRequired) ||
     !PersistenceOutcomePolicyDetail::hasFailureClass(dryRun.rollbackFailureClasses,
                                                      AiDialogIntentDeliveryPersistenceFailureClass::ExecutionRejected)) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingRollbackPolicy;
    out.reason = "step278_outcome_policy_requires_rollback_policy";
    return out;
  }
  if(!dryRun.deadLetterReportReady || dryRun.deadLetterFailureClasses.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingDeadLetterPolicy;
    out.reason = "step278_outcome_policy_requires_dead_letter_policy";
    return out;
  }

  out.steps.reserve(6);
  out.steps.push_back(PersistenceOutcomePolicyDetail::makeStep(
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistDeliverySent,
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::RequiresStep273Storage,
      "record_gameplay_delivery_sent"));
  out.steps.push_back(PersistenceOutcomePolicyDetail::makeStep(
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::AwaitClientAckOrNack,
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::RequiresClientReceipt,
      "await_ack_or_nack"));
  out.steps.push_back(PersistenceOutcomePolicyDetail::makeStep(
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistClientTerminalReceipt,
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::RequiresClientReceipt,
      "record_terminal_receipt"));
  out.steps.push_back(PersistenceOutcomePolicyDetail::makeStep(
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistClientObservationReceipt,
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::RequiresClientReceipt,
      "record_observation_receipt"));
  out.steps.push_back(PersistenceOutcomePolicyDetail::makeStep(
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::PersistTimeoutOrDeadLetter,
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::RequiresDeadLetterStorage,
      "record_timeout_or_dead_letter"));
  out.steps.push_back(PersistenceOutcomePolicyDetail::makeStep(
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepKind::KeepActionApplyBlocked,
      AiDialogIntentDeliveryPersistenceOutcomePolicyStepStatus::BlockedUntilDurableTerminal,
      "keep_mark_applied_blocked"));

  if(out.steps.size() > request.maxPolicySteps) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::TooManyPolicySteps;
    out.reason = "step278_outcome_policy_step_limit_exceeded";
    return out;
  }

  out.policyStepCount = out.steps.size();
  out.deliverySentPolicyReady = true;
  out.terminalReceiptPolicyReady = true;
  out.observationReceiptPolicyReady = true;
  out.timeoutDeadLetterPolicyReady = true;
  out.actionApplyBlockedUntilDurableTerminal = true;
  if(!out.deliverySentPolicyReady || !out.terminalReceiptPolicyReady ||
     !out.observationReceiptPolicyReady || !out.timeoutDeadLetterPolicyReady ||
     !out.actionApplyBlockedUntilDurableTerminal) {
    out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::MissingDurableTerminalPolicy;
    out.reason = "step278_outcome_policy_requires_durable_terminal_policy";
    return out;
  }

  out.status = AiDialogIntentDeliveryPersistenceOutcomePolicyStatus::ReadyNoExecute;
  out.ready = true;
  out.reason = "step278_persistence_outcome_policy_ready_no_execute";
  return out;
}

} // namespace Mmo::Server

