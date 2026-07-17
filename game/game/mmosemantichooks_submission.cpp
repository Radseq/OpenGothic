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

static thread_local unsigned captureSuppressionDepth = 0;

void beginCaptureSuppression() noexcept {
  ++captureSuppressionDepth;
}

void endCaptureSuppression() noexcept {
  if(captureSuppressionDepth != 0)
    --captureSuppressionDepth;
}

bool isCaptureSuppressed() noexcept {
  return captureSuppressionDepth != 0;
}

static SemanticActionEnvelope makeEnvelope(SemanticActionKind kind,
                                    std::string targetKey,
                                    std::string payload,
                                    std::uint64_t tick) {
  SemanticActionEnvelope e;
  e.kind = kind;
  e.localSequence = nextClientIntentSequence();
  e.clientTick = tick;
  e.targetKey = std::move(targetKey);
  e.idempotencyKey = makeIdempotencyKey(clientMmoSessionKey(), e.localSequence, kind, e.targetKey);
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

void submit(SemanticActionKind kind, std::string targetKey, std::string payload, std::uint64_t tick) noexcept {
  if((!isClientMmoDiagnosticsEnabled() && !isServerBoundClientModeEnabled()) || isCaptureSuppressed())
    return;
  recordClientMmoDiagnostic(makeEnvelope(kind, std::move(targetKey), std::move(payload), tick));
}

void submitObservedNpcState(SemanticActionKind kind, Npc& actor, std::string target, std::string payload) noexcept {
  appendWorld(payload, actor.world());
  appendVec3(payload, "position", actor.position());
  payload.push_back('}');
  submit(kind, std::move(target), std::move(payload), actor.world().tickCount());
}

bool shouldCaptureScriptAction(Npc* actor) noexcept {
  return isClientMmoDiagnosticsEnabled() && shouldCapturePlayerAction(actor);
}

} // namespace Mmo::Hooks::Detail
