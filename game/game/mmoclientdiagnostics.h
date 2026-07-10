#pragma once

#include <string>

#include "../../../shared/net/mmo/mmonetprotocol.h"
#include "mmoserverdialogpresentation.h"

namespace Mmo::ClientDiagnostics {

[[nodiscard]] std::string liveDeltaJson(const Net::ServerLiveDeltaPacket& delta);
[[nodiscard]] std::string dialogPresentationDecisionJson(
    const ServerDialogPresentationDecision& decision);
[[nodiscard]] std::string npcDialogIntentJson(
    const Net::ServerNpcDialogIntentPacket& intent,
    bool duplicateDelivery,
    const ServerDialogPresentationDecision& decision);

} // namespace Mmo::ClientDiagnostics
