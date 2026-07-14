#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../../../shared/game/mmo/mmosemanticevents.h"
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


enum class ClientMmoSessionPhase : std::uint8_t {
  Disabled,
  Connecting,
  Authenticating,
  LoadingRoster,
  RosterReady,
  CreatingCharacter,
  SelectingCharacter,
  EnteringWorld,
  InWorld,
  Recovering,
  Failed,
};

struct ClientMmoCharacter final {
  std::uint64_t characterId = 0;
  std::uint64_t characterRevision = 0;
  std::uint32_t archetypeId = 0;
  std::uint32_t appearanceProfileId = 0;
  std::string name;
  bool temporary = false;
};

struct ClientMmoSessionRequest final {
  std::uint64_t characterId = 0;
  std::string characterName;
  std::uint32_t archetypeId = 1;
  std::uint32_t appearanceProfileId = 1;
  std::uint64_t contentManifestId = 1;
  bool createIfMissing = true;
  bool enterWorld = true;
};

struct ClientMmoSessionSnapshot final {
  ClientMmoSessionPhase phase = ClientMmoSessionPhase::Disabled;
  std::uint64_t connectionId = 0;
  std::uint64_t routeEpoch = 0;
  std::uint64_t accountId = 0;
  std::uint64_t characterId = 0;
  std::uint64_t characterRevision = 0;
  std::uint64_t rosterRevision = 0;
  std::uint64_t lastServerTick = 0;
  std::uint64_t worldId = 0;
  std::uint32_t worldGeneration = 0;
  std::vector<ClientMmoCharacter> characters;
  std::string error;

  [[nodiscard]] constexpr bool rosterReady() const noexcept {
    return phase == ClientMmoSessionPhase::RosterReady ||
           phase == ClientMmoSessionPhase::CreatingCharacter ||
           phase == ClientMmoSessionPhase::SelectingCharacter ||
           phase == ClientMmoSessionPhase::EnteringWorld ||
           phase == ClientMmoSessionPhase::InWorld;
  }

  [[nodiscard]] constexpr bool inWorld() const noexcept {
    return phase == ClientMmoSessionPhase::InWorld;
  }

  [[nodiscard]] constexpr bool failed() const noexcept {
    return phase == ClientMmoSessionPhase::Failed;
  }
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


[[nodiscard]] bool beginClientMmoSession(
    const ClientMmoSessionRequest& request);
void pollClientMmoSession() noexcept;
[[nodiscard]] ClientMmoSessionSnapshot clientMmoSessionSnapshot();
[[nodiscard]] ClientMmoSessionSnapshot waitForClientMmoSession(
    const ClientMmoSessionRequest& request,
    std::uint64_t timeoutMilliseconds);
[[nodiscard]] ClientMmoSessionSnapshot waitForClientMmoRoster(
    std::uint64_t timeoutMilliseconds);

[[nodiscard]] ClientMmoSubmitResult submitProtocolV2Movement(
    const ClientMovementIntent& intent) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitProtocolV2Interaction(
    const ClientInteractionRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitProtocolV2WeaponState(
    const ClientWeaponStateRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitProtocolV2Combat(
    const ClientCombatRequest& request) noexcept;
[[nodiscard]] ClientMmoSubmitResult submitProtocolV2DialogChoice(
    const ClientDialogChoiceRequest& request) noexcept;

// JSON exists only at this local diagnostic boundary. Diagnostic envelopes are
// never parsed back into packets and never sent over the network.
void recordClientMmoDiagnostic(SemanticActionEnvelope envelope) noexcept;

void configureClientMmoBridge(const ClientMmoBridgeConfig& config);
void configureClientMmoBridge(const CommandLine& commandLine);
void shutdownClientMmoBridge() noexcept;
void flushClientMmoBridge() noexcept;

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
} // namespace Mmo
