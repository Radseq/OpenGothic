#pragma once

#include "mmo_ai_runtime_persistence.h"
#include "mmo_npc_perception_action_dispatcher_boundary.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionTypedEffectDescriptor final {
  bool described = false;
  bool liveDispatchImplemented = false;
  bool liveDispatchAllowed = false;
  bool requiresDialogUi = false;
  bool requiresAudio = false;
  bool requiresNpcTurn = false;
  bool requiresNpcMovement = false;
  bool requiresCombat = false;

  std::string status = "not_described";
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

[[nodiscard]] bool supportsNpcPerceptionTypedEffectDescriptor(std::string_view actionKind) noexcept;

[[nodiscard]] NpcPerceptionTypedEffectDescriptor describeNpcPerceptionTypedEffect(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidation& validation);

[[nodiscard]] std::string typedEffectDescriptorJson(const NpcPerceptionTypedEffectDescriptor& descriptor);

} // namespace Mmo::AiRuntime
