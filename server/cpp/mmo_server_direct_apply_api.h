#pragma once

#include <string_view>

#include "../../game/game/mmonetprotocol.h"
#include "mmo_server_types.h"

namespace Mmo::Server {

struct DirectApplyRequest final {
  const MySqlTarget& target;
  std::string_view sessionUuid;
  const Mmo::Net::ClientActionPacket& packet;
  std::string_view dbPayload;
};

using DirectApplyHandler = DirectApplyResult (*)(const DirectApplyRequest&);

} // namespace Mmo::Server
