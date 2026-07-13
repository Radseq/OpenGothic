#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../../shared/game/mmo/mmosemanticevents.h"
#include "../../../shared/net/mmo/mmo_client_intent.h"
#include "../../../shared/net/mmo/mmonetprotocol.h"
#include "mmoclientadapter.h"
#include "mmoserverpresentationmailbox.h"

class CommandLine;

namespace Mmo {

struct ClientMmoBridgeConfig final {
  std::string diagnosticsJsonlPath;
  std::string udpEndpoint;
  std::string sessionKey = "local-dev";
  std::size_t queueCapacity = 4096;
  std::size_t bootstrapCapacity = 4;
  bool strictOverflow = false;
  bool serverBoundClientMode = false;
  std::string processGateReportPath;
  std::string processGateClientId = "graphical";
  std::string processGateCharacterName = "ProcessGateHero";
  std::uint32_t processGateArchetypeId = 1;
  std::uint32_t processGateAppearanceProfileId = 1;
  std::uint64_t processGateContentManifestId = 1;
  bool processGateRequireRestart = true;
};

enum class ClientMmoProcessGatePresentationEvent : std::uint8_t {
  RouteApplied,
  BootstrapApplied,
  LocalPlayerMaterialized,
  RemotePlayerMaterialized,
  NpcMaterialized,
  EntityDespawnApplied,
  TransformApplied,
  MovementCorrectionApplied,
  NpcStateApplied,
  DialogApplied,
  InteractiveApplied,
  MoverApplied,
  RenderedFrame,
};

struct ServerBootstrapSnapshot final {
  std::uint32_t snapshotId = 0;
  std::uint16_t chunkCount = 0;
  std::string payload;
};

struct ServerBootstrapStatus final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  bool accepted = false;
  bool ready = false;
  std::string message;

  [[nodiscard]] constexpr bool rejected() const noexcept {
    return !accepted;
  }
};

[[nodiscard]] bool isClientMmoDiagnosticsEnabled() noexcept;
[[nodiscard]] bool isServerBoundClientModeEnabled() noexcept;
[[nodiscard]] bool isClientMmoProcessGateEnabled() noexcept;
void recordClientMmoProcessGatePresentation(
    ClientMmoProcessGatePresentationEvent event,
    std::uint64_t amount = 1) noexcept;
[[nodiscard]] std::uint64_t nextClientIntentSequence() noexcept;
[[nodiscard]] std::string_view clientMmoSessionKey() noexcept;

[[nodiscard]] ClientMmoSubmitResult submitClientIntent(
    Net::ClientIntentPacket intent) noexcept;

// JSON exists only at this local diagnostic boundary. Diagnostic envelopes are
// never parsed back into packets and never sent over the network.
void recordClientMmoDiagnostic(SemanticActionEnvelope envelope) noexcept;

void configureClientMmoBridge(const ClientMmoBridgeConfig& config);
void configureClientMmoBridge(const CommandLine& commandLine);
void shutdownClientMmoBridge() noexcept;
void flushClientMmoBridge() noexcept;

[[nodiscard]] std::vector<Net::ServerLiveDeltaPacket> drainServerLiveDeltas() noexcept;
[[nodiscard]] ClientPresentation::ServerPresentationMailboxBatch
drainTypedServerPresentationMailbox() noexcept;
[[nodiscard]] std::vector<ServerBootstrapSnapshot>
drainServerBootstrapSnapshots() noexcept;
[[nodiscard]] std::optional<ServerBootstrapSnapshot>
latestServerBootstrapSnapshot() noexcept;
[[nodiscard]] std::vector<ServerBootstrapStatus>
drainServerBootstrapStatuses() noexcept;
[[nodiscard]] std::optional<ServerBootstrapStatus>
latestServerBootstrapStatus() noexcept;
void resetServerBootstrapStatus() noexcept;
[[nodiscard]] bool enqueueClientGameplayObservationReceipt(
    Net::ClientGameplayObservationPacket packet) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitClientDialogChoicePacket(
    Net::ClientDialogChoiceIntentPacket packet) noexcept;

} // namespace Mmo
