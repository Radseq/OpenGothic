#include "mmosemanticactionsink.h"

#include <Tempest/Log>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
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
#include "mmonetprotocol.h"

namespace Mmo {
namespace {

struct UdpTarget final {
  asio::ip::udp::endpoint endpoint;
};

std::optional<std::pair<std::string, std::string>> splitHostPort(std::string_view endpoint) {
  const auto colon = endpoint.rfind(':');
  if(colon == std::string_view::npos || colon == 0 || colon + 1 >= endpoint.size())
    return std::nullopt;

  std::string host(endpoint.substr(0, colon));
  std::string port(endpoint.substr(colon + 1));
  if(host == "localhost")
    host = "127.0.0.1";
  return std::make_pair(std::move(host), std::move(port));
}

std::optional<UdpTarget> resolveUdpTarget(asio::io_context& io, std::string_view endpoint) {
  const auto parts = splitHostPort(endpoint);
  if(!parts)
    return std::nullopt;

  asio::error_code ec;
  asio::ip::udp::resolver resolver(io);
  auto results = resolver.resolve(asio::ip::udp::v4(), parts->first, parts->second, ec);
  if(ec || results.empty())
    return std::nullopt;
  return UdpTarget {results.begin()->endpoint()};
}

struct QueuedAction final {
  std::string               jsonLine;
  std::vector<std::uint8_t> serverPacket;
  bool                      bootstrapRequest = false;
};

class QueuedSemanticActionSink final : public SemanticActionSink {
  public:
    explicit QueuedSemanticActionSink(SemanticActionSinkConfig cfg)
      : strictOverflow(cfg.strictOverflow),
        serverBoundUdp(cfg.serverBoundClientMode),
        configuredSessionKey(cfg.sessionKey.empty() ? std::string("local-dev") : cfg.sessionKey),
        queue(std::max<std::size_t>(cfg.queueCapacity, 1)) {
      worker = std::thread([this, cfg = std::move(cfg)]() mutable {
        run(std::move(cfg.jsonlPath), std::move(cfg.udpEndpoint));
      });
    }

    ~QueuedSemanticActionSink() override {
      {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
      }
      cv.notify_one();
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
          action.serverPacket = Net::encodeClientActionPacket(envelope, configuredSessionKey);
          action.bootstrapRequest = envelope.kind == SemanticActionKind::ClientBootstrapRequest;
        }
      }
      catch(...) {
        return {SemanticSubmitStatus::SinkError, dropped.load(std::memory_order_relaxed)};
      }

      if(serverBoundUdp && action.serverPacket.empty())
        return {SemanticSubmitStatus::SinkError, dropped.load(std::memory_order_relaxed)};

      {
        std::lock_guard<std::mutex> lock(mutex);
        if(count == queue.size()) {
          const auto nowDropped = dropped.fetch_add(1, std::memory_order_relaxed) + 1;
          if(strictOverflow)
            return {SemanticSubmitStatus::QueueFull, nowDropped};
          queue[tail] = {};
          tail = (tail + 1u) % queue.size();
          --count;
          }
        queue[head] = std::move(action);
        head = (head + 1u) % queue.size();
        ++count;
      }
      cv.notify_one();
      return {SemanticSubmitStatus::Accepted, dropped.load(std::memory_order_relaxed)};
    }

