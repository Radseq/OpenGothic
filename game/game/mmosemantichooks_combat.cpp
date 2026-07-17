#include "mmosemantichooks_internal.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "mmoclientadapter.h"
#include "mmoclientbridge.h"
#include "world/world.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "commandline.h"
#include "utils/versioninfo.h"

namespace Mmo::Hooks::Detail {

static std::string attributeName(Attribute a) {
  switch(a) {
    case ATR_HITPOINTS: return "hitpoints";
    case ATR_HITPOINTSMAX: return "hitpoints_max";
    case ATR_MANA: return "mana";
    case ATR_MANAMAX: return "mana_max";
    case ATR_STRENGTH: return "strength";
    case ATR_DEXTERITY: return "dexterity";
    default: break;
  }
  std::string out = "attribute:";
  appendUInt(out, static_cast<std::uint8_t>(a));
  return out;
}

std::string weaponStateName(WeaponState state) {
  switch(state) {
    case WeaponState::NoWeapon: return "no_weapon";
    case WeaponState::Fist:     return "fist";
    case WeaponState::W1H:      return "one_handed";
    case WeaponState::W2H:      return "two_handed";
    case WeaponState::Bow:      return "bow";
    case WeaponState::CBow:     return "crossbow";
    case WeaponState::Mage:     return "mage";
  }
  return "unknown";
}

float observedWeaponRange(Npc& actor) {
  auto& guildValues = actor.world().script().guildVal();
  const auto guild = actor.guild();
  const auto* weapon = actor.inventory().activeWeapon();
  const int weaponLength = weapon != nullptr ? weapon->swordLength() : 0;
  switch(actor.weaponState()) {
    case WeaponState::W1H:
      return float(guildValues.fight_range_1ha[guild] + weaponLength);
    case WeaponState::W2H:
      return float(guildValues.fight_range_2ha[guild] + weaponLength);
    case WeaponState::NoWeapon:
    case WeaponState::Fist:
      return float(guildValues.fight_range_fist[guild]);
    case WeaponState::Bow:
    case WeaponState::CBow:
    case WeaponState::Mage:
      return 0.f;
  }
  return 0.f;
}

float observedAttackRange(Npc& actor, const Npc* target) {
  if(target == nullptr)
    return observedWeaponRange(actor);
  auto& guildValues = actor.world().script().guildVal();
  return float(guildValues.fight_range_base[actor.guild()] +
               guildValues.fight_range_base[target->guild()]) +
         observedWeaponRange(actor);
}

float observedFightRangeBase(Npc& actor) {
  return float(actor.world().script().guildVal().fight_range_base[actor.guild()]);
}

static std::int32_t observedDamageTypeMask(Npc& actor) {
  if(auto* weapon = actor.inventory().activeWeapon())
    return weapon->handle().damage_type;
  return actor.handle().damage_type;
}

static std::string_view observedDamageKind(Npc& sourceActor) noexcept {
  switch(sourceActor.weaponState()) {
    case WeaponState::Bow:
    case WeaponState::CBow:
      return "ranged";
    case WeaponState::Mage:
      return "magic";
    case WeaponState::NoWeapon:
    case WeaponState::Fist:
    case WeaponState::W1H:
    case WeaponState::W2H:
      return "melee";
  }
  return "unknown";
}

static std::int32_t observedMeleeTalentChance(Npc& actor) {
  if(actor.isMonster() && actor.inventory().activeWeapon() == nullptr && actor.world().version().game == 2)
    return 100;

  Talent talent = TALENT_UNKNOWN;
  if(auto* weapon = actor.inventory().activeWeapon())
    talent = weapon->is2H() ? TALENT_2H : TALENT_1H;

  if(talent == TALENT_UNKNOWN)
    return 0;
  return actor.world().version().game == 2 ? actor.hitChance(talent) : actor.talentValue(talent);
}

static void appendCombatDamageProfile(std::string& out, const char* prefix, Npc& npc) {
  const auto& handle = npc.handle();
  out.append(",\"");
  out.append(prefix);
  out.append("_combat_strength\":");
  appendInt(out, npc.attribute(ATR_STRENGTH));
  out.append(",\"");
  out.append(prefix);
  out.append("_combat_dexterity\":");
  appendInt(out, npc.attribute(ATR_DEXTERITY));
  out.append(",\"");
  out.append(prefix);
  out.append("_combat_damage_type_mask\":");
  appendInt(out, observedDamageTypeMask(npc));
  out.append(",\"");
  out.append(prefix);
  out.append("_combat_monster\":");
  appendBool(out, npc.isMonster());
  out.append(",\"");
  out.append(prefix);
  out.append("_combat_has_active_weapon\":");
  appendBool(out, npc.inventory().activeWeapon() != nullptr);
  out.append(",\"");
  out.append(prefix);
  out.append("_combat_melee_talent_chance\":");
  appendInt(out, observedMeleeTalentChance(npc));
  for(std::size_t i = 0; i < zenkit::DamageType::NUM; ++i) {
    out.append(",\"");
    out.append(prefix);
    out.append("_combat_damage_");
    appendUInt(out, i);
    out.append("\":");
    appendInt(out, handle.damage[i]);
    out.append(",\"");
    out.append(prefix);
    out.append("_combat_protection_");
    appendUInt(out, i);
    out.append("\":");
    appendInt(out, handle.protection[i]);
  }
  for(std::size_t i = 0; i < zenkit::INpc::hitchance_count; ++i) {
    out.append(",\"");
    out.append(prefix);
    out.append("_combat_hit_chance_");
    appendUInt(out, i);
    out.append("\":");
    appendInt(out, handle.hitchance[i]);
  }
}

struct WeaponStateCacheEntry final {
  bool occupied = false;
  bool player = false;
  std::uint32_t persistentId = 0;
  std::size_t symbol = 0;
  WeaponState state = WeaponState::NoWeapon;
  std::uint64_t lastTick = 0;
};

static std::array<WeaponStateCacheEntry, 256> weaponStateCache = {};
static std::size_t weaponStateCacheCursor = 0;

static bool sameWeaponActor(const WeaponStateCacheEntry& e, const Npc& actor) noexcept {
  return e.occupied &&
         e.player == actor.isPlayer() &&
         e.persistentId == actor.persistentId() &&
         e.symbol == actor.instanceSymbol();
}

static bool shouldEmitWeaponState(Npc& actor, WeaponState previousState, WeaponState newState) noexcept {
  if(previousState == newState)
    return false;

  auto& world = actor.world();
  const auto tick = world.tickCount();
  WeaponStateCacheEntry* reusable = nullptr;
  for(auto& e : weaponStateCache) {
    if(sameWeaponActor(e, actor)) {
      // Some AI paths call closeWeapon()/set fight mode repeatedly while the
      // visual state is still transitioning. The semantic stream must describe
      // accepted state changes, not per-frame AI retries.
      if(e.state == newState)
        return false;
      e.state = newState;
      e.lastTick = tick;
      return true;
      }
    if(!e.occupied && reusable == nullptr)
      reusable = &e;
    }

  if(reusable == nullptr) {
    reusable = &weaponStateCache[weaponStateCacheCursor % weaponStateCache.size()];
    ++weaponStateCacheCursor;
    }

  reusable->occupied = true;
  reusable->player = actor.isPlayer();
  reusable->persistentId = actor.persistentId();
  reusable->symbol = actor.instanceSymbol();
  reusable->state = newState;
  reusable->lastTick = tick;
  return true;
}

void onWeaponStateChanged(Npc& actor,
                          WeaponState previousState,
                          WeaponState newState,
                          const char* sourceLocation,
                          const char* reason) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || !shouldCaptureWorldAiAction(actor) || !shouldEmitWeaponState(actor, previousState, newState))
    return;
  auto& world = actor.world();
  const bool holstered = newState == WeaponState::NoWeapon;
  auto target = actorKey(actor);
  target.append(holstered ? ":weapon:holster" : ":weapon:ready");
  target.push_back(':');
  appendUInt(target, world.tickCount());

  if(isServerBoundClientModeEnabled() && actor.isPlayer()) {
    const auto actorIdentity = actorKey(actor);
    const auto actorPos = actor.position();
    const ClientWeaponStateRequest request{
        .clientTick = world.tickCount(),
        .intent = holstered ? ClientWeaponStateIntent::Holster
                            : ClientWeaponStateIntent::Ready,
        .actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z},
        .targetKey = target,
        .source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                            : std::string_view("unknown"),
        .actorKey = actorIdentity,
        .characterKey = characterKey(),
        .world = world.name(),
        .reason = reason != nullptr ? std::string_view(reason)
                                    : std::string_view("weapon_state_intent"),
    };
    (void)submitClientWeaponState(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(704);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "actor", actor);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"previous_weapon_state\":"); appendEscaped(payload, weaponStateName(previousState));
  payload.append(",\"new_weapon_state\":"); appendEscaped(payload, weaponStateName(newState));
  payload.append(",\"previous_weapon_state_id\":"); appendUInt(payload, static_cast<std::uint8_t>(previousState));
  payload.append(",\"new_weapon_state_id\":"); appendUInt(payload, static_cast<std::uint8_t>(newState));
  payload.append(",\"ready\":"); payload.append(holstered ? "false" : "true");
  payload.append(",\"reason\":"); appendEscaped(payload, reason);
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(holstered ? SemanticActionKind::HolsterWeapon : SemanticActionKind::ReadyWeapon,
           std::move(target), std::move(payload), world.tickCount());
}

