#include "mmosemanticevents.h"

#include <charconv>
#include <system_error>

namespace Mmo {

std::string makeIdempotencyKey(std::string_view sessionKey,
                               std::uint64_t localSequence,
                               SemanticActionKind kind,
                               std::string_view targetKey) {
  std::string out;
  out.reserve(sessionKey.size() + targetKey.size() + 64);
  out.append(sessionKey);
  out.push_back(':');
  out.append(actionKindName(kind));
  out.push_back(':');
  out.append(targetKey);
  out.push_back(':');

  char buf[32] = {};
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), localSequence);
  if(ec == std::errc{})
    out.append(buf, ptr);
  else
    out.append("0");

  return out;
}

bool isValidEnvelope(const SemanticActionEnvelope& envelope) noexcept {
  const auto* def = findSemanticAction(envelope.kind);
  if(def == nullptr)
    return false;
  if(def->requiresSession && envelope.idempotencyKey.empty())
    return false;
  if(envelope.payloadJson.empty())
    return false;
  if(envelope.payloadJson.front() != '{')
    return false;
  return true;
}

std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(char ch : text) {
    switch(ch) {
      case '\\': out.append("\\\\"); break;
      case '"':  out.append("\\\""); break;
      case '\n': out.append("\\n");  break;
      case '\r': out.append("\\r");  break;
      case '\t': out.append("\\t");  break;
      default:
        if(static_cast<unsigned char>(ch) < 0x20)
          out.push_back(' ');
        else
          out.push_back(ch);
        break;
      }
    }
  out.push_back('"');
  return out;
}

static void appendUInt(std::string& out, std::uint64_t v) {
  char buf[32] = {};
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  if(ec == std::errc{})
    out.append(buf, ptr);
  else
    out.push_back('0');
}

std::string toJsonLine(const SemanticActionEnvelope& envelope) {
  std::string out;
  out.reserve(envelope.payloadJson.size() + envelope.targetKey.size() + envelope.idempotencyKey.size() + 256);
  out.append("{\"version\":1");
  out.append(",\"action_kind\":");
  out.append(jsonEscape(actionKindName(envelope.kind)));
  out.append(",\"event_type\":");
  out.append(jsonEscape(eventTypeName(envelope.kind)));
  out.append(",\"event_class\":");
  out.append(jsonEscape(eventClassName(envelope.kind)));
  out.append(",\"procedure\":");
  out.append(jsonEscape(procedureName(envelope.kind)));
  out.append(",\"local_sequence\":");
  appendUInt(out, envelope.localSequence);
  out.append(",\"client_tick\":");
  appendUInt(out, envelope.clientTick);
  out.append(",\"target_key\":");
  out.append(jsonEscape(envelope.targetKey));
  out.append(",\"idempotency_key\":");
  out.append(jsonEscape(envelope.idempotencyKey));
  out.append(",\"payload\":");
  out.append(envelope.payloadJson.empty() ? "{}" : envelope.payloadJson);
  out.push_back('}');
  return out;
}

} // namespace Mmo


