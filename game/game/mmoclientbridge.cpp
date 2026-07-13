#include "mmoclientbridge.h"

#include <Tempest/Log>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "commandline.h"

#ifndef OPENGOTHIC_MMO_SANDBOX_FACADE
#define OPENGOTHIC_MMO_SANDBOX_FACADE 0
#endif

#if OPENGOTHIC_MMO_SANDBOX_FACADE
#include <gothic/mmo/client_runtime_facade.h>
#include <gothic/mmo/client_runtime_process_gate.h>
#include "mmoserverpresentationfacadeadapter.h"
#endif

namespace Mmo {
namespace {

class ClientMmoBridgeState final {
  public:
    explicit ClientMmoBridgeState(const ClientMmoBridgeConfig& config) {
      if(!config.diagnosticsJsonlPath.empty()) {
        diagnostics_.open(config.diagnosticsJsonlPath,
                          std::ios::out | std::ios::app | std::ios::binary);
        if(!diagnostics_.is_open()) {
          Tempest::Log::e("MMO diagnostic JSONL: unable to open ",
                          config.diagnosticsJsonlPath);
        }
      }
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(config.serverBoundClientMode && !config.udpEndpoint.empty()) {
        ClientSandbox::ClientRuntimeFacadeConfig runtime;
        runtime.endpoint = config.udpEndpoint;
        runtime.sessionKey = config.sessionKey.empty() ? "local-dev" : config.sessionKey;
        runtime.outgoingCapacity = std::max<std::size_t>(config.queueCapacity, 1);
        runtime.bootstrapCapacity = std::max<std::size_t>(config.bootstrapCapacity, 1);
        runtime.strictOverflow = config.strictOverflow;
        if(!config.processGateReportPath.empty())
          runtime.serverSilenceTimeout = std::chrono::milliseconds{1'000};
        facade_ = std::make_unique<ClientSandbox::ClientRuntimeFacade>(std::move(runtime));
        if(!facade_->start()) {
          Tempest::Log::e("MMO client_sandbox facade failed to start endpoint=",
                          config.udpEndpoint);
          facade_.reset();
        } else if(!config.processGateReportPath.empty()) {
          ClientSandbox::ClientRuntimeProcessGateConfig gate;
          gate.reportPath = config.processGateReportPath;
          gate.clientId = config.processGateClientId;
          gate.characterName = config.processGateCharacterName;
          gate.guestCredential.assign(config.sessionKey.begin(), config.sessionKey.end());
          gate.archetypeId = config.processGateArchetypeId;
          gate.appearanceProfileId = config.processGateAppearanceProfileId;
          gate.contentManifestId = config.processGateContentManifestId;
          gate.mode = ClientSandbox::ClientRuntimeProcessGateMode::Graphical;
          gate.requireServerRestart = config.processGateRequireRestart;
          processGate_ = std::make_unique<ClientSandbox::ClientRuntimeProcessGate>(
              *facade_, std::move(gate));
        }
      }
#else
      if(config.serverBoundClientMode && !config.udpEndpoint.empty()) {
        Tempest::Log::e(
            "MMO binary transport unavailable: full client must link client_sandbox facade");
      }
#endif
    }

    ~ClientMmoBridgeState() {
      flush();
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        facade_->stop();
#endif
    }

    [[nodiscard]] ClientMmoSubmitResult submit(Net::ClientIntentPacket intent) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_) {
        const auto result = facade_->submitIntent(std::move(intent));
        return mapResult(result);
      }
#else
      static_cast<void>(intent);
#endif
      return {};
    }

    void diagnostic(SemanticActionEnvelope envelope) noexcept {
      if(!diagnostics_.is_open() || !isValidEnvelope(envelope))
        return;
      try {
        const auto line = toJsonLine(envelope);
        std::lock_guard lock(diagnosticMutex_);
        diagnostics_.write(line.data(), static_cast<std::streamsize>(line.size()));
        diagnostics_.put('\n');
      } catch(...) {
      }
    }

    void flush() noexcept {
      {
        std::lock_guard lock(diagnosticMutex_);
        if(diagnostics_.is_open())
          diagnostics_.flush();
      }
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        facade_->flush();
#endif
    }

    [[nodiscard]] std::vector<Net::ServerLiveDeltaPacket> liveDeltas() {
      // Protocol V2 exposes typed domain mailboxes. The legacy aggregate live
      // delta has no lossless mapping and remains inactive until the typed
      // presentation adapter consumes those mailboxes directly.
      return {};
    }

