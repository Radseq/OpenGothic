#include "mmosemanticactionsink.h"

#include <Tempest/Log>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wconversion"
#endif
#if defined(__has_include)
#  if __has_include(<asio.hpp>)
#    include <asio.hpp>
#  elif __has_include("../../thirdparty/asio/include/asio.hpp")
#    include "../../thirdparty/asio/include/asio.hpp"
#  else
#    error "MMO ASIO UDP transport requires thirdparty/asio/include/asio.hpp"
#  endif
#else
#  include <asio.hpp>
#endif
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include "commandline.h"
#include "mmoclientdiagnostics.h"
#include "mmoclientjsonfields.h"
#include "mmoclientpacketbuilder.h"
#include "../../../client_sandbox/mmo_client_exchange_queue.h"
#include "../../../client_sandbox/mmo_udp_endpoint.h"
#include "../../../shared/net/mmo/mmo_bootstrap_assembly.h"
#include "../../../shared/net/mmo/mmo_bounded_mailbox.h"
#include "../../../shared/net/mmo/mmo_recent_cache.h"
#include "../../../shared/net/mmo/mmo_server_packet_dispatch.h"
#include "../../../shared/net/mmo/mmonetprotocol.h"
#include "mmoserverdialogpresentation.h"

namespace Mmo {
namespace {

struct UdpTarget final {
  asio::ip::udp::endpoint endpoint;
};

std::optional<UdpTarget> resolveUdpTarget(asio::io_context& io, std::string_view endpoint) {
  const auto address = ClientSandbox::parseUdpEndpointAddress(endpoint);
  if(!address)
    return std::nullopt;

  asio::error_code ec;
  asio::ip::udp::resolver resolver(io);
  auto results = resolver.resolve(asio::ip::udp::v4(),
                                  address->host,
                                  std::to_string(address->port),
                                  ec);
  if(ec || results.empty())
    return std::nullopt;
  return UdpTarget {results.begin()->endpoint()};
}

constexpr std::size_t MaxQueuedServerLiveDeltas = 512;
constexpr std::size_t MaxQueuedServerDialogPresentationEvents = 256;
constexpr std::size_t MaxQueuedClientGameplayObservationReceipts = 256;
constexpr std::size_t MaxRememberedDialogIntentReceipts = 4096;
Net::BoundedMailbox<Net::ServerLiveDeltaPacket> serverLiveDeltaInbox {
  MaxQueuedServerLiveDeltas, Net::MailboxOverflowPolicy::DropOldest};
Net::BoundedMailbox<ServerDialogPresentationEvent> serverDialogPresentationInbox {
  MaxQueuedServerDialogPresentationEvents, Net::MailboxOverflowPolicy::RejectNewest};
Net::BoundedMailbox<Net::ClientGameplayObservationPacket> clientGameplayObservationReceiptOutbox {
  MaxQueuedClientGameplayObservationReceipts, Net::MailboxOverflowPolicy::RejectNewest};
std::atomic_uint64_t serverDialogPresentationReceivedOrder {0};
std::atomic_uint64_t clientGameplayObservationReceiptSequence {0};

void enqueueServerLiveDelta(Net::ServerLiveDeltaPacket delta) noexcept {
  static_cast<void>(serverLiveDeltaInbox.push(std::move(delta)));
}

[[nodiscard]] bool enqueueServerDialogPresentationEvent(ServerDialogPresentationEvent event) noexcept {
  event.receivedOrder = serverDialogPresentationReceivedOrder.fetch_add(1, std::memory_order_relaxed) + 1;
  return serverDialogPresentationInbox.push(std::move(event)) != Net::MailboxPushStatus::RejectedFull;
}

[[nodiscard]] bool enqueueClientGameplayObservationReceiptInternal(Net::ClientGameplayObservationPacket packet) noexcept {
  if(packet.actionId.empty() || packet.ackKey.empty())
    return false;
  packet.packetSequence = clientGameplayObservationReceiptSequence.fetch_add(1, std::memory_order_relaxed) + 1;
  return clientGameplayObservationReceiptOutbox.push(std::move(packet)) != Net::MailboxPushStatus::RejectedFull;
}

ServerDialogPresentationConfig makeServerDialogPresentationConfig(const SemanticActionSinkConfig& cfg) noexcept {
  ServerDialogPresentationConfig out;
  out.validateOnly = cfg.serverBoundClientMode && cfg.serverDialogPresentationValidateOnly;
  out.requireSpeakerEntityKey = true;
  out.requireTextOrAudio = true;
  out.requireLineIdentity = false;
  out.minDurationMs = 1;
  out.maxDurationMs = 30000;
  return out;
}


struct QueuedAction final {
  std::string               jsonLine;
  std::vector<std::uint8_t> serverPacket;
  bool                      bootstrapRequest = false;
};

class QueuedSemanticActionSink final : public SemanticActionSink {
  public:
    explicit QueuedSemanticActionSink(SemanticActionSinkConfig cfg)
      : serverBoundUdp(cfg.serverBoundClientMode),
        queueDialogPresentationMainThreadProbe(cfg.serverBoundClientMode && cfg.serverDialogPresentationMainThreadProbe),
        sendDialogObservationReceipts(cfg.serverBoundClientMode && cfg.serverDialogObservationReceipt),
        configuredSessionKey(cfg.sessionKey.empty() ? std::string("local-dev") : cfg.sessionKey),
        dialogPresentationConfig(makeServerDialogPresentationConfig(cfg)),
        queue(std::max<std::size_t>(cfg.queueCapacity, 1),
              cfg.strictOverflow
                  ? ClientSandbox::ExchangeQueueOverflowPolicy::RejectNewest
                  : ClientSandbox::ExchangeQueueOverflowPolicy::DropOldest) {
      worker = std::thread([this, cfg = std::move(cfg)]() mutable {
        run(std::move(cfg.jsonlPath), std::move(cfg.udpEndpoint));
      });
    }

