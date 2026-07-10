#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "../../../shared/game/mmo/mmosemanticevents.h"

namespace Mmo {

struct EncodedClientGameplayPacket final {
  std::vector<std::uint8_t> bytes;
  bool bootstrapRequest = false;
};

[[nodiscard]] std::optional<EncodedClientGameplayPacket> encodeClientGameplayPacket(
    const SemanticActionEnvelope& envelope,
    std::string_view sessionKey);

} // namespace Mmo
