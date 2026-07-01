#include "mmosemantichooks.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <string_view>

#include "mmosemanticactionsink.h"
#include "world/world.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "commandline.h"

namespace Mmo::Hooks {
namespace {

thread_local unsigned captureSuppressionDepth = 0;

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

void appendHex2(std::string& out, unsigned char v) {
  constexpr char hex[] = "0123456789ABCDEF";
  out.push_back(hex[(v >> 4) & 0x0F]);
  out.push_back(hex[v & 0x0F]);
}

void appendEscaped(std::string& out, std::string_view v) {
  // Semantic envelopes are sent over UDP as UTF-8 JSON. Gothic/Zen labels can
  // still contain legacy single-byte text, so raw bytes >=0x80 are escaped.
  // Stable identity is numeric/key-based; display names are diagnostic labels.
  out.push_back('"');
  for(unsigned char ch : v) {
    switch(ch) {
      case '\\': out.append("\\\\"); break;
      case '"':  out.append("\\\""); break;
      case '\n': out.append("\\n");  break;
      case '\r': out.append("\\r");  break;
      case '\t': out.append("\\t");  break;
      default:
        if(ch < 0x20) {
          out.push_back(' ');
          }
        else if(ch < 0x80) {
          out.push_back(static_cast<char>(ch));
          }
        else {
          out.append("\\u00");
          appendHex2(out, ch);
          }
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

std::string playerOrDefaultKey(const World& world) {
  if(const auto* player = world.player())
    return actorKey(*player);
  return "character:PC_HERO";
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

std::string interactiveEntityKey(World& world, Interactive& interactive) {
  const std::uint32_t slotId = world.mobsiId(&interactive);
  std::string focus = std::string(interactive.focusName());
  if(focus.empty())
    focus = std::string(interactive.tag());
  if(focus.empty()) {
    focus = "mobsi:";
    appendUInt(focus, interactive.getId());
    }

  std::string out = "mobsi:";
  out.append(world.name());
  out.push_back(':');
  appendUInt(out, slotId);
  out.push_back(':');
  appendUInt(out, interactive.getId());
  out.push_back(':');
  out.append(focus);
  return out;
}

std::string triggerEntityKey(World& world, std::uint32_t vobId, std::string_view name) {
  std::string out = "trigger:";
  out.append(world.name());
  out.push_back(':');
  appendUInt(out, vobId);
  out.push_back(':');
  if(name.empty()) {
    out.append("trigger:");
    appendUInt(out, vobId);
    }
  else {
    out.append(name);
    }
  return out;
}

std::string moverEntityKey(World& world, std::uint32_t vobId, std::string_view name) {
  std::string out = "mover:";
  out.append(world.name());
  out.push_back(':');
  appendUInt(out, vobId);
  out.push_back(':');
  if(name.empty()) {
    out.append("mover:");
    appendUInt(out, vobId);
    }
  else {
    out.append(name);
    }
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
  (void)world;
  (void)sourceNpc;
  // Inventory::transfer is too generic to be a server intent: the same call path
  // covers container loot, trade, NPC inventory moves and player inventory moves,
  // but it does not carry both source and target owner identities. Use the richer
  // domain hooks at Npc/Interactive boundaries instead.
  return false;
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

bool shouldCaptureWorldAiAction(Npc& actor, Npc* other = nullptr) noexcept {
  if(!isLiveWorldTick(actor.world()))
    return false;
  // Step45: weapon transitions and NPC-vs-NPC combat/death are sparse semantic
  // events. Capture them for live world actors too, because Gothic AI reacts to
  // readied weapons and non-player fights can change durable world state.
  (void)other;
  return true;
}

bool shouldCapturePlayerRelated(Npc& actor, Npc* other = nullptr) noexcept {
  if(!isLiveWorldTick(actor.world()))
    return false;
  if(actor.isPlayer())
    return true;
  if(other != nullptr && other->isPlayer())
    return true;
  return shouldCaptureWorldAiAction(actor, other);
}

struct WeaponStateCacheEntry final {
  bool occupied = false;
  bool player = false;
  std::uint32_t persistentId = 0;
  std::size_t symbol = 0;
  WeaponState state = WeaponState::NoWeapon;
  std::uint64_t lastTick = 0;
};

std::array<WeaponStateCacheEntry, 256> weaponStateCache = {};
std::size_t weaponStateCacheCursor = 0;

bool sameWeaponActor(const WeaponStateCacheEntry& e, const Npc& actor) noexcept {
  return e.occupied &&
         e.player == actor.isPlayer() &&
         e.persistentId == actor.persistentId() &&
         e.symbol == actor.instanceSymbol();
}

bool shouldEmitWeaponState(Npc& actor, WeaponState previousState, WeaponState newState) noexcept {
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

struct InteractiveStateCacheEntry final {
  bool occupied = false;
  std::uint32_t vobId = 0;
  std::uint32_t slotId = 0;
  std::int32_t state = 0;
  bool locked = false;
  bool cracked = false;
  std::uint64_t lastTick = 0;
};

struct InteractivePlayerUseEntry final {
  bool occupied = false;
  std::uint32_t vobId = 0;
  std::uint32_t slotId = 0;
  std::uint64_t tick = 0;
};

enum class InteractiveStateCaptureCause : std::uint8_t {
  None,
  DirectPlayerActor,
  RecentPlayerInteractiveUse,
  RecentPlayerWorldInteraction,
};

constexpr std::uint64_t RecentInteractiveSameTargetTicks = 60000;
constexpr std::uint64_t RecentInteractiveWorldCauseTicks = 8000;

std::array<InteractiveStateCacheEntry, 512> interactiveStateCache = {};
std::size_t interactiveStateCacheCursor = 0;
std::array<InteractivePlayerUseEntry, 128> interactivePlayerUseCache = {};
std::size_t interactivePlayerUseCacheCursor = 0;
std::uint64_t lastPlayerInteractiveUseTick = 0;

bool tickWithin(std::uint64_t now, std::uint64_t then, std::uint64_t limit) noexcept {
  return then != 0 && now >= then && now - then <= limit;
}

bool sameInteractive(const InteractiveStateCacheEntry& e, World& world, Interactive& interactive) noexcept {
  return e.occupied &&
         e.vobId == interactive.getId() &&
         e.slotId == world.mobsiId(&interactive);
}

bool sameInteractiveUse(const InteractivePlayerUseEntry& e, World& world, Interactive& interactive) noexcept {
  return e.occupied &&
         e.vobId == interactive.getId() &&
         e.slotId == world.mobsiId(&interactive);
}

void markPlayerInteractiveUse(World& world, Interactive& interactive) noexcept {
  const auto tick = world.tickCount();
  lastPlayerInteractiveUseTick = tick;

  InteractivePlayerUseEntry* reusable = nullptr;
  for(auto& e : interactivePlayerUseCache) {
    if(sameInteractiveUse(e, world, interactive)) {
      e.tick = tick;
      return;
      }
    if(!e.occupied && reusable == nullptr)
      reusable = &e;
    }

  if(reusable == nullptr) {
    reusable = &interactivePlayerUseCache[interactivePlayerUseCacheCursor % interactivePlayerUseCache.size()];
    ++interactivePlayerUseCacheCursor;
    }

  reusable->occupied = true;
  reusable->vobId = interactive.getId();
  reusable->slotId = world.mobsiId(&interactive);
  reusable->tick = tick;
}

bool hasRecentPlayerUseOfInteractive(World& world, Interactive& interactive) noexcept {
  const auto now = world.tickCount();
  for(const auto& e : interactivePlayerUseCache) {
    if(sameInteractiveUse(e, world, interactive) && tickWithin(now, e.tick, RecentInteractiveSameTargetTicks))
      return true;
    }
  return false;
}

bool hasRecentPlayerWorldInteraction(World& world) noexcept {
  return tickWithin(world.tickCount(), lastPlayerInteractiveUseTick, RecentInteractiveWorldCauseTicks);
}

InteractiveStateCaptureCause interactiveStateCaptureCause(World& world, Interactive& interactive, const Npc* actor) noexcept {
  if(actor != nullptr)
    return actor->isPlayer() && isLiveWorldTick(world) ? InteractiveStateCaptureCause::DirectPlayerActor : InteractiveStateCaptureCause::None;
  if(hasRecentPlayerUseOfInteractive(world, interactive))
    return InteractiveStateCaptureCause::RecentPlayerInteractiveUse;
  if(hasRecentPlayerWorldInteraction(world))
    return InteractiveStateCaptureCause::RecentPlayerWorldInteraction;
  return InteractiveStateCaptureCause::None;
}

std::string_view captureCauseName(InteractiveStateCaptureCause cause) noexcept {
  switch(cause) {
    case InteractiveStateCaptureCause::DirectPlayerActor:            return "direct_player_actor";
    case InteractiveStateCaptureCause::RecentPlayerInteractiveUse:   return "recent_player_interactive_use";
    case InteractiveStateCaptureCause::RecentPlayerWorldInteraction: return "recent_player_world_interaction";
    case InteractiveStateCaptureCause::None:                         return "none";
  }
  return "none";
}

bool shouldEmitInteractiveState(World& world,
                                Interactive& interactive,
                                std::int32_t stateAfter,
                                bool lockedAfter,
                                bool crackedAfter) noexcept {
  const auto tick = world.tickCount();
  InteractiveStateCacheEntry* reusable = nullptr;
  for(auto& e : interactiveStateCache) {
    if(sameInteractive(e, world, interactive)) {
      if(e.state == stateAfter && e.locked == lockedAfter && e.cracked == crackedAfter)
        return false;
      e.state = stateAfter;
      e.locked = lockedAfter;
      e.cracked = crackedAfter;
      e.lastTick = tick;
      return true;
      }
    if(!e.occupied && reusable == nullptr)
      reusable = &e;
    }

  if(reusable == nullptr) {
    reusable = &interactiveStateCache[interactiveStateCacheCursor % interactiveStateCache.size()];
    ++interactiveStateCacheCursor;
    }

  reusable->occupied = true;
  reusable->vobId = interactive.getId();
  reusable->slotId = world.mobsiId(&interactive);
  reusable->state = stateAfter;
  reusable->locked = lockedAfter;
  reusable->cracked = crackedAfter;
  reusable->lastTick = tick;
  return true;
}

void appendInteractiveIdentity(std::string& out, World& world, Interactive& interactive) {
  const auto target = interactiveEntityKey(world, interactive);
  out.append(",\"target_key\":");
  appendEscaped(out, target);
  out.append(",\"interactive_key\":");
  appendEscaped(out, target);
  out.append(",\"interactive_entity_key\":");
  appendEscaped(out, target);
  out.append(",\"slot_id\":");
  appendUInt(out, world.mobsiId(&interactive));
  out.append(",\"vob_id\":");
  appendUInt(out, interactive.getId());
  out.append(",\"tag\":");
  appendEscaped(out, interactive.tag());
  out.append(",\"focus_name\":");
  appendEscaped(out, interactive.focusName());
  out.append(",\"display_name\":");
  appendEscaped(out, interactive.displayName());
  out.append(",\"scheme\":");
  appendEscaped(out, interactive.schemeName());
  out.append(",\"state_count\":");
  appendInt(out, interactive.stateCount());
  out.append(",\"state_mask\":");
  appendUInt(out, interactive.stateMask());
  out.append(",\"container\":");
  appendBool(out, interactive.isContainer());
  out.append(",\"door\":");
  appendBool(out, interactive.isDoor());
  out.append(",\"ladder\":");
  appendBool(out, interactive.isLadder());
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
  if(!isSemanticActionCaptureEnabled() || isCaptureSuppressed())
    return;
  (void)submitSemanticAction(makeEnvelope(kind, std::move(targetKey), std::move(payload), tick));
}


} // namespace

ScopedCaptureSuppression::ScopedCaptureSuppression() noexcept {
  ++captureSuppressionDepth;
}

ScopedCaptureSuppression::~ScopedCaptureSuppression() {
  if(active && captureSuppressionDepth != 0)
    --captureSuppressionDepth;
}

bool isCaptureSuppressed() noexcept {
  return captureSuppressionDepth != 0;
}

void onClientBootstrapRequest(World& world,
                              const char* sourceLocation,
                              const char* reason) noexcept {
  if(!isServerBoundClientModeEnabled() || !isSemanticActionCaptureEnabled())
    return;

  const auto seq = nextSemanticActionSequence();
  const auto target = playerOrDefaultKey(world);

  std::string payload;
  payload.reserve(384);
  payload.append("{\"actor_key\":");
  payload.append(jsonEscape(target));
  payload.append(",\"character_key\":\"PC_HERO\"");
  payload.append(",\"world\":");
  payload.append(jsonEscape(world.name()));
  payload.append(",\"server_tick\":");
  appendUInt(payload, world.tickCount());
  payload.append(",\"server_bound_client_mode\":true");
  payload.append(",\"server_endpoint\":");
  payload.append(jsonEscape(CommandLine::inst().mmoServerEndpoint()));
  payload.append(",\"reason\":");
  payload.append(jsonEscape(reason ? reason : "client_bootstrap_request"));
  payload.append(",\"source_location\":");
  payload.append(jsonEscape(sourceLocation ? sourceLocation : "unknown"));
  payload.push_back('}');

  SemanticActionEnvelope env;
  env.kind = SemanticActionKind::ClientBootstrapRequest;
  env.targetKey = target;
  env.localSequence = seq;
  env.clientTick = world.tickCount();
  env.idempotencyKey = makeIdempotencyKey(semanticActionSessionKey(), seq, env.kind, env.targetKey);
  env.payloadJson = std::move(payload);
  (void)submitSemanticAction(env);
}

bool shouldCaptureScriptAction(Npc* actor) noexcept {
  return isSemanticActionCaptureEnabled() && shouldCapturePlayerAction(actor);
}

void onWorldTimeChanged(World& world,
                        gtime before,
                        gtime after,
                        const char* sourceLocation,
                        const char* reason) noexcept {
  if(!isSemanticActionCaptureEnabled() || !isLiveWorldTick(world) || before == after)
    return;

  std::string target = "world:";
  target.append(world.name());
  target.append(":clock");

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("world_time_changed"));
  payload.append(",\"world_time_before_ms\":"); appendInt(payload, before.toInt());
  payload.append(",\"world_day_before\":"); appendInt(payload, before.day());
  payload.append(",\"world_hour_before\":"); appendInt(payload, before.hour());
  payload.append(",\"world_minute_before\":"); appendInt(payload, before.minute());
  payload.append(",\"world_time_after_ms\":"); appendInt(payload, after.toInt());
  payload.append(",\"world_day_after\":"); appendInt(payload, after.day());
  payload.append(",\"world_hour_after\":"); appendInt(payload, after.hour());
  payload.append(",\"world_minute_after\":"); appendInt(payload, after.minute());
  payload.append(",\"time_delta_ms\":"); appendInt(payload, after.toInt() - before.toInt());
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::WorldTimeChanged, std::move(target), std::move(payload), world.tickCount());
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

void onSaveCheckpointManifest(World& world,
                              std::string_view slotPath,
                              std::string_view displayName,
                              const char* sourceLocation,
                              const char* reason) noexcept {
  if(!isServerBoundClientModeEnabled() || !isSemanticActionCaptureEnabled() || isCaptureSuppressed())
    return;
  if(!isLiveWorldTick(world))
    return;

  const std::string target = "character:PC_HERO:save-checkpoint";

  std::string payload;
  payload.reserve(1280);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation != nullptr ? std::string_view(sourceLocation) : std::string_view("unknown"));
  payload.append(",\"actor_key\":\"character:PC_HERO\"");
  payload.append(",\"character_key\":\"PC_HERO\"");
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"manifest_key\":"); appendEscaped(payload, target);
  payload.append(",\"checkpoint_kind\":\"native_save\"");
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("native_save_checkpoint_manifest"));
  payload.append(",\"save_slot_key\":"); appendEscaped(payload, slotPath.empty() ? std::string_view("native-save:unknown") : slotPath);
  payload.append(",\"slot_path\":"); appendEscaped(payload, slotPath);
  payload.append(",\"native_save_path\":"); appendEscaped(payload, slotPath);
  payload.append(",\"slot_display_name\":"); appendEscaped(payload, displayName);
  payload.append(",\"display_name\":"); appendEscaped(payload, displayName.empty() ? slotPath : displayName);
  payload.append(",\"client_world_name\":"); appendEscaped(payload, world.name());
  payload.append(",\"native_save_present\":true");
  payload.append(",\"db_save_snapshot_requested\":true");
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::SaveCheckpointManifest, std::move(target), std::move(payload), world.tickCount());
}

void onInteractiveUsed(World& world,
                       Interactive& interactive,
                       Npc& actor,
                       const char* sourceLocation,
                       const char* reason) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCapturePlayerAction(actor))
    return;
  if(!isLiveWorldTick(world))
    return;

