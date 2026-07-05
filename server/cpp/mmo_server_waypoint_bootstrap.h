#pragma once

#include <string>
#include <string_view>

#include "mmo_server_persistence.h"

namespace Mmo::Server::Waypoint {

struct NearbyWaypointBootstrap final {
  std::string json = "[]";
  std::string diagnostic;
};

[[nodiscard]] NearbyWaypointBootstrap readNearbyWaypointBootstrap(const MySqlTarget& target,
                                                                  std::string_view sessionUuid,
                                                                  std::string_view worldName);

} // namespace Mmo::Server::Waypoint
