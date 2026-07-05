#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Mmo::Server::CombatTimeline {

inline constexpr std::uint32_t ObservedAttackGraceMs = 750;
inline constexpr std::uint32_t ObservedDefenceGraceMs = 450;
inline constexpr std::uint32_t MaxObservedCombatStateMs = 30000;
inline constexpr std::int64_t MaxObservedBodyState = 65535;
inline constexpr std::int64_t MaxObservedWeaponState = 255;
inline constexpr std::int64_t MaxObservedComboIndex = 32;

enum class Phase : std::uint8_t {
  Idle,
  Ready,
  Windup,
  HitFrameCandidate,
  Recovery,
  Defence,
  Down,
  Dead,
};

struct ApplyResult final {
  bool accepted = true;
  const char* reason = "ok";
  bool phaseChanged = false;
  bool hitFrameCandidate = false;
};

struct ObservedFightInput final {
  std::string_view actorKey;
  std::string_view opponentKey;
  std::string_view fightState;
  std::string_view attackState;
  std::string_view weaponState;
  std::string_view animationName;
  std::string_view attackAnimationName;
  std::uint64_t serverTickMs = 0;
  std::uint64_t animationElapsedMs = 0;
  std::uint64_t attackAnimationElapsedMs = 0;
  std::uint64_t animationTotalMs = 0;
  std::uint64_t attackTotalMs = 0;
  std::int64_t comboIndex = 0;
  std::int64_t bodyState = 0;
  std::int64_t weaponStateId = 0;
  bool attackAnim = false;
  bool prehit = false;
  bool down = false;
  bool dead = false;
  bool unconscious = false;
};

struct DamageEvidenceInput final {
  std::string_view attackerKey;
  std::string_view targetKey;
  std::uint64_t serverTickMs = 0;
  bool projectileOrSpell = false;
};

struct DamageEvidence final {
  bool hasAttackerTimeline = false;
  bool plausibleMeleeHit = false;
  const char* reason = "no_attacker_timeline";
};

struct Snapshot final {
  std::string actorKey;
  std::string opponentKey;
  std::string fightState;
  std::string attackState;
  std::string weaponState;
  std::string animationName;
  std::string attackAnimationName;
  Phase phase = Phase::Idle;
  std::uint64_t startedServerTickMs = 0;
  std::uint64_t updatedServerTickMs = 0;
  std::uint64_t lastHitCandidateTickMs = 0;
  std::uint64_t lastDefenceTickMs = 0;
  std::uint64_t animationElapsedMs = 0;
  std::uint64_t attackAnimationElapsedMs = 0;
  std::uint64_t animationTotalMs = 0;
  std::uint64_t attackTotalMs = 0;
  std::int64_t comboIndex = 0;
  std::int64_t bodyState = 0;
  std::int64_t weaponStateId = 0;
  bool attackAnim = false;
  bool prehit = false;
  bool down = false;
  bool dead = false;
  bool unconscious = false;
};

class Registry final {
public:
  [[nodiscard]] ApplyResult applyObservedFight(const ObservedFightInput& input);
  [[nodiscard]] DamageEvidence evaluateDamageEvidence(const DamageEvidenceInput& input) const;
  [[nodiscard]] std::optional<Snapshot> snapshot(std::string_view actorKey) const;
  [[nodiscard]] std::vector<Snapshot> snapshots() const;

  void expire(std::uint64_t nowServerTickMs);
  void clear();

private:
  [[nodiscard]] static Phase classify(const ObservedFightInput& input) noexcept;
  [[nodiscard]] static bool validInput(const ObservedFightInput& input) noexcept;
  [[nodiscard]] static bool timedOut(const Snapshot& snapshot, std::uint64_t nowServerTickMs) noexcept;

  std::unordered_map<std::string, Snapshot> states_;
};

[[nodiscard]] std::string_view phaseName(Phase phase) noexcept;
[[nodiscard]] bool isAttackPhase(Phase phase) noexcept;
[[nodiscard]] bool isTerminalPhase(Phase phase) noexcept;

} // namespace Mmo::Server::CombatTimeline
