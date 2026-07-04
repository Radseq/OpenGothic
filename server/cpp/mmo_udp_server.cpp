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
#    error "mmo_udp_server requires thirdparty/asio/include/asio.hpp"
#  endif
#else
#  include <asio.hpp>
#endif
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include <array>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#include <iostream>
#include <limits>

#include "../../game/game/mmonetprotocol.h"
#include "mmo_server_snapshot_limits.h"
#include "mmo_server_types.h"
#include "mmo_server_identity.h"
#include "mmo_server_movement_authority.h"
#include "mmo_server_persistence.h"
#include "mmo_server_world_clock.h"

namespace {

std::atomic_bool gRunning {true};

struct ServerPacketLogState final {
  std::uint64_t suppressedMovementLines = 0;
  std::uint64_t nextMovementSummaryAt = 100;
  std::uint64_t suppressedWeaponStateLines = 0;
  std::uint64_t nextWeaponStateSummaryAt = 25;
};

struct LiveWorldSnapshotState final {
  bool          initialized = false;
  double        lastX = 0.0;
  double        lastY = 0.0;
  double        lastZ = 0.0;
  std::uint64_t lastTick = 0;
};

[[nodiscard]] std::optional<std::uint16_t> rawClientActionKind(std::string_view bytes) noexcept {
  constexpr std::size_t ActionKindOffset = 4 + 2 + 2 + 2;
  if(bytes.size() < ActionKindOffset + 2)
    return std::nullopt;
  const auto lo = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[ActionKindOffset]));
  const auto hi = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[ActionKindOffset + 1]));
  return static_cast<std::uint16_t>(lo | (hi << 8));
}

using Mmo::Server::BootstrapReadiness;
using Mmo::Server::DirectApplyResult;
using Mmo::Server::MySqlTarget;
using Mmo::Server::Options;
using Mmo::Server::WorldItemIdentity;
using Mmo::Server::DbBridgeVersion;
using Mmo::Server::sqlJson;
using Mmo::Server::sqlLiteral;
using Mmo::Server::parseMysqlUrl;
using Mmo::Server::runMysql;
using Mmo::Server::splitMysqlLastRow;
using Mmo::Server::mysqlSingleField;
using Mmo::Server::dbLogin;
using Mmo::Server::ensureActiveDbSession;
using Mmo::Server::readBootstrapReadinessWithFallback;
using Mmo::Server::readCharacterBootstrapSnapshotSlices;
using Mmo::Server::readNpcAuthoritySnapshotSlices;
using Mmo::Server::readPositionedBootstrapSnapshotSlices;
using Mmo::Server::readWorldBootstrapSnapshotSlices;
using Mmo::Server::buildSaveCheckpointBootstrapSnapshotJson;

void stopHandler(int) {
  gRunning.store(false, std::memory_order_relaxed);
}