  markPlayerInteractiveUse(world, interactive);

  auto target = interactiveEntityKey(world, interactive);

  std::string payload;
  payload.reserve(1024);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  appendNpcIdentity(payload, "actor", actor);
  appendInteractiveIdentity(payload, world, interactive);
  payload.append(",\"state\":"); appendInt(payload, interactive.stateId());
  payload.append(",\"locked\":"); appendBool(payload, interactive.isLocked());
  payload.append(",\"cracked\":"); appendBool(payload, interactive.isCracked());
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("interactive_use_accepted"));
  appendWorld(payload, world);
  appendVec3(payload, "interactive_position", interactive.position());
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::UseInteractive, std::move(target), std::move(payload), world.tickCount());
}

void onInteractiveStateChanged(World& world,
                               Interactive& interactive,
                               const Npc* actor,
                               std::int32_t stateBefore,
                               std::int32_t stateAfter,
                               bool lockedBefore,
                               bool lockedAfter,
                               bool crackedBefore,
                               bool crackedAfter,
                               const char* sourceLocation,
                               const char* reason) noexcept {
  if(!isSemanticActionCaptureEnabled() || !isLiveWorldTick(world))
    return;
  if(stateBefore == stateAfter && lockedBefore == lockedAfter && crackedBefore == crackedAfter)
    return;

  const auto captureCause = interactiveStateCaptureCause(world, interactive, actor);
  if(captureCause == InteractiveStateCaptureCause::None)
    return;

  if(!shouldEmitInteractiveState(world, interactive, stateAfter, lockedAfter, crackedAfter))
    return;

  auto target = interactiveEntityKey(world, interactive);

  std::string payload;
  payload.reserve(1152);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  if(actor != nullptr) {
    payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(*actor));
    appendNpcIdentity(payload, "actor", const_cast<Npc&>(*actor));
    }
  appendInteractiveIdentity(payload, world, interactive);
  payload.append(",\"capture_cause\":"); appendEscaped(payload, captureCauseName(captureCause));
  payload.append(",\"player_caused\":"); appendBool(payload, captureCause != InteractiveStateCaptureCause::None);
  payload.append(",\"state_before\":"); appendInt(payload, stateBefore);
  payload.append(",\"state_after\":"); appendInt(payload, stateAfter);
  payload.append(",\"locked_before\":"); appendBool(payload, lockedBefore);
  payload.append(",\"locked_after\":"); appendBool(payload, lockedAfter);
  payload.append(",\"cracked_before\":"); appendBool(payload, crackedBefore);
  payload.append(",\"cracked_after\":"); appendBool(payload, crackedAfter);
  payload.append(",\"lifecycle_state\":\"active\"");
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("interactive_state_changed"));
  appendWorld(payload, world);
  appendVec3(payload, "interactive_position", interactive.position());
  if(actor != nullptr)
    appendVec3(payload, "actor_position", actor->position());
  payload.push_back('}');

  submit(SemanticActionKind::UpdateInteractiveState, std::move(target), std::move(payload), world.tickCount());
}


