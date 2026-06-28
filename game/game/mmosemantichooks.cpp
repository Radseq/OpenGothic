#include "mmosemantichooks.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <string_view>

#include "mmosemanticactionsink.h"
#include "world/world.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "commandline.h"

namespace Mmo::Hooks {
namespace {

void appendUInt(std::string& out, std::uint64_t v) {
  char buf[32] = {};
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  if(ec == std::errc{})
    out.append(buf, ptr);
  else
    out.push_back('0');
}

void appendInt(std::string& out, std::int64_t v) {
  char buf[32] = {};
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  if(ec == std::errc{})
    out.append(buf, ptr);
  else
    out.push_back('0');
}

void appendFloat(std::string& out, float v) {
  char buf[48] = {};
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  if(ec == std::errc{})
    out.append(buf, ptr);
  else
    out.push_back('0');
}

void appendBool(std::string& out, bool v) {
  out.append(v ? "true" : "false");
}

void appendEscaped(std::string& out, std::string_view v) {
  out.push_back('"');
  for(char ch : v) {
    switch(ch) {
      case '\\': out.append("\\\\"); break;
      case '"':  out.append("\\\""); break;
      case '\n': out.append("\\n");  break;
      case '\r': out.append("\\r");  break;
      case '\t': out.append("\\t");  break;
      default:
        if(static_cast<unsigned char>(ch) < 0x20)
          out.push_back(' ');
        else
          out.push_back(ch);
        break;
      }
    }
  out.push_back('"');
}

std::string actorKey(const Npc& npc) {
  std::string out = npc.isPlayer() ? "character:PC_HERO" : "npc:";
  if(!npc.isPlayer())
    appendUInt(out, npc.persistentId());
  out.append(":sym:");
  appendUInt(out, npc.instanceSymbol());
  return out;
}

std::string worldItemKey(std::string_view worldName, std::uint32_t persistentId, std::size_t symbol) {
  std::string out = "world-item:";
  out.append(worldName);
  out.append(":pid:");
  appendUInt(out, persistentId);
  out.append(":sym:");
  appendUInt(out, symbol);
  return out;
}

std::string itemTemplateKey(std::size_t symbol) {
  std::string out = "item-template:";
  appendUInt(out, symbol);
  return out;
}

std::string scriptKey(std::size_t symbolIndex, std::uint16_t valueIndex) {
  std::string out = "script-int:";
  appendUInt(out, symbolIndex);
  out.push_back(':');
  appendUInt(out, valueIndex);
  return out;
}

std::string symbolKey(const char* prefix, std::size_t symbolIndex) {
  std::string out = prefix;
  out.push_back(':');
  appendUInt(out, symbolIndex);
  return out;
}

void appendScriptContext(std::string& out, std::uint32_t scriptFunctionSymbol, std::string_view scriptFunctionName) {
  out.append(",\"script_function_symbol\":");
  appendUInt(out, scriptFunctionSymbol);
  out.append(",\"script_function_name\":");
  appendEscaped(out, scriptFunctionName);
}

void appendWorld(std::string& out, const World& world) {
  out.append(",\"world\":");
  appendEscaped(out, world.name());
  out.append(",\"client_tick\":");
  appendUInt(out, world.tickCount());
}

void appendVec3(std::string& out, const char* key, const Tempest::Vec3& v) {
  out.append(",\"");
  out.append(key);
  out.append("\":{");
  out.append("\"x\":"); appendFloat(out, v.x);
  out.append(",\"y\":"); appendFloat(out, v.y);
  out.append(",\"z\":"); appendFloat(out, v.z);
  out.push_back('}');
}

SemanticActionEnvelope makeEnvelope(SemanticActionKind kind,
                                    std::string targetKey,
                                    std::string payload,
                                    std::uint64_t tick) {
  SemanticActionEnvelope e;
  e.kind = kind;
  e.localSequence = nextSemanticActionSequence();
  e.clientTick = tick;
  e.targetKey = std::move(targetKey);
  e.idempotencyKey = makeIdempotencyKey(semanticActionSessionKey(), e.localSequence, kind, e.targetKey);
  e.payloadJson = std::move(payload);
  return e;
}

bool isLiveWorldTick(const World& world) noexcept {
  // tickCount()==0 is used heavily while loading/restoring bootstrap state.
  // Those mutations are state materialization, not accepted gameplay intents.
  return world.tickCount() != 0;
}

bool shouldCapturePlayerAction(Npc& actor) noexcept {
  if(!actor.isPlayer())
    return false;
  return isLiveWorldTick(actor.world());
}

bool shouldCapturePlayerAction(Npc* actor) noexcept {
  if(actor == nullptr || !actor->isPlayer())
    return false;
  return isLiveWorldTick(actor->world());
}

bool shouldCaptureTransfer(const World& world, const Npc* sourceNpc) noexcept {
  if(!isLiveWorldTick(world))
    return false;
  // Source NPC context is all the current generic Inventory::transfer hook can see.
  // Non-player NPC transfers during routines/trade/bootstrap are not client intents.
  // Null source is kept for chest/world-container transfers until a richer owner
  // envelope is added at the Npc/Interactive boundary.
  return sourceNpc == nullptr || sourceNpc->isPlayer();
}


std::string npcEntityKey(std::string_view worldName, std::uint32_t persistentId, std::size_t symbol) {
  std::string out = "npc:";
  out.append(worldName);
  out.append(":pid:");
  appendUInt(out, persistentId);
  out.append(":sym:");
  appendUInt(out, symbol);
  return out;
}

std::string attributeName(Attribute a) {
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

bool shouldCapturePlayerRelated(Npc& actor, Npc* other = nullptr) noexcept {
  if(!isLiveWorldTick(actor.world()))
    return false;
  if(actor.isPlayer())
    return true;
  return other != nullptr && other->isPlayer();
}

void appendNpcIdentity(std::string& out, const char* prefix, Npc& npc) {
  out.append(",\"");
  out.append(prefix);
  out.append("_key\":");
  appendEscaped(out, actorKey(npc));
  out.append(",\"");
  out.append(prefix);
  out.append("_entity_key\":");
  appendEscaped(out, npcEntityKey(npc.world().name(), npc.persistentId(), npc.instanceSymbol()));
  out.append(",\"");
  out.append(prefix);
  out.append("_symbol\":");
  appendUInt(out, npc.instanceSymbol());
  out.append(",\"");
  out.append(prefix);
  out.append("_persistent_id\":");
  appendUInt(out, npc.persistentId());
  out.append(",\"");
  out.append(prefix);
  out.append("_display_name\":");
  appendEscaped(out, npc.displayName());
}

void submit(SemanticActionKind kind, std::string targetKey, std::string payload, std::uint64_t tick) noexcept {
  if(!isSemanticActionCaptureEnabled())
    return;
  (void)submitSemanticAction(makeEnvelope(kind, std::move(targetKey), std::move(payload), tick));
}

} // namespace

bool shouldCaptureScriptAction(Npc* actor) noexcept {
  return isSemanticActionCaptureEnabled() && shouldCapturePlayerAction(actor);
}


void onCharacterMovementProposal(Npc& actor,
                                 std::uint64_t fromTick,
                                 float fromX,
                                 float fromY,
                                 float fromZ,
                                 float fromYaw,
                                 std::int32_t fromHealthCurrent,
                                 std::int32_t fromHealthMax,
                                 std::int32_t fromManaCurrent,
                                 std::int32_t fromManaMax,
                                 bool fromInAir,
                                 bool fromFalling,
                                 bool fromFallingDeep,
                                 bool fromSlide,
                                 bool fromJump,
                                 bool fromJumpUp,
                                 bool fromSwim,
                                 bool fromDive,
                                 bool fromInWater,
                                 const char* sourceLocation,
                                 const char* reason) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor))
    return;

  auto& world = actor.world();
  auto target = std::string("character:PC_HERO:movement-proposal");
  const auto pos = actor.position();
  const auto* wp = actor.currentWayPoint();
  const auto& cmd = CommandLine::inst();
  const std::uint64_t toTick = world.tickCount();
  const std::uint64_t deltaTick = toTick >= fromTick ? toTick - fromTick : 0;

  std::string payload;
  payload.reserve(1536);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"character_key\":\"PC_HERO\"");
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"proposal_version\":1");
  payload.append(",\"input_model\":\"checkpoint_delta_v1\"");
  payload.append(",\"movement_intent\":\"delta_transform\"");
  payload.append(",\"from_tick\":"); appendUInt(payload, fromTick);
  payload.append(",\"to_tick\":"); appendUInt(payload, toTick);
  payload.append(",\"delta_ms\":"); appendUInt(payload, deltaTick);
  payload.append(",\"from_pos_x\":"); appendFloat(payload, fromX);
  payload.append(",\"from_pos_y\":"); appendFloat(payload, fromY);
  payload.append(",\"from_pos_z\":"); appendFloat(payload, fromZ);
  payload.append(",\"to_pos_x\":"); appendFloat(payload, pos.x);
  payload.append(",\"to_pos_y\":"); appendFloat(payload, pos.y);
  payload.append(",\"to_pos_z\":"); appendFloat(payload, pos.z);
  payload.append(",\"from_rotation_yaw\":"); appendFloat(payload, fromYaw);
  payload.append(",\"to_rotation_yaw\":"); appendFloat(payload, actor.rotationY());
  payload.append(",\"vertical_axis\":\"y\"");
  payload.append(",\"from_health_current\":"); appendInt(payload, fromHealthCurrent);
  payload.append(",\"from_health_max\":"); appendInt(payload, fromHealthMax);
  payload.append(",\"from_mana_current\":"); appendInt(payload, fromManaCurrent);
  payload.append(",\"from_mana_max\":"); appendInt(payload, fromManaMax);
  payload.append(",\"from_is_in_air\":"); appendBool(payload, fromInAir);
  payload.append(",\"from_is_falling\":"); appendBool(payload, fromFalling);
  payload.append(",\"from_is_falling_deep\":"); appendBool(payload, fromFallingDeep);
  payload.append(",\"from_is_slide\":"); appendBool(payload, fromSlide);
  payload.append(",\"from_is_jump\":"); appendBool(payload, fromJump);
  payload.append(",\"from_is_jump_up\":"); appendBool(payload, fromJumpUp);
  payload.append(",\"from_is_swim\":"); appendBool(payload, fromSwim);
  payload.append(",\"from_is_dive\":"); appendBool(payload, fromDive);
  payload.append(",\"from_is_in_water\":"); appendBool(payload, fromInWater);
  payload.append(",\"to_is_in_air\":"); appendBool(payload, actor.isInAir());
  payload.append(",\"to_is_falling\":"); appendBool(payload, actor.isFalling());
  payload.append(",\"to_is_falling_deep\":"); appendBool(payload, actor.isFallingDeep());
  payload.append(",\"to_is_slide\":"); appendBool(payload, actor.isSlide());
  payload.append(",\"to_is_jump\":"); appendBool(payload, actor.isJump());
  payload.append(",\"to_is_jump_up\":"); appendBool(payload, actor.isJumpUp());
  payload.append(",\"to_is_swim\":"); appendBool(payload, actor.isSwim());
  payload.append(",\"to_is_dive\":"); appendBool(payload, actor.isDive());
  payload.append(",\"to_is_in_water\":"); appendBool(payload, actor.isInWater());
  payload.append(",\"current_waypoint_key\":"); appendEscaped(payload, wp != nullptr ? std::string_view(wp->name) : std::string_view{});
  payload.append(",\"level\":"); appendInt(payload, actor.level());
  payload.append(",\"experience\":"); appendInt(payload, actor.experience());
  payload.append(",\"experience_next\":"); appendInt(payload, actor.experienceNext());
  payload.append(",\"learning_points\":"); appendInt(payload, actor.learningPoints());
  payload.append(",\"health_current\":"); appendInt(payload, actor.attribute(ATR_HITPOINTS));
  payload.append(",\"health_max\":"); appendInt(payload, actor.attribute(ATR_HITPOINTSMAX));
  payload.append(",\"mana_current\":"); appendInt(payload, actor.attribute(ATR_MANA));
  payload.append(",\"mana_max\":"); appendInt(payload, actor.attribute(ATR_MANAMAX));
  payload.append(",\"strength\":"); appendInt(payload, actor.attribute(ATR_STRENGTH));
  payload.append(",\"dexterity\":"); appendInt(payload, actor.attribute(ATR_DEXTERITY));
  payload.append(",\"guild\":"); appendInt(payload, actor.guild());
  payload.append(",\"true_guild\":"); appendInt(payload, actor.trueGuild());
  payload.append(",\"permanent_attitude\":"); appendInt(payload, static_cast<std::int32_t>(actor.attitude()));
  payload.append(",\"temporary_attitude\":"); appendInt(payload, static_cast<std::int32_t>(actor.tempAttitude()));
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("movement_delta_proposal"));
  payload.append(",\"proposal_interval_ms\":"); appendUInt(payload, cmd.mmoActionMovementProposalIntervalMs());
  payload.append(",\"proposal_min_distance\":"); appendFloat(payload, cmd.mmoActionMovementProposalMinDistance());
  payload.append(",\"proposal_min_yaw_deg\":"); appendFloat(payload, cmd.mmoActionMovementProposalMinYawDeg());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", pos);
  payload.push_back('}');

  submit(SemanticActionKind::MovementProposal, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterCheckpoint(Npc& actor,
                           const char* sourceLocation,
                           const char* reason) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor))
    return;

  auto& world = actor.world();
  auto target = std::string("character:PC_HERO:checkpoint");
  const auto pos = actor.position();
  const auto* wp = actor.currentWayPoint();

  std::string payload;
  payload.reserve(1152);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"character_key\":\"PC_HERO\"");
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"pos_x\":"); appendFloat(payload, pos.x);
  payload.append(",\"pos_y\":"); appendFloat(payload, pos.y);
  payload.append(",\"pos_z\":"); appendFloat(payload, pos.z);
  payload.append(",\"rotation_yaw\":"); appendFloat(payload, actor.rotationY());
  payload.append(",\"current_waypoint_key\":"); appendEscaped(payload, wp != nullptr ? std::string_view(wp->name) : std::string_view{});
  payload.append(",\"level\":"); appendInt(payload, actor.level());
  payload.append(",\"experience\":"); appendInt(payload, actor.experience());
  payload.append(",\"experience_next\":"); appendInt(payload, actor.experienceNext());
  payload.append(",\"learning_points\":"); appendInt(payload, actor.learningPoints());
  payload.append(",\"health_current\":"); appendInt(payload, actor.attribute(ATR_HITPOINTS));
  payload.append(",\"health_max\":"); appendInt(payload, actor.attribute(ATR_HITPOINTSMAX));
  payload.append(",\"mana_current\":"); appendInt(payload, actor.attribute(ATR_MANA));
  payload.append(",\"mana_max\":"); appendInt(payload, actor.attribute(ATR_MANAMAX));
  payload.append(",\"strength\":"); appendInt(payload, actor.attribute(ATR_STRENGTH));
  payload.append(",\"dexterity\":"); appendInt(payload, actor.attribute(ATR_DEXTERITY));
  payload.append(",\"guild\":"); appendInt(payload, actor.guild());
  payload.append(",\"true_guild\":"); appendInt(payload, actor.trueGuild());
  payload.append(",\"permanent_attitude\":"); appendInt(payload, static_cast<std::int32_t>(actor.attitude()));
  payload.append(",\"temporary_attitude\":"); appendInt(payload, static_cast<std::int32_t>(actor.tempAttitude()));
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("step39_periodic_movement_checkpoint"));
  const auto& cmd = CommandLine::inst();
  payload.append(",\"checkpoint_interval_ms\":"); appendUInt(payload, cmd.mmoActionCheckpointIntervalMs());
  payload.append(",\"checkpoint_min_distance\":"); appendFloat(payload, cmd.mmoActionCheckpointMinDistance());
  payload.append(",\"checkpoint_min_yaw_deg\":"); appendFloat(payload, cmd.mmoActionCheckpointMinYawDeg());
  payload.append(",\"checkpoint_force_interval_ms\":"); appendUInt(payload, cmd.mmoActionCheckpointForceIntervalMs());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", pos);
  payload.push_back('}');

  submit(SemanticActionKind::CharacterCheckpoint, std::move(target), std::move(payload), world.tickCount());
}

