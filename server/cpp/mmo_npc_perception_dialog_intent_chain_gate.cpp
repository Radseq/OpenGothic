#include "mmo_npc_perception_dialog_intent_chain_gate.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Mmo::AiRuntime {
namespace {

constexpr std::array<std::string_view, 7> RequiredBeforeLiveDispatch = {
    "resume_db_work_explicitly",
    "materialize_dispatch_receipts_schema_from_docs_llm_llm_db_changes",
    "materialize_client_ack_nack_receipts_schema_from_docs_llm_llm_db_changes",
    "materialize_timeout_dead_letter_retry_schema_from_docs_llm_llm_db_changes",
    "add_idempotent_terminal_apply_procedures",
    "add_guarded_udp_send_and_receive_observation_under_explicit_flags",
    "prove_action_queue_cleanup_and_terminal_state_replay_safety",
};

constexpr std::array<std::string_view, 9> BlockedTransitions = {
    "do_not_enable_live_dispatch_from_step252_gate",
    "do_not_generate_sql_from_step252_gate",
    "do_not_touch_server_sql_from_step252_gate",
    "do_not_mark_ai_action_applied_from_step252_gate",
    "do_not_enter_udp_receive_loop_from_step252_gate",
    "do_not_send_udp_packet_from_step252_gate",
    "do_not_open_dialog_ui_or_audio_from_step252_gate",
    "do_not_schedule_timeout_dead_letter_worker_from_step252_gate",
    "do_not_treat_llm_db_changes_as_applied_schema",
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

void addIssue(NpcPerceptionDialogIntentChainGate& gate, std::string issue) {
  gate.issues.push_back(std::move(issue));
}

void addPassedCheck(NpcPerceptionDialogIntentChainGate& gate, std::string check) {
  gate.passedChecks.push_back(std::move(check));
}

void fillBlockedTransitions(NpcPerceptionDialogIntentChainGate& gate) {
  gate.blockedTransitions.clear();
  gate.blockedTransitions.reserve(BlockedTransitions.size());
  for(const std::string_view transition : BlockedTransitions) {
    gate.blockedTransitions.emplace_back(transition);
  }
}

void fillRequiredBeforeLiveDispatch(NpcPerceptionDialogIntentChainGate& gate) {
  gate.requiredBeforeLiveDispatch.clear();
  gate.requiredBeforeLiveDispatch.reserve(RequiredBeforeLiveDispatch.size());
  for(const std::string_view item : RequiredBeforeLiveDispatch) {
    gate.requiredBeforeLiveDispatch.emplace_back(item);
  }
}

} // namespace

bool supportsNpcPerceptionDialogIntentChainGate(
    const NpcPerceptionDialogIntentChainReport& report) noexcept {
  return report.built && report.proofChainComplete && report.noLiveSideEffects && report.futureStoragePrerequisitesMissing &&
         report.explicitDbMigrationRequiredBeforeLiveDispatch && !report.dbMutated && !report.sqlMigrationGenerated &&
         !report.serverSqlTouched && !report.liveDispatchEnabled && !report.liveDispatchReady &&
         !report.canEnableLiveDispatch && !report.missingStorageSurfaces.empty();
}

NpcPerceptionDialogIntentChainGate buildNpcPerceptionDialogIntentChainGate(
    const NpcPerceptionDialogIntentChainReport& report,
    const NpcPerceptionDialogIntentChainGateOptions& options) {
  NpcPerceptionDialogIntentChainGate out;
  out.contractVersion = options.contractVersion;
  out.gateSource = options.gateSource;
  out.gateMode = options.gateMode;
  out.reportStatus = report.status;
  out.actionQueueUuid = report.actionQueueUuid;
  out.decisionUuid = report.decisionUuid;
  out.actionKind = report.actionKind;
  out.worldInstanceUuid = report.worldInstanceUuid;
  out.targetSessionUuid = report.targetSessionUuid;
  out.targetCharacterUuid = report.targetCharacterUuid;
  out.ackCorrelationKey = report.ackCorrelationKey;
  out.packetSequence = report.packetSequence;
  out.localSequence = report.localSequence;
  out.ackTimeoutMs = report.ackTimeoutMs;
  out.maxRetryAttempts = report.maxRetryAttempts;
  out.completedStageCount = report.completedStageCount + 1U;
  out.blockedSideEffectCount = report.blockedSideEffectCount;
  out.reportBuilt = report.built;
  out.proofChainComplete = report.proofChainComplete;
  out.transportProofComplete = report.transportProofComplete;
  out.ackProofComplete = report.ackProofComplete;
  out.timeoutPlanComplete = report.timeoutPlanComplete;
  out.terminalPlanReady = report.terminalPlanReady;
  out.noLiveSideEffects = report.noLiveSideEffects;
  out.futureStoragePrerequisitesMissing = report.futureStoragePrerequisitesMissing;
  out.explicitDbMigrationRequiredBeforeLiveDispatch = report.explicitDbMigrationRequiredBeforeLiveDispatch;
  out.dbMutated = false;
  out.sqlMigrationGenerated = false;
  out.serverSqlTouched = false;
  out.liveDispatchEnabled = false;
  out.liveDispatchReady = false;
  out.canEnableLiveDispatch = false;
  out.missingStorageSurfaces = report.missingStorageSurfaces;
  out.missingStorageSurfaceCount = out.missingStorageSurfaces.size();
  out.missingStorageSurfacesDocumented = !out.missingStorageSurfaces.empty();
  fillBlockedTransitions(out);
  fillRequiredBeforeLiveDispatch(out);

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.gateSource.empty()) {
    addIssue(out, "missing_gate_source");
  }
  if(options.gateMode != "ci_readiness_gate_no_dispatch_no_db") {
    addIssue(out, "gate_mode_must_remain_ci_readiness_gate_no_dispatch_no_db");
  }
  if(options.requireReportBuilt && !report.built) {
    addIssue(out, "chain_report_not_built");
  } else if(report.built) {
    addPassedCheck(out, "chain_report_built");
  }
  if(!report.proofChainComplete) {
    addIssue(out, "proof_chain_incomplete");
  } else {
    addPassedCheck(out, "proof_chain_complete");
  }
  if(!report.transportProofComplete) {
    addIssue(out, "transport_proof_incomplete");
  } else {
    addPassedCheck(out, "transport_proof_complete");
  }
  if(!report.ackProofComplete) {
    addIssue(out, "ack_nack_proof_incomplete");
  } else {
    addPassedCheck(out, "ack_nack_proof_complete");
  }
  if(!report.timeoutPlanComplete) {
    addIssue(out, "timeout_plan_incomplete");
  } else {
    addPassedCheck(out, "timeout_plan_complete");
  }
  if(options.requireNoLiveSideEffects && !report.noLiveSideEffects) {
    addIssue(out, "live_side_effect_detected");
  } else if(report.noLiveSideEffects) {
    addPassedCheck(out, "no_live_side_effects");
  }
  if(options.requireDbWorkPaused && !out.dbWorkPaused) {
    addIssue(out, "db_work_not_marked_paused");
  } else if(out.dbWorkPaused) {
    addPassedCheck(out, "db_work_paused");
  }
  if(options.requireNoDbMutation && report.dbMutated) {
    addIssue(out, "db_mutation_detected");
  } else if(!report.dbMutated) {
    addPassedCheck(out, "no_db_mutation");
  }
  if(options.requireNoSqlGeneration && (report.sqlMigrationGenerated || report.serverSqlTouched)) {
    addIssue(out, "sql_generation_or_server_sql_touch_detected");
  } else if(!report.sqlMigrationGenerated && !report.serverSqlTouched) {
    addPassedCheck(out, "no_sql_generated_or_touched");
  }
  if(options.requireLiveDispatchBlocked && (report.liveDispatchEnabled || report.liveDispatchReady || report.canEnableLiveDispatch)) {
    addIssue(out, "live_dispatch_not_blocked");
  } else if(!report.liveDispatchEnabled && !report.liveDispatchReady && !report.canEnableLiveDispatch) {
    addPassedCheck(out, "live_dispatch_blocked");
  }
  if(options.requireFutureStoragePrerequisitesMissing && !report.futureStoragePrerequisitesMissing) {
    addIssue(out, "future_storage_prerequisites_not_marked_missing");
  } else if(report.futureStoragePrerequisitesMissing) {
    addPassedCheck(out, "future_storage_prerequisites_missing");
  }
  if(options.requireMissingStorageSurfacesDocumented && report.missingStorageSurfaces.empty()) {
    addIssue(out, "missing_storage_surfaces_not_documented");
  } else if(!report.missingStorageSurfaces.empty()) {
    addPassedCheck(out, "missing_storage_surfaces_documented");
  }

