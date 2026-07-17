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

static std::array<InteractiveStateCacheEntry, 512> interactiveStateCache = {};
static std::size_t interactiveStateCacheCursor = 0;
static std::array<InteractivePlayerUseEntry, 128> interactivePlayerUseCache = {};
static std::size_t interactivePlayerUseCacheCursor = 0;
static std::uint64_t lastPlayerInteractiveUseTick = 0;

static bool tickWithin(std::uint64_t now, std::uint64_t then, std::uint64_t limit) noexcept {
  return then != 0 && now >= then && now - then <= limit;
}

static bool sameInteractive(const InteractiveStateCacheEntry& e, World& world, Interactive& interactive) noexcept {
  return e.occupied &&
         e.vobId == interactive.getId() &&
         e.slotId == world.mobsiId(&interactive);
}

static bool sameInteractiveUse(const InteractivePlayerUseEntry& e, World& world, Interactive& interactive) noexcept {
  return e.occupied &&
         e.vobId == interactive.getId() &&
         e.slotId == world.mobsiId(&interactive);
}

static void markPlayerInteractiveUse(World& world, Interactive& interactive) noexcept {
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

static bool hasRecentPlayerUseOfInteractive(World& world, Interactive& interactive) noexcept {
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

static InteractiveStateCaptureCause interactiveStateCaptureCause(World& world, Interactive& interactive, const Npc* actor) noexcept {
  if(actor != nullptr)
    return actor->isPlayer() && isLiveWorldTick(world) ? InteractiveStateCaptureCause::DirectPlayerActor : InteractiveStateCaptureCause::None;
  if(hasRecentPlayerUseOfInteractive(world, interactive))
    return InteractiveStateCaptureCause::RecentPlayerInteractiveUse;
  if(hasRecentPlayerWorldInteraction(world))
    return InteractiveStateCaptureCause::RecentPlayerWorldInteraction;
  return InteractiveStateCaptureCause::None;
}

static std::string_view captureCauseName(InteractiveStateCaptureCause cause) noexcept {
  switch(cause) {
    case InteractiveStateCaptureCause::DirectPlayerActor:            return "direct_player_actor";
    case InteractiveStateCaptureCause::RecentPlayerInteractiveUse:   return "recent_player_interactive_use";
    case InteractiveStateCaptureCause::RecentPlayerWorldInteraction: return "recent_player_world_interaction";
    case InteractiveStateCaptureCause::None:                         return "none";
  }
  return "none";
}

static bool shouldEmitInteractiveState(World& world,
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

void onInteractiveUsed(World& world,
                       Interactive& interactive,
                       Npc& actor,
                       const char* sourceLocation,
                       const char* reason) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || !shouldCapturePlayerAction(actor))
    return;
  if(!isLiveWorldTick(world))
    return;

  markPlayerInteractiveUse(world, interactive);

  auto target = interactiveEntityKey(world, interactive);

  if(isServerBoundClientModeEnabled()) {
    const auto actorIdentity = actorKey(actor);
    const auto characterIdentity = characterKey();
    const auto actorPos = actor.position();
    const ClientInteractionRequest request{
        .clientTick = world.tickCount(),
        .verb = ClientInteractionVerb::Use,
        .actorPosition = {
            .x = actorPos.x,
            .y = actorPos.y,
            .z = actorPos.z,
        },
        .localSlotId = static_cast<std::int64_t>(world.mobsiId(&interactive)),
        .localVobId = static_cast<std::int64_t>(interactive.getId()),
        .targetKey = target,
        .source = sourceLocation != nullptr ? std::string_view(sourceLocation)
                                            : std::string_view("unknown"),
        .actorKey = actorIdentity,
        .characterKey = characterIdentity,
        .world = world.name(),
        .reason = reason != nullptr ? std::string_view(reason)
                                   : std::string_view("use_interactive"),
    };
    (void)submitClientInteraction(request);
  }
  if(!isClientMmoDiagnosticsEnabled())
    return;

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

  if(isClientMmoDiagnosticsEnabled())
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
  if(!isClientMmoDiagnosticsEnabled() || !isLiveWorldTick(world))
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
  if(!isClientMmoDiagnosticsEnabled() || !isLiveWorldTick(world))
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

} // namespace Mmo::Hooks::Detail