[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<int> parseInt(std::string_view text) noexcept {
  int value = 0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

[[nodiscard]] std::optional<std::int64_t> parseI64(std::string_view text) noexcept {
  std::int64_t value = 0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

[[nodiscard]] std::optional<double> parseDouble(std::string_view text) noexcept {
  char* end = nullptr;
  const std::string copy(text);
  const double value = std::strtod(copy.c_str(), &end);
  if(end == nullptr || *end != '\0')
    return std::nullopt;
  return value;
}

[[nodiscard]] bool finiteCoord(double value) noexcept {
  return std::isfinite(value) && std::abs(value) <= 10000000.0;
}

[[nodiscard]] double distanceSquared3d(double ax, double ay, double az,
                                       double bx, double by, double bz) noexcept {
  const double dx = ax - bx;
  const double dy = ay - by;
  const double dz = az - bz;
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] double distance3d(double ax, double ay, double az,
                                double bx, double by, double bz) noexcept {
  return std::sqrt(distanceSquared3d(ax, ay, az, bx, by, bz));
}

[[nodiscard]] double finiteOrThrow(double value, std::string_view field) {
  if(!std::isfinite(value))
    throw std::runtime_error("non-finite numeric payload field: " + std::string(field));
  return value;
}

[[nodiscard]] std::string trim(std::string_view text) {
  while(!text.empty() && static_cast<unsigned char>(text.front()) <= ' ')
    text.remove_prefix(1);
  while(!text.empty() && static_cast<unsigned char>(text.back()) <= ' ')
    text.remove_suffix(1);
  return std::string(text);
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  for(char ch : text) {
    switch(ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
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

[[nodiscard]] std::optional<std::size_t> findFieldValueStart(std::string_view json, std::string_view key) {
  const auto needle = jsonEscape(key);
  auto pos = json.find(needle);
  while(pos != std::string_view::npos) {
    pos += needle.size();
    while(pos < json.size() && static_cast<unsigned char>(json[pos]) <= ' ')
      ++pos;
    if(pos < json.size() && json[pos] == ':') {
      ++pos;
      while(pos < json.size() && static_cast<unsigned char>(json[pos]) <= ' ')
        ++pos;
      return pos;
    }
    pos = json.find(needle, pos);
  }
  return std::nullopt;
}


[[nodiscard]] int hexNibble(char c) noexcept {
  if(c >= '0' && c <= '9')
    return c - '0';
  if(c >= 'a' && c <= 'f')
    return 10 + c - 'a';
  if(c >= 'A' && c <= 'F')
    return 10 + c - 'A';
  return -1;
}

[[nodiscard]] bool readJsonHex4(std::string_view text, std::size_t pos, std::uint32_t& out) noexcept {
  if(pos + 4 > text.size())
    return false;
  std::uint32_t value = 0;
  for(std::size_t i = 0; i < 4; ++i) {
    const int nibble = hexNibble(text[pos + i]);
    if(nibble < 0)
      return false;
    value = (value << 4U) | static_cast<std::uint32_t>(nibble);
  }
  out = value;
  return true;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
  if(cp <= 0x7FU) {
    out.push_back(static_cast<char>(cp));
  } else if(cp <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if(cp <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if(cp <= 0x10FFFFU) {
    out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
}

[[nodiscard]] bool appendJsonEscape(std::string& out, std::string_view json, std::size_t& i) {
  if(i + 1 >= json.size())
    return false;
  const char esc = json[++i];
  switch(esc) {
    case '"': out.push_back('"'); return true;
    case '\\': out.push_back('\\'); return true;
    case '/': out.push_back('/'); return true;
    case 'b': out.push_back('\b'); return true;
    case 'f': out.push_back('\f'); return true;
    case 'n': out.push_back('\n'); return true;
    case 'r': out.push_back('\r'); return true;
    case 't': out.push_back('\t'); return true;
    case 'u': {
      std::uint32_t first = 0;
      if(!readJsonHex4(json, i + 1, first))
        return false;
      i += 4;
      std::uint32_t cp = first;
      if(first >= 0xD800U && first <= 0xDBFFU) {
        if(i + 6 >= json.size() || json[i + 1] != '\\' || json[i + 2] != 'u')
          return false;
        std::uint32_t second = 0;
        if(!readJsonHex4(json, i + 3, second) || second < 0xDC00U || second > 0xDFFFU)
          return false;
        i += 6;
        cp = 0x10000U + (((first - 0xD800U) << 10U) | (second - 0xDC00U));
      } else if(first >= 0xDC00U && first <= 0xDFFFU) {
        return false;
      }
      appendUtf8(out, cp);
      return true;
    }
    default:
      return false;
  }
}

[[nodiscard]] std::optional<std::string> jsonStringField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos || *pos >= json.size() || json[*pos] != '"')
    return std::nullopt;
  std::string out;
  for(std::size_t i = *pos + 1; i < json.size(); ++i) {
    const char ch = json[i];
    if(ch == '"')
      return out;
    if(ch == '\\') {
      if(!appendJsonEscape(out, json, i))
        return std::nullopt;
      continue;
    }
    out.push_back(ch);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> jsonNumberTextField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos || *pos >= json.size())
    return std::nullopt;
  std::size_t end = *pos;
  while(end < json.size()) {
    const char ch = json[end];
    if((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')
      ++end;
    else
      break;
  }
  if(end == *pos)
    return std::nullopt;
  return std::string(json.substr(*pos, end - *pos));
}

[[nodiscard]] double requiredJsonDouble(std::string_view json, std::string_view key) {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    throw std::runtime_error("missing numeric payload field: " + std::string(key));
  auto value = parseDouble(*text);
  if(!value)
    throw std::runtime_error("invalid numeric payload field: " + std::string(key));
  return finiteOrThrow(*value, key);
}

[[nodiscard]] double optionalJsonDouble(std::string_view json, std::string_view key, double fallback) {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    return fallback;
  auto value = parseDouble(*text);
  if(!value)
    return fallback;
  return finiteOrThrow(*value, key);
}

[[nodiscard]] std::string optionalJsonDoubleSql(std::string_view json,
                                                std::string_view key0,
                                                std::string_view key1 = {},
                                                std::string_view key2 = {}) {
  const std::array<std::string_view, 3> keys {key0, key1, key2};
  for(const auto key : keys) {
    if(key.empty())
      continue;
    auto text = jsonNumberTextField(json, key);
    if(!text)
      continue;
    auto value = parseDouble(*text);
    if(value)
      return std::to_string(finiteOrThrow(*value, key));
  }
  return "NULL";
}

[[nodiscard]] std::optional<std::string_view> jsonObjectField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos || *pos >= json.size() || json[*pos] != '{')
    return std::nullopt;

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for(std::size_t i = *pos; i < json.size(); ++i) {
    const char ch = json[i];
    if(inString) {
      if(escaped) {
        escaped = false;
      } else if(ch == '\\') {
        escaped = true;
      } else if(ch == '"') {
        inString = false;
      }
      continue;
    }
    if(ch == '"') {
      inString = true;
      continue;
    }
    if(ch == '{') {
      ++depth;
      continue;
    }
    if(ch == '}') {
      --depth;
      if(depth == 0)
        return json.substr(*pos, i - *pos + 1);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<double> optionalJsonPositionDouble(std::string_view json,
                                                               std::string_view nestedKey,
                                                               std::string_view flatKey0,
                                                               std::string_view flatKey1,
                                                               std::string_view flatKey2) {
  if(auto item = jsonObjectField(json, "item_position")) {
    if(auto text = jsonNumberTextField(*item, nestedKey)) {
      if(auto value = parseDouble(*text))
        return finiteOrThrow(*value, nestedKey);
    }
  }
  if(auto actor = jsonObjectField(json, "actor_position")) {
    if(auto text = jsonNumberTextField(*actor, nestedKey)) {
      if(auto value = parseDouble(*text))
        return finiteOrThrow(*value, nestedKey);
    }
  }
  const std::array<std::string_view, 3> keys {flatKey0, flatKey1, flatKey2};
  for(const auto key : keys) {
    if(key.empty())
      continue;
    if(auto text = jsonNumberTextField(json, key)) {
      if(auto value = parseDouble(*text))
        return finiteOrThrow(*value, key);
    }
  }
  return std::nullopt;
}


struct JsonVec3 final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

[[nodiscard]] std::optional<JsonVec3> optionalJsonVec3(std::string_view json, std::string_view objectKey) {
  const auto object = jsonObjectField(json, objectKey);
  if(!object)
    return std::nullopt;

  const auto xText = jsonNumberTextField(*object, "x");
  const auto yText = jsonNumberTextField(*object, "y");
  const auto zText = jsonNumberTextField(*object, "z");
  if(!xText || !yText || !zText)
    return std::nullopt;

  const auto x = parseDouble(*xText);
  const auto y = parseDouble(*yText);
  const auto z = parseDouble(*zText);
  if(!x || !y || !z)
    return std::nullopt;

  return JsonVec3{finiteOrThrow(*x, "x"), finiteOrThrow(*y, "y"), finiteOrThrow(*z, "z")};
}

[[nodiscard]] std::int64_t requiredJsonI64(std::string_view json, std::string_view key) {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    throw std::runtime_error("missing integer payload field: " + std::string(key));
  auto value = parseI64(*text);
  if(!value)
    throw std::runtime_error("invalid integer payload field: " + std::string(key));
  return *value;
}

[[nodiscard]] std::int64_t optionalJsonI64(std::string_view json, std::string_view key, std::int64_t fallback) {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    return fallback;
  auto value = parseI64(*text);
  if(!value)
    return fallback;
  return *value;
}

[[nodiscard]] std::string optionalJsonString(std::string_view json, std::string_view key, std::string fallback = {}) {
  return jsonStringField(json, key).value_or(std::move(fallback));
}

[[nodiscard]] std::uint64_t packetServerTick(const Mmo::Net::ClientActionPacket& packet) {
  const auto payloadServerTick = optionalJsonI64(packet.payloadJson, "server_tick", 0);
  if(packet.clientTick != 0)
    return packet.clientTick;
  return payloadServerTick > 0 ? static_cast<std::uint64_t>(payloadServerTick) : 0u;
}

[[nodiscard]] std::optional<bool> jsonBoolField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos)
    return std::nullopt;
  const auto rest = json.substr(*pos);
  if(startsWith(rest, "true"))
    return true;
  if(startsWith(rest, "false"))
    return false;
  return std::nullopt;
}

[[nodiscard]] bool optionalJsonBool(std::string_view json, std::string_view key, bool fallback) {
  return jsonBoolField(json, key).value_or(fallback);
}

void appendJsonField(std::string& out, std::string_view key, std::string_view value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += jsonEscape(value);
}

void appendJsonRawField(std::string& out, std::string_view key, std::string_view value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += value.empty() ? "null" : std::string(value);
}

void appendJsonRawFieldBeforeFinalObjectBrace(std::string& out, std::string_view key, std::string_view value) {
  if(out.find(jsonEscape(key)) != std::string::npos)
    return;
  while(!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
    out.pop_back();
  if(out.empty() || out.back() != '}')
    return;
  out.pop_back();
  appendJsonRawField(out, key, value);
  out.push_back('}');
}

void appendJsonNumberField(std::string& out, std::string_view key, std::uint64_t value) {
  out.push_back(',');
  out += jsonEscape(key);
  out.push_back(':');
  out += std::to_string(value);
}

void appendPayloadStringAlias(std::string& out, std::string_view payload, std::string_view outputKey, std::string_view payloadKey) {
  if(auto v = jsonStringField(payload, payloadKey))
    appendJsonField(out, outputKey, *v);
}

void appendPayloadNumberAlias(std::string& out, std::string_view payload, std::string_view outputKey, std::string_view payloadKey) {
  if(auto v = jsonNumberTextField(payload, payloadKey))
    appendJsonRawField(out, outputKey, *v);
}

void appendPayloadBoolAlias(std::string& out, std::string_view payload, std::string_view outputKey, std::string_view payloadKey) {
  if(auto v = jsonBoolField(payload, payloadKey))
    appendJsonRawField(out, outputKey, *v ? "true" : "false");
}

[[nodiscard]] std::string equipmentSlotName(std::string_view raw) {
  auto slot = parseInt(raw);
  if(!slot)
    return "unknown";
  if(*slot == 1)
    return "weapon_melee";
  if(*slot == 2)
    return "weapon_ranged";
  return "unknown";
}

[[nodiscard]] std::string makeDbPayload(const Mmo::Net::ClientActionPacket& p, std::string_view remote) {
  const auto* def = Mmo::findSemanticAction(p.kind);
  const std::string_view actionName = def ? def->actionKind : std::string_view("unknown");
  const std::string_view eventType = def ? def->eventType : std::string_view("unknown");
  const std::string_view eventClass = def ? def->eventClass : std::string_view("unknown");
  const std::string_view procedure = def ? def->procedureName : std::string_view("unknown");
  const std::string_view payload = p.payloadJson;

  std::string out;
  out.reserve(payload.size() + 1400);
  out.push_back('{');
  out += "\"server_tick\":";
  out += std::to_string(p.clientTick);
  appendJsonNumberField(out, "client_tick", p.clientTick);
  appendJsonNumberField(out, "client_local_sequence", p.localSequence);
  appendJsonField(out, "client_idempotency_key", p.idempotencyKey);
  appendJsonField(out, "client_target_key", p.targetKey);
  appendJsonField(out, "client_action_kind", actionName);
  appendJsonField(out, "client_event_type", eventType);
  appendJsonField(out, "client_event_class", eventClass);
  appendJsonField(out, "client_procedure", procedure);
  appendJsonRawField(out, "client_payload", payload);
  appendJsonRawField(out, "metadata",
                     std::string("{\"source\":\"mmo_udp_server_cpp\",\"transport\":\"asio-udp-binary\",\"remote\":") +
                       jsonEscape(remote) + ",\"db_bridge_version\":" + std::to_string(DbBridgeVersion) + "}");

  appendPayloadStringAlias(out, payload, "actor_key", "actor_key");
  appendPayloadStringAlias(out, payload, "world", "world");
  appendPayloadStringAlias(out, payload, "item_symbol", "item_symbol");
  appendPayloadStringAlias(out, payload, "item_template_key", "item_template_key");
  appendPayloadStringAlias(out, payload, "item_persistent_id", "item_persistent_id");
  appendPayloadNumberAlias(out, payload, "amount", "amount");

  const std::string_view action = actionName;
  if(action == "client_bootstrap_request") {
    appendJsonField(out, "character_key", jsonStringField(payload, "character_key").value_or("PC_HERO"));
    appendPayloadStringAlias(out, payload, "server_endpoint", "server_endpoint");
    appendJsonRawField(out, "server_bound_client_mode", jsonBoolField(payload, "server_bound_client_mode").value_or(true) ? "true" : "false");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("client_bootstrap_request"));
  } else if(action == "movement_proposal" || action == "character_checkpoint") {
    appendJsonField(out, "character_key", jsonStringField(payload, "character_key").value_or("PC_HERO"));
    appendPayloadNumberAlias(out, payload, "pos_x", "pos_x");
    appendPayloadNumberAlias(out, payload, "pos_y", "pos_y");
    appendPayloadNumberAlias(out, payload, "pos_z", "pos_z");
    appendPayloadNumberAlias(out, payload, "rotation_yaw", "rotation_yaw");
    appendPayloadNumberAlias(out, payload, "yaw", "yaw");
    appendPayloadNumberAlias(out, payload, "from_tick", "from_tick");
    appendPayloadNumberAlias(out, payload, "to_tick", "to_tick");
    appendPayloadStringAlias(out, payload, "current_waypoint_key", "current_waypoint_key");
    appendPayloadNumberAlias(out, payload, "level", "level");
    appendPayloadNumberAlias(out, payload, "experience", "experience");
    appendPayloadNumberAlias(out, payload, "experience_next", "experience_next");
    appendPayloadNumberAlias(out, payload, "learning_points", "learning_points");
    appendPayloadNumberAlias(out, payload, "health_current", "health_current");
    appendPayloadNumberAlias(out, payload, "health_max", "health_max");
    appendPayloadNumberAlias(out, payload, "mana_current", "mana_current");
    appendPayloadNumberAlias(out, payload, "mana_max", "mana_max");
    appendPayloadNumberAlias(out, payload, "strength", "strength");
    appendPayloadNumberAlias(out, payload, "dexterity", "dexterity");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or(std::string(action)));
  } else if(action == "pickup_world_item" || action == "remove_world_item") {
    const auto target = jsonStringField(payload, "target_key").value_or(p.targetKey);
    appendJsonField(out, "world_item_entity_key", target);
    appendJsonField(out, "engine_world_item_key", target);
    appendPayloadStringAlias(out, payload, "source_world_item_persistent_id", "source_world_item_persistent_id");
    appendPayloadNumberAlias(out, payload, "bag_index", "bag_index");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("semantic_action"));
  } else if(action == "drop_character_item" || action == "loot_npc_inventory") {
    appendPayloadStringAlias(out, payload, "source_item_persistent_id", "source_item_persistent_id");
    appendPayloadStringAlias(out, payload, "target_npc_entity_key", "target_npc_entity_key");
    appendPayloadStringAlias(out, payload, "npc_key", "npc_key");
    appendPayloadNumberAlias(out, payload, "bag_index", "bag_index");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or(std::string(action)));
  } else if(action == "equip_character_item" || action == "unequip_character_item") {
    appendPayloadStringAlias(out, payload, "item_instance_id", "item_instance_id");
    appendPayloadStringAlias(out, payload, "item_persistent_id", "item_persistent_id");
    if(auto slot = jsonNumberTextField(payload, "slot")) {
      appendJsonField(out, "equipment_slot", equipmentSlotName(*slot));
      appendJsonRawField(out, "engine_equipment_slot", *slot);
    }
    appendPayloadNumberAlias(out, payload, "target_bag_index", "target_bag_index");
  } else if(action == "use_interactive" || action == "update_interactive_state") {
    appendPayloadStringAlias(out, payload, "interactive_key", "interactive_key");
    appendPayloadStringAlias(out, payload, "target_key", "target_key");
    appendPayloadStringAlias(out, payload, "state_after", "state_after");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or(std::string(action)));
  } else if(action == "set_script_int") {
    appendJsonField(out, "script_key", jsonStringField(payload, "script_key").value_or(jsonStringField(payload, "symbol_name").value_or(p.targetKey)));
    appendPayloadNumberAlias(out, payload, "value_index", "value_index");
    appendPayloadNumberAlias(out, payload, "value_before", "value_before");
    appendPayloadNumberAlias(out, payload, "value_after", "value_after");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("script_int_changed"));
  } else if(action == "update_quest") {
    appendJsonField(out, "quest_key", jsonStringField(payload, "quest_key").value_or(jsonStringField(payload, "topic").value_or(p.targetKey)));
    appendPayloadStringAlias(out, payload, "quest_name", "quest_name");
    appendPayloadStringAlias(out, payload, "status", "status");
    appendPayloadNumberAlias(out, payload, "entry_count", "entry_count");
  } else if(action == "set_known_dialog") {
    appendPayloadStringAlias(out, payload, "npc_key", "npc_key");
    appendPayloadStringAlias(out, payload, "info_key", "info_key");
    appendPayloadBoolAlias(out, payload, "known", "known");
    appendPayloadBoolAlias(out, payload, "removed", "removed");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("script_dialog_known"));
  } else if(action == "adjust_progression" || action == "apply_experience_reward") {
    appendPayloadNumberAlias(out, payload, "experience_delta", "experience_delta");
    appendPayloadNumberAlias(out, payload, "learning_points_delta", "learning_points_delta");
    appendJsonField(out, "reason", jsonStringField(payload, "reason").value_or("script_progression"));
  }

  out += ",\"resolver_ready\":true,\"resolver_missing_fields\":[],\"dispatch_ready\":true,\"dispatch_missing_fields\":[]}";
  return out;
}

[[nodiscard]] bool isFailOpenNpcObservationAction(Mmo::SemanticActionKind kind) noexcept {
  return kind == Mmo::SemanticActionKind::RecordNpcRoutineState ||
         kind == Mmo::SemanticActionKind::RecordNpcAiState ||
         kind == Mmo::SemanticActionKind::RecordNpcPathState ||
         kind == Mmo::SemanticActionKind::RecordNpcFightState;
}


[[nodiscard]] std::string buildBootstrapSnapshotJson(const MySqlTarget& target,
                                                     std::string_view sessionUuid,
                                                     std::string_view characterKey,
                                                     std::string_view worldName,
                                                     const BootstrapReadiness& readiness,
                                                     bool preferSaveCheckpointRestore,
                                                     bool requireSaveCheckpointRestore) {
  if(preferSaveCheckpointRestore) {
    if(auto checkpointSnapshot = buildSaveCheckpointBootstrapSnapshotJson(target, sessionUuid); !checkpointSnapshot.empty()) {
      std::cout << "[bootstrap_db_save_checkpoint_restore] bytes=" << checkpointSnapshot.size()
                << " session=" << sessionUuid << "\n";
      return checkpointSnapshot;
    }

    if(requireSaveCheckpointRestore)
      throw std::runtime_error("strict DB-save-checkpoint restore requested but no checkpoint bootstrap snapshot is available");

    std::cout << "[bootstrap_live_projection_fallback] reason=no_db_save_checkpoint session="
              << sessionUuid << "\n";
  }

  const std::string sessionSql = sqlLiteral(sessionUuid);
  const std::string worldSql = sqlLiteral(worldName);
  const auto characterSlices = readCharacterBootstrapSnapshotSlices(target, sessionUuid, characterKey, worldName);
  const auto& characterList = characterSlices.characterListJson;
  const auto& character = characterSlices.characterJson;
  const auto& inventory = characterSlices.inventoryJson;
  const auto& equipment = characterSlices.equipmentJson;
  const auto& dialogs = characterSlices.knownDialogsJson;
  const auto& quests = characterSlices.questsJson;
  const auto& scriptState = characterSlices.scriptStateJson;

  const auto worldSlices = readWorldBootstrapSnapshotSlices(target, sessionUuid, worldName);
  const auto& worldDeltas = worldSlices.worldDeltasJson;
  const auto& worldClock = worldSlices.worldClockJson;

  const auto positionedSlices = readPositionedBootstrapSnapshotSlices(target, sessionUuid, worldName);
  const auto& activeWorldItems = positionedSlices.activeWorldItemsJson;
  const auto& nearbyNpcs = positionedSlices.nearbyNpcsJson;
  const auto& nearbyNpcKnownDialogs = positionedSlices.nearbyNpcKnownDialogsJson;
  const auto& nearbyWaypoints = positionedSlices.nearbyWaypointsJson;

  const auto& interactivesSample = worldSlices.interactiveStateJson;
  const auto& npcLifecycle = worldSlices.npcLifecycleJson;
  const auto& recentEvents = worldSlices.recentEventsJson;
  const auto& moverState = worldSlices.moverStateJson;

  const auto npcAuthority = readNpcAuthoritySnapshotSlices(target, sessionUuid, "bootstrap");
  const auto& npcRoutineState = npcAuthority.routineStateJson;
  const auto& npcAiState = npcAuthority.aiStateJson;
  const auto& npcPathState = npcAuthority.pathStateJson;
  const auto& npcFightState = npcAuthority.fightStateJson;

  const auto& triggerQueue = worldSlices.triggerQueueJson;
  const auto& worldTransitionState = worldSlices.worldTransitionStateJson;
  const auto& clientCorrections = worldSlices.clientCorrectionsJson;
  const auto& checkpointManifest = worldSlices.checkpointManifestJson;

  std::string out;
  out.reserve(character.size() + inventory.size() + equipment.size() + dialogs.size() + quests.size() +
              scriptState.size() + worldDeltas.size() + worldClock.size() + activeWorldItems.size() +
              nearbyNpcs.size() + nearbyNpcKnownDialogs.size() + nearbyWaypoints.size() +
              interactivesSample.size() + npcLifecycle.size() + recentEvents.size() +
              moverState.size() + npcRoutineState.size() + npcAiState.size() + npcPathState.size() +
              npcFightState.size() + triggerQueue.size() + worldTransitionState.size() +
              clientCorrections.size() + checkpointManifest.size() + characterList.size() + 2048);
  out.push_back('{');
  out += "\"schema\":";
  out += jsonEscape(Mmo::Server::BootstrapSnapshotSchema);
  appendJsonField(out, "source", "mmo_udp_server_cpp_live_mysql");
  appendJsonField(out, "snapshot_source", "current_projections_v1");
  appendJsonField(out, "session_uuid", sessionUuid);
  appendJsonField(out, "character_key", characterKey);
  appendJsonField(out, "world_name", worldName);
  appendJsonRawField(out, "ready", readiness.ready ? "true" : "false");
  appendJsonNumberField(out, "world_entity_count", readiness.worldEntityRows);
  appendJsonNumberField(out, "world_inventory_count", readiness.worldInventoryRows);
  appendJsonRawField(out, "active_world_item_radius", std::to_string(Mmo::Server::BootstrapActiveWorldItemRadius));
  appendJsonRawField(out, "nearby_npc_radius", std::to_string(Mmo::Server::BootstrapNearbyNpcRadius));
  appendJsonRawField(out, "nearby_waypoint_radius", std::to_string(Mmo::Server::BootstrapNearbyWaypointRadius));
  appendJsonNumberField(out, "interactive_count", readiness.interactiveRows);
  appendJsonNumberField(out, "script_int_count", readiness.scriptIntRows);
  appendJsonRawField(out, "script_state_truncated", readiness.scriptIntRows > Mmo::Server::MaxBootstrapScriptStateRows ? "true" : "false");
  appendJsonRawField(out, "character_list", characterList);
  appendJsonRawField(out, "character", character);
  appendJsonRawField(out, "inventory", inventory);
  appendJsonRawField(out, "equipment", equipment);
  appendJsonRawField(out, "known_dialogs", dialogs);
  appendJsonRawField(out, "quests", quests);
  appendJsonRawField(out, "script_state", scriptState);
  appendJsonRawField(out, "world_clock", worldClock);
  appendJsonRawField(out, Mmo::Server::BootstrapActiveWorldItemsSection, activeWorldItems);
  appendJsonRawField(out, "world_inventory_sample", activeWorldItems);
  appendJsonRawField(out, Mmo::Server::BootstrapNearbyNpcsSection, nearbyNpcs);
  appendJsonRawField(out, Mmo::Server::BootstrapNearbyNpcKnownDialogsSection, nearbyNpcKnownDialogs);
  appendJsonRawField(out, Mmo::Server::BootstrapNearbyWaypointsSection, nearbyWaypoints);
  appendJsonRawField(out, Mmo::Server::BootstrapInteractiveStateSection, interactivesSample);
  appendJsonRawField(out, "interactive_sample", "[]");
  appendJsonRawField(out, Mmo::Server::BootstrapNpcLifecycleStateSection, npcLifecycle);
  appendJsonRawField(out, Mmo::Server::BootstrapWorldItemDeltasSection, worldDeltas);
  appendJsonRawField(out, "world_entity_delta_sample", "[]");
  appendJsonRawField(out, Mmo::Server::BootstrapRecentActionsSection, recentEvents);
  appendJsonRawField(out, "recent_events_sample", recentEvents);
  appendJsonRawField(out, Mmo::Server::BootstrapMoverStateSection, moverState);
  appendJsonRawField(out, Mmo::Server::BootstrapNpcRoutineStateSection, npcRoutineState);
  appendJsonRawField(out, Mmo::Server::BootstrapNpcAiStateSection, npcAiState);
  appendJsonRawField(out, Mmo::Server::BootstrapNpcPathStateSection, npcPathState);
  appendJsonRawField(out, Mmo::Server::BootstrapNpcFightStateSection, npcFightState);
  appendJsonRawField(out, Mmo::Server::BootstrapTriggerQueueSection, triggerQueue);
  appendJsonRawField(out, Mmo::Server::BootstrapWorldTransitionStateSection, worldTransitionState);
  appendJsonRawField(out, Mmo::Server::BootstrapClientCorrectionsSection, clientCorrections);
  appendJsonRawField(out, Mmo::Server::BootstrapServerCheckpointManifestSection, checkpointManifest);
  out += ",\"server_note\":\"server-bound client applies HERO stats, inventory, equipment, position, story, script ints, world item tombstones, active world items, nearby NPC/dialog/waypoint/action windows, interactive state, mover state, correction slices, server checkpoint manifest and NPC lifecycle/authority slices when safe\"}";
  return out;
}


[[nodiscard]] std::optional<JsonVec3> movementToPosition(std::string_view payload) {
  const auto x = optionalJsonDouble(payload, "to_pos_x", std::numeric_limits<double>::quiet_NaN());
  const auto y = optionalJsonDouble(payload, "to_pos_y", std::numeric_limits<double>::quiet_NaN());
  const auto z = optionalJsonDouble(payload, "to_pos_z", std::numeric_limits<double>::quiet_NaN());
  if(!finiteCoord(x) || !finiteCoord(y) || !finiteCoord(z))
    return std::nullopt;
  return JsonVec3{x, y, z};
}

[[nodiscard]] bool shouldSendLiveWorldSnapshot(LiveWorldSnapshotState& state,
                                              const Mmo::Net::ClientActionPacket& packet,
                                              bool packetAccepted,
                                              const DirectApplyResult& direct) {
  if(packet.kind != Mmo::SemanticActionKind::MovementProposal || !packetAccepted || !direct.handled || !direct.accepted)
    return false;

  const auto pos = movementToPosition(packet.payloadJson);
  if(!pos)
    return false;

  const auto tick = packetServerTick(packet);

  if(!state.initialized) {
    state.initialized = true;
    state.lastX = pos->x;
    state.lastY = pos->y;
    state.lastZ = pos->z;
    state.lastTick = tick;
    return false;
  }

  const double dist = distance3d(pos->x, pos->y, pos->z, state.lastX, state.lastY, state.lastZ);
  const auto elapsed = tick >= state.lastTick ? tick - state.lastTick : 0;
  if(dist < Mmo::Server::LiveWorldItemRefreshDistance &&
     !(elapsed >= Mmo::Server::LiveWorldItemRefreshMaxIntervalMs &&
       dist >= Mmo::Server::LiveWorldItemRefreshMinMoveDistance))
    return false;

  state.lastX = pos->x;
  state.lastY = pos->y;
  state.lastZ = pos->z;
  state.lastTick = tick;
  return true;
}

void printPacketProgress(ServerPacketLogState& logState,
                         std::uint64_t accepted,
                         std::uint64_t received,
                         std::uint64_t invalid,
                         std::uint64_t duplicate,
                         std::uint64_t enqueued,
                         std::uint64_t directDb,
                         std::uint64_t unhandled,
                         std::uint64_t failed,
                         std::string_view actionName,
                         bool packetAccepted,
                         bool hasDiagnostic,
                         bool snapshotSent,
                         bool isMovement,
                         bool isWeaponState) {
  if(isMovement) {
    if(packetAccepted && !hasDiagnostic) {
      ++logState.suppressedMovementLines;
      if(!snapshotSent && logState.suppressedMovementLines < logState.nextMovementSummaryAt)
        return;
      if(!snapshotSent)
        logState.nextMovementSummaryAt += 100;
      std::cout << "[movement_summary] accepted=" << accepted
                << " received=" << received
                << " movement_lines_suppressed=" << logState.suppressedMovementLines
                << " direct_db=" << directDb
                << " failed=" << failed
                << " snapshot_sent=" << (snapshotSent ? 1 : 0)
                << "\n";
      return;
    }

    std::cout << "[movement_result] accepted=" << accepted
              << " received=" << received
              << " invalid=" << invalid
              << " duplicate=" << duplicate
              << " direct_db=" << directDb
              << " failed=" << failed
              << " action=" << actionName
              << " packet_accepted=" << (packetAccepted ? 1 : 0)
              << " diagnostic=" << (hasDiagnostic ? 1 : 0)
              << "\n";
    return;
  }

  const bool looksLikeWeaponState = isWeaponState ||
                                    actionName == "ready_weapon" ||
                                    actionName == "holster_weapon";
  if(looksLikeWeaponState && packetAccepted && !hasDiagnostic && !snapshotSent) {
    ++logState.suppressedWeaponStateLines;
    if(logState.suppressedWeaponStateLines < logState.nextWeaponStateSummaryAt)
      return;
    logState.nextWeaponStateSummaryAt += 25;
    std::cout << "[weapon_state_summary] accepted=" << accepted
              << " received=" << received
              << " weapon_state_lines_suppressed=" << logState.suppressedWeaponStateLines
              << " direct_db=" << directDb
              << " failed=" << failed
              << "\n";
    return;
  }

  std::cout << "accepted=" << accepted
            << " received=" << received
            << " invalid=" << invalid
            << " duplicate=" << duplicate
            << " enqueued=" << enqueued
            << " direct_db=" << directDb
            << " unhandled=" << unhandled
            << " failed=" << failed;
  if(logState.suppressedMovementLines != 0)
    std::cout << " movement_lines_suppressed=" << logState.suppressedMovementLines;
  if(logState.suppressedWeaponStateLines != 0)
    std::cout << " weapon_state_lines_suppressed=" << logState.suppressedWeaponStateLines;
  std::cout << " last=" << actionName << "\n";
}

void printBootstrapAck(const Mmo::Net::ClientActionPacket& packet,
                       std::string_view characterKey,
                       std::string_view worldName,
                       const BootstrapReadiness& readiness,
                       bool dbChecked) {
  std::cout << "bootstrap_ack"
            << " accepted=1"
            << " ready=" << (readiness.ready ? 1 : 0)
            << " db_checked=" << (dbChecked ? 1 : 0)
            << " session=" << packet.sessionKey
            << " character=" << characterKey
            << " world=" << worldName
            << " meta=" << readiness.metaRows
            << " char=" << readiness.characterRows
            << " world_entities=" << readiness.worldEntityRows
            << " inventory=" << readiness.characterInventoryRows
            << " quests=" << readiness.questRows
            << " dialogs=" << readiness.knownDialogRows
            << " script_ints=" << readiness.scriptIntRows
            << " waypoints=" << readiness.waypointRows
            << " waypoint_edges=" << readiness.waypointEdgeRows
            << " world_inventory=" << readiness.worldInventoryRows
            << " interactives=" << readiness.interactiveRows
            << " clock=" << readiness.worldClockRows
            << "\n";
}

void sendBootstrapSnapshot(asio::ip::udp::socket& socket,
                           const asio::ip::udp::endpoint& remote,
                           const Mmo::Net::ClientActionPacket& request,
                           std::uint32_t snapshotId,
                           std::string_view snapshotJson) {
  constexpr std::size_t ChunkBytes = Mmo::Server::BootstrapSnapshotChunkPayloadBytes;
  if(snapshotJson.empty())
    return;
  const std::size_t chunkCountSize = (snapshotJson.size() + ChunkBytes - 1u) / ChunkBytes;
  if(chunkCountSize == 0 || chunkCountSize > 65535u)
    throw std::runtime_error("bootstrap snapshot too large for UDP chunk envelope");

  const auto chunkCount = static_cast<std::uint16_t>(chunkCountSize);
  for(std::uint16_t index = 0; index != chunkCount; ++index) {
    const std::size_t offset = static_cast<std::size_t>(index) * ChunkBytes;
    const std::size_t count = std::min<std::size_t>(ChunkBytes, snapshotJson.size() - offset);
    Mmo::Net::ServerSnapshotChunkPacket chunk;
    chunk.packetSequence = request.packetSequence;
    chunk.localSequence = request.localSequence;
    chunk.snapshotId = snapshotId;
    chunk.chunkIndex = index;
    chunk.chunkCount = chunkCount;
    chunk.totalBytes = static_cast<std::uint32_t>(snapshotJson.size());
    chunk.payloadJsonFragment = std::string(snapshotJson.substr(offset, count));

    const auto packet = Mmo::Net::encodeServerSnapshotChunkPacket(chunk);
    if(packet.empty())
      throw std::runtime_error("failed to encode bootstrap snapshot chunk");
    asio::error_code ec;
    socket.send_to(asio::buffer(packet), remote, 0, ec);
    if(ec)
      throw std::runtime_error("failed to send bootstrap snapshot chunk: " + ec.message());

    if((index + 1u) % 8u == 0u)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::cout << "bootstrap_snapshot_sent"
            << " id=" << snapshotId
            << " bytes=" << snapshotJson.size()
            << " chunks=" << chunkCount
            << "\n";
}

void sendServerDiagnostic(asio::ip::udp::socket& socket,
                          const asio::ip::udp::endpoint& remote,
                          const Mmo::Net::ClientActionPacket& request,
                          std::uint16_t severity,
                          std::string_view actionKind,
                          std::string_view reason,
                          std::string_view message) noexcept {
  try {
    constexpr std::size_t MaxDiagnosticMessageBytes = 4096;
    Mmo::Net::ServerDiagnosticPacket diag;
    diag.packetSequence = request.packetSequence;
    diag.localSequence = request.localSequence;
    diag.severity = severity;
    diag.actionKind = std::string(actionKind);
    diag.reason = std::string(reason);
    diag.message = std::string(message.substr(0, std::min<std::size_t>(message.size(), MaxDiagnosticMessageBytes)));

    const auto encoded = Mmo::Net::encodeServerDiagnosticPacket(diag);
    if(encoded.empty()) {
      std::cerr << "[diagnostic_encode_failed] action=" << actionKind
                << " reason=" << reason << "\n";
      return;
    }

    asio::error_code ec;
    socket.send_to(asio::buffer(encoded), remote, 0, ec);
    if(ec) {
      std::cerr << "[diagnostic_send_failed] action=" << actionKind
                << " reason=" << reason
                << " error=" << ec.message() << "\n";
    }
  } catch(const std::exception& exc) {
    std::cerr << "[diagnostic_failed] action=" << actionKind
              << " reason=" << reason
              << " error=" << exc.what() << "\n";
  } catch(...) {
    std::cerr << "[diagnostic_failed] action=" << actionKind
              << " reason=" << reason
              << " error=unknown\n";
  }
}

void enqueueOutbox(const MySqlTarget& target,
                   std::string_view sessionUuid,
                   const Mmo::Net::ClientActionPacket& packet,
                   std::string_view dbPayload,
                   int priority,
                   int maxAttempts) {
  const auto* def = Mmo::findSemanticAction(packet.kind);
  const std::string_view actionName = def ? def->actionKind : std::string_view("unknown");
  const Mmo::Server::OutboxActionRecord record {
    .sessionUuid = sessionUuid,
    .actionName = actionName,
    .targetKey = packet.targetKey,
    .dbPayload = dbPayload,
    .idempotencyKey = packet.idempotencyKey,
    .priority = priority,
    .maxAttempts = maxAttempts,
  };
  Mmo::Server::enqueueOutboxAction(target, record);
}

void applyCharacterCheckpoint(const MySqlTarget& target,
                              std::string_view sessionUuid,
                              const Mmo::Net::ClientActionPacket& packet,
                              std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const auto serverTick = packetServerTick(packet);
  const auto waypoint = jsonStringField(payload, "current_waypoint_key").value_or("");
  const Mmo::Server::CharacterCheckpointRecord record {
    .sessionUuid = sessionUuid,
    .serverTick = serverTick,
    .posX = requiredJsonDouble(payload, "pos_x"),
    .posY = requiredJsonDouble(payload, "pos_y"),
    .posZ = requiredJsonDouble(payload, "pos_z"),
    .rotationYaw = requiredJsonDouble(payload, "rotation_yaw"),
    .waypoint = waypoint,
    .level = requiredJsonI64(payload, "level"),
    .experience = requiredJsonI64(payload, "experience"),
    .experienceNext = requiredJsonI64(payload, "experience_next"),
    .learningPoints = requiredJsonI64(payload, "learning_points"),
    .healthCurrent = requiredJsonI64(payload, "health_current"),
    .healthMax = requiredJsonI64(payload, "health_max"),
    .manaCurrent = requiredJsonI64(payload, "mana_current"),
    .manaMax = requiredJsonI64(payload, "mana_max"),
    .strength = requiredJsonI64(payload, "strength"),
    .dexterity = requiredJsonI64(payload, "dexterity"),
    .guild = optionalJsonI64(payload, "guild", 0),
    .trueGuild = optionalJsonI64(payload, "true_guild", 0),
    .permanentAttitude = optionalJsonI64(payload, "permanent_attitude", 0),
    .temporaryAttitude = optionalJsonI64(payload, "temporary_attitude", 0),
    .dbPayload = dbPayload,
    .idempotencyKey = packet.idempotencyKey,
  };
  Mmo::Server::recordCharacterCheckpoint(target, record);
}


void applySaveCheckpointManifest(const MySqlTarget& target,
                                 std::string_view sessionUuid,
                                 const Mmo::Net::ClientActionPacket& packet,
                                 std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const auto serverTick = packetServerTick(packet);
  const auto manifestKey = optionalJsonString(payload, "manifest_key", packet.targetKey);
  const auto checkpointKind = optionalJsonString(payload, "checkpoint_kind", "native_save");
  const auto reason = optionalJsonString(payload, "reason", "save_checkpoint_manifest");
  const Mmo::Server::SaveCheckpointManifestRecord record {
    .sessionUuid = sessionUuid,
    .manifestKey = manifestKey,
    .checkpointKind = checkpointKind,
    .reason = reason,
    .serverTick = serverTick,
    .dbPayload = dbPayload,
    .idempotencyKey = packet.idempotencyKey,
  };
  Mmo::Server::createSaveCheckpointManifest(target, record);
}

void callCheckpoint(const MySqlTarget& target,
                    std::string_view sessionUuid,
                    const Mmo::Net::ClientActionPacket& packet,
                    std::string_view dbPayload,
                    double posX,
                    double posY,
                    double posZ,
                    double rotationYaw) {
  const std::string_view payload = packet.payloadJson;
  const auto serverTick = packetServerTick(packet);
  const auto waypoint = optionalJsonString(payload, "current_waypoint_key");
  const Mmo::Server::CharacterCheckpointRecord record {
    .sessionUuid = sessionUuid,
    .serverTick = serverTick,
    .posX = posX,
    .posY = posY,
    .posZ = posZ,
    .rotationYaw = rotationYaw,
    .waypoint = waypoint,
    .level = optionalJsonI64(payload, "level", 0),
    .experience = optionalJsonI64(payload, "experience", 0),
    .experienceNext = optionalJsonI64(payload, "experience_next", 500),
    .learningPoints = optionalJsonI64(payload, "learning_points", 0),
    .healthCurrent = optionalJsonI64(payload, "health_current", 0),
    .healthMax = optionalJsonI64(payload, "health_max", 0),
    .manaCurrent = optionalJsonI64(payload, "mana_current", 0),
    .manaMax = optionalJsonI64(payload, "mana_max", 0),
    .strength = optionalJsonI64(payload, "strength", 0),
    .dexterity = optionalJsonI64(payload, "dexterity", 0),
    .guild = optionalJsonI64(payload, "guild", 0),
    .trueGuild = optionalJsonI64(payload, "true_guild", 0),
    .permanentAttitude = optionalJsonI64(payload, "permanent_attitude", 0),
    .temporaryAttitude = optionalJsonI64(payload, "temporary_attitude", 0),
    .dbPayload = dbPayload,
    .idempotencyKey = packet.idempotencyKey,
  };
  Mmo::Server::recordCharacterCheckpoint(target, record);
}

[[nodiscard]] DirectApplyResult applyMovementProposal(const MySqlTarget& target,
                                                      std::string_view sessionUuid,
                                                      const Mmo::Net::ClientActionPacket& packet,
                                                      std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const double fromX = requiredJsonDouble(payload, "from_pos_x");
  const double fromY = requiredJsonDouble(payload, "from_pos_y");
  const double fromZ = requiredJsonDouble(payload, "from_pos_z");
  const double toX = requiredJsonDouble(payload, "to_pos_x");
  const double toY = requiredJsonDouble(payload, "to_pos_y");
  const double toZ = requiredJsonDouble(payload, "to_pos_z");
  const double yaw = optionalJsonDouble(payload, "to_rotation_yaw", optionalJsonDouble(payload, "rotation_yaw", 0.0));
  const auto fromTick = optionalJsonI64(payload, "from_tick", 0);
  const auto toTick = optionalJsonI64(payload, "to_tick", static_cast<std::int64_t>(packet.clientTick));
  const auto deltaMs = optionalJsonI64(payload, "delta_ms", toTick - fromTick);

  const Mmo::Server::MovementProposalInput movement {
    .fromX = fromX,
    .fromY = fromY,
    .fromZ = fromZ,
    .toX = toX,
    .toY = toY,
    .toZ = toZ,
    .deltaMs = deltaMs,
  };
  const auto validation = Mmo::Server::validateMovementProposal(movement);

  if(!validation.accepted) {
    std::cerr << "[movement_rejected]"
              << " delta_ms=" << deltaMs
              << " total=" << validation.totalDistance
              << " horizontal_speed=" << validation.horizontalSpeed
              << " vertical_delta=" << validation.verticalDelta
              << " vertical_speed=" << validation.verticalSpeed
              << " stale_tiny=" << (validation.staleTinyDelta ? 1 : 0)
              << "\n";
    return {true, false, false, "movement_rejected"};
  }

  if(validation.staleTinyDelta) {
    std::cout << "[movement_stale_delta_accepted]"
              << " delta_ms=" << deltaMs
              << " total=" << validation.totalDistance
              << " vertical_delta=" << validation.verticalDelta
              << "\n";
  }
  callCheckpoint(target, sessionUuid, packet, dbPayload, toX, toY, toZ, yaw);
  return {true, true, true, "movement_checkpoint"};
}

void recordClientActionCorrection(const MySqlTarget& target,
                                  std::string_view sessionUuid,
                                  const Mmo::Net::ClientActionPacket& packet,
                                  std::string_view actionName,
                                  std::string_view reason,
                                  std::string_view dbPayload) {
  const auto tick = packetServerTick(packet);
  std::string idempotency = packet.idempotencyKey;
  idempotency += ":correction";
  const Mmo::Server::ClientActionCorrectionRecord record {
    .sessionUuid = sessionUuid,
    .actionName = actionName,
    .localSequence = packet.localSequence,
    .correctionKind = "rollback_to_authoritative_position",
    .reason = reason,
    .serverTick = tick,
    .dbPayload = dbPayload,
    .idempotencyKey = idempotency,
  };
  Mmo::Server::recordClientActionCorrection(target, record);
}

[[nodiscard]] std::string normalizedEquipmentSlot(std::string_view payload) {
  if(auto slot = jsonStringField(payload, "equipment_slot"); slot && !slot->empty())
    return *slot;
  if(auto slot = jsonStringField(payload, "slot"); slot && !slot->empty())
    return *slot;
  const auto numeric = optionalJsonI64(payload, "slot", 0);
  if(numeric == 1)
    return "weapon_melee";
  if(numeric == 2)
    return "weapon_ranged";
  return "unknown";
}

[[nodiscard]] std::string questStatus(std::string_view payload) {
  std::string value = optionalJsonString(payload, "status", "running");
  for(char& ch : value)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if(value == "1" || value == "run" || value == "in_progress")
    return "running";
  if(value == "2" || value == "completed_success" || value == "succeeded")
    return "success";
  if(value == "3" || value == "failure" || value == "completed_failed")
    return "failed";
  if(value == "4" || value == "closed")
    return "obsolete";
  return value.empty() ? "running" : value;
}

[[nodiscard]] std::string scriptKeyFromPayload(const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  if(auto key = jsonStringField(payload, "script_key"); key && !key->empty())
    return *key;
  if(auto key = jsonStringField(payload, "global_key"); key && !key->empty())
    return *key;
  if(auto key = jsonStringField(payload, "symbol_name"); key && !key->empty())
    return *key;
  if(!packet.targetKey.empty())
    return packet.targetKey;
  return "script-int:" + std::to_string(optionalJsonI64(payload, "symbol_index", 0)) + ":" +
         std::to_string(optionalJsonI64(payload, "value_index", 0));
}

[[nodiscard]] std::optional<std::int64_t> parseI64Segment(std::string_view text) noexcept {
  if(text.empty())
    return std::nullopt;
  std::int64_t value = 0;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

[[nodiscard]] WorldItemIdentity parseWorldItemIdentity(std::string raw) {
  WorldItemIdentity out;
  out.exact = std::move(raw);
  std::string_view text = out.exact;

  const auto pidMarker = text.find(":pid:");
  const auto symMarker = text.find(":sym:");
  if(pidMarker != std::string_view::npos && symMarker != std::string_view::npos && pidMarker < symMarker) {
    if(Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::WorldItemHookPrefix)) {
      out.world = std::string(text.substr(Mmo::Server::Identity::WorldItemHookPrefix.size(), pidMarker - Mmo::Server::Identity::WorldItemHookPrefix.size()));
    }
    // Malformed keys such as world-item.zen:pid:... appeared in old local
    // binaries. Do not trust the abbreviated world part; payload.world is more
    // canonical and will fill out.world below in the resolver.
    if(auto pid = parseI64Segment(text.substr(pidMarker + 5, symMarker - (pidMarker + 5))))
      out.persistentId = *pid;
    if(auto sym = parseI64Segment(text.substr(symMarker + 5)))
      out.symbol = *sym;
    return out;
  }

  constexpr std::string_view dbPrefix = "world_item:";
  if(Mmo::Server::Identity::startsWith(text, dbPrefix)) {
    std::string_view rest = text.substr(dbPrefix.size());
    const auto first = rest.find(':');
    if(first != std::string_view::npos) {
      const auto second = rest.find(':', first + 1);
      if(second != std::string_view::npos) {
        out.world = std::string(rest.substr(0, first));
        if(auto pid = parseI64Segment(rest.substr(first + 1, second - first - 1)))
          out.persistentId = *pid;
        const auto third = rest.find(':', second + 1);
        const auto symEnd = third == std::string_view::npos ? rest.size() : third;
        if(auto sym = parseI64Segment(rest.substr(second + 1, symEnd - second - 1)))
          out.symbol = *sym;
      }
    }
  }

  return out;
}


struct ResolvedWorldNpcEntity final {
  std::string entityKey;
  std::string lifecycleState;
  std::int64_t rowVersion = 0;
};

struct WorldNpcIdentity final {
  std::string exact;
  std::string world;
  std::int64_t persistentId = -1;
  std::int64_t symbol = -1;
};

void fillWorldNpcIdentityFromPayload(WorldNpcIdentity& identity, std::string_view payload) {
  if(identity.persistentId < 0)
    identity.persistentId = optionalJsonI64(payload, "target_npc_persistent_id",
                            optionalJsonI64(payload, "source_npc_persistent_id",
                            optionalJsonI64(payload, "npc_persistent_id",
                            optionalJsonI64(payload, "persistent_id", -1))));
  if(identity.symbol < 0)
    identity.symbol = optionalJsonI64(payload, "target_npc_symbol",
                      optionalJsonI64(payload, "source_npc_symbol",
                      optionalJsonI64(payload, "npc_symbol",
                      optionalJsonI64(payload, "symbol", -1))));
  if(identity.world.empty())
    identity.world = optionalJsonString(payload, "world");
}

[[nodiscard]] WorldNpcIdentity parseWorldNpcIdentity(std::string raw) {
  WorldNpcIdentity out;
  out.exact = std::move(raw);
  const std::string_view text = out.exact;
  if(!Mmo::Server::Identity::looksLikeNpcKey(text))
    return out;

  const auto pidMarker = text.find(":pid:");
  const auto symMarker = text.find(":sym:");
  if(pidMarker != std::string_view::npos && symMarker != std::string_view::npos && pidMarker < symMarker) {
    if(Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::NpcHookPrefix))
      out.world = std::string(text.substr(Mmo::Server::Identity::NpcHookPrefix.size(), pidMarker - Mmo::Server::Identity::NpcHookPrefix.size()));
    else if(Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::CreatureHookPrefix))
      out.world = std::string(text.substr(Mmo::Server::Identity::CreatureHookPrefix.size(), pidMarker - Mmo::Server::Identity::CreatureHookPrefix.size()));
    // Old malformed packets can look like npc.zen:pid:...; do not trust that
    // abbreviated world segment because payload.world is the authoritative
    // world instance name used by the server session.
    if(auto pid = parseI64Segment(text.substr(pidMarker + 5, symMarker - (pidMarker + 5))))
      out.persistentId = *pid;
    if(auto sym = parseI64Segment(text.substr(symMarker + 5)))
      out.symbol = *sym;
    return out;
  }

  // Older actor key emitted by lightweight hooks: npc:<persistent_id>:sym:<symbol>.
  if(Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::NpcHookPrefix) && symMarker != std::string_view::npos && symMarker > 4) {
    if(auto pid = parseI64Segment(text.substr(4, symMarker - 4)))
      out.persistentId = *pid;
    if(auto sym = parseI64Segment(text.substr(symMarker + 5)))
      out.symbol = *sym;
    return out;
  }

  const std::string_view prefix = Mmo::Server::Identity::startsWith(text, Mmo::Server::Identity::CreatureHookPrefix) ?
    Mmo::Server::Identity::CreatureHookPrefix : Mmo::Server::Identity::NpcHookPrefix;
  if(!Mmo::Server::Identity::startsWith(text, prefix))
    return out;

  // Runtime/import key: npc|creature:<world>:<persistent_id>:<symbol>[:script_id].
  std::string_view rest = text.substr(prefix.size());
  const auto first = rest.find(':');
  if(first == std::string_view::npos)
    return out;
  const auto second = rest.find(':', first + 1);
  if(second == std::string_view::npos)
    return out;
  out.world = std::string(rest.substr(0, first));
  if(auto pid = parseI64Segment(rest.substr(first + 1, second - first - 1)))
    out.persistentId = *pid;
  const auto third = rest.find(':', second + 1);
  const auto symbolEnd = third == std::string_view::npos ? rest.size() : third;
  if(auto sym = parseI64Segment(rest.substr(second + 1, symbolEnd - second - 1)))
    out.symbol = *sym;
  return out;
}