void onWorldTriggerEvent(World& world,
                         std::uint32_t triggerVobId,
                         std::string_view triggerName,
                         std::string_view targetName,
                         std::string_view eventTarget,
                         std::string_view eventEmitter,
                         std::uint8_t eventType,
                         std::string_view eventTypeName,
                         const char* sourceLocation,
                         const char* reason) noexcept {
  if(!isSemanticActionCaptureEnabled() || !isLiveWorldTick(world))
    return;
  if(!hasRecentPlayerWorldInteraction(world))
    return;

  auto target = triggerEntityKey(world, triggerVobId, triggerName);

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"trigger_key\":"); appendEscaped(payload, target);
  payload.append(",\"trigger_vob_id\":"); appendUInt(payload, triggerVobId);
  payload.append(",\"trigger_name\":"); appendEscaped(payload, triggerName);
  payload.append(",\"trigger_target\":"); appendEscaped(payload, targetName);
  payload.append(",\"event_target\":"); appendEscaped(payload, eventTarget);
  payload.append(",\"event_emitter\":"); appendEscaped(payload, eventEmitter);
  payload.append(",\"event_type\":"); appendUInt(payload, eventType);
  payload.append(",\"event_type_name\":"); appendEscaped(payload, eventTypeName);
  payload.append(",\"capture_cause\":\"recent_player_world_interaction\"");
  payload.append(",\"player_caused\":true");
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("world_trigger_event"));
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::TriggerEvent, std::move(target), std::move(payload), world.tickCount());
}

