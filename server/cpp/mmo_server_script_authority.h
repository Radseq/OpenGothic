#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Mmo::Server::Script {

struct SetIntInput final {
  std::string_view scriptKey;
  std::string_view globalKey;
  std::string_view symbolName;
  std::string_view targetKey;
  std::int64_t symbolIndex = 0;
  std::int64_t valueIndex = 0;
  std::int64_t valueAfter = 0;
};

struct SetIntCommand final {
  bool accepted = true;
  const char* reason = "ok";
  std::string scriptKey;
  std::int64_t symbolIndex = 0;
  std::int64_t valueIndex = 0;
  std::int64_t valueAfter = 0;
};

[[nodiscard]] SetIntCommand buildSetIntCommand(const SetIntInput& input);

} // namespace Mmo::Server::Script
