#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Mmo::Server::Gameplay {

inline constexpr double MaxWorldCoordAbs = 10000000.0;
inline constexpr double DefaultNpcInterestRange = 4500.0;
inline constexpr double DefaultNpcFullTickRange = 2800.0;
inline constexpr double DefaultNpcPerceptionRange = 2200.0;
inline constexpr double DefaultNpcPerceptionVerticalRange = 700.0;
inline constexpr double DefaultNpcFocusCos = 0.30;
inline constexpr double DefaultMeleeWeaponRange = 150.0;
inline constexpr double DefaultUnarmedRange = 90.0;
inline constexpr std::int64_t MinInteractiveState = -1;
inline constexpr std::int64_t MaxInteractiveState = 64;
inline constexpr std::int64_t MaxInteractiveStateCount = 4096;
inline constexpr std::int64_t MaxInteractiveStateMask = 0x7fffffff;
inline constexpr std::int64_t MinNpcFightComboIndex = 0;
inline constexpr std::int64_t MaxNpcFightComboIndex = 32;
inline constexpr std::int64_t MinMoverState = -1024;
inline constexpr std::int64_t MaxMoverState = 1024;
inline constexpr std::int64_t MinMoverFrameIndex = 0;
inline constexpr std::int64_t MaxMoverFrameIndex = 4096;
inline constexpr std::size_t MaxEntityKeyBytes = 512;
inline constexpr std::size_t MaxStateNameBytes = 128;
inline constexpr std::size_t MaxIntentNameBytes = 128;
inline constexpr std::size_t MaxWaypointKeyBytes = 512;

struct Vec3 final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

enum class NpcProcessPolicy : std::uint8_t {
  Sleeping,
  RoutineOnly,
  Awareness,
  Full,
};

enum class NpcNavigationIntent : std::uint8_t {
  None,
  HoldPosition,
  MoveToWaypoint,
  FollowTarget,
  EnterCombat,
  UseInteractive,
};

struct ValidationResult final {
  bool accepted = true;
  bool shouldPersist = true;
  const char* reason = "ok";
};

struct RoutineWindow final {
  std::uint16_t startMinute = 0;
  std::uint16_t endMinute = 0;
};

struct NpcRoutineObservation final {
  std::string_view npcKey;
  std::string_view routineState;
  std::string_view scheduleKey;
  std::string_view currentWaypoint;
  std::string_view targetWaypoint;
};

struct NpcAiObservation final {
  std::string_view npcKey;
  std::string_view aiState;
  std::string_view aiIntent;
  std::string_view targetKey;
  std::string_view perceptionState;
};

struct NpcPathObservation final {
  std::string_view npcKey;
  std::string_view pathState;
  std::string_view routeKey;
  std::string_view currentWaypoint;
  std::string_view nextWaypoint;
  std::string_view targetWaypoint;
  std::optional<Vec3> position;
};

struct NpcFightObservation final {
  std::string_view npcKey;
  std::string_view opponentKey;
  std::string_view fightState;
  std::string_view attackState;
  std::int64_t comboIndex = 0;
};

struct InteractiveUseInput final {
  std::string_view key;
  std::int64_t state = 0;
};

struct InteractiveStateInput final {
  std::string_view key;
  std::int64_t state = 0;
  std::int64_t stateCount = 0;
  std::int64_t stateMask = 0;
  std::string_view lifecycleState;
};

struct TriggerEventInput final {
  std::string_view key;
  std::string_view eventTypeName;
};

struct MoverStateInput final {
  std::string_view key;
  std::int64_t stateBefore = 0;
  std::int64_t stateAfter = 0;
  std::string_view stateAfterName;
  std::int64_t frameIndex = 0;
  std::int64_t targetFrameIndex = 0;
};

struct PerceptionInput final {
  Vec3 observer;
  Vec3 target;
  Vec3 observerForward {0.0, 0.0, 1.0};
  double range = DefaultNpcPerceptionRange;
  double verticalRange = DefaultNpcPerceptionVerticalRange;
  double minForwardDot = DefaultNpcFocusCos;
  bool requiresFocus = true;
};

[[nodiscard]] constexpr std::uint16_t minuteOfDay(std::uint32_t hour,
                                                  std::uint32_t minute) noexcept {
  return static_cast<std::uint16_t>(((hour % 24U) * 60U + (minute % 60U)) % 1440U);
}

[[nodiscard]] constexpr std::uint16_t normalizeMinute(std::uint32_t minute) noexcept {
  return static_cast<std::uint16_t>(minute % 1440U);
}

[[nodiscard]] constexpr bool routineWindowContains(RoutineWindow window,
                                                   std::uint16_t minute) noexcept {
  if(window.startMinute == window.endMinute)
    return true;
  if(window.startMinute < window.endMinute)
    return minute >= window.startMinute && minute < window.endMinute;
  return minute >= window.startMinute || minute < window.endMinute;
}