void onWorldItemPickedUp(Npc& actor,
                         const Item& inventoryItem,
                         std::uint32_t sourceWorldItemPersistentId,
                         std::size_t sourceItemSymbol,
                         std::size_t sourceAmount,
                         const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = worldItemKey(world.name(), sourceWorldItemPersistentId, sourceItemSymbol);

  std::string payload;
  payload.reserve(512);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(sourceItemSymbol));
  payload.append(",\"source_world_item_persistent_id\":"); appendUInt(payload, sourceWorldItemPersistentId);
  payload.append(",\"item_symbol\":"); appendUInt(payload, sourceItemSymbol);
  payload.append(",\"inventory_item_symbol\":"); appendUInt(payload, inventoryItem.clsId());
  payload.append(",\"amount\":"); appendUInt(payload, sourceAmount);
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::PickupWorldItem, std::move(target), std::move(payload), world.tickCount());
}

void onWorldItemRemoved(World& world,
                        const Item& worldItem,
                        const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || !isLiveWorldTick(world))
    return;
  auto target = worldItemKey(world.name(), worldItem.persistentId(), worldItem.clsId());

  std::string payload;
  payload.reserve(384);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(worldItem.clsId()));
  payload.append(",\"source_world_item_persistent_id\":"); appendUInt(payload, worldItem.persistentId());
  payload.append(",\"item_symbol\":"); appendUInt(payload, worldItem.clsId());
  payload.append(",\"amount\":"); appendUInt(payload, worldItem.count());
  appendWorld(payload, world);
  appendVec3(payload, "item_position", worldItem.position());
  payload.push_back('}');

  submit(SemanticActionKind::RemoveWorldItem, std::move(target), std::move(payload), world.tickCount());
}

