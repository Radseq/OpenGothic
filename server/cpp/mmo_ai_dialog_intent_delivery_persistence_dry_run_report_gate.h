#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mmo_ai_dialog_intent_delivery_persistence_executor_adapter.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryPersistenceDryRunReportStatus : std::uint8_t {
  ReadyNoExecute = 0,
  GateDisabled,
  MissingExecutorAdapter,
  ExecutorAdapterNotReady,
  AdapterRequestedMysql,
  AdapterWouldCallRunMysql,
  AdapterAlreadyMutatedDb,
  EmptyExecutorPlan,
  TooManyExecutorSteps,
  MissingBeginTransaction,
  MissingCommitTransaction,
  MissingRollbackTransaction,
  MissingOutputParser,
  MissingRollbackClassification,
  MissingDeadLetterClassification,
  UnsafeMysqlExecutionAllowed,
  UnsafeMutatingPlanWithoutTransaction,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceDryRunReportStatusName(
    AiDialogIntentDeliveryPersistenceDryRunReportStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::ReadyNoExecute:
      return "ready_no_execute";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::GateDisabled:
      return "gate_disabled";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingExecutorAdapter:
      return "missing_executor_adapter";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::ExecutorAdapterNotReady:
      return "executor_adapter_not_ready";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::AdapterRequestedMysql:
      return "adapter_requested_mysql";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::AdapterWouldCallRunMysql:
      return "adapter_would_call_runmysql";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::AdapterAlreadyMutatedDb:
      return "adapter_already_mutated_db";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::EmptyExecutorPlan:
      return "empty_executor_plan";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::TooManyExecutorSteps:
      return "too_many_executor_steps";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingBeginTransaction:
      return "missing_begin_transaction";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingCommitTransaction:
      return "missing_commit_transaction";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingRollbackTransaction:
      return "missing_rollback_transaction";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingOutputParser:
      return "missing_output_parser";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingRollbackClassification:
      return "missing_rollback_classification";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingDeadLetterClassification:
      return "missing_dead_letter_classification";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::UnsafeMysqlExecutionAllowed:
      return "unsafe_mysql_execution_allowed";
    case AiDialogIntentDeliveryPersistenceDryRunReportStatus::UnsafeMutatingPlanWithoutTransaction:
      return "unsafe_mutating_plan_without_transaction";
  }
  return "unknown";
}