void onMoverStateChanged(World& world,
                         std::uint32_t moverVobId,
                         std::string_view moverName,
                         std::int32_t stateBefore,
                         std::int32_t stateAfter,
                         std::uint32_t frame,
                         std::uint32_t targetFrame,
                         std::string_view stateBeforeName,
                         std::string_view stateAfterName,
                         const char* sourceLocation,
                         const char* reason) noexcept {
  if(!isSemanticActionCaptureEnabled() || !isLiveWorldTick(world))
    return;
  if(stateBefore == stateAfter)
    return;
  if(!hasRecentPlayerWorldInteraction(world))
    return;

  auto target = moverEntityKey(world, moverVobId, moverName);

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"mover_key\":"); appendEscaped(payload, target);
  payload.append(",\"mover_vob_id\":"); appendUInt(payload, moverVobId);
  payload.append(",\"mover_name\":"); appendEscaped(payload, moverName);
  payload.append(",\"state_before\":"); appendInt(payload, stateBefore);
  payload.append(",\"state_after\":"); appendInt(payload, stateAfter);
  payload.append(",\"state_before_name\":"); appendEscaped(payload, stateBeforeName);
  payload.append(",\"state_after_name\":"); appendEscaped(payload, stateAfterName);
  payload.append(",\"frame\":"); appendUInt(payload, frame);
  payload.append(",\"target_frame\":"); appendUInt(payload, targetFrame);
  payload.append(",\"capture_cause\":\"recent_player_world_interaction\"");
  payload.append(",\"player_caused\":true");
  payload.append(",\"reason\":"); appendEscaped(payload, reason != nullptr ? std::string_view(reason) : std::string_view("mover_state_changed"));
  appendWorld(payload, world);
  payload.push_back('}');

  submit(SemanticActionKind::MoverStateChanged, std::move(target), std::move(payload), world.tickCount());
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



