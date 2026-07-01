#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mmosemanticevents.h"

namespace Mmo::Net {

enum class PacketKind : std::uint16_t {
  ClientAction         = 1,
  ServerAck            = 2,
  ServerSnapshotChunk  = 3,
  ServerDiagnostic     = 4,
};

enum class ServerAckKind : std::uint16_t {
  GenericAction = 1,
  Bootstrap     = 2,
  Movement      = 3,
};

enum class DecodeError : std::uint8_t {
  None,
  TooSmall,
  BadMagic,
  BadVersion,
  BadPacketKind,
  BadActionKind,
  Truncated,
  StringTooLong,
  InvalidPayload,
};

struct ClientActionPacket final {
  SemanticActionKind kind = SemanticActionKind::ClientBootstrapRequest;
  std::uint16_t      flags = 0;
  std::uint64_t      packetSequence = 0;
  std::uint64_t      clientTick = 0;
  std::uint64_t      localSequence = 0;
  std::string        sessionKey;
  std::string        targetKey;
  std::string        idempotencyKey;
  std::string        payloadJson;
};

struct ServerAckPacket final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  ServerAckKind kind = ServerAckKind::GenericAction;
  bool          accepted = false;
  bool          ready = false;
};

struct ServerSnapshotChunkPacket final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint32_t snapshotId = 0;
  std::uint16_t chunkIndex = 0;
  std::uint16_t chunkCount = 0;
  std::uint32_t totalBytes = 0;
  std::string   payloadJsonFragment;
};

struct ServerDiagnosticPacket final {
  std::uint64_t packetSequence = 0;
  std::uint64_t localSequence = 0;
  std::uint16_t severity = 0;
  std::string   actionKind;
  std::string   reason;
  std::string   message;
};

struct DecodeResult final {
  DecodeError        error = DecodeError::None;
  ClientActionPacket clientAction;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ServerAckDecodeResult final {
  DecodeError     error = DecodeError::None;
  ServerAckPacket serverAck;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ServerSnapshotChunkDecodeResult final {
  DecodeError               error = DecodeError::None;
  ServerSnapshotChunkPacket snapshotChunk;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

struct ServerDiagnosticDecodeResult final {
  DecodeError            error = DecodeError::None;
  ServerDiagnosticPacket diagnostic;

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == DecodeError::None;
  }
};

inline constexpr std::uint32_t PacketMagic = 0x4d4d474f; // "OGMM" little endian.
inline constexpr std::uint16_t PacketVersion = 1;
inline constexpr std::size_t MaxStringBytes = 8192;
inline constexpr std::size_t MaxPayloadBytes = 48 * 1024;
inline constexpr std::size_t MaxDatagramBytes = 60 * 1024;

inline void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xffu));
  out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xffu));
}

inline void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for(unsigned i = 0; i != 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8u)) & 0xffu));
}

inline void appendU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for(unsigned i = 0; i != 8; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8u)) & 0xffu));
}

inline bool readU16(std::string_view bytes, std::size_t& at, std::uint16_t& out) noexcept {
  if(bytes.size() - at < 2)
    return false;
  out = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at])) |
        static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at + 1]) << 8u);
  at += 2;
  return true;
}

inline bool readU32(std::string_view bytes, std::size_t& at, std::uint32_t& out) noexcept {
  if(bytes.size() - at < 4)
    return false;
  out = 0;
  for(unsigned i = 0; i != 4; ++i)
    out |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + i])) << (i * 8u);
  at += 4;
  return true;
}

inline bool readU64(std::string_view bytes, std::size_t& at, std::uint64_t& out) noexcept {
  if(bytes.size() - at < 8)
    return false;
  out = 0;
  for(unsigned i = 0; i != 8; ++i)
    out |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[at + i])) << (i * 8u);
  at += 8;
  return true;
}

