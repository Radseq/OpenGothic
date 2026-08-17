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
#include <thread>
#include <utility>
#include <vector>

#include "commandline.h"
#include "mmoclientadapterdetail.h"

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
        // Native Gothic world loading can take longer than the small
        // protocol default. Keep the authoritative route alive while the
        // graphical client is still materialising its scene.
        runtime.acknowledgementTimeout = std::chrono::milliseconds{5'000};
        runtime.serverSilenceTimeout = std::chrono::milliseconds{60'000};
        runtime.strictOverflow = config.strictOverflow;
        if(!config.processGateReportPath.empty())
          runtime.serverSilenceTimeout = std::chrono::milliseconds{1'000};
        facade_ = std::make_unique<ClientSandbox::ClientRuntimeFacade>(std::move(runtime));
        if(!facade_->start()) {
          Tempest::Log::e("MMO client_sandbox facade failed to start endpoint=",
                          config.udpEndpoint);
          facade_.reset();
        } else {
          sessionKey_ = config.sessionKey.empty() ? "local-dev" : config.sessionKey;
          observedReconnectSuccesses_ = facade_->stats().reconnectSuccesses;
          sessionSnapshot_.phase = ClientMmoSessionPhase::Connecting;
          if(!config.processGateReportPath.empty()) {
            ClientSandbox::ClientRuntimeProcessGateConfig gate;
            gate.reportPath = config.processGateReportPath;
            gate.clientId = config.processGateClientId;
            gate.characterName = config.processGateCharacterName;
            gate.guestCredential.assign(
                config.sessionKey.begin(), config.sessionKey.end());
            gate.archetypeId = config.processGateArchetypeId;
            gate.appearanceProfileId = config.processGateAppearanceProfileId;
            gate.contentManifestId = config.processGateContentManifestId;
            gate.mode = ClientSandbox::ClientRuntimeProcessGateMode::Graphical;
            gate.requireServerRestart = config.processGateRequireRestart;
            processGate_ =
                std::make_unique<ClientSandbox::ClientRuntimeProcessGate>(
                    *facade_, std::move(gate));
          }
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

    [[nodiscard]] bool beginSession(
        const ClientMmoSessionRequest& request) {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(!facade_)
        return false;
      constexpr std::size_t MaximumCharacterNameUtf8Bytes = 48U;
      if(request.contentManifestId == 0U ||
         request.characterName.size() > MaximumCharacterNameUtf8Bytes ||
         (request.enterWorld && request.characterId == 0U &&
          request.characterName.empty())) {
        failSession("invalid graphical client session request");
        return false;
      }

      sessionSnapshot_.phase = ClientMmoSessionPhase::Disabled;
      sessionSnapshot_.error.clear();
      sessionRequest_ = request;
      sessionActive_ = true;
      selectedCharacter_.reset();
      createIssued_ = false;
      selectIssued_ = false;
      enterIssued_ = false;

      const auto route = facade_->protocolV2State();
      if(route.routeStage == ClientSandbox::ClientRuntimeRouteStage::Connected) {
        authenticated_ = false;
        authenticationIssued_ = false;
        rosterIssued_ = false;
      } else {
        authenticated_ = true;
        authenticationIssued_ = true;
      }

      if(hasRosterSnapshot_)
        resolveRequestedCharacter();

      if(route.routeStage == ClientSandbox::ClientRuntimeRouteStage::InWorld) {
        if(request.characterId != 0U && request.characterId != route.characterId) {
          failSession("cannot replace an active in-world character without a new logical session");
          return false;
        }
        sessionSnapshot_.characterId = route.characterId;
        setSessionPhase(ClientMmoSessionPhase::InWorld);
        return true;
      }

      setSessionPhase(route.routeBound
                          ? ClientMmoSessionPhase::Authenticating
                          : ClientMmoSessionPhase::Connecting);
      pollSession();
      return !sessionSnapshot_.failed();
#else
      static_cast<void>(request);
      return false;
#endif
    }

    void pollSession() noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(!facade_)
        return;
      try {
        processSessionFaults();
        processSessionTickets();
        processSessionResults();
        processSessionCompletions();
        processReconnect();
        driveSession();
        driveHeartbeat();
      } catch(const std::exception& error) {
        failSession(std::string("graphical MMO session exception: ") + error.what());
      } catch(...) {
        failSession("graphical MMO session failed with an unknown exception");
      }
#endif
    }

    [[nodiscard]] ClientMmoSessionSnapshot sessionSnapshot() const {
      return sessionSnapshot_;
    }

    [[nodiscard]] bool requestInventoryResync() noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(!facade_ || !sessionSnapshot_.inWorld() || resumeTicket_.empty()) {
        return false;
      }
      if(recovering_)
        return true;
      return beginRecovery("unable to restart Protocol V2 for inventory resync");
#else
      return false;
#endif
    }

    [[nodiscard]] ClientMmoSubmitResult submitMovement(
        const ClientMovementIntent& intent) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto request = ClientAdapterDetail::makeProtocolV2MovementRequest(intent))
          return mapResult(facade_->requestMovement(*request));
#else
      static_cast<void>(intent);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitInteraction(
        const ClientInteractionRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2InteractionRequest(request))
          return mapResult(facade_->requestInteract(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitEquipItem(
        const ClientEquipItemRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2EquipItemRequest(request))
          return mapResult(facade_->requestEquipItem(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitUnequipItem(
        const ClientUnequipItemRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2UnequipItemRequest(request))
          return mapResult(facade_->requestUnequipItem(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitUseItem(
        const ClientUseItemRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2UseItemRequest(request))
          return mapResult(facade_->requestUseItem(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitPickupItem(
        const ClientPickupItemRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2PickupItemRequest(request))
          return mapResult(facade_->requestPickupItem(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitDropItem(
        const ClientDropItemRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2DropItemRequest(request))
          return mapResult(facade_->requestDropItem(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitSplitStack(
        const ClientSplitStackRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2SplitStackRequest(request))
          return mapResult(facade_->requestSplitStack(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitMergeStack(
        const ClientMergeStackRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2MergeStackRequest(request))
          return mapResult(facade_->requestMergeStack(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitOpenCorpseLoot(
        const ClientOpenCorpseLootRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2OpenCorpseLootRequest(request))
          return mapResult(facade_->openCorpseLoot(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitTakeCorpseLootStack(
        const ClientTakeCorpseLootStackRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2TakeCorpseLootStackRequest(request))
          return mapResult(facade_->takeCorpseLootStack(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitTakeAllCorpseLoot(
        const ClientTakeAllCorpseLootRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2TakeAllCorpseLootRequest(request))
          return mapResult(facade_->takeAllCorpseLoot(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitCloseCorpseLoot(
        const ClientCloseCorpseLootRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2CloseCorpseLootRequest(request))
          return mapResult(facade_->closeCorpseLoot(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitWeaponState(
        const ClientWeaponStateRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2WeaponStateRequest(request))
          return mapResult(facade_->requestCombatAction(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitCombat(
        const ClientCombatRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2CombatRequest(request))
          return mapResult(facade_->requestCombatAction(*runtime));
#else
      static_cast<void>(request);
#endif
      return {};
    }

    [[nodiscard]] ClientMmoSubmitResult submitDialogChoice(
        const ClientDialogChoiceRequest& request) noexcept {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_)
        if(auto runtime = ClientAdapterDetail::makeProtocolV2DialogChoiceRequest(request))
          return mapResult(facade_->requestDialogChoice(*runtime));
#else
      static_cast<void>(request);
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

    [[nodiscard]] ClientPresentation::ServerPresentationMailboxBatch
    typedPresentationMailbox() {
#if OPENGOTHIC_MMO_SANDBOX_FACADE
      if(facade_) {
        pollSession();
        if(processGate_)
          processGate_->poll();
        auto source = ClientPresentation::drainClientRuntimePresentationMailbox(*facade_);
        if(processGate_) {
          for(const auto& bootstrap : source.bootstraps)
            processGate_->observeBootstrap(bootstrap);
          processGate_->observeEntitySpawns(source.entitySpawns);
          processGate_->observeEntityTransforms(source.entityTransforms.size());
          processGate_->observeMovementCorrections(source.movementCorrections.size());
          processGate_->observeDialogEvents(source.dialogEvents);
          processGate_->observeInteractiveStates(source.interactiveStates);
          processGate_->observeMoverStates(source.moverStates);
        }
        return ClientPresentation::mapClientRuntimePresentationMailbox(
            std::move(source));
      }
#endif
      return {};
    }

    [[nodiscard]] std::vector<ClientMmoCommandCompletion>
    commandCompletions() {
      pollSession();
      auto out = std::move(commandCompletions_);
      commandCompletions_.clear();
      return out;
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

    [[nodiscard]] bool diagnosticsEnabled() const noexcept {
      return diagnostics_.is_open();
    }

  private:
#if OPENGOTHIC_MMO_SANDBOX_FACADE
    using RuntimeCharacterSelection =
        ClientSandbox::ClientRuntimeCharacterSelection;

    [[nodiscard]] static std::string_view sessionPhaseName(
        const ClientMmoSessionPhase phase) noexcept {
      switch(phase) {
        case ClientMmoSessionPhase::Disabled: return "disabled";
        case ClientMmoSessionPhase::Connecting: return "connecting";
        case ClientMmoSessionPhase::Authenticating: return "authenticating";
        case ClientMmoSessionPhase::LoadingRoster: return "loading_roster";
        case ClientMmoSessionPhase::RosterReady: return "roster_ready";
        case ClientMmoSessionPhase::CreatingCharacter: return "creating_character";
        case ClientMmoSessionPhase::SelectingCharacter: return "selecting_character";
        case ClientMmoSessionPhase::EnteringWorld: return "entering_world";
        case ClientMmoSessionPhase::InWorld: return "in_world";
        case ClientMmoSessionPhase::Recovering: return "recovering";
        case ClientMmoSessionPhase::Failed: return "failed";
      }
      return "unknown";
    }

    void setSessionPhase(const ClientMmoSessionPhase next) noexcept {
      if(sessionSnapshot_.phase == next)
        return;
      Tempest::Log::i("MMO graphical session phase ",
                      sessionPhaseName(sessionSnapshot_.phase), " -> ",
                      sessionPhaseName(next));
      sessionSnapshot_.phase = next;
    }

    void failSession(std::string message) noexcept {
      if(sessionSnapshot_.failed())
        return;
      sessionSnapshot_.error = std::move(message);
      setSessionPhase(ClientMmoSessionPhase::Failed);
      sessionActive_ = false;
      Tempest::Log::e("MMO graphical session failed: ", sessionSnapshot_.error);
    }

    [[nodiscard]] static bool isSessionCommand(
        const ClientSandbox::ClientRuntimeV2CommandKind kind) noexcept {
      using Kind = ClientSandbox::ClientRuntimeV2CommandKind;
      return kind == Kind::Authenticate || kind == Kind::ListCharacters ||
             kind == Kind::CreateCharacter || kind == Kind::SelectCharacter ||
             kind == Kind::EnterWorld;
    }

    void processSessionFaults() {
      for(const auto& fault : facade_->drainFaults()) {
        if(!fault.recoverable) {
          failSession("facade fault " + std::to_string(fault.code) + ": " +
                      fault.message);
          return;
        }
      }
    }

    void processSessionTickets() {
      for(auto& ticket : facade_->drainResumeTickets()) {
        if(!ticket.ticket.empty())
          resumeTicket_ = std::move(ticket.ticket);
      }
      for(const auto& accepted : facade_->drainResumeAcceptances()) {
        sessionSnapshot_.accountId = accepted.accountId;
        sessionSnapshot_.rosterRevision = accepted.rosterRevision;
        if(accepted.selectedCharacter.has_value()) {
          selectedCharacter_ = accepted.selectedCharacter;
          sessionSnapshot_.characterId = accepted.selectedCharacter->characterId;
          sessionSnapshot_.characterRevision =
              accepted.selectedCharacter->characterRevision;
        }
        authenticated_ = true;
        recovering_ = false;
      }
    }

    void processSessionResults() {
      for(const auto& result : facade_->drainAuthenticationResults()) {
        sessionSnapshot_.accountId = result.accountId;
        sessionSnapshot_.rosterRevision = result.rosterRevision;
        if(result.selectedCharacter.has_value()) {
          selectedCharacter_ = result.selectedCharacter;
          sessionSnapshot_.characterId = result.selectedCharacter->characterId;
          sessionSnapshot_.characterRevision =
              result.selectedCharacter->characterRevision;
        }
        authenticated_ = true;
      }

      for(const auto& roster : facade_->drainCharacterRosters()) {
        sessionSnapshot_.accountId = roster.accountId;
        sessionSnapshot_.rosterRevision = roster.rosterRevision;
        sessionSnapshot_.characters.clear();
        sessionSnapshot_.characters.reserve(roster.characters.size());
        for(const auto& value : roster.characters) {
          sessionSnapshot_.characters.push_back({
              .characterId = value.characterId,
              .characterRevision = value.characterRevision,
              .archetypeId = value.archetypeId,
              .appearanceProfileId = value.appearanceProfileId,
              .name = value.nameUtf8,
              .temporary = value.temporary,
          });
        }
        hasRosterSnapshot_ = true;
        rosterIssued_ = true;
        resolveRequestedCharacter();
      }
    }

    [[nodiscard]] static bool isPresentationCommand(
        const ClientSandbox::ClientRuntimeV2CommandKind kind) noexcept {
      using Kind = ClientSandbox::ClientRuntimeV2CommandKind;
      return kind == Kind::Interact || kind == Kind::DialogChoice ||
             kind == Kind::EquipItem || kind == Kind::PickupItem ||
             kind == Kind::DropItem || kind == Kind::UnequipItem ||
             kind == Kind::SplitStack || kind == Kind::MergeStack ||
             kind == Kind::UseItem || kind == Kind::SelectiveLoot;
    }

    [[nodiscard]] static ClientMmoCommandKind mapCommandKind(
        const ClientSandbox::ClientRuntimeV2CommandKind kind) noexcept {
      using Source = ClientSandbox::ClientRuntimeV2CommandKind;
      switch(kind) {
        case Source::Interact: return ClientMmoCommandKind::Interact;
        case Source::DialogChoice: return ClientMmoCommandKind::DialogChoice;
        case Source::EquipItem: return ClientMmoCommandKind::EquipItem;
        case Source::PickupItem: return ClientMmoCommandKind::PickupItem;
        case Source::DropItem: return ClientMmoCommandKind::DropItem;
        case Source::UnequipItem: return ClientMmoCommandKind::UnequipItem;
        case Source::SplitStack: return ClientMmoCommandKind::SplitStack;
        case Source::MergeStack: return ClientMmoCommandKind::MergeStack;
        case Source::UseItem: return ClientMmoCommandKind::UseItem;
        case Source::SelectiveLoot: return ClientMmoCommandKind::SelectiveLoot;
        default: return ClientMmoCommandKind::UseItem;
      }
    }

    [[nodiscard]] static ClientMmoCommandToken mapCommandToken(
        const ClientSandbox::ClientRuntimeV2CommandToken& command) noexcept {
      if(!command.valid() || !isPresentationCommand(command.kind))
        return {};
      return {
          .kind = mapCommandKind(command.kind),
          .routeEpoch = command.routeEpoch,
          .sequence = command.sequence,
          .idempotencyKeyHigh = command.idempotencyKeyHigh,
          .idempotencyKeyLow = command.idempotencyKeyLow,
      };
    }

    [[nodiscard]] static ClientMmoCommandCompletionStatus mapCompletionStatus(
        const ClientSandbox::ClientRuntimeV2CompletionStatus status) noexcept {
      using Source = ClientSandbox::ClientRuntimeV2CompletionStatus;
      switch(status) {
        case Source::Applied: return ClientMmoCommandCompletionStatus::Applied;
        case Source::Rejected: return ClientMmoCommandCompletionStatus::Rejected;
        case Source::CancelledByRouteChange:
          return ClientMmoCommandCompletionStatus::CancelledByRouteChange;
        case Source::TimedOut: return ClientMmoCommandCompletionStatus::TimedOut;
        case Source::ConnectionLost:
          return ClientMmoCommandCompletionStatus::ConnectionLost;
        case Source::TransportClosed:
          return ClientMmoCommandCompletionStatus::TransportClosed;
      }
      return ClientMmoCommandCompletionStatus::Rejected;
    }

    [[nodiscard]] static ClientMmoCommandCompletion mapCompletion(
        const ClientSandbox::ClientRuntimeV2CommandCompletion& value) noexcept {
      return {
          .command = mapCommandToken(value.command),
          .status = mapCompletionStatus(value.status),
          .rejectionCode = value.rejectionCode,
          .serverTick = value.serverTick,
          .aggregateRevision = value.aggregateRevision,
      };
    }

    void processSessionCompletions() {
      using Kind = ClientSandbox::ClientRuntimeV2CommandKind;
      for(const auto& completion : facade_->drainProtocolV2CommandCompletions()) {
        if(completion.command.kind == Kind::Heartbeat) {
          heartbeatPending_ = false;
          continue;
        }
        if(!isSessionCommand(completion.command.kind)) {
          if(isPresentationCommand(completion.command.kind)) {
            auto mapped = mapCompletion(completion);
            if(mapped.command.valid())
              commandCompletions_.push_back(mapped);
          }
          if(completion.status ==
             ClientSandbox::ClientRuntimeV2CompletionStatus::Rejected) {
            ++gameplayRejectedCount_;
            if(gameplayRejectedCount_ == 1U ||
               (gameplayRejectedCount_ % 100U) == 0U) {
              Tempest::Log::e(
                  "MMO gameplay command rejected kind=",
                  static_cast<unsigned>(completion.command.kind),
                  " code=", completion.rejectionCode,
                  " rejected_total=", gameplayRejectedCount_);
            }
          }
          continue;
        }
        if(completion.status ==
           ClientSandbox::ClientRuntimeV2CompletionStatus::Rejected) {
          failSession("session command rejected kind=" +
                      std::to_string(static_cast<unsigned>(completion.command.kind)) +
                      " code=" + std::to_string(completion.rejectionCode));
          return;
        }
      }
    }

    [[nodiscard]] bool beginRecovery(const std::string_view failure) noexcept {
      recovering_ = true;
      authenticationIssued_ = false;
      authenticated_ = false;
      heartbeatPending_ = false;
      setSessionPhase(ClientMmoSessionPhase::Recovering);
      if(facade_->restartProtocolV2Session())
        return true;
      failSession(std::string(failure));
      return false;
    }

    void processReconnect() {
      const auto stats = facade_->stats();
      if(stats.reconnectSuccesses <= observedReconnectSuccesses_)
        return;
      observedReconnectSuccesses_ = stats.reconnectSuccesses;
      if(resumeTicket_.empty()) {
        failSession("transport reconnected before a resume ticket was issued");
        return;
      }
      static_cast<void>(beginRecovery(
          "unable to restart Protocol V2 after transport replacement"));
    }

    void resolveRequestedCharacter() noexcept {
      selectedCharacter_.reset();
      const auto found = std::find_if(
          sessionSnapshot_.characters.begin(),
          sessionSnapshot_.characters.end(),
          [this](const ClientMmoCharacter& value) {
            if(sessionRequest_.characterId != 0U)
              return value.characterId == sessionRequest_.characterId;
            return !sessionRequest_.characterName.empty() &&
                   value.name == sessionRequest_.characterName;
          });
      if(found == sessionSnapshot_.characters.end())
        return;
      selectedCharacter_ = RuntimeCharacterSelection{
          .characterId = found->characterId,
          .characterRevision = found->characterRevision,
      };
      sessionSnapshot_.characterId = found->characterId;
      sessionSnapshot_.characterRevision = found->characterRevision;
      createIssued_ = true;
    }

    [[nodiscard]] bool submitAccepted(
        const ClientSandbox::ClientRuntimeV2SubmitResult& result,
        const std::string_view operation) {
      if(result.accepted())
        return true;
      failSession(std::string(operation) + " submission failed status=" +
                  std::to_string(static_cast<unsigned>(result.status)));
      return false;
    }

    void driveSession() {
      const auto route = facade_->protocolV2State();
      sessionSnapshot_.connectionId = route.connectionId;
      sessionSnapshot_.routeEpoch = route.routeEpoch;
      sessionSnapshot_.lastServerTick = route.lastServerTick;
      sessionSnapshot_.worldId = route.world.id;
      sessionSnapshot_.worldGeneration = route.world.generation;
      if(route.accountId != 0U)
        sessionSnapshot_.accountId = route.accountId;
      if(route.characterId != 0U)
        sessionSnapshot_.characterId = route.characterId;

      if(route.phase == ClientSandbox::ClientRuntimeProtocolV2Phase::Rejected) {
        failSession("server rejected Protocol V2 negotiation");
        return;
      }
      if(!route.routeBound || sessionSnapshot_.failed())
        return;

      if(route.routeStage == ClientSandbox::ClientRuntimeRouteStage::Connected &&
         !authenticationIssued_) {
        ClientSandbox::ClientRuntimeAuthenticateRequest request;
        if(recovering_) {
          request.credentialKind =
              ClientSandbox::ClientRuntimeCredentialKind::ResumeTicket;
          request.credential = resumeTicket_;
        } else {
          request.credentialKind =
              ClientSandbox::ClientRuntimeCredentialKind::GuestAdmissionToken;
          constexpr std::size_t MaximumCredentialBytes = 64U;
          const auto size = std::min<std::size_t>(sessionKey_.size(),
                                                  MaximumCredentialBytes);
          request.credential.assign(sessionKey_.begin(),
                                    sessionKey_.begin() + size);
          if(request.credential.empty() ||
             std::none_of(request.credential.begin(), request.credential.end(),
                          [](const auto value) { return value != 0U; })) {
            request.credential = {1U};
          }
        }
        if(submitAccepted(facade_->authenticate(request), "authenticate")) {
          authenticationIssued_ = true;
          setSessionPhase(recovering_ ? ClientMmoSessionPhase::Recovering
                                      : ClientMmoSessionPhase::Authenticating);
        }
        return;
      }

      if(recovering_)
        return;

      if(!sessionActive_) {
        if(route.routeStage == ClientSandbox::ClientRuntimeRouteStage::InWorld)
          setSessionPhase(ClientMmoSessionPhase::InWorld);
        return;
      }

      if(route.routeStage ==
             ClientSandbox::ClientRuntimeRouteStage::AccountAuthenticated &&
         authenticated_ && !rosterIssued_) {
        if(submitAccepted(facade_->listCharacters({.knownRosterRevision = 0U}),
                          "listCharacters")) {
          rosterIssued_ = true;
          setSessionPhase(ClientMmoSessionPhase::LoadingRoster);
        }
        return;
      }

      if(route.routeStage ==
             ClientSandbox::ClientRuntimeRouteStage::AccountAuthenticated &&
         hasRosterSnapshot_ && !selectedCharacter_.has_value()) {
        if(!sessionRequest_.enterWorld && sessionRequest_.characterName.empty() &&
           sessionRequest_.characterId == 0U) {
          sessionActive_ = false;
          setSessionPhase(ClientMmoSessionPhase::RosterReady);
          return;
        }
        if(!sessionRequest_.createIfMissing) {
          failSession("requested character does not exist in the account roster");
          return;
        }
        if(!createIssued_) {
          if(sessionRequest_.characterName.empty() ||
             sessionRequest_.archetypeId == 0U) {
            failSession("character creation requires a name and archetype");
            return;
          }
          if(submitAccepted(facade_->createCharacter({
                                .nameUtf8 = sessionRequest_.characterName,
                                .archetypeId = sessionRequest_.archetypeId,
                                .appearanceProfileId =
                                    sessionRequest_.appearanceProfileId,
                                .expectedRosterRevision =
                                    sessionSnapshot_.rosterRevision,
                              }),
                              "createCharacter")) {
            createIssued_ = true;
            setSessionPhase(ClientMmoSessionPhase::CreatingCharacter);
          }
        }
        return;
      }

      if(route.routeStage ==
             ClientSandbox::ClientRuntimeRouteStage::AccountAuthenticated &&
         selectedCharacter_.has_value() && !selectIssued_) {
        if(submitAccepted(facade_->selectCharacter({
                              .characterId = selectedCharacter_->characterId,
                              .expectedRosterRevision =
                                  sessionSnapshot_.rosterRevision,
                            }),
                            "selectCharacter")) {
          selectIssued_ = true;
          setSessionPhase(ClientMmoSessionPhase::SelectingCharacter);
        }
        return;
      }

      if(route.routeStage ==
             ClientSandbox::ClientRuntimeRouteStage::CharacterSelected &&
         !sessionRequest_.enterWorld) {
        sessionActive_ = false;
        setSessionPhase(ClientMmoSessionPhase::RosterReady);
        return;
      }

      if(route.routeStage ==
             ClientSandbox::ClientRuntimeRouteStage::CharacterSelected &&
         !enterIssued_) {
        const auto revision = selectedCharacter_.has_value()
                                  ? selectedCharacter_->characterRevision
                                  : route.aggregateRevision;
        if(submitAccepted(facade_->enterWorld({
                              .expectedCharacterRevision = revision,
                              .clientContentManifestId =
                                  sessionRequest_.contentManifestId,
                              .lastAppliedServerTick = route.lastServerTick,
                            }),
                            "enterWorld")) {
          enterIssued_ = true;
          setSessionPhase(ClientMmoSessionPhase::EnteringWorld);
        }
        return;
      }

      if(route.routeStage == ClientSandbox::ClientRuntimeRouteStage::InWorld) {
        sessionActive_ = false;
        setSessionPhase(ClientMmoSessionPhase::InWorld);
      }
    }

    void driveHeartbeat() noexcept {
      const auto route = facade_->protocolV2State();
      if(!route.routeBound ||
         route.routeStage != ClientSandbox::ClientRuntimeRouteStage::InWorld)
        return;
      const auto now = std::chrono::steady_clock::now();
      if(heartbeatPending_ && now - heartbeatIssuedAt_ <
                                  std::chrono::milliseconds(1500))
        return;
      if(heartbeatIssuedAt_ != std::chrono::steady_clock::time_point{} &&
         now - heartbeatIssuedAt_ < std::chrono::milliseconds(500))
        return;
      const auto result = facade_->heartbeat({
          .lastAppliedServerTick = route.lastServerTick,
      });
      if(result.accepted()) {
        heartbeatPending_ = true;
        heartbeatIssuedAt_ = now;
      }
    }

    [[nodiscard]] static ClientMmoSubmitResult mapResult(
        const ClientSandbox::ClientRuntimeV2SubmitResult& result) noexcept {
      ClientMmoSubmitResult out;
      out.command = mapCommandToken(result.command);
      out.droppedCount = result.droppedTotal;
      switch(result.status) {
        case ClientSandbox::ClientRuntimeV2SubmitStatus::Accepted:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::DeferredUntilBaseline:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::Coalesced:
          out.status = ClientMmoSubmitStatus::Accepted;
          break;
        case ClientSandbox::ClientRuntimeV2SubmitStatus::LocalValidationFailed:
          out.status = ClientMmoSubmitStatus::InvalidIntent;
          break;
        case ClientSandbox::ClientRuntimeV2SubmitStatus::ProtocolNotNegotiated:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::RouteUnavailable:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::RouteStageRejected:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::CapabilityNotNegotiated:
          out.status = ClientMmoSubmitStatus::UnsupportedIntent;
          break;
        case ClientSandbox::ClientRuntimeV2SubmitStatus::QueueFull:
          out.status = ClientMmoSubmitStatus::QueueFull;
          break;
        case ClientSandbox::ClientRuntimeV2SubmitStatus::SequenceExhausted:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::EncodingFailed:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::Closed:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::TransportUnavailable:
        case ClientSandbox::ClientRuntimeV2SubmitStatus::InternalError:
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
    std::vector<ClientMmoCommandCompletion> commandCompletions_;
#if OPENGOTHIC_MMO_SANDBOX_FACADE
    std::unique_ptr<ClientSandbox::ClientRuntimeFacade> facade_;
    std::unique_ptr<ClientSandbox::ClientRuntimeProcessGate> processGate_;
    ClientMmoSessionRequest sessionRequest_;
    ClientMmoSessionSnapshot sessionSnapshot_;
    std::optional<RuntimeCharacterSelection> selectedCharacter_;
    std::vector<std::uint8_t> resumeTicket_;
    std::string sessionKey_;
    std::chrono::steady_clock::time_point heartbeatIssuedAt_{};
    std::uint64_t observedReconnectSuccesses_ = 0;
    std::uint64_t gameplayRejectedCount_ = 0;
    bool sessionActive_ = false;
    bool authenticationIssued_ = false;
    bool authenticated_ = false;
    bool rosterIssued_ = false;
    bool hasRosterSnapshot_ = false;
    bool createIssued_ = false;
    bool selectIssued_ = false;
    bool enterIssued_ = false;
    bool recovering_ = false;
    bool heartbeatPending_ = false;
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

bool beginClientMmoSession(const ClientMmoSessionRequest& request) {
  std::lock_guard lock(stateMutex);
  return state && state->beginSession(request);
}

void pollClientMmoSession() noexcept {
  std::lock_guard lock(stateMutex);
  if(state)
    state->pollSession();
}

bool requestClientMmoInventoryResync() noexcept {
  std::lock_guard lock(stateMutex);
  return state && state->requestInventoryResync();
}

ClientMmoSessionSnapshot clientMmoSessionSnapshot() {
  std::lock_guard lock(stateMutex);
  return state ? state->sessionSnapshot() : ClientMmoSessionSnapshot{};
}

ClientMmoSessionSnapshot waitForClientMmoSession(
    const ClientMmoSessionRequest& request,
    const std::uint64_t timeoutMilliseconds) {
  if(!beginClientMmoSession(request))
    return clientMmoSessionSnapshot();

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMilliseconds);
  for(;;) {
    pollClientMmoSession();
    auto snapshot = clientMmoSessionSnapshot();
    const bool ready = request.enterWorld ? snapshot.inWorld()
                                          : snapshot.rosterReady();
    if(ready || snapshot.failed() ||
       std::chrono::steady_clock::now() >= deadline) {
      if(!ready && !snapshot.failed()) {
        Tempest::Log::e("MMO graphical session timed out phase=",
                        static_cast<unsigned>(snapshot.phase),
                        " timeout_ms=", timeoutMilliseconds);
      }
      return snapshot;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

ClientMmoSessionSnapshot waitForClientMmoRoster(
    const std::uint64_t timeoutMilliseconds) {
  ClientMmoSessionRequest request;
  request.createIfMissing = false;
  request.enterWorld = false;
  return waitForClientMmoSession(request, timeoutMilliseconds);
}

ClientMmoSubmitResult submitProtocolV2Movement(
    const ClientMovementIntent& intent) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitMovement(intent) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2Interaction(
    const ClientInteractionRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitInteraction(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2EquipItem(
    const ClientEquipItemRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitEquipItem(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2UnequipItem(
    const ClientUnequipItemRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitUnequipItem(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2UseItem(
    const ClientUseItemRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitUseItem(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2PickupItem(
    const ClientPickupItemRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitPickupItem(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2DropItem(
    const ClientDropItemRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitDropItem(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2SplitStack(
    const ClientSplitStackRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitSplitStack(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2MergeStack(
    const ClientMergeStackRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitMergeStack(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2OpenCorpseLoot(
    const ClientOpenCorpseLootRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitOpenCorpseLoot(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2TakeCorpseLootStack(
    const ClientTakeCorpseLootStackRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitTakeCorpseLootStack(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2TakeAllCorpseLoot(
    const ClientTakeAllCorpseLootRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitTakeAllCorpseLoot(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2CloseCorpseLoot(
    const ClientCloseCorpseLootRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitCloseCorpseLoot(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2WeaponState(
    const ClientWeaponStateRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitWeaponState(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2Combat(
    const ClientCombatRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitCombat(request) : ClientMmoSubmitResult{};
}

ClientMmoSubmitResult submitProtocolV2DialogChoice(
    const ClientDialogChoiceRequest& request) noexcept {
  std::lock_guard lock(stateMutex);
  return state ? state->submitDialogChoice(request) : ClientMmoSubmitResult{};
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

std::vector<ClientMmoCommandCompletion>
drainClientMmoCommandCompletions() noexcept {
  std::lock_guard lock(stateMutex);
  if(!state)
    return {};
  try {
    return state->commandCompletions();
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

} // namespace Mmo