void onWeaponStateChanged(Npc& actor,
                          WeaponState previousState,
                          WeaponState newState,
                          const char* sourceLocation,
                          const char* reason) noexcept {
  if(!isSemanticActionCaptureEnabled() || !shouldCaptureWorldAiAction(actor) || !shouldEmitWeaponState(actor, previousState, newState))
    return;
  auto& world = actor.world();
  const bool holstered = newState == WeaponState::NoWeapon;
  auto target = actorKey(actor);
  target.append(holstered ? ":weapon:holster" : ":weapon:ready");
  target.push_back(':');
  appendUInt(target, world.tickCount());

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

  submit(holstered ? SemanticActionKind::HolsterWeapon : SemanticActionKind::ReadyWeapon,
         std::move(target), std::move(payload), world.tickCount());
}

void onContainerInventoryTaken(Npc& actor,
                               Interactive& container,
                               std::size_t itemSymbol,
                               std::uint32_t sourceItemPersistentId,
                               std::size_t amount,
                               const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || amount == 0 || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto sourceKey = interactiveEntityKey(world, container);

  std::string target = sourceKey;
  target.append(":take:");
  appendUInt(target, itemSymbol);
  target.push_back(':');
  appendUInt(target, sourceItemPersistentId);

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"source_entity_key\":"); appendEscaped(payload, sourceKey);
  payload.append(",\"source_container_key\":"); appendEscaped(payload, sourceKey);
  payload.append(",\"container_key\":"); appendEscaped(payload, sourceKey);
  payload.append(",\"slot_id\":"); appendUInt(payload, world.mobsiId(&container));
  payload.append(",\"vob_id\":"); appendUInt(payload, container.getId());
  payload.append(",\"tag\":"); appendEscaped(payload, container.tag());
  payload.append(",\"focus_name\":"); appendEscaped(payload, container.focusName());
  payload.append(",\"display_name\":"); appendEscaped(payload, container.displayName());
  payload.append(",\"scheme\":"); appendEscaped(payload, container.schemeName());
  payload.append(",\"container\":"); appendBool(payload, container.isContainer());
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"source_item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"reason\":\"take_container_item\"");
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  payload.push_back('}');

  submit(SemanticActionKind::TakeContainerItem, std::move(target), std::move(payload), world.tickCount());
}