inline bool appendString16(std::vector<std::uint8_t>& out, std::string_view text) {
  if(text.size() > std::numeric_limits<std::uint16_t>::max())
    return false;
  appendU16(out, static_cast<std::uint16_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
  return true;
}

inline bool appendString32(std::vector<std::uint8_t>& out, std::string_view text) {
  if(text.size() > std::numeric_limits<std::uint32_t>::max())
    return false;
  appendU32(out, static_cast<std::uint32_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
  return true;
}

inline bool readString16(std::string_view bytes, std::size_t& at, std::string& out) {
  std::uint16_t len = 0;
  if(!readU16(bytes, at, len))
    return false;
  if(len > MaxStringBytes || bytes.size() - at < len)
    return false;
  out.assign(bytes.data() + at, len);
  at += len;
  return true;
}

inline bool readString32(std::string_view bytes, std::size_t& at, std::string& out) {
  std::uint32_t len = 0;
  if(!readU32(bytes, at, len))
    return false;
  if(len > MaxPayloadBytes || bytes.size() - at < len)
    return false;
  out.assign(bytes.data() + at, len);
  at += len;
  return true;
}

inline std::vector<std::uint8_t> encodeClientActionPacket(const SemanticActionEnvelope& envelope,
                                                         std::string_view sessionKey) {
  std::vector<std::uint8_t> out;
  out.reserve(envelope.payloadJson.size() + envelope.targetKey.size() + envelope.idempotencyKey.size() + sessionKey.size() + 64);

  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ClientAction));
  appendU16(out, 0);
  appendU16(out, static_cast<std::uint16_t>(envelope.kind));
  appendU64(out, envelope.localSequence);
  appendU64(out, envelope.clientTick);
  appendU64(out, envelope.localSequence);

  if(!appendString16(out, sessionKey) ||
     !appendString16(out, envelope.targetKey) ||
     !appendString16(out, envelope.idempotencyKey) ||
     !appendString32(out, envelope.payloadJson)) {
    return {};
  }

  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline DecodeResult decodeClientActionPacket(std::string_view bytes) {
  DecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 8) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t actionKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ClientAction)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }

  if(!readU16(bytes, at, result.clientAction.flags) ||
     !readU16(bytes, at, actionKind) ||
     !readU64(bytes, at, result.clientAction.packetSequence) ||
     !readU64(bytes, at, result.clientAction.clientTick) ||
     !readU64(bytes, at, result.clientAction.localSequence)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  result.clientAction.kind = static_cast<SemanticActionKind>(actionKind);
  if(findSemanticAction(result.clientAction.kind) == nullptr) {
    result.error = DecodeError::BadActionKind;
    return result;
  }

  if(!readString16(bytes, at, result.clientAction.sessionKey) ||
     !readString16(bytes, at, result.clientAction.targetKey) ||
     !readString16(bytes, at, result.clientAction.idempotencyKey) ||
     !readString32(bytes, at, result.clientAction.payloadJson)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  if(result.clientAction.payloadJson.empty() || result.clientAction.payloadJson.front() != '{') {
    result.error = DecodeError::InvalidPayload;
    return result;
  }
  return result;
}

inline std::vector<std::uint8_t> encodeServerAckPacket(const ServerAckPacket& ack) {
  std::vector<std::uint8_t> out;
  out.reserve(4 + 2 + 2 + 2 + 2 + 8 + 8);
  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ServerAck));
  std::uint16_t flags = 0;
  if(ack.accepted)
    flags |= 0x0001u;
  if(ack.ready)
    flags |= 0x0002u;
  appendU16(out, flags);
  appendU16(out, static_cast<std::uint16_t>(ack.kind));
  appendU64(out, ack.packetSequence);
  appendU64(out, ack.localSequence);
  return out;
}

inline ServerAckDecodeResult decodeServerAckPacket(std::string_view bytes) {
  ServerAckDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t ackKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ServerAck)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, ackKind) ||
     !readU64(bytes, at, result.serverAck.packetSequence) ||
     !readU64(bytes, at, result.serverAck.localSequence)) {
    result.error = DecodeError::Truncated;
    return result;
  }

  result.serverAck.kind = static_cast<ServerAckKind>(ackKind);
  result.serverAck.accepted = (flags & 0x0001u) != 0;
  result.serverAck.ready = (flags & 0x0002u) != 0;
  return result;
}

