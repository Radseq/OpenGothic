#pragma once

#include "mmo_npc_perception_effect_descriptor.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentPreviewOptions final {
  std::string previewSource = "mmo_manual_action_dispatcher_probe";
  std::string previewMode = "log_only";
};

struct NpcPerceptionDialogIntentPreview final {
  bool previewed = false;
  bool liveDispatchExecuted = false;
  bool markAppliedExecuted = false;
  bool packetFanoutExecuted = false;
  bool requiresDialogUi = false;
  bool requiresAudio = false;
  bool requiresNpcTurn = false;
  bool requiresNpcMovement = false;
  bool requiresCombat = false;

  std::string status = "not_previewed";
  std::string previewSource;
  std::string previewMode;
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
  std::string logLine;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool supportsNpcPerceptionDialogIntentPreview(
    const NpcPerceptionTypedEffectDescriptor& descriptor) noexcept;

[[nodiscard]] NpcPerceptionDialogIntentPreview previewNpcPerceptionDialogIntent(
    const NpcPerceptionTypedEffectDescriptor& descriptor,
    const NpcPerceptionDialogIntentPreviewOptions& options = {});

[[nodiscard]] std::string dialogIntentPreviewJson(const NpcPerceptionDialogIntentPreview& preview);

} // namespace Mmo::AiRuntime