[[nodiscard]] inline bool finiteCoord(double value) noexcept {
  return std::isfinite(value) && std::abs(value) <= MaxWorldCoordAbs;
}

[[nodiscard]] inline bool finitePosition(Vec3 value) noexcept {
  return finiteCoord(value.x) && finiteCoord(value.y) && finiteCoord(value.z);
}

[[nodiscard]] constexpr double lengthSq2d(double x, double z) noexcept {
  return x * x + z * z;
}

[[nodiscard]] constexpr double distanceSq3d(Vec3 a, Vec3 b) noexcept {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  const double dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] constexpr double distanceSq2d(Vec3 a, Vec3 b) noexcept {
  return lengthSq2d(a.x - b.x, a.z - b.z);
}

[[nodiscard]] inline bool canPerceive(const PerceptionInput& input) noexcept {
  if(!finitePosition(input.observer) || !finitePosition(input.target))
    return false;
  const double dy = std::abs(input.target.y - input.observer.y);
  if(dy > input.verticalRange)
    return false;
  if(distanceSq3d(input.observer, input.target) > input.range * input.range)
    return false;
  if(!input.requiresFocus)
    return true;

  const double dx = input.target.x - input.observer.x;
  const double dz = input.target.z - input.observer.z;
  const double len = std::sqrt(lengthSq2d(dx, dz));
  const double forwardLen = std::sqrt(lengthSq2d(input.observerForward.x, input.observerForward.z));
  if(len <= 0.0001 || forwardLen <= 0.0001)
    return true;
  const double dot = (dx * input.observerForward.x + dz * input.observerForward.z) / (len * forwardLen);
  return dot >= input.minForwardDot;
}

[[nodiscard]] inline bool isInAttackRange(Vec3 attacker,
                                          Vec3 target,
                                          double weaponRange = DefaultMeleeWeaponRange) noexcept {
  if(!finitePosition(attacker) || !finitePosition(target))
    return false;
  const double range = std::max(weaponRange, DefaultUnarmedRange);
  return distanceSq2d(attacker, target) <= range * range;
}

[[nodiscard]] constexpr NpcProcessPolicy chooseNpcProcessPolicy(double nearestPlayerDistance,
                                                                bool routineCritical,
                                                                bool inCombat,
                                                                bool hasPerceptionTarget) noexcept {
  if(inCombat || hasPerceptionTarget)
    return NpcProcessPolicy::Full;
  if(nearestPlayerDistance <= DefaultNpcFullTickRange)
    return NpcProcessPolicy::Full;
  if(nearestPlayerDistance <= DefaultNpcInterestRange)
    return NpcProcessPolicy::Awareness;
  return routineCritical ? NpcProcessPolicy::RoutineOnly : NpcProcessPolicy::Sleeping;
}

[[nodiscard]] inline NpcNavigationIntent inferNavigationIntent(std::string_view routineState,
                                                               std::string_view pathState,
                                                               std::string_view aiState,
                                                               std::string_view fightState,
                                                               std::string_view targetKey) noexcept {
  if(!fightState.empty() && fightState != "none" && fightState != "idle" && fightState != "unknown")
    return NpcNavigationIntent::EnterCombat;
  if(aiState == "fight" || aiState == "combat" || aiState == "attack" || aiState == "flee")
    return NpcNavigationIntent::EnterCombat;
  if(pathState == "moving" || pathState == "walking" || pathState == "routing" || pathState == "following")
    return targetKey.empty() ? NpcNavigationIntent::MoveToWaypoint : NpcNavigationIntent::FollowTarget;
  if(routineState == "using_mob" || routineState == "use_mob" || routineState == "mobsi")
    return NpcNavigationIntent::UseInteractive;
  if(routineState == "idle" || routineState == "waiting" || routineState == "sleeping")
    return NpcNavigationIntent::HoldPosition;
  return NpcNavigationIntent::None;
}

[[nodiscard]] constexpr bool validTextLen(std::string_view text, std::size_t maxLen) noexcept {
  return text.size() <= maxLen;
}

[[nodiscard]] constexpr bool nonEmptyKey(std::string_view key) noexcept {
  return !key.empty() && key.size() <= MaxEntityKeyBytes;
}

[[nodiscard]] inline bool knownLifecycle(std::string_view value) noexcept {
  constexpr std::array<std::string_view, 6> states {
    "active", "dead", "removed", "disabled", "consumed", "archived"
  };
  return std::find(states.begin(), states.end(), value) != states.end();
}