    ~QueuedSemanticActionSink() override {
      queue.close();
      if(worker.joinable())
        worker.join();
    }

    SemanticSubmitResult submit(const SemanticActionEnvelope& envelope) noexcept override {
      if(!isValidEnvelope(envelope))
        return {SemanticSubmitStatus::InvalidEnvelope, dropped.load(std::memory_order_relaxed)};

      QueuedAction action;
      try {
        action.jsonLine = toJsonLine(envelope);
        if(serverBoundUdp) {
          auto encoded = encodeClientGameplayPacket(envelope, configuredSessionKey);
          if(!encoded)
            return {SemanticSubmitStatus::SinkError, dropped.load(std::memory_order_relaxed)};
          action.serverPacket = std::move(encoded->bytes);
          action.bootstrapRequest = encoded->bootstrapRequest;
        }
      }
      catch(...) {
        return {SemanticSubmitStatus::SinkError, dropped.load(std::memory_order_relaxed)};
      }

      if(serverBoundUdp && action.serverPacket.empty())
        return {SemanticSubmitStatus::SinkError, dropped.load(std::memory_order_relaxed)};

      const auto queueStatus = queue.push(std::move(action));
      switch(queueStatus) {
        case ClientSandbox::ExchangeQueuePushStatus::Accepted:
          break;
        case ClientSandbox::ExchangeQueuePushStatus::AcceptedAfterDroppingOldest:
          dropped.fetch_add(1, std::memory_order_relaxed);
          break;
        case ClientSandbox::ExchangeQueuePushStatus::RejectedFull: {
          const auto nowDropped = dropped.fetch_add(1, std::memory_order_relaxed) + 1;
          return {SemanticSubmitStatus::QueueFull, nowDropped};
        }
        case ClientSandbox::ExchangeQueuePushStatus::RejectedClosed:
          return {SemanticSubmitStatus::SinkError, dropped.load(std::memory_order_relaxed)};
      }
      return {SemanticSubmitStatus::Accepted, dropped.load(std::memory_order_relaxed)};
    }

    void flush() noexcept override {
      try {
        queue.waitUntilEmpty();
      } catch(...) {
      }
    }

  private:
    void run(std::string jsonlPath, std::string udpEndpoint) noexcept {
      std::ofstream out;
      if(!jsonlPath.empty()) {
        out.open(jsonlPath, std::ios::out | std::ios::app | std::ios::binary);
        if(!out.is_open())
          Tempest::Log::e("MMO semantic action JSONL sink: unable to open ", jsonlPath);
      }

      asio::io_context io;
      asio::ip::udp::socket udpSocket(io);
      std::optional<UdpTarget> udp;
      if(!udpEndpoint.empty()) {
        udp = resolveUdpTarget(io, udpEndpoint);
        if(!udp) {
          Tempest::Log::e("MMO ASIO UDP sink: invalid endpoint ", udpEndpoint, " expected host:port");
        } else {
          asio::error_code ec;
          udpSocket.open(asio::ip::udp::v4(), ec);
          if(ec) {
            Tempest::Log::e("MMO ASIO UDP sink: socket open failed: ", ec.message());
            udp.reset();
          } else {
            asio::socket_base::receive_buffer_size receiveBuffer(4 * 1024 * 1024);
            udpSocket.set_option(receiveBuffer, ec);
            if(ec)
              Tempest::Log::e("MMO ASIO UDP sink: receive buffer option failed: ", ec.message());

            asio::socket_base::send_buffer_size sendBuffer(1024 * 1024);
            udpSocket.set_option(sendBuffer, ec);
            if(ec)
              Tempest::Log::e("MMO ASIO UDP sink: send buffer option failed: ", ec.message());

            udpSocket.non_blocking(true, ec);
            if(ec) {
              Tempest::Log::e("MMO ASIO UDP sink: non-blocking mode failed: ", ec.message());
              udp.reset();
            }
          }
        }
      }

      for(;;) {
        auto queued = udp && udpSocket.is_open()
            ? queue.waitPopFor(std::chrono::milliseconds(5))
            : queue.waitPop();
        if(!queued) {
          if(queue.closed())
            break;
          if(udp && udpSocket.is_open()) {
            drainClientGameplayObservationReceipts(udpSocket, udp->endpoint);
            drainServerPackets(udpSocket);
          }
          continue;
        }
        QueuedAction action = std::move(*queued);

        if(!action.jsonLine.empty() && out.is_open()) {
          out.write(action.jsonLine.data(), static_cast<std::streamsize>(action.jsonLine.size()));
          out.put('\n');
        }

        if(udp && udpSocket.is_open()) {
          asio::error_code ec;
          if(serverBoundUdp) {
            drainClientGameplayObservationReceipts(udpSocket, udp->endpoint);
            drainServerPackets(udpSocket);
            if(action.bootstrapRequest)
              beginBootstrapSnapshotReceive();
            udpSocket.send_to(asio::buffer(action.serverPacket), udp->endpoint, 0, ec);
            drainClientGameplayObservationReceipts(udpSocket, udp->endpoint);
            drainServerPackets(udpSocket);
            if(action.bootstrapRequest) {
              for(unsigned i = 0; i != 200; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                drainServerPackets(udpSocket);
                if(snapshotCompleteAfterBootstrap)
                  break;
              }
              snapshotCompleteAfterBootstrap = false;
            }
          } else if(!action.jsonLine.empty()) {
            udpSocket.send_to(asio::buffer(action.jsonLine), udp->endpoint, 0, ec);
          }
          if(ec)
            (void)dropped.fetch_add(1, std::memory_order_relaxed);
        }
      }

      if(udp && udpSocket.is_open()) {
        for(unsigned i = 0; i != 80; ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
          drainClientGameplayObservationReceipts(udpSocket, udp->endpoint);
          drainServerPackets(udpSocket);
        }
        logIncompleteSnapshot();
        maybeLogServerAckSummary(true);
      }

      if(out.is_open())
        out.flush();
      if(udpSocket.is_open()) {
        asio::error_code ec;
        udpSocket.close(ec);
      }
    }

