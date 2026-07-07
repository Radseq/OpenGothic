#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mmo_server_combat_timeline_authority.h"
#include "mmo_server_fight_move_model.h"

namespace Mmo::Server::FightIntent {

inline constexpr double NarrowAttackFocusCos5 = 0.99619469809174553230;
inline constexpr std::uint64_t ProposedIntentTimeoutMs = 1500;

struct IntentInput final {
  std::string_view actorKey;
  std::string_view targetKey;
  FightMove::Action action = FightMove::Action::None;
  std::uint64_t serverTickMs = 0;
};

struct Decision final {
  bool hasTimeline = false;
  bool accepted = true;
  bool softOnly = true;
  const char* reason = "ok";
};

enum class IntentState : std::uint8_t {
  Observed,
  Proposed,
  Accepted,
  Rejected,
};

struct IntentEvent final {
  std::string_view actorKey;
  std::string_view targetKey;
  std::string_view actionName;
  IntentState state = IntentState::Observed;
  FightMove::Action action = FightMove::Action::None;
  std::uint64_t serverTickMs = 0;
};

struct CorrelationResult final {
  bool accepted = true;
  const char* reason = "ok";
  bool matchedProposal = false;
  bool storedProposal = false;
};

struct PendingIntent final {
  std::string actorKey;
  std::string targetKey;
  std::string actionName;
  FightMove::Action action = FightMove::Action::None;
  std::uint64_t proposedTickMs = 0;
};

class Registry final {
public:
  [[nodiscard]] CorrelationResult apply(const IntentEvent& event);
  [[nodiscard]] std::optional<PendingIntent> pending(std::string_view actorKey) const;
  [[nodiscard]] std::vector<PendingIntent> expire(std::uint64_t nowServerTickMs);
  void clear();

private:
  std::unordered_map<std::string, PendingIntent> pending_;
};

[[nodiscard]] IntentState intentStateFromName(std::string_view name) noexcept;
[[nodiscard]] std::string_view intentStateName(IntentState state) noexcept;
[[nodiscard]] FightMove::Action observedAction(std::string_view attackState,
                                               std::int64_t bodyState,
                                               bool attackAnim,
                                               bool prehit) noexcept;
[[nodiscard]] Decision evaluate(const IntentInput& input,
                                const CombatTimeline::Registry& timeline) noexcept;

} // namespace Mmo::Server::FightIntent