void onCombatIntent(Npc& actor,
                    std::string_view combatAction,
                    std::string_view intentState,
                    const char* sourceLocation,
                    const char* reason) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || !isLiveWorldTick(actor.world()))
    return;

  auto& world = actor.world();
  auto actorEntity = npcEntityKey(world.name(), actor.persistentId(), actor.instanceSymbol());
  Npc* targetNpc = actor.stateVictim() != nullptr ? actor.stateVictim() :
                   actor.target() != nullptr ? actor.target() : actor.stateOther();
  const auto targetEntity = npcTargetKey(targetNpc);

  if(isServerBoundClientModeEnabled() && actor.isPlayer()) {
    const auto actorIdentity = actorKey(actor);
    const auto actorPos = actor.position();
    const ClientCombatRequest request{
        .clientTick = world.tickCount(),
        .actorPosition = {.x = actorPos.x, .y = actorPos.y, .z = actorPos.z},
        .targetKey = targetEntity.empty() ? std::string_view(actorEntity)
                                          : std::string_view(targetEntity),
        .source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                            : std::string_view("unknown"),
        .reason = reason != nullptr ? std::string_view(reason)
                                    : std::string_view("combat_intent"),
        .actorKey = actorIdentity,
        .npcEntityKey = actorEntity,
        .targetNpcEntityKey = targetEntity,
        .world = world.name(),
        .combatAction = combatAction,
        .intentState = intentState,
    };
    (void)submitClientCombat(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

  std::string payload;
  payload.reserve(1024);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"reason\":"); appendEscaped(payload, reason);
  appendNpcIdentity(payload, "actor_npc", actor);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorEntity);
  payload.append(",\"target_key\":"); appendEscaped(payload, targetEntity);
  payload.append(",\"combat_action\":"); appendEscaped(payload, combatAction);
  payload.append(",\"intent_state\":"); appendEscaped(payload, intentState);
  payload.append(",\"weapon_state\":"); appendEscaped(payload, weaponStateName(actor.weaponState()));
  payload.append(",\"weapon_state_id\":"); appendUInt(payload, static_cast<std::uint8_t>(actor.weaponState()));
  payload.append(",\"body_state\":"); appendUInt(payload, static_cast<std::uint64_t>(actor.bodyStateMasked()));
  payload.append(",\"attack_anim\":"); appendBool(payload, actor.isAttackAnim());
  payload.append(",\"prehit\":"); appendBool(payload, actor.isPrehit());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  appendVec3(payload, "attacker_center", actor.collosionCenter());
  if(targetNpc != nullptr) {
    appendNpcIdentity(payload, "target_npc", *targetNpc);
    appendVec3(payload, "target_position", targetNpc->position());
    appendVec3(payload, "opponent_center", targetNpc->collosionCenter());
  }
  payload.push_back('}');

  if(isClientMmoDiagnosticsEnabled())
    submit(SemanticActionKind::RecordCombatIntent, std::move(actorEntity), std::move(payload), world.tickCount());
}

