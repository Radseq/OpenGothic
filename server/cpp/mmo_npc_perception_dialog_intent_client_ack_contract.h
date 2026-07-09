#pragma once

#include "mmo_npc_perception_dialog_intent_endpoint_resolution.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentClientAckContractOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_client_ack_contract.v1";
  std::string ackSource = "mmo_manual_action_dispatcher_probe";
  std::string ackMode = "preview_only_no_client_ack";
  std::string expectedAckPacketKind = "mmo_client_dialog_intent_ack_v1";
  std::string expectedNackPacketKind = "mmo_client_dialog_intent_nack_v1";
  std::string ackRouteKind = "active_session_udp_endpoint";
  std::uint64_t ackTimeoutMs = 2500;
  bool requireEndpointResolutionProofed = true;
  bool requireNoEndpointLookup = true;
  bool requireNoSend = true;
  bool requireNoAckObserved = true;
  bool requireSingleDatagram = true;
};

struct NpcPerceptionDialogIntentClientAckContract final {
  bool built = false;
  bool dbMutated = false;
  bool routeLookupExecuted = false;
  bool endpointResolverExecuted = false;
  bool endpointResolved = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool clientAckObserved = false;
  bool clientNackObserved = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  bool endpointResolutionProofed = false;
  bool endpointLookupInputReady = false;
  bool hasTargetSession = false;
  bool hasTargetCharacter = false;
  bool hasEncodedPayload = false;
  bool fitsSingleDatagram = false;
  bool ackExpectedIfSent = false;
  bool nackExpectedOnClientReject = false;
  bool ackCorrelationReady = false;
  bool timeoutConfigured = false;
  bool terminalStatusDeferred = true;
  bool wouldWaitForClientAckIfEnabled = false;
  bool wouldMarkAppliedOnlyAfterAck = false;

  std::string status = "not_built";
  std::string contractVersion;
  std::string ackSource;
  std::string ackMode;
  std::string expectedAckPacketKind;
  std::string expectedNackPacketKind;
  std::string ackRouteKind;
  std::string packetKind;

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
  std::string routeLookupKey;
  std::string ackCorrelationKey;
  std::string expectedAckIdempotencyKey;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint64_t ackTimeoutMs = 0;
  std::size_t encodedBytes = 0;
  std::size_t encodedPayloadBytes = 0;
  std::size_t plannedDatagrams = 0;

  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentClientAckContract(
    const NpcPerceptionDialogIntentEndpointResolutionProof& endpointResolutionProof) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentClientAckContract buildNpcPerceptionDialogIntentClientAckContract(
    const NpcPerceptionDialogIntentEndpointResolutionProof& endpointResolutionProof,
    const NpcPerceptionDialogIntentClientAckContractOptions& options = {});

[[nodiscard]] std::string dialogIntentClientAckContractJson(
    const NpcPerceptionDialogIntentClientAckContract& contract);

} // namespace Mmo::AiRuntime
