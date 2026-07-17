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

void onWorldTimeChanged(World& world,
                        gtime before,
                        gtime after,
                        const char* sourceLocation,
                        const char* reason) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || !isLiveWorldTick(world) || before == after)
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
  if(!isClientMmoDiagnosticsEnabled() || !isLiveWorldTick(world))
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

void onWorldItemRemoved(World& world,
                        const Item& worldItem,
                        const char* sourceLocation) noexcept {
  if(!isClientMmoDiagnosticsEnabled() || !isLiveWorldTick(world))
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

} // namespace Mmo::Hooks::Detail
