#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Mmo {

enum class SemanticActionKind : std::uint8_t {
  MovementProposal,
  CharacterCheckpoint,
  WalletDelta,
  GrantGold,
  SpendGold,
  PickupWorldItem,
  RemoveWorldItem,
  TransferCharacterItem,
  EquipCharacterItem,
  UnequipCharacterItem,
  TakeContainerItem,
  PutContainerItem,
  UpdateInteractiveState,
  SetScriptInt,
  UpdateQuest,
  SetKnownDialog,
  AdjustProgression,
  ApplyExperienceReward,
  MarkNpcDead,
  RespawnNpc,
  TradeBuyFromNpc,
  TradeSellToNpc,
  ApplyCharacterDamage,
  ApplyWorldEntityDamage,
  ConsumeMana,
  ConsumeItem,
  SplitItemStack,
  MergeItemStack,
};

struct SemanticActionDef final {
  SemanticActionKind kind;
  std::string_view   actionKind;
  std::string_view   eventType;
  std::string_view   eventClass;
  std::string_view   procedureName;
  bool               requiresSession = true;
  bool               serverAuthoritative = true;
};

inline constexpr std::array<SemanticActionDef, 28> SemanticActionDefs {{
  {SemanticActionKind::MovementProposal,      "movement_proposal",       "movement_proposal_submitted",     "movement",     "server_validate_movement_proposal",     true, true},
  {SemanticActionKind::CharacterCheckpoint,   "character_checkpoint",      "character_position_checkpoint",    "character",    "mmo_checkpoint_character_state",        true, true},
  {SemanticActionKind::WalletDelta,           "wallet_delta",              "character_wallet_delta",           "inventory",    "mmo_adjust_character_wallet",           true, true},
  {SemanticActionKind::GrantGold,             "grant_gold",                "character_wallet_delta",           "inventory",    "mmo_grant_character_gold",              true, true},
  {SemanticActionKind::SpendGold,             "spend_gold",                "character_wallet_delta",           "inventory",    "mmo_spend_character_gold",              true, true},
  {SemanticActionKind::PickupWorldItem,       "pickup_world_item",         "world_item_picked_up",             "inventory",    "mmo_pickup_world_item",                 true, true},
  {SemanticActionKind::RemoveWorldItem,       "remove_world_item",         "world_item_removed",               "world_entity", "mmo_remove_world_item",                 true, true},
  {SemanticActionKind::TransferCharacterItem, "transfer_character_item",   "character_inventory_transferred",  "inventory",    "mmo_transfer_character_item",           true, true},
  {SemanticActionKind::EquipCharacterItem,    "equip_character_item",      "character_item_equipped",          "equipment",    "mmo_equip_character_item",              true, true},
  {SemanticActionKind::UnequipCharacterItem,  "unequip_character_item",    "character_item_unequipped",        "equipment",    "mmo_unequip_character_item",            true, true},
  {SemanticActionKind::TakeContainerItem,     "take_container_item",       "container_item_taken",             "inventory",    "mmo_take_container_item",               true, true},
  {SemanticActionKind::PutContainerItem,      "put_container_item",        "container_item_put",               "inventory",    "mmo_put_container_item",                true, true},
  {SemanticActionKind::UpdateInteractiveState,"update_interactive_state",  "interactive_state_changed",        "world_entity", "mmo_update_interactive_state",          true, true},
  {SemanticActionKind::SetScriptInt,          "set_script_int",            "character_script_int_set",         "script",       "mmo_set_character_script_int",          true, true},
  {SemanticActionKind::UpdateQuest,           "update_quest",              "character_quest_updated",          "quest",        "mmo_update_character_quest",            true, true},
  {SemanticActionKind::SetKnownDialog,        "set_known_dialog",          "character_dialog_known_set",       "dialog",       "mmo_set_character_known_dialog",        true, true},
  {SemanticActionKind::AdjustProgression,     "adjust_progression",        "character_progression_adjusted",   "character",    "mmo_adjust_character_progression",      true, true},
  {SemanticActionKind::ApplyExperienceReward, "apply_experience_reward",   "character_progression_adjusted",   "character",    "mmo_apply_character_experience_reward", true, true},
  {SemanticActionKind::MarkNpcDead,           "mark_npc_dead",             "npc_marked_dead",                  "combat",       "mmo_mark_npc_dead",                     true, true},
  {SemanticActionKind::RespawnNpc,            "respawn_npc",               "npc_respawned",                    "combat",       "mmo_respawn_npc",                       true, true},
  {SemanticActionKind::TradeBuyFromNpc,       "trade_buy_from_npc",        "trade_buy_from_npc",               "trade",        "mmo_trade_buy_from_npc",                true, true},
  {SemanticActionKind::TradeSellToNpc,        "trade_sell_to_npc",         "trade_sell_to_npc",                "trade",        "mmo_trade_sell_to_npc",                 true, true},
  {SemanticActionKind::ApplyCharacterDamage,  "apply_character_damage",    "character_damage_applied",         "combat",       "mmo_apply_character_damage",            true, true},
  {SemanticActionKind::ApplyWorldEntityDamage,"apply_world_entity_damage", "world_entity_damage_applied",      "combat",       "mmo_apply_world_entity_damage",         true, true},
  {SemanticActionKind::ConsumeMana,           "consume_mana",              "character_mana_consumed",          "spell",        "mmo_consume_character_mana",            true, true},
  {SemanticActionKind::ConsumeItem,           "consume_item",              "character_item_consumed",          "inventory",    "mmo_consume_character_item",            true, true},
  {SemanticActionKind::SplitItemStack,        "split_item_stack",          "item_stack_split",                 "inventory",    "mmo_split_character_item_stack",        true, true},
  {SemanticActionKind::MergeItemStack,        "merge_item_stack",          "item_stack_merged",                "inventory",    "mmo_merge_character_item_stack",        true, true},
}};

