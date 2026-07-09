#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mmo_ai_dialog_intent_delivery_persistence_execution_boundary.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryPersistenceExecutorAdapterStatus : std::uint8_t {
  ReadyNoExecute = 0,
  AdapterDisabled,
  MissingExecutionBoundary,
  MissingPreview,
  ExecutionBoundaryNotReady,
  ExecutionRequestedMysql,
  ExecutionAlreadyMutatedDb,
  PreviewMismatch,
  EmptyExecutionPlan,
  MissingOutputSlots,
  MissingFailureClasses,
  UnsupportedPlanStep,
  ResultParserSchemaMismatch,
  MissingRollbackClassification,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceExecutorAdapterStatusName(
    AiDialogIntentDeliveryPersistenceExecutorAdapterStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ReadyNoExecute:
      return "ready_no_execute";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::AdapterDisabled:
      return "adapter_disabled";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingExecutionBoundary:
      return "missing_execution_boundary";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingPreview:
      return "missing_preview";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ExecutionBoundaryNotReady:
      return "execution_boundary_not_ready";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ExecutionRequestedMysql:
      return "execution_requested_mysql";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ExecutionAlreadyMutatedDb:
      return "execution_already_mutated_db";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::PreviewMismatch:
      return "preview_mismatch";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::EmptyExecutionPlan:
      return "empty_execution_plan";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingOutputSlots:
      return "missing_output_slots";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingFailureClasses:
      return "missing_failure_classes";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::UnsupportedPlanStep:
      return "unsupported_plan_step";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ResultParserSchemaMismatch:
      return "result_parser_schema_mismatch";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingRollbackClassification:
      return "missing_rollback_classification";
  }
  return "unknown";
}

