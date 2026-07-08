#include "mmo_ai_runtime_persistence.h"
#include "mmo_npc_perception_action_dispatcher_boundary.h"
#include "mmo_npc_perception_dialog_intent_preview.h"
#include "mmo_npc_perception_dialog_intent_diagnostic_packet.h"
#include "mmo_npc_perception_dialog_intent_diagnostic_encoder.h"
#include "mmo_npc_perception_dialog_intent_durable_evidence.h"
#include "mmo_npc_perception_dialog_intent_fanout_plan.h"
#include "mmo_npc_perception_effect_descriptor.h"
#include "mmo_server_persistence.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct ProbeOptions final {
  std::string mysqlUrl;
  std::string aiDatabaseName = "mmo_ai_runtime";
  std::string worldInstanceUuid;
  std::string workerId = "mmo_manual_action_dispatcher_probe";
  std::string skipReason = "step236_dispatch_contract_probe_cleanup";
  std::string previewSource = "mmo_manual_action_dispatcher_probe";
  std::string previewMode = "log_only";
  std::string evidenceSource = "mmo_manual_action_dispatcher_probe";
  std::string evidenceMode = "jsonl_only_no_dispatch";
  std::string evidenceJsonlPath;
  std::string fanoutSource = "mmo_manual_action_dispatcher_probe";
  std::string fanoutMode = "plan_only_no_send";
  std::size_t maxInvalidRows = 16;
  std::uint64_t diagnosticPacketSequence = 0;
  std::uint64_t diagnosticLocalSequence = 0;
  std::size_t maxDiagnosticMessageBytes = 4096;
  bool claimOne = false;
  bool skipAfterClaim = false;
  bool allowMutation = false;
  bool strictContract = false;
  bool requireTypedEffect = false;
  bool previewDialogIntent = false;
  bool requirePreview = false;
  bool emitPreviewDiagnosticPacket = false;
  bool requirePreviewDiagnosticPacket = false;
  bool encodePreviewDiagnosticPacket = false;
  bool requirePreviewDiagnosticEncoding = false;
  bool writePreviewEvidenceJsonl = false;
  bool requirePreviewEvidence = false;
  bool allowEvidenceWrite = false;
  bool planPreviewClientFanout = false;
  bool requirePreviewClientFanoutPlan = false;
};

void jsonEscape(std::ostream& out, std::string_view text) {
  out << '"';
  for(const unsigned char c : text) {
    switch(c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if(c < 0x20U) {
          static constexpr char Hex[] = "0123456789abcdef";
          out << "\\u00" << Hex[(c >> 4U) & 0xFU] << Hex[c & 0xFU];
        } else {
          out << static_cast<char>(c);
        }
        break;
    }
  }
  out << '"';
}

