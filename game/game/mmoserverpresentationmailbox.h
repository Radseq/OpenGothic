#pragma once

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

#include "mmoserverpresentationevents.h"

namespace Mmo::ClientPresentation {

struct ServerPresentationMailboxBatch final {
  std::optional<ServerPresentationRouteIdentity> route;
  std::vector<ServerPresentationBootstrap> bootstraps;
  std::vector<ServerPresentationEvent> events;
  std::size_t rejectedRecords = 0U;

  [[nodiscard]] bool empty() const noexcept {
    return !route.has_value() && bootstraps.empty() && events.empty();
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
