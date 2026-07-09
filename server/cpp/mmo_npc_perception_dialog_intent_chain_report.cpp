#include "mmo_npc_perception_dialog_intent_chain_report.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Mmo::AiRuntime {
namespace {

constexpr std::array<std::string_view, 4> MissingStorageSurfaces = {
    "ai_dialog_intent_dispatch_receipts",
    "ai_dialog_intent_client_ack_receipts",
    "ai_dialog_intent_timeout_dead_letters",
    "idempotent_ai_action_terminal_apply_procedures",
};

constexpr std::array<std::string_view, 6> NextChecks = {
    "keep_live_dispatch_disabled",
    "keep_database_changes_in_docs_llm_llm_db_changes_while_db_work_is_paused",
    "keep_claimed_test_actions_cleaned_up_by_skip_only",
    "run_full_server_cpp_build_when_the_repo_is_available",
    "run_action_queue_probe_before_any_future_dispatcher_test",
    "design_db_storage_migration_before_mark_applied_or_ack_receipt_persistence",
};

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(const unsigned char c : text) {
    switch(c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if(c < 0x20U) {
          static constexpr char Hex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(Hex[(c >> 4U) & 0xFU]);
          out.push_back(Hex[c & 0xFU]);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

void appendComma(std::string& out) {
  if(!out.empty() && out.back() != '{' && out.back() != '[') {
    out.push_back(',');
  }
}

void appendJsonField(std::string& out, std::string_view key, std::string_view value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += jsonEscape(value);
}

void appendJsonBoolField(std::string& out, std::string_view key, bool value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += value ? "true" : "false";
}

void appendJsonCountField(std::string& out, std::string_view key, std::uint64_t value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void appendStringArrayJson(std::string& out, const std::vector<std::string>& values) {
  out.push_back('[');
  for(std::size_t i = 0; i < values.size(); ++i) {
    if(i != 0U) {
      out.push_back(',');
    }
    out += jsonEscape(values[i]);
  }
  out.push_back(']');
}

void addIssue(NpcPerceptionDialogIntentChainReport& report, std::string issue) {
  report.issues.push_back(std::move(issue));
}

void fillMissingStorageSurfaces(NpcPerceptionDialogIntentChainReport& report) {
  report.missingStorageSurfaces.clear();
  report.missingStorageSurfaces.reserve(MissingStorageSurfaces.size());
  for(const std::string_view surface : MissingStorageSurfaces) {
    report.missingStorageSurfaces.emplace_back(surface);
  }
}

void fillNextChecks(NpcPerceptionDialogIntentChainReport& report) {
  report.nextChecks.clear();
  report.nextChecks.reserve(NextChecks.size());
  for(const std::string_view check : NextChecks) {
    report.nextChecks.emplace_back(check);
  }
}

[[nodiscard]] bool guardHasNoLiveSideEffects(const NpcPerceptionDialogIntentChainGuard& guard) noexcept {
  return guard.noLiveSideEffects && !guard.dbMutated && !guard.timerScheduled && !guard.timeoutObserved &&
         !guard.retryQueued && !guard.deadLetterWritten && !guard.socketReceiveExecuted &&
         !guard.livePacketDecoded && !guard.clientAckObserved && !guard.clientNackObserved &&
         !guard.sendExecuted && !guard.packetFanoutExecuted && !guard.dialogUiExecuted &&
         !guard.audioExecuted && !guard.markAppliedExecuted && !guard.actionMarkedApplied &&
         !guard.actionMarkedFailed;
}

[[nodiscard]] bool transportProofComplete(const NpcPerceptionDialogIntentChainGuard& guard) noexcept {
  return guard.proofChainComplete && !guard.targetSessionUuid.empty() && !guard.targetCharacterUuid.empty();
}

[[nodiscard]] bool ackProofComplete(const NpcPerceptionDialogIntentChainGuard& guard) noexcept {
  return guard.pendingAckSlotShapeReady && guard.ackRouteShapeReady && guard.nackRouteShapeReady &&
         guard.malformedRouteRejected && !guard.ackCorrelationKey.empty() &&
         !guard.expectedAckIdempotencyKey.empty() && !guard.receiveRouteKey.empty();
}

[[nodiscard]] bool timeoutPlanComplete(const NpcPerceptionDialogIntentChainGuard& guard) noexcept {
  return guard.timeoutConfigured && guard.terminalStatusDeferred && guard.timeoutDeadLetterPlanned;
}

} // namespace

bool supportsNpcPerceptionDialogIntentChainReport(
    const NpcPerceptionDialogIntentChainGuard& guard) noexcept {
  return guard.guarded && guard.proofChainComplete && guardHasNoLiveSideEffects(guard);
}

NpcPerceptionDialogIntentChainReport buildNpcPerceptionDialogIntentChainReport(
    const NpcPerceptionDialogIntentChainGuard& guard,
    const NpcPerceptionDialogIntentChainReportOptions& options) {
  NpcPerceptionDialogIntentChainReport out;
  out.contractVersion = options.contractVersion;
  out.reportSource = options.reportSource;
  out.reportMode = options.reportMode;
  out.guardStatus = guard.status;
  out.actionQueueUuid = guard.actionQueueUuid;
  out.decisionUuid = guard.decisionUuid;
  out.actionKind = guard.actionKind;
  out.worldInstanceUuid = guard.worldInstanceUuid;
  out.targetSessionUuid = guard.targetSessionUuid;
  out.targetCharacterUuid = guard.targetCharacterUuid;
  out.ackCorrelationKey = guard.ackCorrelationKey;
  out.packetSequence = guard.packetSequence;
  out.localSequence = guard.localSequence;
  out.ackTimeoutMs = guard.ackTimeoutMs;
  out.maxRetryAttempts = guard.maxRetryAttempts;
  out.completedStages = guard.completedStages;
  out.blockedSideEffects = guard.blockedSideEffects;
  out.completedStageCount = out.completedStages.size();
  out.blockedSideEffectCount = out.blockedSideEffects.size();
  out.guardIssueCount = guard.issueCount();
  out.guardAccepted = guard.guarded;
  out.proofChainComplete = guard.proofChainComplete;
  out.terminalPlanReady = guard.terminalPlanReady;
  out.transportProofComplete = transportProofComplete(guard);
  out.ackProofComplete = ackProofComplete(guard);
  out.timeoutPlanComplete = timeoutPlanComplete(guard);
  out.noLiveSideEffects = guardHasNoLiveSideEffects(guard);
  out.futureStoragePrerequisitesMissing = guard.futureStoragePrerequisitesMissing;
  out.liveDispatchReady = false;
  out.canEnableLiveDispatch = false;
  out.liveDispatchEnabled = false;
  out.dbMutated = false;
  out.sqlMigrationGenerated = false;
  out.serverSqlTouched = false;
  out.explicitDbMigrationRequiredBeforeLiveDispatch = true;
  fillMissingStorageSurfaces(out);
  fillNextChecks(out);

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.reportSource.empty()) {
    addIssue(out, "missing_report_source");
  }
  if(options.reportMode != "read_only_report_no_dispatch_no_db") {
    addIssue(out, "report_mode_must_remain_read_only_report_no_dispatch_no_db");
  }
  if(options.requireGuardedChain && !guard.guarded) {
    addIssue(out, "chain_guard_not_accepted");
  }
  if(!guard.proofChainComplete) {
    addIssue(out, "proof_chain_incomplete");
  }
  if(!out.terminalPlanReady) {
    addIssue(out, "terminal_plan_not_ready");
  }
  if(!out.transportProofComplete) {
    addIssue(out, "transport_proof_incomplete");
  }
  if(!out.ackProofComplete) {
    addIssue(out, "ack_nack_proof_incomplete");
  }
  if(!out.timeoutPlanComplete) {
    addIssue(out, "timeout_plan_incomplete");
  }
  if(options.requireNoLiveSideEffects && !out.noLiveSideEffects) {
    addIssue(out, "live_side_effect_detected");
  }
  if(options.requireFutureStoragePrerequisitesMissing && !guard.futureStoragePrerequisitesMissing) {
    addIssue(out, "future_storage_prerequisites_not_marked_missing");
  }
  if(options.requireNoDbMutation && (guard.dbMutated || guard.deadLetterWritten || guard.actionMarkedFailed ||
                                     guard.actionMarkedApplied || guard.markAppliedExecuted)) {
    addIssue(out, "db_or_terminal_action_mutation_detected");
  }

  out.built = out.issues.empty();
  out.status = out.built ? "proof_chain_report_ready_no_live_dispatch" : "proof_chain_report_rejected";
  return out;
}

std::string dialogIntentChainReportJson(const NpcPerceptionDialogIntentChainReport& report) {
  std::string out;
  out.reserve(4096 + report.completedStages.size() * 48 + report.blockedSideEffects.size() * 32 +
              report.missingStorageSurfaces.size() * 48 + report.nextChecks.size() * 64 +
              report.issues.size() * 48);
  out.push_back('{');
  appendJsonBoolField(out, "built", report.built);
  appendComma(out);
  appendJsonField(out, "status", report.status);
  appendComma(out);
  appendJsonField(out, "contract_version", report.contractVersion);
  appendComma(out);
  appendJsonField(out, "report_source", report.reportSource);
  appendComma(out);
  appendJsonField(out, "report_mode", report.reportMode);
  appendComma(out);
  appendJsonBoolField(out, "report_only", report.reportOnly);
  appendComma(out);
  appendJsonBoolField(out, "proof_only", report.proofOnly);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", report.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "sql_migration_generated", report.sqlMigrationGenerated);
  appendComma(out);
  appendJsonBoolField(out, "server_sql_touched", report.serverSqlTouched);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_enabled", report.liveDispatchEnabled);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_ready", report.liveDispatchReady);
  appendComma(out);
  appendJsonBoolField(out, "can_enable_live_dispatch", report.canEnableLiveDispatch);
  appendComma(out);
  appendJsonBoolField(out, "no_live_side_effects", report.noLiveSideEffects);
  appendComma(out);
  appendJsonBoolField(out, "future_storage_prerequisites_missing", report.futureStoragePrerequisitesMissing);
  appendComma(out);
  appendJsonBoolField(out, "explicit_db_migration_required_before_live_dispatch", report.explicitDbMigrationRequiredBeforeLiveDispatch);
  appendComma(out);
  appendJsonBoolField(out, "guard_accepted", report.guardAccepted);
  appendComma(out);
  appendJsonBoolField(out, "proof_chain_complete", report.proofChainComplete);
  appendComma(out);
  appendJsonBoolField(out, "terminal_plan_ready", report.terminalPlanReady);
  appendComma(out);
  appendJsonBoolField(out, "transport_proof_complete", report.transportProofComplete);
  appendComma(out);
  appendJsonBoolField(out, "ack_proof_complete", report.ackProofComplete);
  appendComma(out);
  appendJsonBoolField(out, "timeout_plan_complete", report.timeoutPlanComplete);
  appendComma(out);
  appendJsonField(out, "guard_status", report.guardStatus);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", report.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", report.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", report.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", report.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", report.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", report.targetCharacterUuid);
  appendComma(out);
  appendJsonField(out, "ack_correlation_key", report.ackCorrelationKey);
  appendComma(out);
  appendJsonField(out, "recommended_next_step", report.recommendedNextStep);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", report.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", report.localSequence);
  appendComma(out);
  appendJsonCountField(out, "ack_timeout_ms", report.ackTimeoutMs);
  appendComma(out);
  appendJsonCountField(out, "max_retry_attempts", report.maxRetryAttempts);
  appendComma(out);
  appendJsonCountField(out, "completed_stage_count", report.completedStageCount);
  appendComma(out);
  appendJsonCountField(out, "expected_stage_count", report.expectedStageCount);
  appendComma(out);
  appendJsonCountField(out, "blocked_side_effect_count", report.blockedSideEffectCount);
  appendComma(out);
  appendJsonCountField(out, "guard_issue_count", report.guardIssueCount);
  appendComma(out);
  out += jsonEscape("completed_stages");
  out.push_back(':');
  appendStringArrayJson(out, report.completedStages);
  appendComma(out);
  out += jsonEscape("blocked_side_effects");
  out.push_back(':');
  appendStringArrayJson(out, report.blockedSideEffects);
  appendComma(out);
  out += jsonEscape("missing_storage_surfaces");
  out.push_back(':');
  appendStringArrayJson(out, report.missingStorageSurfaces);
  appendComma(out);
  out += jsonEscape("next_checks");
  out.push_back(':');
  appendStringArrayJson(out, report.nextChecks);
  appendComma(out);
  appendJsonCountField(out, "issue_count", report.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out.push_back(':');
  appendStringArrayJson(out, report.issues);
  out.push_back('}');
  return out;
}

} // namespace Mmo::AiRuntime
