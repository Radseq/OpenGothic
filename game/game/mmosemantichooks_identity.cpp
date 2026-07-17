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

std::string_view characterKey() noexcept {
  return CommandLine::inst().mmoCharacterKey();
}

std::string characterEntityKey() {
  std::string out = "character:";
  out.append(characterKey());
  return out;
}

std::string characterTargetKey(std::string_view suffix) {
  auto out = characterEntityKey();
  out.push_back(':');
  out.append(suffix);
  return out;
}

std::string actorKey(const Npc& npc) {
  std::string out = npc.isPlayer() ? characterEntityKey() : "npc:";
  if(!npc.isPlayer())
    appendUInt(out, npc.persistentId());
  out.append(":sym:");
  appendUInt(out, npc.instanceSymbol());
  return out;
}

std::string playerOrDefaultKey(const World& world) {
  if(const auto* player = world.player())
    return actorKey(*player);
  return characterEntityKey();
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

std::string npcEntityKey(std::string_view worldName, std::uint32_t persistentId, std::size_t symbol) {
  std::string out = "npc:";
  out.append(worldName);
  out.append(":pid:");
  appendUInt(out, persistentId);
  out.append(":sym:");
  appendUInt(out, symbol);
  return out;
}

std::string_view waypointName(const WayPoint* waypoint) noexcept {
  if(waypoint == nullptr)
    return {};
  return waypoint->name;
}

std::string waypointKey(const World& world, const WayPoint* waypoint) {
  if(waypoint == nullptr || waypoint->name.empty())
    return {};
  std::string out = "waypoint:";
  out.append(world.name());
  out.push_back(':');
  out.append(waypoint->name);
  return out;
}

std::string npcTargetKey(Npc* npc) {
  if(npc == nullptr)
    return {};
  if(npc->isPlayer())
    return actorKey(*npc);
  return npcEntityKey(npc->world().name(), npc->persistentId(), npc->instanceSymbol());
}

std::string scriptFunctionKey(size_t function) {
  if(function == 0)
    return {};
  std::string out = "script-fn:";
  appendUInt(out, function);
  return out;
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

} // namespace Mmo::Hooks::Detail