void jsonField(std::ostream& out, std::string_view name, std::string_view value, bool comma = true) {
  jsonEscape(out, name);
  out << ": ";
  jsonEscape(out, value);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonRawField(std::ostream& out, std::string_view name, std::string_view value, bool comma = true) {
  jsonEscape(out, name);
  out << ": " << value;
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonCountField(std::ostream& out, std::string_view name, std::size_t value, bool comma = true) {
  jsonEscape(out, name);
  out << ": " << value;
  if(comma) {
    out << ',';
  }
  out << '\n';
}

[[nodiscard]] std::string need(int& i, int argc, char** argv, std::string_view flag) {
  if(i + 1 >= argc) {
    throw std::runtime_error(std::string(flag) + " requires value");
  }
  return argv[++i];
}

[[nodiscard]] std::optional<std::size_t> parseSize(std::string_view text) noexcept {
  std::size_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if(result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] ProbeOptions parseArgs(int argc, char** argv) {
  if(argc < 2) {
    throw std::runtime_error(
        "usage: " + std::string(argv[0]) +
        " <mysql_url> [--ai-db-name NAME] [--world-instance-uuid UUID] [--max-invalid N] "
        "[--strict-contract] [--require-typed-effect] [--preview-dialog-intent] [--require-preview] [--emit-preview-diagnostic-packet] [--require-preview-diagnostic-packet] [--encode-preview-diagnostic-packet] [--require-preview-diagnostic-encoding] [--write-preview-evidence-jsonl PATH] [--require-preview-evidence] [--i-understand-this-writes-evidence] [--diagnostic-packet-sequence N] [--diagnostic-local-sequence N] [--max-diagnostic-message-bytes N] [--claim-one --i-understand-this-mutates-db] [--skip-after-claim]");
  }

  ProbeOptions options;
  options.mysqlUrl = argv[1];
  for(int i = 2; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--ai-db-name") {
      options.aiDatabaseName = need(i, argc, argv, arg);
    } else if(arg == "--world-instance-uuid") {
      options.worldInstanceUuid = need(i, argc, argv, arg);
    } else if(arg == "--worker-id") {
      options.workerId = need(i, argc, argv, arg);
    } else if(arg == "--skip-reason") {
      options.skipReason = need(i, argc, argv, arg);
    } else if(arg == "--preview-source") {
      options.previewSource = need(i, argc, argv, arg);
    } else if(arg == "--preview-mode") {
      options.previewMode = need(i, argc, argv, arg);
    } else if(arg == "--evidence-source") {
      options.evidenceSource = need(i, argc, argv, arg);
    } else if(arg == "--evidence-mode") {
      options.evidenceMode = need(i, argc, argv, arg);
    } else if(arg == "--fanout-source") {
      options.fanoutSource = need(i, argc, argv, arg);
    } else if(arg == "--fanout-mode") {
      options.fanoutMode = need(i, argc, argv, arg);
    } else if(arg == "--write-preview-evidence-jsonl" || arg == "--record-preview-evidence-jsonl") {
      options.writePreviewEvidenceJsonl = true;
      options.evidenceJsonlPath = need(i, argc, argv, arg);
    } else if(arg == "--max-invalid" || arg == "--max-invalid-rows") {
      options.maxInvalidRows = parseSize(need(i, argc, argv, arg)).value_or(options.maxInvalidRows);
    } else if(arg == "--diagnostic-packet-sequence") {
      options.diagnosticPacketSequence = parseSize(need(i, argc, argv, arg)).value_or(options.diagnosticPacketSequence);
    } else if(arg == "--diagnostic-local-sequence") {
      options.diagnosticLocalSequence = parseSize(need(i, argc, argv, arg)).value_or(options.diagnosticLocalSequence);
    } else if(arg == "--max-diagnostic-message-bytes") {
      options.maxDiagnosticMessageBytes = parseSize(need(i, argc, argv, arg)).value_or(options.maxDiagnosticMessageBytes);
    } else if(arg == "--strict-contract") {
      options.strictContract = true;
    } else if(arg == "--require-typed-effect") {
      options.requireTypedEffect = true;
    } else if(arg == "--preview-dialog-intent" || arg == "--preview-typed-effect") {
      options.previewDialogIntent = true;
    } else if(arg == "--require-preview") {
      options.requirePreview = true;
    } else if(arg == "--emit-preview-diagnostic-packet" || arg == "--build-preview-diagnostic-packet") {
      options.emitPreviewDiagnosticPacket = true;
    } else if(arg == "--require-preview-diagnostic-packet") {
      options.requirePreviewDiagnosticPacket = true;
    } else if(arg == "--encode-preview-diagnostic-packet" || arg == "--adapt-preview-diagnostic-packet") {
      options.encodePreviewDiagnosticPacket = true;
    } else if(arg == "--require-preview-diagnostic-encoding") {
      options.requirePreviewDiagnosticEncoding = true;
    } else if(arg == "--require-preview-evidence" || arg == "--require-durable-preview-evidence") {
      options.requirePreviewEvidence = true;
    } else if(arg == "--plan-preview-client-fanout" || arg == "--build-preview-client-fanout-plan") {
      options.planPreviewClientFanout = true;
    } else if(arg == "--require-preview-client-fanout-plan") {
      options.requirePreviewClientFanoutPlan = true;
    } else if(arg == "--claim-one") {
      options.claimOne = true;
    } else if(arg == "--skip-after-claim") {
      options.skipAfterClaim = true;
    } else if(arg == "--i-understand-this-mutates-db") {
      options.allowMutation = true;
    } else if(arg == "--i-understand-this-writes-evidence") {
      options.allowEvidenceWrite = true;
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }

  if(options.skipAfterClaim && !options.claimOne) {
    throw std::runtime_error("--skip-after-claim requires --claim-one");
  }
  if(options.previewDialogIntent && !options.claimOne) {
    throw std::runtime_error("--preview-dialog-intent requires --claim-one");
  }
  if(options.requirePreview && !options.previewDialogIntent) {
    throw std::runtime_error("--require-preview requires --preview-dialog-intent");
  }
  if(options.emitPreviewDiagnosticPacket && !options.previewDialogIntent) {
    throw std::runtime_error("--emit-preview-diagnostic-packet requires --preview-dialog-intent");
  }
  if(options.requirePreviewDiagnosticPacket && !options.emitPreviewDiagnosticPacket) {
    throw std::runtime_error("--require-preview-diagnostic-packet requires --emit-preview-diagnostic-packet");
  }
  if(options.encodePreviewDiagnosticPacket && !options.emitPreviewDiagnosticPacket) {
    throw std::runtime_error("--encode-preview-diagnostic-packet requires --emit-preview-diagnostic-packet");
  }
  if(options.requirePreviewDiagnosticEncoding && !options.encodePreviewDiagnosticPacket) {
    throw std::runtime_error("--require-preview-diagnostic-encoding requires --encode-preview-diagnostic-packet");
  }
  if(options.writePreviewEvidenceJsonl && !options.claimOne) {
    throw std::runtime_error("--write-preview-evidence-jsonl requires --claim-one");
  }
  if(options.writePreviewEvidenceJsonl && !options.encodePreviewDiagnosticPacket) {
    throw std::runtime_error("--write-preview-evidence-jsonl requires --encode-preview-diagnostic-packet");
  }
  if(options.writePreviewEvidenceJsonl && !options.allowEvidenceWrite) {
    throw std::runtime_error("--write-preview-evidence-jsonl writes a local JSONL evidence file; pass --i-understand-this-writes-evidence explicitly");
  }
  if(options.requirePreviewEvidence && !options.writePreviewEvidenceJsonl) {
    throw std::runtime_error("--require-preview-evidence requires --write-preview-evidence-jsonl");
  }
  if(options.planPreviewClientFanout && !options.writePreviewEvidenceJsonl) {
    throw std::runtime_error("--plan-preview-client-fanout requires --write-preview-evidence-jsonl");
  }
  if(options.planPreviewClientFanout && !options.requirePreviewEvidence) {
    throw std::runtime_error("--plan-preview-client-fanout requires --require-preview-evidence");
  }
  if(options.requirePreviewClientFanoutPlan && !options.planPreviewClientFanout) {
    throw std::runtime_error("--require-preview-client-fanout-plan requires --plan-preview-client-fanout");
  }
  if(options.claimOne && !options.allowMutation) {
    throw std::runtime_error("--claim-one mutates DB; pass --i-understand-this-mutates-db explicitly");
  }
  if(options.workerId.empty()) {
    throw std::runtime_error("worker id must not be empty");
  }
  return options;
}

[[nodiscard]] bool inspectionHasContractFailures(
    const Mmo::AiRuntime::NpcPerceptionActionDispatchContractInspection& inspection) noexcept {
  return inspection.unknownActionKindCount != 0 || inspection.missingWorldInstanceUuidCount != 0 ||
         inspection.missingTargetKeyCount != 0 || inspection.missingPayloadObjectCount != 0 ||
         inspection.missingPayloadDecisionUuidCount != 0 || inspection.missingPayloadNpcEntityKeyCount != 0 ||
         inspection.missingPayloadPerceptionKindCount != 0;
}

void jsonContractInspection(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionActionDispatchContractInspection& inspection,
    bool comma = true) {
  out << "{\n";
  jsonCountField(out, "due_pending_count", inspection.duePendingCount);
  jsonCountField(out, "known_action_kind_count", inspection.knownActionKindCount);
  jsonCountField(out, "unknown_action_kind_count", inspection.unknownActionKindCount);
  jsonCountField(out, "missing_world_instance_uuid_count", inspection.missingWorldInstanceUuidCount);
  jsonCountField(out, "missing_target_key_count", inspection.missingTargetKeyCount);
  jsonCountField(out, "missing_payload_object_count", inspection.missingPayloadObjectCount);
  jsonCountField(out, "missing_payload_decision_uuid_count", inspection.missingPayloadDecisionUuidCount);
  jsonCountField(out, "missing_payload_npc_entity_key_count", inspection.missingPayloadNpcEntityKeyCount);
  jsonCountField(out, "missing_payload_perception_kind_count", inspection.missingPayloadPerceptionKindCount);
  jsonCountField(out, "valid_shape_count", inspection.validShapeCount);
  jsonCountField(out, "live_dispatch_implemented_count", inspection.liveDispatchImplementedCount);
  jsonCountField(out, "live_dispatch_blocked_count", inspection.liveDispatchBlockedCount);
  jsonRawField(out, "invalid_actions", inspection.invalidActionsJson, false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonClaimedAction(
    std::ostream& out,
    const Mmo::AiRuntime::ClaimedNpcPerceptionAction& action,
    bool comma = true) {
  out << "{\n";
  jsonRawField(out, "claimed", action.claimed ? "true" : "false");
  jsonField(out, "action_queue_uuid", action.actionQueueUuid);
  jsonField(out, "decision_uuid", action.decisionUuid);
  jsonField(out, "action_kind", action.actionKind);
  jsonField(out, "world_instance_uuid", action.worldInstanceUuid);
  jsonField(out, "session_uuid", action.sessionUuid);
  jsonField(out, "character_uuid", action.characterUuid);
  jsonField(out, "target_key", action.targetKey);
  jsonField(out, "idempotency_key", action.idempotencyKey);
  jsonRawField(out, "request_payload", action.requestPayloadJson.empty() ? "{}" : action.requestPayloadJson, false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonValidation(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionActionDispatchValidation& validation,
    bool comma = true) {
  out << "{\n";
  jsonRawField(out, "accepted", validation.accepted ? "true" : "false");
  jsonField(out, "status", validation.status);
  jsonField(out, "action_kind", validation.actionKind);
  jsonField(out, "effect_kind", validation.effectKind);
  jsonRawField(out, "live_dispatch_allowed", validation.liveDispatchAllowed ? "true" : "false");
  jsonCountField(out, "issue_count", validation.issueCount());
  jsonRawField(out, "issues", Mmo::AiRuntime::validationIssuesJson(validation), false);
  out << '}';
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonTypedEffectDescriptor(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionTypedEffectDescriptor& descriptor,
    bool comma = true) {
  out << Mmo::AiRuntime::typedEffectDescriptorJson(descriptor);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentDiagnosticPacket(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentDiagnosticPacket& packet,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentDiagnosticPacketJson(packet);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentDiagnosticEncoding(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentDiagnosticEncoding& encoding,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentDiagnosticEncodingJson(encoding);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentFanoutPlan(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentFanoutPlan& plan,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentFanoutPlanJson(plan);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

[[nodiscard]] std::string statusFor(
    const ProbeOptions& options,
    const Mmo::AiRuntime::NpcPerceptionActionDispatchContractInspection& inspection,
    const Mmo::AiRuntime::ClaimedNpcPerceptionAction* claimed,
    const Mmo::AiRuntime::NpcPerceptionActionDispatchValidation* validation,
    const Mmo::AiRuntime::NpcPerceptionTypedEffectDescriptor* typedEffect,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentPreview* preview,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentDiagnosticPacket* diagnosticPacket,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentDiagnosticEncoding* diagnosticEncoding,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentDurableEvidence* durableEvidence,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentFanoutPlan* fanoutPlan,
    const Mmo::AiRuntime::SkippedNpcPerceptionAction* skipped) {
  if(claimed == nullptr) {
    if(inspectionHasContractFailures(inspection)) {
      return options.strictContract ? "contract_failed" : "ready_with_contract_failures";
    }
    return inspection.duePendingCount > 0 ? "ready" : "empty";
  }
  if(!claimed->claimed) {
    return "claim_empty";
  }
  if(validation == nullptr || !validation->accepted) {
    return "claimed_contract_failed";
  }
  if(options.requireTypedEffect && (typedEffect == nullptr || !typedEffect->described)) {
    return "claimed_typed_effect_failed";
  }
  if(options.requirePreview && (preview == nullptr || !preview->previewed)) {
    return "claimed_dialog_intent_preview_failed";
  }
  if(options.requirePreviewDiagnosticPacket && (diagnosticPacket == nullptr || !diagnosticPacket->built)) {
    return "claimed_dialog_intent_diagnostic_packet_failed";
  }
  if(options.requirePreviewDiagnosticEncoding && (diagnosticEncoding == nullptr || !diagnosticEncoding->encoded)) {
    return "claimed_dialog_intent_diagnostic_encoding_failed";
  }
  if(options.requirePreviewEvidence && (durableEvidence == nullptr || !durableEvidence->written)) {
    return "claimed_dialog_intent_durable_evidence_failed";
  }
  if(options.requirePreviewClientFanoutPlan && (fanoutPlan == nullptr || !fanoutPlan->built)) {
    return "claimed_dialog_intent_client_fanout_plan_failed";
  }
  if(skipped != nullptr) {
    if(fanoutPlan != nullptr && fanoutPlan->built) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_client_fanout_planned_and_skipped"
                                                : "claimed_dialog_intent_client_fanout_plan_skip_unconfirmed";
    }
    if(durableEvidence != nullptr && durableEvidence->written) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_durable_evidence_written_and_skipped"
                                                : "claimed_dialog_intent_durable_evidence_skip_unconfirmed";
    }
    if(diagnosticEncoding != nullptr && diagnosticEncoding->encoded) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_diagnostic_encoded_and_skipped"
                                                : "claimed_dialog_intent_diagnostic_encoding_skip_unconfirmed";
    }
    if(diagnosticPacket != nullptr && diagnosticPacket->built) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_diagnostic_packet_built_and_skipped"
                                                : "claimed_dialog_intent_diagnostic_packet_skip_unconfirmed";
    }
    if(preview != nullptr && preview->previewed) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_previewed_and_skipped"
                                                : "claimed_dialog_intent_preview_skip_unconfirmed";
    }
    if(typedEffect != nullptr && typedEffect->described) {
      return skipped->actionStatus == "skipped" ? "claimed_typed_effect_validated_and_skipped"
                                                : "claimed_typed_effect_skip_unconfirmed";
    }
    return skipped->actionStatus == "skipped" ? "claimed_validated_and_skipped" : "claimed_validated_skip_unconfirmed";
  }
  if(fanoutPlan != nullptr && fanoutPlan->built) {
    return "claimed_dialog_intent_client_fanout_planned_no_send";
  }
  if(durableEvidence != nullptr && durableEvidence->written) {
    return "claimed_dialog_intent_durable_evidence_written_no_dispatch";
  }
  if(durableEvidence != nullptr && durableEvidence->built) {
    return "claimed_dialog_intent_durable_evidence_built_no_write";
  }
  if(diagnosticEncoding != nullptr && diagnosticEncoding->encoded) {
    return "claimed_dialog_intent_diagnostic_encoded_no_fanout";
  }
  if(diagnosticPacket != nullptr && diagnosticPacket->built) {
    return "claimed_dialog_intent_diagnostic_packet_built_no_fanout";
  }
  if(preview != nullptr && preview->previewed) {
    return "claimed_dialog_intent_previewed_no_dispatch";
  }
  if(typedEffect != nullptr && typedEffect->described) {
    return "claimed_typed_effect_validated_no_dispatch";
  }
  return "claimed_validated_no_dispatch";
}

} // namespace

int main(int argc, char** argv) {
  try {
    const ProbeOptions options = parseArgs(argc, argv);
    const auto target = Mmo::Server::parseMysqlUrl(options.mysqlUrl);

    Mmo::AiRuntime::NpcPerceptionActionDispatchContractInspectOptions inspectOptions;
    inspectOptions.aiDatabaseName = options.aiDatabaseName;
    inspectOptions.worldInstanceUuid = options.worldInstanceUuid;
    inspectOptions.maxInvalidRows = options.maxInvalidRows;
    const auto inspection = Mmo::AiRuntime::inspectNpcPerceptionActionDispatchContracts(target, inspectOptions);

    Mmo::AiRuntime::ClaimedNpcPerceptionAction claimed;
    bool hasClaim = false;
    Mmo::AiRuntime::NpcPerceptionActionDispatchValidation validation;
    bool hasValidation = false;
    Mmo::AiRuntime::NpcPerceptionTypedEffectDescriptor typedEffect;
    bool hasTypedEffect = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentPreview preview;
    bool hasPreview = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentDiagnosticPacket diagnosticPacket;
    bool hasDiagnosticPacket = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentDiagnosticEncoding diagnosticEncoding;
    bool hasDiagnosticEncoding = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentDurableEvidence durableEvidence;
    bool hasDurableEvidence = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentFanoutPlan fanoutPlan;
    bool hasFanoutPlan = false;
    Mmo::AiRuntime::SkippedNpcPerceptionAction skipped;
    bool hasSkip = false;

    if(options.claimOne) {
      Mmo::AiRuntime::ClaimNpcPerceptionActionOptions claimOptions;
      claimOptions.aiDatabaseName = options.aiDatabaseName;
      claimOptions.workerId = options.workerId;
      claimed = Mmo::AiRuntime::claimNextNpcPerceptionAction(target, claimOptions);
      hasClaim = true;

      if(claimed.claimed) {
        validation = Mmo::AiRuntime::validateNpcPerceptionActionDispatchContract(claimed);
        hasValidation = true;
        typedEffect = Mmo::AiRuntime::describeNpcPerceptionTypedEffect(claimed, validation);
        hasTypedEffect = true;
        if(options.previewDialogIntent) {
          Mmo::AiRuntime::NpcPerceptionDialogIntentPreviewOptions previewOptions;
          previewOptions.previewSource = options.previewSource;
          previewOptions.previewMode = options.previewMode;
          preview = Mmo::AiRuntime::previewNpcPerceptionDialogIntent(typedEffect, previewOptions);
          hasPreview = true;
          if(options.emitPreviewDiagnosticPacket) {
            Mmo::AiRuntime::NpcPerceptionDialogIntentDiagnosticPacketOptions diagnosticOptions;
            diagnosticOptions.diagnosticSource = options.previewSource;
            diagnosticPacket = Mmo::AiRuntime::buildNpcPerceptionDialogIntentDiagnosticPacket(preview, diagnosticOptions);
            hasDiagnosticPacket = true;
            if(options.encodePreviewDiagnosticPacket) {
              Mmo::AiRuntime::NpcPerceptionDialogIntentDiagnosticEncodeOptions encodeOptions;
              encodeOptions.packetSequence = options.diagnosticPacketSequence;
              encodeOptions.localSequence = options.diagnosticLocalSequence;
              encodeOptions.maxMessageBytes = options.maxDiagnosticMessageBytes;
              diagnosticEncoding = Mmo::AiRuntime::encodeNpcPerceptionDialogIntentDiagnostic(diagnosticPacket, encodeOptions);
              hasDiagnosticEncoding = true;
              if(options.writePreviewEvidenceJsonl) {
                Mmo::AiRuntime::NpcPerceptionDialogIntentDurableEvidenceOptions evidenceOptions;
                evidenceOptions.evidenceSource = options.evidenceSource;
                evidenceOptions.evidenceMode = options.evidenceMode;
                evidenceOptions.jsonlPath = options.evidenceJsonlPath;
                evidenceOptions.allowFileWrite = options.allowEvidenceWrite;
                durableEvidence = Mmo::AiRuntime::writeNpcPerceptionDialogIntentDurableEvidenceJsonl(
                    claimed,
                    validation,
                    typedEffect,
                    preview,
                    diagnosticPacket,
                    diagnosticEncoding,
                    evidenceOptions);
                hasDurableEvidence = true;
                if(options.planPreviewClientFanout) {
                  Mmo::AiRuntime::NpcPerceptionDialogIntentFanoutPlanOptions fanoutOptions;
                  fanoutOptions.fanoutSource = options.fanoutSource;
                  fanoutOptions.fanoutMode = options.fanoutMode;
                  fanoutOptions.requireDurableEvidenceWritten = true;
                  fanoutPlan = Mmo::AiRuntime::buildNpcPerceptionDialogIntentFanoutPlan(
                      claimed,
                      validation,
                      typedEffect,
                      preview,
                      diagnosticPacket,
                      diagnosticEncoding,
                      durableEvidence,
                      fanoutOptions);
                  hasFanoutPlan = true;
                }
              }
            }
          }
        }
        if(options.skipAfterClaim) {
          Mmo::AiRuntime::SkipNpcPerceptionActionOptions skipOptions;
          skipOptions.aiDatabaseName = options.aiDatabaseName;
          skipOptions.actionQueueUuid = claimed.actionQueueUuid;
          skipOptions.workerId = options.workerId;
          skipOptions.reason = options.skipReason;
          skipped = Mmo::AiRuntime::skipNpcPerceptionAction(target, skipOptions);
          hasSkip = true;
        }
      }
    }

    const auto after = options.claimOne
        ? Mmo::AiRuntime::inspectNpcPerceptionActionDispatchContracts(target, inspectOptions)
        : inspection;

    std::cout << "{\n";
    jsonField(
        std::cout,
        "status",
        statusFor(
            options,
            inspection,
            hasClaim ? &claimed : nullptr,
            hasValidation ? &validation : nullptr,
            hasTypedEffect ? &typedEffect : nullptr,
            hasPreview ? &preview : nullptr,
            hasDiagnosticPacket ? &diagnosticPacket : nullptr,
            hasDiagnosticEncoding ? &diagnosticEncoding : nullptr,
            hasDurableEvidence ? &durableEvidence : nullptr,
            hasFanoutPlan ? &fanoutPlan : nullptr,
            hasSkip ? &skipped : nullptr));
    jsonRawField(std::cout, "mutated_db", options.claimOne ? "true" : "false");
    jsonRawField(std::cout, "live_dispatch_executed", "false");
    jsonField(std::cout, "ai_db_name", options.aiDatabaseName);
    jsonField(std::cout, "world_instance_uuid", options.worldInstanceUuid);
    jsonField(std::cout, "worker_id", options.workerId);
    jsonRawField(std::cout, "strict_contract", options.strictContract ? "true" : "false");
    jsonRawField(std::cout, "require_typed_effect", options.requireTypedEffect ? "true" : "false");
    jsonRawField(std::cout, "preview_dialog_intent", options.previewDialogIntent ? "true" : "false");
    jsonRawField(std::cout, "require_preview", options.requirePreview ? "true" : "false");
    jsonRawField(std::cout, "emit_preview_diagnostic_packet", options.emitPreviewDiagnosticPacket ? "true" : "false");
    jsonRawField(std::cout, "require_preview_diagnostic_packet", options.requirePreviewDiagnosticPacket ? "true" : "false");
    jsonRawField(std::cout, "encode_preview_diagnostic_packet", options.encodePreviewDiagnosticPacket ? "true" : "false");
    jsonRawField(std::cout, "require_preview_diagnostic_encoding", options.requirePreviewDiagnosticEncoding ? "true" : "false");
    jsonRawField(std::cout, "write_preview_evidence_jsonl", options.writePreviewEvidenceJsonl ? "true" : "false");
    jsonRawField(std::cout, "require_preview_evidence", options.requirePreviewEvidence ? "true" : "false");
    jsonRawField(std::cout, "plan_preview_client_fanout", options.planPreviewClientFanout ? "true" : "false");
    jsonRawField(std::cout, "require_preview_client_fanout_plan", options.requirePreviewClientFanoutPlan ? "true" : "false");
    jsonRawField(std::cout, "mutated_files", options.writePreviewEvidenceJsonl ? "true" : "false");
    jsonField(std::cout, "evidence_source", options.evidenceSource);
    jsonField(std::cout, "evidence_mode", options.evidenceMode);
    jsonField(std::cout, "evidence_jsonl_path", options.evidenceJsonlPath);
    jsonField(std::cout, "fanout_source", options.fanoutSource);
    jsonField(std::cout, "fanout_mode", options.fanoutMode);
    jsonField(std::cout, "preview_source", options.previewSource);
    jsonField(std::cout, "preview_mode", options.previewMode);
    jsonRawField(std::cout, "typed_effect_live_dispatch_executed", "false");
    jsonRawField(std::cout, "dialog_intent_preview_live_dispatch_executed", "false");
    jsonRawField(std::cout, "dialog_intent_preview_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_preview_packet_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_packet_live_dispatch_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_packet_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_packet_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_packet_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_packet_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_encoding_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_encoding_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_encoding_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_encoding_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_diagnostic_encoding_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_durable_evidence_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_durable_evidence_live_dispatch_executed", "false");
    jsonRawField(std::cout, "dialog_intent_durable_evidence_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_durable_evidence_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_durable_evidence_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_durable_evidence_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_durable_evidence_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_fanout_plan_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_client_fanout_plan_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_fanout_plan_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_fanout_plan_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_fanout_plan_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_fanout_plan_mark_applied_executed", "false");
    jsonCountField(std::cout, "max_invalid_rows", options.maxInvalidRows);
    jsonCountField(std::cout, "diagnostic_packet_sequence", options.diagnosticPacketSequence);
    jsonCountField(std::cout, "diagnostic_local_sequence", options.diagnosticLocalSequence);
    jsonCountField(std::cout, "max_diagnostic_message_bytes", options.maxDiagnosticMessageBytes);
    std::cout << "\"before\": ";
    jsonContractInspection(std::cout, inspection, true);
    if(hasClaim) {
      std::cout << "\"claim\": ";
      jsonClaimedAction(std::cout, claimed, true);
    }
    if(hasValidation) {
      std::cout << "\"validation\": ";
      jsonValidation(std::cout, validation, true);
    }
    if(hasTypedEffect) {
      std::cout << "\"typed_effect\": ";
      jsonTypedEffectDescriptor(std::cout, typedEffect, true);
    }
    if(hasPreview) {
      std::cout << "\"dialog_intent_preview\": ";
      std::cout << Mmo::AiRuntime::dialogIntentPreviewJson(preview) << ",\n";
    }
    if(hasDiagnosticPacket) {
      std::cout << "\"dialog_intent_diagnostic_packet\": ";
      jsonDialogIntentDiagnosticPacket(std::cout, diagnosticPacket, true);
    }
    if(hasDiagnosticEncoding) {
      std::cout << "\"dialog_intent_diagnostic_encoding\": ";
      jsonDialogIntentDiagnosticEncoding(std::cout, diagnosticEncoding, true);
    }
    if(hasDurableEvidence) {
      std::cout << "\"dialog_intent_durable_evidence\": ";
      std::cout << Mmo::AiRuntime::dialogIntentDurableEvidenceJson(durableEvidence) << ",\n";
    }
    if(hasFanoutPlan) {
      std::cout << "\"dialog_intent_client_fanout_plan\": ";
      jsonDialogIntentFanoutPlan(std::cout, fanoutPlan, true);
    }
    if(hasSkip) {
      std::cout << "\"skip\": {\n";
      jsonField(std::cout, "action_status", skipped.actionStatus, false);
      std::cout << "},\n";
    }
    std::cout << "\"after\": ";
    jsonContractInspection(std::cout, after, false);
    std::cout << "}\n";

    if(options.claimOne && !claimed.claimed) {
      return 2;
    }
    if(hasValidation && !validation.accepted) {
      return 3;
    }
    if(options.requireTypedEffect && (!hasTypedEffect || !typedEffect.described)) {
      return 5;
    }
    if(options.requirePreview && (!hasPreview || !preview.previewed)) {
      return 6;
    }
    if(options.requirePreviewDiagnosticPacket && (!hasDiagnosticPacket || !diagnosticPacket.built)) {
      return 7;
    }
    if(options.requirePreviewDiagnosticEncoding && (!hasDiagnosticEncoding || !diagnosticEncoding.encoded)) {
      return 8;
    }
    if(options.requirePreviewEvidence && (!hasDurableEvidence || !durableEvidence.written)) {
      return 9;
    }
    if(options.requirePreviewClientFanoutPlan && (!hasFanoutPlan || !fanoutPlan.built)) {
      return 10;
    }
    if(options.strictContract && inspectionHasContractFailures(inspection)) {
      return 3;
    }
    if(hasSkip && skipped.actionStatus != "skipped") {
      return 4;
    }
    return 0;
  } catch(const std::exception& e) {
    std::cout << "{\n";
    jsonField(std::cout, "status", "error");
    jsonField(std::cout, "message", e.what(), false);
    std::cout << "}\n";
    return 1;
  }
}












