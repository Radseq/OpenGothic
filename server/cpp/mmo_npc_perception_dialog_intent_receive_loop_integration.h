#pragma once

#include "mmo_npc_perception_dialog_intent_client_ack_receipt_preview.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentReceiveLoopIntegrationOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_receive_loop_integration.v1";
  std::string integrationSource = "mmo_manual_action_dispatcher_probe";
  std::string integrationMode = "proof_only_no_socket_receive";
  bool requireReceiptPreviewBuilt = true;
  bool requireParserReady = true;
  bool requireValidatorReady = true;
  bool requireNoSocketReceive = true;
  bool requireNoClientPacketDecoded = true;
  bool requireNoClientReceiptObserved = true;
  bool requireNoSend = true;
  bool requireNoDbMutation = true;
};

struct NpcPerceptionDialogIntentReceiveLoopIntegrationProof final {
  bool proofed = false;
  bool dbMutated = false;
  bool receiveLoopEntered = false;
  bool socketReceiveExecuted = false;
  bool livePacketDecoded = false;
  bool clientAckObserved = false;
  bool clientNackObserved = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  bool receiptPreviewBuilt = false;
  bool parserReady = false;
  bool validatorReady = false;
  bool syntheticAckReceiptValidated = false;
  bool syntheticNackReceiptValidated = false;
  bool syntheticMalformedReceiptRejected = false;
  bool ackCorrelationReady = false;
  bool timeoutConfigured = false;
  bool targetSessionReady = false;
  bool targetCharacterReady = false;
  bool expectedPacketKindsRegistered = false;
  bool ackRouteShapeReady = false;
  bool nackRouteShapeReady = false;
  bool malformedRouteRejected = false;
  bool pendingAckSlotShapeReady = false;
  bool terminalStatusDeferred = true;
  bool wouldEnterReceiveLoopIfEnabled = false;
  bool wouldRouteAckToReceiptParserIfEnabled = false;
  bool wouldRouteNackToReceiptParserIfEnabled = false;
  bool wouldRejectMalformedReceiptIfEnabled = false;
  bool wouldMarkAppliedAfterAckIfEnabled = false;
  bool wouldRejectOrRetryAfterNackIfEnabled = false;

  std::string status = "not_proofed";
  std::string contractVersion;
  std::string integrationSource;
  std::string integrationMode;
  std::string expectedAckPacketKind;
  std::string expectedNackPacketKind;
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
  std::string receiveRouteKey;
  std::string ackRouteDescription = "future_receive_loop_ack_to_step247_parser";
  std::string nackRouteDescription = "future_receive_loop_nack_to_step247_parser";
  std::string malformedRouteDescription = "future_receive_loop_malformed_rejected_before_apply";

  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t ackTimeoutMs = 0;

  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentReceiveLoopIntegrationProof(
    const NpcPerceptionDialogIntentClientAckReceiptPreview& receiptPreview) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentReceiveLoopIntegrationProof
proveNpcPerceptionDialogIntentReceiveLoopIntegration(
    const NpcPerceptionDialogIntentClientAckReceiptPreview& receiptPreview,
    const NpcPerceptionDialogIntentReceiveLoopIntegrationOptions& options = {});

[[nodiscard]] std::string dialogIntentReceiveLoopIntegrationProofJson(
    const NpcPerceptionDialogIntentReceiveLoopIntegrationProof& proof);

} // namespace Mmo::AiRuntime