    void flush() noexcept override {
      for(;;) {
        std::unique_lock<std::mutex> lock(mutex);
        if(count == 0)
          break;
        lock.unlock();
        std::this_thread::yield();
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
        QueuedAction action;
        {
          std::unique_lock<std::mutex> lock(mutex);
          if(count == 0 && !stopping && udp && udpSocket.is_open())
            cv.wait_for(lock, std::chrono::milliseconds(5), [this] { return stopping || count != 0; });
          else
            cv.wait(lock, [this] { return stopping || count != 0; });

          if(count == 0) {
            if(stopping)
              break;
            lock.unlock();
            if(udp && udpSocket.is_open())
              drainServerPackets(udpSocket);
            continue;
          }

          action = std::move(queue[tail]);
          queue[tail] = {};
          tail = (tail + 1u) % queue.size();
          --count;
        }

        if(!action.jsonLine.empty() && out.is_open()) {
          out.write(action.jsonLine.data(), static_cast<std::streamsize>(action.jsonLine.size()));
          out.put('\n');
        }

        if(udp && udpSocket.is_open()) {
          asio::error_code ec;
          if(serverBoundUdp) {
            drainServerPackets(udpSocket);
            if(action.bootstrapRequest)
              beginBootstrapSnapshotReceive();
            udpSocket.send_to(asio::buffer(action.serverPacket), udp->endpoint, 0, ec);
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

    struct SnapshotAssembly final {
      std::uint32_t id = 0;
      std::uint16_t chunkCount = 0;
      std::uint32_t totalBytes = 0;
      std::uint16_t receivedChunks = 0;
      std::size_t receivedBytes = 0;
      std::vector<std::string> chunks;
    };

    struct ServerPacketStats final {
      std::uint64_t acceptedAcks = 0;
      std::uint64_t rejectedAcks = 0;
      std::uint64_t bootstrapAcks = 0;
      std::uint64_t movementAcks = 0;
      std::uint64_t genericAcks = 0;
      std::uint64_t diagnostics = 0;
      std::uint64_t snapshotChunks = 0;
      std::uint64_t snapshotBytes = 0;
      std::uint64_t lastAckSeq = 0;
      std::uint64_t lastSummaryAccepted = 0;
      std::uint64_t lastSummaryRejected = 0;
      std::chrono::steady_clock::time_point lastSummaryLog = std::chrono::steady_clock::now();
    };

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

        const std::string_view packet(buffer.data(), n);
        if(auto ack = Net::decodeServerAckPacket(packet); ack.ok()) {
          recordServerAck(ack.serverAck);
          if(!ack.serverAck.accepted || ack.serverAck.kind == Net::ServerAckKind::Bootstrap) {
            Tempest::Log::i("MMO server ACK kind=", static_cast<unsigned>(ack.serverAck.kind),
                            " accepted=", ack.serverAck.accepted ? 1 : 0,
                            " ready=", ack.serverAck.ready ? 1 : 0,
                            " seq=", ack.serverAck.packetSequence);
          }
          maybeLogServerAckSummary(false);
          continue;
        }

        if(auto chunk = Net::decodeServerSnapshotChunkPacket(packet); chunk.ok()) {
          ++serverStats.snapshotChunks;
          serverStats.snapshotBytes += static_cast<std::uint64_t>(chunk.snapshotChunk.payloadJsonFragment.size());
          acceptSnapshotChunk(std::move(chunk.snapshotChunk));
          continue;
        }

        if(auto diag = Net::decodeServerDiagnosticPacket(packet); diag.ok()) {
          ++serverStats.diagnostics;
          const auto& d = diag.diagnostic;
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
          continue;
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
                      " snapshot_chunks=", serverStats.snapshotChunks,
                      " snapshot_bytes=", serverStats.snapshotBytes,
                      " last_seq=", serverStats.lastAckSeq);
      serverStats.lastSummaryAccepted = serverStats.acceptedAcks;
      serverStats.lastSummaryRejected = serverStats.rejectedAcks;
      serverStats.lastSummaryLog = now;
    }

    void logIncompleteSnapshot() noexcept {
      if(snapshot.receivedChunks == 0)
        return;
      Tempest::Log::e("MMO server bootstrap snapshot incomplete: id=", snapshot.id,
                      " chunks=", static_cast<unsigned>(snapshot.receivedChunks),
                      "/", static_cast<unsigned>(snapshot.chunkCount),
                      " bytes=", snapshot.receivedBytes,
                      "/", snapshot.totalBytes);
    }

    void beginBootstrapSnapshotReceive() noexcept {
      snapshot = {};
      snapshotCompleteAfterBootstrap = false;
      std::error_code ec;
      std::filesystem::create_directories("runtime", ec);
      std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot.json", ec);
      std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot.json.tmp", ec);
      std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot_manifest.json", ec);
      std::filesystem::remove("runtime/mmo_server_bootstrap_snapshot_manifest.json.tmp", ec);
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
            << "  \"snapshot_datagrams_seen\": " << serverStats.snapshotChunks << "\n"
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
      if(chunk.chunkCount == 0 || chunk.chunkIndex >= chunk.chunkCount)
        return;
      if(snapshot.id != chunk.snapshotId || snapshot.chunkCount != chunk.chunkCount || snapshot.totalBytes != chunk.totalBytes) {
        snapshot = {};
        snapshot.id = chunk.snapshotId;
        snapshot.chunkCount = chunk.chunkCount;
        snapshot.totalBytes = chunk.totalBytes;
        snapshot.chunks.resize(chunk.chunkCount);
        Tempest::Log::i("MMO server bootstrap snapshot receiving: id=", snapshot.id,
                        " bytes=", snapshot.totalBytes,
                        " chunks=", static_cast<unsigned>(snapshot.chunkCount));
      }

      auto& slot = snapshot.chunks[chunk.chunkIndex];
      if(!slot.empty())
        return;
      snapshot.receivedBytes += chunk.payloadJsonFragment.size();
      slot = std::move(chunk.payloadJsonFragment);
      ++snapshot.receivedChunks;

      if(snapshot.receivedChunks == 1 ||
         snapshot.receivedChunks == snapshot.chunkCount ||
         snapshot.receivedChunks % 16u == 0) {
        Tempest::Log::i("MMO server bootstrap snapshot progress: id=", snapshot.id,
                        " chunks=", static_cast<unsigned>(snapshot.receivedChunks),
                        "/", static_cast<unsigned>(snapshot.chunkCount),
                        " bytes=", snapshot.receivedBytes,
                        "/", snapshot.totalBytes);
      }

      if(snapshot.receivedChunks != snapshot.chunkCount)
        return;

      std::string json;
      json.reserve(snapshot.receivedBytes);
      for(const auto& part : snapshot.chunks)
        json += part;
      if(json.size() != snapshot.totalBytes) {
        Tempest::Log::e("MMO server bootstrap snapshot rejected: size mismatch bytes=", json.size(),
                        " expected=", snapshot.totalBytes);
        snapshot = {};
        return;
      }

      try {
        std::filesystem::create_directories("runtime");
        std::ofstream out("runtime/mmo_server_bootstrap_snapshot.json.tmp", std::ios::out | std::ios::binary | std::ios::trunc);
        if(!out.is_open()) {
          Tempest::Log::e("MMO server bootstrap snapshot: unable to open runtime/mmo_server_bootstrap_snapshot.json.tmp");
          snapshot = {};
          return;
        }
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        out.put('\n');
        out.close();
        std::error_code ec;
        std::filesystem::rename("runtime/mmo_server_bootstrap_snapshot.json.tmp",
                                "runtime/mmo_server_bootstrap_snapshot.json",
                                ec);
        if(ec) {
          Tempest::Log::e("MMO server bootstrap snapshot rename failed: ", ec.message());
          snapshot = {};
          return;
        }
        Tempest::Log::i("MMO server bootstrap snapshot received: bytes=", json.size(),
                        " chunks=", static_cast<unsigned>(snapshot.chunkCount),
                        " path=runtime/mmo_server_bootstrap_snapshot.json");
        writeSnapshotManifest(json.size(), snapshot.chunkCount, snapshot.id);
        snapshotCompleteAfterBootstrap = true;
      } catch(const std::exception& exc) {
        Tempest::Log::e("MMO server bootstrap snapshot write failed: ", exc.what());
      }
      snapshot = {};
    }

    bool                        strictOverflow = false;
    bool                        serverBoundUdp = false;
    std::string                 configuredSessionKey;
    std::vector<QueuedAction>   queue;
    std::size_t                 head = 0;
    std::size_t                 tail = 0;
    std::size_t                 count = 0;
    bool                        stopping = false;
    std::mutex                  mutex;
    std::condition_variable     cv;
    std::thread                 worker;
    std::atomic_uint64_t        dropped {0};
    SnapshotAssembly            snapshot;
    ServerPacketStats           serverStats;
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
  configureSemanticActionSink(cfg);
}

void shutdownSemanticActionSink() noexcept {
  setSemanticActionSink(nullptr);
}

} // namespace Mmo