    struct ServerPacketStats final {
      std::uint64_t acceptedAcks = 0;
      std::uint64_t rejectedAcks = 0;
      std::uint64_t bootstrapAcks = 0;
      std::uint64_t movementAcks = 0;
      std::uint64_t genericAcks = 0;
      std::uint64_t diagnostics = 0;
      std::uint64_t contentManifestDiagnostics = 0;
      std::uint64_t snapshotChunks = 0;
      std::uint64_t snapshotBytes = 0;
      std::uint64_t liveDeltas = 0;
      std::uint64_t liveDeltaBytes = 0;
      std::uint64_t dialogIntents = 0;
      std::uint64_t dialogIntentBytes = 0;
      std::uint64_t gameplayAcksSent = 0;
      std::uint64_t gameplayAckSendFailures = 0;
      std::uint64_t duplicateDialogIntents = 0;
      std::uint64_t dialogReceiptCacheEvictions = 0;
      std::uint64_t malformedServerPackets = 0;
      std::uint64_t unsupportedServerPackets = 0;
      std::uint64_t dialogPresentationAcks = 0;
      std::uint64_t dialogPresentationNacks = 0;
      std::uint64_t dialogPresentationMainThreadQueued = 0;
      std::uint64_t dialogPresentationMainThreadDropped = 0;
      std::uint64_t gameplayObservationReceiptsSent = 0;
      std::uint64_t gameplayObservationReceiptSendFailures = 0;
      std::uint64_t gameplayObservationReceiptEncodeFailures = 0;
      std::uint64_t lastLiveDeltaSeq = 0;
      std::uint64_t lastDialogIntentSeq = 0;
      std::uint64_t lastAckSeq = 0;
      std::uint64_t lastSummaryAccepted = 0;
      std::uint64_t lastSummaryRejected = 0;
      std::chrono::steady_clock::time_point lastSummaryLog = std::chrono::steady_clock::now();
    };

    struct ContentManifestDiagnosticDetails final {
      bool structured = false;
      std::string reason;
      std::string phase;
      std::string clientHash;
      std::string serverRequiredHash;
      std::string contentRevisionKey;
    };

    static bool isContentManifestDiagnosticReason(std::string_view reason) noexcept {
      return reason == "client_manifest_missing" ||
             reason == "content_hash_mismatch" ||
             reason == "server_manifest_missing" ||
             reason == "active_session_not_found" ||
             reason == "session_uuid_missing" ||
             reason == "realm_not_found" ||
             reason == "content_manifest_rejected";
    }

    static ContentManifestDiagnosticDetails parseContentManifestDiagnostic(std::string_view reason,
                                                                          std::string_view message) {
      ContentManifestDiagnosticDetails out;
      out.reason = std::string(reason);
      if(message.empty() || message.front() != '{')
        return out;

      const auto error = ClientJson::optionalJsonString(message, "error");
      if(error != "client_content_manifest_rejected")
        return out;

      out.structured = true;
      out.reason = ClientJson::optionalJsonString(message, "reason", out.reason);
      out.phase = ClientJson::optionalJsonString(message, "phase");
      out.clientHash = ClientJson::optionalJsonString(message, "client_content_manifest_hash");
      out.serverRequiredHash = ClientJson::optionalJsonString(message, "server_required_content_hash");
      out.contentRevisionKey = ClientJson::optionalJsonString(message, "content_revision_key");
      return out;
    }

    void writeContentManifestRejectManifest(const Net::ServerDiagnosticPacket& d,
                                            const ContentManifestDiagnosticDetails& details) noexcept {
      try {
        std::filesystem::create_directories("runtime");
        std::ofstream out("runtime/mmo_server_bootstrap_reject.json.tmp",
                          std::ios::out | std::ios::binary | std::ios::trunc);
        if(!out.is_open())
          return;
        out << "{\n"
            << "  \"status\": \"rejected\",\n"
            << "  \"reject_kind\": \"content_manifest\",\n"
            << "  \"action\": " << jsonEscape(d.actionKind) << ",\n"
            << "  \"reason\": " << jsonEscape(details.reason) << ",\n"
            << "  \"phase\": " << jsonEscape(details.phase) << ",\n"
            << "  \"client_content_manifest_hash\": " << jsonEscape(details.clientHash) << ",\n"
            << "  \"server_required_content_hash\": " << jsonEscape(details.serverRequiredHash) << ",\n"
            << "  \"content_revision_key\": " << jsonEscape(details.contentRevisionKey) << ",\n"
            << "  \"structured\": " << (details.structured ? "true" : "false") << ",\n"
            << "  \"severity\": " << static_cast<unsigned>(d.severity) << ",\n"
            << "  \"packet_sequence\": " << d.packetSequence << ",\n"
            << "  \"local_sequence\": " << d.localSequence << ",\n"
            << "  \"message\": " << jsonEscape(d.message) << "\n"
            << "}\n";
        out.close();
        std::error_code ec;
        std::filesystem::rename("runtime/mmo_server_bootstrap_reject.json.tmp",
                                "runtime/mmo_server_bootstrap_reject.json",
                                ec);
      } catch(...) {
      }
    }