void onInventoryTransfer(World& world,
                         const Npc* sourceNpc,
                         std::size_t itemSymbol,
                         std::uint32_t sourceItemPersistentId,
                         std::size_t amount,
                         bool movedWholeInstance,
                         const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || amount == 0 || !shouldCaptureTransfer(world, sourceNpc))
    return;
  std::string target = itemTemplateKey(itemSymbol);
  target.append(":transfer:");
  appendUInt(target, sourceItemPersistentId);

  std::string payload;
  payload.reserve(448);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  if(sourceNpc != nullptr) {
    payload.append(",\"source_actor_key\":"); appendEscaped(payload, actorKey(*sourceNpc));
    payload.append(",\"source_actor_symbol\":"); appendUInt(payload, sourceNpc->instanceSymbol());
    payload.append(",\"source_actor_persistent_id\":"); appendUInt(payload, sourceNpc->persistentId());
    }
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"source_item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"moved_whole_instance\":"); payload.append(movedWholeInstance ? "true" : "false");
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::TransferCharacterItem, std::move(target), std::move(payload), world.tickCount());
}

void onItemEquipped(Npc& actor,
                    const Item& item,
                    std::uint8_t slot,
                    const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = itemTemplateKey(item.clsId());
  target.append(":equip:");
  appendUInt(target, actor.persistentId());
  target.push_back(':');
  appendUInt(target, slot);

  std::string payload;
  payload.reserve(448);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(item.clsId()));
  payload.append(",\"item_symbol\":"); appendUInt(payload, item.clsId());
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, item.persistentId());
  payload.append(",\"slot\":"); appendUInt(payload, slot);
  payload.append(",\"amount\":"); appendUInt(payload, item.count());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::EquipCharacterItem, std::move(target), std::move(payload), world.tickCount());
}