  out.built = out.issues.empty();
  out.gateAccepted = out.built && supportsNpcPerceptionDialogIntentChainGate(report);
  out.ciSafe = out.gateAccepted;
  out.ciVerdict = out.gateAccepted ? "pass_proof_chain_safe_keep_live_dispatch_disabled" : "fail_keep_live_dispatch_disabled";
  out.status = out.gateAccepted ? "proof_chain_gate_passed_no_live_dispatch_no_db" : "proof_chain_gate_rejected";
  return out;
}

std::string dialogIntentChainGateJson(const NpcPerceptionDialogIntentChainGate& gate) {
  std::string out;
  out.reserve(4096 + gate.passedChecks.size() * 48 + gate.blockedTransitions.size() * 64 +
              gate.missingStorageSurfaces.size() * 48 + gate.requiredBeforeLiveDispatch.size() * 72 +
              gate.issues.size() * 48);
  out.push_back('{');
  appendJsonBoolField(out, "built", gate.built);
  appendComma(out);
  appendJsonBoolField(out, "gate_accepted", gate.gateAccepted);
  appendComma(out);
  appendJsonField(out, "status", gate.status);
  appendComma(out);
  appendJsonField(out, "contract_version", gate.contractVersion);
  appendComma(out);
  appendJsonField(out, "gate_source", gate.gateSource);
  appendComma(out);
  appendJsonField(out, "gate_mode", gate.gateMode);
  appendComma(out);
  appendJsonBoolField(out, "read_only", gate.readOnly);
  appendComma(out);
  appendJsonBoolField(out, "proof_only", gate.proofOnly);
  appendComma(out);
  appendJsonBoolField(out, "ci_safe", gate.ciSafe);
  appendComma(out);
  appendJsonBoolField(out, "db_work_paused", gate.dbWorkPaused);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", gate.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "sql_migration_generated", gate.sqlMigrationGenerated);
  appendComma(out);
  appendJsonBoolField(out, "server_sql_touched", gate.serverSqlTouched);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_enabled", gate.liveDispatchEnabled);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_ready", gate.liveDispatchReady);
  appendComma(out);
  appendJsonBoolField(out, "can_enable_live_dispatch", gate.canEnableLiveDispatch);
  appendComma(out);
  appendJsonBoolField(out, "no_live_side_effects", gate.noLiveSideEffects);
  appendComma(out);
  appendJsonBoolField(out, "future_storage_prerequisites_missing", gate.futureStoragePrerequisitesMissing);
  appendComma(out);
  appendJsonBoolField(out, "missing_storage_surfaces_documented", gate.missingStorageSurfacesDocumented);
  appendComma(out);
  appendJsonBoolField(out, "explicit_db_migration_required_before_live_dispatch", gate.explicitDbMigrationRequiredBeforeLiveDispatch);
  appendComma(out);
  appendJsonBoolField(out, "report_built", gate.reportBuilt);
  appendComma(out);
  appendJsonBoolField(out, "proof_chain_complete", gate.proofChainComplete);
  appendComma(out);
  appendJsonBoolField(out, "transport_proof_complete", gate.transportProofComplete);
  appendComma(out);
  appendJsonBoolField(out, "ack_proof_complete", gate.ackProofComplete);
  appendComma(out);
  appendJsonBoolField(out, "timeout_plan_complete", gate.timeoutPlanComplete);
  appendComma(out);
  appendJsonBoolField(out, "terminal_plan_ready", gate.terminalPlanReady);
  appendComma(out);
  appendJsonField(out, "report_status", gate.reportStatus);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", gate.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", gate.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", gate.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", gate.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", gate.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", gate.targetCharacterUuid);
  appendComma(out);
  appendJsonField(out, "ack_correlation_key", gate.ackCorrelationKey);
  appendComma(out);
  appendJsonField(out, "ci_verdict", gate.ciVerdict);
  appendComma(out);
  appendJsonField(out, "recommended_next_step", gate.recommendedNextStep);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", gate.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", gate.localSequence);
  appendComma(out);
  appendJsonCountField(out, "ack_timeout_ms", gate.ackTimeoutMs);
  appendComma(out);
  appendJsonCountField(out, "max_retry_attempts", gate.maxRetryAttempts);
  appendComma(out);
  appendJsonCountField(out, "completed_stage_count", gate.completedStageCount);
  appendComma(out);
  appendJsonCountField(out, "expected_stage_count", gate.expectedStageCount);
  appendComma(out);
  appendJsonCountField(out, "blocked_side_effect_count", gate.blockedSideEffectCount);
  appendComma(out);
  appendJsonCountField(out, "missing_storage_surface_count", gate.missingStorageSurfaceCount);
  appendComma(out);
  out += jsonEscape("passed_checks");
  out.push_back(':');
  appendStringArrayJson(out, gate.passedChecks);
  appendComma(out);
  out += jsonEscape("blocked_transitions");
  out.push_back(':');
  appendStringArrayJson(out, gate.blockedTransitions);
  appendComma(out);
  out += jsonEscape("missing_storage_surfaces");
  out.push_back(':');
  appendStringArrayJson(out, gate.missingStorageSurfaces);
  appendComma(out);
  out += jsonEscape("required_before_live_dispatch");
  out.push_back(':');
  appendStringArrayJson(out, gate.requiredBeforeLiveDispatch);
  appendComma(out);
  appendJsonCountField(out, "issue_count", gate.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out.push_back(':');
  appendStringArrayJson(out, gate.issues);
  out.push_back('}');
  return out;
}

} // namespace Mmo::AiRuntime
