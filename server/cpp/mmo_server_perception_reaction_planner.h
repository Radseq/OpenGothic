#pragma once

#include <cstdint>
#include <string_view>

#include "mmo_server_perception_witness_registry.h"

namespace Mmo::Server::Perception {

enum class ReactionKind : std::uint8_t {
  Ignore,
  Observe,
  QueueScript,
  Interrupt,
  Warn,
  SuspectCrime,
  CallHelp,
  StartCombat,
};

struct ReactionPlan final {
  ReactionKind kind = ReactionKind::Ignore;
  const char* reason = "ignored";
  bool needsUdpReplication = false;
  bool needsScriptHandler = false;
  bool interruptsCurrentAction = false;
  bool changesHostility = false;
};

[[nodiscard]] constexpr std::string_view reactionKindName(ReactionKind kind) noexcept {
  switch(kind) {
    case ReactionKind::Ignore: return "ignore";
    case ReactionKind::Observe: return "observe";
    case ReactionKind::QueueScript: return "queue_script";
    case ReactionKind::Interrupt: return "interrupt";
    case ReactionKind::Warn: return "warn";
    case ReactionKind::SuspectCrime: return "suspect_crime";
    case ReactionKind::CallHelp: return "call_help";
    case ReactionKind::StartCombat: return "start_combat";
  }
  return "unknown";
}

[[nodiscard]] constexpr ReactionPlan planReaction(const Event& event,
                                                  const WitnessSummary& witnesses) noexcept {
  if(event.def.needsWitness && witnesses.witnessed == 0)
    return {ReactionKind::Ignore, "no_witness"};

  switch(event.def.need) {
    case ServerNeed::ObserveOnly:
      return {ReactionKind::Observe, "observe_only"};

    case ServerNeed::QueueScriptHandler:
      return {ReactionKind::QueueScript,
              "script_handler_required",
              true,
              true,
              event.def.canInterruptRoutine,
              false};

    case ServerNeed::InterruptAction:
      if(event.perceptionId == 15)
        return {ReactionKind::Warn, "warn_interrupt", true, true, true, false};
      return {ReactionKind::Interrupt, "action_interrupt", true, true, true, false};

    case ServerNeed::ChangeHostility:
      return {ReactionKind::StartCombat, "hostility_changed", true, true, true, true};

    case ServerNeed::StartCombat:
      return {ReactionKind::StartCombat, "combat_required", true, true, true, true};

    case ServerNeed::CrimeReaction:
      if(event.perceptionId == 6 || event.perceptionId == 9)
        return {ReactionKind::CallHelp, "violent_crime_witnessed", true, true, true, true};
      return {ReactionKind::SuspectCrime, "crime_witnessed", true, true, true, false};
  }

  return {ReactionKind::Ignore, "unhandled_need"};
}

} // namespace Mmo::Server::Perception