    void logServerDiagnostic(const Net::ServerDiagnosticPacket& d) noexcept {
      if(isContentManifestDiagnosticReason(d.reason)) {
        ++serverStats.contentManifestDiagnostics;
        ContentManifestDiagnosticDetails details;
        try {
          details = parseContentManifestDiagnostic(d.reason, d.message);
        } catch(...) {
          details.reason = d.reason;
        }
        writeContentManifestRejectManifest(d, details);
        Tempest::Log::e("MMO content manifest diagnostic severity=", static_cast<unsigned>(d.severity),
                        " action=", d.actionKind,
                        " reason=", details.reason,
                        " phase=", details.phase,
                        " client_hash=", details.clientHash.empty() ? "<empty>" : details.clientHash,
                        " server_required_hash=", details.serverRequiredHash.empty() ? "<empty>" : details.serverRequiredHash,
                        " content_revision=", details.contentRevisionKey.empty() ? "<empty>" : details.contentRevisionKey,
                        " structured=", details.structured ? 1 : 0,
                        " seq=", d.packetSequence);
        return;
      }

      if(d.severity >= 2) {
        Tempest::Log::e("MMO server diagnostic severity=", static_cast<unsigned>(d.severity),
                        " action=", d.actionKind,
                        " reason=", d.reason,
                        " seq=", d.packetSequence,
                        " message=", d.message);
      } else {
        Tempest::Log::i("MMO server diagnostic severity=", static_cast<unsigned>(d.severity),
                        " action=", d.actionKind,
                        " reason=", d.reason,
                        " seq=", d.packetSequence,
                        " message=", d.message);
      }
    }

    void drainClientGameplayObservationReceipts(asio::ip::udp::socket& socket,
                                                const asio::ip::udp::endpoint& remote) noexcept {
      if(!sendDialogObservationReceipts)
        return;

      auto receipts = clientGameplayObservationReceiptOutbox.drain();
      if(receipts.empty())
        return;

      for(const auto& receipt : receipts) {
        const auto encoded = Net::encodeClientGameplayObservationPacket(receipt);
        if(encoded.empty()) {
          ++serverStats.gameplayObservationReceiptEncodeFailures;
          Tempest::Log::e("MMO server dialog observation receipt encode failed action_id=", receipt.actionId,
                          " ack_key=", receipt.ackKey);
          continue;
        }

        asio::error_code ec;
        socket.send_to(asio::buffer(encoded), remote, 0, ec);
        if(ec) {
          ++serverStats.gameplayObservationReceiptSendFailures;
          Tempest::Log::e("MMO server dialog observation receipt send failed: ", ec.message());
          continue;
        }
        ++serverStats.gameplayObservationReceiptsSent;
      }
    }

    void drainServerPackets(asio::ip::udp::socket& socket) noexcept {
      std::array<char, Net::MaxDatagramBytes> buffer {};
      for(unsigned i = 0; i != 256; ++i) {
        asio::ip::udp::endpoint remote;
        asio::error_code ec;
        const auto n = socket.receive_from(asio::buffer(buffer), remote, 0, ec);
        if(ec) {
          if(ec == asio::error::would_block || ec == asio::error::try_again)
            return;
          Tempest::Log::e("MMO ASIO UDP sink: receive failed: ", ec.message());
          return;
        }

        auto decoded = Net::decodeServerPacket(std::string_view(buffer.data(), n));
        if(decoded.status == Net::ServerPacketDecodeStatus::Malformed) {
          ++serverStats.malformedServerPackets;
          if(serverStats.malformedServerPackets == 1 ||
             serverStats.malformedServerPackets % 100u == 0u) {
            Tempest::Log::e("MMO server packet malformed kind=",
                            static_cast<unsigned>(decoded.kind),
                            " error=", static_cast<unsigned>(decoded.error),
                            " bytes=", n,
                            " total=", serverStats.malformedServerPackets);
          }
          continue;
        }
        if(decoded.status == Net::ServerPacketDecodeStatus::UnsupportedPacketKind) {
          ++serverStats.unsupportedServerPackets;
          if(serverStats.unsupportedServerPackets == 1 ||
             serverStats.unsupportedServerPackets % 100u == 0u) {
            Tempest::Log::e("MMO server packet unsupported kind=",
                            static_cast<unsigned>(decoded.kind),
                            " bytes=", n,
                            " total=", serverStats.unsupportedServerPackets);
          }
          continue;
        }

        switch(decoded.kind) {
          case Net::PacketKind::ServerAck: {
            auto ack = std::move(*decoded.getIf<Net::ServerAckPacket>());
            recordServerAck(ack);
            if(!ack.accepted || ack.kind == Net::ServerAckKind::Bootstrap) {
              Tempest::Log::i("MMO server ACK kind=", static_cast<unsigned>(ack.kind),
                              " accepted=", ack.accepted ? 1 : 0,
                              " ready=", ack.ready ? 1 : 0,
                              " seq=", ack.packetSequence);
            }
            maybeLogServerAckSummary(false);
            break;
          }
          case Net::PacketKind::ServerSnapshotChunk: {
            auto chunk = std::move(*decoded.getIf<Net::ServerSnapshotChunkPacket>());
            ++serverStats.snapshotChunks;
            serverStats.snapshotBytes += static_cast<std::uint64_t>(chunk.payloadJsonFragment.size());
            acceptSnapshotChunk(std::move(chunk));
            break;
          }
          case Net::PacketKind::ServerDiagnostic: {
            auto diagnostic = std::move(*decoded.getIf<Net::ServerDiagnosticPacket>());
            ++serverStats.diagnostics;
            logServerDiagnostic(diagnostic);
            break;
          }
          case Net::PacketKind::ServerNpcDialogIntent: {
            auto intent = std::move(*decoded.getIf<Net::ServerNpcDialogIntentPacket>());
            ++serverStats.dialogIntents;
            serverStats.dialogIntentBytes +=
                static_cast<std::uint64_t>(intent.text.size() + intent.audioRef.size());
            serverStats.lastDialogIntentSeq = intent.packetSequence;
            acceptNpcDialogIntent(std::move(intent), socket, remote);
            maybeLogServerAckSummary(false);
            break;
          }
          case Net::PacketKind::ServerLiveDelta: {
            auto delta = std::move(*decoded.getIf<Net::ServerLiveDeltaPacket>());
            ++serverStats.liveDeltas;
            serverStats.liveDeltaBytes += static_cast<std::uint64_t>(delta.debugJson.size());
            serverStats.lastLiveDeltaSeq = delta.packetSequence;
            acceptLiveDelta(std::move(delta));
            maybeLogServerAckSummary(false);
            break;
          }
          default:
            ++serverStats.unsupportedServerPackets;
            break;
        }
      }
    }