void onItemUnequipped(Npc& actor,
                      const Item& item,
                      std::uint8_t slot,
                      const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = itemTemplateKey(item.clsId());
  target.append(":unequip:");
  appendUInt(target, actor.persistentId());
  target.push_back(':');
  appendUInt(target, slot);

  std::string payload;
  payload.reserve(448);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(item.clsId()));
  payload.append(",\"item_symbol\":"); appendUInt(payload, item.clsId());
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, item.persistentId());
  payload.append(",\"slot\":"); appendUInt(payload, slot);
  payload.append(",\"amount\":"); appendUInt(payload, item.count());
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::UnequipCharacterItem, std::move(target), std::move(payload), world.tickCount());
}


void onTradeBuyFromNpc(Npc& buyer,
                       Npc& vendor,
                       std::size_t itemSymbol,
                       std::uint32_t vendorItemPersistentId,
                       std::size_t amount,
                       std::int32_t unitPrice,
                       std::size_t goldBefore,
                       std::size_t goldAfter,
                       const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || amount == 0 || !shouldCapturePlayerAction(buyer))
    return;
  auto& world = buyer.world();
  auto target = itemTemplateKey(itemSymbol);
  target.append(":buy-from:");
  appendUInt(target, vendor.persistentId());
  target.push_back(':');
  appendUInt(target, vendorItemPersistentId);

  const auto totalPrice = static_cast<std::int64_t>(unitPrice) * static_cast<std::int64_t>(amount);
  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "buyer", buyer);
  appendNpcIdentity(payload, "npc", vendor);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(buyer));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"vendor_item_persistent_id\":"); appendUInt(payload, vendorItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"unit_price\":"); appendInt(payload, unitPrice);
  payload.append(",\"price_total\":"); appendInt(payload, totalPrice);
  payload.append(",\"currency_key\":\"g2notr:gold\"");
  payload.append(",\"wallet_before\":"); appendUInt(payload, goldBefore);
  payload.append(",\"wallet_after\":"); appendUInt(payload, goldAfter);
  payload.append(",\"reason\":\"trade_buy_from_npc\"");
  appendWorld(payload, world);
  appendVec3(payload, "buyer_position", buyer.position());
  payload.push_back('}');

  submit(SemanticActionKind::TradeBuyFromNpc, std::move(target), std::move(payload), world.tickCount());
}

