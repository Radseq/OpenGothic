#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Mmo::Server::FightMove {

inline constexpr std::size_t MaxExpandedActions = 6;

enum class WeaponMode : std::uint8_t {
  Unknown,
  NoWeapon,
  Fist,
  OneHanded,
  TwoHanded,
  Bow,
  Crossbow,
  Mage,
};

enum class ScriptMove : std::uint8_t {
  Nop,
  Turn,
  Run,
  RunBack,
  JumpBack,
  Strafe,
  Attack,
  AttackSide,
  AttackFront,
  AttackTriple,
  AttackWhirl,
  AttackMaster,
  TurnToHit,
  Parry,
  StandUp,
  Wait,
  WaitExt,
  WaitLonger,
};

enum class Action : std::uint8_t {
  None,
  Move,
  JumpBack,
  Attack,
  AttackLeft,
  AttackRight,
  StrafeLeft,
  StrafeRight,
  StrafeEnd,
  Block,
  ShootRanged,
  CastSpell,
  Wait,
  WaitLong,
  TurnToHit,
  Turn,
};

enum class TableChoice : std::uint8_t {
  None,
  EnemyPrehit,
  EnemyStormPrehit,
  MyWStrafe,
  MyWRunTo,
  MyWFocus,
  MyWNoFocus,
  MyGRunTo,
  MyGFocus,
  MyFkFocusMag,
  MyFkNoFocusMag,
  MyFkFocusFar,
  MyFkNoFocusFar,
  FallbackMyWNoFocus,
};

struct ExpandedActions final {
  std::array<Action, MaxExpandedActions> actions {};
  std::uint8_t count = 0;
};

struct SelectionInput final {
  WeaponMode weaponMode = WeaponMode::Unknown;
  bool hitFlag = false;
  bool focus = false;
  bool inWRange = false;
  bool inGRange = false;
  bool actorRunning = false;
  bool targetPrehit = false;
  bool targetInWRange = false;
  bool targetFocus = false;
  bool targetRunning = false;
};

struct ExecuteInput final {
  Action action = Action::None;
  WeaponMode weaponMode = WeaponMode::Unknown;
  bool focus = false;
  bool narrowFocus = false;
  bool inWRange = false;
  bool closeupJumpBack = false;
  bool actorSwimming = false;
  bool actorInParade = false;
  bool targetDown = false;
};

struct ExecuteResult final {
  bool legal = true;
  bool consumesAction = false;
  const char* reason = "ok";
};

[[nodiscard]] constexpr bool isMelee(WeaponMode mode) noexcept {
  return mode == WeaponMode::Fist || mode == WeaponMode::OneHanded || mode == WeaponMode::TwoHanded;
}

[[nodiscard]] WeaponMode weaponModeFromName(std::string_view name) noexcept;
[[nodiscard]] Action actionFromName(std::string_view name) noexcept;
[[nodiscard]] std::string_view tableChoiceName(TableChoice choice) noexcept;
[[nodiscard]] std::string_view actionName(Action action) noexcept;
[[nodiscard]] TableChoice selectTable(const SelectionInput& input) noexcept;
[[nodiscard]] ExpandedActions expand(ScriptMove move, bool randomBit) noexcept;
[[nodiscard]] ExecuteResult validateExecution(const ExecuteInput& input) noexcept;

} // namespace Mmo::Server::FightMove