    void recordServerAck(const Net::ServerAckPacket& ack) noexcept {
      if(ack.accepted)
        ++serverStats.acceptedAcks;
      else
        ++serverStats.rejectedAcks;

      serverStats.lastAckSeq = ack.packetSequence;
      switch(ack.kind) {
        case Net::ServerAckKind::Bootstrap:
          ++serverStats.bootstrapAcks;
          break;
        case Net::ServerAckKind::Movement:
          ++serverStats.movementAcks;
          break;
        case Net::ServerAckKind::GenericAction:
          ++serverStats.genericAcks;
          break;
      }
    }

    void maybeLogServerAckSummary(bool force) noexcept {
      const auto ackTotal = serverStats.acceptedAcks + serverStats.rejectedAcks;
      if(ackTotal == 0)
        return;

      const auto now = std::chrono::steady_clock::now();
      const bool enoughAccepted = serverStats.acceptedAcks >= serverStats.lastSummaryAccepted + 50;
      const bool rejectedChanged = serverStats.rejectedAcks != serverStats.lastSummaryRejected;
      const bool enoughTime = now - serverStats.lastSummaryLog >= std::chrono::seconds(5);
      if(!force && !enoughAccepted && !rejectedChanged && !enoughTime)
        return;

      Tempest::Log::i("MMO server ACK summary accepted=", serverStats.acceptedAcks,
                      " rejected=", serverStats.rejectedAcks,
                      " bootstrap=", serverStats.bootstrapAcks,
                      " movement=", serverStats.movementAcks,
                      " generic=", serverStats.genericAcks,
                      " diagnostics=", serverStats.diagnostics,
                      " content_manifest_diagnostics=", serverStats.contentManifestDiagnostics,
                      " snapshot_chunks=", serverStats.snapshotChunks,
                      " snapshot_bytes=", serverStats.snapshotBytes,
                      " live_deltas=", serverStats.liveDeltas,
                      " live_delta_bytes=", serverStats.liveDeltaBytes,
                      " dialog_intents=", serverStats.dialogIntents,
                      " dialog_intent_bytes=", serverStats.dialogIntentBytes,
                      " gameplay_acks_sent=", serverStats.gameplayAcksSent,
                      " gameplay_ack_send_failures=", serverStats.gameplayAckSendFailures,
                      " duplicate_dialog_intents=", serverStats.duplicateDialogIntents,
                      " dialog_receipt_cache_evictions=", serverStats.dialogReceiptCacheEvictions,
                      " malformed_server_packets=", serverStats.malformedServerPackets,
                      " unsupported_server_packets=", serverStats.unsupportedServerPackets,
                      " dialog_presentation_acks=", serverStats.dialogPresentationAcks,
                      " dialog_presentation_nacks=", serverStats.dialogPresentationNacks,
                      " dialog_main_thread_queued=", serverStats.dialogPresentationMainThreadQueued,
                      " dialog_main_thread_dropped=", serverStats.dialogPresentationMainThreadDropped,
                      " gameplay_observation_receipts_sent=", serverStats.gameplayObservationReceiptsSent,
                      " gameplay_observation_receipt_send_failures=", serverStats.gameplayObservationReceiptSendFailures,
                      " gameplay_observation_receipt_encode_failures=", serverStats.gameplayObservationReceiptEncodeFailures,
                      " last_live_delta_seq=", serverStats.lastLiveDeltaSeq,
                      " last_dialog_intent_seq=", serverStats.lastDialogIntentSeq,
                      " last_seq=", serverStats.lastAckSeq);
      serverStats.lastSummaryAccepted = serverStats.acceptedAcks;
      serverStats.lastSummaryRejected = serverStats.rejectedAcks;
      serverStats.lastSummaryLog = now;
    }

    void logIncompleteSnapshot() noexcept {
      if(!snapshot.active())
        return;
      Tempest::Log::e("MMO server bootstrap snapshot incomplete: id=", snapshot.snapshotId(),
                      " chunks=", static_cast<unsigned>(snapshot.receivedChunks()),
                      "/", static_cast<unsigned>(snapshot.chunkCount()),
                      " bytes=", snapshot.receivedBytes(),
                      "/", snapshot.totalBytes());
    }

    void beginBootstrapSnapshotReceive() noexcept {
      snapshot.reset();
      snapshotCompleteAfterBootstrap = false;
      std::error_code ec;
      std::filesystem::create_directories("runtime", ec);
      std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot.json", ec);
      std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot.json.tmp", ec);
      std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot_manifest.json", ec);
      std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot_manifest.json.tmp", ec);
      std::filesystem::remove("runtime/mmo_server_live_deltas.jsonl", ec);
    }