void onNpcInventoryLooted(Npc& looter,
                          Npc& sourceNpc,
                          std::size_t itemSymbol,
                          std::uint32_t sourceItemPersistentId,
                          std::size_t amount,
                          const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || amount == 0 || !shouldCapturePlayerAction(looter))
    return;
  if(!sourceNpc.isDead() && !sourceNpc.isUnconscious())
    return;
  auto& world = looter.world();
  auto sourceKey = npcEntityKey(world.name(), sourceNpc.persistentId(), sourceNpc.instanceSymbol());
  std::string target = sourceKey;
  target.append(":loot:");
  appendUInt(target, itemSymbol);
  target.push_back(':');
  appendUInt(target, sourceItemPersistentId);

  std::string payload;
  payload.reserve(768);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  appendNpcIdentity(payload, "looter", looter);
  appendNpcIdentity(payload, "source_npc", sourceNpc);
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"source_npc_key\":"); appendEscaped(payload, sourceKey);
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"source_item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"source_dead\":"); payload.append(sourceNpc.isDead() ? "true" : "false");
  payload.append(",\"source_unconscious\":"); payload.append(sourceNpc.isUnconscious() ? "true" : "false");
  payload.append(",\"reason\":\"loot_dead_or_unconscious_npc\"");
  appendWorld(payload, world);
  appendVec3(payload, "looter_position", looter.position());
  appendVec3(payload, "source_npc_position", sourceNpc.position());
  payload.push_back('}');

  submit(SemanticActionKind::LootNpcInventory, std::move(target), std::move(payload), world.tickCount());
}

