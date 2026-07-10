#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../../../shared/game/mmo/mmosemanticevents.h"
#include "../../../shared/net/mmo/mmonetprotocol.h"
#include "mmoserverdialogpresentation.h"

class CommandLine;

namespace Mmo {

enum class SemanticSubmitStatus : std::uint8_t {
  Disabled,
  Accepted,
  InvalidEnvelope,
  QueueFull,
  SinkError,
};

struct SemanticSubmitResult final {
  SemanticSubmitStatus status = SemanticSubmitStatus::Disabled;
  std::uint64_t        droppedCount = 0;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == SemanticSubmitStatus::Accepted || status == SemanticSubmitStatus::Disabled;
  }
};

class SemanticActionSink {
  public:
    virtual ~SemanticActionSink() = default;
    virtual SemanticSubmitResult submit(const SemanticActionEnvelope& envelope) noexcept = 0;
    virtual void flush() noexcept {}
};

class NoopSemanticActionSink final : public SemanticActionSink {
  public:
    SemanticSubmitResult submit(const SemanticActionEnvelope&) noexcept override {
      return {SemanticSubmitStatus::Disabled, 0};
    }
};

struct SemanticActionSinkConfig final {
  std::string  jsonlPath;
  std::string  udpEndpoint;
  std::string  sessionKey = "local-dev";
  std::size_t  queueCapacity = 4096;
  bool         strictOverflow = false;
  bool         serverBoundClientMode = false;
  bool         serverDialogPresentationValidateOnly = false;
  bool         serverDialogPresentationMainThreadProbe = false;
  bool         serverDialogObservationReceipt = false;
};

[[nodiscard]] bool isSemanticActionCaptureEnabled() noexcept;
[[nodiscard]] bool isServerBoundClientModeEnabled() noexcept;
[[nodiscard]] std::uint64_t nextSemanticActionSequence() noexcept;
[[nodiscard]] std::string_view semanticActionSessionKey() noexcept;

SemanticSubmitResult submitSemanticAction(SemanticActionEnvelope&& envelope) noexcept;
SemanticSubmitResult submitSemanticAction(const SemanticActionEnvelope& envelope) noexcept;

void setSemanticActionSink(std::unique_ptr<SemanticActionSink> sink) noexcept;
void configureSemanticActionSink(const SemanticActionSinkConfig& cfg);
void configureSemanticActionSink(const CommandLine& cmd);
void shutdownSemanticActionSink() noexcept;

std::vector<Net::ServerLiveDeltaPacket> drainServerLiveDeltas() noexcept;
std::vector<ServerDialogPresentationEvent> drainServerDialogPresentationEvents() noexcept;
bool enqueueClientGameplayObservationReceipt(Net::ClientGameplayObservationPacket packet) noexcept;

} // namespace Mmo