    void writeSnapshotManifest(std::size_t jsonBytes, std::uint16_t chunks, std::uint32_t snapshotId) noexcept {
      try {
        std::ofstream out("runtime/mmo_server_bootstrap_snapshot_manifest.json.tmp",
                          std::ios::out | std::ios::binary | std::ios::trunc);
        if(!out.is_open())
          return;
        out << "{\n"
            << "  \"status\": \"received\",\n"
            << "  \"path\": \"runtime/mmo_server_bootstrap_snapshot.json\",\n"
            << "  \"snapshot_id\": " << snapshotId << ",\n"
            << "  \"bytes\": " << jsonBytes << ",\n"
            << "  \"chunks\": " << static_cast<unsigned>(chunks) << ",\n"
            << "  \"ack_accepted\": " << serverStats.acceptedAcks << ",\n"
            << "  \"ack_rejected\": " << serverStats.rejectedAcks << ",\n"
            << "  \"snapshot_datagrams_seen\": " << serverStats.snapshotChunks << ",\n"
            << "  \"live_deltas_seen\": " << serverStats.liveDeltas << ",\n"
            << "  \"dialog_intents_seen\": " << serverStats.dialogIntents << ",\n"
            << "  \"gameplay_acks_sent\": " << serverStats.gameplayAcksSent << ",\n"
            << "  \"duplicate_dialog_intents_seen\": " << serverStats.duplicateDialogIntents << ",\n"
            << "  \"dialog_presentation_acks\": " << serverStats.dialogPresentationAcks << ",\n"
            << "  \"dialog_presentation_nacks\": " << serverStats.dialogPresentationNacks << ",\n"
            << "  \"dialog_main_thread_queued\": " << serverStats.dialogPresentationMainThreadQueued << ",\n"
            << "  \"dialog_main_thread_dropped\": " << serverStats.dialogPresentationMainThreadDropped << "\n"
            << "}\n";
        out.close();
        std::error_code ec;
        std::filesystem::rename("runtime/mmo_server_bootstrap_snapshot_manifest.json.tmp",
                                "runtime/mmo_server_bootstrap_snapshot_manifest.json",
                                ec);
      } catch(...) {
      }
    }

    void acceptSnapshotChunk(Net::ServerSnapshotChunkPacket chunk) noexcept {
      const auto beforeChunks = snapshot.receivedChunks();
      auto result = snapshot.accept(std::move(chunk));
      if(result.status == Net::BootstrapChunkStatus::Duplicate)
        return;
      if(!result.accepted()) {
        Tempest::Log::e("MMO server bootstrap snapshot chunk rejected status=",
                        static_cast<unsigned>(result.status));
        return;
      }

      if(result.status == Net::BootstrapChunkStatus::Accepted) {
        if(beforeChunks == 0) {
          Tempest::Log::i("MMO server bootstrap snapshot receiving: id=", snapshot.snapshotId(),
                          " bytes=", snapshot.totalBytes(),
                          " chunks=", static_cast<unsigned>(snapshot.chunkCount()));
        }
        if(snapshot.receivedChunks() == 1 ||
           snapshot.receivedChunks() % 16u == 0) {
          Tempest::Log::i("MMO server bootstrap snapshot progress: id=", snapshot.snapshotId(),
                          " chunks=", static_cast<unsigned>(snapshot.receivedChunks()),
                          "/", static_cast<unsigned>(snapshot.chunkCount()),
                          " bytes=", snapshot.receivedBytes(),
                          "/", snapshot.totalBytes());
        }
        return;
      }

      if(!result.completed.has_value())
        return;

      auto completed = std::move(*result.completed);
      try {
        std::filesystem::create_directories("runtime");
        std::ofstream out("runtime/mmo_server_bootstrap_snapshot.json.tmp", std::ios::out | std::ios::binary | std::ios::trunc);
        if(!out.is_open()) {
          Tempest::Log::e("MMO server bootstrap snapshot: unable to open runtime/mmo_server_bootstrap_snapshot.json.tmp");
          return;
        }
        out.write(completed.payload.data(), static_cast<std::streamsize>(completed.payload.size()));
        out.put('\n');
        out.close();
        std::error_code ec;
        std::filesystem::rename("runtime/mmo_server_bootstrap_snapshot.json.tmp",
                                "runtime/mmo_server_bootstrap_snapshot.json",
                                ec);
        if(ec) {
          Tempest::Log::e("MMO server bootstrap snapshot rename failed: ", ec.message());
          return;
        }
        Tempest::Log::i("MMO server bootstrap snapshot received: bytes=", completed.payload.size(),
                        " chunks=", static_cast<unsigned>(completed.chunkCount),
                        " path=runtime/mmo_server_bootstrap_snapshot.json");
        writeSnapshotManifest(completed.payload.size(), completed.chunkCount, completed.snapshotId);
        snapshotCompleteAfterBootstrap = true;
      } catch(const std::exception& exc) {
        Tempest::Log::e("MMO server bootstrap snapshot write failed: ", exc.what());
      }
    }

    struct DialogIntentReceipt final {
      Net::ClientGameplayAckStatus status = Net::ClientGameplayAckStatus::Ack;
      std::uint32_t flags = Net::ClientGameplayAckAccepted;
      ServerDialogPresentationStatus presentationStatus = ServerDialogPresentationStatus::Disabled;
      std::string reason;
      std::string message;
      bool validationEnabled = false;
      bool uiApplied = false;
      bool audioApplied = false;
    };

    [[nodiscard]] static DialogIntentReceipt receiptFromDecision(const ServerDialogPresentationDecision& decision) {
      return {decision.status,
              decision.flags,
              decision.presentationStatus,
              decision.reason,
              decision.message,
              decision.validationEnabled,
              decision.uiApplied,
              decision.audioApplied};
    }

    [[nodiscard]] static ServerDialogPresentationDecision decisionFromReceipt(const DialogIntentReceipt& receipt) {
      ServerDialogPresentationDecision out;
      out.status = receipt.status;
      out.flags = receipt.flags;
      out.presentationStatus = receipt.presentationStatus;
      out.reason = receipt.reason;
      out.message = receipt.message;
      out.validationEnabled = receipt.validationEnabled;
      out.uiApplied = receipt.uiApplied;
      out.audioApplied = receipt.audioApplied;
      return out;
    }

