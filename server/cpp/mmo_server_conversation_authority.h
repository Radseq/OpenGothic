#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Mmo::Server::Conversation {

inline constexpr std::uint32_t MaxLineDurationMs = 60000;
inline constexpr std::uint32_t ExpiredConversationGraceMs = 5000;

struct LineInput final {
  std::string_view conversationKey;
  std::string_view speakerKey;
  std::string_view listenerKey;
  std::string_view outputName;
  std::string_view subtitleText;
  std::uint64_t startServerTickMs = 0;
  std::uint32_t durationMs = 0;
};

struct LineState final {
  std::string speakerKey;
  std::string listenerKey;
  std::string outputName;
  std::string subtitleText;
  std::uint64_t startServerTickMs = 0;
  std::uint32_t durationMs = 0;
};

struct Snapshot final {
  bool active = false;
  std::string conversationKey;
  std::vector<std::string> participants;
  LineState currentLine;
  std::uint32_t elapsedLineMs = 0;
  std::uint32_t remainingLineMs = 0;
};

struct ApplyResult final {
  bool accepted = true;
  const char* reason = "ok";
  Snapshot snapshot;
};

class Registry final {
public:
  [[nodiscard]] ApplyResult applyLine(const LineInput& input);
  [[nodiscard]] std::optional<Snapshot> snapshot(std::string_view conversationKey,
                                                 std::uint64_t nowServerTickMs) const;
  [[nodiscard]] std::vector<Snapshot> activeSnapshots(std::uint64_t nowServerTickMs) const;

  bool cancel(std::string_view conversationKey);
  void expire(std::uint64_t nowServerTickMs);
  void clear();

private:
  struct State final {
    std::string conversationKey;
    std::vector<std::string> participants;
    LineState currentLine;
    std::uint64_t updatedServerTickMs = 0;
  };

  [[nodiscard]] static Snapshot makeSnapshot(const State& state, std::uint64_t nowServerTickMs);

  std::unordered_map<std::string, State> states_;
};

} // namespace Mmo::Server::Conversation
