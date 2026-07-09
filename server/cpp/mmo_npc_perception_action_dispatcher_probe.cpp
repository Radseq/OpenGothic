#include "mmo_ai_runtime_persistence.h"
#include "mmo_npc_perception_action_dispatcher_boundary.h"
#include "mmo_npc_perception_dialog_intent_preview.h"
#include "mmo_npc_perception_dialog_intent_diagnostic_packet.h"
#include "mmo_npc_perception_dialog_intent_diagnostic_encoder.h"
#include "mmo_npc_perception_dialog_intent_durable_evidence.h"
#include "mmo_npc_perception_dialog_intent_endpoint_resolution.h"
#include "mmo_npc_perception_dialog_intent_client_ack_contract.h"
#include "mmo_npc_perception_dialog_intent_client_ack_receipt_preview.h"
#include "mmo_npc_perception_dialog_intent_receive_loop_integration.h"
#include "mmo_npc_perception_dialog_intent_terminal_plan.h"
#include "mmo_npc_perception_dialog_intent_chain_guard.h"
#include "mmo_npc_perception_dialog_intent_chain_report.h"
#include "mmo_npc_perception_dialog_intent_chain_gate.h"
#include "mmo_npc_perception_dialog_intent_chain_package.h"
#include "mmo_npc_perception_dialog_intent_fanout_plan.h"
#include "mmo_npc_perception_dialog_intent_send_boundary.h"
#include "mmo_npc_perception_dialog_intent_sender_adapter.h"
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
  std::string sendSource = "mmo_manual_action_dispatcher_probe";
  std::string sendMode = "prepare_only_no_send";
  std::string senderAdapterSource = "mmo_manual_action_dispatcher_probe";
  std::string senderAdapterMode = "proof_only_no_send";
  std::string endpointResolutionSource = "mmo_manual_action_dispatcher_probe";
  std::string endpointResolutionMode = "proof_only_no_endpoint_lookup";
  std::string clientAckSource = "mmo_manual_action_dispatcher_probe";
  std::string clientAckMode = "preview_only_no_client_ack";
  std::string clientAckReceiptSource = "mmo_manual_action_dispatcher_probe";
  std::string clientAckReceiptMode = "preview_only_no_socket_receive";
  std::string receiveLoopSource = "mmo_manual_action_dispatcher_probe";
  std::string receiveLoopMode = "proof_only_no_socket_receive";
  std::string terminalPlanSource = "mmo_manual_action_dispatcher_probe";
  std::string terminalPlanMode = "plan_only_no_timer_no_db";
  std::string chainGuardSource = "mmo_manual_action_dispatcher_probe";
  std::string chainGuardMode = "guard_only_no_dispatch_no_db";
  std::string chainReportSource = "mmo_manual_action_dispatcher_probe";
  std::string chainReportMode = "read_only_report_no_dispatch_no_db";
  std::string chainGateSource = "mmo_manual_action_dispatcher_probe";
  std::string chainGateMode = "ci_readiness_gate_no_dispatch_no_db";
  std::string chainPackageSource = "mmo_manual_action_dispatcher_probe";
  std::string chainPackageMode = "read_only_package_no_dispatch_no_db";
  std::size_t maxInvalidRows = 16;
  std::uint64_t diagnosticPacketSequence = 0;
  std::uint64_t diagnosticLocalSequence = 0;
  std::size_t maxDiagnosticMessageBytes = 4096;
  std::uint64_t clientAckTimeoutMs = 2500;
  std::uint32_t terminalPlanMaxRetries = 0;
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
  bool preparePreviewSendBoundary = false;
  bool requirePreviewSendBoundary = false;
  bool provePreviewSenderAdapter = false;
  bool requirePreviewSenderAdapter = false;
  bool provePreviewEndpointResolution = false;
  bool requirePreviewEndpointResolution = false;
  bool previewClientAckContract = false;
  bool requirePreviewClientAckContract = false;
  bool previewClientAckReceipt = false;
  bool requirePreviewClientAckReceipt = false;
  bool previewReceiveLoopIntegration = false;
  bool requirePreviewReceiveLoopIntegration = false;
  bool previewTerminalPlan = false;
  bool requirePreviewTerminalPlan = false;
  bool previewChainGuard = false;
  bool requirePreviewChainGuard = false;
  bool reportPreviewChainGuard = false;
  bool requirePreviewChainReport = false;
  bool gatePreviewChainReport = false;
  bool requirePreviewChainGate = false;
  bool packagePreviewChainGate = false;
  bool requirePreviewChainPackage = false;
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
        "[--strict-contract] [--require-typed-effect] [--preview-dialog-intent] [--require-preview] [--emit-preview-diagnostic-packet] [--require-preview-diagnostic-packet] [--encode-preview-diagnostic-packet] [--require-preview-diagnostic-encoding] [--write-preview-evidence-jsonl PATH] [--require-preview-evidence] [--plan-preview-client-fanout] [--prepare-preview-send-boundary] [--require-preview-send-boundary] [--prove-preview-sender-adapter] [--require-preview-sender-adapter] [--prove-preview-endpoint-resolution] [--require-preview-endpoint-resolution] [--preview-client-ack-contract] [--require-preview-client-ack-contract] [--preview-client-ack-receipt-parser] [--require-preview-client-ack-receipt-parser] [--preview-receive-loop-integration] [--require-preview-receive-loop-integration] [--preview-terminal-plan] [--require-preview-terminal-plan] [--preview-chain-guard] [--require-preview-chain-guard] [--report-preview-chain-guard] [--require-preview-chain-report] [--gate-preview-chain-report] [--require-preview-chain-gate] [--package-preview-chain-gate] [--require-preview-chain-package] [--i-understand-this-writes-evidence] [--diagnostic-packet-sequence N] [--diagnostic-local-sequence N] [--max-diagnostic-message-bytes N] [--claim-one --i-understand-this-mutates-db] [--skip-after-claim]");
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
    } else if(arg == "--send-source") {
      options.sendSource = need(i, argc, argv, arg);
    } else if(arg == "--send-mode") {
      options.sendMode = need(i, argc, argv, arg);
    } else if(arg == "--sender-adapter-source") {
      options.senderAdapterSource = need(i, argc, argv, arg);
    } else if(arg == "--sender-adapter-mode") {
      options.senderAdapterMode = need(i, argc, argv, arg);
    } else if(arg == "--endpoint-resolution-source") {
      options.endpointResolutionSource = need(i, argc, argv, arg);
    } else if(arg == "--endpoint-resolution-mode") {
      options.endpointResolutionMode = need(i, argc, argv, arg);
    } else if(arg == "--client-ack-source") {
      options.clientAckSource = need(i, argc, argv, arg);
    } else if(arg == "--client-ack-mode") {
      options.clientAckMode = need(i, argc, argv, arg);
    } else if(arg == "--client-ack-receipt-source") {
      options.clientAckReceiptSource = need(i, argc, argv, arg);
    } else if(arg == "--client-ack-receipt-mode") {
      options.clientAckReceiptMode = need(i, argc, argv, arg);
    } else if(arg == "--receive-loop-source") {
      options.receiveLoopSource = need(i, argc, argv, arg);
    } else if(arg == "--receive-loop-mode") {
      options.receiveLoopMode = need(i, argc, argv, arg);
    } else if(arg == "--terminal-plan-source") {
      options.terminalPlanSource = need(i, argc, argv, arg);
    } else if(arg == "--terminal-plan-mode") {
      options.terminalPlanMode = need(i, argc, argv, arg);
    } else if(arg == "--chain-guard-source") {
      options.chainGuardSource = need(i, argc, argv, arg);
    } else if(arg == "--chain-guard-mode") {
      options.chainGuardMode = need(i, argc, argv, arg);
    } else if(arg == "--chain-report-source") {
      options.chainReportSource = need(i, argc, argv, arg);
    } else if(arg == "--chain-report-mode") {
      options.chainReportMode = need(i, argc, argv, arg);
    } else if(arg == "--chain-gate-source") {
      options.chainGateSource = need(i, argc, argv, arg);
    } else if(arg == "--chain-gate-mode") {
      options.chainGateMode = need(i, argc, argv, arg);
    } else if(arg == "--chain-package-source") {
      options.chainPackageSource = need(i, argc, argv, arg);
    } else if(arg == "--chain-package-mode") {
      options.chainPackageMode = need(i, argc, argv, arg);
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
    } else if(arg == "--client-ack-timeout-ms") {
      options.clientAckTimeoutMs = parseSize(need(i, argc, argv, arg)).value_or(options.clientAckTimeoutMs);
    } else if(arg == "--terminal-plan-max-retries") {
      options.terminalPlanMaxRetries = static_cast<std::uint32_t>(
          parseSize(need(i, argc, argv, arg)).value_or(options.terminalPlanMaxRetries));
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
    } else if(arg == "--prepare-preview-send-boundary" || arg == "--build-preview-send-boundary") {
      options.preparePreviewSendBoundary = true;
    } else if(arg == "--require-preview-send-boundary") {
      options.requirePreviewSendBoundary = true;
    } else if(arg == "--prove-preview-sender-adapter" || arg == "--build-preview-sender-adapter-proof") {
      options.provePreviewSenderAdapter = true;
    } else if(arg == "--require-preview-sender-adapter" || arg == "--require-preview-sender-adapter-proof") {
      options.requirePreviewSenderAdapter = true;
    } else if(arg == "--prove-preview-endpoint-resolution" || arg == "--prepare-preview-endpoint-resolution" ||
              arg == "--build-preview-endpoint-resolution-proof") {
      options.provePreviewEndpointResolution = true;
    } else if(arg == "--require-preview-endpoint-resolution" ||
              arg == "--require-preview-endpoint-resolution-proof") {
      options.requirePreviewEndpointResolution = true;
    } else if(arg == "--preview-client-ack-contract" || arg == "--build-preview-client-ack-contract") {
      options.previewClientAckContract = true;
    } else if(arg == "--require-preview-client-ack-contract") {
      options.requirePreviewClientAckContract = true;
    } else if(arg == "--preview-client-ack-receipt-parser" || arg == "--build-preview-client-ack-receipt-parser" ||
              arg == "--preview-client-ack-receipt-validator" || arg == "--build-preview-client-ack-receipt-validator") {
      options.previewClientAckReceipt = true;
    } else if(arg == "--require-preview-client-ack-receipt-parser" ||
              arg == "--require-preview-client-ack-receipt-validator" ||
              arg == "--require-preview-client-ack-receipt") {
      options.requirePreviewClientAckReceipt = true;
    } else if(arg == "--preview-receive-loop-integration" ||
              arg == "--prove-preview-receive-loop-integration" ||
              arg == "--build-preview-receive-loop-integration-proof") {
      options.previewReceiveLoopIntegration = true;
    } else if(arg == "--require-preview-receive-loop-integration" ||
              arg == "--require-preview-receive-loop-integration-proof") {
      options.requirePreviewReceiveLoopIntegration = true;
    } else if(arg == "--preview-terminal-plan" || arg == "--plan-preview-terminal-status" ||
              arg == "--plan-preview-timeout-dead-letter" || arg == "--build-preview-terminal-plan") {
      options.previewTerminalPlan = true;
    } else if(arg == "--require-preview-terminal-plan" ||
              arg == "--require-preview-timeout-dead-letter-plan") {
      options.requirePreviewTerminalPlan = true;
    } else if(arg == "--preview-chain-guard" || arg == "--build-preview-chain-guard" ||
              arg == "--build-preview-proof-chain-guard" || arg == "--summarize-preview-proof-chain") {
      options.previewChainGuard = true;
    } else if(arg == "--require-preview-chain-guard" || arg == "--require-preview-proof-chain-guard" ||
              arg == "--require-preview-chain-summary") {
      options.requirePreviewChainGuard = true;
    } else if(arg == "--report-preview-chain-guard" || arg == "--build-preview-chain-report" ||
              arg == "--build-preview-proof-chain-report" || arg == "--summarize-preview-chain-report") {
      options.reportPreviewChainGuard = true;
    } else if(arg == "--require-preview-chain-report" || arg == "--require-preview-proof-chain-report") {
      options.requirePreviewChainReport = true;
    } else if(arg == "--gate-preview-chain-report" || arg == "--build-preview-chain-gate" ||
              arg == "--build-preview-proof-chain-gate" || arg == "--ci-gate-preview-chain-report") {
      options.gatePreviewChainReport = true;
    } else if(arg == "--require-preview-chain-gate" || arg == "--require-preview-proof-chain-gate" ||
              arg == "--require-preview-ci-chain-gate") {
      options.requirePreviewChainGate = true;
    } else if(arg == "--package-preview-chain-gate" || arg == "--build-preview-chain-package" ||
              arg == "--build-preview-proof-chain-package" || arg == "--package-preview-proof-chain") {
      options.packagePreviewChainGate = true;
    } else if(arg == "--require-preview-chain-package" || arg == "--require-preview-proof-chain-package" ||
              arg == "--require-preview-chain-gate-package") {
      options.requirePreviewChainPackage = true;
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
  if(options.preparePreviewSendBoundary && !options.planPreviewClientFanout) {
    throw std::runtime_error("--prepare-preview-send-boundary requires --plan-preview-client-fanout");
  }
  if(options.preparePreviewSendBoundary && !options.requirePreviewClientFanoutPlan) {
    throw std::runtime_error("--prepare-preview-send-boundary requires --require-preview-client-fanout-plan");
  }
  if(options.requirePreviewSendBoundary && !options.preparePreviewSendBoundary) {
    throw std::runtime_error("--require-preview-send-boundary requires --prepare-preview-send-boundary");
  }
  if(options.provePreviewSenderAdapter && !options.preparePreviewSendBoundary) {
    throw std::runtime_error("--prove-preview-sender-adapter requires --prepare-preview-send-boundary");
  }
  if(options.provePreviewSenderAdapter && !options.requirePreviewSendBoundary) {
    throw std::runtime_error("--prove-preview-sender-adapter requires --require-preview-send-boundary");
  }
  if(options.requirePreviewSenderAdapter && !options.provePreviewSenderAdapter) {
    throw std::runtime_error("--require-preview-sender-adapter requires --prove-preview-sender-adapter");
  }
  if(options.provePreviewEndpointResolution && !options.provePreviewSenderAdapter) {
    throw std::runtime_error("--prove-preview-endpoint-resolution requires --prove-preview-sender-adapter");
  }
  if(options.provePreviewEndpointResolution && !options.requirePreviewSenderAdapter) {
    throw std::runtime_error("--prove-preview-endpoint-resolution requires --require-preview-sender-adapter");
  }
  if(options.requirePreviewEndpointResolution && !options.provePreviewEndpointResolution) {
    throw std::runtime_error("--require-preview-endpoint-resolution requires --prove-preview-endpoint-resolution");
  }
  if(options.previewClientAckContract && !options.provePreviewEndpointResolution) {
    throw std::runtime_error("--preview-client-ack-contract requires --prove-preview-endpoint-resolution");
  }
  if(options.previewClientAckContract && !options.requirePreviewEndpointResolution) {
    throw std::runtime_error("--preview-client-ack-contract requires --require-preview-endpoint-resolution");
  }
  if(options.requirePreviewClientAckContract && !options.previewClientAckContract) {
    throw std::runtime_error("--require-preview-client-ack-contract requires --preview-client-ack-contract");
  }
  if(options.previewClientAckReceipt && !options.previewClientAckContract) {
    throw std::runtime_error("--preview-client-ack-receipt-parser requires --preview-client-ack-contract");
  }
  if(options.previewClientAckReceipt && !options.requirePreviewClientAckContract) {
    throw std::runtime_error("--preview-client-ack-receipt-parser requires --require-preview-client-ack-contract");
  }
  if(options.requirePreviewClientAckReceipt && !options.previewClientAckReceipt) {
    throw std::runtime_error("--require-preview-client-ack-receipt-parser requires --preview-client-ack-receipt-parser");
  }
  if(options.previewReceiveLoopIntegration && !options.previewClientAckReceipt) {
    throw std::runtime_error("--preview-receive-loop-integration requires --preview-client-ack-receipt-parser");
  }
  if(options.previewReceiveLoopIntegration && !options.requirePreviewClientAckReceipt) {
    throw std::runtime_error("--preview-receive-loop-integration requires --require-preview-client-ack-receipt");
  }
  if(options.requirePreviewReceiveLoopIntegration && !options.previewReceiveLoopIntegration) {
    throw std::runtime_error("--require-preview-receive-loop-integration requires --preview-receive-loop-integration");
  }
  if(options.previewTerminalPlan && !options.previewReceiveLoopIntegration) {
    throw std::runtime_error("--preview-terminal-plan requires --preview-receive-loop-integration");
  }
  if(options.previewTerminalPlan && !options.requirePreviewReceiveLoopIntegration) {
    throw std::runtime_error("--preview-terminal-plan requires --require-preview-receive-loop-integration");
  }
  if(options.requirePreviewTerminalPlan && !options.previewTerminalPlan) {
    throw std::runtime_error("--require-preview-terminal-plan requires --preview-terminal-plan");
  }
  if(options.previewChainGuard && !options.previewTerminalPlan) {
    throw std::runtime_error("--preview-chain-guard requires --preview-terminal-plan");
  }
  if(options.previewChainGuard && !options.requirePreviewTerminalPlan) {
    throw std::runtime_error("--preview-chain-guard requires --require-preview-terminal-plan");
  }
  if(options.requirePreviewChainGuard && !options.previewChainGuard) {
    throw std::runtime_error("--require-preview-chain-guard requires --preview-chain-guard");
  }
  if(options.reportPreviewChainGuard && !options.previewChainGuard) {
    throw std::runtime_error("--report-preview-chain-guard requires --preview-chain-guard");
  }
  if(options.reportPreviewChainGuard && !options.requirePreviewChainGuard) {
    throw std::runtime_error("--report-preview-chain-guard requires --require-preview-chain-guard");
  }
  if(options.requirePreviewChainReport && !options.reportPreviewChainGuard) {
    throw std::runtime_error("--require-preview-chain-report requires --report-preview-chain-guard");
  }
  if(options.gatePreviewChainReport && !options.reportPreviewChainGuard) {
    throw std::runtime_error("--gate-preview-chain-report requires --report-preview-chain-guard");
  }
  if(options.gatePreviewChainReport && !options.requirePreviewChainReport) {
    throw std::runtime_error("--gate-preview-chain-report requires --require-preview-chain-report");
  }
  if(options.requirePreviewChainGate && !options.gatePreviewChainReport) {
    throw std::runtime_error("--require-preview-chain-gate requires --gate-preview-chain-report");
  }
  if(options.packagePreviewChainGate && !options.gatePreviewChainReport) {
    throw std::runtime_error("--package-preview-chain-gate requires --gate-preview-chain-report");
  }
  if(options.packagePreviewChainGate && !options.requirePreviewChainGate) {
    throw std::runtime_error("--package-preview-chain-gate requires --require-preview-chain-gate");
  }
  if(options.requirePreviewChainPackage && !options.packagePreviewChainGate) {
    throw std::runtime_error("--require-preview-chain-package requires --package-preview-chain-gate");
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

void jsonDialogIntentSendBoundary(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentSendBoundary& boundary,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentSendBoundaryJson(boundary);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentSenderAdapterProof(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentSenderAdapterProof& proof,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentSenderAdapterProofJson(proof);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentEndpointResolutionProof(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentEndpointResolutionProof& proof,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentEndpointResolutionProofJson(proof);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentClientAckContract(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentClientAckContract& contract,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentClientAckContractJson(contract);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentClientAckReceiptPreview(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentClientAckReceiptPreview& preview,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentClientAckReceiptPreviewJson(preview);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentReceiveLoopIntegrationProof(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentReceiveLoopIntegrationProof& proof,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentReceiveLoopIntegrationProofJson(proof);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentTerminalPlan(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentTerminalPlan& plan,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentTerminalPlanJson(plan);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentChainGuard(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentChainGuard& guard,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentChainGuardJson(guard);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentChainReport(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentChainReport& report,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentChainReportJson(report);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentChainGate(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentChainGate& gate,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentChainGateJson(gate);
  if(comma) {
    out << ',';
  }
  out << '\n';
}

void jsonDialogIntentChainPackage(
    std::ostream& out,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentChainPackage& package,
    bool comma = true) {
  out << Mmo::AiRuntime::dialogIntentChainPackageJson(package);
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
    const Mmo::AiRuntime::NpcPerceptionDialogIntentSendBoundary* sendBoundary,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentSenderAdapterProof* senderAdapterProof,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentEndpointResolutionProof* endpointResolutionProof,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentClientAckContract* clientAckContract,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentClientAckReceiptPreview* clientAckReceiptPreview,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentReceiveLoopIntegrationProof* receiveLoopIntegrationProof,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentTerminalPlan* terminalPlan,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentChainGuard* chainGuard,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentChainReport* chainReport,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentChainGate* chainGate,
    const Mmo::AiRuntime::NpcPerceptionDialogIntentChainPackage* chainPackage,
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
  if(options.requirePreviewSendBoundary && (sendBoundary == nullptr || !sendBoundary->prepared)) {
    return "claimed_dialog_intent_send_boundary_failed";
  }
  if(options.requirePreviewSenderAdapter && (senderAdapterProof == nullptr || !senderAdapterProof->proofed)) {
    return "claimed_dialog_intent_sender_adapter_failed";
  }
  if(options.requirePreviewEndpointResolution &&
     (endpointResolutionProof == nullptr || !endpointResolutionProof->proofed)) {
    return "claimed_dialog_intent_endpoint_resolution_failed";
  }
  if(options.requirePreviewClientAckContract && (clientAckContract == nullptr || !clientAckContract->built)) {
    return "claimed_dialog_intent_client_ack_contract_failed";
  }
  if(options.requirePreviewClientAckReceipt && (clientAckReceiptPreview == nullptr || !clientAckReceiptPreview->built)) {
    return "claimed_dialog_intent_client_ack_receipt_preview_failed";
  }
  if(options.requirePreviewReceiveLoopIntegration &&
     (receiveLoopIntegrationProof == nullptr || !receiveLoopIntegrationProof->proofed)) {
    return "claimed_dialog_intent_receive_loop_integration_failed";
  }
  if(options.requirePreviewTerminalPlan && (terminalPlan == nullptr || !terminalPlan->planned)) {
    return "claimed_dialog_intent_terminal_plan_failed";
  }
  if(options.requirePreviewChainGuard && (chainGuard == nullptr || !chainGuard->guarded)) {
    return "claimed_dialog_intent_chain_guard_failed";
  }
  if(options.requirePreviewChainReport && (chainReport == nullptr || !chainReport->built)) {
    return "claimed_dialog_intent_chain_report_failed";
  }
  if(options.requirePreviewChainGate && (chainGate == nullptr || !chainGate->gateAccepted)) {
    return "claimed_dialog_intent_chain_gate_failed";
  }
  if(options.requirePreviewChainPackage && (chainPackage == nullptr || !chainPackage->packageReady)) {
    return "claimed_dialog_intent_chain_package_failed";
  }
  if(skipped != nullptr) {
    if(chainPackage != nullptr && chainPackage->packageReady) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_chain_package_ready_and_skipped"
                                                : "claimed_dialog_intent_chain_package_skip_unconfirmed";
    }
    if(chainGate != nullptr && chainGate->gateAccepted) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_chain_gate_passed_and_skipped"
                                                : "claimed_dialog_intent_chain_gate_skip_unconfirmed";
    }
    if(chainReport != nullptr && chainReport->built) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_chain_report_ready_and_skipped"
                                                : "claimed_dialog_intent_chain_report_skip_unconfirmed";
    }
    if(chainGuard != nullptr && chainGuard->guarded) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_chain_guarded_and_skipped"
                                                : "claimed_dialog_intent_chain_guard_skip_unconfirmed";
    }
    if(terminalPlan != nullptr && terminalPlan->planned) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_terminal_plan_ready_and_skipped"
                                                : "claimed_dialog_intent_terminal_plan_skip_unconfirmed";
    }
    if(receiveLoopIntegrationProof != nullptr && receiveLoopIntegrationProof->proofed) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_receive_loop_integration_proofed_and_skipped"
                                                : "claimed_dialog_intent_receive_loop_integration_skip_unconfirmed";
    }
    if(clientAckReceiptPreview != nullptr && clientAckReceiptPreview->built) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_client_ack_receipt_previewed_and_skipped"
                                                : "claimed_dialog_intent_client_ack_receipt_preview_skip_unconfirmed";
    }
    if(clientAckContract != nullptr && clientAckContract->built) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_client_ack_contract_previewed_and_skipped"
                                                : "claimed_dialog_intent_client_ack_contract_skip_unconfirmed";
    }
    if(endpointResolutionProof != nullptr && endpointResolutionProof->proofed) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_endpoint_resolution_proofed_and_skipped"
                                                : "claimed_dialog_intent_endpoint_resolution_skip_unconfirmed";
    }
    if(senderAdapterProof != nullptr && senderAdapterProof->proofed) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_sender_adapter_proofed_and_skipped"
                                                : "claimed_dialog_intent_sender_adapter_skip_unconfirmed";
    }
    if(sendBoundary != nullptr && sendBoundary->prepared) {
      return skipped->actionStatus == "skipped" ? "claimed_dialog_intent_send_boundary_prepared_and_skipped"
                                                : "claimed_dialog_intent_send_boundary_skip_unconfirmed";
    }
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
  if(chainPackage != nullptr && chainPackage->packageReady) {
    return "claimed_dialog_intent_chain_package_ready_no_live_dispatch_no_db";
  }
  if(chainGate != nullptr && chainGate->gateAccepted) {
    return "claimed_dialog_intent_chain_gate_passed_no_live_dispatch_no_db";
  }
  if(chainReport != nullptr && chainReport->built) {
    return "claimed_dialog_intent_chain_report_ready_no_live_dispatch";
  }
  if(chainGuard != nullptr && chainGuard->guarded) {
    return "claimed_dialog_intent_chain_guarded_no_live_side_effects";
  }
  if(terminalPlan != nullptr && terminalPlan->planned) {
    return "claimed_dialog_intent_terminal_plan_ready_no_timer_no_db";
  }
  if(receiveLoopIntegrationProof != nullptr && receiveLoopIntegrationProof->proofed) {
    return "claimed_dialog_intent_receive_loop_integration_proofed_no_socket_receive";
  }
  if(clientAckReceiptPreview != nullptr && clientAckReceiptPreview->built) {
    return "claimed_dialog_intent_client_ack_receipt_previewed_no_socket_receive";
  }
  if(clientAckContract != nullptr && clientAckContract->built) {
    return "claimed_dialog_intent_client_ack_contract_previewed_no_ack";
  }
  if(endpointResolutionProof != nullptr && endpointResolutionProof->proofed) {
    return "claimed_dialog_intent_endpoint_resolution_proofed_no_lookup";
  }
  if(senderAdapterProof != nullptr && senderAdapterProof->proofed) {
    return "claimed_dialog_intent_sender_adapter_proofed_no_send";
  }
  if(sendBoundary != nullptr && sendBoundary->prepared) {
    return "claimed_dialog_intent_send_boundary_prepared_no_send";
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
    Mmo::AiRuntime::NpcPerceptionDialogIntentSendBoundary sendBoundary;
    bool hasSendBoundary = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentSenderAdapterProof senderAdapterProof;
    bool hasSenderAdapterProof = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentEndpointResolutionProof endpointResolutionProof;
    bool hasEndpointResolutionProof = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentClientAckContract clientAckContract;
    bool hasClientAckContract = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentClientAckReceiptPreview clientAckReceiptPreview;
    bool hasClientAckReceiptPreview = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentReceiveLoopIntegrationProof receiveLoopIntegrationProof;
    bool hasReceiveLoopIntegrationProof = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentTerminalPlan terminalPlan;
    bool hasTerminalPlan = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentChainGuard chainGuard;
    bool hasChainGuard = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentChainReport chainReport;
    bool hasChainReport = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentChainGate chainGate;
    bool hasChainGate = false;
    Mmo::AiRuntime::NpcPerceptionDialogIntentChainPackage chainPackage;
    bool hasChainPackage = false;
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
                  if(options.preparePreviewSendBoundary) {
                    Mmo::AiRuntime::NpcPerceptionDialogIntentSendBoundaryOptions sendOptions;
                    sendOptions.sendSource = options.sendSource;
                    sendOptions.sendMode = options.sendMode;
                    sendBoundary = Mmo::AiRuntime::prepareNpcPerceptionDialogIntentSendBoundary(
                        fanoutPlan,
                        diagnosticEncoding,
                        sendOptions);
                    hasSendBoundary = true;
                    if(options.provePreviewSenderAdapter) {
                      Mmo::AiRuntime::NpcPerceptionDialogIntentSenderAdapterOptions adapterOptions;
                      adapterOptions.adapterSource = options.senderAdapterSource;
                      adapterOptions.adapterMode = options.senderAdapterMode;
                      senderAdapterProof = Mmo::AiRuntime::proveNpcPerceptionDialogIntentSenderAdapter(
                          sendBoundary,
                          diagnosticEncoding,
                          adapterOptions);
                      hasSenderAdapterProof = true;
                      if(options.provePreviewEndpointResolution) {
                        Mmo::AiRuntime::NpcPerceptionDialogIntentEndpointResolutionOptions endpointOptions;
                        endpointOptions.resolutionSource = options.endpointResolutionSource;
                        endpointOptions.resolutionMode = options.endpointResolutionMode;
                        endpointResolutionProof = Mmo::AiRuntime::proveNpcPerceptionDialogIntentEndpointResolution(
                            senderAdapterProof,
                            endpointOptions);
                        hasEndpointResolutionProof = true;
                        if(options.previewClientAckContract) {
                          Mmo::AiRuntime::NpcPerceptionDialogIntentClientAckContractOptions ackOptions;
                          ackOptions.ackSource = options.clientAckSource;
                          ackOptions.ackMode = options.clientAckMode;
                          ackOptions.ackTimeoutMs = options.clientAckTimeoutMs;
                          clientAckContract = Mmo::AiRuntime::buildNpcPerceptionDialogIntentClientAckContract(
                              endpointResolutionProof,
                              ackOptions);
                          hasClientAckContract = true;
                          if(options.previewClientAckReceipt) {
                            Mmo::AiRuntime::NpcPerceptionDialogIntentClientAckReceiptPreviewOptions receiptOptions;
                            receiptOptions.receiptSource = options.clientAckReceiptSource;
                            receiptOptions.receiptMode = options.clientAckReceiptMode;
                            clientAckReceiptPreview =
                                Mmo::AiRuntime::buildNpcPerceptionDialogIntentClientAckReceiptPreview(
                                    clientAckContract,
                                    receiptOptions);
                            hasClientAckReceiptPreview = true;
                            if(options.previewReceiveLoopIntegration) {
                              Mmo::AiRuntime::NpcPerceptionDialogIntentReceiveLoopIntegrationOptions receiveOptions;
                              receiveOptions.integrationSource = options.receiveLoopSource;
                              receiveOptions.integrationMode = options.receiveLoopMode;
                              receiveLoopIntegrationProof =
                                  Mmo::AiRuntime::proveNpcPerceptionDialogIntentReceiveLoopIntegration(
                                      clientAckReceiptPreview,
                                      receiveOptions);
                              hasReceiveLoopIntegrationProof = true;
                              if(options.previewTerminalPlan) {
                                Mmo::AiRuntime::NpcPerceptionDialogIntentTerminalPlanOptions terminalOptions;
                                terminalOptions.planningSource = options.terminalPlanSource;
                                terminalOptions.planningMode = options.terminalPlanMode;
                                terminalOptions.maxRetryAttempts = options.terminalPlanMaxRetries;
                                terminalPlan = Mmo::AiRuntime::buildNpcPerceptionDialogIntentTerminalPlan(
                                    receiveLoopIntegrationProof,
                                    terminalOptions);
                                hasTerminalPlan = true;
                                if(options.previewChainGuard) {
                                  Mmo::AiRuntime::NpcPerceptionDialogIntentChainGuardOptions chainOptions;
                                  chainOptions.guardSource = options.chainGuardSource;
                                  chainOptions.guardMode = options.chainGuardMode;
                                  chainGuard = Mmo::AiRuntime::buildNpcPerceptionDialogIntentChainGuard(
                                      terminalPlan,
                                      chainOptions);
                                  hasChainGuard = true;
                                  if(options.reportPreviewChainGuard) {
                                    Mmo::AiRuntime::NpcPerceptionDialogIntentChainReportOptions reportOptions;
                                    reportOptions.reportSource = options.chainReportSource;
                                    reportOptions.reportMode = options.chainReportMode;
                                    chainReport = Mmo::AiRuntime::buildNpcPerceptionDialogIntentChainReport(
                                        chainGuard,
                                        reportOptions);
                                    hasChainReport = true;
                                    if(options.gatePreviewChainReport) {
                                      Mmo::AiRuntime::NpcPerceptionDialogIntentChainGateOptions gateOptions;
                                      gateOptions.gateSource = options.chainGateSource;
                                      gateOptions.gateMode = options.chainGateMode;
                                      chainGate = Mmo::AiRuntime::buildNpcPerceptionDialogIntentChainGate(
                                          chainReport,
                                          gateOptions);
                                      hasChainGate = true;
                                      if(options.packagePreviewChainGate) {
                                        Mmo::AiRuntime::NpcPerceptionDialogIntentChainPackageOptions packageOptions;
                                        packageOptions.packageSource = options.chainPackageSource;
                                        packageOptions.packageMode = options.chainPackageMode;
                                        chainPackage = Mmo::AiRuntime::buildNpcPerceptionDialogIntentChainPackage(
                                            chainGate,
                                            packageOptions);
                                        hasChainPackage = true;
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
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
            hasSendBoundary ? &sendBoundary : nullptr,
            hasSenderAdapterProof ? &senderAdapterProof : nullptr,
            hasEndpointResolutionProof ? &endpointResolutionProof : nullptr,
            hasClientAckContract ? &clientAckContract : nullptr,
            hasClientAckReceiptPreview ? &clientAckReceiptPreview : nullptr,
            hasReceiveLoopIntegrationProof ? &receiveLoopIntegrationProof : nullptr,
            hasTerminalPlan ? &terminalPlan : nullptr,
            hasChainGuard ? &chainGuard : nullptr,
            hasChainReport ? &chainReport : nullptr,
            hasChainGate ? &chainGate : nullptr,
            hasChainPackage ? &chainPackage : nullptr,
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
    jsonRawField(std::cout, "prepare_preview_send_boundary", options.preparePreviewSendBoundary ? "true" : "false");
    jsonRawField(std::cout, "require_preview_send_boundary", options.requirePreviewSendBoundary ? "true" : "false");
    jsonRawField(std::cout, "prove_preview_sender_adapter", options.provePreviewSenderAdapter ? "true" : "false");
    jsonRawField(std::cout, "require_preview_sender_adapter", options.requirePreviewSenderAdapter ? "true" : "false");
    jsonRawField(std::cout, "prove_preview_endpoint_resolution", options.provePreviewEndpointResolution ? "true" : "false");
    jsonRawField(std::cout, "require_preview_endpoint_resolution", options.requirePreviewEndpointResolution ? "true" : "false");
    jsonRawField(std::cout, "preview_client_ack_contract", options.previewClientAckContract ? "true" : "false");
    jsonRawField(std::cout, "require_preview_client_ack_contract", options.requirePreviewClientAckContract ? "true" : "false");
    jsonRawField(std::cout, "preview_client_ack_receipt", options.previewClientAckReceipt ? "true" : "false");
    jsonRawField(std::cout, "require_preview_client_ack_receipt", options.requirePreviewClientAckReceipt ? "true" : "false");
    jsonRawField(std::cout, "preview_receive_loop_integration", options.previewReceiveLoopIntegration ? "true" : "false");
    jsonRawField(std::cout, "require_preview_receive_loop_integration", options.requirePreviewReceiveLoopIntegration ? "true" : "false");
    jsonRawField(std::cout, "preview_terminal_plan", options.previewTerminalPlan ? "true" : "false");
    jsonRawField(std::cout, "require_preview_terminal_plan", options.requirePreviewTerminalPlan ? "true" : "false");
    jsonRawField(std::cout, "preview_chain_guard", options.previewChainGuard ? "true" : "false");
    jsonRawField(std::cout, "require_preview_chain_guard", options.requirePreviewChainGuard ? "true" : "false");
    jsonRawField(std::cout, "report_preview_chain_guard", options.reportPreviewChainGuard ? "true" : "false");
    jsonRawField(std::cout, "require_preview_chain_report", options.requirePreviewChainReport ? "true" : "false");
    jsonRawField(std::cout, "gate_preview_chain_report", options.gatePreviewChainReport ? "true" : "false");
    jsonRawField(std::cout, "require_preview_chain_gate", options.requirePreviewChainGate ? "true" : "false");
    jsonRawField(std::cout, "package_preview_chain_gate", options.packagePreviewChainGate ? "true" : "false");
    jsonRawField(std::cout, "require_preview_chain_package", options.requirePreviewChainPackage ? "true" : "false");
    jsonRawField(std::cout, "mutated_files", options.writePreviewEvidenceJsonl ? "true" : "false");
    jsonField(std::cout, "evidence_source", options.evidenceSource);
    jsonField(std::cout, "evidence_mode", options.evidenceMode);
    jsonField(std::cout, "evidence_jsonl_path", options.evidenceJsonlPath);
    jsonField(std::cout, "fanout_source", options.fanoutSource);
    jsonField(std::cout, "fanout_mode", options.fanoutMode);
    jsonField(std::cout, "send_source", options.sendSource);
    jsonField(std::cout, "send_mode", options.sendMode);
    jsonField(std::cout, "sender_adapter_source", options.senderAdapterSource);
    jsonField(std::cout, "sender_adapter_mode", options.senderAdapterMode);
    jsonField(std::cout, "endpoint_resolution_source", options.endpointResolutionSource);
    jsonField(std::cout, "endpoint_resolution_mode", options.endpointResolutionMode);
    jsonField(std::cout, "client_ack_source", options.clientAckSource);
    jsonField(std::cout, "client_ack_mode", options.clientAckMode);
    jsonField(std::cout, "client_ack_receipt_source", options.clientAckReceiptSource);
    jsonField(std::cout, "client_ack_receipt_mode", options.clientAckReceiptMode);
    jsonField(std::cout, "receive_loop_source", options.receiveLoopSource);
    jsonField(std::cout, "receive_loop_mode", options.receiveLoopMode);
    jsonField(std::cout, "terminal_plan_source", options.terminalPlanSource);
    jsonField(std::cout, "terminal_plan_mode", options.terminalPlanMode);
    jsonField(std::cout, "chain_guard_source", options.chainGuardSource);
    jsonField(std::cout, "chain_guard_mode", options.chainGuardMode);
    jsonField(std::cout, "chain_report_source", options.chainReportSource);
    jsonField(std::cout, "chain_report_mode", options.chainReportMode);
    jsonField(std::cout, "chain_gate_source", options.chainGateSource);
    jsonField(std::cout, "chain_gate_mode", options.chainGateMode);
    jsonField(std::cout, "chain_package_source", options.chainPackageSource);
    jsonField(std::cout, "chain_package_mode", options.chainPackageMode);
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
    jsonRawField(std::cout, "dialog_intent_send_boundary_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_send_boundary_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_send_boundary_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_send_boundary_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_send_boundary_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_send_boundary_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_sender_adapter_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_sender_adapter_route_lookup_executed", "false");
    jsonRawField(std::cout, "dialog_intent_sender_adapter_endpoint_resolved", "false");
    jsonRawField(std::cout, "dialog_intent_sender_adapter_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_sender_adapter_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_sender_adapter_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_sender_adapter_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_sender_adapter_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_route_lookup_executed", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_endpoint_resolver_executed", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_endpoint_resolved", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_endpoint_resolution_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_route_lookup_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_endpoint_resolver_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_endpoint_resolved", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_client_ack_observed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_client_nack_observed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_contract_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_socket_receive_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_client_packet_decoded", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_client_ack_observed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_client_nack_observed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_client_ack_receipt_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_receive_loop_entered", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_socket_receive_executed", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_live_packet_decoded", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_client_ack_observed", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_client_nack_observed", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_receive_loop_integration_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_timer_scheduled", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_timeout_observed", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_dead_letter_written", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_action_marked_applied", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_action_marked_failed", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_socket_receive_executed", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_terminal_plan_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_timer_scheduled", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_socket_receive_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_client_ack_observed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_client_nack_observed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_fanout_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_dialog_ui_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_audio_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_guard_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_report_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_chain_report_sql_migration_generated", "false");
    jsonRawField(std::cout, "dialog_intent_chain_report_server_sql_touched", "false");
    jsonRawField(std::cout, "dialog_intent_chain_report_live_dispatch_enabled", "false");
    jsonRawField(std::cout, "dialog_intent_chain_report_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_report_socket_receive_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_report_mark_applied_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_gate_db_mutated", "false");
    jsonRawField(std::cout, "dialog_intent_chain_gate_sql_migration_generated", "false");
    jsonRawField(std::cout, "dialog_intent_chain_gate_server_sql_touched", "false");
    jsonRawField(std::cout, "dialog_intent_chain_gate_live_dispatch_enabled", "false");
    jsonRawField(std::cout, "dialog_intent_chain_gate_send_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_gate_socket_receive_executed", "false");
    jsonRawField(std::cout, "dialog_intent_chain_gate_mark_applied_executed", "false");
    jsonCountField(std::cout, "max_invalid_rows", options.maxInvalidRows);
    jsonCountField(std::cout, "diagnostic_packet_sequence", options.diagnosticPacketSequence);
    jsonCountField(std::cout, "diagnostic_local_sequence", options.diagnosticLocalSequence);
    jsonCountField(std::cout, "max_diagnostic_message_bytes", options.maxDiagnosticMessageBytes);
    jsonCountField(std::cout, "client_ack_timeout_ms", options.clientAckTimeoutMs);
    jsonCountField(std::cout, "terminal_plan_max_retries", options.terminalPlanMaxRetries);
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
    if(hasSendBoundary) {
      std::cout << "\"dialog_intent_send_boundary\": ";
      jsonDialogIntentSendBoundary(std::cout, sendBoundary, true);
    }
    if(hasSenderAdapterProof) {
      std::cout << "\"dialog_intent_sender_adapter\": ";
      jsonDialogIntentSenderAdapterProof(std::cout, senderAdapterProof, true);
    }
    if(hasEndpointResolutionProof) {
      std::cout << "\"dialog_intent_endpoint_resolution\": ";
      jsonDialogIntentEndpointResolutionProof(std::cout, endpointResolutionProof, true);
    }
    if(hasClientAckContract) {
      std::cout << "\"dialog_intent_client_ack_contract\": ";
      jsonDialogIntentClientAckContract(std::cout, clientAckContract, true);
    }
    if(hasClientAckReceiptPreview) {
      std::cout << "\"dialog_intent_client_ack_receipt_preview\": ";
      jsonDialogIntentClientAckReceiptPreview(std::cout, clientAckReceiptPreview, true);
    }
    if(hasReceiveLoopIntegrationProof) {
      std::cout << "\"dialog_intent_receive_loop_integration\": ";
      jsonDialogIntentReceiveLoopIntegrationProof(std::cout, receiveLoopIntegrationProof, true);
    }
    if(hasTerminalPlan) {
      std::cout << "\"dialog_intent_terminal_plan\": ";
      jsonDialogIntentTerminalPlan(std::cout, terminalPlan, true);
    }
    if(hasChainGuard) {
      std::cout << "\"dialog_intent_chain_guard\": ";
      jsonDialogIntentChainGuard(std::cout, chainGuard, true);
    }
    if(hasChainReport) {
      std::cout << "\"dialog_intent_chain_report\": ";
      jsonDialogIntentChainReport(std::cout, chainReport, true);
    }
    if(hasChainGate) {
      std::cout << "\"dialog_intent_chain_gate\": ";
      jsonDialogIntentChainGate(std::cout, chainGate, true);
    }
    if(hasChainPackage) {
      std::cout << "\"dialog_intent_chain_package\": ";
      jsonDialogIntentChainPackage(std::cout, chainPackage, true);
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
    if(options.requirePreviewSendBoundary && (!hasSendBoundary || !sendBoundary.prepared)) {
      return 11;
    }
    if(options.requirePreviewSenderAdapter && (!hasSenderAdapterProof || !senderAdapterProof.proofed)) {
      return 12;
    }
    if(options.requirePreviewEndpointResolution &&
       (!hasEndpointResolutionProof || !endpointResolutionProof.proofed)) {
      return 13;
    }
    if(options.requirePreviewClientAckContract && (!hasClientAckContract || !clientAckContract.built)) {
      return 14;
    }
    if(options.requirePreviewClientAckReceipt && (!hasClientAckReceiptPreview || !clientAckReceiptPreview.built)) {
      return 15;
    }
    if(options.requirePreviewReceiveLoopIntegration &&
       (!hasReceiveLoopIntegrationProof || !receiveLoopIntegrationProof.proofed)) {
      return 16;
    }
    if(options.requirePreviewTerminalPlan && (!hasTerminalPlan || !terminalPlan.planned)) {
      return 17;
    }
    if(options.requirePreviewChainGuard && (!hasChainGuard || !chainGuard.guarded)) {
      return 18;
    }
    if(options.requirePreviewChainReport && (!hasChainReport || !chainReport.built)) {
      return 19;
    }
    if(options.requirePreviewChainGate && (!hasChainGate || !chainGate.gateAccepted)) {
      return 20;
    }
    if(options.requirePreviewChainPackage && (!hasChainPackage || !chainPackage.packageReady)) {
      return 21;
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