inline std::vector<std::uint8_t> encodeServerSnapshotChunkPacket(const ServerSnapshotChunkPacket& chunk) {
  std::vector<std::uint8_t> out;
  out.reserve(chunk.payloadJsonFragment.size() + 40);
  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ServerSnapshotChunk));
  appendU16(out, 0);
  appendU16(out, 1);
  appendU64(out, chunk.packetSequence);
  appendU64(out, chunk.localSequence);
  appendU32(out, chunk.snapshotId);
  appendU16(out, chunk.chunkIndex);
  appendU16(out, chunk.chunkCount);
  appendU32(out, chunk.totalBytes);
  if(!appendString32(out, chunk.payloadJsonFragment))
    return {};
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ServerSnapshotChunkDecodeResult decodeServerSnapshotChunkPacket(std::string_view bytes) {
  ServerSnapshotChunkDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 4 + 2 + 2 + 4 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;
  std::uint16_t snapshotKind = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ServerSnapshotChunk)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, snapshotKind) ||
     !readU64(bytes, at, result.snapshotChunk.packetSequence) ||
     !readU64(bytes, at, result.snapshotChunk.localSequence) ||
     !readU32(bytes, at, result.snapshotChunk.snapshotId) ||
     !readU16(bytes, at, result.snapshotChunk.chunkIndex) ||
     !readU16(bytes, at, result.snapshotChunk.chunkCount) ||
     !readU32(bytes, at, result.snapshotChunk.totalBytes) ||
     !readString32(bytes, at, result.snapshotChunk.payloadJsonFragment)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  (void)snapshotKind;
  if(result.snapshotChunk.chunkCount == 0 ||
     result.snapshotChunk.chunkIndex >= result.snapshotChunk.chunkCount ||
     result.snapshotChunk.totalBytes > MaxPayloadBytes * 64u) {
    result.error = DecodeError::InvalidPayload;
    return result;
  }
  return result;
}

inline std::vector<std::uint8_t> encodeServerDiagnosticPacket(const ServerDiagnosticPacket& diag) {
  std::vector<std::uint8_t> out;
  out.reserve(diag.actionKind.size() + diag.reason.size() + diag.message.size() + 56);
  appendU32(out, PacketMagic);
  appendU16(out, PacketVersion);
  appendU16(out, static_cast<std::uint16_t>(PacketKind::ServerDiagnostic));
  appendU16(out, 0);
  appendU16(out, diag.severity);
  appendU64(out, diag.packetSequence);
  appendU64(out, diag.localSequence);
  if(!appendString16(out, diag.actionKind) ||
     !appendString16(out, diag.reason) ||
     !appendString32(out, diag.message)) {
    return {};
  }
  if(out.size() > MaxDatagramBytes)
    return {};
  return out;
}

inline ServerDiagnosticDecodeResult decodeServerDiagnosticPacket(std::string_view bytes) {
  ServerDiagnosticDecodeResult result;
  if(bytes.size() < 4 + 2 + 2 + 2 + 2 + 8 + 8 + 2 + 2 + 4) {
    result.error = DecodeError::TooSmall;
    return result;
  }

  std::size_t at = 0;
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t packetKind = 0;
  std::uint16_t flags = 0;

  if(!readU32(bytes, at, magic) || magic != PacketMagic) {
    result.error = DecodeError::BadMagic;
    return result;
  }
  if(!readU16(bytes, at, version) || version != PacketVersion) {
    result.error = DecodeError::BadVersion;
    return result;
  }
  if(!readU16(bytes, at, packetKind) || packetKind != static_cast<std::uint16_t>(PacketKind::ServerDiagnostic)) {
    result.error = DecodeError::BadPacketKind;
    return result;
  }
  if(!readU16(bytes, at, flags) ||
     !readU16(bytes, at, result.diagnostic.severity) ||
     !readU64(bytes, at, result.diagnostic.packetSequence) ||
     !readU64(bytes, at, result.diagnostic.localSequence) ||
     !readString16(bytes, at, result.diagnostic.actionKind) ||
     !readString16(bytes, at, result.diagnostic.reason) ||
     !readString32(bytes, at, result.diagnostic.message)) {
    result.error = DecodeError::Truncated;
    return result;
  }
  (void)flags;
  return result;
}

inline const char* decodeErrorName(DecodeError error) noexcept {
  switch(error) {
    case DecodeError::None: return "none";
    case DecodeError::TooSmall: return "too_small";
    case DecodeError::BadMagic: return "bad_magic";
    case DecodeError::BadVersion: return "bad_version";
    case DecodeError::BadPacketKind: return "bad_packet_kind";
    case DecodeError::BadActionKind: return "bad_action_kind";
    case DecodeError::Truncated: return "truncated";
    case DecodeError::StringTooLong: return "string_too_long";
    case DecodeError::InvalidPayload: return "invalid_payload";
  }
  return "unknown";
}

} // namespace Mmo::Net

