#include "mmo_npc_perception_dialog_intent_chain_package.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Mmo::AiRuntime {
namespace {

constexpr std::array<std::string_view, 7> PackageSections = {
    "gate_verdict",
    "safety_flags",
    "transport_correlation",
    "blocked_transitions",
    "missing_storage_surfaces",
    "required_before_live_dispatch",
    "llm_db_changes_pointer",
};

constexpr std::array<std::string_view, 5> DbLedgerNotes = {
    "step253_does_not_apply_sql",
    "step253_does_not_touch_server_sql_directory",
    "step253_does_not_mutate_mysql",
    "future_dispatch_receipts_ack_receipts_timeout_dead_letter_and_terminal_apply_remain_design_only",
    "llm_db_changes_entries_are_not_applied_schema_until_explicitly_migrated",
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

void addIssue(NpcPerceptionDialogIntentChainPackage& package, std::string issue) {
  package.issues.push_back(std::move(issue));
}

void fillPackageSections(NpcPerceptionDialogIntentChainPackage& package) {
  package.packageSections.clear();
  package.packageSections.reserve(PackageSections.size());
  for(const std::string_view section : PackageSections) {
    package.packageSections.emplace_back(section);
  }
}

void fillDbLedgerNotes(NpcPerceptionDialogIntentChainPackage& package) {
  package.dbLedgerNotes.clear();
  package.dbLedgerNotes.reserve(DbLedgerNotes.size());
  for(const std::string_view note : DbLedgerNotes) {
    package.dbLedgerNotes.emplace_back(note);
  }
}

[[nodiscard]] bool gateHasNoLiveSideEffects(const NpcPerceptionDialogIntentChainGate& gate) noexcept {
  return gate.noLiveSideEffects && !gate.dbMutated && !gate.sqlMigrationGenerated && !gate.serverSqlTouched &&
         !gate.liveDispatchEnabled && !gate.liveDispatchReady && !gate.canEnableLiveDispatch;
}

} // namespace

bool supportsNpcPerceptionDialogIntentChainPackage(
    const NpcPerceptionDialogIntentChainGate& gate) noexcept {
  return gate.gateAccepted && gate.ciSafe && gate.dbWorkPaused && gateHasNoLiveSideEffects(gate) &&
         gate.futureStoragePrerequisitesMissing && gate.missingStorageSurfacesDocumented &&
         gate.explicitDbMigrationRequiredBeforeLiveDispatch && !gate.blockedTransitions.empty() &&
         !gate.requiredBeforeLiveDispatch.empty();
}

NpcPerceptionDialogIntentChainPackage buildNpcPerceptionDialogIntentChainPackage(
    const NpcPerceptionDialogIntentChainGate& gate,
    const NpcPerceptionDialogIntentChainPackageOptions& options) {
  NpcPerceptionDialogIntentChainPackage out;
  out.contractVersion = options.contractVersion;
  out.packageSource = options.packageSource;
  out.packageMode = options.packageMode;
  out.gateStatus = gate.status;
  out.gateCiVerdict = gate.ciVerdict;
  out.actionQueueUuid = gate.actionQueueUuid;
  out.decisionUuid = gate.decisionUuid;
  out.actionKind = gate.actionKind;
  out.worldInstanceUuid = gate.worldInstanceUuid;
  out.targetSessionUuid = gate.targetSessionUuid;
  out.targetCharacterUuid = gate.targetCharacterUuid;
  out.ackCorrelationKey = gate.ackCorrelationKey;
  out.llmDbChangesLedgerPath = options.llmDbChangesLedgerPath;
  out.stepLedgerEntry = options.stepLedgerEntry;
  out.packetSequence = gate.packetSequence;
  out.localSequence = gate.localSequence;
  out.ackTimeoutMs = gate.ackTimeoutMs;
  out.maxRetryAttempts = gate.maxRetryAttempts;
  out.completedStageCount = gate.completedStageCount + 1U;
  out.blockedTransitions = gate.blockedTransitions;
  out.missingStorageSurfaces = gate.missingStorageSurfaces;
  out.requiredBeforeLiveDispatch = gate.requiredBeforeLiveDispatch;
  out.blockedTransitionCount = out.blockedTransitions.size();
  out.missingStorageSurfaceCount = out.missingStorageSurfaces.size();
  out.requiredBeforeLiveDispatchCount = out.requiredBeforeLiveDispatch.size();
  out.dbWorkPaused = true;
  out.dbMutated = false;
  out.sqlMigrationGenerated = false;
  out.serverSqlTouched = false;
  out.liveDispatchEnabled = false;
  out.liveDispatchReady = false;
  out.canEnableLiveDispatch = false;
  out.noLiveSideEffects = gateHasNoLiveSideEffects(gate);
  out.gateAccepted = gate.gateAccepted;
  out.gatePackaged = gate.gateAccepted;
  out.ciSafe = gate.gateAccepted && gate.ciSafe;
  out.dbLedgerPointerIncluded = !out.llmDbChangesLedgerPath.empty() && !out.stepLedgerEntry.empty();
  out.reportArtifactWriteRequired = false;
  out.fileWriteExecuted = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.socketReceiveExecuted = false;
  out.livePacketDecoded = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;
  out.actionMarkedApplied = false;
  out.futureDbWorkDocumentedOnly = true;
  out.explicitDbMigrationRequiredBeforeLiveDispatch = true;
  fillPackageSections(out);
  fillDbLedgerNotes(out);

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.packageSource.empty()) {
    addIssue(out, "missing_package_source");
  }
  if(options.packageMode != "read_only_package_no_dispatch_no_db") {
    addIssue(out, "package_mode_must_remain_read_only_package_no_dispatch_no_db");
  }
  if(options.requireGateAccepted && !gate.gateAccepted) {
    addIssue(out, "chain_gate_not_accepted");
  }
  if(options.requireNoLiveSideEffects && !out.noLiveSideEffects) {
    addIssue(out, "live_side_effect_detected");
  }
  if(options.requireDbWorkPaused && !out.dbWorkPaused) {
    addIssue(out, "db_work_not_paused");
  }
  if(options.requireNoDbMutation && gate.dbMutated) {
    addIssue(out, "db_mutation_detected");
  }
  if(options.requireNoSqlGeneration && (gate.sqlMigrationGenerated || gate.serverSqlTouched)) {
    addIssue(out, "sql_generation_or_server_sql_touch_detected");
  }
  if(options.requireLiveDispatchBlocked && (gate.liveDispatchEnabled || gate.liveDispatchReady || gate.canEnableLiveDispatch)) {
    addIssue(out, "live_dispatch_not_blocked");
  }
  if(options.requireDbLedgerPointer && !out.dbLedgerPointerIncluded) {
    addIssue(out, "llm_db_changes_pointer_missing");
  }
  if(gate.missingStorageSurfaces.empty()) {
    addIssue(out, "missing_storage_surfaces_empty");
  }
  if(gate.requiredBeforeLiveDispatch.empty()) {
    addIssue(out, "required_before_live_dispatch_empty");
  }
  if(gate.blockedTransitions.empty()) {
    addIssue(out, "blocked_transitions_empty");
  }

