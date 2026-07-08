#pragma once

#include "mmo_npc_perception_action_dispatcher_boundary.h"
#include "mmo_npc_perception_dialog_intent_diagnostic_encoder.h"
#include "mmo_npc_perception_dialog_intent_diagnostic_packet.h"
#include "mmo_npc_perception_dialog_intent_preview.h"
#include "mmo_npc_perception_effect_descriptor.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentDurableEvidenceOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_preview_evidence.v1";
  std::string evidenceKind = "dialog_intent_diagnostic_encoding_preview";
  std::string evidenceSource = "mmo_manual_action_dispatcher_probe";
  std::string evidenceMode = "jsonl_only_no_dispatch";
  std::filesystem::path jsonlPath;
  bool allowFileWrite = false;
};

struct NpcPerceptionDialogIntentDurableEvidence final {
  bool built = false;
  bool written = false;
  bool dbMutated = false;
  bool liveDispatchExecuted = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  std::string status = "not_built";
  std::string contractVersion;
  std::string evidenceKind;
  std::string evidenceSource;
  std::string evidenceMode;
  std::string jsonlPath;

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

  std::string typedEffectStatus;
  std::string previewStatus;
  std::string diagnosticPacketStatus;
  std::string diagnosticEncodingStatus;
  std::size_t encodedBytes = 0;
  bool fitsDatagram = false;
  bool decodedRoundTrip = false;
  bool decodedFieldsMatch = false;

  std::string jsonRecord;
  std::size_t jsonBytes = 0;
  std::size_t writtenBytes = 0;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentDurableEvidence(
    const NpcPerceptionDialogIntentDiagnosticEncoding& encoding) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentDurableEvidence buildNpcPerceptionDialogIntentDurableEvidence(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation,
    const NpcPerceptionTypedEffectDescriptor& typedEffect,
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacket& diagnosticPacket,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidenceOptions& options = {});

[[nodiscard]] NpcPerceptionDialogIntentDurableEvidence writeNpcPerceptionDialogIntentDurableEvidenceJsonl(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation,
    const NpcPerceptionTypedEffectDescriptor& typedEffect,
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacket& diagnosticPacket,
    const NpcPerceptionDialogIntentDiagnosticEncoding& diagnosticEncoding,
    const NpcPerceptionDialogIntentDurableEvidenceOptions& options);

[[nodiscard]] std::string dialogIntentDurableEvidenceJson(
    const NpcPerceptionDialogIntentDurableEvidence& evidence);

} // namespace Mmo::AiRuntime