enum class AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind : std::uint8_t {
  BeginTransaction = 0,
  ExecuteStatement,
  ExecuteProcedure,
  SelectOutputSlots,
  CommitTransaction,
  RollbackTransaction,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceExecutorAdapterStepKindName(
    AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind kind) noexcept {
  switch(kind) {
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::BeginTransaction:
      return "begin_transaction";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteStatement:
      return "execute_statement";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteProcedure:
      return "execute_procedure";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::SelectOutputSlots:
      return "select_output_slots";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::CommitTransaction:
      return "commit_transaction";
    case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::RollbackTransaction:
      return "rollback_transaction";
  }
  return "unknown";
}

enum class AiDialogIntentDeliveryPersistenceOutputSlotKind : std::uint8_t {
  DeliveryId = 0,
  DeliveryStatus,
  ReceiptDeliveryId,
  ReceiptKind,
  ReceiptDeliveryStatus,
  DeadLetterId,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceOutputSlotKindName(
    AiDialogIntentDeliveryPersistenceOutputSlotKind kind) noexcept {
  switch(kind) {
    case AiDialogIntentDeliveryPersistenceOutputSlotKind::DeliveryId:
      return "delivery_id";
    case AiDialogIntentDeliveryPersistenceOutputSlotKind::DeliveryStatus:
      return "delivery_status";
    case AiDialogIntentDeliveryPersistenceOutputSlotKind::ReceiptDeliveryId:
      return "receipt_delivery_id";
    case AiDialogIntentDeliveryPersistenceOutputSlotKind::ReceiptKind:
      return "receipt_kind";
    case AiDialogIntentDeliveryPersistenceOutputSlotKind::ReceiptDeliveryStatus:
      return "receipt_delivery_status";
    case AiDialogIntentDeliveryPersistenceOutputSlotKind::DeadLetterId:
      return "dead_letter_id";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryPersistenceOutputSlotSpec final {
  std::string variableName;
  std::string columnName;
  AiDialogIntentDeliveryPersistenceOutputSlotKind kind = AiDialogIntentDeliveryPersistenceOutputSlotKind::DeliveryId;
  bool nullableBeforeExecution = true;
  bool binaryUuid = false;
};

struct AiDialogIntentDeliveryPersistenceExecutorPlanStep final {
  AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind kind =
      AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::BeginTransaction;
  std::string statementName;
  std::string sql;
  bool mutatesDb = false;
  bool requiresMysqlConnection = true;
  bool callsStoredProcedure = false;
  bool readsOutputSlots = false;
  bool failureBranch = false;
};

struct AiDialogIntentDeliveryPersistenceParsedOutputValue final {
  std::string variableName;
  std::string columnName;
  AiDialogIntentDeliveryPersistenceOutputSlotKind kind = AiDialogIntentDeliveryPersistenceOutputSlotKind::DeliveryId;
  std::string valuePreview;
  bool isNull = true;
  bool acceptedShape = false;
};

struct AiDialogIntentDeliveryPersistenceExecutorAdapterRequest final {
  bool enabled = false;
  bool allowMysqlExecution = false;
  const AiDialogIntentDeliveryPersistencePreviewResult* preview = nullptr;
  const AiDialogIntentDeliveryPersistenceExecutionBoundaryResult* execution = nullptr;
  std::size_t maxResultValuePreviewBytes = 128;
};

struct AiDialogIntentDeliveryPersistenceExecutorAdapterResult final {
  AiDialogIntentDeliveryPersistenceExecutorAdapterStatus status =
      AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::AdapterDisabled;
  bool ready = false;
  bool executeMysql = false;
  bool wouldOpenMysqlConnection = false;
  bool wouldCallRunMysql = false;
  bool wouldMutateDb = false;
  bool mutatedDb = false;
  bool transactionRequired = true;
  bool canParseResultRows = false;
  bool canClassifyRollback = false;
  bool rollbackRequiredOnFailure = true;
  bool requiresStep273Schema = true;
  std::string reason;
  std::string actionId;
  std::string ackKey;
  std::string conversationKey;
  std::string sessionUuid;
  std::string characterKey;
  std::size_t statementCount = 0;
  std::size_t procedureCallCount = 0;
  std::size_t outputSlotCount = 0;
  std::size_t executorStepCount = 0;
  std::vector<AiDialogIntentDeliveryPersistenceExecutorPlanStep> executorSteps;
  std::vector<AiDialogIntentDeliveryPersistenceOutputSlotSpec> outputSlots;
  std::vector<AiDialogIntentDeliveryPersistenceParsedOutputValue> parsedOutputPreview;
  std::vector<AiDialogIntentDeliveryPersistenceFailureClass> rollbackFailureClasses;
};

namespace Detail {

[[nodiscard]] inline std::string trimLeadingAt(std::string_view value) {
  if(!value.empty() && value.front() == '@')
    value.remove_prefix(1);
  return std::string(value);
}

[[nodiscard]] inline std::string outputSlotSelectSql(
    const std::vector<AiDialogIntentDeliveryPersistenceOutputSlotSpec>& slots) {
  std::string sql = "SELECT ";
  for(std::size_t i = 0; i != slots.size(); ++i) {
    if(i != 0)
      sql += ",";
    sql += slots[i].variableName;
    sql += " AS `";
    sql += slots[i].columnName;
    sql += "`";
  }
  sql += ";";
  return sql;
}

[[nodiscard]] inline bool containsSlot(const std::vector<std::string>& slots, std::string_view slot) noexcept {
  for(const auto& value : slots) {
    if(value == slot)
      return true;
  }
  return false;
}

[[nodiscard]] inline std::vector<AiDialogIntentDeliveryPersistenceOutputSlotSpec>
expectedStep273OutputSlots() {
  return {
      {"@step273_delivery_id", "step273_delivery_id", AiDialogIntentDeliveryPersistenceOutputSlotKind::DeliveryId, true, true},
      {"@step273_delivery_status", "step273_delivery_status", AiDialogIntentDeliveryPersistenceOutputSlotKind::DeliveryStatus, true, false},
      {"@step273_receipt_delivery_id", "step273_receipt_delivery_id", AiDialogIntentDeliveryPersistenceOutputSlotKind::ReceiptDeliveryId, true, true},
      {"@step273_receipt_kind", "step273_receipt_kind", AiDialogIntentDeliveryPersistenceOutputSlotKind::ReceiptKind, true, false},
      {"@step273_receipt_delivery_status", "step273_receipt_delivery_status", AiDialogIntentDeliveryPersistenceOutputSlotKind::ReceiptDeliveryStatus, true, false},
      {"@step273_dead_letter_id", "step273_dead_letter_id", AiDialogIntentDeliveryPersistenceOutputSlotKind::DeadLetterId, true, true},
  };
}

[[nodiscard]] inline std::vector<AiDialogIntentDeliveryPersistenceParsedOutputValue>
buildNoExecuteParsedOutputPreview(
    const std::vector<AiDialogIntentDeliveryPersistenceOutputSlotSpec>& slots,
    std::size_t maxValuePreviewBytes) {
  std::vector<AiDialogIntentDeliveryPersistenceParsedOutputValue> out;
  out.reserve(slots.size());
  for(const auto& slot : slots) {
    AiDialogIntentDeliveryPersistenceParsedOutputValue value;
    value.variableName = slot.variableName;
    value.columnName = slot.columnName;
    value.kind = slot.kind;
    value.isNull = true;
    value.acceptedShape = slot.nullableBeforeExecution;
    value.valuePreview = "<no_execute>";
    if(value.valuePreview.size() > maxValuePreviewBytes)
      value.valuePreview.resize(maxValuePreviewBytes);
    out.push_back(std::move(value));
  }
  return out;
}

[[nodiscard]] inline bool hasFailureClass(
    const std::vector<AiDialogIntentDeliveryPersistenceFailureClass>& classes,
    AiDialogIntentDeliveryPersistenceFailureClass expected) noexcept {
  for(const auto failureClass : classes) {
    if(failureClass == expected)
      return true;
  }
  return false;
}

} // namespace Detail

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceExecutorPlanStepNamesCsv(
    const AiDialogIntentDeliveryPersistenceExecutorAdapterResult& result) {
  std::string out;
  for(const auto& step : result.executorSteps) {
    if(step.statementName.empty())
      continue;
    if(!out.empty())
      out.push_back(',');
    out += step.statementName;
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceExecutorStepKindsCsv(
    const AiDialogIntentDeliveryPersistenceExecutorAdapterResult& result) {
  std::string out;
  for(const auto& step : result.executorSteps) {
    if(!out.empty())
      out.push_back(',');
    out += aiDialogIntentDeliveryPersistenceExecutorAdapterStepKindName(step.kind);
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceExecutorOutputSlotsCsv(
    const AiDialogIntentDeliveryPersistenceExecutorAdapterResult& result) {
  std::string out;
  for(const auto& slot : result.outputSlots) {
    if(!out.empty())
      out.push_back(',');
    out += slot.variableName;
  }
  return out;
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceExecutorRollbackFailureClassesCsv(
    const AiDialogIntentDeliveryPersistenceExecutorAdapterResult& result) {
  std::string out;
  for(const auto failureClass : result.rollbackFailureClasses) {
    if(!out.empty())
      out.push_back(',');
    out += aiDialogIntentDeliveryPersistenceFailureClassName(failureClass);
  }
  return out;
}

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceExecutorAdapterResult
buildAiDialogIntentDeliveryPersistenceExecutorAdapter(
    const AiDialogIntentDeliveryPersistenceExecutorAdapterRequest& request) {
  AiDialogIntentDeliveryPersistenceExecutorAdapterResult out;
  out.executeMysql = false;
  out.wouldCallRunMysql = false;
  out.mutatedDb = false;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::AdapterDisabled;
    out.reason = "step276_persistence_executor_adapter_disabled";
    return out;
  }
  if(request.execution == nullptr) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingExecutionBoundary;
    out.reason = "step276_executor_adapter_requires_step275_execution_boundary";
    return out;
  }
  if(request.preview == nullptr) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingPreview;
    out.reason = "step276_executor_adapter_requires_step274_preview";
    return out;
  }

  const auto& execution = *request.execution;
  const auto& preview = *request.preview;
  out.actionId = execution.actionId;
  out.ackKey = execution.ackKey;
  out.conversationKey = execution.conversationKey;
  out.sessionUuid = execution.sessionUuid;
  out.characterKey = execution.characterKey;
  out.statementCount = execution.statementCount;
  out.procedureCallCount = execution.procedureCallCount;
  out.outputSlotCount = execution.outputSlotCount;
  out.transactionRequired = execution.transactionRequired;
  out.rollbackRequiredOnFailure = execution.rollbackRequiredOnFailure;
  out.requiresStep273Schema = execution.requiresStep273Schema;

  if(!execution.ready) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ExecutionBoundaryNotReady;
    out.reason = execution.reason.empty() ? "step275_execution_boundary_not_ready" : execution.reason;
    return out;
  }
  if(execution.executeMysql || request.allowMysqlExecution) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ExecutionRequestedMysql;
    out.reason = "step276_executor_adapter_forbids_mysql_execution";
    return out;
  }
  if(execution.mutatedDb) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ExecutionAlreadyMutatedDb;
    out.reason = "step276_executor_adapter_requires_unmutated_execution_boundary";
    return out;
  }
  if(preview.actionId != execution.actionId || preview.ackKey != execution.ackKey ||
     preview.conversationKey != execution.conversationKey || preview.sessionUuid != execution.sessionUuid) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::PreviewMismatch;
    out.reason = "step276_executor_adapter_preview_execution_identity_mismatch";
    return out;
  }
  if(execution.planSteps.empty() || preview.statements.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::EmptyExecutionPlan;
    out.reason = "step276_executor_adapter_requires_non_empty_step275_plan";
    return out;
  }
  if(execution.outputSlots.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingOutputSlots;
    out.reason = "step276_executor_adapter_requires_output_slots";
    return out;
  }
  if(execution.failureClasses.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingFailureClasses;
    out.reason = "step276_executor_adapter_requires_failure_classes";
    return out;
  }

  out.outputSlots = Detail::expectedStep273OutputSlots();
  for(const auto& slot : out.outputSlots) {
    if(!Detail::containsSlot(execution.outputSlots, slot.variableName)) {
      out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ResultParserSchemaMismatch;
      out.reason = "step276_executor_adapter_output_slot_schema_mismatch";
      return out;
    }
  }

  if(!Detail::hasFailureClass(execution.failureClasses,
                              AiDialogIntentDeliveryPersistenceFailureClass::TransactionRollbackRequired) ||
     !Detail::hasFailureClass(execution.failureClasses,
                              AiDialogIntentDeliveryPersistenceFailureClass::ExecutionRejected)) {
    out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::MissingRollbackClassification;
    out.reason = "step276_executor_adapter_requires_rollback_failure_classes";
    return out;
  }

  out.executorSteps.reserve(preview.statements.size() + 4);
  out.executorSteps.push_back({
      AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::BeginTransaction,
      "begin_transaction",
      "START TRANSACTION;",
      false,
      true,
      false,
      false,
      false,
  });

  for(const auto& statement : preview.statements) {
    out.executorSteps.push_back({
        statement.procedureCall
            ? AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteProcedure
            : AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteStatement,
        statement.name,
        statement.sql,
        statement.mutating,
        true,
        statement.procedureCall,
        false,
        false,
    });
  }

  out.executorSteps.push_back({
      AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::SelectOutputSlots,
      "select_step273_output_slots",
      Detail::outputSlotSelectSql(out.outputSlots),
      false,
      true,
      false,
      true,
      false,
  });
  out.executorSteps.push_back({
      AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::CommitTransaction,
      "commit_transaction",
      "COMMIT;",
      false,
      true,
      false,
      false,
      false,
  });
  out.executorSteps.push_back({
      AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::RollbackTransaction,
      "rollback_on_failure",
      "ROLLBACK;",
      false,
      true,
      false,
      false,
      true,
  });

  for(const auto& step : out.executorSteps) {
    switch(step.kind) {
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::BeginTransaction:
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteStatement:
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::ExecuteProcedure:
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::SelectOutputSlots:
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::CommitTransaction:
      case AiDialogIntentDeliveryPersistenceExecutorAdapterStepKind::RollbackTransaction:
        break;
      default:
        out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::UnsupportedPlanStep;
        out.reason = "step276_executor_adapter_unsupported_plan_step";
        return out;
    }
  }

  out.rollbackFailureClasses = execution.failureClasses;
  out.parsedOutputPreview = Detail::buildNoExecuteParsedOutputPreview(out.outputSlots, request.maxResultValuePreviewBytes);
  out.executorStepCount = out.executorSteps.size();
  out.canParseResultRows = !out.parsedOutputPreview.empty();
  out.canClassifyRollback = !out.rollbackFailureClasses.empty();
  out.wouldOpenMysqlConnection = execution.wouldOpenMysqlConnection;
  out.wouldMutateDb = execution.wouldMutateDb;
  out.status = AiDialogIntentDeliveryPersistenceExecutorAdapterStatus::ReadyNoExecute;
  out.ready = true;
  out.reason = "step276_persistence_executor_adapter_ready_no_execute";
  return out;
}

} // namespace Mmo::Server