void onCharacterItemDropped(Npc& actor,
                            const Item& worldItem,
                            std::size_t itemSymbol,
                            std::uint32_t sourceItemPersistentId,
                            std::size_t amount,
                            const char* sourceLocation) noexcept {
  if(!isSemanticActionCaptureEnabled() || amount == 0 || !shouldCapturePlayerAction(actor))
    return;
  auto& world = actor.world();
  auto target = worldItemKey(world.name(), worldItem.persistentId(), itemSymbol);

  std::string payload;
  payload.reserve(640);
  payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
  payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
  payload.append(",\"target_key\":"); appendEscaped(payload, target);
  payload.append(",\"item_template_key\":"); appendEscaped(payload, itemTemplateKey(itemSymbol));
  payload.append(",\"item_symbol\":"); appendUInt(payload, itemSymbol);
  payload.append(",\"item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"source_item_persistent_id\":"); appendUInt(payload, sourceItemPersistentId);
  payload.append(",\"world_item_persistent_id\":"); appendUInt(payload, worldItem.persistentId());
  payload.append(",\"amount\":"); appendUInt(payload, amount);
  payload.append(",\"reason\":\"player_drop_item\"");
  appendWorld(payload, world);
  appendVec3(payload, "actor_position", actor.position());
  appendVec3(payload, "item_position", worldItem.position());
  payload.push_back('}');

  submit(SemanticActionKind::DropCharacterItem, std::move(target), std::move(payload), world.tickCount());
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
  auto& world = actor.world();
  const auto attrName = attributeName(attribute);

  if(delta > 0) {
    if(actor.isPlayer() && (attribute == ATR_HITPOINTS || attribute == ATR_MANA)) {
      std::string target = attribute == ATR_HITPOINTS ? "character:PC_HERO:hitpoints" : "character:PC_HERO:mana";
      std::string payload;
      payload.reserve(640);
      payload.append("{\"source\":"); appendEscaped(payload, sourceLocation);
      payload.append(",\"actor_key\":"); appendEscaped(payload, actorKey(actor));
      payload.append(",\"character_key\":\"PC_HERO\"");
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

  if(sourceActor == nullptr)
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
  payload.append(",\"reason\":");
  appendEscaped(payload, sourceActor->isPlayer() ? "player_world_entity_damage" : "world_ai_entity_damage");
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
  payload.append(",\"reason\":");
  appendEscaped(payload, sourceActor != nullptr && sourceActor->isPlayer() ? "player_npc_no_health" : "world_ai_npc_no_health");
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


























