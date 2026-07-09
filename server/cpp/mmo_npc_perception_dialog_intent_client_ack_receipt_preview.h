#pragma once

#include "mmo_npc_perception_dialog_intent_client_ack_contract.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentClientAckReceiptPreviewOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_client_ack_receipt_preview.v1";
  std::string receiptSource = "mmo_manual_action_dispatcher_probe";
  std::string receiptMode = "preview_only_no_socket_receive";
  std::string ackResultCode = "accepted";
  std::string nackResultCode = "rejected";
  std::string nackReason = "client_rejected_dialog_intent_preview";
  bool requireClientAckContractBuilt = true;
  bool requireNoSocketReceive = true;
  bool requireNoClientReceiptObserved = true;
  bool requireNoSend = true;
  bool requireNoDbMutation = true;
};

struct NpcPerceptionDialogIntentClientAckReceiptPreview final {
  bool built = false;
  bool dbMutated = false;
  bool socketReceiveExecuted = false;
  bool clientPacketDecoded = false;
  bool clientAckObserved = false;
  bool clientNackObserved = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  bool clientAckContractBuilt = false;
  bool ackCorrelationReady = false;
  bool timeoutConfigured = false;
  bool hasExpectedAckKind = false;
  bool hasExpectedNackKind = false;
  bool targetSessionMatched = false;
  bool targetCharacterMatched = false;
  bool syntheticAckReceiptParsed = false;
  bool syntheticAckReceiptValidated = false;
  bool syntheticNackReceiptParsed = false;
  bool syntheticNackReceiptValidated = false;
  bool syntheticMalformedReceiptRejected = false;
  bool parserReady = false;
  bool validatorReady = false;
  bool terminalStatusDeferred = true;
  bool wouldMarkAppliedAfterAckIfEnabled = false;
  bool wouldRejectOrRetryAfterNackIfEnabled = false;

  std::string status = "not_built";
  std::string contractVersion;
  std::string receiptSource;
  std::string receiptMode;
  std::string expectedAckPacketKind;
  std::string expectedNackPacketKind;
  std::string ackResultCode;
  std::string nackResultCode;
  std::string nackReason;

  std::string actionQueueUuid;
  std::string decisionUuid;
  std::string actionKind;
  std::string worldInstanceUuid;
  std::string sessionUuid;
  std::string characterUuid;
  std::string npcEntityKey;
  std::string targetKey;
  std::string perceptionKind;
  std::string idempotencyKey;

  std::string targetSessionUuid;
  std::string targetCharacterUuid;
  std::string ackCorrelationKey;
  std::string expectedAckIdempotencyKey;
  std::string ackReceiptPreview;
  std::string nackReceiptPreview;
  std::string malformedReceiptPreview;
  std::string ackTerminalStatus = "client_ack_validated_mark_applied_disabled";
  std::string nackTerminalStatus = "client_nack_validated_apply_disabled";

  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t ackTimeoutMs = 0;

  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentClientAckReceiptPreview(
    const NpcPerceptionDialogIntentClientAckContract& clientAckContract) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentClientAckReceiptPreview buildNpcPerceptionDialogIntentClientAckReceiptPreview(
    const NpcPerceptionDialogIntentClientAckContract& clientAckContract,
    const NpcPerceptionDialogIntentClientAckReceiptPreviewOptions& options = {});

[[nodiscard]] std::string dialogIntentClientAckReceiptPreviewJson(
    const NpcPerceptionDialogIntentClientAckReceiptPreview& preview);

} // namespace Mmo::AiRuntime
