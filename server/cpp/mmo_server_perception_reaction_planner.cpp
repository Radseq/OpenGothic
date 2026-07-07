#include "mmo_server_perception_reaction_planner.h"

namespace Mmo::Server::Perception {

static_assert(reactionKindName(ReactionKind::Ignore) == "ignore");
static_assert(reactionKindName(ReactionKind::Warn) == "warn");
static_assert(reactionKindName(ReactionKind::StartCombat) == "start_combat");

} // namespace Mmo::Server::Perception
