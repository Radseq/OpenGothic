#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "mmo_server_story_authority.h"

namespace Mmo::Server::Quest {

enum class Status : std::uint8_t {
  Running,
  Success,
  Failed,
  Obsolete,
};

struct UpdateInput final {
  std::string_view questKey;
  std::string_view topic;
  std::string_view targetKey;
  std::string_view questName;
  std::string_view rawStatus;
  std::string_view previousStatus;
  std::int64_t entryCount = 0;
  bool allowTerminalReopen = false;
};

struct UpdateCommand final {
  bool accepted = true;
  const char* reason = "ok";
  std::string questKey;
  std::string questName;
  std::string status;
  std::int64_t entryCount = 0;
};

[[nodiscard]] constexpr std::string_view statusName(Status status) noexcept {
  switch(status) {
    case Status::Running:
      return "running";
    case Status::Success:
      return "success";
    case Status::Failed:
      return "failed";
    case Status::Obsolete:
      return "obsolete";
  }
  return "running";
}

[[nodiscard]] constexpr bool isTerminal(Status status) noexcept {
  return status == Status::Success || status == Status::Failed || status == Status::Obsolete;
}

[[nodiscard]] Status parseStatus(std::string_view raw) noexcept;
[[nodiscard]] UpdateCommand buildUpdateCommand(const UpdateInput& input);

} // namespace Mmo::Server::Quest
