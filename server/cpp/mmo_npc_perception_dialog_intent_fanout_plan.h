#pragma once

#include "mmo_ai_runtime_persistence.h"
#include "mmo_npc_perception_action_dispatcher_boundary.h"
#include "mmo_npc_perception_dialog_intent_diagnostic_encoder.h"
#include "mmo_npc_perception_dialog_intent_diagnostic_packet.h"
#include "mmo_npc_perception_dialog_intent_durable_evidence.h"
#include "mmo_npc_perception_dialog_intent_preview.h"
#include "mmo_npc_perception_effect_descriptor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentFanoutPlanOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_client_fanout_plan.v1";
  std::string fanoutSource = "mmo_manual_action_dispatcher_probe";
  std::string fanoutMode = "plan_only_no_send";
  std::string recipientKind = "target_session";
  bool requireDurableEvidenceWritten = true;
};

struct NpcPerceptionDialogIntentFanoutPlan final {
  bool built = false;
  bool dbMutated = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  std::string status = "not_built";
  std::string contractVersion;
  std::string fanoutSource;
  std::string fanoutMode;
  std::string recipientKind;
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
  std::size_t maxDatagramBytes = 0;
  std::size_t plannedDatagrams = 0;
  bool fitsSingleDatagram = false;
  bool durableEvidenceWritten = false;
  bool diagnosticEncodingSafe = false;

  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentFanoutPlan(
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidence& durableEvidence) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentFanoutPlan buildNpcPerceptionDialogIntentFanoutPlan(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation,
    const NpcPerceptionTypedEffectDescriptor& typedEffect,
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacket& diagnosticPacket,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidence& durableEvidence,
    const NpcPerceptionDialogIntentFanoutPlanOptions& options = {});

[[nodiscard]] std::string dialogIntentFanoutPlanJson(
    const NpcPerceptionDialogIntentFanoutPlan& plan);

} // namespace Mmo::AiRuntime
