#include "mmo_server_waypoint_authority.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace Mmo::Server::Waypoint {

namespace {

[[nodiscard]] std::string trimAscii(std::string_view text) {
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    text.remove_prefix(1);
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.remove_suffix(1);
  return std::string(text);
}

[[nodiscard]] std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
  for(const auto value : values) {
    const auto trimmed = trimAscii(value);
    if(!trimmed.empty())
      return trimmed;
  }
  return {};
}

[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string waypointNameFromAuthorityKey(std::string_view key) {
  constexpr std::string_view Prefix = "waypoint:";
  if(!startsWith(key, Prefix))
    return {};

  std::string_view rest = key.substr(Prefix.size());
  for(int i = 0; i < 3; ++i) {
    const auto pos = rest.find(':');
    if(pos == std::string_view::npos)
      return {};
    rest.remove_prefix(pos + 1);
  }
  return trimAscii(rest);
}

void applyValidation(NpcRoutineCommand& out, const Gameplay::ValidationResult& validation) {
  out.accepted = validation.accepted;
  out.shouldPersist = validation.shouldPersist;
  out.reason = validation.reason;
}

void applyValidation(NpcPathCommand& out, const Gameplay::ValidationResult& validation) {
  out.accepted = validation.accepted;
  out.shouldPersist = validation.shouldPersist;
  out.reason = validation.reason;
}

} // namespace

std::string canonicalWaypointRef(const WaypointRefInput& input) {
  const auto key = firstNonEmpty({input.key});
  if(!key.empty()) {
    if(const auto name = waypointNameFromAuthorityKey(key); !name.empty())
      return name;
    return key;
  }
  return firstNonEmpty({input.name, input.legacy});
}

NpcRoutineCommand buildNpcRoutineCommand(const NpcRoutineInput& input) {
  NpcRoutineCommand out;
  out.npcKey = firstNonEmpty({input.npcKey});
  out.routineState = firstNonEmpty({input.routineState, "unknown"});
  out.scheduleKey = firstNonEmpty({input.scheduleKey});
  out.currentWaypoint = canonicalWaypointRef(input.currentWaypoint);
  out.targetWaypoint = canonicalWaypointRef(input.targetWaypoint);

  applyValidation(out, Gameplay::validateNpcRoutineObservation({
    .npcKey = out.npcKey,
    .routineState = out.routineState,
    .scheduleKey = out.scheduleKey,
    .currentWaypoint = out.currentWaypoint,
    .targetWaypoint = out.targetWaypoint,
  }));
  return out;
}

NpcPathCommand buildNpcPathCommand(const NpcPathInput& input) {
  NpcPathCommand out;
  out.npcKey = firstNonEmpty({input.npcKey});
  out.pathState = firstNonEmpty({input.pathState, "unknown"});
  out.routeKey = firstNonEmpty({input.routeKey});
  out.currentWaypoint = canonicalWaypointRef(input.currentWaypoint);
  out.nextWaypoint = canonicalWaypointRef(input.nextWaypoint);
  out.targetWaypoint = canonicalWaypointRef(input.targetWaypoint);
  out.position = input.position;

  applyValidation(out, Gameplay::validateNpcPathObservation({
    .npcKey = out.npcKey,
    .pathState = out.pathState,
    .routeKey = out.routeKey,
    .currentWaypoint = out.currentWaypoint,
    .nextWaypoint = out.nextWaypoint,
    .targetWaypoint = out.targetWaypoint,
    .position = out.position,
  }));
  return out;
}

} // namespace Mmo::Server::Waypoint
