#include "mmo_server_npc_activity_authority.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace Mmo::Server::NpcActivity {

namespace {

[[nodiscard]] std::string trimAscii(std::string_view text) {
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    text.remove_prefix(1);
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.remove_suffix(1);
  return std::string(text);
}

[[nodiscard]] std::string lowerAscii(std::string_view text) {
  std::string out = trimAscii(text);
  for(char& ch : out)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

[[nodiscard]] std::string firstNonEmpty(std::initializer_list<std::string_view> values) {
  for(const auto value : values) {
    auto out = trimAscii(value);
    if(!out.empty())
      return out;
  }
  return {};
}

[[nodiscard]] bool isFinishedState(std::string_view state) noexcept {
  return state == "idle" || state == "done" || state == "finished" || state == "ended" || state == "inactive";
}

[[nodiscard]] bool timedOut(const Snapshot& current, std::uint64_t nowServerTickMs) noexcept {
  return current.expectedDurationMs > 0 &&
         nowServerTickMs > current.startedServerTickMs + current.expectedDurationMs;
}

} // namespace

Kind parseKind(std::string_view actionKey) noexcept {
  const auto key = lowerAscii(actionKey);
  if(key.empty() || key == "unknown")
    return Kind::Unknown;
  if(key == "idle")
    return Kind::Idle;
  if(key == "routine" || key == "routine_active")
    return Kind::Routine;
  if(key == "moving" || key == "pathing" || key == "moving_to_waypoint")
    return Kind::Moving;
  if(key == "talking" || key == "dialog" || key == "conversation")
    return Kind::Talking;
  if(key == "sleeping" || key == "sleep")
    return Kind::Sleeping;
  if(key == "using_mob" || key == "use_mob" || key == "mobsi")
    return Kind::UsingMob;
  if(key == "alert" || key == "threat" || key == "weapon_ready" || key == "ready_weapon" || key == "draw_weapon" ||
     key == "warn" || key == "interrupt" || key == "suspect_crime" || key == "call_help" || key == "queue_script")
    return Kind::Alert;
  if(key == "combat" || key == "fight" || key == "attacking" || key == "engaged" || key == "aggro" || key == "agro" ||
     key == "start_combat")
    return Kind::Combat;
  if(key == "down" || key == "unconscious")
    return Kind::Down;
  if(key == "dead")
    return Kind::Dead;
  return Kind::Unknown;
}

std::string_view kindName(Kind kind) noexcept {
  switch(kind) {
    case Kind::Unknown:
      return "unknown";
    case Kind::Idle:
      return "idle";
    case Kind::Routine:
      return "routine";
    case Kind::Moving:
      return "moving";
    case Kind::Talking:
      return "talking";
    case Kind::Sleeping:
      return "sleeping";
    case Kind::UsingMob:
      return "using_mob";
    case Kind::Alert:
      return "alert";
    case Kind::Combat:
      return "combat";
    case Kind::Down:
      return "down";
    case Kind::Dead:
      return "dead";
  }
  return "unknown";
}

bool isExclusive(Kind kind) noexcept {
  return kind == Kind::Talking || kind == Kind::Sleeping || kind == Kind::UsingMob ||
         kind == Kind::Alert || kind == Kind::Combat || kind == Kind::Down || kind == Kind::Dead;
}

ApplyResult Registry::applyActivity(const ActivityInput& input) {
  const auto actorKey = trimAscii(input.actorKey);
  if(actorKey.empty())
    return {false, "npc_activity_missing_actor"};

  const auto state = lowerAscii(input.actionState);
  const auto kind = isFinishedState(state) ? Kind::Idle : parseKind(input.actionKey);
  if(kind == Kind::Idle) {
    states_.erase(actorKey);
    return {};
  }

  const auto it = states_.find(actorKey);
  const auto syncGroup = firstNonEmpty({input.syncGroup, input.actionKey});
  if(it != states_.end() && !canReplace(it->second, kind, syncGroup, input.serverTickMs))
    return {false, "npc_activity_conflict"};
  std::optional<Snapshot> interrupted;
  const bool preempted = it != states_.end() && isPreemption(it->second, kind, syncGroup, input.serverTickMs);
  if(preempted) {
    interrupted = it->second;
    if(!interrupted->syncGroup.empty())
      clearSyncGroup(interrupted->syncGroup);
  }

  setActivity(input, kind);
  return {
    .accepted = true,
    .reason = preempted ? "npc_activity_preempted" : "ok",
    .preempted = preempted,
    .interrupted = std::move(interrupted),
  };
}

ApplyResult Registry::applyDialogLock(const DialogLockInput& input) {
  const auto conversationKey = trimAscii(input.conversationKey);
  if(conversationKey.empty())
    return {false, "npc_activity_dialog_missing_conversation"};

  const auto speaker = trimAscii(input.speakerKey);
  if(speaker.empty())
    return {false, "npc_activity_dialog_missing_speaker"};

  const auto listener = trimAscii(input.listenerKey);
  const auto speakerIt = states_.find(speaker);
  if(speakerIt != states_.end() && !canReplace(speakerIt->second, Kind::Talking, conversationKey, input.serverTickMs))
    return {false, "npc_activity_dialog_speaker_busy"};
  if(!listener.empty()) {
    const auto listenerIt = states_.find(listener);
    if(listenerIt != states_.end() && !canReplace(listenerIt->second, Kind::Talking, conversationKey, input.serverTickMs))
      return {false, "npc_activity_dialog_listener_busy"};
  }

  const ActivityInput speakerInput {
    .actorKey = speaker,
    .actionKey = "talking",
    .actionState = "active",
    .targetKey = listener,
    .syncGroup = conversationKey,
    .serverTickMs = input.serverTickMs,
    .expectedDurationMs = input.lineDurationMs,
  };
  if(const auto result = applyActivity(speakerInput); !result.accepted)
    return result;

  if(listener.empty())
    return {};

  const ActivityInput listenerInput {
    .actorKey = listener,
    .actionKey = "talking",
    .actionState = "active",
    .targetKey = speaker,
    .syncGroup = conversationKey,
    .serverTickMs = input.serverTickMs,
    .expectedDurationMs = input.lineDurationMs,
  };
  return applyActivity(listenerInput);
}

std::optional<Snapshot> Registry::snapshot(std::string_view actorKey) const {
  const auto it = states_.find(std::string(actorKey));
  if(it == states_.end())
    return std::nullopt;
  return it->second;
}

std::vector<Snapshot> Registry::snapshots() const {
  std::vector<Snapshot> out;
  out.reserve(states_.size());
  for(const auto& [_, state] : states_)
    out.push_back(state);
  return out;
}

void Registry::expire(std::uint64_t nowServerTickMs) {
  for(auto it = states_.begin(); it != states_.end();) {
    if(timedOut(it->second, nowServerTickMs))
      it = states_.erase(it);
    else
      ++it;
  }
}

bool Registry::clearActorIfKind(std::string_view actorKey, Kind kind) {
  const auto key = trimAscii(actorKey);
  if(key.empty())
    return false;
  const auto it = states_.find(key);
  if(it == states_.end() || it->second.kind != kind)
    return false;
  states_.erase(it);
  return true;
}

void Registry::clearSyncGroup(std::string_view syncGroup) {
  const auto group = trimAscii(syncGroup);
  if(group.empty())
    return;
  for(auto it = states_.begin(); it != states_.end();) {
    if(it->second.syncGroup == group)
      it = states_.erase(it);
    else
      ++it;
  }
}

void Registry::clear() {
  states_.clear();
}

std::uint8_t Registry::priority(Kind kind) noexcept {
  switch(kind) {
    case Kind::Unknown:
      return 0;
    case Kind::Idle:
      return 1;
    case Kind::Routine:
      return 10;
    case Kind::Moving:
      return 20;
    case Kind::Talking:
    case Kind::Sleeping:
    case Kind::UsingMob:
      return 40;
    case Kind::Alert:
      return 60;
    case Kind::Combat:
      return 80;
    case Kind::Down:
      return 90;
    case Kind::Dead:
      return 100;
  }
  return 0;
}

bool Registry::isPreemption(const Snapshot& current,
                            Kind nextKind,
                            std::string_view nextSyncGroup,
                            std::uint64_t nowServerTickMs) noexcept {
  if(timedOut(current, nowServerTickMs))
    return false;
  if(current.kind == nextKind && current.syncGroup == nextSyncGroup)
    return false;
  return priority(nextKind) > priority(current.kind);
}

bool Registry::canReplace(const Snapshot& current,
                          Kind nextKind,
                          std::string_view nextSyncGroup,
                          std::uint64_t nowServerTickMs) noexcept {
  if(timedOut(current, nowServerTickMs))
    return true;
  if(current.kind == nextKind && current.syncGroup == nextSyncGroup)
    return true;
  if(current.kind == Kind::Dead)
    return nextKind == Kind::Dead;
  if(priority(nextKind) > priority(current.kind))
    return true;
  if(isExclusive(current.kind) && isExclusive(nextKind))
    return false;
  return true;
}

void Registry::setActivity(ActivityInput input, Kind kind) {
  const auto actorKey = trimAscii(input.actorKey);
  const auto syncGroup = firstNonEmpty({input.syncGroup, input.actionKey});
  auto& state = states_[actorKey];
  const bool sameKind = state.kind == kind && state.syncGroup == syncGroup;
  state.actorKey = actorKey;
  state.actionKey = std::string(kindName(kind));
  state.kind = kind;
  state.targetKey = trimAscii(input.targetKey);
  state.syncGroup = syncGroup;
  if(!sameKind || state.startedServerTickMs == 0)
    state.startedServerTickMs = input.serverTickMs;
  state.updatedServerTickMs = input.serverTickMs;
  state.expectedDurationMs = input.expectedDurationMs;
}

} // namespace Mmo::Server::NpcActivity


