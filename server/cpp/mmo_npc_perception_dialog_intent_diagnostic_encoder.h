#pragma once

#include "mmo_npc_perception_dialog_intent_diagnostic_packet.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Mmo::AiRuntime {

struct NpcPerceptionDialogIntentDiagnosticEncodeOptions final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::size_t maxMessageBytes = 4096;
};

struct NpcPerceptionDialogIntentDiagnosticEncoding final {
  bool encoded = false;
  bool decodedRoundTrip = false;
  bool sendExecuted = false;
  bool packetFanoutExecuted = false;
  bool dialogUiExecuted = false;
  bool audioExecuted = false;
  bool markAppliedExecuted = false;

  std::string status = "not_encoded";
  std::string packetKind = "server_diagnostic";
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint16_t severity = 0;
  std::string actionKind;
  std::string reason;
  std::string message;
  std::size_t messageBytes = 0;
  std::size_t encodedBytes = 0;
  std::size_t maxDatagramBytes = 0;
  std::size_t maxStringBytes = 0;
  std::size_t maxPayloadBytes = 0;
  bool fitsDatagram = false;
  bool actionKindWithinClientLimit = false;
  bool reasonWithinClientLimit = false;
  bool messageWithinClientLimit = false;
  bool decodedFieldsMatch = false;
  std::vector<std::uint8_t> encodedBytesBuffer;
  std::vector<std::string> issues;

  [[nodiscard]] std::size_t issueCount() const noexcept { return issues.size(); }
};

[[nodiscard]] NpcPerceptionDialogIntentDiagnosticEncoding encodeNpcPerceptionDialogIntentDiagnostic(
    const NpcPerceptionDialogIntentDiagnosticPacket& packet,
    const NpcPerceptionDialogIntentDiagnosticEncodeOptions& options = {});

[[nodiscard]] std::string dialogIntentDiagnosticEncodingJson(
    const NpcPerceptionDialogIntentDiagnosticEncoding& encoding);

} // namespace Mmo::AiRuntime
