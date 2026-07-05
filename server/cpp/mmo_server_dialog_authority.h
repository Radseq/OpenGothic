#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Mmo::Server::Dialog {

struct KnownDialogInput final {
  std::string_view npcKey;
  std::string_view npcSymbolName;
  std::string_view infoKey;
  std::string_view infoSymbolName;
  std::string_view targetKey;
  std::string_view availabilityState;
  std::optional<bool> known;
  std::optional<bool> removed;
  std::optional<bool> permanent;
  std::optional<bool> repeatable;
};

struct KnownDialogCommand final {
  bool accepted = true;
  const char* reason = "ok";
  std::string npcKey;
  std::string infoKey;
  bool known = true;
  bool permanent = false;
  std::string availability;
};

[[nodiscard]] KnownDialogCommand buildKnownDialogCommand(const KnownDialogInput& input);

} // namespace Mmo::Server::Dialog
