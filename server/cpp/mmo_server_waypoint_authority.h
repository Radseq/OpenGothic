#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "mmo_server_gameplay_authority.h"

namespace Mmo::Server::Waypoint {

struct WaypointRefInput final {
  std::string_view key;
  std::string_view name;
  std::string_view legacy;
};

struct NpcRoutineInput final {
  std::string_view npcKey;
  std::string_view routineState;
  std::string_view scheduleKey;
  WaypointRefInput currentWaypoint;
  WaypointRefInput targetWaypoint;
};

struct NpcPathInput final {
  std::string_view npcKey;
  std::string_view pathState;
  std::string_view routeKey;
  WaypointRefInput currentWaypoint;
  WaypointRefInput nextWaypoint;
  WaypointRefInput targetWaypoint;
  std::optional<Gameplay::Vec3> position;
};

struct NpcRoutineCommand final {
  bool accepted = true;
  bool shouldPersist = true;
  const char* reason = "ok";
  std::string npcKey;
  std::string routineState;
  std::string scheduleKey;
  std::string currentWaypoint;
  std::string targetWaypoint;
};

struct NpcPathCommand final {
  bool accepted = true;
  bool shouldPersist = true;
  const char* reason = "ok";
  std::string npcKey;
  std::string pathState;
  std::string routeKey;
  std::string currentWaypoint;
  std::string nextWaypoint;
  std::string targetWaypoint;
  std::optional<Gameplay::Vec3> position;
};

[[nodiscard]] std::string canonicalWaypointRef(const WaypointRefInput& input);
[[nodiscard]] NpcRoutineCommand buildNpcRoutineCommand(const NpcRoutineInput& input);
[[nodiscard]] NpcPathCommand buildNpcPathCommand(const NpcPathInput& input);

} // namespace Mmo::Server::Waypoint
