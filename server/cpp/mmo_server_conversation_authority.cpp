#include "mmo_server_conversation_authority.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace Mmo::Server::Conversation {

namespace {

[[nodiscard]] std::string trimAscii(std::string_view text) {
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    text.remove_prefix(1);
  while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.remove_suffix(1);
  return std::string(text);
}

void addParticipant(std::vector<std::string>& participants, std::string_view key) {
  const auto trimmed = trimAscii(key);
  if(trimmed.empty())
    return;
  if(std::find(participants.begin(), participants.end(), trimmed) == participants.end())
    participants.push_back(trimmed);
}

[[nodiscard]] std::uint32_t clampDuration(std::uint32_t durationMs) noexcept {
  if(durationMs == 0)
    return 1;
  return std::min(durationMs, MaxLineDurationMs);
}

[[nodiscard]] bool validKey(std::string_view key) noexcept {
  return !key.empty() && key.size() <= 512;
}

} // namespace

ApplyResult Registry::applyLine(const LineInput& input) {
  const auto conversationKey = trimAscii(input.conversationKey);
  if(!validKey(conversationKey))
    return {.accepted = false, .reason = "conversation_key_invalid"};

  const auto speakerKey = trimAscii(input.speakerKey);
  if(!validKey(speakerKey))
    return {.accepted = false, .reason = "conversation_speaker_invalid"};
  if(input.outputName.empty() && input.subtitleText.empty())
    return {.accepted = false, .reason = "conversation_line_empty"};

  auto& state = states_[conversationKey];
  state.conversationKey = conversationKey;
  addParticipant(state.participants, speakerKey);
  addParticipant(state.participants, input.listenerKey);
  state.currentLine = {
    .speakerKey = speakerKey,
    .listenerKey = trimAscii(input.listenerKey),
    .outputName = trimAscii(input.outputName),
    .subtitleText = trimAscii(input.subtitleText),
    .startServerTickMs = input.startServerTickMs,
    .durationMs = clampDuration(input.durationMs),
  };
  state.updatedServerTickMs = input.startServerTickMs;

  return {
    .accepted = true,
    .reason = "ok",
    .snapshot = makeSnapshot(state, input.startServerTickMs),
  };
}

std::optional<Snapshot> Registry::snapshot(std::string_view conversationKey,
                                           std::uint64_t nowServerTickMs) const {
  const auto it = states_.find(std::string(conversationKey));
  if(it == states_.end())
    return std::nullopt;
  auto out = makeSnapshot(it->second, nowServerTickMs);
  if(!out.active)
    return std::nullopt;
  return out;
}

std::vector<Snapshot> Registry::activeSnapshots(std::uint64_t nowServerTickMs) const {
  std::vector<Snapshot> out;
  out.reserve(states_.size());
  for(const auto& [_, state] : states_) {
    auto snapshot = makeSnapshot(state, nowServerTickMs);
    if(snapshot.active)
      out.push_back(std::move(snapshot));
  }
  return out;
}

bool Registry::cancel(std::string_view conversationKey) {
  const auto key = trimAscii(conversationKey);
  if(key.empty())
    return false;
  return states_.erase(key) > 0;
}

void Registry::expire(std::uint64_t nowServerTickMs) {
  for(auto it = states_.begin(); it != states_.end();) {
    const auto& line = it->second.currentLine;
    const auto endsAt = line.startServerTickMs + line.durationMs + ExpiredConversationGraceMs;
    if(nowServerTickMs > endsAt)
      it = states_.erase(it);
    else
      ++it;
  }
}

void Registry::clear() {
  states_.clear();
}

Snapshot Registry::makeSnapshot(const State& state, std::uint64_t nowServerTickMs) {
  Snapshot out;
  out.conversationKey = state.conversationKey;
  out.participants = state.participants;
  out.currentLine = state.currentLine;

  if(nowServerTickMs <= state.currentLine.startServerTickMs) {
    out.active = true;
    out.elapsedLineMs = 0;
    out.remainingLineMs = state.currentLine.durationMs;
    return out;
  }

  const auto elapsed = nowServerTickMs - state.currentLine.startServerTickMs;
  if(elapsed >= state.currentLine.durationMs) {
    out.active = false;
    out.elapsedLineMs = state.currentLine.durationMs;
    out.remainingLineMs = 0;
    return out;
  }

  out.active = true;
  out.elapsedLineMs = static_cast<std::uint32_t>(elapsed);
  out.remainingLineMs = state.currentLine.durationMs - out.elapsedLineMs;
  return out;
}

} // namespace Mmo::Server::Conversation