    void acceptNpcDialogIntent(Net::ServerNpcDialogIntentPacket intent,
                               asio::ip::udp::socket& socket,
                               const asio::ip::udp::endpoint& remote) noexcept {
      try {
        ServerDialogPresentationDecision decision;
        bool duplicateDelivery = false;

        if(const auto* found = seenDialogIntentReceipts.find(intent.ackKey)) {
          duplicateDelivery = true;
          decision = decisionFromReceipt(*found);
          decision.reason = decision.accepted()
              ? "client_duplicate_delivery_ack_replay"
              : "client_duplicate_delivery_nack_replay";
          decision.message = "ServerNpcDialogIntent duplicate delivery observed; terminal client receipt replayed idempotently.";
          ++serverStats.duplicateDialogIntents;
        } else {
          decision = evaluateServerDialogPresentation(intent, dialogPresentationConfig);
          const auto cacheStatus = seenDialogIntentReceipts.insert(intent.ackKey, receiptFromDecision(decision));
          if(cacheStatus == Net::RecentCacheInsertStatus::InsertedAfterEviction)
            ++serverStats.dialogReceiptCacheEvictions;
        }

        if(decision.accepted())
          ++serverStats.dialogPresentationAcks;
        else
          ++serverStats.dialogPresentationNacks;

        std::filesystem::create_directories("runtime");
        std::ofstream out("runtime/mmo_server_npc_dialog_intents.jsonl", std::ios::out | std::ios::app | std::ios::binary);
        if(out.is_open())
          out << ClientDiagnostics::npcDialogIntentJson(intent, duplicateDelivery, decision) << "\n";

        if(queueDialogPresentationMainThreadProbe) {
          ServerDialogPresentationEvent event;
          event.intent = intent;
          event.decision = decision;
          event.duplicateDelivery = duplicateDelivery;
          if(enqueueServerDialogPresentationEvent(std::move(event)))
            ++serverStats.dialogPresentationMainThreadQueued;
          else
            ++serverStats.dialogPresentationMainThreadDropped;
        }

        Net::ClientGameplayAckPacket ack;
        ack.packetSequence = ++clientGameplayAckSequence;
        ack.localSequence = intent.localSequence;
        ack.clientTick = intent.serverTick;
        ack.status = decision.status;
        ack.gameplayKind = Net::ServerGameplayKind::NpcDialogIntent;
        ack.flags = decision.flags;
        ack.sessionKey = configuredSessionKey;
        ack.sessionUuid = intent.sessionUuid;
        ack.characterKey = intent.targetCharacterKey;
        ack.actionId = intent.actionId;
        ack.ackKey = intent.ackKey;
        ack.reason = decision.reason;
        ack.message = decision.message;

        const auto encoded = Net::encodeClientGameplayAckPacket(ack);
        if(encoded.empty()) {
          ++serverStats.gameplayAckSendFailures;
          Tempest::Log::e("MMO server NPC dialog intent ACK encode failed action_id=", intent.actionId,
                          " ack_key=", intent.ackKey);
          return;
        }

        asio::error_code ec;
        socket.send_to(asio::buffer(encoded), remote, 0, ec);
        if(ec) {
          ++serverStats.gameplayAckSendFailures;
          Tempest::Log::e("MMO server NPC dialog intent ACK send failed: ", ec.message());
          return;
        }

        ++serverStats.gameplayAcksSent;
        Tempest::Log::i("MMO server NPC dialog intent received action_id=", intent.actionId,
                        " speaker=", intent.speakerEntityKey,
                        " line=", intent.lineId,
                        " ack_key=", intent.ackKey,
                        " seq=", intent.packetSequence,
                        " ack_seq=", ack.packetSequence,
                        " ack_status=", decision.accepted() ? "ack" : "nack",
                        " presentation=", serverDialogPresentationStatusName(decision.presentationStatus).data(),
                        " duplicate_delivery=", duplicateDelivery ? 1 : 0,
                        " ui_applied=", decision.uiApplied ? 1 : 0,
                        " audio_applied=", decision.audioApplied ? 1 : 0);
      } catch(const std::exception& exc) {
        ++serverStats.gameplayAckSendFailures;
        Tempest::Log::e("MMO server NPC dialog intent handling failed: ", exc.what());
      } catch(...) {
        ++serverStats.gameplayAckSendFailures;
        Tempest::Log::e("MMO server NPC dialog intent handling failed: unknown error");
      }
    }

    void acceptLiveDelta(Net::ServerLiveDeltaPacket delta) noexcept {
      try {
        enqueueServerLiveDelta(delta);
        std::filesystem::create_directories("runtime");
        std::ofstream out("runtime/mmo_server_live_deltas.jsonl", std::ios::out | std::ios::app | std::ios::binary);
        if(!out.is_open()) {
          Tempest::Log::e("MMO server live delta: unable to open runtime/mmo_server_live_deltas.jsonl");
          return;
        }

        const auto envelope = ClientDiagnostics::liveDeltaJson(delta);
        out << envelope << "\n";

        if(serverStats.liveDeltas == 1 || serverStats.liveDeltas % 25u == 0u) {
          Tempest::Log::i("MMO server live delta kind=", static_cast<unsigned>(delta.kind),
                          " action=", delta.actionKind,
                          " seq=", delta.packetSequence,
                          " total=", serverStats.liveDeltas,
                          " flags=", delta.flags);
        }
      } catch(const std::exception& exc) {
        Tempest::Log::e("MMO server live delta write failed: ", exc.what());
      } catch(...) {
        Tempest::Log::e("MMO server live delta write failed: unknown error");
      }
    }