[[nodiscard]] constexpr const SemanticActionDef* findSemanticAction(SemanticActionKind kind) noexcept {
  for(const auto& def : SemanticActionDefs) {
    if(def.kind == kind)
      return &def;
  }
  return nullptr;
}

[[nodiscard]] constexpr const SemanticActionDef* findSemanticAction(std::string_view actionKind) noexcept {
  for(const auto& def : SemanticActionDefs) {
    if(def.actionKind == actionKind)
      return &def;
  }
  return nullptr;
}

[[nodiscard]] constexpr std::string_view actionKindName(SemanticActionKind kind) noexcept {
  if(const auto* def = findSemanticAction(kind))
    return def->actionKind;
  return "unknown";
}

[[nodiscard]] constexpr std::string_view eventTypeName(SemanticActionKind kind) noexcept {
  if(const auto* def = findSemanticAction(kind))
    return def->eventType;
  return "unknown";
}

[[nodiscard]] constexpr std::string_view eventClassName(SemanticActionKind kind) noexcept {
  if(const auto* def = findSemanticAction(kind))
    return def->eventClass;
  return "unknown";
}

[[nodiscard]] constexpr std::string_view procedureName(SemanticActionKind kind) noexcept {
  if(const auto* def = findSemanticAction(kind))
    return def->procedureName;
  return "unknown";
}

struct SemanticActionEnvelope final {
  SemanticActionKind kind = SemanticActionKind::CharacterCheckpoint;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        payloadJson;
  std::uint64_t      localSequence = 0;
  std::uint64_t      clientTick = 0;
};

[[nodiscard]] std::string makeIdempotencyKey(std::string_view sessionKey,
                                             std::uint64_t localSequence,
                                             SemanticActionKind kind,
                                             std::string_view targetKey);

[[nodiscard]] bool isValidEnvelope(const SemanticActionEnvelope& envelope) noexcept;
[[nodiscard]] std::string jsonEscape(std::string_view text);
[[nodiscard]] std::string toJsonLine(const SemanticActionEnvelope& envelope);

} // namespace Mmo