void onCharacterAttributeChanged(Npc& actor,
                                 Attribute attribute,
                                 std::int32_t valueBefore,
                                 std::int32_t valueAfter,
                                 std::int32_t requestedDelta,
                                 Npc* sourceActor,
                                 const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || valueBefore == valueAfter || !shouldCapturePlayerRelated(actor, sourceActor))
    return;

  const auto delta = valueAfter - valueBefore;
  auto& world = actor.world();
  const auto attrName = attributeName(attribute);

  if(delta > 0) {
    if(actor.isPlayer() && (attribute == ATR_HITPOINTS || attribute == ATR_MANA)) {
      std::string target = attribute == ATR_HITPOINTS ? characterTargetKey("hitpoints") : characterTargetKey("mana");
      std::string payload;
      payload.reserve(640);
      payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
      payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
      payload.append(",\"character_key\":"); appendEscaped(payload, characterKey());
      payload.append(",\"target_key\":"); appendEscaped(payload, target);
      payload.append(",\"resource_key\":"); appendEscaped(payload, attrName);
      payload.append(",\"delta_amount\":"); appendInt(payload, delta);
      payload.append(",\"value_before\":"); appendInt(payload, valueBefore);
      payload.append(",\"value_after\":"); appendInt(payload, valueAfter);
      payload.append(",\"requested_delta\":"); appendInt(payload, requestedDelta);
      payload.append(",\"reason\":\"character_resource_recovered\"");
      appendWorld(payload, world);
      appendVec3(payload, "actor_position", actor.position());
      payload.push_back('}');
      submit(SemanticActionKind::ApplyCharacterResourceDelta, std::move(target), std::move(payload), world.tickCount());
      }
    return;
    }

  if(delta == 0)
    return;

  const auto amount = -delta;

  if(actor.isPlayer() && attribute == ATR_MANA) {
    std::string target = characterTargetKey("mana");
    std::string payload;
    payload.reserve(512);
    payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
    payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
    payload.append(",\"character_key\":"); appendEscaped(payload, characterKey());
    payload.append(",\"resource_key\":\"mana\"");
    payload.append(",\"mana_amount\":"); appendInt(payload, amount);
    payload.append(",\"value_before\":"); appendInt(payload, valueBefore);
    payload.append(",\"value_after\":"); appendInt(payload, valueAfter);
    payload.append(",\"requested_delta\":"); appendInt(payload, requestedDelta);
    payload.append(",\"reason\":\"resource_delta\"");
    appendWorld(payload, world);
    appendVec3(payload, "actor_position", actor.position());
    payload.push_back('}');
    submit(SemanticActionKind::ConsumeMana, std::move(target), std::move(payload), world.tickCount());
    return;
  }

  if(attribute != ATR_HITPOINTS)
    return;

  if(actor.isPlayer()) {
    std::string target = characterTargetKey("hitpoints");
    std::string payload;
    payload.reserve(1792);
    payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
    payload.append(",\"target_character_key\":"); appendEscaped(payload, characterKey());
    payload.append(",\"target_key\":"); appendEscaped(payload, characterEntityKey());
    if(sourceActor != nullptr) {
      appendNpcIdentity(payload, "source_actor", *sourceActor);
      payload.append(",\"damage_kind\":"); appendEscaped(payload, observedDamageKind(*sourceActor));
      appendCombatDamageProfile(payload, "source_actor", *sourceActor);
      payload.append(",\"critical_damage_multiplier\":");
      appendInt(payload, world.script().criticalDamageMultiplyer());
      }
    else {
      payload.append(",\"damage_kind\":\"unknown\"");
      }
    appendCombatDamageProfile(payload, "target", actor);
    payload.append(",\"gothic_game\":"); appendInt(payload, world.version().game);
    payload.append(",\"damage_amount\":"); appendInt(payload, amount);
    payload.append(",\"attribute_key\":"); appendEscaped(payload, attrName);
    payload.append(",\"value_before\":"); appendInt(payload, valueBefore);
    payload.append(",\"value_after\":"); appendInt(payload, valueAfter);
    payload.append(",\"requested_delta\":"); appendInt(payload, requestedDelta);
    payload.append(",\"reason\":\"character_damage\"");
    appendWorld(payload, world);
    appendVec3(payload, "target_position", actor.position());
    payload.push_back('}');
    submit(SemanticActionKind::ApplyCharacterDamage, std::move(target), std::move(payload), world.tickCount());
    return;
  }

  if(sourceActor == nullptr)
    return;

  auto target = npcEntityKey(world.name(), actor.persistentId(), actor.instanceSymbol());
  std::string payload;
  payload.reserve(1792);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "source_actor", *sourceActor);
  appendNpcIdentity(payload, "target_npc", actor);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"damage_kind\":"); appendEscaped(payload, observedDamageKind(*sourceActor));
  appendCombatDamageProfile(payload, "source_actor", *sourceActor);
  appendCombatDamageProfile(payload, "target", actor);
  payload.append(",\"critical_damage_multiplier\":");
  appendInt(payload, world.script().criticalDamageMultiplyer());
  payload.append(",\"gothic_game\":"); appendInt(payload, world.version().game);
  payload.append(",\"damage_amount\":"); appendInt(payload, amount);
  payload.append(",\"fatal\":"); payload.append(valueAfter <= 0 ? "true" : "false");
  payload.append(",\"attribute_key\":"); appendEscaped(payload, attrName);
  payload.append(",\"value_before\":"); appendInt(payload, valueBefore);
  payload.append(",\"value_after\":"); appendInt(payload, valueAfter);
  payload.append(",\"requested_delta\":"); appendInt(payload, requestedDelta);
  payload.append(",\"reason\":");
  appendEscaped(payload, sourceActor->isPlayer() ? "player_world_entity_damage" : "world_ai_entity_damage");
  appendWorld(payload, world);
  appendVec3(payload, "target_position", actor.position());
  payload.push_back('}');
  submit(SemanticActionKind::ApplyWorldEntityDamage, std::move(target), std::move(payload), world.tickCount());
}

} // namespace Mmo::Hooks::Detail
