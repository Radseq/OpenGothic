#include "mmo_npc_perception_dialog_intent_diagnostic_encoder.h"

#include "../../game/game/mmonetprotocol.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace Mmo::AiRuntime {
namespace {

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(const unsigned char c : text) {
    switch(c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if(c < 0x20U) {
          static constexpr char Hex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(Hex[(c >> 4U) & 0xFU]);
          out.push_back(Hex[c & 0xFU]);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

void appendComma(std::string& out) {
  if(!out.empty() && out.back() != '{' && out.back() != '[') {
    out.push_back(',');
  }
}

void appendJsonField(std::string& out, std::string_view key, std::string_view value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += jsonEscape(value);
}

void appendJsonBoolField(std::string& out, std::string_view key, bool value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += value ? "true" : "false";
}

void appendJsonCountField(std::string& out, std::string_view key, std::uint64_t value) {
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void addIssue(NpcPerceptionDialogIntentDiagnosticEncoding& encoding, std::string issue) {
  encoding.issues.push_back(std::move(issue));
}

[[nodiscard]] std::string boundedMessage(std::string_view text, std::size_t maxBytes) {
  if(maxBytes == 0 || text.size() <= maxBytes) {
    return std::string(text);
  }
  return std::string(text.substr(0, maxBytes));
}

[[nodiscard]] bool decodeMatches(
    const Mmo::Net::ServerDiagnosticPacket& source,
    const Mmo::Net::ServerDiagnosticDecodeResult& decoded) noexcept {
  return decoded.ok() && decoded.diagnostic.packetSequence == source.packetSequence &&
         decoded.diagnostic.localSequence == source.localSequence && decoded.diagnostic.severity == source.severity &&
         decoded.diagnostic.actionKind == source.actionKind && decoded.diagnostic.reason == source.reason &&
         decoded.diagnostic.message == source.message;
}

} // namespace

NpcPerceptionDialogIntentDiagnosticEncoding encodeNpcPerceptionDialogIntentDiagnostic(
    const NpcPerceptionDialogIntentDiagnosticPacket& packet,
    const NpcPerceptionDialogIntentDiagnosticEncodeOptions& options) {
  NpcPerceptionDialogIntentDiagnosticEncoding out;
  out.packetSequence = options.packetSequence;
  out.localSequence = options.localSequence;
  out.maxDatagramBytes = Mmo::Net::MaxDatagramBytes;
  out.maxStringBytes = Mmo::Net::MaxStringBytes;
  out.maxPayloadBytes = Mmo::Net::MaxPayloadBytes;
  out.severity = packet.severity;
  out.actionKind = packet.actionKind;
  out.reason = packet.diagnosticReason;
  out.message = boundedMessage(packet.messageText, options.maxMessageBytes);
  out.messageBytes = out.message.size();

  out.sendExecuted = false;
  out.packetFanoutExecuted = false;
  out.dialogUiExecuted = false;
  out.audioExecuted = false;
  out.markAppliedExecuted = false;

  if(!packet.built) {
    addIssue(out, "diagnostic_packet_contract_not_built");
  }
  if(packet.packetKind != "server_diagnostic") {
    addIssue(out, "unsupported_packet_kind");
  }
  if(packet.liveDispatchExecuted || packet.packetFanoutExecuted || packet.dialogUiExecuted || packet.audioExecuted ||
     packet.markAppliedExecuted) {
    addIssue(out, "diagnostic_packet_contract_has_live_side_effect");
  }
  if(out.actionKind.empty()) {
    addIssue(out, "missing_mapped_action_kind");
  }
  if(out.reason.empty()) {
    addIssue(out, "missing_mapped_reason");
  }
  if(out.message.empty()) {
    addIssue(out, "missing_mapped_message");
  }

  out.actionKindWithinClientLimit = out.actionKind.size() <= Mmo::Net::MaxStringBytes;
  out.reasonWithinClientLimit = out.reason.size() <= Mmo::Net::MaxStringBytes;
  out.messageWithinClientLimit = out.message.size() <= Mmo::Net::MaxPayloadBytes;
  if(!out.actionKindWithinClientLimit) {
    addIssue(out, "mapped_action_kind_exceeds_client_limit");
  }
  if(!out.reasonWithinClientLimit) {
    addIssue(out, "mapped_reason_exceeds_client_limit");
  }
  if(!out.messageWithinClientLimit) {
    addIssue(out, "mapped_message_exceeds_client_limit");
  }

  if(out.issues.empty()) {
    Mmo::Net::ServerDiagnosticPacket diagnostic;
    diagnostic.packetSequence = out.packetSequence;
    diagnostic.localSequence = out.localSequence;
    diagnostic.severity = out.severity;
    diagnostic.actionKind = out.actionKind;
    diagnostic.reason = out.reason;
    diagnostic.message = out.message;

    out.encodedBytesBuffer = Mmo::Net::encodeServerDiagnosticPacket(diagnostic);
    out.encodedBytes = out.encodedBytesBuffer.size();
    out.fitsDatagram = out.encodedBytes != 0 && out.encodedBytes <= Mmo::Net::MaxDatagramBytes;
    if(!out.fitsDatagram) {
      addIssue(out, "encoded_server_diagnostic_packet_too_large_or_empty");
    } else {
      const std::string_view encodedView(
          reinterpret_cast<const char*>(out.encodedBytesBuffer.data()),
          out.encodedBytesBuffer.size());
      const auto decoded = Mmo::Net::decodeServerDiagnosticPacket(encodedView);
      out.decodedRoundTrip = decoded.ok();
      out.decodedFieldsMatch = decodeMatches(diagnostic, decoded);
      if(!out.decodedRoundTrip) {
        addIssue(out, "encoded_server_diagnostic_packet_decode_failed");
      } else if(!out.decodedFieldsMatch) {
        addIssue(out, "encoded_server_diagnostic_packet_round_trip_mismatch");
      }
    }
  }

  out.encoded = out.issues.empty() && out.fitsDatagram && out.decodedRoundTrip && out.decodedFieldsMatch;
  out.status = out.encoded ? "encoded_server_diagnostic_packet_contract_only_no_fanout" : "encoding_rejected";
  return out;
}

std::string dialogIntentDiagnosticEncodingJson(
    const NpcPerceptionDialogIntentDiagnosticEncoding& encoding) {
  std::string out;
  out.reserve(1536 + encoding.issues.size() * 48 + encoding.message.size());
  out.push_back('{');

  appendJsonBoolField(out, "encoded", encoding.encoded);
  appendComma(out);
  appendJsonField(out, "status", encoding.status);
  appendComma(out);
  appendJsonField(out, "packet_kind", encoding.packetKind);
  appendComma(out);
  appendJsonCountField(out, "packet_sequence", encoding.packetSequence);
  appendComma(out);
  appendJsonCountField(out, "local_sequence", encoding.localSequence);
  appendComma(out);
  appendJsonCountField(out, "severity", encoding.severity);
  appendComma(out);
  appendJsonField(out, "mapped_action_kind", encoding.actionKind);
  appendComma(out);
  appendJsonField(out, "mapped_reason", encoding.reason);
  appendComma(out);
  appendJsonField(out, "mapped_message", encoding.message);
  appendComma(out);
  appendJsonCountField(out, "mapped_message_bytes", encoding.messageBytes);
  appendComma(out);
  appendJsonCountField(out, "encoded_bytes", encoding.encodedBytes);
  appendComma(out);
  appendJsonCountField(out, "max_datagram_bytes", encoding.maxDatagramBytes);
  appendComma(out);
  appendJsonCountField(out, "max_string_bytes", encoding.maxStringBytes);
  appendComma(out);
  appendJsonCountField(out, "max_payload_bytes", encoding.maxPayloadBytes);
  appendComma(out);
  appendJsonBoolField(out, "fits_datagram", encoding.fitsDatagram);
  appendComma(out);
  appendJsonBoolField(out, "action_kind_within_client_limit", encoding.actionKindWithinClientLimit);
  appendComma(out);
  appendJsonBoolField(out, "reason_within_client_limit", encoding.reasonWithinClientLimit);
  appendComma(out);
  appendJsonBoolField(out, "message_within_client_limit", encoding.messageWithinClientLimit);
  appendComma(out);
  appendJsonBoolField(out, "decoded_round_trip", encoding.decodedRoundTrip);
  appendComma(out);
  appendJsonBoolField(out, "decoded_fields_match", encoding.decodedFieldsMatch);
  appendComma(out);
  appendJsonBoolField(out, "send_executed", encoding.sendExecuted);
  appendComma(out);
  appendJsonBoolField(out, "packet_fanout_executed", encoding.packetFanoutExecuted);
  appendComma(out);
  appendJsonBoolField(out, "dialog_ui_executed", encoding.dialogUiExecuted);
  appendComma(out);
  appendJsonBoolField(out, "audio_executed", encoding.audioExecuted);
  appendComma(out);
  appendJsonBoolField(out, "mark_applied_executed", encoding.markAppliedExecuted);
  appendComma(out);
  appendJsonCountField(out, "issue_count", encoding.issueCount());
  appendComma(out);
  out += jsonEscape("issues");
  out += ":[";
  bool first = true;
  for(const auto& issue : encoding.issues) {
    if(!first) {
      out.push_back(',');
    }
    first = false;
    out += jsonEscape(issue);
  }
  out += "]}";
  return out;
}

} // namespace Mmo::AiRuntime