void onTradeSellToNpc(Npc& seller,
                      Npc& buyer,
                      std::size_t itemSymbol,
                      std::uint32_t sellerItemPersistentId,
                      std::size_t amount,
                      std::int32_t unitPrice,
                      std::size_t goldBefore,
                      std::size_t goldAfter,
                      const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || amount == 0 || !shouldCapturePlayerAction(seller))
    return;
  auto& world = seller.world();
  auto target = itemTemplateKey(itemSymbol);
  target.append(":sell-to:");
  appendUInt(target, buyer.persistentId());
  target.push_back(':');
  appendUInt(target, sellerItemPersistentId);

  const auto totalPrice = static_cast<std::int64_t>(unitPrice) * static_cast<std::int64_t>(amount);
  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "seller", seller);
  appendNpcIdentity(payload, "npc", buyer);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(seller));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"seller_item_persistent_id\":"); appendUInt(payload, sellerItemPersistentId);
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, sellerItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"unit_price\":"); appendInt(payload, unitPrice);
  payload.append(",\"price_total\":"); appendInt(payload, totalPrice);
  payload.append(",\"currency_key\":\"g2notr:gold\"");
  payload.append(",\"wallet_before\":"); appendUInt(payload, goldBefore);
  payload.append(",\"wallet_after\":"); appendUInt(payload, goldAfter);
  payload.append(",\"reason\":\"trade_sell_to_npc\"");
  appendWorld(payload, world);
  appendVec3(payload, "seller_position", seller.position());
  payload.push_back('}');

  submit(SemanticActionKind::TradeSellToNpc, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterItemConsumed(Npc& actor,
                             std::size_t itemSymbol,
                             std::uint32_t itemPersistentId,
                             std::size_t amount,
                             std::string_view reason,
                             const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || amount == 0 || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = itemTemplateKey(itemSymbol);
  target.append(":consume:");
  appendUInt(target, itemPersistentId);

  std::string payload;
  payload.reserve(512);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, itemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"reason\":"); appendEscaped(payload, reason);
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::ConsumeItem, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterAttributeChanged(Npc& actor,
                                 Attribute attribute,
                                 std::int32_t valueBefore,
                                 std::int32_t valueAfter,
                                 std::int32_t requestedDelta,
                                 Npc* sourceActor,
                                 const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || valueBefore == valueAfter || !shouldCapturePlayerRelated(actor, sourceActor))
    return;

  const auto delta = valueAfter - valueBefore;
  if(delta >= 0)
    return;

  auto& world = actor.world();
  const auto amount = -delta;
  const auto attrName = attributeName(attribute);

  if(actor.isPlayer() && attribute == ATR_MANA) {
    std::string target = "character:PC_HERO:mana";
    std::string payload;
    payload.reserve(512);
    payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
    payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
    payload.append(",\"character_key\":\"PC_HERO\"");
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
    std::string target = "character:PC_HERO:hitpoints";
    std::string payload;
    payload.reserve(640);
    payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
    payload.append(",\"target_character_key\":\"PC_HERO\"");
    payload.append(",\"target_key\":\"character:PC_HERO\"");
    if(sourceActor != nullptr)
      appendNpcIdentity(payload, "source_actor", *sourceActor);
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

  if(sourceActor == nullptr || !sourceActor->isPlayer())
    return;

  auto target = npcEntityKey(world.name(), actor.persistentId(), actor.instanceSymbol());
  std::string payload;
  payload.reserve(704);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "source_actor", *sourceActor);
  appendNpcIdentity(payload, "target_npc", actor);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"damage_amount\":"); appendInt(payload, amount);
  payload.append(",\"fatal\":"); payload.append(valueAfter <= 0 ? "true" : "false");
  payload.append(",\"attribute_key\":"); appendEscaped(payload, attrName);
  payload.append(",\"value_before\":"); appendInt(payload, valueBefore);
  payload.append(",\"value_after\":"); appendInt(payload, valueAfter);
  payload.append(",\"requested_delta\":"); appendInt(payload, requestedDelta);
  payload.append(",\"reason\":\"world_entity_damage\"");
  appendWorld(payload, world);
  appendVec3(payload, "target_position", actor.position());
  payload.push_back('}');
  submit(SemanticActionKind::ApplyWorldEntityDamage, std::move(target), std::move(payload), world.tickCount());
}