    [[nodiscard]] ClientPresentation::ServerPresentationMailboxBatch
    typedPresentationMailbox() {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_) {
        if(processGate_)
          processGate_->poll();
        auto source = ClientPresentation::drainClientRuntimePresentationMailbox(*facade_);
        if(processGate_) {
          for(const auto& bootstrap : source.bootstraps)
            processGate_->observeBootstrap(bootstrap);
          processGate_->observeEntitySpawns(source.entitySpawns);
          processGate_->observeEntityTransforms(source.entityTransforms.size());
          processGate_->observeMovementCorrections(source.movementCorrections.size());
          processGate_->observeDialogEvents(source.dialogEvents.size());
          processGate_->observeInteractiveStates(source.interactiveStates.size());
          processGate_->observeMoverStates(source.moverStates.size());
        }
        return ClientPresentation::mapClientRuntimePresentationMailbox(
            std::move(source));
      }
#endif
      return {};
    }

    void recordProcessGatePresentation(
        const ClientMmoProcessGatePresentationEvent event,
        const std::uint64_t amount) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(!processGate_)
        return;
      using Source = ClientMmoProcessGatePresentationEvent;
      using Target = ClientSandbox::ClientRuntimeProcessGatePresentationEvent;
      Target mapped = Target::RouteApplied;
      switch(event) {
        case Source::RouteApplied: mapped = Target::RouteApplied; break;
        case Source::BootstrapApplied: mapped = Target::BootstrapApplied; break;
        case Source::LocalPlayerMaterialized: mapped = Target::LocalPlayerMaterialized; break;
        case Source::RemotePlayerMaterialized: mapped = Target::RemotePlayerMaterialized; break;
        case Source::NpcMaterialized: mapped = Target::NpcMaterialized; break;
        case Source::EntityDespawnApplied: mapped = Target::EntityDespawnApplied; break;
        case Source::TransformApplied: mapped = Target::TransformApplied; break;
        case Source::MovementCorrectionApplied: mapped = Target::MovementCorrectionApplied; break;
        case Source::NpcStateApplied: mapped = Target::NpcStateApplied; break;
        case Source::DialogApplied: mapped = Target::DialogApplied; break;
        case Source::InteractiveApplied: mapped = Target::InteractiveApplied; break;
        case Source::MoverApplied: mapped = Target::MoverApplied; break;
        case Source::RenderedFrame: mapped = Target::RenderedFrame; break;
      }
      processGate_->observePresentation(mapped, amount);
      processGate_->poll();
#else
      static_cast<void>(event);
      static_cast<void>(amount);
#endif
    }

    [[nodiscard]] bool processGateEnabled() const noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      return processGate_ != nullptr;
#else
      return false;
#endif
    }

    [[nodiscard]] std::vector<ServerBootstrapSnapshot> bootstrapSnapshots() {
      // Completed Protocol V2 bootstraps are typed binary projections. They
      // must not be serialized into the removed legacy string snapshot.
      return {};
    }

    [[nodiscard]] std::optional<ServerBootstrapSnapshot> latestBootstrapSnapshot() const {
      return latestBootstrap_;
    }

    [[nodiscard]] std::vector<ServerBootstrapStatus> bootstrapStatuses() {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_) {
        std::vector<ServerBootstrapStatus> out;
        for(const auto& ack : facade_->drainServerAcks()) {
          if(ack.kind != Net::ServerAckKind::Bootstrap)
            continue;
          ServerBootstrapStatus status;
          status.packetSequence = ack.packetSequence;
          status.localSequence = ack.localSequence;
          status.accepted = ack.accepted;
          status.ready = ack.ready;
          status.message = !ack.accepted
              ? "Server rejected bootstrap request"
              : (ack.ready ? "Server bootstrap is ready"
                           : "Server accepted bootstrap request but is not ready");
          latestBootstrapStatus_ = status;
          out.push_back(std::move(status));
        }
        return out;
      }
#endif
      return {};
    }

    [[nodiscard]] std::optional<ServerBootstrapStatus> latestBootstrapStatus() {
      static_cast<void>(bootstrapStatuses());
      return latestBootstrapStatus_;
    }

    void resetBootstrapStatus() {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        static_cast<void>(facade_->drainServerAcks());
#endif
      latestBootstrapStatus_.reset();
    }

    [[nodiscard]] bool submitObservation(
        Net::ClientGameplayObservationPacket packet) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      return facade_ && facade_->submitGameplayObservation(std::move(packet)).accepted();