[[nodiscard]] std::string worldNpcFallbackRawKey(const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  return optionalJsonString(payload, "target_world_entity_key",
         optionalJsonString(payload, "target_npc_entity_key",
         optionalJsonString(payload, "npc_entity_key",
         optionalJsonString(payload, "target_key", packet.targetKey))));
}

[[nodiscard]] ResolvedWorldNpcEntity resolveWorldNpcEntityKey(const MySqlTarget& target,
                                                             std::string_view sessionUuid,
                                                             const Mmo::Net::ClientActionPacket& packet,
                                                             std::string rawKey) {
  const std::string_view payload = packet.payloadJson;
  if(rawKey.empty())
    rawKey = worldNpcFallbackRawKey(packet);

  auto identity = parseWorldNpcIdentity(std::move(rawKey));
  fillWorldNpcIdentityFromPayload(identity, payload);

  const std::string exactSql = sqlLiteral(identity.exact);
  const bool hasStableIdentity = !identity.world.empty() && identity.persistentId >= 0 && identity.symbol >= 0;
  const bool hasPidSym = identity.persistentId >= 0 && identity.symbol >= 0;
  const std::string canonicalHookKey = hasStableIdentity ?
    Mmo::Server::Identity::canonicalNpcHookKey(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string canonicalCreatureKey = hasStableIdentity ?
    Mmo::Server::Identity::canonicalCreatureHookKey(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string worldPidSymLike = hasStableIdentity ?
    Mmo::Server::Identity::canonicalNpcLegacyLike(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string creaturePidSymLike = hasStableIdentity ?
    Mmo::Server::Identity::canonicalCreatureLegacyLike(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string anyWorldHookLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldNpcHookLike(identity.persistentId, identity.symbol) : std::string();
  const std::string anyWorldCreatureLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldCreatureHookLike(identity.persistentId, identity.symbol) : std::string();
  const std::string anyWorldPidSymLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldNpcLegacyLike(identity.persistentId, identity.symbol) : std::string();
  const std::string anyWorldCreaturePidSymLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldCreatureLegacyLike(identity.persistentId, identity.symbol) : std::string();

  std::string where = "wes.entity_key=" + exactSql;
  if(!canonicalHookKey.empty())
    where += " OR wes.entity_key=" + sqlLiteral(canonicalHookKey);
  if(!canonicalCreatureKey.empty())
    where += " OR wes.entity_key=" + sqlLiteral(canonicalCreatureKey);
  if(!worldPidSymLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(worldPidSymLike);
  if(!creaturePidSymLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(creaturePidSymLike);
  if(!anyWorldHookLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyWorldHookLike);
  if(!anyWorldCreatureLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyWorldCreatureLike);
  if(!anyWorldPidSymLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyWorldPidSymLike);
  if(!anyWorldCreaturePidSymLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyWorldCreaturePidSymLike);
  if(!identity.exact.empty()) {
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.creature_spawn_key'))=" + exactSql;
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.entity_key'))=" + exactSql;
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_entity_key'))=" + exactSql;
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.source_entity_key'))=" + exactSql;
  }
  if(identity.persistentId >= 0 && identity.symbol >= 0) {
    where += " OR (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.source_persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.target_npc_persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += ") AND (";
    where += "CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR cet.symbol_index=";
    where += std::to_string(identity.symbol);
    where += " OR cet.script_id=";
    where += std::to_string(identity.symbol);
    where += ")";
    where += " OR (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += " AND (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += "))";
  } else if(identity.persistentId >= 0) {
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
  } else if(identity.symbol >= 0) {
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_symbol')) AS SIGNED)=";
    where += std::to_string(identity.symbol);
    where += " OR cet.symbol_index=";
    where += std::to_string(identity.symbol);
    where += " OR cet.script_id=";
    where += std::to_string(identity.symbol);
  }

  std::string query;
  query += "SELECT wes.entity_key, wes.lifecycle_state, COALESCE(wes.row_version,0) ";
  query += "FROM world_entity_state wes ";
  query += "LEFT JOIN content_entity_templates cet ON cet.entity_template_id=wes.entity_template_id ";
  query += "JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND wes.entity_kind IN ('npc','creature') AND (" + where + ") ";
  query += "ORDER BY CASE WHEN wes.entity_key=" + exactSql + " THEN 0 ";
  if(!canonicalHookKey.empty())
    query += "WHEN wes.entity_key=" + sqlLiteral(canonicalHookKey) + " THEN 1 ";
  if(!canonicalCreatureKey.empty())
    query += "WHEN wes.entity_key=" + sqlLiteral(canonicalCreatureKey) + " THEN 2 ";
  if(!worldPidSymLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(worldPidSymLike) + " THEN 3 ";
  if(!creaturePidSymLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(creaturePidSymLike) + " THEN 4 ";
  if(!anyWorldHookLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyWorldHookLike) + " THEN 5 ";
  if(!anyWorldCreatureLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyWorldCreatureLike) + " THEN 6 ";
  if(!anyWorldPidSymLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyWorldPidSymLike) + " THEN 7 ";
  if(!anyWorldCreaturePidSymLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyWorldCreaturePidSymLike) + " THEN 8 ";
  query += "ELSE 9 END, CASE WHEN wes.lifecycle_state='active' THEN 0 ELSE 1 END, wes.updated_at DESC LIMIT 1;";

  auto parts = splitMysqlLastRow(runMysql(target, query));

  if((parts.empty() || parts.front().empty()) && identity.symbol >= 0) {
    auto pos = optionalJsonVec3(payload, "target_position");
    if(!pos)
      pos = optionalJsonVec3(payload, "source_npc_position");
    if(!pos)
      pos = optionalJsonVec3(payload, "npc_position");
    if(!pos)
      pos = optionalJsonVec3(payload, "source_position");
    if(pos) {
      std::string fuzzy;
      fuzzy += "SELECT wes.entity_key, wes.lifecycle_state, COALESCE(wes.row_version,0) ";
      fuzzy += "FROM world_entity_state wes ";
      fuzzy += "LEFT JOIN content_entity_templates cet ON cet.entity_template_id=wes.entity_template_id ";
      fuzzy += "JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id ";
      fuzzy += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
      fuzzy += "AND wes.entity_kind IN ('npc','creature') AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL ";
      fuzzy += "AND (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=" + std::to_string(identity.symbol);
      fuzzy += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED)=" + std::to_string(identity.symbol);
      fuzzy += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)=" + std::to_string(identity.symbol);
      fuzzy += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_symbol')) AS SIGNED)=" + std::to_string(identity.symbol);
      fuzzy += " OR cet.symbol_index=" + std::to_string(identity.symbol);
      fuzzy += " OR cet.script_id=" + std::to_string(identity.symbol);
      fuzzy += " OR wes.entity_key LIKE " + sqlLiteral("%:sym:" + std::to_string(identity.symbol) + "%");
      fuzzy += " OR wes.entity_key LIKE " + sqlLiteral("%:" + std::to_string(identity.symbol) + ":%");
      fuzzy += ") ";
      fuzzy += "AND ((wes.pos_x-(" + std::to_string(pos->x) + "))*(wes.pos_x-(" + std::to_string(pos->x) + ")) + ";
      fuzzy += "(wes.pos_y-(" + std::to_string(pos->y) + "))*(wes.pos_y-(" + std::to_string(pos->y) + ")) + ";
      fuzzy += "(wes.pos_z-(" + std::to_string(pos->z) + "))*(wes.pos_z-(" + std::to_string(pos->z) + "))) <= 100000000.0 ";
      fuzzy += "ORDER BY ((wes.pos_x-(" + std::to_string(pos->x) + "))*(wes.pos_x-(" + std::to_string(pos->x) + ")) + ";
      fuzzy += "(wes.pos_y-(" + std::to_string(pos->y) + "))*(wes.pos_y-(" + std::to_string(pos->y) + ")) + ";
      fuzzy += "(wes.pos_z-(" + std::to_string(pos->z) + "))*(wes.pos_z-(" + std::to_string(pos->z) + "))) ASC, ";
      fuzzy += "CASE WHEN wes.lifecycle_state='active' THEN 0 ELSE 1 END, wes.updated_at DESC LIMIT 1;";
      parts = splitMysqlLastRow(runMysql(target, fuzzy));
    }
  }

  if(parts.empty() || parts.front().empty()) {
    throw std::runtime_error("world NPC entity could not be resolved: key=" + identity.exact +
                             " world=" + identity.world +
                             " pid=" + std::to_string(identity.persistentId) +
                             " sym=" + std::to_string(identity.symbol));
  }

  ResolvedWorldNpcEntity out;
  out.entityKey = parts[0];
  if(parts.size() > 1)
    out.lifecycleState = parts[1];
  if(parts.size() > 2)
    out.rowVersion = parseI64(parts[2]).value_or(0);
  return out;
}

[[nodiscard]] std::optional<JsonVec3> worldNpcPositionFromPayload(std::string_view payload) {
  auto pos = optionalJsonVec3(payload, "target_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "source_npc_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "npc_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "source_position");
  return pos;
}

[[nodiscard]] std::string stableObservedWorldNpcKey(const WorldNpcIdentity& identity) {
  if(Mmo::Server::Identity::startsWith(identity.exact, Mmo::Server::Identity::NpcHookPrefix) ||
     Mmo::Server::Identity::startsWith(identity.exact, Mmo::Server::Identity::CreatureHookPrefix))
    return identity.exact;
  if(identity.world.empty() || identity.persistentId < 0 || identity.symbol < 0)
    return {};
  return Mmo::Server::Identity::canonicalNpcHookKey(identity.world, identity.persistentId, identity.symbol);
}

[[nodiscard]] ResolvedWorldNpcEntity materializeObservedWorldNpcEntity(const MySqlTarget& target,
                                                                      std::string_view sessionUuid,
                                                                      const Mmo::Net::ClientActionPacket& packet,
                                                                      std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  auto identity = parseWorldNpcIdentity(worldNpcFallbackRawKey(packet));
  fillWorldNpcIdentityFromPayload(identity, payload);
  const auto entityKey = stableObservedWorldNpcKey(identity);
  if(entityKey.empty() || identity.persistentId < 0 || identity.symbol < 0)
    throw std::runtime_error("observed world NPC cannot be materialized without stable pid/symbol identity");

  const auto pos = worldNpcPositionFromPayload(payload);
  const std::int64_t valueBefore = optionalJsonI64(payload, "value_before", -1);
  const std::int64_t valueAfter = optionalJsonI64(payload, "value_after", -1);
  const std::int64_t rawDamage = optionalJsonI64(payload, "damage_amount",
                                optionalJsonI64(payload, "amount",
                                optionalJsonI64(payload, "delta", 0)));
  const std::int64_t damage = rawDamage < 0 ? -rawDamage : rawDamage;
  std::int64_t healthMax = optionalJsonI64(payload, "health_max",
                           optionalJsonI64(payload, "target_npc_health_max",
                           optionalJsonI64(payload, "max_hitpoints", -1)));
  if(healthMax < 0)
    healthMax = std::max<std::int64_t>(1, std::max(valueBefore, std::max(valueAfter, damage)));
  std::int64_t healthCurrent = valueBefore >= 0 ? valueBefore : healthMax;
  healthCurrent = std::max<std::int64_t>(0, std::min(healthCurrent, healthMax));

  const auto displayName = optionalJsonString(payload, "target_npc_display_name",
                           optionalJsonString(payload, "source_npc_display_name",
                           optionalJsonString(payload, "npc_display_name")));
  const auto tick = packetServerTick(packet);

  std::string sql;
  sql += "SET @realm_id=NULL; SET @world_id=NULL; SET @character_id=NULL;";
  sql += "SET @content_revision_id=NULL; SET @template_id=NULL; SET @entity_kind=NULL; SET @event_id=NULL;";
  sql += "SELECT ss.realm_id,ss.world_instance_id,ss.character_id,rr.active_content_revision_id ";
  sql += "INTO @realm_id,@world_id,@character_id,@content_revision_id ";
  sql += "FROM server_sessions ss JOIN realm_realms rr ON rr.realm_id=ss.realm_id ";
  sql += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) AND ss.lifecycle_state='active' LIMIT 1;";
  sql += "SELECT cet.entity_template_id,cet.entity_kind INTO @template_id,@entity_kind ";
  sql += "FROM content_entity_templates cet WHERE cet.content_revision_id=@content_revision_id ";
  sql += "AND cet.entity_kind IN ('creature','npc') AND (cet.symbol_index=" + std::to_string(identity.symbol);
  sql += " OR cet.script_id=" + std::to_string(identity.symbol);
  sql += " OR cet.engine_template_key=" + sqlLiteral("creature-symbol:" + std::to_string(identity.symbol));
  sql += " OR cet.engine_template_key=" + sqlLiteral("npc-symbol:" + std::to_string(identity.symbol));
  sql += " OR cet.engine_template_key LIKE " + sqlLiteral("%:" + std::to_string(identity.symbol)) + ") ";
  sql += "ORDER BY CASE WHEN cet.entity_kind='creature' THEN 0 ELSE 1 END,cet.engine_template_key LIMIT 1;";
  sql += "INSERT INTO world_entity_state(";
  sql += "world_instance_id,entity_key,entity_kind,entity_template_id,lifecycle_state,pos_x,pos_y,pos_z,rotation_yaw,health_current,health_max,state_json,row_version";
  sql += ") VALUES(@world_id,";
  sql += sqlLiteral(entityKey) + ",COALESCE(@entity_kind,'creature'),@template_id,'active',";
  sql += (pos ? std::to_string(pos->x) : "NULL");
  sql += ",";
  sql += (pos ? std::to_string(pos->y) : "NULL");
  sql += ",";
  sql += (pos ? std::to_string(pos->z) : "NULL");
  sql += ",NULL,";
  sql += std::to_string(healthCurrent) + "," + std::to_string(healthMax) + ",";
  sql += "JSON_OBJECT(";
  sql += "'observed_runtime_entity',true,";
  sql += "'observed_from','mmo_udp_server_cpp',";
  sql += "'observed_at_tick'," + std::to_string(tick) + ",";
  sql += "'world'," + sqlLiteral(identity.world) + ",";
  sql += "'persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'symbol_index'," + std::to_string(identity.symbol) + ",";
  sql += "'entity_key'," + sqlLiteral(entityKey) + ",";
  sql += "'display_name'," + sqlLiteral(displayName) + ",";
  sql += "'last_payload'," + sqlJson(dbPayload);
  sql += "),1) ";
  sql += "ON DUPLICATE KEY UPDATE ";
  sql += "entity_template_id=COALESCE(entity_template_id,VALUES(entity_template_id)),";
  sql += "pos_x=COALESCE(VALUES(pos_x),pos_x),pos_y=COALESCE(VALUES(pos_y),pos_y),pos_z=COALESCE(VALUES(pos_z),pos_z),";
  sql += "health_max=GREATEST(COALESCE(health_max,0),VALUES(health_max)),";
  sql += "health_current=COALESCE(health_current,VALUES(health_current)),";
  sql += "state_json=JSON_MERGE_PATCH(COALESCE(state_json,JSON_OBJECT()),JSON_OBJECT(";
  sql += "'observed_runtime_entity',true,'last_observed_tick'," + std::to_string(tick) + ",";
  sql += "'persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'symbol_index'," + std::to_string(identity.symbol) + ",";
  sql += "'display_name'," + sqlLiteral(displayName);
  sql += ")),row_version=row_version+1,updated_at=CURRENT_TIMESTAMP(6);";
  sql += "CALL mmo_append_world_event(@realm_id,@world_id,@character_id,";
  sql += "'world_npc_observed','world_entity'," + std::to_string(tick) + ",";
  sql += sqlLiteral(entityKey) + "," + sqlLiteral(entityKey) + ",";
  sql += "JSON_OBJECT('entity_key'," + sqlLiteral(entityKey);
  sql += ",'persistent_id'," + std::to_string(identity.persistentId);
  sql += ",'symbol_index'," + std::to_string(identity.symbol);
  sql += ",'display_name'," + sqlLiteral(displayName);
  sql += ",'metadata'," + sqlJson(dbPayload) + "),";
  sql += sqlLiteral(packet.idempotencyKey + ":observed-npc") + ",'server',NULL,NULL,@event_id);";
  (void)runMysql(target, sql);
  std::cerr << "[observed_world_npc_materialized] entity=" << entityKey
            << " pid=" << identity.persistentId
            << " sym=" << identity.symbol
            << " display=" << displayName
            << "\n";
  return resolveWorldNpcEntityKey(target, sessionUuid, packet, entityKey);
}

[[nodiscard]] ResolvedWorldNpcEntity resolveTargetWorldNpcEntityKey(const MySqlTarget& target,
                                                                   std::string_view sessionUuid,
                                                                   const Mmo::Net::ClientActionPacket& packet) {
  return resolveWorldNpcEntityKey(target, sessionUuid, packet, worldNpcFallbackRawKey(packet));
}

[[nodiscard]] std::string resolveWorldInventoryOwnerEntityKey(const MySqlTarget& target,
                                                              std::string_view sessionUuid,
                                                              const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  const auto raw = optionalJsonString(payload, "source_npc_entity_key",
                   optionalJsonString(payload, "source_entity_key",
                   optionalJsonString(payload, "source_container_key",
                   optionalJsonString(payload, "container_key",
                   optionalJsonString(payload, "owner_entity_key",
                   optionalJsonString(payload, "source_npc_key",
                   optionalJsonString(payload, "source_actor_key", packet.targetKey)))))));
  if(Mmo::Server::Identity::looksLikeNpcKey(raw))
    return resolveWorldNpcEntityKey(target, sessionUuid, packet, raw).entityKey;
  return raw;
}

[[nodiscard]] std::int64_t damageAmountFromPayload(std::string_view payload) noexcept {
  const auto amount = optionalJsonI64(payload, "damage_amount",
                      optionalJsonI64(payload, "amount",
                      optionalJsonI64(payload, "delta", 0)));
  if(amount == std::numeric_limits<std::int64_t>::min())
    return std::numeric_limits<std::int64_t>::max();
  return amount < 0 ? -amount : amount;
}

[[nodiscard]] std::string resolveWorldItemEntityKey(const MySqlTarget& target,
                                                    std::string_view sessionUuid,
                                                    const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  auto identity = parseWorldItemIdentity(optionalJsonString(payload, "world_item_entity_key",
                                      optionalJsonString(payload, "engine_world_item_key",
                                      optionalJsonString(payload, "target_key", packet.targetKey))));
  if(identity.persistentId < 0)
    identity.persistentId = optionalJsonI64(payload, "source_world_item_persistent_id",
                            optionalJsonI64(payload, "item_persistent_id", -1));
  if(identity.symbol < 0)
    identity.symbol = optionalJsonI64(payload, "item_symbol",
                      optionalJsonI64(payload, "inventory_item_symbol",
                      optionalJsonI64(payload, "item_template_symbol", -1)));
  if(identity.world.empty())
    identity.world = optionalJsonString(payload, "world");

  const bool hasStableIdentity = !identity.world.empty() && identity.persistentId >= 0 && identity.symbol >= 0;
  const bool hasPidSym = identity.persistentId >= 0 && identity.symbol >= 0;
  const std::string dbLike = hasStableIdentity ?
    Mmo::Server::Identity::canonicalWorldItemDbLike(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string hookKey = hasStableIdentity ?
    Mmo::Server::Identity::canonicalWorldItemHookKey(identity.world, identity.persistentId, identity.symbol) : std::string();
  const std::string anyHookLike = hasPidSym ?
    Mmo::Server::Identity::anyWorldItemHookLike(identity.persistentId, identity.symbol) : std::string();

  std::string where = "wes.entity_key=" + sqlLiteral(identity.exact);
  if(!dbLike.empty()) {
    where += " OR wes.entity_key LIKE ";
    where += sqlLiteral(dbLike);
  }
  if(!hookKey.empty())
    where += " OR wes.entity_key=" + sqlLiteral(hookKey);
  if(!anyHookLike.empty())
    where += " OR wes.entity_key LIKE " + sqlLiteral(anyHookLike);
  if(identity.persistentId >= 0) {
    where += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED)=";
    where += std::to_string(identity.persistentId);
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_spawn_key')) LIKE ";
    where += sqlLiteral("%:" + std::to_string(identity.persistentId) + ":%");
    where += " OR wes.entity_key LIKE ";
    where += sqlLiteral("%:pid:" + std::to_string(identity.persistentId) + ":%");
  }
  if(!identity.exact.empty()) {
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_spawn_key'))=";
    where += sqlLiteral(identity.exact);
    where += " OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.entity_key'))=";
    where += sqlLiteral(identity.exact);
  }

  std::string query;
  query += "SELECT wes.entity_key FROM world_entity_state wes ";
  query += "JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND wes.entity_kind='item' AND wes.lifecycle_state='active' AND (";
  query += where;
  query += ")";
  if(identity.symbol >= 0) {
    query += " AND (CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED)=";
    query += std::to_string(identity.symbol);
    query += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)=";
    query += std::to_string(identity.symbol);
    query += " OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_symbol')) AS SIGNED)=";
    query += std::to_string(identity.symbol);
    query += " OR wes.entity_key LIKE ";
    query += sqlLiteral("%:sym:" + std::to_string(identity.symbol) + "%");
    query += " OR wes.entity_key LIKE ";
    query += sqlLiteral("%:" + std::to_string(identity.symbol) + ":%");
    query += ")";
  }
  query += " ORDER BY CASE WHEN wes.entity_key=" + sqlLiteral(identity.exact) + " THEN 0 ";
  if(!hookKey.empty())
    query += "WHEN wes.entity_key=" + sqlLiteral(hookKey) + " THEN 1 ";
  if(!dbLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(dbLike) + " THEN 2 ";
  if(!anyHookLike.empty())
    query += "WHEN wes.entity_key LIKE " + sqlLiteral(anyHookLike) + " THEN 3 ";
  query += "ELSE 4 END, wes.updated_at DESC LIMIT 1;";
  auto out = mysqlSingleField(target, query);
  if(out.empty())
    throw std::runtime_error("world item could not be resolved: key=" + identity.exact +
                             " world=" + identity.world +
                             " pid=" + std::to_string(identity.persistentId) +
                             " sym=" + std::to_string(identity.symbol));
  return out;
}

[[nodiscard]] std::optional<JsonVec3> worldItemPositionFromPayload(std::string_view payload) {
  auto pos = optionalJsonVec3(payload, "item_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "world_item_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "target_position");
  if(!pos)
    pos = optionalJsonVec3(payload, "actor_position");
  return pos;
}

[[nodiscard]] std::string materializeObservedWorldItem(const MySqlTarget& target,
                                                      std::string_view sessionUuid,
                                                      const Mmo::Net::ClientActionPacket& packet,
                                                      std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  auto identity = parseWorldItemIdentity(optionalJsonString(payload, "world_item_entity_key",
                                      optionalJsonString(payload, "engine_world_item_key",
                                      optionalJsonString(payload, "target_key", packet.targetKey))));
  if(identity.persistentId < 0)
    identity.persistentId = optionalJsonI64(payload, "source_world_item_persistent_id",
                            optionalJsonI64(payload, "world_item_persistent_id",
                            optionalJsonI64(payload, "item_persistent_id", -1)));
  if(identity.symbol < 0)
    identity.symbol = optionalJsonI64(payload, "item_symbol",
                      optionalJsonI64(payload, "inventory_item_symbol",
                      optionalJsonI64(payload, "item_template_symbol", -1)));
  if(identity.symbol < 0) {
    const auto key = optionalJsonString(payload, "item_template_key");
    constexpr std::string_view Prefix = "item-template:";
    if(startsWith(key, Prefix))
      identity.symbol = parseI64(std::string_view(key).substr(Prefix.size())).value_or(-1);
  }
  if(identity.world.empty())
    identity.world = optionalJsonString(payload, "world");

  const auto entityKey = (!identity.world.empty() && identity.persistentId >= 0 && identity.symbol >= 0) ?
    Mmo::Server::Identity::canonicalWorldItemHookKey(identity.world, identity.persistentId, identity.symbol) :
    std::string(identity.exact);
  if(entityKey.empty() || identity.symbol < 0)
    throw std::runtime_error("observed world item cannot be materialized without stable key/symbol");

  const auto amount = std::max<std::int64_t>(1, optionalJsonI64(payload, "amount", 1));
  const auto tick = packetServerTick(packet);
  const auto pos = worldItemPositionFromPayload(payload);
  const std::string entitySql = sqlLiteral(entityKey);
  const std::string idem = packet.idempotencyKey + ":observed-world-item";

  std::string sql;
  sql += "SET @realm_id=NULL; SET @world_id=NULL; SET @character_id=NULL; SET @content_revision_id=NULL;";
  sql += "SET @template_id=NULL; SET @item_id=NULL; SET @item_key=NULL; SET @event_id=NULL;";
  sql += "SELECT ss.realm_id,ss.world_instance_id,ss.character_id,rr.active_content_revision_id ";
  sql += "INTO @realm_id,@world_id,@character_id,@content_revision_id ";
  sql += "FROM server_sessions ss JOIN realm_realms rr ON rr.realm_id=ss.realm_id ";
  sql += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) AND ss.lifecycle_state='active' LIMIT 1;";
  sql += "SELECT cit.item_template_id INTO @template_id FROM content_item_templates cit ";
  sql += "WHERE cit.content_revision_id=@content_revision_id AND cit.symbol_index=" + std::to_string(identity.symbol) + " ";
  sql += "ORDER BY cit.item_template_key LIMIT 1;";
  sql += "SET @item_key=LEFT(CONCAT('observed-world-item:',SHA2(" + sqlLiteral(idem) + ",256)),191);";
  sql += "SELECT ii.item_instance_id INTO @item_id FROM item_instances ii ";
  sql += "WHERE ii.realm_id=@realm_id AND ii.item_instance_key=@item_key LIMIT 1;";
  sql += "INSERT INTO item_instances(";
  sql += "item_instance_id,realm_id,item_template_id,item_instance_key,owner_type,owner_id,quantity,bind_state,lifecycle_state,raw_payload";
  sql += ") SELECT UUID_TO_BIN(UUID(),1),@realm_id,@template_id,@item_key,'world_entity',NULL,";
  sql += std::to_string(amount) + ",'unbound','active',JSON_OBJECT(";
  sql += "'observed_world_item',true,";
  sql += "'observed_from','mmo_udp_server_cpp',";
  sql += "'entity_key'," + entitySql + ",";
  sql += "'item_spawn_key'," + entitySql + ",";
  sql += "'persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'source_world_item_persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'item_symbol'," + std::to_string(identity.symbol) + ",";
  sql += "'symbol_index'," + std::to_string(identity.symbol) + ",";
  sql += "'item_template_symbol'," + std::to_string(identity.symbol) + ",";
  sql += "'amount'," + std::to_string(amount) + ",";
  sql += "'observed_at_tick'," + std::to_string(tick) + ",";
  sql += "'metadata'," + sqlJson(dbPayload);
  sql += ") WHERE @realm_id IS NOT NULL AND @template_id IS NOT NULL AND @item_id IS NULL;";
  sql += "SELECT ii.item_instance_id INTO @item_id FROM item_instances ii ";
  sql += "WHERE ii.realm_id=@realm_id AND ii.item_instance_key=@item_key LIMIT 1;";
  sql += "INSERT INTO world_entity_state(";
  sql += "world_instance_id,entity_key,entity_kind,lifecycle_state,pos_x,pos_y,pos_z,state_json,row_version";
  sql += ") SELECT @world_id," + entitySql + ",'item','active',";
  sql += (pos ? std::to_string(pos->x) : "NULL");
  sql += ",";
  sql += (pos ? std::to_string(pos->y) : "NULL");
  sql += ",";
  sql += (pos ? std::to_string(pos->z) : "NULL");
  sql += ",JSON_OBJECT(";
  sql += "'exists_in_world',true,";
  sql += "'observed_world_item',true,";
  sql += "'item_instance_id',BIN_TO_UUID(@item_id,1),";
  sql += "'entity_key'," + entitySql + ",";
  sql += "'item_spawn_key'," + entitySql + ",";
  sql += "'persistent_id'," + std::to_string(identity.persistentId) + ",";
  sql += "'item_symbol'," + std::to_string(identity.symbol) + ",";
  sql += "'symbol_index'," + std::to_string(identity.symbol) + ",";
  sql += "'amount'," + std::to_string(amount) + ",";
  sql += "'observed_at_tick'," + std::to_string(tick);
  sql += "),1 WHERE @world_id IS NOT NULL AND @item_id IS NOT NULL ";
  sql += "ON DUPLICATE KEY UPDATE ";
  sql += "entity_kind='item',lifecycle_state='active',";
  sql += "pos_x=COALESCE(VALUES(pos_x),pos_x),pos_y=COALESCE(VALUES(pos_y),pos_y),pos_z=COALESCE(VALUES(pos_z),pos_z),";
  sql += "state_json=JSON_MERGE_PATCH(COALESCE(state_json,JSON_OBJECT()),VALUES(state_json)),";
  sql += "row_version=COALESCE(row_version,0)+1,updated_at=CURRENT_TIMESTAMP(6);";
  sql += "INSERT INTO world_inventory(world_instance_id,owner_entity_key,item_instance_id,amount,source_amount,source_iterator_count) ";
  sql += "SELECT @world_id," + entitySql + ",@item_id," + std::to_string(amount) + ",";
  sql += std::to_string(amount) + "," + std::to_string(amount) + " ";
  sql += "WHERE @world_id IS NOT NULL AND @item_id IS NOT NULL ";
  sql += "ON DUPLICATE KEY UPDATE ";
  sql += "amount=GREATEST(world_inventory.amount,VALUES(amount)),";
  sql += "source_amount=GREATEST(COALESCE(world_inventory.source_amount,0),VALUES(source_amount)),";
  sql += "source_iterator_count=GREATEST(COALESCE(world_inventory.source_iterator_count,0),VALUES(source_iterator_count)),";
  sql += "updated_at=CURRENT_TIMESTAMP(6);";
  sql += "CALL mmo_append_world_event(@realm_id,@world_id,@character_id,";
  sql += "'world_item_observed','world_entity'," + std::to_string(tick) + ",";
  sql += entitySql + ",@item_key,";
  sql += "JSON_OBJECT('world_item_entity_key'," + entitySql;
  sql += ",'item_instance_id',BIN_TO_UUID(@item_id,1)";
  sql += ",'item_symbol'," + std::to_string(identity.symbol);
  sql += ",'persistent_id'," + std::to_string(identity.persistentId);
  sql += ",'amount'," + std::to_string(amount);
  sql += ",'metadata'," + sqlJson(dbPayload) + "),";
  sql += sqlLiteral(idem) + ",'server',NULL,NULL,@event_id);";
  sql += "SELECT " + entitySql + ";";

  auto out = mysqlSingleField(target, sql);
  if(out.empty())
    throw std::runtime_error("observed world item materialization failed: key=" + entityKey +
                             " symbol=" + std::to_string(identity.symbol));

  std::cerr << "[observed_world_item_materialized] entity=" << out
            << " symbol=" << identity.symbol
            << " amount=" << amount
            << "\n";
  return out;
}

[[nodiscard]] int nextBagIndex(const MySqlTarget& target, std::string_view sessionUuid) {
  std::string sql;
  sql += "SELECT COALESCE(MAX(ci.bag_index), -1) + 1 ";
  sql += "FROM character_inventory ci ";
  sql += "JOIN server_sessions ss ON ss.character_id=ci.character_id ";
  sql += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1);";
  auto value = parseInt(mysqlSingleField(target, sql));
  return value.value_or(0);
}

constexpr std::int64_t InvalidGothicPersistentId = 4294967295LL;

[[nodiscard]] bool isUsablePersistentId(std::int64_t value) noexcept {
  return value >= 0 && value != InvalidGothicPersistentId;
}

[[nodiscard]] std::int64_t itemSymbolFromPayload(std::string_view payload) noexcept {
  const auto explicitSymbol = optionalJsonI64(payload, "item_symbol",
                              optionalJsonI64(payload, "inventory_item_symbol",
                              optionalJsonI64(payload, "item_template_symbol", -1)));
  if(explicitSymbol >= 0)
    return explicitSymbol;

  const auto key = optionalJsonString(payload, "item_template_key");
  constexpr std::string_view Prefix = "item-template:";
  if(!startsWith(key, Prefix))
    return -1;
  return parseI64(std::string_view(key).substr(Prefix.size())).value_or(-1);
}

[[nodiscard]] std::int64_t itemPersistentIdFromPayload(std::string_view payload) noexcept {
  const auto value = optionalJsonI64(payload, "item_instance_persistent_id",
                     optionalJsonI64(payload, "source_item_persistent_id",
                     optionalJsonI64(payload, "source_world_item_persistent_id",
                     optionalJsonI64(payload, "item_persistent_id", -1))));
  return isUsablePersistentId(value) ? value : -1;
}

[[nodiscard]] std::string resolveNpcInventoryItemUuid(const MySqlTarget& target,
                                                     std::string_view sessionUuid,
                                                     std::string_view sourceNpcKey,
                                                     const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  if(auto id = jsonStringField(payload, "item_instance_id"); id && !id->empty())
    return *id;
  if(auto id = jsonStringField(payload, "item_instance_uuid"); id && !id->empty())
    return *id;

  const auto symbol = itemSymbolFromPayload(payload);
  const auto pid = itemPersistentIdFromPayload(payload);
  if(sourceNpcKey.empty())
    throw std::runtime_error("source_entity_key is required to resolve world inventory item");
  if(symbol < 0)
    throw std::runtime_error("item_symbol is required to resolve world inventory item");

  std::string query;
  query += "SELECT BIN_TO_UUID(ii.item_instance_id,1) ";
  query += "FROM world_inventory wi ";
  query += "JOIN item_instances ii ON ii.item_instance_id=wi.item_instance_id ";
  query += "JOIN content_item_templates it ON it.item_template_id=ii.item_template_id ";
  query += "JOIN server_sessions ss ON ss.world_instance_id=wi.world_instance_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND wi.owner_entity_key=" + sqlLiteral(sourceNpcKey) + " ";
  query += "AND ii.lifecycle_state='active' AND it.symbol_index=" + std::to_string(symbol) + " ";
  if(isUsablePersistentId(pid)) {
    query += "AND (JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR ii.item_instance_key LIKE " + sqlLiteral("%:" + std::to_string(symbol) + ":" + std::to_string(pid) + "%");
    query += " OR ii.item_instance_key LIKE " + sqlLiteral("%:" + std::to_string(pid) + ":" + std::to_string(symbol) + "%") + ") ";
  }
  query += "ORDER BY wi.amount DESC, ii.item_instance_key ASC LIMIT 1;";

  auto out = mysqlSingleField(target, query);
  if(out.empty())
    throw std::runtime_error("world inventory item could not be resolved: owner=" + std::string(sourceNpcKey) +
                             " pid=" + std::to_string(pid) + " sym=" + std::to_string(symbol));
  return out;
}

[[nodiscard]] std::string materializeObservedNpcLootItem(const MySqlTarget& target,
                                                        std::string_view sessionUuid,
                                                        std::string_view sourceNpcKey,
                                                        const Mmo::Net::ClientActionPacket& packet,
                                                        std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const auto symbol = itemSymbolFromPayload(payload);
  if(sourceNpcKey.empty())
    throw std::runtime_error("observed NPC loot cannot be materialized without source entity key");
  if(symbol < 0)
    throw std::runtime_error("observed NPC loot cannot be materialized without item symbol");

  const auto amount = std::max<std::int64_t>(1, optionalJsonI64(payload, "amount", 1));
  const auto pid = itemPersistentIdFromPayload(payload);
  const auto tick = packetServerTick(packet);
  const std::string sourceNpcSql = sqlLiteral(sourceNpcKey);
  const std::string idem = packet.idempotencyKey + ":observed-npc-loot";

  std::string sql;
  sql += "SET @realm_id=NULL; SET @world_id=NULL; SET @character_id=NULL; SET @content_revision_id=NULL;";
  sql += "SET @template_id=NULL; SET @item_id=NULL; SET @item_key=NULL; SET @event_id=NULL;";
  sql += "SELECT ss.realm_id,ss.world_instance_id,ss.character_id,rr.active_content_revision_id ";
  sql += "INTO @realm_id,@world_id,@character_id,@content_revision_id ";
  sql += "FROM server_sessions ss JOIN realm_realms rr ON rr.realm_id=ss.realm_id ";
  sql += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) AND ss.lifecycle_state='active' LIMIT 1;";
  sql += "SELECT cit.item_template_id INTO @template_id FROM content_item_templates cit ";
  sql += "WHERE cit.content_revision_id=@content_revision_id AND cit.symbol_index=" + std::to_string(symbol) + " ";
  sql += "ORDER BY cit.item_template_key LIMIT 1;";
  sql += "SET @item_key=LEFT(CONCAT('observed-corpse-loot:',SHA2(" + sqlLiteral(idem) + ",256)),191);";
  sql += "SELECT ii.item_instance_id INTO @item_id FROM item_instances ii ";
  sql += "WHERE ii.realm_id=@realm_id AND ii.item_instance_key=@item_key LIMIT 1;";
  sql += "INSERT INTO item_instances(";
  sql += "item_instance_id,realm_id,item_template_id,item_instance_key,owner_type,owner_id,quantity,bind_state,lifecycle_state,raw_payload";
  sql += ") SELECT UUID_TO_BIN(UUID(),1),@realm_id,@template_id,@item_key,'world_entity',NULL,";
  sql += std::to_string(amount) + ",'unbound','active',JSON_OBJECT(";
  sql += "'observed_corpse_loot',true,";
  sql += "'observed_from','mmo_udp_server_cpp',";
  sql += "'source_entity_key'," + sourceNpcSql + ",";
  sql += "'item_symbol'," + std::to_string(symbol) + ",";
  sql += "'persistent_id'," + std::to_string(pid) + ",";
  sql += "'source_item_persistent_id'," + std::to_string(pid) + ",";
  sql += "'amount'," + std::to_string(amount) + ",";
  sql += "'observed_at_tick'," + std::to_string(tick) + ",";
  sql += "'metadata'," + sqlJson(dbPayload);
  sql += ") WHERE @realm_id IS NOT NULL AND @template_id IS NOT NULL AND @item_id IS NULL;";
  sql += "SELECT ii.item_instance_id INTO @item_id FROM item_instances ii ";
  sql += "WHERE ii.realm_id=@realm_id AND ii.item_instance_key=@item_key LIMIT 1;";
  sql += "INSERT INTO world_inventory(world_instance_id,owner_entity_key,item_instance_id,amount,source_amount,source_iterator_count) ";
  sql += "SELECT @world_id," + sourceNpcSql + ",@item_id," + std::to_string(amount) + ",";
  sql += std::to_string(amount) + "," + std::to_string(amount) + " ";
  sql += "WHERE @world_id IS NOT NULL AND @item_id IS NOT NULL ";
  sql += "ON DUPLICATE KEY UPDATE ";
  sql += "amount=GREATEST(world_inventory.amount,VALUES(amount)),";
  sql += "source_amount=GREATEST(COALESCE(world_inventory.source_amount,0),VALUES(source_amount)),";
  sql += "source_iterator_count=GREATEST(COALESCE(world_inventory.source_iterator_count,0),VALUES(source_iterator_count)),";
  sql += "updated_at=CURRENT_TIMESTAMP(6);";
  sql += "CALL mmo_append_world_event(@realm_id,@world_id,@character_id,";
  sql += "'world_npc_loot_observed','inventory'," + std::to_string(tick) + ",";
  sql += sourceNpcSql + ",@item_key,";
  sql += "JSON_OBJECT('source_entity_key'," + sourceNpcSql;
  sql += ",'item_instance_id',BIN_TO_UUID(@item_id,1)";
  sql += ",'item_symbol'," + std::to_string(symbol);
  sql += ",'persistent_id'," + std::to_string(pid);
  sql += ",'amount'," + std::to_string(amount);
  sql += ",'metadata'," + sqlJson(dbPayload) + "),";
  sql += sqlLiteral(idem) + ",'server',NULL,NULL,@event_id);";
  sql += "SELECT BIN_TO_UUID(@item_id,1);";

  auto out = mysqlSingleField(target, sql);
  if(out.empty())
    throw std::runtime_error("observed NPC loot materialization failed: owner=" + std::string(sourceNpcKey) +
                             " symbol=" + std::to_string(symbol));

  std::cerr << "[observed_npc_loot_materialized] owner=" << sourceNpcKey
            << " symbol=" << symbol
            << " amount=" << amount
            << " item=" << out
            << "\n";
  return out;
}

[[nodiscard]] std::string resolveCharacterItemUuid(const MySqlTarget& target,
                                                   std::string_view sessionUuid,
                                                   const Mmo::Net::ClientActionPacket& packet) {
  const std::string_view payload = packet.payloadJson;
  if(auto id = jsonStringField(payload, "item_instance_id"); id && !id->empty())
    return *id;
  if(auto id = jsonStringField(payload, "item_instance_uuid"); id && !id->empty())
    return *id;

  const auto symbol = itemSymbolFromPayload(payload);
  const auto pid = itemPersistentIdFromPayload(payload);
  const auto slot = normalizedEquipmentSlot(payload);
  if(symbol < 0)
    throw std::runtime_error("item_symbol or item_template_key is required to resolve character item");

  std::string query;
  query += "SELECT BIN_TO_UUID(ii.item_instance_id,1) ";
  query += "FROM item_instances ii ";
  query += "JOIN character_inventory ci ON ci.item_instance_id=ii.item_instance_id ";
  query += "JOIN content_item_templates it ON it.item_template_id=ii.item_template_id ";
  query += "JOIN server_sessions ss ON ss.character_id=ci.character_id ";
  query += "LEFT JOIN character_equipment ce ON ce.character_id=ci.character_id AND ce.item_instance_id=ii.item_instance_id ";
  query += "WHERE ss.session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  query += "AND ii.owner_type='character' AND ii.lifecycle_state='active' ";
  query += "AND it.symbol_index=" + std::to_string(symbol) + " ";
  if(isUsablePersistentId(pid)) {
    query += "AND (JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_world_item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_persistent_id'))=" + sqlLiteral(std::to_string(pid));
    query += " OR ii.item_instance_key LIKE " + sqlLiteral("%:" + std::to_string(symbol) + ":" + std::to_string(pid) + ":%");
    query += " OR ii.item_instance_key LIKE " + sqlLiteral("%:" + std::to_string(pid) + ":" + std::to_string(symbol) + ":%") + ") ";
  }
  query += "ORDER BY CASE ";
  query += "WHEN ce.equipment_slot=" + sqlLiteral(slot) + " THEN 0 ";
  query += "WHEN ce.equipment_slot IS NOT NULL THEN 1 ";
  query += "ELSE 2 END, COALESCE(ci.bag_index,999999), ci.amount DESC, ii.item_instance_key ASC LIMIT 1;";

  auto out = mysqlSingleField(target, query);
  if(out.empty())
    throw std::runtime_error("character item could not be resolved: symbol=" + std::to_string(symbol) +
                             " pid=" + std::to_string(pid) + " slot=" + slot);
  return out;
}

[[nodiscard]] DirectApplyResult applyDirectDb(const MySqlTarget& target,
                                             std::string_view sessionUuid,
                                             const Mmo::Net::ClientActionPacket& packet,
                                             std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const auto tick = packetServerTick(packet);

  if(packet.kind == Mmo::SemanticActionKind::ClientBootstrapRequest)
    return {false, true, false, "bootstrap"};
  if(packet.kind == Mmo::SemanticActionKind::CharacterCheckpoint) {
    applyCharacterCheckpoint(target, sessionUuid, packet, dbPayload);
    return {true, true, true, "character_checkpoint"};
  }
  if(packet.kind == Mmo::SemanticActionKind::SaveCheckpointManifest) {
    applySaveCheckpointManifest(target, sessionUuid, packet, dbPayload);
    return {true, true, true, "save_checkpoint_manifest"};
  }
  if(packet.kind == Mmo::SemanticActionKind::MovementProposal)
    return applyMovementProposal(target, sessionUuid, packet, dbPayload);

  if(packet.kind == Mmo::SemanticActionKind::SetScriptInt) {
    const auto scriptKey = scriptKeyFromPayload(packet);
    Mmo::Server::setCharacterScriptInt(target, {
      .sessionUuid = sessionUuid,
      .scriptKey = scriptKey,
      .symbolIndex = optionalJsonI64(payload, "symbol_index", 0),
      .valueIndex = optionalJsonI64(payload, "value_index", 0),
      .valueAfter = optionalJsonI64(payload, "value_after", optionalJsonI64(payload, "value", 0)),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::UpdateQuest) {
    const auto questKey = optionalJsonString(payload, "quest_key", optionalJsonString(payload, "topic", packet.targetKey));
    const auto questName = optionalJsonString(payload, "quest_name", optionalJsonString(payload, "name", questKey));
    Mmo::Server::updateCharacterQuest(target, {
      .sessionUuid = sessionUuid,
      .questKey = questKey,
      .questName = questName,
      .status = questStatus(payload),
      .entryCount = optionalJsonI64(payload, "entry_count", 0),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::SetKnownDialog) {
    const auto npcKey = optionalJsonString(payload, "npc_key", optionalJsonString(payload, "npc_symbol_name"));
    const auto infoKey = optionalJsonString(payload, "info_key", optionalJsonString(payload, "info_symbol_name", packet.targetKey));
    const bool known = optionalJsonBool(payload, "known", true);
    bool permanent = optionalJsonBool(payload, "permanent", optionalJsonBool(payload, "repeatable", false));
    if(!jsonBoolField(payload, "permanent") && !jsonBoolField(payload, "repeatable") && jsonBoolField(payload, "removed"))
      permanent = !optionalJsonBool(payload, "removed", false);
    const std::string availability = optionalJsonString(payload, "availability_state",
      known && permanent ? "repeatable_known" : (known ? "consumed_hidden" : (permanent ? "repeatable_not_seen" : "one_shot_not_seen")));
    Mmo::Server::setCharacterKnownDialog(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npcKey,
      .infoKey = infoKey,
      .known = known,
      .permanent = permanent,
      .availability = availability,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::AdjustProgression) {
    Mmo::Server::adjustCharacterProgression(target, {
      .sessionUuid = sessionUuid,
      .experienceDelta = optionalJsonI64(payload, "experience_delta", optionalJsonI64(payload, "xp_delta", optionalJsonI64(payload, "delta", 0))),
      .learningPointsDelta = optionalJsonI64(payload, "learning_points_delta", optionalJsonI64(payload, "lp_delta", 0)),
      .reason = optionalJsonString(payload, "reason", "script_progression"),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::ApplyExperienceReward) {
    Mmo::Server::applyCharacterExperienceReward(target, {
      .sessionUuid = sessionUuid,
      .experienceDelta = optionalJsonI64(payload, "experience_delta", optionalJsonI64(payload, "xp_delta", optionalJsonI64(payload, "delta", 0))),
      .reason = optionalJsonString(payload, "reason", "script_experience_reward"),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::ApplyCharacterDamage) {
    Mmo::Server::applyCharacterDamage(target, {
      .sessionUuid = sessionUuid,
      .characterKey = optionalJsonString(payload, "target_character_key", optionalJsonString(payload, "character_key", "PC_HERO")),
      .damage = damageAmountFromPayload(payload),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::ApplyWorldEntityDamage) {
    ResolvedWorldNpcEntity npc;
    try {
      npc = resolveTargetWorldNpcEntityKey(target, sessionUuid, packet);
    } catch(const std::exception& resolveError) {
      std::cerr << "[observed_world_npc_resolve_fallback] action=apply_world_entity_damage"
                << " target=" << packet.targetKey
                << " reason=" << resolveError.what() << "\n";
      npc = materializeObservedWorldNpcEntity(target, sessionUuid, packet, dbPayload);
    }
    if(npc.lifecycleState != "active")
      return {true, true, false, "world_entity_damage_noop_inactive"};
    const auto damage = damageAmountFromPayload(payload);
    const bool fatal = optionalJsonBool(payload, "fatal", optionalJsonBool(payload, "dead", false));
    Mmo::Server::applyWorldEntityDamage(target, {
      .sessionUuid = sessionUuid,
      .entityKey = npc.entityKey,
      .damage = damage,
      .fatal = fatal,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::MarkNpcDead) {
    ResolvedWorldNpcEntity npc;
    try {
      npc = resolveTargetWorldNpcEntityKey(target, sessionUuid, packet);
    } catch(const std::exception& resolveError) {
      std::cerr << "[observed_world_npc_resolve_fallback] action=mark_npc_dead"
                << " target=" << packet.targetKey
                << " reason=" << resolveError.what() << "\n";
      npc = materializeObservedWorldNpcEntity(target, sessionUuid, packet, dbPayload);
    }
    if(npc.lifecycleState != "active")
      return {true, true, false, "mark_npc_dead_noop_inactive"};
    Mmo::Server::markNpcDead(target, {
      .sessionUuid = sessionUuid,
      .entityKey = npc.entityKey,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::ApplyCharacterResourceDelta ||
            packet.kind == Mmo::SemanticActionKind::ConsumeMana) {
    const auto characterKey = optionalJsonString(payload, "character_key",
                            optionalJsonString(payload, "target_character_key", "PC_HERO"));
    const auto resourceKey = optionalJsonString(payload, "resource_key",
                           packet.kind == Mmo::SemanticActionKind::ConsumeMana ? "mana" : "unknown");
    const auto valueBefore = optionalJsonI64(payload, "value_before", 0);
    const auto valueAfter = optionalJsonI64(payload, "value_after", valueBefore);
    const auto delta = packet.kind == Mmo::SemanticActionKind::ConsumeMana ?
      -optionalJsonI64(payload, "mana_amount", optionalJsonI64(payload, "amount", valueBefore - valueAfter)) :
      optionalJsonI64(payload, "delta_amount", valueAfter - valueBefore);
    Mmo::Server::recordCharacterResourceDelta(target, {
      .sessionUuid = sessionUuid,
      .characterKey = characterKey,
      .resourceKey = resourceKey,
      .delta = delta,
      .valueBefore = valueBefore,
      .valueAfter = valueAfter,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::TriggerEvent) {
    Mmo::Server::recordTriggerEvent(target, {
      .sessionUuid = sessionUuid,
      .triggerKey = optionalJsonString(payload, "trigger_key", optionalJsonString(payload, "target_key", packet.targetKey)),
      .eventTypeName = optionalJsonString(payload, "event_type_name", optionalJsonString(payload, "reason", "trigger_event")),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::MoverStateChanged) {
    const auto moverKey = optionalJsonString(payload, "mover_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto stateBefore = optionalJsonI64(payload, "state_before", 0);
    const auto stateAfter = optionalJsonI64(payload, "state_after", stateBefore);
    const auto stateAfterName = optionalJsonString(payload, "state_after_name", "");
    const auto frame = optionalJsonI64(payload, "frame", optionalJsonI64(payload, "frame_index", 0));
    const auto targetFrame = optionalJsonI64(payload, "target_frame", optionalJsonI64(payload, "target_frame_index", frame));
    Mmo::Server::recordMoverState(target, {
      .sessionUuid = sessionUuid,
      .moverKey = moverKey,
      .stateBefore = stateBefore,
      .stateAfter = stateAfter,
      .stateAfterName = stateAfterName,
      .frame = frame,
      .targetFrame = targetFrame,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::RecordNpcRoutineState) {
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto routineState = optionalJsonString(payload, "routine_state", "unknown");
    const auto scheduleKey = optionalJsonString(payload, "schedule_key", optionalJsonString(payload, "routine_key"));
    const auto currentWaypoint = optionalJsonString(payload, "current_waypoint_key",
                                 optionalJsonString(payload, "current_waypoint"));
    const auto targetWaypoint = optionalJsonString(payload, "target_waypoint_key",
                                optionalJsonString(payload, "target_waypoint"));
    Mmo::Server::recordNpcRoutineState(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npcKey,
      .routineState = routineState,
      .scheduleKey = scheduleKey,
      .currentWaypoint = currentWaypoint,
      .targetWaypoint = targetWaypoint,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::RecordNpcAiState) {
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto aiState = optionalJsonString(payload, "ai_state", optionalJsonString(payload, "ai_state_name", "unknown"));
    const auto aiIntent = optionalJsonString(payload, "ai_intent", optionalJsonString(payload, "intent", ""));
    const auto targetEntity = optionalJsonString(payload, "ai_target_key",
                            optionalJsonString(payload, "target_entity_key",
                            optionalJsonString(payload, "target_key", "")));
    const auto perceptionState = optionalJsonString(payload, "perception_state", "");
    Mmo::Server::recordNpcAiState(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npcKey,
      .aiState = aiState,
      .aiIntent = aiIntent,
      .targetEntity = targetEntity,
      .perceptionState = perceptionState,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::RecordNpcPathState) {
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto pathState = optionalJsonString(payload, "path_state", "unknown");
    const auto routeKey = optionalJsonString(payload, "route_key", "");
    const auto currentWaypoint = optionalJsonString(payload, "current_waypoint_key",
                                 optionalJsonString(payload, "current_waypoint"));
    const auto nextWaypoint = optionalJsonString(payload, "next_waypoint_key",
                              optionalJsonString(payload, "next_waypoint"));
    const auto targetWaypoint = optionalJsonString(payload, "target_waypoint_key",
                                optionalJsonString(payload, "target_waypoint"));
    const auto posXText = jsonNumberTextField(payload, "pos_x");
    const auto posYText = jsonNumberTextField(payload, "pos_y");
    const auto posZText = jsonNumberTextField(payload, "pos_z");
    Mmo::Server::recordNpcPathState(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npcKey,
      .pathState = pathState,
      .routeKey = routeKey,
      .currentWaypoint = currentWaypoint,
      .nextWaypoint = nextWaypoint,
      .targetWaypoint = targetWaypoint,
      .posX = posXText ? parseDouble(*posXText) : std::optional<double>{},
      .posY = posYText ? parseDouble(*posYText) : std::optional<double>{},
      .posZ = posZText ? parseDouble(*posZText) : std::optional<double>{},
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::RecordNpcFightState) {
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto opponentKey = optionalJsonString(payload, "opponent_key", optionalJsonString(payload, "target_entity_key"));
    const auto fightState = optionalJsonString(payload, "fight_state", "unknown");
    const auto attackState = optionalJsonString(payload, "attack_state", "");
    Mmo::Server::recordNpcFightState(target, {
      .sessionUuid = sessionUuid,
      .npcKey = npcKey,
      .opponentKey = opponentKey,
      .fightState = fightState,
      .attackState = attackState,
      .comboIndex = optionalJsonI64(payload, "combo_index", 0),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::RecordTriggerQueueState) {
    const auto triggerKey = optionalJsonString(payload, "trigger_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto queueState = optionalJsonString(payload, "queue_state", "queued");
    const auto eventTypeName = optionalJsonString(payload, "event_type_name", optionalJsonString(payload, "reason", "trigger_event"));
    Mmo::Server::recordTriggerQueueState(target, {
      .sessionUuid = sessionUuid,
      .triggerKey = triggerKey,
      .queueState = queueState,
      .eventTypeName = eventTypeName,
      .scheduledServerTick = optionalJsonI64(payload, "scheduled_server_tick", optionalJsonI64(payload, "execute_at_tick", tick)),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::RecordWorldTransitionState) {
    const auto fromWorld = optionalJsonString(payload, "from_world_key", optionalJsonString(payload, "from_world", ""));
    const auto toWorld = optionalJsonString(payload, "to_world_key", optionalJsonString(payload, "to_world", optionalJsonString(payload, "world", "")));
    const auto transitionState = optionalJsonString(payload, "transition_state", "visited");
    const auto chapterKey = optionalJsonString(payload, "chapter_key", optionalJsonString(payload, "chapter", ""));
    Mmo::Server::recordWorldTransitionState(target, {
      .sessionUuid = sessionUuid,
      .fromWorld = fromWorld,
      .toWorld = toWorld,
      .transitionState = transitionState,
      .chapterKey = chapterKey,
      .visited = optionalJsonBool(payload, "visited", true),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::ClientCorrectionAck) {
    Mmo::Server::ackClientActionCorrection(target, {
      .sessionUuid = sessionUuid,
      .actionKind = optionalJsonString(payload, "action_kind", ""),
      .localSequence = optionalJsonI64(payload, "client_local_sequence", 0),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::SplitItemStack ||
            packet.kind == Mmo::SemanticActionKind::MergeItemStack) {
    return {true, true, false, "stack_layout_noop"};
  } else if(packet.kind == Mmo::SemanticActionKind::TransferCharacterItem) {
    const auto targetCharacter = optionalJsonString(payload, "target_character_key");
    const auto sourceActor = optionalJsonString(payload, "source_actor_key");
    if(targetCharacter.empty() || sourceActor.empty()) {
      // Legacy Inventory::transfer packets did not carry enough owner identity to
      // apply a safe authoritative mutation. New server-bound clients emit
      // domain-specific container/loot/trade/drop hooks instead. Accept the
      // legacy packet as a no-op to avoid punishing local UI-only inventory churn.
      return {true, true, false, "transfer_character_item_legacy_noop"};
    }
    const auto itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet);
    const auto amount = optionalJsonI64(payload, "amount", 1);
    Mmo::Server::transferCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .itemUuid = itemUuid,
      .targetCharacterKey = targetCharacter,
      .amount = amount,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::LootNpcInventory ||
            packet.kind == Mmo::SemanticActionKind::TakeContainerItem) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    auto bagIndex = optionalJsonI64(payload, "server_bag_index", -1);
    if(bagIndex < 0)
      bagIndex = nextBagIndex(target, sessionUuid);
    try {
      const auto sourceEntityKey = resolveWorldInventoryOwnerEntityKey(target, sessionUuid, packet);
      const auto itemUuid = resolveNpcInventoryItemUuid(target, sessionUuid, sourceEntityKey, packet);
      Mmo::Server::lootWorldInventoryItem(target, {
        .sessionUuid = sessionUuid,
        .sourceEntityKey = sourceEntityKey,
        .itemUuid = itemUuid,
        .amount = amount,
        .bagIndex = bagIndex,
        .serverTick = tick,
        .dbPayload = dbPayload,
        .idempotencyKey = packet.idempotencyKey,
      });
      return {true, true, true, "direct_applied"};
    } catch(const std::exception& resolveError) {
      const bool corpseLoot = packet.kind == Mmo::SemanticActionKind::LootNpcInventory &&
        (optionalJsonBool(payload, "source_dead", false) ||
         optionalJsonBool(payload, "source_unconscious", false) ||
         optionalJsonString(payload, "reason") == "loot_dead_or_unconscious_npc");
      const auto symbol = itemSymbolFromPayload(payload);
      if(!corpseLoot || symbol < 0)
        throw;
      try {
        const auto sourceEntityKey = resolveWorldInventoryOwnerEntityKey(target, sessionUuid, packet);
        const auto itemUuid = materializeObservedNpcLootItem(target, sessionUuid, sourceEntityKey, packet, dbPayload);
        Mmo::Server::lootWorldInventoryItem(target, {
          .sessionUuid = sessionUuid,
          .sourceEntityKey = sourceEntityKey,
          .itemUuid = itemUuid,
          .amount = amount,
          .bagIndex = bagIndex,
          .serverTick = tick,
          .dbPayload = dbPayload,
          .idempotencyKey = packet.idempotencyKey,
        });
        return {true, true, true, "direct_applied"};
      } catch(const std::exception& materializeError) {
        std::cerr << "[npc_loot_grant_fallback] target=" << packet.targetKey
                  << " symbol=" << symbol
                  << " amount=" << amount
                  << " resolve_reason=" << resolveError.what()
                  << " materialize_reason=" << materializeError.what() << "\n";
        Mmo::Server::grantCharacterItemBySymbol(target, {
          .sessionUuid = sessionUuid,
          .itemSymbol = symbol,
          .amount = amount,
          .bagIndex = bagIndex,
          .serverTick = tick,
          .dbPayload = dbPayload,
          .idempotencyKey = packet.idempotencyKey,
        });
        return {true, true, true, "direct_applied"};
      }
    }
  } else if(packet.kind == Mmo::SemanticActionKind::PickupWorldItem) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    auto bagIndex = optionalJsonI64(payload, "server_bag_index", -1);
    if(bagIndex < 0)
      bagIndex = nextBagIndex(target, sessionUuid);
    try {
      const auto entityKey = resolveWorldItemEntityKey(target, sessionUuid, packet);
      Mmo::Server::pickupWorldItem(target, {
        .sessionUuid = sessionUuid,
        .entityKey = entityKey,
        .amount = amount,
        .bagIndex = bagIndex,
        .serverTick = tick,
        .dbPayload = dbPayload,
        .idempotencyKey = packet.idempotencyKey,
      });
      return {true, true, true, "direct_applied"};
    } catch(const std::exception& resolveError) {
      const auto symbol = itemSymbolFromPayload(payload);
      if(symbol < 0)
        throw;
      try {
        const auto entityKey = materializeObservedWorldItem(target, sessionUuid, packet, dbPayload);
        Mmo::Server::pickupWorldItem(target, {
          .sessionUuid = sessionUuid,
          .entityKey = entityKey,
          .amount = amount,
          .bagIndex = bagIndex,
          .serverTick = tick,
          .dbPayload = dbPayload,
          .idempotencyKey = packet.idempotencyKey,
        });
        return {true, true, true, "direct_applied"};
      } catch(const std::exception& materializeError) {
        std::cerr << "[world_item_pickup_grant_fallback] target=" << packet.targetKey
                  << " symbol=" << symbol
                  << " amount=" << amount
                  << " resolve_reason=" << resolveError.what()
                  << " materialize_reason=" << materializeError.what() << "\n";
        Mmo::Server::grantCharacterItemBySymbol(target, {
          .sessionUuid = sessionUuid,
          .itemSymbol = symbol,
          .amount = amount,
          .bagIndex = bagIndex,
          .serverTick = tick,
          .dbPayload = dbPayload,
          .idempotencyKey = packet.idempotencyKey,
        });
        return {true, true, true, "direct_applied"};
      }
    }
  } else if(packet.kind == Mmo::SemanticActionKind::RemoveWorldItem) {
    const auto entityKey = resolveWorldItemEntityKey(target, sessionUuid, packet);
    const auto reason = optionalJsonString(payload, "reason", "semantic_action");
    Mmo::Server::removeWorldItem(target, {
      .sessionUuid = sessionUuid,
      .entityKey = entityKey,
      .reason = reason,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::EquipCharacterItem) {
    const auto itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet);
    const auto slot = normalizedEquipmentSlot(payload);
    Mmo::Server::equipCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .itemUuid = itemUuid,
      .equipmentSlot = slot,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::UnequipCharacterItem) {
    const auto slot = normalizedEquipmentSlot(payload);
    Mmo::Server::unequipCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .equipmentSlot = slot,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::DropCharacterItem) {
    const auto itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet);
    const auto amount = optionalJsonI64(payload, "amount", 1);
    const auto entityKey = optionalJsonString(payload, "world_item_entity_key",
                           optionalJsonString(payload, "engine_world_item_key",
                           optionalJsonString(payload, "dropped_world_item_key",
                           optionalJsonString(payload, "target_key", packet.targetKey))));
    Mmo::Server::dropCharacterItem(target, {
      .sessionUuid = sessionUuid,
      .itemUuid = itemUuid,
      .amount = amount,
      .entityKey = entityKey,
      .posX = optionalJsonPositionDouble(payload, "x", "pos_x", "world_pos_x", "actor_pos_x"),
      .posY = optionalJsonPositionDouble(payload, "y", "pos_y", "world_pos_y", "actor_pos_y"),
      .posZ = optionalJsonPositionDouble(payload, "z", "pos_z", "world_pos_z", "actor_pos_z"),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::UseInteractive) {
    Mmo::Server::recordInteractiveUse(target, {
      .sessionUuid = sessionUuid,
      .interactiveKey = optionalJsonString(payload, "interactive_key", optionalJsonString(payload, "target_key", packet.targetKey)),
      .stateAfter = optionalJsonI64(payload, "state_after", optionalJsonI64(payload, "state", 0)),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::UpdateInteractiveState) {
    const auto key = optionalJsonString(payload, "interactive_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto state = optionalJsonI64(payload, "state_after", optionalJsonI64(payload, "state", 0));
    const auto count = optionalJsonI64(payload, "state_count", 0);
    const auto mask = optionalJsonI64(payload, "state_mask", 0);
    const bool locked = optionalJsonBool(payload, "locked_after", optionalJsonBool(payload, "locked", false));
    const bool cracked = optionalJsonBool(payload, "cracked_after", optionalJsonBool(payload, "cracked", false));
    const auto lifecycle = optionalJsonString(payload, "lifecycle_state", "active");
    Mmo::Server::updateInteractiveState(target, {
      .sessionUuid = sessionUuid,
      .interactiveKey = key,
      .stateAfter = state,
      .stateCount = count,
      .stateMask = mask,
      .locked = locked,
      .cracked = cracked,
      .lifecycle = lifecycle,
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else if(packet.kind == Mmo::SemanticActionKind::ReadyWeapon || packet.kind == Mmo::SemanticActionKind::HolsterWeapon) {
    const auto actorKey = optionalJsonString(payload, "actor_key", optionalJsonString(payload, "actor_entity_key", packet.targetKey));
    const auto state = optionalJsonString(payload, "new_weapon_state",
                       optionalJsonString(payload, "weapon_state",
                       packet.kind == Mmo::SemanticActionKind::HolsterWeapon ? "no_weapon" : "ready_weapon"));
    Mmo::Server::recordNpcWeaponState(target, {
      .sessionUuid = sessionUuid,
      .actorKey = actorKey,
      .weaponState = state,
      .ready = optionalJsonBool(payload, "ready", packet.kind == Mmo::SemanticActionKind::ReadyWeapon),
      .serverTick = tick,
      .dbPayload = dbPayload,
      .idempotencyKey = packet.idempotencyKey,
    });
    return {true, true, true, "direct_applied"};
  } else {
    return {false, true, false, "unhandled"};
  }
}

[[nodiscard]] std::pair<std::string, std::string> parseBind(std::string_view value) {
  const auto colon = value.rfind(':');
  if(colon == std::string_view::npos || colon == 0 || colon + 1 >= value.size())
    throw std::runtime_error("expected --bind host:port");
  return {std::string(value.substr(0, colon)), std::string(value.substr(colon + 1))};
}

Options parseArgs(int argc, char** argv) {
  Options opt;
  auto need = [&](int& i, std::string_view flag) -> std::string {
    if(i + 1 >= argc)
      throw std::runtime_error(std::string(flag) + " requires value");
    return argv[++i];
  };

  for(int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if(arg == "--bind") opt.bind = need(i, arg);
    else if(arg == "--mysql-url" || arg == "--url") opt.mysqlUrl = need(i, arg);
    else if(arg == "--account-name") opt.accountName = need(i, arg);
    else if(arg == "--character-key") opt.characterKey = need(i, arg);
    else if(arg == "--character-name" || arg == "--character-display-name") opt.characterDisplayName = need(i, arg);
    else if(arg == "--session-key") opt.sessionKey = need(i, arg);
    else if(arg == "--db-session-uuid") opt.dbSessionUuid = need(i, arg);
    else if(arg == "--outbox-priority") opt.outboxPriority = parseInt(need(i, arg)).value_or(opt.outboxPriority);
    else if(arg == "--outbox-max-attempts") opt.outboxMaxAttempts = parseInt(need(i, arg)).value_or(opt.outboxMaxAttempts);
    else if(arg == "--max-packets") opt.maxPackets = parseInt(need(i, arg)).value_or(0);
    else if(arg == "--direct-db") opt.directDb = true;
    else if(arg == "--no-direct-db") opt.directDb = false;
    else if(arg == "--enqueue-outbox") opt.enqueueOutbox = true;
    else if(arg == "--no-enqueue-outbox") opt.enqueueOutbox = false;
    else if(arg == "--forward-bootstrap-outbox") opt.forwardBootstrapOutbox = true;
    else if(arg == "--require-db-save-checkpoint-restore") opt.requireDbSaveCheckpointRestore = true;
    else if(arg == "--no-require-db-save-checkpoint-restore") opt.requireDbSaveCheckpointRestore = false;
    else if(arg == "--help" || arg == "-h") {
      std::cout << "Usage: mmo_udp_server --bind 127.0.0.1:29777 --mysql-url mysql://user:pass@host:3306/db [--session-key local-dev-PC_HERO_TEST] [--enqueue-outbox] [--no-direct-db] [--require-db-save-checkpoint-restore]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(arg));
    }
  }
  return opt;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Options opt = parseArgs(argc, argv);
    Options activeOpt = opt;
    std::optional<MySqlTarget> mysql;
    std::string sessionUuid = opt.dbSessionUuid;
    if(opt.directDb || opt.enqueueOutbox) {
      if(opt.mysqlUrl.empty())
        throw std::runtime_error("--mysql-url is required when direct DB or outbox mode is enabled");
      mysql = parseMysqlUrl(opt.mysqlUrl);
      if(sessionUuid.empty())
        sessionUuid = dbLogin(*mysql, activeOpt);
      else
        (void)ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "startup");
      std::cout << "db_session=" << sessionUuid
                << " direct_db=" << (opt.directDb ? "on" : "off")
                << " enqueue_outbox=" << (opt.enqueueOutbox ? "on" : "off")
                << " require_db_save_checkpoint_restore=" << (opt.requireDbSaveCheckpointRestore ? "on" : "off")
                << "\n";
    }

    const auto [bindHost, bindPort] = parseBind(opt.bind);
    asio::io_context io;
    asio::ip::udp::resolver resolver(io);
    asio::error_code ec;
    auto results = resolver.resolve(asio::ip::udp::v4(), bindHost, bindPort, ec);
    if(ec || results.empty())
      throw std::runtime_error("bind resolve failed: " + ec.message());

    asio::ip::udp::socket socket(io, results.begin()->endpoint());
    socket.non_blocking(true);
    std::signal(SIGINT, stopHandler);
    std::signal(SIGTERM, stopHandler);

    std::unordered_set<std::string> seen;
    std::array<char, Mmo::Net::MaxDatagramBytes> buffer {};
    std::uint64_t received = 0;
    std::uint64_t accepted = 0;
    std::uint64_t invalid = 0;
    std::uint64_t duplicate = 0;
    std::uint64_t enqueued = 0;
    std::uint64_t directDb = 0;
    std::uint64_t unhandled = 0;
    std::uint64_t failed = 0;
    std::uint32_t nextSnapshotId = 1;
    ServerPacketLogState logState;
    LiveWorldSnapshotState liveWorldSnapshotState;

    std::cout << "listening udp://" << opt.bind << " binary_protocol=v1\n";
    while(gRunning.load(std::memory_order_relaxed)) {
      if(opt.maxPackets > 0 && static_cast<int>(received) >= opt.maxPackets)
        break;

      asio::ip::udp::endpoint remote;
      const auto n = socket.receive_from(asio::buffer(buffer), remote, 0, ec);
      if(ec) {
        if(ec == asio::error::would_block || ec == asio::error::try_again) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          continue;
        }
        if(ec == asio::error::connection_reset) {
          std::cout << "[udp_receive_ignored] error=connection_reset message=" << ec.message() << "\n";
          continue;
        }
        throw std::runtime_error("receive_from failed: " + ec.message());
      }
      ++received;

      const auto decoded = Mmo::Net::decodeClientActionPacket(std::string_view(buffer.data(), n));
      if(!decoded.ok()) {
        ++invalid;
        const auto bytes = std::string_view(buffer.data(), n);
        std::cout << "[invalid] remote=" << remote << " error=" << Mmo::Net::decodeErrorName(decoded.error);
        if(decoded.error == Mmo::Net::DecodeError::BadActionKind) {
          if(const auto raw = rawClientActionKind(bytes))
            std::cout << " raw_action_kind=" << *raw << " known_actions=" << Mmo::SemanticActionDefs.size();
        }
        std::cout << " datagram_bytes=" << n << "\n";
        continue;
      }

      const auto& packet = decoded.clientAction;
      const auto* def = Mmo::findSemanticAction(packet.kind);
      const bool isBootstrap = packet.kind == Mmo::SemanticActionKind::ClientBootstrapRequest;
      const bool isMovement = packet.kind == Mmo::SemanticActionKind::MovementProposal ||
                              packet.kind == Mmo::SemanticActionKind::CharacterCheckpoint;
      const bool isWeaponState = packet.kind == Mmo::SemanticActionKind::ReadyWeapon ||
                                 packet.kind == Mmo::SemanticActionKind::HolsterWeapon;
      if(!seen.insert(packet.idempotencyKey).second) {
        if(isBootstrap) {
          seen.clear();
          seen.insert(packet.idempotencyKey);
          std::cout << "[bootstrap_restarts_dedupe] session=" << packet.sessionKey << "\n";
        } else {
          ++duplicate;
          continue;
        }
      }

      bool packetAccepted = true;
      bool packetReady = false;
      std::string bootstrapSnapshotJson;
      std::string liveWorldSnapshotJson;
      std::string diagnosticReason;
      std::string diagnosticMessage;
      std::uint16_t diagnosticSeverity = 0;
      const std::string_view actionName = def ? def->actionKind : std::string_view("unknown");
      if(isBootstrap) {
        BootstrapReadiness readiness;
        const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
        const std::string displayName = jsonStringField(packet.payloadJson, "display_name").value_or(characterKey);
        std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
        if(mysql) {
          try {
            if(characterKey != activeOpt.characterKey) {
              activeOpt.characterKey = characterKey;
              activeOpt.characterDisplayName = displayName.empty() ? characterKey : displayName;
              sessionUuid = dbLogin(*mysql, activeOpt);
              seen.clear();
              seen.insert(packet.idempotencyKey);
              std::cout << "[db_session_character_selected]"
                        << " character=" << activeOpt.characterKey
                        << " session=" << sessionUuid << "\n";
            } else if(ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "bootstrap")) {
              seen.clear();
              seen.insert(packet.idempotencyKey);
            }
            readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
            packetReady = readiness.ready;
            printBootstrapAck(packet, characterKey, worldName, readiness, true);
            if(packetReady) {
              try {
                bootstrapSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, true, opt.requireDbSaveCheckpointRestore);
              } catch(const std::exception& exc) {
                diagnosticSeverity = 2;
                diagnosticReason = opt.requireDbSaveCheckpointRestore ? "db_save_checkpoint_restore_required" : "bootstrap_snapshot_build_failed";
                diagnosticMessage = exc.what();
                if(opt.requireDbSaveCheckpointRestore) {
                  packetAccepted = false;
                  packetReady = false;
                  ++failed;
                }
                std::cerr << "[bootstrap_snapshot_build_failed] error=" << exc.what()
                          << " strict_db_save_checkpoint_restore=" << (opt.requireDbSaveCheckpointRestore ? 1 : 0) << "\n";
              }
            }
          } catch(const std::exception& exc) {
            packetAccepted = false;
            ++failed;
            diagnosticSeverity = 2;
            diagnosticReason = "bootstrap_failed";
            diagnosticMessage = exc.what();
            std::cerr << "[bootstrap_failed] error=" << exc.what() << "\n";
          }
        } else {
          readiness.ready = true;
          packetReady = true;
          printBootstrapAck(packet, characterKey, worldName, readiness, false);
        }
      }

      const std::string remoteText = remote.address().to_string() + ":" + std::to_string(remote.port());
      const auto dbPayload = mysql ? makeDbPayload(packet, remoteText) : std::string();
      DirectApplyResult direct;
      if(mysql && opt.directDb && !isBootstrap) {
        try {
          if(!isActiveDbSession(*mysql, sessionUuid)) {
            (void)ensureActiveDbSession(*mysql, activeOpt, sessionUuid, "direct_db");
            seen.clear();
            seen.insert(packet.idempotencyKey);
          }
          direct = applyDirectDb(*mysql, sessionUuid, packet, dbPayload);
          if(direct.handled) {
            ++directDb;
            packetAccepted = direct.accepted;
            packetReady = packetReady || direct.ready;
            if(!direct.accepted) {
              diagnosticSeverity = 1;
              diagnosticReason = direct.label;
              diagnosticMessage = "direct DB rejected semantic action";
            }
          }
        } catch(const std::exception& exc) {
          direct.handled = true;
          if(isFailOpenNpcObservationAction(packet.kind)) {
            direct.accepted = true;
            packetAccepted = true;
            std::cerr << "[direct_db_observation_failed_accepted] action=" << actionName
                      << " target=" << packet.targetKey
                      << " error=" << exc.what()
                      << " payload=" << packet.payloadJson
                      << "\n";
          } else {
            packetAccepted = false;
            ++failed;
            direct.accepted = false;
            diagnosticSeverity = 2;
            diagnosticReason = "direct_db_failed";
            diagnosticMessage = exc.what();
            std::cerr << "[direct_db_failed] action=" << actionName
                      << " target=" << packet.targetKey
                      << " error=" << exc.what()
                      << " payload=" << packet.payloadJson
                      << "\n";
          }
        }
      }

      if(mysql && opt.directDb && direct.handled && !direct.accepted) {
        try {
          recordClientActionCorrection(*mysql, sessionUuid, packet, actionName, direct.label, dbPayload);
          const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
          std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
          auto readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
          if(readiness.ready) {
            liveWorldSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, false, false);
            std::cout << "[client_correction_snapshot_queued]"
                      << " action=" << actionName
                      << " reason=" << direct.label
                      << " local_sequence=" << packet.localSequence
                      << " bytes=" << liveWorldSnapshotJson.size()
                      << "\n";
          }
        } catch(const std::exception& exc) {
          std::cerr << "[client_correction_snapshot_failed] action=" << actionName
                    << " reason=" << direct.label
                    << " error=" << exc.what() << "\n";
        }
      }

      if(mysql && opt.directDb && shouldSendLiveWorldSnapshot(liveWorldSnapshotState, packet, packetAccepted, direct)) {
        try {
          const std::string characterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
          std::string worldName = jsonStringField(packet.payloadJson, "world").value_or("UNKNOWN");
          auto readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
          if(readiness.ready) {
            liveWorldSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, false, false);
            if(const auto pos = movementToPosition(packet.payloadJson)) {
              std::cout << "[live_world_item_snapshot_queued] reason=movement_interest"
                        << " x=" << pos->x
                        << " y=" << pos->y
                        << " z=" << pos->z
                        << " bytes=" << liveWorldSnapshotJson.size()
                        << "\n";
            } else {
              std::cout << "[live_world_item_snapshot_queued] reason=movement_interest bytes="
                        << liveWorldSnapshotJson.size() << "\n";
            }
          }
        } catch(const std::exception& exc) {
          std::cerr << "[live_world_item_snapshot_build_failed] action=" << actionName
                    << " error=" << exc.what() << "\n";
        }
      }

      if(mysql && opt.enqueueOutbox && !direct.handled && (!isBootstrap || opt.forwardBootstrapOutbox)) {
        try {
          enqueueOutbox(*mysql, sessionUuid, packet, dbPayload, opt.outboxPriority, opt.outboxMaxAttempts);
          ++enqueued;
        } catch(const std::exception& exc) {
          packetAccepted = false;
          ++failed;
          diagnosticSeverity = 2;
          diagnosticReason = "enqueue_failed";
          diagnosticMessage = exc.what();
          std::cerr << "[enqueue_failed] action=" << actionName << " error=" << exc.what() << "\n";
        }
      } else if(mysql && opt.directDb && !isBootstrap && !direct.handled && !opt.enqueueOutbox) {
        packetAccepted = false;
        ++unhandled;
        diagnosticSeverity = 2;
        diagnosticReason = "direct_db_unhandled";
        diagnosticMessage = "semantic action has no direct C++ DB handler and outbox fallback is disabled";
        std::cerr << "[direct_db_unhandled] action=" << actionName << "\n";
      }

      ++accepted;
      const auto ackKind = isBootstrap ? Mmo::Net::ServerAckKind::Bootstrap :
                           (isMovement ? Mmo::Net::ServerAckKind::Movement : Mmo::Net::ServerAckKind::GenericAction);
      const auto ack = Mmo::Net::encodeServerAckPacket({packet.packetSequence, packet.localSequence, ackKind, packetAccepted, packetReady});
      socket.send_to(asio::buffer(ack), remote, 0, ec);
      if(!diagnosticReason.empty()) {
        sendServerDiagnostic(socket, remote, packet, diagnosticSeverity, actionName, diagnosticReason, diagnosticMessage);
      }
      bool snapshotSent = false;
      if(isBootstrap && packetAccepted && !bootstrapSnapshotJson.empty()) {
        try {
          sendBootstrapSnapshot(socket, remote, packet, nextSnapshotId++, bootstrapSnapshotJson);
          snapshotSent = true;
        } catch(const std::exception& exc) {
          ++failed;
          std::cerr << "[bootstrap_snapshot_send_failed] error=" << exc.what() << "\n";
          sendServerDiagnostic(socket, remote, packet, 2, actionName, "bootstrap_snapshot_send_failed", exc.what());
        }
      }
      if(!isBootstrap && packetAccepted && !liveWorldSnapshotJson.empty()) {
        try {
          sendBootstrapSnapshot(socket, remote, packet, nextSnapshotId++, liveWorldSnapshotJson);
          snapshotSent = true;
        } catch(const std::exception& exc) {
          ++failed;
          std::cerr << "[live_world_item_snapshot_send_failed] error=" << exc.what() << "\n";
          sendServerDiagnostic(socket, remote, packet, 2, actionName, "live_world_item_snapshot_send_failed", exc.what());
        }
      }
      printPacketProgress(logState, accepted, received, invalid, duplicate, enqueued, directDb, unhandled, failed,
                          actionName, packetAccepted, !diagnosticReason.empty(), snapshotSent, isMovement, isWeaponState);
    }

    std::cout << "summary:\n"
              << "received=" << received << "\n"
              << "accepted=" << accepted << "\n"
              << "invalid=" << invalid << "\n"
              << "duplicate=" << duplicate << "\n"
              << "enqueued=" << enqueued << "\n"
              << "direct_db=" << directDb << "\n"
              << "unhandled=" << unhandled << "\n"
              << "failed=" << failed << "\n";
    return invalid == 0 && failed == 0 && unhandled == 0 ? 0 : 2;
  } catch(const std::exception& exc) {
    std::cerr << "ERROR: " << exc.what() << "\n";
    return 2;
  }
}
