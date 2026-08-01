#pragma once

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

#include "mmoserverpresentationevents.h"
#include "mmoservercorpselootreadmodel.h"

namespace Mmo::ClientPresentation {

struct ServerPresentationMailboxBatch final {
  std::optional<ServerPresentationRouteIdentity> route;
  std::vector<ServerPresentationBootstrap> bootstraps;
  std::vector<ServerPresentationEvent> events;
  std::vector<ServerCorpseLootAvailabilityChanged> corpseLootAvailability;
  std::vector<ServerCorpseLootSnapshot> corpseLootSnapshots;
  std::vector<ServerCorpseLootStackDelta> corpseLootDeltas;
  std::vector<ServerCorpseLootSessionClosed> corpseLootClosed;
  std::vector<ServerCorpseLootResync> corpseLootResyncs;
  std::size_t rejectedRecords = 0U;

  [[nodiscard]] bool empty() const noexcept {
    return !route.has_value() && bootstraps.empty() && events.empty() &&
           corpseLootAvailability.empty() && corpseLootSnapshots.empty() &&
           corpseLootDeltas.empty() && corpseLootClosed.empty() &&
           corpseLootResyncs.empty();
  }
};

[[nodiscard]] inline const ServerPresentationEventHeader&
serverPresentationEventHeader(const ServerPresentationEvent& event) noexcept {
  return std::visit(
      [](const auto& value) -> const ServerPresentationEventHeader& {
        return value.header;
      },
      event);
}

} // namespace Mmo::ClientPresentation
