#include "mmoclientadapter.h"

#include <utility>

#include "mmoclientadapterdetail.h"
#include "mmoclientbridge.h"

namespace Mmo {

ClientMmoSubmitResult submitClientMovement(
    const ClientMovementIntent& intent) noexcept {
  if(!isServerBoundClientModeEnabled())
    return {};

  try {
    auto packet = ClientAdapterDetail::makeCompatibilityMovementPacket(intent);
    if(!packet.has_value())
      return {.status = ClientMmoSubmitStatus::InvalidIntent};
    return submitClientIntent(Net::ClientIntentPacket{std::move(*packet)});
  } catch(...) {
    return {.status = ClientMmoSubmitStatus::TransportError};
  }
}

} // namespace Mmo
