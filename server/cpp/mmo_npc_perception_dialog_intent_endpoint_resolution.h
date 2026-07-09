#pragma once

#include "mmo_npc_perception_dialog_intent_sender_adapter.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentEndpointResolutionOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_endpoint_resolution.v1";
  std::string resolutionSource = "mmo_manual_action_dispatcher_probe";
  std::string resolutionMode = "proof_only_no_endpoint_lookup";
  std::string endpointResolverKind = "active_session_udp_endpoint_map";
  std::string requiredTransportKind = "asio_udp_server_diagnostic_packet";
  std::string requiredRecipientRouteKind = "active_session_udp_endpoint";
  bool requireSenderAdapterProofed = true;
  bool requireLiveEndpointResolver = true;
  bool requireNoRouteLookup = true;
  bool requireNoEndpointResolution = true;
  bool requireSingleDatagram = true;
};

struct NpcPerceptionDialogIntentEndpointResolutionProof final {
  bool proofed = false;
  bool dbMutated = false;
  bool routeLookupExecuted = false;
  bool endpointResolverExecuted = false;
  bool endpointResolved = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  bool senderAdapterProofed = false;
  bool senderAdapterSafe = false;
  bool requiresLiveEndpointResolver = true;
  bool hasTargetSession = false;
  bool hasTargetCharacter = false;
  bool hasEncodedPayload = false;
  bool fitsSingleDatagram = false;
  bool payloadBytesMatch = false;
  bool sequenceNumbersMatch = false;
  bool transportKindSupported = false;
  bool recipientRouteKindSupported = false;
  bool endpointLookupInputReady = false;
  bool wouldResolveEndpointIfEnabled = false;

  std::string status = "not_proofed";
  std::string contractVersion;
  std::string resolutionSource;
  std::string resolutionMode;
  std::string endpointResolverKind;
  std::string transportKind;
  std::string recipientRouteKind;
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
  std::string endpointAddress;
  std::uint16_t endpointPort = 0;
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::size_t encodedBytes = 0;
  std::size_t encodedPayloadBytes = 0;
  std::size_t maxDatagramBytes = 0;
  std::size_t plannedDatagrams = 0;

  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentEndpointResolutionProof(
    const NpcPerceptionDialogIntentSenderAdapterProof& senderAdapterProof) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentEndpointResolutionProof proveNpcPerceptionDialogIntentEndpointResolution(
    const NpcPerceptionDialogIntentSenderAdapterProof& senderAdapterProof,
    const NpcPerceptionDialogIntentEndpointResolutionOptions& options = {});

[[nodiscard]] std::string dialogIntentEndpointResolutionProofJson(
    const NpcPerceptionDialogIntentEndpointResolutionProof& proof);

} // namespace Mmo::AiRuntime