void onNpcLifecycleChanged(Npc& actor,
                           Npc* sourceActor,
                           bool dead,
                           bool unconscious,
                           const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || actor.isPlayer() || !dead || !shouldCapturePlayerRelated(actor, sourceActor))
    return;
  auto& world = actor.world();
  auto target = npcEntityKey(world.name(), actor.persistentId(), actor.instanceSymbol());

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  if(sourceActor != nullptr)
    appendNpcIdentity(payload, "source_actor", *sourceActor);
  appendNpcIdentity(payload, "target_npc", actor);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"dead\":"); payload.append(dead ? "true" : "false");
  payload.append(",\"unconscious\":"); payload.append(unconscious ? "true" : "false");
  payload.append(",\"reason\":\"npc_no_health\"");
  appendWorld(payload, world);
  appendVec3(payload, "target_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::MarkNpcDead, std::move(target), std::move(payload), world.tickCount());
}

void onScriptIntChanged(Npc& actor,
                        std::uint32_t scriptFunctionSymbol,
                        std::string_view scriptFunctionName,
                        std::size_t symbolIndex,
                        std::uint16_t valueIndex,
                        std::string_view symbolName,
                        std::int32_t valueBefore,
                        std::int32_t valueAfter,
                        const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor) || valueBefore == valueAfter)
    return;
  auto& world = actor.world();
  auto target = scriptKey(symbolIndex, valueIndex);

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  appendScriptContext(payload, scriptFunctionSymbol, scriptFunctionName);
  payload.append(",\"script_key\":"); appendEscaped(payload, target);
  payload.append(",\"global_key\":"); appendEscaped(payload, target);
  payload.append(",\"symbol_name\":"); appendEscaped(payload, symbolName);
  payload.append(",\"symbol_index\":"); appendUInt(payload, symbolIndex);
  payload.append(",\"value_index\":"); appendUInt(payload, valueIndex);
  payload.append(",\"value_before\":"); appendInt(payload, static_cast<std::int64_t>(valueBefore));
  payload.append(",\"value_after\":"); appendInt(payload, static_cast<std::int64_t>(valueAfter));
  payload.append(",\"reason\":\"script_int_changed\"");
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::SetScriptInt, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterProgressionChanged(Npc& actor,
                                   std::uint32_t scriptFunctionSymbol,
                                   std::string_view scriptFunctionName,
                                   std::int32_t levelBefore,
                                   std::int32_t levelAfter,
                                   std::int32_t experienceBefore,
                                   std::int32_t experienceAfter,
                                   std::int32_t experienceNextBefore,
                                   std::int32_t experienceNextAfter,
                                   std::int32_t learningPointsBefore,
                                   std::int32_t learningPointsAfter,
                                   const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor))
    return;
  const auto experienceDelta = experienceAfter - experienceBefore;
  const auto learningPointsDelta = learningPointsAfter - learningPointsBefore;
  const auto levelDelta = levelAfter - levelBefore;
  if(experienceDelta == 0 && learningPointsDelta == 0 && levelDelta == 0 && experienceNextAfter == experienceNextBefore)
    return;

  auto& world = actor.world();
  std::string target = "character:PC_HERO:progression";

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"character_key\":\"PC_HERO\"");
  appendScriptContext(payload, scriptFunctionSymbol, scriptFunctionName);
  payload.append(",\"level_before\":"); appendInt(payload, static_cast<std::int64_t>(levelBefore));
  payload.append(",\"level_after\":"); appendInt(payload, static_cast<std::int64_t>(levelAfter));
  payload.append(",\"level_delta\":"); appendInt(payload, static_cast<std::int64_t>(levelDelta));
  payload.append(",\"experience_before\":"); appendInt(payload, static_cast<std::int64_t>(experienceBefore));
  payload.append(",\"experience_after\":"); appendInt(payload, static_cast<std::int64_t>(experienceAfter));
  payload.append(",\"experience_delta\":"); appendInt(payload, static_cast<std::int64_t>(experienceDelta));
  payload.append(",\"experience_next_before\":"); appendInt(payload, static_cast<std::int64_t>(experienceNextBefore));
  payload.append(",\"experience_next_after\":"); appendInt(payload, static_cast<std::int64_t>(experienceNextAfter));
  payload.append(",\"learning_points_before\":"); appendInt(payload, static_cast<std::int64_t>(learningPointsBefore));
  payload.append(",\"learning_points_after\":"); appendInt(payload, static_cast<std::int64_t>(learningPointsAfter));
  payload.append(",\"learning_points_delta\":"); appendInt(payload, static_cast<std::int64_t>(learningPointsDelta));
  payload.append(",\"reason\":\"script_progression\"");
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::AdjustProgression, std::move(target), std::move(payload), world.tickCount());
}