[[nodiscard]] inline ValidationResult validateNpcRoutineObservation(const NpcRoutineObservation& input) noexcept {
  if(!nonEmptyKey(input.npcKey))
    return {true, false, "npc_observation_missing_key"};
  if(!validTextLen(input.routineState, MaxStateNameBytes) ||
     !validTextLen(input.scheduleKey, MaxEntityKeyBytes) ||
     !validTextLen(input.currentWaypoint, MaxWaypointKeyBytes) ||
     !validTextLen(input.targetWaypoint, MaxWaypointKeyBytes))
    return {true, false, "npc_routine_observation_too_large"};
  return {};
}

[[nodiscard]] inline ValidationResult validateNpcAiObservation(const NpcAiObservation& input) noexcept {
  if(!nonEmptyKey(input.npcKey))
    return {true, false, "npc_observation_missing_key"};
  if(!validTextLen(input.aiState, MaxStateNameBytes) ||
     !validTextLen(input.aiIntent, MaxIntentNameBytes) ||
     !validTextLen(input.targetKey, MaxEntityKeyBytes) ||
     !validTextLen(input.perceptionState, MaxEntityKeyBytes))
    return {true, false, "npc_ai_observation_too_large"};
  return {};
}

[[nodiscard]] inline ValidationResult validateNpcPathObservation(const NpcPathObservation& input) noexcept {
  if(!nonEmptyKey(input.npcKey))
    return {true, false, "npc_observation_missing_key"};
  if(!validTextLen(input.pathState, MaxStateNameBytes) ||
     !validTextLen(input.routeKey, MaxEntityKeyBytes) ||
     !validTextLen(input.currentWaypoint, MaxWaypointKeyBytes) ||
     !validTextLen(input.nextWaypoint, MaxWaypointKeyBytes) ||
     !validTextLen(input.targetWaypoint, MaxWaypointKeyBytes))
    return {true, false, "npc_path_observation_too_large"};
  if(input.position && !finitePosition(*input.position))
    return {true, false, "npc_path_position_invalid"};
  return {};
}

[[nodiscard]] inline ValidationResult validateNpcFightObservation(const NpcFightObservation& input) noexcept {
  if(!nonEmptyKey(input.npcKey))
    return {true, false, "npc_observation_missing_key"};
  if(!validTextLen(input.opponentKey, MaxEntityKeyBytes) ||
     !validTextLen(input.fightState, MaxStateNameBytes) ||
     !validTextLen(input.attackState, MaxStateNameBytes))
    return {true, false, "npc_fight_observation_too_large"};
  if(input.comboIndex < MinNpcFightComboIndex || input.comboIndex > MaxNpcFightComboIndex)
    return {true, false, "npc_fight_combo_invalid"};
  return {};
}

[[nodiscard]] inline ValidationResult validateInteractiveUse(const InteractiveUseInput& input) noexcept {
  if(!nonEmptyKey(input.key))
    return {false, false, "interactive_missing_key"};
  if(input.state < MinInteractiveState || input.state > MaxInteractiveState)
    return {false, false, "interactive_state_invalid"};
  return {};
}

[[nodiscard]] inline ValidationResult validateInteractiveState(const InteractiveStateInput& input) noexcept {
  if(!nonEmptyKey(input.key))
    return {false, false, "interactive_missing_key"};
  if(input.state < MinInteractiveState || input.state > MaxInteractiveState)
    return {false, false, "interactive_state_invalid"};
  if(input.stateCount < 0 || input.stateCount > MaxInteractiveStateCount)
    return {false, false, "interactive_state_count_invalid"};
  if(input.stateMask < 0 || input.stateMask > MaxInteractiveStateMask)
    return {false, false, "interactive_state_mask_invalid"};
  if(!knownLifecycle(input.lifecycleState))
    return {false, false, "interactive_lifecycle_invalid"};
  return {};
}

[[nodiscard]] inline ValidationResult validateTriggerEvent(const TriggerEventInput& input) noexcept {
  if(!nonEmptyKey(input.key))
    return {false, false, "trigger_missing_key"};
  if(!validTextLen(input.eventTypeName, MaxStateNameBytes))
    return {false, false, "trigger_event_type_too_large"};
  return {};
}

[[nodiscard]] inline ValidationResult validateMoverState(const MoverStateInput& input) noexcept {
  if(!nonEmptyKey(input.key))
    return {false, false, "mover_missing_key"};
  if(input.stateBefore < MinMoverState || input.stateBefore > MaxMoverState ||
     input.stateAfter < MinMoverState || input.stateAfter > MaxMoverState)
    return {false, false, "mover_state_invalid"};
  if(!validTextLen(input.stateAfterName, MaxStateNameBytes))
    return {false, false, "mover_state_name_too_large"};
  if(input.frameIndex < MinMoverFrameIndex || input.frameIndex > MaxMoverFrameIndex ||
     input.targetFrameIndex < MinMoverFrameIndex || input.targetFrameIndex > MaxMoverFrameIndex)
    return {false, false, "mover_frame_invalid"};
  return {};
}

} // namespace Mmo::Server::Gameplay
