#pragma once

#include <optional>
#include <string>

namespace Mmo::Server::PersistenceSql {

[[nodiscard]] inline std::string nullableDouble(std::optional<double> value) {
  if(!value)
    return "NULL";
  return std::to_string(*value);
}

} // namespace Mmo::Server::PersistenceSql
