#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mmo_ai_dialog_intent_delivery_persistence_bridge.h"

namespace Mmo::Server {

enum class AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus : std::uint8_t {
  ReadyNoExecute = 0,
  BoundaryDisabled,
  MissingPreview,
  PreviewNotReady,
  PreviewRequestedExecution,
  PreviewAlreadyMutatedDb,
  EmptyStatementPlan,
  TooManyStatements,
  UnexpectedStatementOrder,
  MissingMutatingPreview,
  MissingProcedureCallPreview,
  MissingRequiredOutputSlot,
  MissingFailureClass,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceExecutionBoundaryStatusName(
    AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus status) noexcept {
  switch(status) {
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::ReadyNoExecute:
      return "ready_no_execute";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::BoundaryDisabled:
      return "boundary_disabled";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingPreview:
      return "missing_preview";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::PreviewNotReady:
      return "preview_not_ready";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::PreviewRequestedExecution:
      return "preview_requested_execution";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::PreviewAlreadyMutatedDb:
      return "preview_already_mutated_db";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::EmptyStatementPlan:
      return "empty_statement_plan";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::TooManyStatements:
      return "too_many_statements";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::UnexpectedStatementOrder:
      return "unexpected_statement_order";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingMutatingPreview:
      return "missing_mutating_preview";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingProcedureCallPreview:
      return "missing_procedure_call_preview";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingRequiredOutputSlot:
      return "missing_required_output_slot";
    case AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingFailureClass:
      return "missing_failure_class";
  }
  return "unknown";
}

enum class AiDialogIntentDeliveryPersistenceFailureClass : std::uint8_t {
  SchemaUnavailable = 0,
  ConnectionUnavailable,
  ExecutionRejected,
  DuplicateDelivery,
  ReceiptConflict,
  DeadLetterConflict,
  TransactionRollbackRequired,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceFailureClassName(
    AiDialogIntentDeliveryPersistenceFailureClass failureClass) noexcept {
  switch(failureClass) {
    case AiDialogIntentDeliveryPersistenceFailureClass::SchemaUnavailable:
      return "schema_unavailable";
    case AiDialogIntentDeliveryPersistenceFailureClass::ConnectionUnavailable:
      return "connection_unavailable";
    case AiDialogIntentDeliveryPersistenceFailureClass::ExecutionRejected:
      return "execution_rejected";
    case AiDialogIntentDeliveryPersistenceFailureClass::DuplicateDelivery:
      return "duplicate_delivery";
    case AiDialogIntentDeliveryPersistenceFailureClass::ReceiptConflict:
      return "receipt_conflict";
    case AiDialogIntentDeliveryPersistenceFailureClass::DeadLetterConflict:
      return "dead_letter_conflict";
    case AiDialogIntentDeliveryPersistenceFailureClass::TransactionRollbackRequired:
      return "transaction_rollback_required";
  }
  return "unknown";
}

enum class AiDialogIntentDeliveryPersistenceExecutionPlanStepKind : std::uint8_t {
  StartTransaction = 0,
  UpsertConversationSession,
  RecordGameplayDeliverySent,
  UpsertConversationObserver,
  PreviewFutureReceiptCall,
  PreviewFutureDeadLetterCall,
  CommitTransaction,
  RollbackOnFailure,
};

[[nodiscard]] constexpr const char* aiDialogIntentDeliveryPersistenceExecutionPlanStepKindName(
    AiDialogIntentDeliveryPersistenceExecutionPlanStepKind kind) noexcept {
  switch(kind) {
    case AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::StartTransaction:
      return "start_transaction";
    case AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::UpsertConversationSession:
      return "upsert_conversation_session";
    case AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::RecordGameplayDeliverySent:
      return "record_gameplay_delivery_sent";
    case AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::UpsertConversationObserver:
      return "upsert_conversation_observer";
    case AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::PreviewFutureReceiptCall:
      return "preview_future_receipt_call";
    case AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::PreviewFutureDeadLetterCall:
      return "preview_future_dead_letter_call";
    case AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::CommitTransaction:
      return "commit_transaction";
    case AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::RollbackOnFailure:
      return "rollback_on_failure";
  }
  return "unknown";
}

struct AiDialogIntentDeliveryPersistenceExecutionPlanStep final {
  AiDialogIntentDeliveryPersistenceExecutionPlanStepKind kind =
      AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::StartTransaction;
  std::string statementName;
  bool requiresMysqlConnection = true;
  bool mutatesDb = true;
  bool procedureCall = false;
  bool outputSlotProducer = false;
  bool failureBranch = false;
};

struct AiDialogIntentDeliveryPersistenceExecutionBoundaryRequest final {
  bool enabled = false;
  bool allowMysqlExecution = false;
  const AiDialogIntentDeliveryPersistencePreviewResult* preview = nullptr;
  std::size_t maxStatements = 16;
};

struct AiDialogIntentDeliveryPersistenceExecutionBoundaryResult final {
  AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus status =
      AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::BoundaryDisabled;
  bool ready = false;
  bool executeMysql = false;
  bool wouldOpenMysqlConnection = false;
  bool wouldMutateDb = false;
  bool mutatedDb = false;
  bool transactionRequired = true;
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
  std::size_t mutatingStatementCount = 0;
  std::size_t outputSlotCount = 0;
  std::vector<AiDialogIntentDeliveryPersistenceExecutionPlanStep> planSteps;
  std::vector<std::string> outputSlots;
  std::vector<AiDialogIntentDeliveryPersistenceFailureClass> failureClasses;
};

namespace Detail {

[[nodiscard]] inline bool containsText(std::string_view text, std::string_view needle) noexcept {
  return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] inline std::string joinStrings(const std::vector<std::string>& values) {
  std::string out;
  for(const auto& value : values) {
    if(!out.empty())
      out.push_back(',');
    out += value;
  }
  return out;
}

[[nodiscard]] inline std::string joinStatementNames(
    const std::vector<AiDialogIntentDeliveryPersistenceExecutionPlanStep>& steps) {
  std::string out;
  for(const auto& step : steps) {
    if(step.statementName.empty())
      continue;
    if(!out.empty())
      out.push_back(',');
    out += step.statementName;
  }
  return out;
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

[[nodiscard]] inline bool hasOutputSlot(const AiDialogIntentDeliveryPersistencePreviewResult& preview,
                                        std::string_view slot) noexcept {
  for(const auto& statement : preview.statements) {
    if(containsText(statement.sql, slot))
      return true;
  }
  return false;
}

} // namespace Detail

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceExecutionPlanStatementNamesCsv(
    const AiDialogIntentDeliveryPersistenceExecutionBoundaryResult& result) {
  return Detail::joinStatementNames(result.planSteps);
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceExecutionOutputSlotsCsv(
    const AiDialogIntentDeliveryPersistenceExecutionBoundaryResult& result) {
  return Detail::joinStrings(result.outputSlots);
}

[[nodiscard]] inline std::string aiDialogIntentDeliveryPersistenceExecutionFailureClassesCsv(
    const AiDialogIntentDeliveryPersistenceExecutionBoundaryResult& result) {
  return Detail::joinFailureClassNames(result.failureClasses);
}

[[nodiscard]] inline AiDialogIntentDeliveryPersistenceExecutionBoundaryResult
buildAiDialogIntentDeliveryPersistenceExecutionBoundary(
    const AiDialogIntentDeliveryPersistenceExecutionBoundaryRequest& request) {
  AiDialogIntentDeliveryPersistenceExecutionBoundaryResult out;
  out.executeMysql = false;
  out.wouldOpenMysqlConnection = false;
  out.wouldMutateDb = false;
  out.mutatedDb = false;

  if(!request.enabled) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::BoundaryDisabled;
    out.reason = "step275_persistence_execution_boundary_disabled";
    return out;
  }

  if(request.preview == nullptr) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingPreview;
    out.reason = "step275_persistence_execution_boundary_requires_step274_preview";
    return out;
  }