enum class AiDialogIntentDeliveryPersistenceDryRunStepStatus : std::uint8_t {
  BeginValidatedNoExecute = 0,
  StatementValidatedNoExecute,
  ProcedureValidatedNoExecute,
  OutputParserValidatedNoExecute,
  CommitValidatedNoExecute,
  RollbackValidatedNoExecute,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceDryRunStepStatusName(
    AiDialogIntentDeliveryPersistenceDryRunStepStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceDryRunStepStatus::BeginValidatedNoExecute:
      return "begin_validated_no_execute";
    case AiDialogIntentDeliveryPersistenceDryRunStepStatus::StatementValidatedNoExecute:
      return "statement_validated_no_execute";
    case AiDialogIntentDeliveryPersistenceDryRunStepStatus::ProcedureValidatedNoExecute:
      return "procedure_validated_no_execute";
    case AiDialogIntentDeliveryPersistenceDryRunStepStatus::OutputParserValidatedNoExecute:
      return "output_parser_validated_no_execute";
    case AiDialogIntentDeliveryPersistenceDryRunStepStatus::CommitValidatedNoExecute:
      return "commit_validated_no_execute";
    case AiDialogIntentDeliveryPersistenceDryRunStepStatus::RollbackValidatedNoExecute:
      return "rollback_validated_no_execute";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryPersistenceDryRunStepEvidence final {
  AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind kind =
      AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::BeginTransaction;
  AiDialogIntentDeliveryPersistenceDryRunStepStatus status =
      AiDialogIntentDeliveryPersistenceDryRunStepStatus::BeginValidatedNoExecute;
  std::string statementName;
  std::string sqlPreview;
  std::size_t sqlBytes = 0;
  bool mutatesDbPreview = false;
  bool requiresMysqlConnection = true;
  bool callsStoredProcedure = false;
  bool readsOutputSlots = false;
  bool failureBranch = false;
  bool wouldExecuteMysql = false;
  bool wouldCallRunMysql = false;
  bool wouldMutateDb = false;
  bool mutatedDb = false;
};

struct AiDialogIntentDeliveryPersistenceDryRunReportRequest final {
  bool enabled = false;
  bool allowMysqlExecution = false;
  const AiDialogIntentDeliveryPersistenceExecutorAdapterResult* adapter = nullptr;
  std::size_t maxExecutorSteps = 16;
  std::size_t maxSqlPreviewBytes = 160;
};

struct AiDialogIntentDeliveryPersistenceDryRunReportResult final {
  AiDialogIntentDeliveryPersistenceDryRunReportStatus status =
      AiDialogIntentDeliveryPersistenceDryRunReportStatus::GateDisabled;
  bool ready = false;
  bool executeMysql = false;
  bool wouldOpenMysqlConnection = false;
  bool wouldCallRunMysql = false;
  bool wouldMutateDb = false;
  bool mutatedDb = false;
  bool transactionDryRunReady = false;
  bool outputParserDryRunReady = false;
  bool rollbackReportReady = false;
  bool deadLetterReportReady = false;
  bool rollbackRequiredOnFailure = true;
  bool deadLetterReportRequiresDurableStorage = true;
  bool runMysqlDisabled = true;
  bool replayUdpSendDisabled = true;
  bool markAppliedDisabled = true;
  std::string reason;
  std::string actionId;
  std::string ackKey;
  std::string conversationKey;
  std::string sessionUuid;
  std::string characterKey;
  std::size_t statementCount = 0;
  std::size_t procedureCallCount = 0;
  std::size_t executorStepCount = 0;
  std::size_t dryRunStepCount = 0;
  std::size_t mutatingPreviewStepCount = 0;
  std::size_t outputParserStepCount = 0;
  std::size_t failureBranchStepCount = 0;
  std::vector<AiDialogIntentDeliveryPersistenceDryRunStepEvidence> steps;
  std::vector<AiDialogIntentDeliveryPersistenceFailureClass> rollbackFailureClasses;
  std::vector<AiDialogIntentDeliveryPersistenceFailureClass> deadLetterFailureClasses;
};

namespace PersistenceDryRunReportDetail {

[[nodiscard]] inline bool hasFailureClass(
    const std::vector<AiDialogIntentDeliveryPersistenceFailureClass>& classes,
    AiDialogIntentDeliveryPersistenceFailureClass expected) noexcept {
  for(const auto failureClass : classes) {
    if(failureClass == expected)
      return true;
  }
  return false;
}

[[nodiscard]] inline std::string limitedPreview(std::string_view value, std::size_t maxBytes) {
  if(value.size() <= maxBytes)
    return std::string(value);
  return std::string(value.substr(0, maxBytes));
}

[[nodiscard]] constexpr AiDialogIntentDeliveryPersistenceDryRunStepStatus dryRunStatusFor(
    AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind kind) noexcept {
  switch(kind) {
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::BeginTransaction:
      return AiDialogIntentDeliveryPersistenceDryRunStepStatus::BeginValidatedNoExecute;
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteStatement:
      return AiDialogIntentDeliveryPersistenceDryRunStepStatus::StatementValidatedNoExecute;
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteProcedure:
      return AiDialogIntentDeliveryPersistenceDryRunStepStatus::ProcedureValidatedNoExecute;
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::SelectOutputSlots:
      return AiDialogIntentDeliveryPersistenceDryRunStepStatus::OutputParserValidatedNoExecute;
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::CommitTransaction:
      return AiDialogIntentDeliveryPersistenceDryRunStepStatus::CommitValidatedNoExecute;
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::RollbackTransaction:
      return AiDialogIntentDeliveryPersistenceDryRunStepStatus::RollbackValidatedNoExecute;
  }
  return AiDialogIntentDeliveryPersistenceDryRunStepStatus::StatementValidatedNoExecute;
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

} // namespace PersistenceDryRunReportDetail

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceDryRunStepNamesCsv(
    const AiDialogIntentDeliveryPersistenceDryRunReportResult& result) {
  std::string out;
  for(const auto& step : result.steps) {
    if(step.statementName.empty())
      continue;
    if(!out.empty())
      out.push_back(',');
    out += step.statementName;
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceDryRunStepKindsCsv(
    const AiDialogIntentDeliveryPersistenceDryRunReportResult& result) {
  std::string out;
  for(const auto& step : result.steps) {
    if(!out.empty())
      out.push_back(',');
    out += aiDialogIntentDeliveryPersistenceExecutorAdapterStepKindName(step.kind);
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceDryRunStepStatusesCsv(
    const AiDialogIntentDeliveryPersistenceDryRunReportResult& result) {
  std::string out;
  for(const auto& step : result.steps) {
    if(!out.empty())
      out.push_back(',');
    out += aiDialogIntentDeliveryPersistenceDryRunStepStatusName(step.status);
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceDryRunRollbackFailureClassesCsv(
    const AiDialogIntentDeliveryPersistenceDryRunReportResult& result) {
  return PersistenceDryRunReportDetail::joinFailureClassNames(result.rollbackFailureClasses);
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceDryRunDeadLetterFailureClassesCsv(
    const AiDialogIntentDeliveryPersistenceDryRunReportResult& result) {
  return PersistenceDryRunReportDetail::joinFailureClassNames(result.deadLetterFailureClasses);
}

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceDryRunReportResult
buildAiDialogIntentDeliveryPersistenceDryRunReportGate(
    const AiDialogIntentDeliveryPersistenceDryRunReportRequest& request) {
  AiDialogIntentDeliveryPersistenceDryRunReportResult out;
  out.executeMysql = false;
  out.wouldCallRunMysql = false;
  out.mutatedDb = false;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::GateDisabled;
    out.reason = "step277_persistence_dry_run_report_gate_disabled";
    return out;
  }
  if(request.allowMysqlExecution) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::UnsafeMysqlExecutionAllowed;
    out.reason = "step277_dry_run_report_gate_forbids_mysql_execution";
    return out;
  }
  if(request.adapter == nullptr) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingExecutorAdapter;
    out.reason = "step277_dry_run_report_gate_requires_step276_executor_adapter";
    return out;
  }

  const auto& adapter = *request.adapter;
  out.actionId = adapter.actionId;
  out.ackKey = adapter.ackKey;
  out.conversationKey = adapter.conversationKey;
  out.sessionUuid = adapter.sessionUuid;
  out.characterKey = adapter.characterKey;
  out.statementCount = adapter.statementCount;
  out.procedureCallCount = adapter.procedureCallCount;
  out.executorStepCount = adapter.executorStepCount;
  out.rollbackRequiredOnFailure = adapter.rollbackRequiredOnFailure;
  out.wouldOpenMysqlConnection = adapter.wouldOpenMysqlConnection;
  out.wouldMutateDb = adapter.wouldMutateDb;

  if(!adapter.ready) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::ExecutorAdapterNotReady;
    out.reason = adapter.reason.empty() ? "step276_executor_adapter_not_ready" : adapter.reason;
    return out;
  }
  if(adapter.executeMysql) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::AdapterRequestedMysql;
    out.reason = "step277_dry_run_report_gate_requires_execute_mysql_off";
    return out;
  }
  if(adapter.wouldCallRunMysql) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::AdapterWouldCallRunMysql;
    out.reason = "step277_dry_run_report_gate_requires_runmysql_off";
    return out;
  }
  if(adapter.mutatedDb) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::AdapterAlreadyMutatedDb;
    out.reason = "step277_dry_run_report_gate_requires_unmutated_adapter";
    return out;
  }
  if(adapter.executorSteps.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::EmptyExecutorPlan;
    out.reason = "step277_dry_run_report_gate_requires_executor_steps";
    return out;
  }
  if(adapter.executorSteps.size() > request.maxExecutorSteps) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::TooManyExecutorSteps;
    out.reason = "step277_dry_run_report_gate_executor_step_limit_exceeded";
    return out;
  }
  if(adapter.wouldMutateDb && !adapter.transactionRequired) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::UnsafeMutatingPlanWithoutTransaction;
    out.reason = "step277_dry_run_report_gate_requires_transaction_for_mutating_plan";
    return out;
  }
  if(!adapter.canParseResultRows || adapter.parsedOutputPreview.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingOutputParser;
    out.reason = "step277_dry_run_report_gate_requires_output_parser_preview";
    return out;
  }
  if(!adapter.canClassifyRollback || adapter.rollbackFailureClasses.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingRollbackClassification;
    out.reason = "step277_dry_run_report_gate_requires_rollback_failure_classes";
    return out;
  }

  bool hasBegin = false;
  bool hasCommit = false;
  bool hasRollback = false;
  bool hasOutputParser = false;
  out.steps.reserve(adapter.executorSteps.size());
  for(const auto& adapterStep : adapter.executorSteps) {
    AiDialogIntentDeliveryPersistenceDryRunStepEvidence step;
    step.kind = adapterStep.kind;
    step.status = PersistenceDryRunReportDetail::dryRunStatusFor(adapterStep.kind);
    step.statementName = adapterStep.statementName;
    step.sqlBytes = adapterStep.sql.size();
    step.sqlPreview = PersistenceDryRunReportDetail::limitedPreview(adapterStep.sql, request.maxSqlPreviewBytes);
    step.mutatesDbPreview = adapterStep.mutatesDb;
    step.requiresMysqlConnection = adapterStep.requiresMysqlConnection;
    step.callsStoredProcedure = adapterStep.callsStoredProcedure;
    step.readsOutputSlots = adapterStep.readsOutputSlots;
    step.failureBranch = adapterStep.failureBranch;
    step.wouldExecuteMysql = false;
    step.wouldCallRunMysql = false;
    step.wouldMutateDb = adapterStep.mutatesDb;
    step.mutatedDb = false;

    switch(adapterStep.kind) {
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::BeginTransaction:
        hasBegin = true;
        break;
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteProcedure:
        break;
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::SelectOutputSlots:
        hasOutputParser = true;
        ++out.outputParserStepCount;
        break;
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::CommitTransaction:
        hasCommit = true;
        break;
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::RollbackTransaction:
        hasRollback = true;
        break;
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteStatement:
        break;
    }
    if(adapterStep.mutatesDb)
      ++out.mutatingPreviewStepCount;
    if(adapterStep.failureBranch)
      ++out.failureBranchStepCount;

    out.steps.push_back(std::move(step));
  }

  if(!hasBegin) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingBeginTransaction;
    out.reason = "step277_dry_run_report_gate_missing_begin_transaction";
    return out;
  }
  if(!hasCommit) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingCommitTransaction;
    out.reason = "step277_dry_run_report_gate_missing_commit_transaction";
    return out;
  }
  if(!hasRollback) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingRollbackTransaction;
    out.reason = "step277_dry_run_report_gate_missing_rollback_transaction";
    return out;
  }
  if(!hasOutputParser) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingOutputParser;
    out.reason = "step277_dry_run_report_gate_missing_output_slot_parser_step";
    return out;
  }

  out.rollbackFailureClasses = adapter.rollbackFailureClasses;
  if(!PersistenceDryRunReportDetail::hasFailureClass(out.rollbackFailureClasses,
                                                     AiDialogIntentDeliveryPersistenceFailureClass::TransactionRollbackRequired) ||
     !PersistenceDryRunReportDetail::hasFailureClass(out.rollbackFailureClasses,
                                                     AiDialogIntentDeliveryPersistenceFailureClass::ExecutionRejected)) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingRollbackClassification;
    out.reason = "step277_dry_run_report_gate_incomplete_rollback_classification";
    return out;
  }

  if(PersistenceDryRunReportDetail::hasFailureClass(adapter.rollbackFailureClasses,
                                                    AiDialogIntentDeliveryPersistenceFailureClass::DeadLetterConflict)) {
    out.deadLetterFailureClasses.push_back(AiDialogIntentDeliveryPersistenceFailureClass::DeadLetterConflict);
  }
  if(PersistenceDryRunReportDetail::hasFailureClass(adapter.rollbackFailureClasses,
                                                    AiDialogIntentDeliveryPersistenceFailureClass::ConnectionUnavailable)) {
    out.deadLetterFailureClasses.push_back(AiDialogIntentDeliveryPersistenceFailureClass::ConnectionUnavailable);
  }
  if(out.deadLetterFailureClasses.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::MissingDeadLetterClassification;
    out.reason = "step277_dry_run_report_gate_requires_dead_letter_failure_classification";
    return out;
  }

  out.dryRunStepCount = out.steps.size();
  out.transactionDryRunReady = true;
  out.outputParserDryRunReady = true;
  out.rollbackReportReady = true;
  out.deadLetterReportReady = true;
  out.status = AiDialogIntentDeliveryPersistenceDryRunReportStatus::ReadyNoExecute;
  out.ready = true;
  out.reason = "step277_persistence_dry_run_report_gate_ready_no_execute";
  return out;
}

} // namespace Mmo::Server

