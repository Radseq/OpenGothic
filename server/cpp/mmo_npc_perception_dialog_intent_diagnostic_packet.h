#pragma once

#include "mmo_npc_perception_dialog_intent_preview.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentDiagnosticPacketOptions final {
  std::string contractVersion = "mmo.ai.dialog_intent_preview_diagnostic.v1";
  std::string packetKind = "server_diagnostic";
  std::string diagnosticAction = "npc_perception_dialog_intent_preview";
  std::string diagnosticReason = "ai_dialog_intent_preview";
  std::string diagnosticSource = "mmo_manual_action_dispatcher_probe";
  std::uint8_t severity = 1;
};

struct NpcPerceptionDialogIntentDiagnosticPacket final {
  bool built = false;
  bool liveDispatchExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  std::uint8_t severity = 1;
  std::string status = "not_built";
  std::string contractVersion;
  std::string packetKind;
  std::string diagnosticAction;
  std::string diagnosticReason;
  std::string diagnosticSource;
  std::string messageText;

  std::string previewKind;
  std::string effectFamily;
  std::string effectKind;
  std::string intentKind;
  std::string actionKind;
  std::string actionQueueUuid;
  std::string decisionUuid;
  std::string worldInstanceUuid;
  std::string sessionUuid;
  std::string characterUuid;
  std::string npcEntityKey;
  std::string targetKey;
  std::string perceptionKind;
  std::string idempotencyKey;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentDiagnosticPacket(
    const NpcPerceptionDialogIntentPreview& preview) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentDiagnosticPacket buildNpcPerceptionDialogIntentDiagnosticPacket(
    const NpcPerceptionDialogIntentPreview& preview,
    const NpcPerceptionDialogIntentDiagnosticPacketOptions& options = {});

[[nodiscard]] std::string dialogIntentDiagnosticPacketJson(
    const NpcPerceptionDialogIntentDiagnosticPacket& packet);

} // namespace Mmo::AiRuntime
