#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "mmoserverpresentationevents.h"

namespace Mmo::ClientPresentation {

// Pure planning boundary between typed server presentation and ZenEngine object
// allocation. The resolver owns ArchetypeId -> Daedalus instance mapping; this
// layer only validates that the request is an exact, materializable NPC spawn.
struct ServerNpcMaterializationPlan final {
  ServerPresentationEntityHandle entity{};
  std::uint32_t daedalusInstanceSymbol = 0U;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return entity.valid() && daedalusInstanceSymbol != 0U;
  }
};

template <class ArchetypeResolver>
[[nodiscard]] constexpr std::optional<ServerNpcMaterializationPlan>
makeServerNpcMaterializationPlan(
    const ServerPresentationEntityRecord& entity,
    ArchetypeResolver&& resolveArchetype) {
  if(!entity.valid() || entity.kind != ServerPresentationEntityKind::Npc)
    return std::nullopt;

  const auto symbol = std::invoke(
      std::forward<ArchetypeResolver>(resolveArchetype),
      entity.presentation.archetypeId);
  if(!symbol.has_value() || *symbol == 0U)
    return std::nullopt;

  return ServerNpcMaterializationPlan{
      .entity = entity.handle,
      .daedalusInstanceSymbol = *symbol,
  };
}

} // namespace Mmo::ClientPresentation
