#include "mmoclientpacketbuilder.h"
#include "mmoclientjsonfields.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "../../../shared/net/mmo/mmonetprotocol.h"

namespace Mmo {
namespace {

using ClientJson::jsonBoolField;
using ClientJson::jsonNumberTextField;
using ClientJson::jsonObjectField;
using ClientJson::jsonStringField;
using ClientJson::optionalJsonDouble;
using ClientJson::optionalJsonI32;
using ClientJson::optionalJsonI64;
using ClientJson::optionalJsonString;
using ClientJson::optionalJsonU64;

std::int32_t damageAmountFromPayload(std::string_view payload) noexcept {
  const auto amount = optionalJsonI32(payload,
                                      "damage_amount",
                                      optionalJsonI32(payload, "amount", optionalJsonI32(payload, "delta", 0)));
  if(amount == std::numeric_limits<std::int32_t>::min())
    return std::numeric_limits<std::int32_t>::max();
  return amount < 0 ? -amount : amount;
}

std::string combatPayloadKey(std::string_view prefix, std::string_view field) {
  std::string out(prefix);
  out += "_combat_";
  out += field;
  return out;
}

std::string indexedCombatPayloadKey(std::string_view prefix, std::string_view field, std::size_t index) {
  auto out = combatPayloadKey(prefix, field);
  out.push_back('_');
  out += std::to_string(index);
  return out;
}

Net::ClientCombatDamageKind clientCombatDamageKind(std::string_view name) noexcept {
  if(name == "melee")
    return Net::ClientCombatDamageKind::Melee;
  if(name == "ranged")
    return Net::ClientCombatDamageKind::Ranged;
  if(name == "magic" || name == "spell")
    return Net::ClientCombatDamageKind::Magic;
  if(name == "fall")
    return Net::ClientCombatDamageKind::Fall;
  return Net::ClientCombatDamageKind::Unknown;
}

Net::ClientCombatDamageModifier clientCombatDamageModifier(std::string_view name) noexcept {
  if(name == "double")
    return Net::ClientCombatDamageModifier::Double;
  if(name == "half")
    return Net::ClientCombatDamageModifier::Half;
  if(name == "blocked")
    return Net::ClientCombatDamageModifier::Blocked;
  return Net::ClientCombatDamageModifier::Normal;
}

void fillCombatProfile(Net::ClientCombatProfile& out, std::string_view payload, std::string_view prefix) {
  out.strength = optionalJsonI32(payload, combatPayloadKey(prefix, "strength"), 0);
  out.dexterity = optionalJsonI32(payload, combatPayloadKey(prefix, "dexterity"), 0);
  out.damageTypeMask = optionalJsonI32(payload, combatPayloadKey(prefix, "damage_type_mask"), 0);
  out.meleeTalentChance = optionalJsonI32(payload, combatPayloadKey(prefix, "melee_talent_chance"), 0);
  for(std::size_t i = 0; i < Net::CombatDamageTypeCount; ++i) {
    out.damage[i] = optionalJsonI32(payload, indexedCombatPayloadKey(prefix, "damage", i), 0);
    out.protection[i] = optionalJsonI32(payload, indexedCombatPayloadKey(prefix, "protection", i), 0);
  }
}

bool hasCombatProfile(std::string_view payload, std::string_view prefix) {
  return jsonNumberTextField(payload, combatPayloadKey(prefix, "damage_type_mask")).has_value();
}

bool hasExplicitDamage(std::string_view payload) {
  for(std::size_t i = 0; i < Net::CombatDamageTypeCount; ++i) {
    const std::string key = "damage_vector_" + std::to_string(i);
    if(jsonNumberTextField(payload, key).has_value())
      return true;
  }
  return false;
}

std::optional<Net::ClientCombatDamagePacket> makeClientCombatDamagePacket(const SemanticActionEnvelope& envelope,
                                                                         std::string_view sessionKey) {
  if(envelope.kind != SemanticActionKind::ApplyCharacterDamage &&
     envelope.kind != SemanticActionKind::ApplyWorldEntityDamage) {
    return std::nullopt;
  }

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientCombatDamagePacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = envelope.targetKey;
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.sourceActorKey = optionalJsonString(payload, "source_actor_key");
  packet.sourceActorEntityKey = optionalJsonString(payload, "source_actor_entity_key", packet.sourceActorKey);
  packet.targetCharacterKey = optionalJsonString(payload, "target_character_key",
                              optionalJsonString(payload, "character_key"));
  packet.targetEntityKey = optionalJsonString(payload,
                           envelope.kind == SemanticActionKind::ApplyWorldEntityDamage ? "target_key" : "target_key",
                           envelope.targetKey);
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.damageKind = clientCombatDamageKind(optionalJsonString(payload, "damage_kind", "unknown"));
  packet.modifier = clientCombatDamageModifier(optionalJsonString(payload, "damage_modifier", "normal"));
  packet.gothicGame = optionalJsonI32(payload, "gothic_game", 2);
  packet.damageAmount = damageAmountFromPayload(payload);
  packet.valueBefore = optionalJsonI32(payload, "value_before", 0);
  packet.valueAfter = optionalJsonI32(payload, "value_after", 0);
  packet.requestedDelta = optionalJsonI32(payload, "requested_delta", 0);
  packet.criticalDamageMultiplier = optionalJsonI32(payload, "critical_damage_multiplier", 0);
  packet.meleeTalentChance = optionalJsonI32(payload, "melee_talent_chance",
                            optionalJsonI32(payload, "source_actor_combat_melee_talent_chance", 0));
  packet.meleeRandomRoll = optionalJsonI32(payload, "melee_random_roll", 0);
  packet.projectileDistance = optionalJsonDouble(payload, "projectile_distance", 0.0);
  packet.projectileWeaponChance = optionalJsonDouble(payload, "projectile_weapon_chance", 0.0);
  packet.projectileRandomHitRoll = optionalJsonDouble(payload, "projectile_random_hit_roll", 0.0);
  packet.fallSpeed = optionalJsonDouble(payload, "fall_speed", 0.0);
  packet.fallGravity = optionalJsonDouble(payload, "fall_gravity", 0.000981);
  packet.fallHeightThreshold = optionalJsonI32(payload, "fall_height_threshold", 0);
  packet.fallDamagePerMeter = optionalJsonI32(payload, "fall_damage_per_meter", 0);

  fillCombatProfile(packet.source, payload, "source_actor");
  fillCombatProfile(packet.target, payload, "target");
  for(std::size_t i = 0; i < Net::CombatDamageTypeCount; ++i) {
    const std::string key = "damage_vector_" + std::to_string(i);
    packet.explicitDamage[i] = optionalJsonI32(payload, key, 0);
  }

  if(hasCombatProfile(payload, "source_actor"))
    packet.flags |= Net::ClientCombatDamageHasSourceProfile;
  if(hasCombatProfile(payload, "target"))
    packet.flags |= Net::ClientCombatDamageHasTargetProfile;
  if(hasExplicitDamage(payload))
    packet.flags |= Net::ClientCombatDamageHasExplicitDamage;
  if(jsonBoolField(payload, "fatal").value_or(jsonBoolField(payload, "dead").value_or(false)))
    packet.flags |= Net::ClientCombatDamageFatal;
  if(jsonBoolField(payload, "critical_hit").value_or(false))
    packet.flags |= Net::ClientCombatDamageCriticalHit;
  if(jsonBoolField(payload, "source_actor_combat_monster").value_or(false))
    packet.flags |= Net::ClientCombatDamageSourceMonster;
  if(jsonBoolField(payload, "source_actor_combat_has_active_weapon").value_or(false))
    packet.flags |= Net::ClientCombatDamageSourceHasActiveWeapon;
  if(jsonBoolField(payload, "target_combat_monster").value_or(false))
    packet.flags |= Net::ClientCombatDamageTargetMonster;
  if(jsonBoolField(payload, "target_combat_has_active_weapon").value_or(false))
    packet.flags |= Net::ClientCombatDamageTargetHasActiveWeapon;
  if(jsonNumberTextField(payload, "melee_random_roll").has_value())
    packet.flags |= Net::ClientCombatDamageHasMeleeRoll;
  if(jsonNumberTextField(payload, "projectile_distance").has_value() &&
     jsonNumberTextField(payload, "projectile_random_hit_roll").has_value()) {
    packet.flags |= Net::ClientCombatDamageHasRangedRoll;
  }
  if(jsonBoolField(payload, "projectile_spell").value_or(false))
    packet.flags |= Net::ClientCombatDamageProjectileSpell;
  if(jsonBoolField(payload, "projectile_critical_hit").value_or(false))
    packet.flags |= Net::ClientCombatDamageProjectileCriticalHit;
  if(jsonNumberTextField(payload, "fall_speed").has_value())
    packet.flags |= Net::ClientCombatDamageHasFallInput;
  if(auto targetPosition = jsonObjectField(payload, "target_position")) {
    packet.targetPosX = optionalJsonDouble(*targetPosition, "x", 0.0);
    packet.targetPosY = optionalJsonDouble(*targetPosition, "y", 0.0);
    packet.targetPosZ = optionalJsonDouble(*targetPosition, "z", 0.0);
    packet.flags |= Net::ClientCombatDamageHasTargetPosition;
  }

  return packet;
}

void fillMovementStats(Net::ClientMovementStats& out, std::string_view payload) {
  out.level = optionalJsonI32(payload, "level", 0);
  out.experience = optionalJsonI32(payload, "experience", 0);
  out.experienceNext = optionalJsonI32(payload, "experience_next", 0);
  out.learningPoints = optionalJsonI32(payload, "learning_points", 0);
  out.healthCurrent = optionalJsonI32(payload, "health_current", 0);
  out.healthMax = optionalJsonI32(payload, "health_max", 0);
  out.manaCurrent = optionalJsonI32(payload, "mana_current", 0);
  out.manaMax = optionalJsonI32(payload, "mana_max", 0);
  out.strength = optionalJsonI32(payload, "strength", 0);
  out.dexterity = optionalJsonI32(payload, "dexterity", 0);
  out.guild = optionalJsonI32(payload, "guild", 0);
  out.trueGuild = optionalJsonI32(payload, "true_guild", 0);
  out.permanentAttitude = optionalJsonI32(payload, "permanent_attitude", 0);
  out.temporaryAttitude = optionalJsonI32(payload, "temporary_attitude", 0);
}

std::optional<Net::ClientMovementPacket> makeClientMovementPacket(const SemanticActionEnvelope& envelope,
                                                                 std::string_view sessionKey) {
  if(envelope.kind != SemanticActionKind::MovementProposal &&
     envelope.kind != SemanticActionKind::CharacterCheckpoint) {
    return std::nullopt;
  }

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientMovementPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = envelope.targetKey;
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalJsonString(payload, "actor_key");
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.world = optionalJsonString(payload, "world");
  packet.waypointKey = optionalJsonString(payload, "current_waypoint_key");
  packet.reason = optionalJsonString(payload, "reason");

  packet.fromTick = optionalJsonU64(payload, "from_tick", 0);
  packet.toTick = optionalJsonU64(payload, "to_tick", envelope.clientTick);
  packet.deltaMs = optionalJsonU64(payload, "delta_ms", packet.toTick >= packet.fromTick ? packet.toTick - packet.fromTick : 0);
  packet.fromX = optionalJsonDouble(payload, "from_pos_x", 0.0);
  packet.fromY = optionalJsonDouble(payload, "from_pos_y", 0.0);
  packet.fromZ = optionalJsonDouble(payload, "from_pos_z", 0.0);
  packet.toX = optionalJsonDouble(payload, "to_pos_x", optionalJsonDouble(payload, "pos_x", 0.0));
  packet.toY = optionalJsonDouble(payload, "to_pos_y", optionalJsonDouble(payload, "pos_y", 0.0));
  packet.toZ = optionalJsonDouble(payload, "to_pos_z", optionalJsonDouble(payload, "pos_z", 0.0));
  packet.fromYaw = optionalJsonDouble(payload, "from_rotation_yaw", 0.0);
  packet.toYaw = optionalJsonDouble(payload, "to_rotation_yaw", optionalJsonDouble(payload, "rotation_yaw", 0.0));
  packet.cadenceIntervalMs = optionalJsonU64(payload,
                                             envelope.kind == SemanticActionKind::MovementProposal ? "proposal_interval_ms" : "checkpoint_interval_ms",
                                             0);
  packet.cadenceMinDistance = optionalJsonDouble(payload,
                                                 envelope.kind == SemanticActionKind::MovementProposal ? "proposal_min_distance" : "checkpoint_min_distance",
                                                 0.0);
  packet.cadenceMinYawDeg = optionalJsonDouble(payload,
                                               envelope.kind == SemanticActionKind::MovementProposal ? "proposal_min_yaw_deg" : "checkpoint_min_yaw_deg",
                                               0.0);
  packet.checkpointForceIntervalMs = optionalJsonU64(payload, "checkpoint_force_interval_ms", 0);
  fillMovementStats(packet.stats, payload);

  if(envelope.kind == SemanticActionKind::MovementProposal) {
    packet.flags |= Net::ClientMovementHasFromTransform;
    packet.flags |= Net::ClientMovementHasToTransform;
  } else {
    packet.flags |= Net::ClientMovementHasToTransform;
  }
  if(jsonNumberTextField(payload, "level").has_value())
    packet.flags |= Net::ClientMovementHasStats;
  if(jsonBoolField(payload, "from_is_in_air").value_or(false))
    packet.flags |= Net::ClientMovementFromInAir;
  if(jsonBoolField(payload, "from_is_falling").value_or(false))
    packet.flags |= Net::ClientMovementFromFalling;
  if(jsonBoolField(payload, "from_is_falling_deep").value_or(false))
    packet.flags |= Net::ClientMovementFromFallingDeep;
  if(jsonBoolField(payload, "from_is_slide").value_or(false))
    packet.flags |= Net::ClientMovementFromSlide;
  if(jsonBoolField(payload, "from_is_jump").value_or(false))
    packet.flags |= Net::ClientMovementFromJump;
  if(jsonBoolField(payload, "from_is_jump_up").value_or(false))
    packet.flags |= Net::ClientMovementFromJumpUp;
  if(jsonBoolField(payload, "from_is_swim").value_or(false))
    packet.flags |= Net::ClientMovementFromSwim;
  if(jsonBoolField(payload, "from_is_dive").value_or(false))
    packet.flags |= Net::ClientMovementFromDive;
  if(jsonBoolField(payload, "from_is_in_water").value_or(false))
    packet.flags |= Net::ClientMovementFromInWater;
  if(jsonBoolField(payload, "to_is_in_air").value_or(false))
    packet.flags |= Net::ClientMovementToInAir;
  if(jsonBoolField(payload, "to_is_falling").value_or(false))
    packet.flags |= Net::ClientMovementToFalling;
  if(jsonBoolField(payload, "to_is_falling_deep").value_or(false))
    packet.flags |= Net::ClientMovementToFallingDeep;
  if(jsonBoolField(payload, "to_is_slide").value_or(false))
    packet.flags |= Net::ClientMovementToSlide;
  if(jsonBoolField(payload, "to_is_jump").value_or(false))
    packet.flags |= Net::ClientMovementToJump;
  if(jsonBoolField(payload, "to_is_jump_up").value_or(false))
    packet.flags |= Net::ClientMovementToJumpUp;
  if(jsonBoolField(payload, "to_is_swim").value_or(false))
    packet.flags |= Net::ClientMovementToSwim;
  if(jsonBoolField(payload, "to_is_dive").value_or(false))
    packet.flags |= Net::ClientMovementToDive;
  if(jsonBoolField(payload, "to_is_in_water").value_or(false))
    packet.flags |= Net::ClientMovementToInWater;

  return packet;
}

bool fillVec3FromObject(std::string_view payload,
                        std::string_view key,
                        double& x,
                        double& y,
                        double& z) noexcept {
  const auto object = jsonObjectField(payload, key);
  if(!object)
    return false;
  x = optionalJsonDouble(*object, "x", 0.0);
  y = optionalJsonDouble(*object, "y", 0.0);
  z = optionalJsonDouble(*object, "z", 0.0);
  return true;
}

std::string optionalFirstJsonString(std::string_view payload,
                                    std::initializer_list<std::string_view> keys,
                                    std::string fallback = {}) {
  for(const auto key : keys) {
    if(auto value = jsonStringField(payload, key))
      return *value;
  }
  return fallback;
}

std::optional<Net::ClientInventoryPacket> makeClientInventoryPacket(const SemanticActionEnvelope& envelope,
                                                                    std::string_view sessionKey) {
  if(!Net::isInventoryPacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientInventoryPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalFirstJsonString(payload, {"target_key", "world_item_entity_key", "engine_world_item_key"}, envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "looter_key", "buyer_key", "seller_key", "character_key"});
  packet.sourceActorKey = optionalJsonString(payload, "source_actor_key");
  packet.targetCharacterKey = optionalFirstJsonString(payload, {"target_character_key", "character_key"});
  packet.itemTemplateKey = optionalJsonString(payload, "item_template_key");
  packet.itemInstanceId = optionalJsonString(payload, "item_instance_id");
  packet.itemInstanceUuid = optionalJsonString(payload, "item_instance_uuid");
  packet.equipmentSlot = optionalFirstJsonString(payload, {"equipment_slot", "slot_name", "slot"});
  packet.sourceEntityKey = optionalJsonString(payload, "source_entity_key");
  packet.sourceContainerKey = optionalJsonString(payload, "source_container_key");
  packet.containerKey = optionalFirstJsonString(payload, {"container_key", "source_container_key"});
  packet.sourceNpcKey = optionalFirstJsonString(payload, {"source_npc_key", "source_npc_entity_key"});
  packet.targetNpcEntityKey = optionalFirstJsonString(payload, {"target_npc_entity_key", "npc_entity_key"});
  packet.npcKey = optionalFirstJsonString(payload, {"npc_key", "npc_entity_key", "target_npc_entity_key"});
  packet.world = optionalJsonString(payload, "world");
  packet.tag = optionalJsonString(payload, "tag");
  packet.focusName = optionalJsonString(payload, "focus_name");
  packet.displayName = optionalJsonString(payload, "display_name");
  packet.scheme = optionalJsonString(payload, "scheme");
  packet.reason = optionalJsonString(payload, "reason");
  packet.currencyKey = optionalJsonString(payload, "currency_key");

  packet.itemSymbol = optionalJsonI64(payload, "item_symbol", optionalJsonI64(payload, "item_template_symbol", -1));
  packet.inventoryItemSymbol = optionalJsonI64(payload, "inventory_item_symbol", -1);
  packet.itemPersistentId = optionalJsonI64(payload, "item_persistent_id", optionalJsonI64(payload, "item_instance_persistent_id", -1));
  packet.sourceItemPersistentId = optionalJsonI64(payload, "source_item_persistent_id", -1);
  packet.sourceWorldItemPersistentId = optionalJsonI64(payload, "source_world_item_persistent_id", -1);
  packet.worldItemPersistentId = optionalJsonI64(payload, "world_item_persistent_id", -1);
  packet.vendorItemPersistentId = optionalJsonI64(payload, "vendor_item_persistent_id", -1);
  packet.sellerItemPersistentId = optionalJsonI64(payload, "seller_item_persistent_id", -1);
  packet.amount = optionalJsonI64(payload, "amount", 1);
  packet.slot = optionalJsonI64(payload, "slot", 0);
  packet.bagIndex = optionalJsonI64(payload, "server_bag_index", optionalJsonI64(payload, "bag_index", -1));
  packet.targetBagIndex = optionalJsonI64(payload, "target_bag_index", -1);
  packet.unitPrice = optionalJsonI64(payload, "unit_price", 0);
  packet.priceTotal = optionalJsonI64(payload, "price_total", 0);
  packet.walletBefore = optionalJsonI64(payload, "wallet_before", 0);
  packet.walletAfter = optionalJsonI64(payload, "wallet_after", 0);
  packet.slotId = optionalJsonU64(payload, "slot_id", 0);
  packet.vobId = optionalJsonU64(payload, "vob_id", 0);

  if(fillVec3FromObject(payload, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ) ||
     fillVec3FromObject(payload, "looter_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ) ||
     fillVec3FromObject(payload, "buyer_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ) ||
     fillVec3FromObject(payload, "seller_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ)) {
    packet.flags |= Net::ClientInventoryHasActorPosition;
  }
  if(fillVec3FromObject(payload, "item_position", packet.itemPosX, packet.itemPosY, packet.itemPosZ))
    packet.flags |= Net::ClientInventoryHasItemPosition;
  if(fillVec3FromObject(payload, "source_npc_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ) ||
     fillVec3FromObject(payload, "source_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ)) {
    packet.flags |= Net::ClientInventoryHasSourcePosition;
  }

  if(jsonBoolField(payload, "moved_whole_instance").value_or(false))
    packet.flags |= Net::ClientInventoryMovedWholeInstance;
  if(jsonBoolField(payload, "source_dead").value_or(false))
    packet.flags |= Net::ClientInventorySourceDead;
  if(jsonBoolField(payload, "source_unconscious").value_or(false))
    packet.flags |= Net::ClientInventorySourceUnconscious;
  if(jsonBoolField(payload, "container").value_or(envelope.kind == SemanticActionKind::TakeContainerItem ||
                                                   envelope.kind == SemanticActionKind::PutContainerItem)) {
    packet.flags |= Net::ClientInventoryContainer;
  }
  if(!packet.equipmentSlot.empty())
    packet.flags |= Net::ClientInventoryHasEquipmentSlot;
  if(jsonNumberTextField(payload, "unit_price").has_value() ||
     jsonNumberTextField(payload, "price_total").has_value()) {
    packet.flags |= Net::ClientInventoryHasTradePrice;
  }
  if(jsonNumberTextField(payload, "wallet_before").has_value() ||
     jsonNumberTextField(payload, "wallet_after").has_value()) {
    packet.flags |= Net::ClientInventoryHasWallet;
  }

  return packet;
}

std::optional<Net::ClientWorldStatePacket> makeClientWorldStatePacket(const SemanticActionEnvelope& envelope,
                                                                      std::string_view sessionKey) {
  if(!Net::isWorldStatePacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientWorldStatePacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalFirstJsonString(payload, {"target_key", "interactive_key", "script_key", "quest_key"}, envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "speaker_key", "character_key"});
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.resourceKey = optionalJsonString(payload, "resource_key");
  packet.interactiveKey = optionalFirstJsonString(payload, {"interactive_key", "interactive_entity_key"});
  packet.entityKey = optionalFirstJsonString(payload, {"entity_key", "interactive_entity_key", "mover_key", "trigger_key"});
  packet.tag = optionalJsonString(payload, "tag");
  packet.focusName = optionalJsonString(payload, "focus_name");
  packet.displayName = optionalJsonString(payload, "display_name");
  packet.scheme = optionalJsonString(payload, "scheme");
  packet.stateBeforeName = optionalFirstJsonString(payload, {"state_before_name", "previous_weapon_state"});
  packet.stateAfterName = optionalFirstJsonString(payload, {"state_after_name", "state_after", "new_weapon_state"});
  packet.eventTypeName = optionalJsonString(payload, "event_type_name");
  packet.eventTarget = optionalJsonString(payload, "event_target");
  packet.eventEmitter = optionalJsonString(payload, "event_emitter");
  packet.triggerName = optionalJsonString(payload, "trigger_name");
  packet.triggerTargetName = optionalJsonString(payload, "target_name");
  packet.scriptKey = optionalFirstJsonString(payload, {"script_key", "global_key"});
  packet.globalKey = optionalJsonString(payload, "global_key");
  packet.symbolName = optionalJsonString(payload, "symbol_name");
  packet.scriptFunctionName = optionalJsonString(payload, "script_function_name");
  packet.questKey = optionalJsonString(payload, "quest_key");
  packet.questName = optionalJsonString(payload, "quest_name");
  packet.status = optionalJsonString(payload, "status");
  packet.npcKey = optionalJsonString(payload, "npc_key");
  packet.npcSymbolName = optionalJsonString(payload, "npc_symbol_name");
  packet.infoKey = optionalJsonString(payload, "info_key");
  packet.infoSymbolName = optionalJsonString(payload, "info_symbol_name");
  packet.conversationKey = optionalJsonString(payload, "conversation_key");
  packet.syncGroup = optionalJsonString(payload, "sync_group");
  packet.speakerKey = optionalJsonString(payload, "speaker_key");
  packet.listenerKey = optionalJsonString(payload, "listener_key");
  packet.outputName = optionalJsonString(payload, "output_name");
  packet.messageName = optionalJsonString(payload, "message_name");
  packet.subtitleText = optionalJsonString(payload, "subtitle_text");

  packet.valueBefore = optionalJsonI64(payload, "value_before", optionalJsonI64(payload, "level_before", 0));
  packet.valueAfter = optionalJsonI64(payload, "value_after", optionalJsonI64(payload, "level_after", 0));
  packet.valueDelta = optionalJsonI64(payload, "delta_amount", optionalJsonI64(payload, "level_delta", packet.valueAfter - packet.valueBefore));
  packet.secondaryBefore = optionalJsonI64(payload, "experience_before", optionalJsonI64(payload, "world_time_before_ms", 0));
  packet.secondaryAfter = optionalJsonI64(payload, "experience_after", optionalJsonI64(payload, "world_time_after_ms", 0));
  packet.secondaryDelta = optionalJsonI64(payload, "experience_delta", optionalJsonI64(payload, "time_delta_ms", packet.secondaryAfter - packet.secondaryBefore));
  packet.tertiaryBefore = optionalJsonI64(payload, "learning_points_before", 0);
  packet.tertiaryAfter = optionalJsonI64(payload, "learning_points_after", 0);
  packet.tertiaryDelta = optionalJsonI64(payload, "learning_points_delta", packet.tertiaryAfter - packet.tertiaryBefore);
  packet.symbolIndex = optionalJsonI64(payload, "symbol_index", -1);
  packet.valueIndex = optionalJsonI64(payload, "value_index", -1);
  packet.scriptFunctionSymbol = optionalJsonI64(payload, "script_function_symbol", -1);
  packet.npcSymbol = optionalJsonI64(payload, "npc_symbol", -1);
  packet.infoSymbol = optionalJsonI64(payload, "info_symbol", -1);
  packet.entryCount = optionalJsonI64(payload, "entry_count", 0);
  packet.durationMs = optionalJsonI64(payload, "line_duration_ms", 0);
  packet.worldTimeBeforeMs = optionalJsonI64(payload, "world_time_before_ms", 0);
  packet.worldTimeAfterMs = optionalJsonI64(payload, "world_time_after_ms", 0);
  packet.worldDayBefore = optionalJsonI64(payload, "world_day_before", 0);
  packet.worldDayAfter = optionalJsonI64(payload, "world_day_after", 0);
  packet.worldHourBefore = optionalJsonI64(payload, "world_hour_before", 0);
  packet.worldHourAfter = optionalJsonI64(payload, "world_hour_after", 0);
  packet.worldMinuteBefore = optionalJsonI64(payload, "world_minute_before", 0);
  packet.worldMinuteAfter = optionalJsonI64(payload, "world_minute_after", 0);
  packet.eventType = optionalJsonI64(payload, "event_type", 0);
  packet.stateBefore = optionalJsonI64(payload, "state_before", optionalJsonI64(payload, "previous_weapon_state_id", 0));
  packet.stateAfter = optionalJsonI64(payload, "state_after", optionalJsonI64(payload, "new_weapon_state_id", 0));
  packet.frame = optionalJsonI64(payload, "frame", 0);
  packet.targetFrame = optionalJsonI64(payload, "target_frame", 0);
  packet.stateCount = optionalJsonI64(payload, "state_count", 0);
  packet.stateMask = optionalJsonI64(payload, "state_mask", 0);
  packet.slotId = optionalJsonI64(payload, "slot_id", 0);
  packet.vobId = optionalJsonI64(payload, "vob_id", optionalJsonI64(payload, "mover_vob_id", optionalJsonI64(payload, "trigger_vob_id", 0)));

  if(fillVec3FromObject(payload, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ) ||
     fillVec3FromObject(payload, "speaker_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ)) {
    packet.flags |= Net::ClientWorldStateHasActorPosition;
  }
  if(fillVec3FromObject(payload, "target_position", packet.targetPosX, packet.targetPosY, packet.targetPosZ))
    packet.flags |= Net::ClientWorldStateHasTargetPosition;
  if(fillVec3FromObject(payload, "source_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ) ||
     fillVec3FromObject(payload, "listener_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ)) {
    packet.flags |= Net::ClientWorldStateHasSourcePosition;
  }

  if(jsonBoolField(payload, "known").value_or(false))
    packet.flags |= Net::ClientWorldStateKnown;
  if(jsonBoolField(payload, "removed").value_or(false))
    packet.flags |= Net::ClientWorldStateRemoved;
  if(jsonBoolField(payload, "locked_before").value_or(false))
    packet.flags |= Net::ClientWorldStateLockedBefore;
  if(jsonBoolField(payload, "locked_after").value_or(false))
    packet.flags |= Net::ClientWorldStateLockedAfter;
  if(jsonBoolField(payload, "cracked_before").value_or(false))
    packet.flags |= Net::ClientWorldStateCrackedBefore;
  if(jsonBoolField(payload, "cracked_after").value_or(false))
    packet.flags |= Net::ClientWorldStateCrackedAfter;
  if(jsonBoolField(payload, "container").value_or(false))
    packet.flags |= Net::ClientWorldStateContainer;
  if(jsonBoolField(payload, "door").value_or(false))
    packet.flags |= Net::ClientWorldStateDoor;
  if(jsonBoolField(payload, "ladder").value_or(false))
    packet.flags |= Net::ClientWorldStateLadder;
  if(jsonBoolField(payload, "ready").value_or(envelope.kind == SemanticActionKind::ReadyWeapon))
    packet.flags |= Net::ClientWorldStateReady;

  return packet;
}

std::optional<Net::ClientNpcStatePacket> makeClientNpcStatePacket(const SemanticActionEnvelope& envelope,
                                                                  std::string_view sessionKey) {
  if(!Net::isNpcStatePacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientNpcStatePacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalFirstJsonString(payload, {"target_key", "npc_entity_key", "actor_npc_entity_key"}, envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.reason = optionalJsonString(payload, "reason");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "actor_npc_entity_key", "actor_npc_key"});
  packet.npcEntityKey = optionalFirstJsonString(payload, {"npc_entity_key", "actor_npc_entity_key", "target_npc_entity_key"}, packet.targetKey);
  packet.npcKey = optionalFirstJsonString(payload, {"npc_key", "actor_npc_key", "target_npc_key"});
  packet.targetNpcEntityKey = optionalFirstJsonString(payload, {"target_npc_entity_key", "target_world_entity_key"});
  packet.targetNpcKey = optionalJsonString(payload, "target_npc_key");
  packet.sourceNpcEntityKey = optionalFirstJsonString(payload, {"source_npc_entity_key", "source_actor_entity_key"});
  packet.sourceActorKey = optionalFirstJsonString(payload, {"source_actor_key", "source_actor_entity_key"});
  packet.displayName = optionalFirstJsonString(payload, {"display_name", "actor_npc_display_name"});
  packet.targetDisplayName = optionalJsonString(payload, "target_npc_display_name");
  packet.world = optionalJsonString(payload, "world");
  packet.routineState = optionalJsonString(payload, "routine_state");
  packet.scheduleKey = optionalJsonString(payload, "schedule_key");
  packet.currentWaypointKey = optionalJsonString(payload, "current_waypoint_key");
  packet.currentWaypointName = optionalJsonString(payload, "current_waypoint_name");
  packet.currentWaypointLegacy = optionalJsonString(payload, "current_waypoint");
  packet.targetWaypointKey = optionalJsonString(payload, "target_waypoint_key");
  packet.targetWaypointName = optionalJsonString(payload, "target_waypoint_name");
  packet.targetWaypointLegacy = optionalJsonString(payload, "target_waypoint");
  packet.nextWaypointKey = optionalJsonString(payload, "next_waypoint_key");
  packet.nextWaypointName = optionalJsonString(payload, "next_waypoint_name");
  packet.nextWaypointLegacy = optionalJsonString(payload, "next_waypoint");
  packet.aiState = optionalFirstJsonString(payload, {"ai_state", "ai_state_name"});
  packet.aiIntent = optionalFirstJsonString(payload, {"ai_intent", "intent"});
  packet.aiTargetKey = optionalJsonString(payload, "ai_target_key");
  packet.perceptionState = optionalJsonString(payload, "perception_state");
  packet.actionKey = optionalJsonString(payload, "action_key");
  packet.actionName = optionalJsonString(payload, "action_name");
  packet.actionState = optionalFirstJsonString(payload, {"action_state", "state"});
  packet.actionTargetKey = optionalFirstJsonString(payload, {"action_target_key", "target_entity_key"});
  packet.syncGroup = optionalFirstJsonString(payload, {"sync_group", "conversation_key"});
  packet.pathState = optionalJsonString(payload, "path_state");
  packet.routeKey = optionalJsonString(payload, "route_key");
  packet.moveHint = optionalJsonString(payload, "move_hint");
  packet.opponentKey = optionalFirstJsonString(payload, {"opponent_key", "target_entity_key", "target_key"});
  packet.fightState = optionalJsonString(payload, "fight_state");
  packet.attackState = optionalJsonString(payload, "attack_state");
  packet.combatAction = optionalJsonString(payload, "combat_action");
  packet.intentState = optionalJsonString(payload, "intent_state");
  packet.weaponState = optionalJsonString(payload, "weapon_state");
  packet.animationName = optionalJsonString(payload, "animation_name");
  packet.attackAnimationName = optionalJsonString(payload, "attack_animation_name");

  packet.npcPersistentId = optionalJsonI64(payload, "npc_persistent_id", optionalJsonI64(payload, "actor_npc_persistent_id", -1));
  packet.npcSymbol = optionalJsonI64(payload, "npc_symbol", optionalJsonI64(payload, "actor_npc_symbol", -1));
  packet.targetNpcPersistentId = optionalJsonI64(payload, "target_npc_persistent_id", -1);
  packet.targetNpcSymbol = optionalJsonI64(payload, "target_npc_symbol", -1);
  packet.sourceNpcPersistentId = optionalJsonI64(payload, "source_npc_persistent_id", optionalJsonI64(payload, "source_actor_persistent_id", -1));
  packet.sourceNpcSymbol = optionalJsonI64(payload, "source_npc_symbol", optionalJsonI64(payload, "source_actor_symbol", -1));
  packet.healthCurrent = optionalJsonI64(payload, "health_current", 0);
  packet.healthMax = optionalJsonI64(payload, "health_max", 0);
  packet.aiStateFunction = optionalJsonI64(payload, "ai_state_function", 0);
  packet.remainingPathPoints = optionalJsonI64(payload, "remaining_path_points", 0);
  packet.comboIndex = optionalJsonI64(payload, "combo_index", 0);
  packet.bodyState = optionalJsonI64(payload, "body_state", 0);
  packet.weaponStateId = optionalJsonI64(payload, "weapon_state_id", 0);
  packet.animationElapsedMs = optionalJsonI64(payload, "animation_elapsed_ms", 0);
  packet.attackAnimationElapsedMs = optionalJsonI64(payload, "attack_animation_elapsed_ms", 0);
  packet.animationTotalMs = optionalJsonI64(payload, "animation_total_ms", 0);
  packet.attackTotalMs = optionalJsonI64(payload, "attack_total_ms", 0);
  packet.attackOptimalMs = optionalJsonI64(payload, "attack_optimal_ms", 0);
  packet.attackHitEndMs = optionalJsonI64(payload, "attack_hit_end_ms", 0);
  packet.parryWindowStartMs = optionalJsonI64(payload, "parry_window_start_ms", 0);
  packet.parryWindowEndMs = optionalJsonI64(payload, "parry_window_end_ms", 0);
  packet.comboWindowStartMs = optionalJsonI64(payload, "combo_window_start_ms", 0);
  packet.comboWindowEndMs = optionalJsonI64(payload, "combo_window_end_ms", 0);
  packet.attackerYawRad = optionalJsonDouble(payload, "attacker_yaw_rad", 0.0);
  packet.opponentYawRad = optionalJsonDouble(payload, "opponent_yaw_rad", 0.0);
  packet.weaponRange = optionalJsonDouble(payload, "weapon_range", 0.0);
  packet.attackRange = optionalJsonDouble(payload, "attack_range", 0.0);
  packet.opponentAttackRange = optionalJsonDouble(payload, "opponent_attack_range", 0.0);
  packet.attackerFightRangeBase = optionalJsonDouble(payload, "attacker_fight_range_base", 0.0);
  packet.opponentFightRangeBase = optionalJsonDouble(payload, "opponent_fight_range_base", 0.0);

  if(fillVec3FromObject(payload, "position", packet.posX, packet.posY, packet.posZ) ||
     fillVec3FromObject(payload, "actor_position", packet.posX, packet.posY, packet.posZ)) {
    packet.flags |= Net::ClientNpcStateHasPosition;
  } else if(jsonNumberTextField(payload, "pos_x").has_value()) {
    packet.posX = optionalJsonDouble(payload, "pos_x", 0.0);
    packet.posY = optionalJsonDouble(payload, "pos_y", 0.0);
    packet.posZ = optionalJsonDouble(payload, "pos_z", 0.0);
    packet.flags |= Net::ClientNpcStateHasPosition;
  }
  if(fillVec3FromObject(payload, "target_position", packet.targetPosX, packet.targetPosY, packet.targetPosZ))
    packet.flags |= Net::ClientNpcStateHasTargetPosition;
  if(fillVec3FromObject(payload, "attacker_center", packet.attackerCenterX, packet.attackerCenterY, packet.attackerCenterZ))
    packet.flags |= Net::ClientNpcStateHasAttackerCenter;
  if(fillVec3FromObject(payload, "opponent_center", packet.opponentCenterX, packet.opponentCenterY, packet.opponentCenterZ))
    packet.flags |= Net::ClientNpcStateHasOpponentCenter;
  if(fillVec3FromObject(payload, "fight_distance", packet.fightDistanceX, packet.fightDistanceY, packet.fightDistanceZ))
    packet.flags |= Net::ClientNpcStateHasFightDistance;

  if(jsonBoolField(payload, "dead").value_or(false))
    packet.flags |= Net::ClientNpcStateDead;
  if(jsonBoolField(payload, "unconscious").value_or(false))
    packet.flags |= Net::ClientNpcStateUnconscious;
  if(jsonBoolField(payload, "down").value_or(false))
    packet.flags |= Net::ClientNpcStateDown;
  if(jsonBoolField(payload, "attack_anim").value_or(false))
    packet.flags |= Net::ClientNpcStateAttackAnim;
  if(jsonBoolField(payload, "prehit").value_or(false))
    packet.flags |= Net::ClientNpcStatePrehit;
  if(jsonBoolField(payload, "actor_running").value_or(false))
    packet.flags |= Net::ClientNpcStateActorRunning;
  if(jsonBoolField(payload, "opponent_running").value_or(false))
    packet.flags |= Net::ClientNpcStateOpponentRunning;
  if(jsonBoolField(payload, "opponent_prehit").value_or(false))
    packet.flags |= Net::ClientNpcStateOpponentPrehit;

  return packet;
}

std::optional<Net::ClientEconomyPacket> makeClientEconomyPacket(const SemanticActionEnvelope& envelope,
                                                                std::string_view sessionKey) {
  if(!Net::isEconomyPacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientEconomyPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalJsonString(payload, "target_key", envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "character_key"});
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.currencyKey = optionalJsonString(payload, "currency_key", "g2notr:gold");
  packet.currencyDisplayName = optionalJsonString(payload, "currency_display_name", "Gold");
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.amount = optionalJsonI64(payload, "amount", 0);
  packet.deltaAmount = optionalJsonI64(payload, "delta_amount",
                       optionalJsonI64(payload, "delta",
                       envelope.kind == SemanticActionKind::SpendGold ? -packet.amount : packet.amount));
  packet.walletBefore = optionalJsonI64(payload, "wallet_before", optionalJsonI64(payload, "value_before", 0));
  packet.walletAfter = optionalJsonI64(payload, "wallet_after", optionalJsonI64(payload, "value_after", packet.walletBefore + packet.deltaAmount));
  packet.itemTemplateSymbol = optionalJsonI64(payload, "item_template_symbol", -1);
  if(fillVec3FromObject(payload, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ))
    packet.flags |= Net::ClientEconomyHasActorPosition;
  if(jsonNumberTextField(payload, "wallet_before").has_value() || jsonNumberTextField(payload, "value_before").has_value())
    packet.flags |= Net::ClientEconomyHasWalletBefore;
  if(jsonNumberTextField(payload, "wallet_after").has_value() || jsonNumberTextField(payload, "value_after").has_value())
    packet.flags |= Net::ClientEconomyHasWalletAfter;

  return packet;
}

std::optional<Net::ClientSessionControlPacket> makeClientSessionControlPacket(const SemanticActionEnvelope& envelope,
                                                                              std::string_view sessionKey) {
  if(!Net::isSessionControlPacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientSessionControlPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalJsonString(payload, "target_key", envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.sourceLocation = optionalJsonString(payload, "source_location", packet.source);
  packet.actorKey = optionalJsonString(payload, "actor_key");
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.displayName = optionalJsonString(payload, "display_name");
  packet.world = optionalJsonString(payload, "world");
  packet.serverEndpoint = optionalJsonString(payload, "server_endpoint");
  packet.clientContentManifestHash = optionalJsonString(payload, "client_content_manifest_hash");
  packet.reason = optionalJsonString(payload, "reason");
  packet.actionKind = optionalJsonString(payload, "action_kind");
  packet.manifestKey = optionalJsonString(payload, "manifest_key");
  packet.checkpointKind = optionalJsonString(payload, "checkpoint_kind");
  packet.saveSlotKey = optionalJsonString(payload, "save_slot_key");
  packet.slotPath = optionalJsonString(payload, "slot_path");
  packet.nativeSavePath = optionalJsonString(payload, "native_save_path");
  packet.slotDisplayName = optionalJsonString(payload, "slot_display_name");
  packet.clientWorldName = optionalJsonString(payload, "client_world_name", packet.world);
  packet.serverTick = optionalJsonI64(payload, "server_tick", static_cast<std::int64_t>(envelope.clientTick));
  packet.acknowledgedLocalSequence = optionalJsonI64(payload, "client_local_sequence", 0);

  if(jsonBoolField(payload, "server_bound_client_mode").value_or(envelope.kind == SemanticActionKind::ClientBootstrapRequest))
    packet.flags |= Net::ClientSessionControlServerBoundClientMode;
  if(jsonBoolField(payload, "native_save_present").value_or(false))
    packet.flags |= Net::ClientSessionControlNativeSavePresent;
  if(jsonBoolField(payload, "db_save_snapshot_requested").value_or(false))
    packet.flags |= Net::ClientSessionControlDbSaveSnapshotRequested;

  return packet;
}

std::optional<Net::ClientDialogStatePacket> makeClientDialogStatePacket(const SemanticActionEnvelope& envelope,
                                                                        std::string_view sessionKey) {
  if(!Net::isDialogStatePacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientDialogStatePacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalJsonString(payload, "target_key", envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalJsonString(payload, "actor_key");
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.npcKey = optionalFirstJsonString(payload, {"npc_key", "speaker_npc_key", "speaker_key"});
  packet.npcSymbolName = optionalFirstJsonString(payload, {"npc_symbol_name", "speaker_symbol_name"});
  packet.infoKey = optionalFirstJsonString(payload, {"info_key", "dialog_key", "known_dialog_key"});
  packet.infoSymbolName = optionalFirstJsonString(payload, {"info_symbol_name", "dialog_symbol_name"});
  packet.conversationKey = optionalFirstJsonString(payload, {"conversation_key", "dialog_session_key"});
  packet.syncGroup = optionalFirstJsonString(payload, {"sync_group", "conversation_key"});
  packet.speakerKey = optionalFirstJsonString(payload, {"speaker_key", "npc_key"});
  packet.listenerKey = optionalFirstJsonString(payload, {"listener_key", "character_key"});
  packet.outputName = optionalJsonString(payload, "output_name");
  packet.messageName = optionalFirstJsonString(payload, {"message_name", "line_name"});
  packet.subtitleText = optionalFirstJsonString(payload, {"subtitle_text", "text", "line_text"});
  packet.dialogState = optionalFirstJsonString(payload, {"dialog_state", "state", "status"});
  packet.topicKey = optionalFirstJsonString(payload, {"topic_key", "topic"});

  packet.npcSymbol = optionalJsonI64(payload, "npc_symbol", -1);
  packet.infoSymbol = optionalJsonI64(payload, "info_symbol", -1);
  packet.lineIndex = optionalJsonI64(payload, "line_index", -1);
  packet.outputIndex = optionalJsonI64(payload, "output_index", -1);
  packet.durationMs = optionalJsonI64(payload, "duration_ms", 0);

  if(jsonBoolField(payload, "known").value_or(envelope.kind == SemanticActionKind::SetKnownDialog))
    packet.flags |= Net::ClientDialogStateKnown;
  if(jsonBoolField(payload, "player_line").value_or(false))
    packet.flags |= Net::ClientDialogStatePlayerLine;
  if(jsonBoolField(payload, "npc_line").value_or(envelope.kind == SemanticActionKind::RecordNpcDialogLine))
    packet.flags |= Net::ClientDialogStateNpcLine;
  if(jsonBoolField(payload, "important").value_or(false))
    packet.flags |= Net::ClientDialogStateImportant;
  if(jsonBoolField(payload, "ambient").value_or(false))
    packet.flags |= Net::ClientDialogStateAmbient;
  if(jsonNumberTextField(payload, "line_index").has_value())
    packet.flags |= Net::ClientDialogStateHasLineIndex;
  if(jsonNumberTextField(payload, "output_index").has_value())
    packet.flags |= Net::ClientDialogStateHasOutputIndex;

  return packet;
}

std::optional<Net::ClientCharacterEventPacket> makeClientCharacterEventPacket(const SemanticActionEnvelope& envelope,
                                                                              std::string_view sessionKey) {
  if(!Net::isCharacterEventPacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientCharacterEventPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.flags = Net::ClientCharacterEventServerMustCalculate;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalFirstJsonString(payload, {"target_key", "character_key", "resource_key", "progression_key"}, envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "character_key"});
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.resourceKey = optionalJsonString(payload,
                                          "resource_key",
                                          envelope.kind == SemanticActionKind::ConsumeMana ? "mana" : "");
  packet.resourceDisplayName = optionalJsonString(payload,
                                                  "resource_display_name",
                                                  packet.resourceKey == "mana" ? "Mana" : "");
  packet.progressionKey = optionalFirstJsonString(payload, {"progression_key", "skill_key", "attribute_key"});
  packet.rewardKey = optionalFirstJsonString(payload, {"reward_key", "reward_source", "experience_reward_key"});
  packet.sourceActorKey = optionalFirstJsonString(payload, {"source_actor_key", "source_npc_key"});
  packet.sourceEntityKey = optionalFirstJsonString(payload, {"source_entity_key", "source_actor_entity_key"});

  packet.requestedDelta = optionalJsonI64(payload,
                                          "requested_delta",
                                          optionalJsonI64(payload,
                                                          "delta_amount",
                                                          optionalJsonI64(payload, "delta", 0)));
  packet.requestedAmount = optionalJsonI64(payload, "amount", packet.requestedDelta);
  packet.manaAmount = optionalJsonI64(payload, "mana_amount", optionalJsonI64(payload, "mana_cost", packet.requestedAmount));
  packet.experienceReward = optionalJsonI64(payload,
                                            "reward_experience",
                                            optionalJsonI64(payload,
                                                            "experience_reward",
                                                            optionalJsonI64(payload, "experience_delta", packet.requestedDelta)));
  packet.learningPointsReward = optionalJsonI64(payload, "reward_learning_points",
                                                optionalJsonI64(payload, "learning_points_delta", 0));
  packet.attributeSymbol = optionalJsonI64(payload, "attribute_symbol", -1);
  packet.skillSymbol = optionalJsonI64(payload, "skill_symbol", -1);

  if(fillVec3FromObject(payload, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ))
    packet.flags |= Net::ClientCharacterEventHasActorPosition;
  if(jsonNumberTextField(payload, "requested_delta").has_value() ||
     jsonNumberTextField(payload, "delta_amount").has_value() ||
     jsonNumberTextField(payload, "delta").has_value())
    packet.flags |= Net::ClientCharacterEventHasRequestedDelta;
  if(jsonNumberTextField(payload, "amount").has_value())
    packet.flags |= Net::ClientCharacterEventHasRequestedAmount;
  if(envelope.kind == SemanticActionKind::ConsumeMana ||
     jsonNumberTextField(payload, "mana_amount").has_value() ||
     jsonNumberTextField(payload, "mana_cost").has_value())
    packet.flags |= Net::ClientCharacterEventHasManaAmount;
  if(envelope.kind == SemanticActionKind::ApplyExperienceReward ||
     jsonNumberTextField(payload, "reward_experience").has_value() ||
     jsonNumberTextField(payload, "experience_reward").has_value())
    packet.flags |= Net::ClientCharacterEventHasExperienceReward;
  if(jsonNumberTextField(payload, "reward_learning_points").has_value() ||
     jsonNumberTextField(payload, "learning_points_delta").has_value())
    packet.flags |= Net::ClientCharacterEventHasLearningReward;
  if(!packet.resourceKey.empty() || envelope.kind == SemanticActionKind::ApplyCharacterResourceDelta)
    packet.flags |= Net::ClientCharacterEventHasExplicitResource;

  return packet;
}

} // namespace

std::optional<EncodedClientGameplayPacket> encodeClientGameplayPacket(
    const SemanticActionEnvelope& envelope,
    std::string_view sessionKey) {
  EncodedClientGameplayPacket out;
  if(auto combatDamage = makeClientCombatDamagePacket(envelope, sessionKey))
    out.bytes = Net::encodeClientCombatDamagePacket(*combatDamage);
  else if(auto movement = makeClientMovementPacket(envelope, sessionKey))
    out.bytes = Net::encodeClientMovementPacket(*movement);
  else if(auto inventory = makeClientInventoryPacket(envelope, sessionKey))
    out.bytes = Net::encodeClientInventoryPacket(*inventory);
  else if(auto dialogState = makeClientDialogStatePacket(envelope, sessionKey))
    out.bytes = Net::encodeClientDialogStatePacket(*dialogState);
  else if(auto characterEvent = makeClientCharacterEventPacket(envelope, sessionKey))
    out.bytes = Net::encodeClientCharacterEventPacket(*characterEvent);
  else if(auto worldState = makeClientWorldStatePacket(envelope, sessionKey))
    out.bytes = Net::encodeClientWorldStatePacket(*worldState);
  else if(auto npcState = makeClientNpcStatePacket(envelope, sessionKey))
    out.bytes = Net::encodeClientNpcStatePacket(*npcState);
  else if(auto economy = makeClientEconomyPacket(envelope, sessionKey))
    out.bytes = Net::encodeClientEconomyPacket(*economy);
  else if(auto sessionControl = makeClientSessionControlPacket(envelope, sessionKey))
    out.bytes = Net::encodeClientSessionControlPacket(*sessionControl);
  else
    out.bytes = Net::encodeClientActionPacket(envelope, sessionKey);

  if(out.bytes.empty())
    return std::nullopt;
  out.bootstrapRequest = envelope.kind == SemanticActionKind::ClientBootstrapRequest;
  return out;
}

} // namespace Mmo
