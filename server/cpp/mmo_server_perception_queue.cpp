#include "mmo_server_perception_queue.h"

#include <algorithm>

namespace Mmo::Server::Perception {

Queue::Queue(std::size_t capacity, std::uint64_t coalesceWindowMs)
  : capacity_(std::max<std::size_t>(1, capacity)),
    coalesceWindowMs_(coalesceWindowMs) {
}

EnqueueResult Queue::enqueue(const EventInput& input) {
  const auto def = byId(input.perceptionId);
  if(!def)
    return {.accepted = false, .reason = "unknown_perception"};
  if(input.sourceKey.empty() && input.otherKey.empty() && input.victimKey.empty())
    return {.accepted = false, .reason = "empty_perception_subject"};

  if(!events_.empty()) {
    auto& last = events_.back();
    if(matchesRecent(last, input)) {
      last.lastServerTickMs = input.serverTickMs;
      ++last.repeats;
      return {.accepted = true, .coalesced = true, .reason = "coalesced", .sequence = last.sequence};
    }
  }

  events_.push_back({
    .sequence = nextSequence_++,
    .perceptionId = input.perceptionId,
    .def = *def,
    .sourceKey = std::string(input.sourceKey),
    .otherKey = std::string(input.otherKey),
    .victimKey = std::string(input.victimKey),
    .itemKey = std::string(input.itemKey),
    .reason = std::string(input.reason),
    .originPosition = input.originPosition,
    .firstServerTickMs = input.serverTickMs,
    .lastServerTickMs = input.serverTickMs,
    .repeats = 1,
  });
  trimToCapacity();
  return {.accepted = true, .reason = "queued", .sequence = events_.back().sequence};
}

std::vector<Event> Queue::snapshot() const {
  return {events_.begin(), events_.end()};
}

std::optional<Event> Queue::latest() const {
  if(events_.empty())
    return std::nullopt;
  return events_.back();
}

std::size_t Queue::size() const noexcept {
  return events_.size();
}

void Queue::expireBefore(std::uint64_t serverTickMs) {
  while(!events_.empty() && events_.front().lastServerTickMs < serverTickMs)
    events_.pop_front();
}

void Queue::clear() {
  events_.clear();
}

bool Queue::matchesRecent(const Event& event, const EventInput& input) const noexcept {
  if(event.perceptionId != input.perceptionId)
    return false;
  if(event.sourceKey != input.sourceKey || event.otherKey != input.otherKey ||
     event.victimKey != input.victimKey || event.itemKey != input.itemKey)
    return false;
  return input.serverTickMs <= event.lastServerTickMs + coalesceWindowMs_;
}

void Queue::trimToCapacity() {
  while(events_.size() > capacity_)
    events_.pop_front();
}

std::string_view domainName(Domain domain) noexcept {
  switch(domain) {
    case Domain::Social: return "social";
    case Domain::Combat: return "combat";
    case Domain::Crime: return "crime";
    case Domain::Sound: return "sound";
    case Domain::Magic: return "magic";
    case Domain::Movement: return "movement";
    case Domain::Item: return "item";
    case Domain::Room: return "room";
    case Domain::Command: return "command";
  }
  return "unknown";
}

std::string_view serverNeedName(ServerNeed need) noexcept {
  switch(need) {
    case ServerNeed::ObserveOnly: return "observe_only";
    case ServerNeed::QueueScriptHandler: return "queue_script_handler";
    case ServerNeed::InterruptAction: return "interrupt_action";
    case ServerNeed::ChangeHostility: return "change_hostility";
    case ServerNeed::StartCombat: return "start_combat";
    case ServerNeed::CrimeReaction: return "crime_reaction";
  }
  return "unknown";
}

static_assert(DefaultQueueCapacity > 0);
static_assert(DefaultCoalesceWindowMs > 0);

} // namespace Mmo::Server::Perception