#else
      static_cast<void>(packet);
      return false;
#endif
    }

    [[nodiscard]] ClientMmoSubmitResult submitDialogChoice(
        Net::ClientDialogChoiceIntentPacket packet) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        return mapResult(facade_->submitDialogChoice(std::move(packet)));
#else
      static_cast<void>(packet);
#endif
      return {};
    }

    [[nodiscard]] bool diagnosticsEnabled() const noexcept {
      return diagnostics_.is_open();
    }

  private:
#if OPENGOTHIC_MMO_SANDBOX_FACADE
    [[nodiscard]] static ClientMmoSubmitResult mapResult(
        ClientSandbox::ClientRuntimeSubmitResult result) noexcept {
      ClientMmoSubmitResult out;
      out.droppedCount = result.droppedTotal;
      switch(result.status) {
        case ClientSandbox::ClientRuntimeSubmitStatus::Accepted:
        case ClientSandbox::ClientRuntimeSubmitStatus::AcceptedAfterDrop:
          out.status = ClientMmoSubmitStatus::Accepted;
          break;
        case ClientSandbox::ClientRuntimeSubmitStatus::RejectedInvalid:
          out.status = ClientMmoSubmitStatus::InvalidIntent;
          break;
        case ClientSandbox::ClientRuntimeSubmitStatus::RejectedUnsupportedIntent:
          out.status = ClientMmoSubmitStatus::UnsupportedIntent;
          break;
        case ClientSandbox::ClientRuntimeSubmitStatus::RejectedQueueFull:
          out.status = ClientMmoSubmitStatus::QueueFull;
          break;
        case ClientSandbox::ClientRuntimeSubmitStatus::RejectedOversized:
        case ClientSandbox::ClientRuntimeSubmitStatus::RejectedClosed:
        case ClientSandbox::ClientRuntimeSubmitStatus::TransportUnavailable:
        case ClientSandbox::ClientRuntimeSubmitStatus::InternalError:
          out.status = ClientMmoSubmitStatus::TransportError;
          break;
      }
      return out;
    }
#endif

    std::ofstream diagnostics_;
    std::mutex diagnosticMutex_;
    std::optional<ServerBootstrapSnapshot> latestBootstrap_;
    std::optional<ServerBootstrapStatus> latestBootstrapStatus_;
#if OPENGOTHIC_MMO_SANDBOX_FACADE
    std::unique_ptr<ClientSandbox::ClientRuntimeFacade> facade_;
    std::unique_ptr<ClientSandbox::ClientRuntimeProcessGate> processGate_;
#endif
};

std::mutex stateMutex;
std::unique_ptr<ClientMmoBridgeState> state;
std::atomic_bool diagnosticsEnabled{false};
std::atomic_bool serverBoundMode{false};
std::atomic_bool processGateEnabled{false};
std::atomic_uint64_t sequence{0};
std::string sessionKey = "local-dev";

} // namespace

bool isClientMmoDiagnosticsEnabled() noexcept {
  return diagnosticsEnabled.load(std::memory_order_relaxed);
}

bool isServerBoundClientModeEnabled() noexcept {
  return serverBoundMode.load(std::memory_order_relaxed);
}

bool isClientMmoProcessGateEnabled() noexcept {
  return processGateEnabled.load(std::memory_order_relaxed);
}

void recordClientMmoProcessGatePresentation(
    const ClientMmoProcessGatePresentationEvent event,
    const std::uint64_t amount) noexcept {
  if(!isClientMmoProcessGateEnabled())
    return;
  std::lock_guard lock(stateMutex);
  if(state)
    state->recordProcessGatePresentation(event, amount);
}

std::uint64_t nextClientIntentSequence() noexcept {
  return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
}

std::string_view clientMmoSessionKey() noexcept {
  return sessionKey;
}

ClientMmoSubmitResult submitClientIntent(Net::ClientIntentPacket intent) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submit(std::move(intent)) : ClientMmoSubmitResult{};
}

void recordClientMmoDiagnostic(SemanticActionEnvelope envelope) noexcept {
  std::lock_guard lock(stateMutex);
  if(state)
    state->diagnostic(std::move(envelope));
}