  const auto& preview = *request.preview;
  out.actionId = preview.actionId;
  out.ackKey = preview.ackKey;
  out.conversationKey = preview.conversationKey;
  out.sessionUuid = preview.sessionUuid;
  out.characterKey = preview.characterKey;
  out.statementCount = preview.statements.size();

  if(!preview.ready) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::PreviewNotReady;
    out.reason = preview.reason.empty() ? "step274_persistence_preview_not_ready" : preview.reason;
    return out;
  }
  if(preview.executeMysql || request.allowMysqlExecution) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::PreviewRequestedExecution;
    out.reason = "step275_boundary_forbids_mysql_execution";
    return out;
  }
  if(preview.mutatedDb) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::PreviewAlreadyMutatedDb;
    out.reason = "step275_boundary_requires_unmutated_step274_preview";
    return out;
  }
  if(preview.statements.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::EmptyStatementPlan;
    out.reason = "step275_boundary_requires_statement_plan";
    return out;
  }
  if(preview.statements.size() > request.maxStatements) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::TooManyStatements;
    out.reason = "step275_boundary_statement_plan_exceeds_limit";
    return out;
  }

  constexpr std::array<std::string_view, 5> ExpectedOrder = {
      "conversation_session_upsert",
      "gameplay_delivery_sent_call",
      "conversation_observer_upsert",
      "future_gameplay_delivery_receipt_call",
      "future_gameplay_delivery_dead_letter_call",
  };
  if(preview.statements.size() != ExpectedOrder.size()) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::UnexpectedStatementOrder;
    out.reason = "step275_boundary_requires_exact_step274_statement_count";
    return out;
  }
  for(std::size_t i = 0; i != ExpectedOrder.size(); ++i) {
    if(preview.statements[i].name != ExpectedOrder[i]) {
      out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::UnexpectedStatementOrder;
      out.reason = "step275_boundary_statement_order_mismatch";
      return out;
    }
  }

  for(const auto& statement : preview.statements) {
    if(statement.mutating)
      ++out.mutatingStatementCount;
    if(statement.procedureCall)
      ++out.procedureCallCount;
  }
  if(out.mutatingStatementCount != preview.statements.size()) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingMutatingPreview;
    out.reason = "step275_boundary_requires_all_step274_statements_to_be_mutation_previews";
    return out;
  }
  if(out.procedureCallCount != 3) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingProcedureCallPreview;
    out.reason = "step275_boundary_requires_delivery_receipt_and_dead_letter_procedure_previews";
    return out;
  }

  constexpr std::array<std::string_view, 6> RequiredOutputSlots = {
      "@step273_delivery_id",
      "@step273_delivery_status",
      "@step273_receipt_delivery_id",
      "@step273_receipt_kind",
      "@step273_receipt_delivery_status",
      "@step273_dead_letter_id",
  };
  out.outputSlots.reserve(RequiredOutputSlots.size());
  for(const auto slot : RequiredOutputSlots) {
    if(!Detail::hasOutputSlot(preview, slot)) {
      out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingRequiredOutputSlot;
      out.reason = "step275_boundary_missing_required_output_slot";
      return out;
    }
    out.outputSlots.emplace_back(slot);
  }
  out.outputSlotCount = out.outputSlots.size();

  out.failureClasses = {
      AiDialogIntentDeliveryPersistenceFailureClass::SchemaUnavailable,
      AiDialogIntentDeliveryPersistenceFailureClass::ConnectionUnavailable,
      AiDialogIntentDeliveryPersistenceFailureClass::ExecutionRejected,
      AiDialogIntentDeliveryPersistenceFailureClass::DuplicateDelivery,
      AiDialogIntentDeliveryPersistenceFailureClass::ReceiptConflict,
      AiDialogIntentDeliveryPersistenceFailureClass::DeadLetterConflict,
      AiDialogIntentDeliveryPersistenceFailureClass::TransactionRollbackRequired,
  };
  if(out.failureClasses.empty()) {
    out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::MissingFailureClass;
    out.reason = "step275_boundary_requires_failure_classes";
    return out;
  }

  out.planSteps.reserve(8);
  out.planSteps.push_back({AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::StartTransaction,
                           {}, true, false, false, false, false});
  out.planSteps.push_back({AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::UpsertConversationSession,
                           "conversation_session_upsert", true, true, false, false, false});
  out.planSteps.push_back({AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::RecordGameplayDeliverySent,
                           "gameplay_delivery_sent_call", true, true, true, true, false});
  out.planSteps.push_back({AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::UpsertConversationObserver,
                           "conversation_observer_upsert", true, true, false, false, false});
  out.planSteps.push_back({AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::PreviewFutureReceiptCall,
                           "future_gameplay_delivery_receipt_call", true, true, true, true, false});
  out.planSteps.push_back({AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::PreviewFutureDeadLetterCall,
                           "future_gameplay_delivery_dead_letter_call", true, true, true, true, true});
  out.planSteps.push_back({AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::CommitTransaction,
                           {}, true, false, false, false, false});
  out.planSteps.push_back({AiDialogIntentDeliveryPersistenceExecutionPlanStepKind::RollbackOnFailure,
                           {}, true, false, false, false, true});

  out.status = AiDialogIntentDeliveryPersistenceExecutionBoundaryStatus::ReadyNoExecute;
  out.ready = true;
  out.wouldOpenMysqlConnection = true;
  out.wouldMutateDb = true;
  out.reason = "step275_persistence_execution_boundary_ready_no_execute";
  return out;
}

} // namespace Mmo::Server

