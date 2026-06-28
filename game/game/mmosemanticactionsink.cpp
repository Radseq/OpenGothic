#include "mmosemanticactionsink.h"

#include <Tempest/Log>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "commandline.h"

namespace Mmo {
namespace {

#if defined(__unix__) || defined(__APPLE__)
struct UdpEndpoint final {
  sockaddr_in addr {};
};

std::optional<std::uint16_t> parseUdpPort(std::string_view text) noexcept {
  if(text.empty())
    return std::nullopt;

  std::uint64_t value = 0;
  for(char ch : text) {
    if(ch < '0' || ch > '9')
      return std::nullopt;
    value = value * 10u + static_cast<std::uint64_t>(ch - '0');
    if(value > 65535u)
      return std::nullopt;
    }

  if(value == 0u)
    return std::nullopt;
  return static_cast<std::uint16_t>(value);
}

std::optional<UdpEndpoint> parseUdpEndpoint(std::string_view endpoint) noexcept {
  const auto colon = endpoint.rfind(':');
  if(colon == std::string_view::npos || colon == 0 || colon + 1 >= endpoint.size())
    return std::nullopt;

  const auto host = endpoint.substr(0, colon);
  const auto port = parseUdpPort(endpoint.substr(colon + 1));
  if(!port)
    return std::nullopt;

  std::string hostText(host);
  if(hostText == "localhost")
    hostText = "127.0.0.1";

  UdpEndpoint result;
  result.addr.sin_family = AF_INET;
  result.addr.sin_port = htons(*port);
  if(::inet_pton(AF_INET, hostText.c_str(), &result.addr.sin_addr) != 1)
    return std::nullopt;

  return result;
}
#endif

class QueuedSemanticActionSink final : public SemanticActionSink {
  public:
    explicit QueuedSemanticActionSink(SemanticActionSinkConfig cfg)
      : strictOverflow(cfg.strictOverflow), queue(std::max<std::size_t>(cfg.queueCapacity, 1)) {
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

      std::string line;
      try {
        line = toJsonLine(envelope);
      }
      catch(...) {
        return {SemanticSubmitStatus::SinkError, dropped.load(std::memory_order_relaxed)};
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        if(count == queue.size()) {
          const auto nowDropped = dropped.fetch_add(1, std::memory_order_relaxed) + 1;
          if(strictOverflow)
            return {SemanticSubmitStatus::QueueFull, nowDropped};
          // Non-strict dev mode: keep newest actions, drop oldest diagnostic line.
          queue[tail].clear();
          tail = (tail + 1u) % queue.size();
          --count;
          }
        queue[head] = std::move(line);
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

#if defined(__unix__) || defined(__APPLE__)
      int udpSocket = -1;
      std::optional<UdpEndpoint> udp;
      if(!udpEndpoint.empty()) {
        udp = parseUdpEndpoint(udpEndpoint);
        if(!udp) {
          Tempest::Log::e("MMO semantic action UDP sink: invalid endpoint ", udpEndpoint, " expected ipv4:port");
        } else {
          udpSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
          if(udpSocket < 0) {
            Tempest::Log::e("MMO semantic action UDP sink: socket failed errno=", errno);
            udp.reset();
          }
        }
      }
#else
      if(!udpEndpoint.empty())
        Tempest::Log::e("MMO semantic action UDP sink is unavailable on this platform: ", udpEndpoint);
#endif

      for(;;) {
        std::string line;
        {
          std::unique_lock<std::mutex> lock(mutex);
          cv.wait(lock, [this] { return stopping || count != 0; });
          if(count == 0 && stopping)
            break;
          line = std::move(queue[tail]);
          queue[tail].clear();
          tail = (tail + 1u) % queue.size();
          --count;
        }
        if(line.empty())
          continue;

        if(out.is_open()) {
          out.write(line.data(), static_cast<std::streamsize>(line.size()));
          out.put('\n');
        }

#if defined(__unix__) || defined(__APPLE__)
        if(udpSocket >= 0 && udp) {
          const auto rc = ::sendto(udpSocket,
                                   line.data(),
                                   line.size(),
                                   0,
                                   reinterpret_cast<const sockaddr*>(&udp->addr),
                                   static_cast<socklen_t>(sizeof(udp->addr)));
          if(rc < 0)
            (void)dropped.fetch_add(1, std::memory_order_relaxed);
        }
#endif
      }

      if(out.is_open())
        out.flush();

#if defined(__unix__) || defined(__APPLE__)
      if(udpSocket >= 0)
        (void)::close(udpSocket);
#endif
    }

    bool                     strictOverflow = false;
    std::vector<std::string> queue;
    std::size_t              head = 0;
    std::size_t              tail = 0;
    std::size_t              count = 0;
    bool                     stopping = false;
    std::mutex               mutex;
    std::condition_variable  cv;
    std::thread              worker;
    std::atomic_uint64_t     dropped {0};
};

NoopSemanticActionSink noopSink;
std::unique_ptr<SemanticActionSink> ownedSink;
std::atomic<SemanticActionSink*> activeSink {&noopSink};
std::atomic_bool captureEnabled {false};
std::atomic_uint64_t sequence {0};
std::string sessionKey = "local-dev";
std::mutex sinkMutex;

} // namespace

bool isSemanticActionCaptureEnabled() noexcept {
  return captureEnabled.load(std::memory_order_relaxed);
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
  if(cfg.jsonlPath.empty() && cfg.udpEndpoint.empty()) {
    setSemanticActionSink(nullptr);
    return;
    }
  if(!cfg.jsonlPath.empty())
    Tempest::Log::i("MMO semantic action JSONL capture enabled: ", cfg.jsonlPath);
  if(!cfg.udpEndpoint.empty())
    Tempest::Log::i("MMO semantic action UDP transport enabled: ", cfg.udpEndpoint);
  setSemanticActionSink(std::make_unique<QueuedSemanticActionSink>(cfg));
}

void configureSemanticActionSink(const CommandLine& cmd) {
  SemanticActionSinkConfig cfg;
  cfg.jsonlPath = std::string(cmd.mmoActionJsonl());
  cfg.udpEndpoint = std::string(cmd.mmoActionUdpEndpoint());
  cfg.sessionKey = std::string(cmd.mmoActionSessionKey());
  cfg.queueCapacity = cmd.mmoActionQueueCapacity();
  cfg.strictOverflow = cmd.mmoActionStrictOverflow();
  configureSemanticActionSink(cfg);
}

void shutdownSemanticActionSink() noexcept {
  setSemanticActionSink(nullptr);
}

} // namespace Mmo