void onKnownDialogChanged(Npc& actor,
                          std::uint32_t scriptFunctionSymbol,
                          std::string_view scriptFunctionName,
                          std::size_t npcSymbol,
                          std::string_view npcSymbolName,
                          std::size_t infoSymbol,
                          std::string_view infoSymbolName,
                          bool known,
                          const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = symbolKey("dialog-info", infoSymbol);
  auto npcKey = symbolKey("npc-symbol", npcSymbol);

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  appendScriptContext(payload, scriptFunctionSymbol, scriptFunctionName);
  payload.append(",\"npc_key\":"); appendEscaped(payload, npcKey);
  payload.append(",\"npc_symbol_name\":"); appendEscaped(payload, npcSymbolName);
  payload.append(",\"npc_symbol\":"); appendUInt(payload, npcSymbol);
  payload.append(",\"info_key\":"); appendEscaped(payload, target);
  payload.append(",\"info_symbol_name\":"); appendEscaped(payload, infoSymbolName);
  payload.append(",\"info_symbol\":"); appendUInt(payload, infoSymbol);
  payload.append(",\"known\":"); payload.append(known ? "true" : "false");
  payload.append(",\"removed\":false");
  payload.append(",\"reason\":\"script_dialog_known\"");
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::SetKnownDialog, std::move(target), std::move(payload), world.tickCount());
}

void onQuestChanged(Npc& actor,
                    std::uint32_t scriptFunctionSymbol,
                    std::string_view scriptFunctionName,
                    std::string_view questKey,
                    std::string_view status,
                    std::size_t entryCount,
                    const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor) || questKey.empty())
    return;
  auto& world = actor.world();
  std::string target = "quest:";
  target.append(questKey);

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  appendScriptContext(payload, scriptFunctionSymbol, scriptFunctionName);
  payload.append(",\"quest_key\":"); appendEscaped(payload, questKey);
  payload.append(",\"quest_name\":"); appendEscaped(payload, questKey);
  payload.append(",\"status\":"); appendEscaped(payload, status);
  payload.append(",\"entry_count\":"); appendUInt(payload, entryCount);
  payload.append(",\"entries\":[]");
  payload.append(",\"reason\":\"script_quest_changed\"");
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::UpdateQuest, std::move(target), std::move(payload), world.tickCount());
}

} // namespace Mmo::Hooks