    bool                        serverBoundUdp = false;
    bool                        queueDialogPresentationMainThreadProbe = false;
    bool                        sendDialogObservationReceipts = false;
    std::string                 configuredSessionKey;
    ServerDialogPresentationConfig dialogPresentationConfig;
    ClientSandbox::ClientExchangeQueue<QueuedAction> queue;
    std::thread                 worker;
    std::atomic_uint64_t        dropped {0};
    Net::BootstrapSnapshotAssembler snapshot;
    ServerPacketStats           serverStats;
    std::uint64_t               clientGameplayAckSequence = 0;
    Net::BoundedRecentCache<std::string, DialogIntentReceipt> seenDialogIntentReceipts {
      MaxRememberedDialogIntentReceipts};
    bool                        snapshotCompleteAfterBootstrap = false;
};

NoopSemanticActionSink noopSink;
std::unique_ptr<SemanticActionSink> ownedSink;
std::atomic<SemanticActionSink*> activeSink {&noopSink};
std::atomic_bool captureEnabled {false};
std::atomic_bool serverBoundClientMode {false};
std::atomic_uint64_t sequence {0};
std::string sessionKey = "local-dev";
std::mutex sinkMutex;

} // namespace

bool isSemanticActionCaptureEnabled() noexcept {
  return captureEnabled.load(std::memory_order_relaxed);
}

bool isServerBoundClientModeEnabled() noexcept {
  return serverBoundClientMode.load(std::memory_order_relaxed);
}

std::uint64_t nextSemanticActionSequence() noexcept {
  return sequence.fetch_add(1, std::memory_order_relaxed) + 1;
}

std::string_view semanticActionSessionKey() noexcept {
  return sessionKey;
}

SemanticSubmitResult submitSemanticAction(SemanticActionEnvelope&& envelope) noexcept {
  return submitSemanticAction(static_cast<const SemanticActionEnvelope&>(envelope));
}

SemanticSubmitResult submitSemanticAction(const SemanticActionEnvelope& envelope) noexcept {
  auto* sink = activeSink.load(std::memory_order_acquire);
  if(sink == nullptr)
    return {SemanticSubmitStatus::Disabled, 0};
  return sink->submit(envelope);
}

void setSemanticActionSink(std::unique_ptr<SemanticActionSink> sink) noexcept {
  std::lock_guard<std::mutex> lock(sinkMutex);
  if(activeSink.load(std::memory_order_acquire) != &noopSink)
    activeSink.load(std::memory_order_acquire)->flush();
  ownedSink = std::move(sink);
  if(ownedSink) {
    activeSink.store(ownedSink.get(), std::memory_order_release);
    captureEnabled.store(true, std::memory_order_relaxed);
    }
  else {
    activeSink.store(&noopSink, std::memory_order_release);
    captureEnabled.store(false, std::memory_order_relaxed);
    }
}

void configureSemanticActionSink(const SemanticActionSinkConfig& cfg) {
  sessionKey = cfg.sessionKey.empty() ? std::string("local-dev") : cfg.sessionKey;
  serverBoundClientMode.store(cfg.serverBoundClientMode, std::memory_order_relaxed);
  if(cfg.jsonlPath.empty() && cfg.udpEndpoint.empty()) {
    setSemanticActionSink(nullptr);
    return;
    }
  if(!cfg.jsonlPath.empty())
    Tempest::Log::i("MMO semantic action JSONL capture enabled: ", cfg.jsonlPath);
  if(!cfg.udpEndpoint.empty())
    Tempest::Log::i("MMO semantic action ASIO UDP transport enabled: ", cfg.udpEndpoint);
  if(cfg.serverBoundClientMode)
    Tempest::Log::i("MMO semantic action sink is in server-bound binary UDP mode");
  if(cfg.serverDialogPresentationValidateOnly)
    Tempest::Log::i("MMO server dialog presentation validation enabled: validate-only, no UI/audio side effects");
  if(cfg.serverDialogPresentationMainThreadProbe)
    Tempest::Log::i("MMO server dialog main-thread presentation probe enabled: no UI/audio side effects");
  if(cfg.serverDialogObservationReceipt)
    Tempest::Log::i("MMO server dialog main-thread observation receipts enabled: no UI/audio side effects");
  setSemanticActionSink(std::make_unique<QueuedSemanticActionSink>(cfg));
}

void configureSemanticActionSink(const CommandLine& cmd) {
  SemanticActionSinkConfig cfg;
  cfg.jsonlPath = std::string(cmd.mmoActionJsonl());
  cfg.udpEndpoint = std::string(cmd.mmoActionUdpEndpoint());
  cfg.sessionKey = std::string(cmd.mmoActionSessionKey());
  cfg.queueCapacity = cmd.mmoActionQueueCapacity();
  cfg.strictOverflow = cmd.mmoActionStrictOverflow();
  cfg.serverBoundClientMode = cmd.mmoClientUsesServer();
  cfg.serverDialogPresentationValidateOnly = cmd.mmoClientDialogPresentationValidateOnly();
  cfg.serverDialogPresentationMainThreadProbe = cmd.mmoClientDialogPresentationMainThreadProbe();
  cfg.serverDialogObservationReceipt = cmd.mmoClientDialogObservationReceipt();
  configureSemanticActionSink(cfg);
}

void shutdownSemanticActionSink() noexcept {
  setSemanticActionSink(nullptr);
}

std::vector<Net::ServerLiveDeltaPacket> drainServerLiveDeltas() noexcept {
  return serverLiveDeltaInbox.drain();
}

std::vector<ServerDialogPresentationEvent> drainServerDialogPresentationEvents() noexcept {
  return serverDialogPresentationInbox.drain();
}

bool enqueueClientGameplayObservationReceipt(Net::ClientGameplayObservationPacket packet) noexcept {
  return enqueueClientGameplayObservationReceiptInternal(std::move(packet));
}

} // namespace Mmo