  out.built = out.issues.empty();
  out.packageReady = out.built && supportsNpcPerceptionDialogIntentChainPackage(gate) && out.dbLedgerPointerIncluded;
  out.status = out.packageReady ? "proof_chain_package_ready_no_live_dispatch_no_db" : "proof_chain_package_rejected";
  return out;
}

std::string dialogIntentChainPackageJson(
    const NpcPerceptionDialogIntentChainPackage& package) {
  std::string out;
  out.reserve(4096 + package.packageSections.size() * 48 + package.blockedTransitions.size() * 64 +
              package.missingStorageSurfaces.size() * 48 + package.requiredBeforeLiveDispatch.size() * 72 +
              package.dbLedgerNotes.size() * 72 + package.issues.size() * 48);
  out.push_back('{');
  appendJsonBoolField(out, "built", package.built);
  appendComma(out);
  appendJsonBoolField(out, "package_ready", package.packageReady);
  appendComma(out);
  appendJsonField(out, "status", package.status);
  appendComma(out);
  appendJsonField(out, "contract_version", package.contractVersion);
  appendComma(out);
  appendJsonField(out, "package_source", package.packageSource);
  appendComma(out);
  appendJsonField(out, "package_mode", package.packageMode);
  appendComma(out);
  appendJsonField(out, "package_name", package.packageName);
  appendComma(out);
  appendJsonBoolField(out, "read_only", package.readOnly);
  appendComma(out);
  appendJsonBoolField(out, "proof_only", package.proofOnly);
  appendComma(out);
  appendJsonBoolField(out, "report_only", package.reportOnly);
  appendComma(out);
  appendJsonBoolField(out, "ci_safe", package.ciSafe);
  appendComma(out);
  appendJsonBoolField(out, "db_work_paused", package.dbWorkPaused);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", package.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "sql_migration_generated", package.sqlMigrationGenerated);
  appendComma(out);
  appendJsonBoolField(out, "server_sql_touched", package.serverSqlTouched);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_enabled", package.liveDispatchEnabled);
  appendComma(out);
  appendJsonBoolField(out, "live_dispatch_ready", package.liveDispatchReady);
  appendComma(out);
  appendJsonBoolField(out, "can_enable_live_dispatch", package.canEnableLiveDispatch);
  appendComma(out);
  appendJsonBoolField(out, "no_live_side_effects", package.noLiveSideEffects);
  appendComma(out);
  appendJsonBoolField(out, "gate_accepted", package.gateAccepted);
  appendComma(out);
  appendJsonBoolField(out, "gate_packaged", package.gatePackaged);
  appendComma(out);
  appendJsonBoolField(out, "db_ledger_pointer_included", package.dbLedgerPointerIncluded);
  appendComma(out);
  appendJsonBoolField(out, "report_artifact_write_required", package.reportArtifactWriteRequired);
  appendComma(out);
  appendJsonBoolField(out, "file_write_executed", package.fileWriteExecuted);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", package.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", package.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "socket_receive_executed", package.socketReceiveExecuted);
  appendComma(out);
  appendJsonBoolField(out, "live_packet_decoded", package.livePacketDecoded);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", package.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", package.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", package.markAppliedExecuted);
  appendComma(out);
  appendJsonBoolField(out, "action_marked_applied", package.actionMarkedApplied);
  appendComma(out);
  appendJsonBoolField(out, "future_db_work_documented_only", package.futureDbWorkDocumentedOnly);
  appendComma(out);
  appendJsonBoolField(out, "explicit_db_migration_required_before_live_dispatch", package.explicitDbMigrationRequiredBeforeLiveDispatch);
  appendComma(out);
  appendJsonField(out, "gate_status", package.gateStatus);
  appendComma(out);
  appendJsonField(out, "gate_ci_verdict", package.gateCiVerdict);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", package.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", package.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", package.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", package.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", package.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", package.targetCharacterUuid);
  appendComma(out);
  appendJsonField(out, "ack_correlation_key", package.ackCorrelationKey);
  appendComma(out);
  appendJsonField(out, "llm_db_changes_ledger_path", package.llmDbChangesLedgerPath);
  appendComma(out);
  appendJsonField(out, "step_ledger_entry", package.stepLedgerEntry);
  appendComma(out);
  appendJsonField(out, "recommended_next_step", package.recommendedNextStep);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", package.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", package.localSequence);
  appendComma(out);
  appendJsonCountField(out, "ack_timeout_ms", package.ackTimeoutMs);
  appendComma(out);
  appendJsonCountField(out, "max_retry_attempts", package.maxRetryAttempts);
  appendComma(out);
  appendJsonCountField(out, "completed_stage_count", package.completedStageCount);
  appendComma(out);
  appendJsonCountField(out, "expected_stage_count", package.expectedStageCount);
  appendComma(out);
  appendJsonCountField(out, "blocked_transition_count", package.blockedTransitionCount);
  appendComma(out);
  appendJsonCountField(out, "missing_storage_surface_count", package.missingStorageSurfaceCount);
  appendComma(out);
  appendJsonCountField(out, "required_before_live_dispatch_count", package.requiredBeforeLiveDispatchCount);
  appendComma(out);
  out += jsonEscape("package_sections");
  out.push_back(':');
  appendStringArrayJson(out, package.packageSections);
  appendComma(out);
  out += jsonEscape("blocked_transitions");
  out.push_back(':');
  appendStringArrayJson(out, package.blockedTransitions);
  appendComma(out);
  out += jsonEscape("missing_storage_surfaces");
  out.push_back(':');
  appendStringArrayJson(out, package.missingStorageSurfaces);
  appendComma(out);
  out += jsonEscape("required_before_live_dispatch");
  out.push_back(':');
  appendStringArrayJson(out, package.requiredBeforeLiveDispatch);
  appendComma(out);
  out += jsonEscape("db_ledger_notes");
  out.push_back(':');
  appendStringArrayJson(out, package.dbLedgerNotes);
  appendComma(out);
  appendJsonCountField(out, "issue_count", package.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out.push_back(':');
  appendStringArrayJson(out, package.issues);
  out.push_back('}');
  return out;
}

} // namespace Mmo::AiRuntime