void configureClientMmoBridge(const ClientMmoBridgeConfig& config) {
  std::lock_guard lock(stateMutex);
  state.reset();
  processGateEnabled.store(false, std::memory_order_relaxed);
  sessionKey = config.sessionKey.empty() ? "local-dev" : config.sessionKey;
  serverBoundMode.store(config.serverBoundClientMode, std::memory_order_relaxed);
  if(config.diagnosticsJsonlPath.empty() && config.udpEndpoint.empty()) {
    diagnosticsEnabled.store(false, std::memory_order_relaxed);
    processGateEnabled.store(false, std::memory_order_relaxed);
    return;
  }
  state = std::make_unique<ClientMmoBridgeState>(config);
  diagnosticsEnabled.store(state->diagnosticsEnabled(), std::memory_order_relaxed);
  processGateEnabled.store(state->processGateEnabled(), std::memory_order_relaxed);
  if(!config.diagnosticsJsonlPath.empty())
    Tempest::Log::i("MMO JSONL diagnostics enabled: ", config.diagnosticsJsonlPath);
  if(config.serverBoundClientMode && !config.udpEndpoint.empty())
    Tempest::Log::i("MMO binary transport owned by client_sandbox endpoint=",
                    config.udpEndpoint);
}

void configureClientMmoBridge(const CommandLine& commandLine) {
  ClientMmoBridgeConfig config;
  config.diagnosticsJsonlPath = std::string(commandLine.mmoActionJsonl());
  config.udpEndpoint = std::string(commandLine.mmoActionUdpEndpoint());
  config.sessionKey = std::string(commandLine.mmoActionSessionKey());
  config.queueCapacity = commandLine.mmoActionQueueCapacity();
  config.strictOverflow = commandLine.mmoActionStrictOverflow();
  config.serverBoundClientMode = commandLine.mmoClientUsesServer();
  config.processGateReportPath = std::string(commandLine.mmoProcessGateReport());
  config.processGateClientId = std::string(commandLine.mmoProcessGateClientId());
  config.processGateCharacterName = std::string(commandLine.mmoCharacterDisplayName());
  config.processGateArchetypeId = commandLine.mmoProcessGateArchetypeId();
  config.processGateAppearanceProfileId =
      commandLine.mmoProcessGateAppearanceProfileId();
  config.processGateContentManifestId =
      commandLine.mmoProcessGateContentManifestId();
  config.processGateRequireRestart = commandLine.mmoProcessGateRequireRestart();
  configureClientMmoBridge(config);
}

void shutdownClientMmoBridge() noexcept {
  std::lock_guard lock(stateMutex);
  state.reset();
  diagnosticsEnabled.store(false, std::memory_order_relaxed);
  serverBoundMode.store(false, std::memory_order_relaxed);
  processGateEnabled.store(false, std::memory_order_relaxed);
}

void flushClientMmoBridge() noexcept {
  std::lock_guard lock(stateMutex);
  if(state)
    state->flush();
}

std::vector<Net::ServerLiveDeltaPacket> drainServerLiveDeltas() noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->liveDeltas() : std::vector<Net::ServerLiveDeltaPacket>{};
}

ClientPresentation::ServerPresentationMailboxBatch
drainTypedServerPresentationMailbox() noexcept {
  std::lock_guard lock(stateMutex);
  if(!state)
    return {};
  try {
    return state->typedPresentationMailbox();
  } catch(...) {
    return {};
  }
}

std::vector<ServerBootstrapSnapshot> drainServerBootstrapSnapshots() noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->bootstrapSnapshots() : std::vector<ServerBootstrapSnapshot>{};
}

std::optional<ServerBootstrapSnapshot> latestServerBootstrapSnapshot() noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->latestBootstrapSnapshot() : std::nullopt;
}

std::vector<ServerBootstrapStatus> drainServerBootstrapStatuses() noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->bootstrapStatuses() : std::vector<ServerBootstrapStatus>{};
}

std::optional<ServerBootstrapStatus> latestServerBootstrapStatus() noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->latestBootstrapStatus() : std::nullopt;
}

void resetServerBootstrapStatus() noexcept {
  std::lock_guard lock(stateMutex);
  if(state)
    state->resetBootstrapStatus();
}

bool enqueueClientGameplayObservationReceipt(
    Net::ClientGameplayObservationPacket packet) noexcept {
  std::lock_guard lock(stateMutex);
  return state && state->submitObservation(std::move(packet));
}

ClientMmoSubmitResult submitClientDialogChoicePacket(
    Net::ClientDialogChoiceIntentPacket packet) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitDialogChoice(std::move(packet))
               : ClientMmoSubmitResult{};
}

} // namespace Mmo
