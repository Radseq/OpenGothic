#include "mmo_server_fight_move_model.h"

namespace Mmo::Server::FightMove {

namespace {

[[nodiscard]] constexpr ExpandedActions one(Action a) noexcept {
  ExpandedActions out;
  if(a != Action::None) {
    out.actions[0] = a;
    out.count = 1;
  }
  return out;
}

[[nodiscard]] constexpr ExpandedActions two(Action a, Action b) noexcept {
  ExpandedActions out;
  out.actions[0] = a;
  out.actions[1] = b;
  out.count = 2;
  return out;
}

[[nodiscard]] constexpr ExpandedActions three(Action a, Action b, Action c) noexcept {
  ExpandedActions out;
  out.actions[0] = a;
  out.actions[1] = b;
  out.actions[2] = c;
  out.count = 3;
  return out;
}

[[nodiscard]] constexpr ExpandedActions four(Action a, Action b, Action c, Action d) noexcept {
  ExpandedActions out;
  out.actions[0] = a;
  out.actions[1] = b;
  out.actions[2] = c;
  out.actions[3] = d;
  out.count = 4;
  return out;
}

[[nodiscard]] constexpr ExpandedActions six(Action a, Action b, Action c, Action d, Action e, Action f) noexcept {
  ExpandedActions out;
  out.actions[0] = a;
  out.actions[1] = b;
  out.actions[2] = c;
  out.actions[3] = d;
  out.actions[4] = e;
  out.actions[5] = f;
  out.count = 6;
  return out;
}

} // namespace

WeaponMode weaponModeFromName(std::string_view name) noexcept {
  if(name == "no_weapon")
    return WeaponMode::NoWeapon;
  if(name == "fist")
    return WeaponMode::Fist;
  if(name == "one_handed")
    return WeaponMode::OneHanded;
  if(name == "two_handed")
    return WeaponMode::TwoHanded;
  if(name == "bow")
    return WeaponMode::Bow;
  if(name == "crossbow")
    return WeaponMode::Crossbow;
  if(name == "mage")
    return WeaponMode::Mage;
  return WeaponMode::Unknown;
}

Action actionFromName(std::string_view name) noexcept {
  if(name == "move")
    return Action::Move;
  if(name == "jump_back")
    return Action::JumpBack;
  if(name == "attack")
    return Action::Attack;
  if(name == "attack_left")
    return Action::AttackLeft;
  if(name == "attack_right")
    return Action::AttackRight;
  if(name == "strafe_left")
    return Action::StrafeLeft;
  if(name == "strafe_right")
    return Action::StrafeRight;
  if(name == "strafe_end")
    return Action::StrafeEnd;
  if(name == "block")
    return Action::Block;
  if(name == "shoot_ranged")
    return Action::ShootRanged;
  if(name == "cast_spell")
    return Action::CastSpell;
  if(name == "wait")
    return Action::Wait;
  if(name == "wait_long")
    return Action::WaitLong;
  if(name == "turn_to_hit")
    return Action::TurnToHit;
  if(name == "turn")
    return Action::Turn;
  return Action::None;
}

std::string_view tableChoiceName(TableChoice choice) noexcept {
  switch(choice) {
    case TableChoice::None: return "none";
    case TableChoice::EnemyPrehit: return "enemy_prehit";
    case TableChoice::EnemyStormPrehit: return "enemy_stormprehit";
    case TableChoice::MyWStrafe: return "my_w_strafe";
    case TableChoice::MyWRunTo: return "my_w_runto";
    case TableChoice::MyWFocus: return "my_w_focus";
    case TableChoice::MyWNoFocus: return "my_w_nofocus";
    case TableChoice::MyGRunTo: return "my_g_runto";
    case TableChoice::MyGFocus: return "my_g_focus";
    case TableChoice::MyFkFocusMag: return "my_fk_focus_mag";
    case TableChoice::MyFkNoFocusMag: return "my_fk_nofocus_mag";
    case TableChoice::MyFkFocusFar: return "my_fk_focus_far";
    case TableChoice::MyFkNoFocusFar: return "my_fk_nofocus_far";
    case TableChoice::FallbackMyWNoFocus: return "fallback_my_w_nofocus";
  }
  return "none";
}

std::string_view actionName(Action action) noexcept {
  switch(action) {
    case Action::None: return "none";
    case Action::Move: return "move";
    case Action::JumpBack: return "jump_back";
    case Action::Attack: return "attack";
    case Action::AttackLeft: return "attack_left";
    case Action::AttackRight: return "attack_right";
    case Action::StrafeLeft: return "strafe_left";
    case Action::StrafeRight: return "strafe_right";
    case Action::StrafeEnd: return "strafe_end";
    case Action::Block: return "block";
    case Action::ShootRanged: return "shoot_ranged";
    case Action::CastSpell: return "cast_spell";
    case Action::Wait: return "wait";
    case Action::WaitLong: return "wait_long";
    case Action::TurnToHit: return "turn_to_hit";
    case Action::Turn: return "turn";
  }
  return "none";
}

TableChoice selectTable(const SelectionInput& input) noexcept {
  if(input.hitFlag)
    return TableChoice::MyWStrafe;

  if(input.targetPrehit && input.targetInWRange && input.targetFocus && input.focus)
    return input.targetRunning ? TableChoice::EnemyStormPrehit : TableChoice::EnemyPrehit;

  if(isMelee(input.weaponMode)) {
    if(input.inWRange) {
      if(input.focus && input.actorRunning)
        return TableChoice::MyWRunTo;
      if(input.focus && !input.actorRunning)
        return TableChoice::MyWFocus;
      return TableChoice::MyWNoFocus;
    }

    if(input.inGRange) {
      if(input.focus && input.actorRunning)
        return TableChoice::MyGRunTo;
      if(input.focus && !input.actorRunning)
        return TableChoice::MyGFocus;
    }
  }

  if(input.weaponMode == WeaponMode::Mage) {
    if(input.inWRange)
      return TableChoice::MyFkFocusMag;
    return TableChoice::MyFkNoFocusMag;
  }

  if(input.inWRange && input.focus)
    return TableChoice::MyFkFocusFar;
  if(input.weaponMode == WeaponMode::Bow || input.weaponMode == WeaponMode::Crossbow || input.weaponMode == WeaponMode::Unknown)
    return TableChoice::MyFkNoFocusFar;
  return TableChoice::FallbackMyWNoFocus;
}

ExpandedActions expand(ScriptMove move, bool randomBit) noexcept {
  switch(move) {
    case ScriptMove::Nop:
    case ScriptMove::RunBack:
    case ScriptMove::StandUp:
      return {};
    case ScriptMove::Turn:
      return one(Action::Turn);
    case ScriptMove::Run:
      return one(Action::Move);
    case ScriptMove::JumpBack:
      return one(Action::JumpBack);
    case ScriptMove::Strafe:
      return two(randomBit ? Action::StrafeLeft : Action::StrafeRight, Action::StrafeEnd);
    case ScriptMove::Attack:
      return one(Action::Attack);
    case ScriptMove::AttackSide:
      return two(Action::AttackLeft, Action::AttackRight);
    case ScriptMove::AttackFront:
      return two(randomBit ? Action::AttackLeft : Action::AttackRight, Action::Attack);
    case ScriptMove::AttackTriple:
      return randomBit ? three(Action::Attack, Action::AttackRight, Action::AttackLeft) :
                         three(Action::AttackLeft, Action::AttackRight, Action::Attack);
    case ScriptMove::AttackWhirl:
      return four(Action::AttackLeft, Action::AttackRight, Action::AttackLeft, Action::AttackRight);
    case ScriptMove::AttackMaster:
      return six(Action::AttackLeft, Action::AttackRight, Action::Attack, Action::Attack, Action::Attack, Action::Attack);
    case ScriptMove::TurnToHit:
      return one(Action::TurnToHit);
    case ScriptMove::Parry:
      return one(Action::Block);
    case ScriptMove::Wait:
    case ScriptMove::WaitExt:
      return one(Action::Wait);
    case ScriptMove::WaitLonger:
      return one(Action::WaitLong);
  }
  return {};
}

ExecuteResult validateExecution(const ExecuteInput& input) noexcept {
  if(input.targetDown && (input.action == Action::Move || input.action == Action::Turn))
    return {.legal = true, .consumesAction = true, .reason = "target_down_idle"};

  if(input.action == Action::Block) {
    if(!input.focus)
      return {.legal = false, .consumesAction = true, .reason = "block_without_focus"};
    if(!isMelee(input.weaponMode))
      return {.legal = false, .consumesAction = true, .reason = "block_without_melee_weapon"};
    return {.legal = true, .consumesAction = false, .reason = "block_legal"};
  }

  if(input.action == Action::Attack || input.action == Action::AttackLeft || input.action == Action::AttackRight) {
    if(!input.narrowFocus)
      return {.legal = false, .consumesAction = false, .reason = "attack_needs_turn_to_target"};
    if(input.action != Action::Attack && isMelee(input.weaponMode) && !input.inWRange)
      return {.legal = false, .consumesAction = true, .reason = "side_attack_outside_w_range"};
    return {.legal = true, .consumesAction = false, .reason = "attack_legal"};
  }

  if(input.action == Action::ShootRanged) {
    if(input.weaponMode != WeaponMode::Bow && input.weaponMode != WeaponMode::Crossbow)
      return {.legal = false, .consumesAction = false, .reason = "shoot_without_ranged_weapon"};
    if(!input.focus)
      return {.legal = false, .consumesAction = false, .reason = "shoot_without_focus"};
    return {.legal = true, .consumesAction = false, .reason = "shoot_ranged_legal"};
  }

  if(input.action == Action::CastSpell) {
    if(input.weaponMode != WeaponMode::Mage)
      return {.legal = false, .consumesAction = false, .reason = "cast_without_spell_weapon"};
    return {.legal = true, .consumesAction = false, .reason = "cast_spell_legal"};
  }

  if(input.action == Action::JumpBack || (input.action == Action::Wait && input.closeupJumpBack) ||
     (input.action == Action::Turn && input.closeupJumpBack)) {
    if(input.actorSwimming)
      return {.legal = false, .consumesAction = true, .reason = "jumpback_while_swimming"};
    if(input.actorInParade)
      return {.legal = false, .consumesAction = true, .reason = "jumpback_while_parading"};
    if(!input.focus && !input.closeupJumpBack)
      return {.legal = false, .consumesAction = true, .reason = "jumpback_without_focus"};
    return {.legal = true, .consumesAction = false, .reason = "jumpback_legal"};
  }

  return {};
}

} // namespace Mmo::Server::FightMove
