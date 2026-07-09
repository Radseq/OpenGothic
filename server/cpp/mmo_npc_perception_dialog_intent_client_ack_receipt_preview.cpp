#include "mmo_npc_perception_dialog_intent_client_ack_receipt_preview.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mmo::AiRuntime {
namespace {

struct ParsedReceipt final {
  bool parsed = false;
  std::string kind;
  std::string correlation;
  std::string idempotencyKey;
  std::string targetSessionUuid;
  std::string targetCharacterUuid;
  std::string result;
  std::string reason;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
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

void addIssue(NpcPerceptionDialogIntentClientAckReceiptPreview& preview, std::string issue) {
  preview.issues.push_back(std::move(issue));
}

[[nodiscard]] bool clientAckContractSafe(
    const NpcPerceptionDialogIntentClientAckContract& contract) noexcept {
  return contract.built && contract.endpointResolutionProofed && contract.endpointLookupInputReady &&
         contract.hasTargetSession && contract.hasTargetCharacter && contract.hasEncodedPayload &&
         contract.fitsSingleDatagram && contract.ackExpectedIfSent && contract.nackExpectedOnClientReject &&
         contract.ackCorrelationReady && contract.timeoutConfigured && contract.wouldWaitForClientAckIfEnabled &&
         contract.wouldMarkAppliedOnlyAfterAck && !contract.dbMutated && !contract.routeLookupExecuted &&
         !contract.endpointResolverExecuted && !contract.endpointResolved && !contract.sendExecuted &&
         !contract.packetFanoutExecuted && !contract.clientAckObserved && !contract.clientNackObserved &&
         !contract.dialogUiExecuted && !contract.audioExecuted && !contract.markAppliedExecuted;
}

[[nodiscard]] std::string makeReceiptPreview(
    std::string_view kind,
    const NpcPerceptionDialogIntentClientAckContract& contract,
    std::string_view result,
    std::string_view reason) {
  std::string out;
  out.reserve(kind.size() + contract.ackCorrelationKey.size() + contract.expectedAckIdempotencyKey.size() + 256);
  out += "kind=";
  out += kind;
  out += ";correlation=";
  out += contract.ackCorrelationKey;
  out += ";idempotency_key=";
  out += contract.expectedAckIdempotencyKey;
  out += ";target_session_uuid=";
  out += contract.targetSessionUuid;
  out += ";target_character_uuid=";
  out += contract.targetCharacterUuid;
  out += ";packet_sequence=";
  out += std::to_string(contract.packetSequence);
  out += ";local_sequence=";
  out += std::to_string(contract.localSequence);
  out += ";result=";
  out += result;
  if(!reason.empty()) {
    out += ";reason=";
    out += reason;
  }
  return out;
}

[[nodiscard]] std::string makeMalformedReceiptPreview(
    const NpcPerceptionDialogIntentClientAckContract& contract) {
  std::string out;
  out.reserve(contract.ackCorrelationKey.size() + 128);
  out += "kind=";
  out += contract.expectedAckPacketKind;
  out += ";correlation=wrong:";
  out += contract.ackCorrelationKey;
  out += ";target_session_uuid=";
  out += contract.targetSessionUuid;
  out += ";packet_sequence=";
  out += std::to_string(contract.packetSequence);
  out += ";result=accepted";
  return out;
}

[[nodiscard]] std::string valueFor(std::string_view receipt, std::string_view key) {
  std::size_t at = 0;
  while(at <= receipt.size()) {
    const auto next = receipt.find(';', at);
    const auto part = receipt.substr(at, next == std::string_view::npos ? receipt.size() - at : next - at);
    const auto eq = part.find('=');
    if(eq != std::string_view::npos && part.substr(0, eq) == key) {
      return std::string(part.substr(eq + 1));
    }
    if(next == std::string_view::npos) {
      break;
    }
    at = next + 1;
  }
  return {};
}

[[nodiscard]] std::uint64_t parseU64OrZero(std::string_view text) noexcept {
  std::uint64_t value = 0;
  for(const unsigned char c : text) {
    if(c < '0' || c > '9') {
      return 0;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if(value > (UINT64_MAX - digit) / 10U) {
      return 0;
    }
    value = value * 10U + digit;
  }
  return value;
}

[[nodiscard]] ParsedReceipt parseReceiptPreview(std::string_view receipt) {
  ParsedReceipt out;
  out.kind = valueFor(receipt, "kind");
  out.correlation = valueFor(receipt, "correlation");
  out.idempotencyKey = valueFor(receipt, "idempotency_key");
  out.targetSessionUuid = valueFor(receipt, "target_session_uuid");
  out.targetCharacterUuid = valueFor(receipt, "target_character_uuid");
  out.result = valueFor(receipt, "result");
  out.reason = valueFor(receipt, "reason");
  out.packetSequence = parseU64OrZero(valueFor(receipt, "packet_sequence"));
  out.localSequence = parseU64OrZero(valueFor(receipt, "local_sequence"));
  out.parsed = !out.kind.empty() && !out.correlation.empty() && !out.idempotencyKey.empty() &&
               !out.targetSessionUuid.empty() && !out.targetCharacterUuid.empty() && !out.result.empty();
  return out;
}

[[nodiscard]] bool validateReceipt(
    const ParsedReceipt& receipt,
    const NpcPerceptionDialogIntentClientAckContract& contract,
    std::string_view expectedKind,
    std::string_view expectedResult) noexcept {
  return receipt.parsed && receipt.kind == expectedKind && receipt.result == expectedResult &&
         receipt.correlation == contract.ackCorrelationKey &&
         receipt.idempotencyKey == contract.expectedAckIdempotencyKey &&
         receipt.targetSessionUuid == contract.targetSessionUuid &&
         receipt.targetCharacterUuid == contract.targetCharacterUuid &&
         receipt.packetSequence == contract.packetSequence && receipt.localSequence == contract.localSequence;
}

} // namespace

bool supportsNpcPerceptionDialogIntentClientAckReceiptPreview(
    const NpcPerceptionDialogIntentClientAckContract& clientAckContract) noexcept {
  return clientAckContractSafe(clientAckContract) && !clientAckContract.expectedAckPacketKind.empty() &&
         !clientAckContract.expectedNackPacketKind.empty() && !clientAckContract.ackCorrelationKey.empty() &&
         !clientAckContract.expectedAckIdempotencyKey.empty();
}

NpcPerceptionDialogIntentClientAckReceiptPreview buildNpcPerceptionDialogIntentClientAckReceiptPreview(
    const NpcPerceptionDialogIntentClientAckContract& clientAckContract,
    const NpcPerceptionDialogIntentClientAckReceiptPreviewOptions& options) {
  NpcPerceptionDialogIntentClientAckReceiptPreview out;
  out.contractVersion = options.contractVersion;
  out.receiptSource = options.receiptSource;
  out.receiptMode = options.receiptMode;
  out.expectedAckPacketKind = clientAckContract.expectedAckPacketKind;
  out.expectedNackPacketKind = clientAckContract.expectedNackPacketKind;
  out.ackResultCode = options.ackResultCode;
  out.nackResultCode = options.nackResultCode;
  out.nackReason = options.nackReason;

  out.actionQueueUuid = clientAckContract.actionQueueUuid;
  out.decisionUuid = clientAckContract.decisionUuid;
  out.actionKind = clientAckContract.actionKind;
  out.worldInstanceUuid = clientAckContract.worldInstanceUuid;
  out.sessionUuid = clientAckContract.sessionUuid;
  out.characterUuid = clientAckContract.characterUuid;
  out.npcEntityKey = clientAckContract.npcEntityKey;
  out.targetKey = clientAckContract.targetKey;
  out.perceptionKind = clientAckContract.perceptionKind;
  out.idempotencyKey = clientAckContract.idempotencyKey;

  out.targetSessionUuid = clientAckContract.targetSessionUuid;
  out.targetCharacterUuid = clientAckContract.targetCharacterUuid;
  out.ackCorrelationKey = clientAckContract.ackCorrelationKey;
  out.expectedAckIdempotencyKey = clientAckContract.expectedAckIdempotencyKey;
  out.packetSequence = clientAckContract.packetSequence;
  out.localSequence = clientAckContract.localSequence;
  out.ackTimeoutMs = clientAckContract.ackTimeoutMs;

  out.dbMutated = false;
  out.socketReceiveExecuted = false;
  out.clientPacketDecoded = false;
  out.clientAckObserved = false;
  out.clientNackObserved = false;
  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;
  out.terminalStatusDeferred = true;

  out.clientAckContractBuilt = clientAckContract.built;
  out.ackCorrelationReady = clientAckContract.ackCorrelationReady;
  out.timeoutConfigured = clientAckContract.timeoutConfigured;
  out.hasExpectedAckKind = !clientAckContract.expectedAckPacketKind.empty();
  out.hasExpectedNackKind = !clientAckContract.expectedNackPacketKind.empty();
  out.targetSessionMatched = !out.targetSessionUuid.empty() && out.targetSessionUuid == clientAckContract.targetSessionUuid;
  out.targetCharacterMatched = !out.targetCharacterUuid.empty() && out.targetCharacterUuid == clientAckContract.targetCharacterUuid;

  out.ackReceiptPreview = makeReceiptPreview(
      clientAckContract.expectedAckPacketKind,
      clientAckContract,
      options.ackResultCode,
      {});
  out.nackReceiptPreview = makeReceiptPreview(
      clientAckContract.expectedNackPacketKind,
      clientAckContract,
      options.nackResultCode,
      options.nackReason);
  out.malformedReceiptPreview = makeMalformedReceiptPreview(clientAckContract);

  const auto ackReceipt = parseReceiptPreview(out.ackReceiptPreview);
  const auto nackReceipt = parseReceiptPreview(out.nackReceiptPreview);
  const auto malformedReceipt = parseReceiptPreview(out.malformedReceiptPreview);
  out.syntheticAckReceiptParsed = ackReceipt.parsed;
  out.syntheticAckReceiptValidated = validateReceipt(
      ackReceipt,
      clientAckContract,
      clientAckContract.expectedAckPacketKind,
      options.ackResultCode);
  out.syntheticNackReceiptParsed = nackReceipt.parsed;
  out.syntheticNackReceiptValidated = validateReceipt(
      nackReceipt,
      clientAckContract,
      clientAckContract.expectedNackPacketKind,
      options.nackResultCode);
  out.syntheticMalformedReceiptRejected = !validateReceipt(
      malformedReceipt,
      clientAckContract,
      clientAckContract.expectedAckPacketKind,
      options.ackResultCode);

  out.parserReady = out.syntheticAckReceiptParsed && out.syntheticNackReceiptParsed && out.syntheticMalformedReceiptRejected;
  out.validatorReady = out.syntheticAckReceiptValidated && out.syntheticNackReceiptValidated &&
                       out.syntheticMalformedReceiptRejected;
  out.wouldMarkAppliedAfterAckIfEnabled = out.validatorReady && clientAckContract.wouldMarkAppliedOnlyAfterAck;
  out.wouldRejectOrRetryAfterNackIfEnabled = out.validatorReady && clientAckContract.nackExpectedOnClientReject;

  if(options.contractVersion.empty()) {
    addIssue(out, "missing_contract_version");
  }
  if(options.receiptSource.empty()) {
    addIssue(out, "missing_receipt_source");
  }
  if(options.receiptMode != "preview_only_no_socket_receive") {
    addIssue(out, "receipt_mode_must_remain_preview_only_no_socket_receive");
  }
  if(options.ackResultCode.empty()) {
    addIssue(out, "missing_ack_result_code");
  }
  if(options.nackResultCode.empty()) {
    addIssue(out, "missing_nack_result_code");
  }
  if(options.requireClientAckContractBuilt && !clientAckContract.built) {
    addIssue(out, "client_ack_contract_not_built");
  }
  if(!clientAckContractSafe(clientAckContract)) {
    addIssue(out, "client_ack_contract_not_safe_for_receipt_preview");
  }
  if(!out.ackCorrelationReady) {
    addIssue(out, "ack_correlation_not_ready");
  }
  if(!out.timeoutConfigured) {
    addIssue(out, "ack_timeout_not_configured");
  }
  if(!out.hasExpectedAckKind) {
    addIssue(out, "missing_expected_ack_packet_kind");
  }
  if(!out.hasExpectedNackKind) {
    addIssue(out, "missing_expected_nack_packet_kind");
  }
  if(!out.targetSessionMatched) {
    addIssue(out, "target_session_not_matched");
  }
  if(!out.targetCharacterMatched) {
    addIssue(out, "target_character_not_matched");
  }
  if(!out.parserReady) {
    addIssue(out, "synthetic_receipt_parser_not_ready");
  }
  if(!out.validatorReady) {
    addIssue(out, "synthetic_receipt_validator_not_ready");
  }
  if(options.requireNoSocketReceive && out.socketReceiveExecuted) {
    addIssue(out, "socket_receive_executed");
  }
  if(options.requireNoClientReceiptObserved && (out.clientAckObserved || out.clientNackObserved)) {
    addIssue(out, "client_ack_or_nack_observed");
  }
  if(options.requireNoSend && (out.sendExecuted || out.packetFanoutExecuted)) {
    addIssue(out, "send_or_fanout_executed");
  }
  if(options.requireNoDbMutation && out.dbMutated) {
    addIssue(out, "db_mutated");
  }
  if(clientAckContract.dbMutated || clientAckContract.sendExecuted || clientAckContract.packetFanoutExecuted ||
     clientAckContract.clientAckObserved || clientAckContract.clientNackObserved || clientAckContract.dialogUiExecuted ||
     clientAckContract.audioExecuted || clientAckContract.markAppliedExecuted) {
    addIssue(out, "upstream_contract_has_live_side_effect");
  }

  out.built = out.issues.empty();
  out.status = out.built ? "client_ack_receipt_parser_previewed_no_socket_receive" :
                           "client_ack_receipt_parser_rejected";
  return out;
}

std::string dialogIntentClientAckReceiptPreviewJson(
    const NpcPerceptionDialogIntentClientAckReceiptPreview& preview) {
  std::string out;
  out.reserve(3072 + preview.issues.size() * 48 + preview.ackReceiptPreview.size() + preview.nackReceiptPreview.size());
  out.push_back('{');

  appendJsonBoolField(out, "built", preview.built);
  appendComma(out);
  appendJsonField(out, "status", preview.status);
  appendComma(out);
  appendJsonField(out, "contract_version", preview.contractVersion);
  appendComma(out);
  appendJsonField(out, "receipt_source", preview.receiptSource);
  appendComma(out);
  appendJsonField(out, "receipt_mode", preview.receiptMode);
  appendComma(out);
  appendJsonField(out, "expected_ack_packet_kind", preview.expectedAckPacketKind);
  appendComma(out);
  appendJsonField(out, "expected_nack_packet_kind", preview.expectedNackPacketKind);
  appendComma(out);
  appendJsonField(out, "ack_result_code", preview.ackResultCode);
  appendComma(out);
  appendJsonField(out, "nack_result_code", preview.nackResultCode);
  appendComma(out);
  appendJsonField(out, "nack_reason", preview.nackReason);
  appendComma(out);
  appendJsonField(out, "action_queue_uuid", preview.actionQueueUuid);
  appendComma(out);
  appendJsonField(out, "decision_uuid", preview.decisionUuid);
  appendComma(out);
  appendJsonField(out, "action_kind", preview.actionKind);
  appendComma(out);
  appendJsonField(out, "world_instance_uuid", preview.worldInstanceUuid);
  appendComma(out);
  appendJsonField(out, "session_uuid", preview.sessionUuid);
  appendComma(out);
  appendJsonField(out, "character_uuid", preview.characterUuid);
  appendComma(out);
  appendJsonField(out, "npc_entity_key", preview.npcEntityKey);
  appendComma(out);
  appendJsonField(out, "target_key", preview.targetKey);
  appendComma(out);
  appendJsonField(out, "perception_kind", preview.perceptionKind);
  appendComma(out);
  appendJsonField(out, "idempotency_key", preview.idempotencyKey);
  appendComma(out);
  appendJsonField(out, "target_session_uuid", preview.targetSessionUuid);
  appendComma(out);
  appendJsonField(out, "target_character_uuid", preview.targetCharacterUuid);
  appendComma(out);
  appendJsonField(out, "ack_correlation_key", preview.ackCorrelationKey);
  appendComma(out);
  appendJsonField(out, "expected_ack_idempotency_key", preview.expectedAckIdempotencyKey);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", preview.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", preview.localSequence);
  appendComma(out);
  appendJsonCountField(out, "ack_timeout_ms", preview.ackTimeoutMs);
  appendComma(out);
  appendJsonBoolField(out, "client_ack_contract_built", preview.clientAckContractBuilt);
  appendComma(out);
  appendJsonBoolField(out, "ack_correlation_ready", preview.ackCorrelationReady);
  appendComma(out);
  appendJsonBoolField(out, "timeout_configured", preview.timeoutConfigured);
  appendComma(out);
  appendJsonBoolField(out, "has_expected_ack_kind", preview.hasExpectedAckKind);
  appendComma(out);
  appendJsonBoolField(out, "has_expected_nack_kind", preview.hasExpectedNackKind);
  appendComma(out);
  appendJsonBoolField(out, "target_session_matched", preview.targetSessionMatched);
  appendComma(out);
  appendJsonBoolField(out, "target_character_matched", preview.targetCharacterMatched);
  appendComma(out);
  appendJsonBoolField(out, "synthetic_ack_receipt_parsed", preview.syntheticAckReceiptParsed);
  appendComma(out);
  appendJsonBoolField(out, "synthetic_ack_receipt_validated", preview.syntheticAckReceiptValidated);
  appendComma(out);
  appendJsonBoolField(out, "synthetic_nack_receipt_parsed", preview.syntheticNackReceiptParsed);
  appendComma(out);
  appendJsonBoolField(out, "synthetic_nack_receipt_validated", preview.syntheticNackReceiptValidated);
  appendComma(out);
  appendJsonBoolField(out, "synthetic_malformed_receipt_rejected", preview.syntheticMalformedReceiptRejected);
  appendComma(out);
  appendJsonBoolField(out, "parser_ready", preview.parserReady);
  appendComma(out);
  appendJsonBoolField(out, "validator_ready", preview.validatorReady);
  appendComma(out);
  appendJsonBoolField(out, "terminal_status_deferred", preview.terminalStatusDeferred);
  appendComma(out);
  appendJsonBoolField(out, "would_mark_applied_after_ack_if_enabled", preview.wouldMarkAppliedAfterAckIfEnabled);
  appendComma(out);
  appendJsonBoolField(out, "would_reject_or_retry_after_nack_if_enabled", preview.wouldRejectOrRetryAfterNackIfEnabled);
  appendComma(out);
  appendJsonField(out, "ack_terminal_status", preview.ackTerminalStatus);
  appendComma(out);
  appendJsonField(out, "nack_terminal_status", preview.nackTerminalStatus);
  appendComma(out);
  appendJsonField(out, "ack_receipt_preview", preview.ackReceiptPreview);
  appendComma(out);
  appendJsonField(out, "nack_receipt_preview", preview.nackReceiptPreview);
  appendComma(out);
  appendJsonField(out, "malformed_receipt_preview", preview.malformedReceiptPreview);
  appendComma(out);
  appendJsonBoolField(out, "db_mutated", preview.dbMutated);
  appendComma(out);
  appendJsonBoolField(out, "socket_receive_executed", preview.socketReceiveExecuted);
  appendComma(out);
  appendJsonBoolField(out, "client_packet_decoded", preview.clientPacketDecoded);
  appendComma(out);
  appendJsonBoolField(out, "client_ack_observed", preview.clientAckObserved);
  appendComma(out);
  appendJsonBoolField(out, "client_nack_observed", preview.clientNackObserved);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", preview.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", preview.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", preview.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", preview.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", preview.markAppliedExecuted);
  appendComma(out);
  appendJsonCountField(out, "issue_count", preview.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  bool first = true;
  for(const auto& issue : preview.issues) {
    if(!first) {
      out.push_back(',');
    }
    first = false;
    out += jsonEscape(issue);
  }
  out += "]}";
  return out;
}

} // namespace Mmo::AiRuntime
