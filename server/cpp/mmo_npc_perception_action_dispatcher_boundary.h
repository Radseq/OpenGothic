#pragma once

#include "mmo_ai_runtime_persistence.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionActionKindContract final {
  std::string actionKind;
  std::string effectKind;
  bool known = false;
  bool safeNoop = false;
  bool liveDispatchImplemented = false;
  bool requiresTargetKey = true;
  bool requiresNpcEntityKey = true;
  bool requiresPerceptionKind = true;
  bool requiresDecisionUuid = true;
};

struct NpcPerceptionActionDispatchValidationOptions final {
  bool requireKnownActionKind = true;
  bool requireWorldInstanceUuid = true;
  bool requireActionQueueUuid = true;
  bool requireDecisionUuid = true;
  bool requireTargetKey = true;
  bool requireIdempotencyKey = true;
  bool requirePayloadJsonObject = true;
  bool requirePayloadDecisionUuid = true;
  bool requirePayloadNpcEntityKey = true;
  bool requirePayloadPerceptionKind = true;
};

struct NpcPerceptionActionDispatchValidation final {
  bool accepted = false;
  bool liveDispatchAllowed = false;
  std::string status = "invalid";
  std::string actionKind;
  std::string effectKind;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] bool isKnownNpcPerceptionActionKind(std::string_view actionKind) noexcept;
[[nodiscard]] NpcPerceptionActionKindContract npcPerceptionActionKindContract(std::string_view actionKind);
[[nodiscard]] bool looksLikeJsonObject(std::string_view payload) noexcept;
[[nodiscard]] bool jsonObjectHasKey(std::string_view payload, std::string_view key) noexcept;

[[nodiscard]] NpcPerceptionActionDispatchValidation validateNpcPerceptionActionDispatchContract(
    const ClaimedNpcPerceptionAction& action,
    const NpcPerceptionActionDispatchValidationOptions& options = {});

[[nodiscard]] std::string validationIssuesJson(const NpcPerceptionActionDispatchValidation& validation);

} // namespace Mmo::AiRuntime
