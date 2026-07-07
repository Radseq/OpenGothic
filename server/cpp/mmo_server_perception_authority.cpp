#include "mmo_server_perception_authority.h"

namespace Mmo::Server::Perception {

static_assert(Gothic2Perceptions.size() == MaxPerceptionId);
static_assert(byId(1)->constant == "PERC_ASSESSPLAYER");
static_assert(byId(17)->eventName == "assess_theft");
static_assert(byId(24)->need == ServerNeed::InterruptAction);
static_assert(byId(32)->domain == Domain::Crime);
static_assert(!byId(0).has_value());
static_assert(!byId(33).has_value());
static_assert(isCrime(byId(17)->domain));
static_assert(isCombat(byId(27)->domain));
static_assert(requiresServerDecision(byId(6)->need));

} // namespace Mmo::Server::Perception
