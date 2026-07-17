#pragma once

#include "gametime.h"
#include "mmoclientbridge.h"
#include "mmorestoresnapshot.h"

#include <cstddef>
#include <optional>

class World;

namespace GameSessionMmoRestoreDetail {

struct MmoNpcRoutineAuthorityApplyStats final {
  std::size_t applied = 0;
  std::size_t fallback = 0;
  std::size_t missingNpc = 0;
  std::size_t skipped = 0;
};

[[nodiscard]] std::optional<Mmo::ServerBootstrapSnapshot> latestMmoBootstrapSnapshot() noexcept;
[[nodiscard]] bool canReuseMmoDbContinuePreWorldSnapshot() noexcept;
[[nodiscard]] bool loadMmoDbContinuePreWorldClock(gtime& out) noexcept;
[[nodiscard]] MmoNpcRoutineAuthorityApplyStats applyMmoNpcRoutineAuthorityState(
    World& world, const Mmo::RestoreSnapshot::Result& snapshot);

} // namespace GameSessionMmoRestoreDetail
