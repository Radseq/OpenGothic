#pragma once

#include "mmo_npc_perception_dialog_intent_diagnostic_encoder.h"
#include "mmo_npc_perception_dialog_intent_fanout_plan.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentSendBoundaryOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_send_boundary.v1";
  std::string sendSource = "mmo_manual_action_dispatcher_probe";
  std::string sendMode = "prepare_only_no_send";
  std::string transportKind = "udp_server_diagnostic_packet";
  bool requireFanoutPlanBuilt = true;
  bool requireSingleDatagram = true;
};

struct NpcPerceptionDialogIntentSendBoundary final {
  bool prepared = false;
  bool dbMutated = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  bool sendBlockedByDesign = true;
  bool wouldSendIfEnabled = false;
  bool fanoutPlanBuilt = false;
  bool diagnosticEncodingSafe = false;
  bool hasEncodedPayload = false;
  bool hasTargetSession = false;
  bool fitsSingleDatagram = false;

  std::string status = "not_prepared";
  std::string contractVersion;
  std::string sendSource;
  std::string sendMode;
  std::string transportKind;
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

[[nodiscard]] bool supportsNpcPerceptionDialogIntentSendBoundary(
    const NpcPerceptionDialogIntentFanoutPlan& fanoutPlan,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentSendBoundary prepareNpcPerceptionDialogIntentSendBoundary(
    const NpcPerceptionDialogIntentFanoutPlan& fanoutPlan,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentSendBoundaryOptions& options = {});

[[nodiscard]] std::string dialogIntentSendBoundaryJson(
    const NpcPerceptionDialogIntentSendBoundary& boundary);

} // namespace Mmo::AiRuntime
