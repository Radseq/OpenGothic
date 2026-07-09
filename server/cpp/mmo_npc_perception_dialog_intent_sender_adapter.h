#pragma once

#include "mmo_npc_perception_dialog_intent_diagnostic_encoder.h"
#include "mmo_npc_perception_dialog_intent_send_boundary.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentSenderAdapterOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_sender_adapter.v1";
  std::string adapterSource = "mmo_manual_action_dispatcher_probe";
  std::string adapterMode = "proof_only_no_send";
  std::string transportKind = "asio_udp_server_diagnostic_packet";
  std::string recipientRouteKind = "active_session_udp_endpoint";
  bool requireSendBoundaryPrepared = true;
  bool requireBlockedByDesign = true;
  bool requireSingleDatagram = true;
};

struct NpcPerceptionDialogIntentSenderAdapterProof final {
  bool proofed = false;
  bool dbMutated = false;
  bool routeLookupExecuted = false;
  bool endpointResolved = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  bool sendBoundaryPrepared = false;
  bool sendBlockedByDesign = true;
  bool wouldSendIfEnabled = false;
  bool diagnosticEncodingSafe = false;
  bool hasEncodedPayload = false;
  bool hasTargetSession = false;
  bool hasTargetCharacter = false;
  bool fitsSingleDatagram = false;
  bool payloadBytesMatch = false;
  bool sequenceNumbersMatch = false;
  bool requiresLiveEndpointResolver = true;

  std::string status = "not_proofed";
  std::string contractVersion;
  std::string adapterSource;
  std::string adapterMode;
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
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::size_t encodedBytes = 0;
  std::size_t encodedPayloadBytes = 0;
  std::size_t maxDatagramBytes = 0;
  std::size_t plannedDatagrams = 0;

  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentSenderAdapterProof(
    const NpcPerceptionDialogIntentSendBoundary& sendBoundary,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentSenderAdapterProof proveNpcPerceptionDialogIntentSenderAdapter(
    const NpcPerceptionDialogIntentSendBoundary& sendBoundary,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentSenderAdapterOptions& options = {});

[[nodiscard]] std::string dialogIntentSenderAdapterProofJson(
    const NpcPerceptionDialogIntentSenderAdapterProof& proof);

} // namespace Mmo::AiRuntime
