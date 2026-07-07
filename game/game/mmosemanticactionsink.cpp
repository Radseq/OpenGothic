#include "mmosemanticactionsink.h"

#include <Tempest/Log>

#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
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

bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::optional<std::size_t> findFieldValueStart(std::string_view json, std::string_view key) {
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

int hexNibble(char c) noexcept {
  if(c >= '0' && c <= '9')
    return c - '0';
  if(c >= 'a' && c <= 'f')
    return 10 + c - 'a';
  if(c >= 'A' && c <= 'F')
    return 10 + c - 'A';
  return -1;
}

bool readJsonHex4(std::string_view text, std::size_t pos, std::uint32_t& out) noexcept {
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

bool appendJsonEscape(std::string& out, std::string_view json, std::size_t& i) {
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

std::optional<std::string> jsonStringField(std::string_view json, std::string_view key) {
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

std::optional<std::string> jsonNumberTextField(std::string_view json, std::string_view key) {
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

std::optional<std::string_view> jsonObjectField(std::string_view json, std::string_view key) {
  auto pos = findFieldValueStart(json, key);
  if(!pos || *pos >= json.size() || json[*pos] != '{')
    return std::nullopt;

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for(std::size_t i = *pos; i < json.size(); ++i) {
    const char ch = json[i];
    if(inString) {
      if(escaped)
        escaped = false;
      else if(ch == '\\')
        escaped = true;
      else if(ch == '"')
        inString = false;
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

std::optional<std::int64_t> parseI64(std::string_view text) noexcept {
  std::int64_t value = 0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

std::optional<std::uint64_t> parseU64(std::string_view text) noexcept {
  std::uint64_t value = 0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

std::optional<double> parseDouble(std::string_view text) noexcept {
  char* end = nullptr;
  const std::string copy(text);
  const double value = std::strtod(copy.c_str(), &end);
  if(end == nullptr || *end != '\0')
    return std::nullopt;
  return value;
}

std::optional<bool> jsonBoolField(std::string_view json, std::string_view key) {
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

std::int32_t optionalJsonI32(std::string_view json, std::string_view key, std::int32_t fallback) noexcept {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    return fallback;
  auto value = parseI64(*text);
  if(!value)
    return fallback;
  if(*value < std::numeric_limits<std::int32_t>::min())
    return std::numeric_limits<std::int32_t>::min();
  if(*value > std::numeric_limits<std::int32_t>::max())
    return std::numeric_limits<std::int32_t>::max();
  return static_cast<std::int32_t>(*value);
}

std::uint64_t optionalJsonU64(std::string_view json, std::string_view key, std::uint64_t fallback) noexcept {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    return fallback;
  auto value = parseU64(*text);
  return value ? *value : fallback;
}

std::int64_t optionalJsonI64(std::string_view json, std::string_view key, std::int64_t fallback) noexcept {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    return fallback;
  auto value = parseI64(*text);
  return value ? *value : fallback;
}

double optionalJsonDouble(std::string_view json, std::string_view key, double fallback) noexcept {
  auto text = jsonNumberTextField(json, key);
  if(!text)
    return fallback;
  auto value = parseDouble(*text);
  return value ? *value : fallback;
}

std::string optionalJsonString(std::string_view json, std::string_view key, std::string fallback = {}) {
  return jsonStringField(json, key).value_or(std::move(fallback));
}

std::int32_t damageAmountFromPayload(std::string_view payload) noexcept {
  const auto amount = optionalJsonI32(payload,
                                      "damage_amount",
                                      optionalJsonI32(payload, "amount", optionalJsonI32(payload, "delta", 0)));
  if(amount == std::numeric_limits<std::int32_t>::min())
    return std::numeric_limits<std::int32_t>::max();
  return amount < 0 ? -amount : amount;
}

std::string combatPayloadKey(std::string_view prefix, std::string_view field) {
  std::string out(prefix);
  out += "_combat_";
  out += field;
  return out;
}

std::string indexedCombatPayloadKey(std::string_view prefix, std::string_view field, std::size_t index) {
  auto out = combatPayloadKey(prefix, field);
  out.push_back('_');
  out += std::to_string(index);
  return out;
}

Net::ClientCombatDamageKind clientCombatDamageKind(std::string_view name) noexcept {
  if(name == "melee")
    return Net::ClientCombatDamageKind::Melee;
  if(name == "ranged")
    return Net::ClientCombatDamageKind::Ranged;
  if(name == "magic" || name == "spell")
    return Net::ClientCombatDamageKind::Magic;
  if(name == "fall")
    return Net::ClientCombatDamageKind::Fall;
  return Net::ClientCombatDamageKind::Unknown;
}

Net::ClientCombatDamageModifier clientCombatDamageModifier(std::string_view name) noexcept {
  if(name == "double")
    return Net::ClientCombatDamageModifier::Double;
  if(name == "half")
    return Net::ClientCombatDamageModifier::Half;
  if(name == "blocked")
    return Net::ClientCombatDamageModifier::Blocked;
  return Net::ClientCombatDamageModifier::Normal;
}

void fillCombatProfile(Net::ClientCombatProfile& out, std::string_view payload, std::string_view prefix) {
  out.strength = optionalJsonI32(payload, combatPayloadKey(prefix, "strength"), 0);
  out.dexterity = optionalJsonI32(payload, combatPayloadKey(prefix, "dexterity"), 0);
  out.damageTypeMask = optionalJsonI32(payload, combatPayloadKey(prefix, "damage_type_mask"), 0);
  out.meleeTalentChance = optionalJsonI32(payload, combatPayloadKey(prefix, "melee_talent_chance"), 0);
  for(std::size_t i = 0; i < Net::CombatDamageTypeCount; ++i) {
    out.damage[i] = optionalJsonI32(payload, indexedCombatPayloadKey(prefix, "damage", i), 0);
    out.protection[i] = optionalJsonI32(payload, indexedCombatPayloadKey(prefix, "protection", i), 0);
  }
}

bool hasCombatProfile(std::string_view payload, std::string_view prefix) {
  return jsonNumberTextField(payload, combatPayloadKey(prefix, "damage_type_mask")).has_value();
}

bool hasExplicitDamage(std::string_view payload) {
  for(std::size_t i = 0; i < Net::CombatDamageTypeCount; ++i) {
    const std::string key = "damage_vector_" + std::to_string(i);
    if(jsonNumberTextField(payload, key).has_value())
      return true;
  }
  return false;
}

std::optional<Net::ClientCombatDamagePacket> makeClientCombatDamagePacket(const SemanticActionEnvelope& envelope,
                                                                         std::string_view sessionKey) {
  if(envelope.kind != SemanticActionKind::ApplyCharacterDamage &&
     envelope.kind != SemanticActionKind::ApplyWorldEntityDamage) {
    return std::nullopt;
  }

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientCombatDamagePacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = envelope.targetKey;
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.sourceActorKey = optionalJsonString(payload, "source_actor_key");
  packet.sourceActorEntityKey = optionalJsonString(payload, "source_actor_entity_key", packet.sourceActorKey);
  packet.targetCharacterKey = optionalJsonString(payload, "target_character_key",
                              optionalJsonString(payload, "character_key"));
  packet.targetEntityKey = optionalJsonString(payload,
                           envelope.kind == SemanticActionKind::ApplyWorldEntityDamage ? "target_key" : "target_key",
                           envelope.targetKey);
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.damageKind = clientCombatDamageKind(optionalJsonString(payload, "damage_kind", "unknown"));
  packet.modifier = clientCombatDamageModifier(optionalJsonString(payload, "damage_modifier", "normal"));
  packet.gothicGame = optionalJsonI32(payload, "gothic_game", 2);
  packet.damageAmount = damageAmountFromPayload(payload);
  packet.valueBefore = optionalJsonI32(payload, "value_before", 0);
  packet.valueAfter = optionalJsonI32(payload, "value_after", 0);
  packet.requestedDelta = optionalJsonI32(payload, "requested_delta", 0);
  packet.criticalDamageMultiplier = optionalJsonI32(payload, "critical_damage_multiplier", 0);
  packet.meleeTalentChance = optionalJsonI32(payload, "melee_talent_chance",
                            optionalJsonI32(payload, "source_actor_combat_melee_talent_chance", 0));
  packet.meleeRandomRoll = optionalJsonI32(payload, "melee_random_roll", 0);
  packet.projectileDistance = optionalJsonDouble(payload, "projectile_distance", 0.0);
  packet.projectileWeaponChance = optionalJsonDouble(payload, "projectile_weapon_chance", 0.0);
  packet.projectileRandomHitRoll = optionalJsonDouble(payload, "projectile_random_hit_roll", 0.0);
  packet.fallSpeed = optionalJsonDouble(payload, "fall_speed", 0.0);
  packet.fallGravity = optionalJsonDouble(payload, "fall_gravity", 0.000981);
  packet.fallHeightThreshold = optionalJsonI32(payload, "fall_height_threshold", 0);
  packet.fallDamagePerMeter = optionalJsonI32(payload, "fall_damage_per_meter", 0);

  fillCombatProfile(packet.source, payload, "source_actor");
  fillCombatProfile(packet.target, payload, "target");
  for(std::size_t i = 0; i < Net::CombatDamageTypeCount; ++i) {
    const std::string key = "damage_vector_" + std::to_string(i);
    packet.explicitDamage[i] = optionalJsonI32(payload, key, 0);
  }

  if(hasCombatProfile(payload, "source_actor"))
    packet.flags |= Net::ClientCombatDamageHasSourceProfile;
  if(hasCombatProfile(payload, "target"))
    packet.flags |= Net::ClientCombatDamageHasTargetProfile;
  if(hasExplicitDamage(payload))
    packet.flags |= Net::ClientCombatDamageHasExplicitDamage;
  if(jsonBoolField(payload, "fatal").value_or(jsonBoolField(payload, "dead").value_or(false)))
    packet.flags |= Net::ClientCombatDamageFatal;
  if(jsonBoolField(payload, "critical_hit").value_or(false))
    packet.flags |= Net::ClientCombatDamageCriticalHit;
  if(jsonBoolField(payload, "source_actor_combat_monster").value_or(false))
    packet.flags |= Net::ClientCombatDamageSourceMonster;
  if(jsonBoolField(payload, "source_actor_combat_has_active_weapon").value_or(false))
    packet.flags |= Net::ClientCombatDamageSourceHasActiveWeapon;
  if(jsonBoolField(payload, "target_combat_monster").value_or(false))
    packet.flags |= Net::ClientCombatDamageTargetMonster;
  if(jsonBoolField(payload, "target_combat_has_active_weapon").value_or(false))
    packet.flags |= Net::ClientCombatDamageTargetHasActiveWeapon;
  if(jsonNumberTextField(payload, "melee_random_roll").has_value())
    packet.flags |= Net::ClientCombatDamageHasMeleeRoll;
  if(jsonNumberTextField(payload, "projectile_distance").has_value() &&
     jsonNumberTextField(payload, "projectile_random_hit_roll").has_value()) {
    packet.flags |= Net::ClientCombatDamageHasRangedRoll;
  }
  if(jsonBoolField(payload, "projectile_spell").value_or(false))
    packet.flags |= Net::ClientCombatDamageProjectileSpell;
  if(jsonBoolField(payload, "projectile_critical_hit").value_or(false))
    packet.flags |= Net::ClientCombatDamageProjectileCriticalHit;
  if(jsonNumberTextField(payload, "fall_speed").has_value())
    packet.flags |= Net::ClientCombatDamageHasFallInput;
  if(auto targetPosition = jsonObjectField(payload, "target_position")) {
    packet.targetPosX = optionalJsonDouble(*targetPosition, "x", 0.0);
    packet.targetPosY = optionalJsonDouble(*targetPosition, "y", 0.0);
    packet.targetPosZ = optionalJsonDouble(*targetPosition, "z", 0.0);
    packet.flags |= Net::ClientCombatDamageHasTargetPosition;
  }

  return packet;
}

void fillMovementStats(Net::ClientMovementStats& out, std::string_view payload) {
  out.level = optionalJsonI32(payload, "level", 0);
  out.experience = optionalJsonI32(payload, "experience", 0);
  out.experienceNext = optionalJsonI32(payload, "experience_next", 0);
  out.learningPoints = optionalJsonI32(payload, "learning_points", 0);
  out.healthCurrent = optionalJsonI32(payload, "health_current", 0);
  out.healthMax = optionalJsonI32(payload, "health_max", 0);
  out.manaCurrent = optionalJsonI32(payload, "mana_current", 0);
  out.manaMax = optionalJsonI32(payload, "mana_max", 0);
  out.strength = optionalJsonI32(payload, "strength", 0);
  out.dexterity = optionalJsonI32(payload, "dexterity", 0);
  out.guild = optionalJsonI32(payload, "guild", 0);
  out.trueGuild = optionalJsonI32(payload, "true_guild", 0);
  out.permanentAttitude = optionalJsonI32(payload, "permanent_attitude", 0);
  out.temporaryAttitude = optionalJsonI32(payload, "temporary_attitude", 0);
}

std::optional<Net::ClientMovementPacket> makeClientMovementPacket(const SemanticActionEnvelope& envelope,
                                                                 std::string_view sessionKey) {
  if(envelope.kind != SemanticActionKind::MovementProposal &&
     envelope.kind != SemanticActionKind::CharacterCheckpoint) {
    return std::nullopt;
  }

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientMovementPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = envelope.targetKey;
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalJsonString(payload, "actor_key");
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.world = optionalJsonString(payload, "world");
  packet.waypointKey = optionalJsonString(payload, "current_waypoint_key");
  packet.reason = optionalJsonString(payload, "reason");

  packet.fromTick = optionalJsonU64(payload, "from_tick", 0);
  packet.toTick = optionalJsonU64(payload, "to_tick", envelope.clientTick);
  packet.deltaMs = optionalJsonU64(payload, "delta_ms", packet.toTick >= packet.fromTick ? packet.toTick - packet.fromTick : 0);
  packet.fromX = optionalJsonDouble(payload, "from_pos_x", 0.0);
  packet.fromY = optionalJsonDouble(payload, "from_pos_y", 0.0);
  packet.fromZ = optionalJsonDouble(payload, "from_pos_z", 0.0);
  packet.toX = optionalJsonDouble(payload, "to_pos_x", optionalJsonDouble(payload, "pos_x", 0.0));
  packet.toY = optionalJsonDouble(payload, "to_pos_y", optionalJsonDouble(payload, "pos_y", 0.0));
  packet.toZ = optionalJsonDouble(payload, "to_pos_z", optionalJsonDouble(payload, "pos_z", 0.0));
  packet.fromYaw = optionalJsonDouble(payload, "from_rotation_yaw", 0.0);
  packet.toYaw = optionalJsonDouble(payload, "to_rotation_yaw", optionalJsonDouble(payload, "rotation_yaw", 0.0));
  packet.cadenceIntervalMs = optionalJsonU64(payload,
                                             envelope.kind == SemanticActionKind::MovementProposal ? "proposal_interval_ms" : "checkpoint_interval_ms",
                                             0);
  packet.cadenceMinDistance = optionalJsonDouble(payload,
                                                 envelope.kind == SemanticActionKind::MovementProposal ? "proposal_min_distance" : "checkpoint_min_distance",
                                                 0.0);
  packet.cadenceMinYawDeg = optionalJsonDouble(payload,
                                               envelope.kind == SemanticActionKind::MovementProposal ? "proposal_min_yaw_deg" : "checkpoint_min_yaw_deg",
                                               0.0);
  packet.checkpointForceIntervalMs = optionalJsonU64(payload, "checkpoint_force_interval_ms", 0);
  fillMovementStats(packet.stats, payload);

  if(envelope.kind == SemanticActionKind::MovementProposal) {
    packet.flags |= Net::ClientMovementHasFromTransform;
    packet.flags |= Net::ClientMovementHasToTransform;
  } else {
    packet.flags |= Net::ClientMovementHasToTransform;
  }
  if(jsonNumberTextField(payload, "level").has_value())
    packet.flags |= Net::ClientMovementHasStats;
  if(jsonBoolField(payload, "from_is_in_air").value_or(false))
    packet.flags |= Net::ClientMovementFromInAir;
  if(jsonBoolField(payload, "from_is_falling").value_or(false))
    packet.flags |= Net::ClientMovementFromFalling;
  if(jsonBoolField(payload, "from_is_falling_deep").value_or(false))
    packet.flags |= Net::ClientMovementFromFallingDeep;
  if(jsonBoolField(payload, "from_is_slide").value_or(false))
    packet.flags |= Net::ClientMovementFromSlide;
  if(jsonBoolField(payload, "from_is_jump").value_or(false))
    packet.flags |= Net::ClientMovementFromJump;
  if(jsonBoolField(payload, "from_is_jump_up").value_or(false))
    packet.flags |= Net::ClientMovementFromJumpUp;
  if(jsonBoolField(payload, "from_is_swim").value_or(false))
    packet.flags |= Net::ClientMovementFromSwim;
  if(jsonBoolField(payload, "from_is_dive").value_or(false))
    packet.flags |= Net::ClientMovementFromDive;
  if(jsonBoolField(payload, "from_is_in_water").value_or(false))
    packet.flags |= Net::ClientMovementFromInWater;
  if(jsonBoolField(payload, "to_is_in_air").value_or(false))
    packet.flags |= Net::ClientMovementToInAir;
  if(jsonBoolField(payload, "to_is_falling").value_or(false))
    packet.flags |= Net::ClientMovementToFalling;
  if(jsonBoolField(payload, "to_is_falling_deep").value_or(false))
    packet.flags |= Net::ClientMovementToFallingDeep;
  if(jsonBoolField(payload, "to_is_slide").value_or(false))
    packet.flags |= Net::ClientMovementToSlide;
  if(jsonBoolField(payload, "to_is_jump").value_or(false))
    packet.flags |= Net::ClientMovementToJump;
  if(jsonBoolField(payload, "to_is_jump_up").value_or(false))
    packet.flags |= Net::ClientMovementToJumpUp;
  if(jsonBoolField(payload, "to_is_swim").value_or(false))
    packet.flags |= Net::ClientMovementToSwim;
  if(jsonBoolField(payload, "to_is_dive").value_or(false))
    packet.flags |= Net::ClientMovementToDive;
  if(jsonBoolField(payload, "to_is_in_water").value_or(false))
    packet.flags |= Net::ClientMovementToInWater;

  return packet;
}

bool fillVec3FromObject(std::string_view payload,
                        std::string_view key,
                        double& x,
                        double& y,
                        double& z) noexcept {
  const auto object = jsonObjectField(payload, key);
  if(!object)
    return false;
  x = optionalJsonDouble(*object, "x", 0.0);
  y = optionalJsonDouble(*object, "y", 0.0);
  z = optionalJsonDouble(*object, "z", 0.0);
  return true;
}

std::string optionalFirstJsonString(std::string_view payload,
                                    std::initializer_list<std::string_view> keys,
                                    std::string fallback = {}) {
  for(const auto key : keys) {
    if(auto value = jsonStringField(payload, key))
      return *value;
  }
  return fallback;
}

std::optional<Net::ClientInventoryPacket> makeClientInventoryPacket(const SemanticActionEnvelope& envelope,
                                                                    std::string_view sessionKey) {
  if(!Net::isInventoryPacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientInventoryPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalFirstJsonString(payload, {"target_key", "world_item_entity_key", "engine_world_item_key"}, envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "looter_key", "buyer_key", "seller_key", "character_key"});
  packet.sourceActorKey = optionalJsonString(payload, "source_actor_key");
  packet.targetCharacterKey = optionalFirstJsonString(payload, {"target_character_key", "character_key"});
  packet.itemTemplateKey = optionalJsonString(payload, "item_template_key");
  packet.itemInstanceId = optionalJsonString(payload, "item_instance_id");
  packet.itemInstanceUuid = optionalJsonString(payload, "item_instance_uuid");
  packet.equipmentSlot = optionalFirstJsonString(payload, {"equipment_slot", "slot_name", "slot"});
  packet.sourceEntityKey = optionalJsonString(payload, "source_entity_key");
  packet.sourceContainerKey = optionalJsonString(payload, "source_container_key");
  packet.containerKey = optionalFirstJsonString(payload, {"container_key", "source_container_key"});
  packet.sourceNpcKey = optionalFirstJsonString(payload, {"source_npc_key", "source_npc_entity_key"});
  packet.targetNpcEntityKey = optionalFirstJsonString(payload, {"target_npc_entity_key", "npc_entity_key"});
  packet.npcKey = optionalFirstJsonString(payload, {"npc_key", "npc_entity_key", "target_npc_entity_key"});
  packet.world = optionalJsonString(payload, "world");
  packet.tag = optionalJsonString(payload, "tag");
  packet.focusName = optionalJsonString(payload, "focus_name");
  packet.displayName = optionalJsonString(payload, "display_name");
  packet.scheme = optionalJsonString(payload, "scheme");
  packet.reason = optionalJsonString(payload, "reason");
  packet.currencyKey = optionalJsonString(payload, "currency_key");

  packet.itemSymbol = optionalJsonI64(payload, "item_symbol", optionalJsonI64(payload, "item_template_symbol", -1));
  packet.inventoryItemSymbol = optionalJsonI64(payload, "inventory_item_symbol", -1);
  packet.itemPersistentId = optionalJsonI64(payload, "item_persistent_id", optionalJsonI64(payload, "item_instance_persistent_id", -1));
  packet.sourceItemPersistentId = optionalJsonI64(payload, "source_item_persistent_id", -1);
  packet.sourceWorldItemPersistentId = optionalJsonI64(payload, "source_world_item_persistent_id", -1);
  packet.worldItemPersistentId = optionalJsonI64(payload, "world_item_persistent_id", -1);
  packet.vendorItemPersistentId = optionalJsonI64(payload, "vendor_item_persistent_id", -1);
  packet.sellerItemPersistentId = optionalJsonI64(payload, "seller_item_persistent_id", -1);
  packet.amount = optionalJsonI64(payload, "amount", 1);
  packet.slot = optionalJsonI64(payload, "slot", 0);
  packet.bagIndex = optionalJsonI64(payload, "server_bag_index", optionalJsonI64(payload, "bag_index", -1));
  packet.targetBagIndex = optionalJsonI64(payload, "target_bag_index", -1);
  packet.unitPrice = optionalJsonI64(payload, "unit_price", 0);
  packet.priceTotal = optionalJsonI64(payload, "price_total", 0);
  packet.walletBefore = optionalJsonI64(payload, "wallet_before", 0);
  packet.walletAfter = optionalJsonI64(payload, "wallet_after", 0);
  packet.slotId = optionalJsonU64(payload, "slot_id", 0);
  packet.vobId = optionalJsonU64(payload, "vob_id", 0);

  if(fillVec3FromObject(payload, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ) ||
     fillVec3FromObject(payload, "looter_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ) ||
     fillVec3FromObject(payload, "buyer_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ) ||
     fillVec3FromObject(payload, "seller_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ)) {
    packet.flags |= Net::ClientInventoryHasActorPosition;
  }
  if(fillVec3FromObject(payload, "item_position", packet.itemPosX, packet.itemPosY, packet.itemPosZ))
    packet.flags |= Net::ClientInventoryHasItemPosition;
  if(fillVec3FromObject(payload, "source_npc_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ) ||
     fillVec3FromObject(payload, "source_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ)) {
    packet.flags |= Net::ClientInventoryHasSourcePosition;
  }

  if(jsonBoolField(payload, "moved_whole_instance").value_or(false))
    packet.flags |= Net::ClientInventoryMovedWholeInstance;
  if(jsonBoolField(payload, "source_dead").value_or(false))
    packet.flags |= Net::ClientInventorySourceDead;
  if(jsonBoolField(payload, "source_unconscious").value_or(false))
    packet.flags |= Net::ClientInventorySourceUnconscious;
  if(jsonBoolField(payload, "container").value_or(envelope.kind == SemanticActionKind::TakeContainerItem ||
                                                   envelope.kind == SemanticActionKind::PutContainerItem)) {
    packet.flags |= Net::ClientInventoryContainer;
  }
  if(!packet.equipmentSlot.empty())
    packet.flags |= Net::ClientInventoryHasEquipmentSlot;
  if(jsonNumberTextField(payload, "unit_price").has_value() ||
     jsonNumberTextField(payload, "price_total").has_value()) {
    packet.flags |= Net::ClientInventoryHasTradePrice;
  }
  if(jsonNumberTextField(payload, "wallet_before").has_value() ||
     jsonNumberTextField(payload, "wallet_after").has_value()) {
    packet.flags |= Net::ClientInventoryHasWallet;
  }

  return packet;
}

std::optional<Net::ClientWorldStatePacket> makeClientWorldStatePacket(const SemanticActionEnvelope& envelope,
                                                                      std::string_view sessionKey) {
  if(!Net::isWorldStatePacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientWorldStatePacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalFirstJsonString(payload, {"target_key", "interactive_key", "script_key", "quest_key"}, envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "speaker_key", "character_key"});
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.resourceKey = optionalJsonString(payload, "resource_key");
  packet.interactiveKey = optionalFirstJsonString(payload, {"interactive_key", "interactive_entity_key"});
  packet.entityKey = optionalFirstJsonString(payload, {"entity_key", "interactive_entity_key", "mover_key", "trigger_key"});
  packet.tag = optionalJsonString(payload, "tag");
  packet.focusName = optionalJsonString(payload, "focus_name");
  packet.displayName = optionalJsonString(payload, "display_name");
  packet.scheme = optionalJsonString(payload, "scheme");
  packet.stateBeforeName = optionalFirstJsonString(payload, {"state_before_name", "previous_weapon_state"});
  packet.stateAfterName = optionalFirstJsonString(payload, {"state_after_name", "state_after", "new_weapon_state"});
  packet.eventTypeName = optionalJsonString(payload, "event_type_name");
  packet.eventTarget = optionalJsonString(payload, "event_target");
  packet.eventEmitter = optionalJsonString(payload, "event_emitter");
  packet.triggerName = optionalJsonString(payload, "trigger_name");
  packet.triggerTargetName = optionalJsonString(payload, "target_name");
  packet.scriptKey = optionalFirstJsonString(payload, {"script_key", "global_key"});
  packet.globalKey = optionalJsonString(payload, "global_key");
  packet.symbolName = optionalJsonString(payload, "symbol_name");
  packet.scriptFunctionName = optionalJsonString(payload, "script_function_name");
  packet.questKey = optionalJsonString(payload, "quest_key");
  packet.questName = optionalJsonString(payload, "quest_name");
  packet.status = optionalJsonString(payload, "status");
  packet.npcKey = optionalJsonString(payload, "npc_key");
  packet.npcSymbolName = optionalJsonString(payload, "npc_symbol_name");
  packet.infoKey = optionalJsonString(payload, "info_key");
  packet.infoSymbolName = optionalJsonString(payload, "info_symbol_name");
  packet.conversationKey = optionalJsonString(payload, "conversation_key");
  packet.syncGroup = optionalJsonString(payload, "sync_group");
  packet.speakerKey = optionalJsonString(payload, "speaker_key");
  packet.listenerKey = optionalJsonString(payload, "listener_key");
  packet.outputName = optionalJsonString(payload, "output_name");
  packet.messageName = optionalJsonString(payload, "message_name");
  packet.subtitleText = optionalJsonString(payload, "subtitle_text");

  packet.valueBefore = optionalJsonI64(payload, "value_before", optionalJsonI64(payload, "level_before", 0));
  packet.valueAfter = optionalJsonI64(payload, "value_after", optionalJsonI64(payload, "level_after", 0));
  packet.valueDelta = optionalJsonI64(payload, "delta_amount", optionalJsonI64(payload, "level_delta", packet.valueAfter - packet.valueBefore));
  packet.secondaryBefore = optionalJsonI64(payload, "experience_before", optionalJsonI64(payload, "world_time_before_ms", 0));
  packet.secondaryAfter = optionalJsonI64(payload, "experience_after", optionalJsonI64(payload, "world_time_after_ms", 0));
  packet.secondaryDelta = optionalJsonI64(payload, "experience_delta", optionalJsonI64(payload, "time_delta_ms", packet.secondaryAfter - packet.secondaryBefore));
  packet.tertiaryBefore = optionalJsonI64(payload, "learning_points_before", 0);
  packet.tertiaryAfter = optionalJsonI64(payload, "learning_points_after", 0);
  packet.tertiaryDelta = optionalJsonI64(payload, "learning_points_delta", packet.tertiaryAfter - packet.tertiaryBefore);
  packet.symbolIndex = optionalJsonI64(payload, "symbol_index", -1);
  packet.valueIndex = optionalJsonI64(payload, "value_index", -1);
  packet.scriptFunctionSymbol = optionalJsonI64(payload, "script_function_symbol", -1);
  packet.npcSymbol = optionalJsonI64(payload, "npc_symbol", -1);
  packet.infoSymbol = optionalJsonI64(payload, "info_symbol", -1);
  packet.entryCount = optionalJsonI64(payload, "entry_count", 0);
  packet.durationMs = optionalJsonI64(payload, "line_duration_ms", 0);
  packet.worldTimeBeforeMs = optionalJsonI64(payload, "world_time_before_ms", 0);
  packet.worldTimeAfterMs = optionalJsonI64(payload, "world_time_after_ms", 0);
  packet.worldDayBefore = optionalJsonI64(payload, "world_day_before", 0);
  packet.worldDayAfter = optionalJsonI64(payload, "world_day_after", 0);
  packet.worldHourBefore = optionalJsonI64(payload, "world_hour_before", 0);
  packet.worldHourAfter = optionalJsonI64(payload, "world_hour_after", 0);
  packet.worldMinuteBefore = optionalJsonI64(payload, "world_minute_before", 0);
  packet.worldMinuteAfter = optionalJsonI64(payload, "world_minute_after", 0);
  packet.eventType = optionalJsonI64(payload, "event_type", 0);
  packet.stateBefore = optionalJsonI64(payload, "state_before", optionalJsonI64(payload, "previous_weapon_state_id", 0));
  packet.stateAfter = optionalJsonI64(payload, "state_after", optionalJsonI64(payload, "new_weapon_state_id", 0));
  packet.frame = optionalJsonI64(payload, "frame", 0);
  packet.targetFrame = optionalJsonI64(payload, "target_frame", 0);
  packet.stateCount = optionalJsonI64(payload, "state_count", 0);
  packet.stateMask = optionalJsonI64(payload, "state_mask", 0);
  packet.slotId = optionalJsonI64(payload, "slot_id", 0);
  packet.vobId = optionalJsonI64(payload, "vob_id", optionalJsonI64(payload, "mover_vob_id", optionalJsonI64(payload, "trigger_vob_id", 0)));

  if(fillVec3FromObject(payload, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ) ||
     fillVec3FromObject(payload, "speaker_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ)) {
    packet.flags |= Net::ClientWorldStateHasActorPosition;
  }
  if(fillVec3FromObject(payload, "target_position", packet.targetPosX, packet.targetPosY, packet.targetPosZ))
    packet.flags |= Net::ClientWorldStateHasTargetPosition;
  if(fillVec3FromObject(payload, "source_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ) ||
     fillVec3FromObject(payload, "listener_position", packet.sourcePosX, packet.sourcePosY, packet.sourcePosZ)) {
    packet.flags |= Net::ClientWorldStateHasSourcePosition;
  }

  if(jsonBoolField(payload, "known").value_or(false))
    packet.flags |= Net::ClientWorldStateKnown;
  if(jsonBoolField(payload, "removed").value_or(false))
    packet.flags |= Net::ClientWorldStateRemoved;
  if(jsonBoolField(payload, "locked_before").value_or(false))
    packet.flags |= Net::ClientWorldStateLockedBefore;
  if(jsonBoolField(payload, "locked_after").value_or(false))
    packet.flags |= Net::ClientWorldStateLockedAfter;
  if(jsonBoolField(payload, "cracked_before").value_or(false))
    packet.flags |= Net::ClientWorldStateCrackedBefore;
  if(jsonBoolField(payload, "cracked_after").value_or(false))
    packet.flags |= Net::ClientWorldStateCrackedAfter;
  if(jsonBoolField(payload, "container").value_or(false))
    packet.flags |= Net::ClientWorldStateContainer;
  if(jsonBoolField(payload, "door").value_or(false))
    packet.flags |= Net::ClientWorldStateDoor;
  if(jsonBoolField(payload, "ladder").value_or(false))
    packet.flags |= Net::ClientWorldStateLadder;
  if(jsonBoolField(payload, "ready").value_or(envelope.kind == SemanticActionKind::ReadyWeapon))
    packet.flags |= Net::ClientWorldStateReady;

  return packet;
}

std::optional<Net::ClientNpcStatePacket> makeClientNpcStatePacket(const SemanticActionEnvelope& envelope,
                                                                  std::string_view sessionKey) {
  if(!Net::isNpcStatePacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientNpcStatePacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalFirstJsonString(payload, {"target_key", "npc_entity_key", "actor_npc_entity_key"}, envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.reason = optionalJsonString(payload, "reason");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "actor_npc_entity_key", "actor_npc_key"});
  packet.npcEntityKey = optionalFirstJsonString(payload, {"npc_entity_key", "actor_npc_entity_key", "target_npc_entity_key"}, packet.targetKey);
  packet.npcKey = optionalFirstJsonString(payload, {"npc_key", "actor_npc_key", "target_npc_key"});
  packet.targetNpcEntityKey = optionalFirstJsonString(payload, {"target_npc_entity_key", "target_world_entity_key"});
  packet.targetNpcKey = optionalJsonString(payload, "target_npc_key");
  packet.sourceNpcEntityKey = optionalFirstJsonString(payload, {"source_npc_entity_key", "source_actor_entity_key"});
  packet.sourceActorKey = optionalFirstJsonString(payload, {"source_actor_key", "source_actor_entity_key"});
  packet.displayName = optionalFirstJsonString(payload, {"display_name", "actor_npc_display_name"});
  packet.targetDisplayName = optionalJsonString(payload, "target_npc_display_name");
  packet.world = optionalJsonString(payload, "world");
  packet.routineState = optionalJsonString(payload, "routine_state");
  packet.scheduleKey = optionalJsonString(payload, "schedule_key");
  packet.currentWaypointKey = optionalJsonString(payload, "current_waypoint_key");
  packet.currentWaypointName = optionalJsonString(payload, "current_waypoint_name");
  packet.currentWaypointLegacy = optionalJsonString(payload, "current_waypoint");
  packet.targetWaypointKey = optionalJsonString(payload, "target_waypoint_key");
  packet.targetWaypointName = optionalJsonString(payload, "target_waypoint_name");
  packet.targetWaypointLegacy = optionalJsonString(payload, "target_waypoint");
  packet.nextWaypointKey = optionalJsonString(payload, "next_waypoint_key");
  packet.nextWaypointName = optionalJsonString(payload, "next_waypoint_name");
  packet.nextWaypointLegacy = optionalJsonString(payload, "next_waypoint");
  packet.aiState = optionalFirstJsonString(payload, {"ai_state", "ai_state_name"});
  packet.aiIntent = optionalFirstJsonString(payload, {"ai_intent", "intent"});
  packet.aiTargetKey = optionalJsonString(payload, "ai_target_key");
  packet.perceptionState = optionalJsonString(payload, "perception_state");
  packet.actionKey = optionalJsonString(payload, "action_key");
  packet.actionName = optionalJsonString(payload, "action_name");
  packet.actionState = optionalFirstJsonString(payload, {"action_state", "state"});
  packet.actionTargetKey = optionalFirstJsonString(payload, {"action_target_key", "target_entity_key"});
  packet.syncGroup = optionalFirstJsonString(payload, {"sync_group", "conversation_key"});
  packet.pathState = optionalJsonString(payload, "path_state");
  packet.routeKey = optionalJsonString(payload, "route_key");
  packet.moveHint = optionalJsonString(payload, "move_hint");
  packet.opponentKey = optionalFirstJsonString(payload, {"opponent_key", "target_entity_key", "target_key"});
  packet.fightState = optionalJsonString(payload, "fight_state");
  packet.attackState = optionalJsonString(payload, "attack_state");
  packet.combatAction = optionalJsonString(payload, "combat_action");
  packet.intentState = optionalJsonString(payload, "intent_state");
  packet.weaponState = optionalJsonString(payload, "weapon_state");
  packet.animationName = optionalJsonString(payload, "animation_name");
  packet.attackAnimationName = optionalJsonString(payload, "attack_animation_name");

  packet.npcPersistentId = optionalJsonI64(payload, "npc_persistent_id", optionalJsonI64(payload, "actor_npc_persistent_id", -1));
  packet.npcSymbol = optionalJsonI64(payload, "npc_symbol", optionalJsonI64(payload, "actor_npc_symbol", -1));
  packet.targetNpcPersistentId = optionalJsonI64(payload, "target_npc_persistent_id", -1);
  packet.targetNpcSymbol = optionalJsonI64(payload, "target_npc_symbol", -1);
  packet.sourceNpcPersistentId = optionalJsonI64(payload, "source_npc_persistent_id", optionalJsonI64(payload, "source_actor_persistent_id", -1));
  packet.sourceNpcSymbol = optionalJsonI64(payload, "source_npc_symbol", optionalJsonI64(payload, "source_actor_symbol", -1));
  packet.healthCurrent = optionalJsonI64(payload, "health_current", 0);
  packet.healthMax = optionalJsonI64(payload, "health_max", 0);
  packet.aiStateFunction = optionalJsonI64(payload, "ai_state_function", 0);
  packet.remainingPathPoints = optionalJsonI64(payload, "remaining_path_points", 0);
  packet.comboIndex = optionalJsonI64(payload, "combo_index", 0);
  packet.bodyState = optionalJsonI64(payload, "body_state", 0);
  packet.weaponStateId = optionalJsonI64(payload, "weapon_state_id", 0);
  packet.animationElapsedMs = optionalJsonI64(payload, "animation_elapsed_ms", 0);
  packet.attackAnimationElapsedMs = optionalJsonI64(payload, "attack_animation_elapsed_ms", 0);
  packet.animationTotalMs = optionalJsonI64(payload, "animation_total_ms", 0);
  packet.attackTotalMs = optionalJsonI64(payload, "attack_total_ms", 0);
  packet.attackOptimalMs = optionalJsonI64(payload, "attack_optimal_ms", 0);
  packet.attackHitEndMs = optionalJsonI64(payload, "attack_hit_end_ms", 0);
  packet.parryWindowStartMs = optionalJsonI64(payload, "parry_window_start_ms", 0);
  packet.parryWindowEndMs = optionalJsonI64(payload, "parry_window_end_ms", 0);
  packet.comboWindowStartMs = optionalJsonI64(payload, "combo_window_start_ms", 0);
  packet.comboWindowEndMs = optionalJsonI64(payload, "combo_window_end_ms", 0);
  packet.attackerYawRad = optionalJsonDouble(payload, "attacker_yaw_rad", 0.0);
  packet.opponentYawRad = optionalJsonDouble(payload, "opponent_yaw_rad", 0.0);
  packet.weaponRange = optionalJsonDouble(payload, "weapon_range", 0.0);
  packet.attackRange = optionalJsonDouble(payload, "attack_range", 0.0);
  packet.opponentAttackRange = optionalJsonDouble(payload, "opponent_attack_range", 0.0);
  packet.attackerFightRangeBase = optionalJsonDouble(payload, "attacker_fight_range_base", 0.0);
  packet.opponentFightRangeBase = optionalJsonDouble(payload, "opponent_fight_range_base", 0.0);

  if(fillVec3FromObject(payload, "position", packet.posX, packet.posY, packet.posZ) ||
     fillVec3FromObject(payload, "actor_position", packet.posX, packet.posY, packet.posZ)) {
    packet.flags |= Net::ClientNpcStateHasPosition;
  } else if(jsonNumberTextField(payload, "pos_x").has_value()) {
    packet.posX = optionalJsonDouble(payload, "pos_x", 0.0);
    packet.posY = optionalJsonDouble(payload, "pos_y", 0.0);
    packet.posZ = optionalJsonDouble(payload, "pos_z", 0.0);
    packet.flags |= Net::ClientNpcStateHasPosition;
  }
  if(fillVec3FromObject(payload, "target_position", packet.targetPosX, packet.targetPosY, packet.targetPosZ))
    packet.flags |= Net::ClientNpcStateHasTargetPosition;
  if(fillVec3FromObject(payload, "attacker_center", packet.attackerCenterX, packet.attackerCenterY, packet.attackerCenterZ))
    packet.flags |= Net::ClientNpcStateHasAttackerCenter;
  if(fillVec3FromObject(payload, "opponent_center", packet.opponentCenterX, packet.opponentCenterY, packet.opponentCenterZ))
    packet.flags |= Net::ClientNpcStateHasOpponentCenter;
  if(fillVec3FromObject(payload, "fight_distance", packet.fightDistanceX, packet.fightDistanceY, packet.fightDistanceZ))
    packet.flags |= Net::ClientNpcStateHasFightDistance;

  if(jsonBoolField(payload, "dead").value_or(false))
    packet.flags |= Net::ClientNpcStateDead;
  if(jsonBoolField(payload, "unconscious").value_or(false))
    packet.flags |= Net::ClientNpcStateUnconscious;
  if(jsonBoolField(payload, "down").value_or(false))
    packet.flags |= Net::ClientNpcStateDown;
  if(jsonBoolField(payload, "attack_anim").value_or(false))
    packet.flags |= Net::ClientNpcStateAttackAnim;
  if(jsonBoolField(payload, "prehit").value_or(false))
    packet.flags |= Net::ClientNpcStatePrehit;
  if(jsonBoolField(payload, "actor_running").value_or(false))
    packet.flags |= Net::ClientNpcStateActorRunning;
  if(jsonBoolField(payload, "opponent_running").value_or(false))
    packet.flags |= Net::ClientNpcStateOpponentRunning;
  if(jsonBoolField(payload, "opponent_prehit").value_or(false))
    packet.flags |= Net::ClientNpcStateOpponentPrehit;

  return packet;
}

std::optional<Net::ClientEconomyPacket> makeClientEconomyPacket(const SemanticActionEnvelope& envelope,
                                                                std::string_view sessionKey) {
  if(!Net::isEconomyPacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientEconomyPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalJsonString(payload, "target_key", envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "character_key"});
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.currencyKey = optionalJsonString(payload, "currency_key", "g2notr:gold");
  packet.currencyDisplayName = optionalJsonString(payload, "currency_display_name", "Gold");
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.amount = optionalJsonI64(payload, "amount", 0);
  packet.deltaAmount = optionalJsonI64(payload, "delta_amount",
                       optionalJsonI64(payload, "delta",
                       envelope.kind == SemanticActionKind::SpendGold ? -packet.amount : packet.amount));
  packet.walletBefore = optionalJsonI64(payload, "wallet_before", optionalJsonI64(payload, "value_before", 0));
  packet.walletAfter = optionalJsonI64(payload, "wallet_after", optionalJsonI64(payload, "value_after", packet.walletBefore + packet.deltaAmount));
  packet.itemTemplateSymbol = optionalJsonI64(payload, "item_template_symbol", -1);
  if(fillVec3FromObject(payload, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ))
    packet.flags |= Net::ClientEconomyHasActorPosition;
  if(jsonNumberTextField(payload, "wallet_before").has_value() || jsonNumberTextField(payload, "value_before").has_value())
    packet.flags |= Net::ClientEconomyHasWalletBefore;
  if(jsonNumberTextField(payload, "wallet_after").has_value() || jsonNumberTextField(payload, "value_after").has_value())
    packet.flags |= Net::ClientEconomyHasWalletAfter;

  return packet;
}

std::optional<Net::ClientSessionControlPacket> makeClientSessionControlPacket(const SemanticActionEnvelope& envelope,
                                                                              std::string_view sessionKey) {
  if(!Net::isSessionControlPacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientSessionControlPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalJsonString(payload, "target_key", envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.sourceLocation = optionalJsonString(payload, "source_location", packet.source);
  packet.actorKey = optionalJsonString(payload, "actor_key");
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.displayName = optionalJsonString(payload, "display_name");
  packet.world = optionalJsonString(payload, "world");
  packet.serverEndpoint = optionalJsonString(payload, "server_endpoint");
  packet.clientContentManifestHash = optionalJsonString(payload, "client_content_manifest_hash");
  packet.reason = optionalJsonString(payload, "reason");
  packet.actionKind = optionalJsonString(payload, "action_kind");
  packet.manifestKey = optionalJsonString(payload, "manifest_key");
  packet.checkpointKind = optionalJsonString(payload, "checkpoint_kind");
  packet.saveSlotKey = optionalJsonString(payload, "save_slot_key");
  packet.slotPath = optionalJsonString(payload, "slot_path");
  packet.nativeSavePath = optionalJsonString(payload, "native_save_path");
  packet.slotDisplayName = optionalJsonString(payload, "slot_display_name");
  packet.clientWorldName = optionalJsonString(payload, "client_world_name", packet.world);
  packet.serverTick = optionalJsonI64(payload, "server_tick", static_cast<std::int64_t>(envelope.clientTick));
  packet.acknowledgedLocalSequence = optionalJsonI64(payload, "client_local_sequence", 0);

  if(jsonBoolField(payload, "server_bound_client_mode").value_or(envelope.kind == SemanticActionKind::ClientBootstrapRequest))
    packet.flags |= Net::ClientSessionControlServerBoundClientMode;
  if(jsonBoolField(payload, "native_save_present").value_or(false))
    packet.flags |= Net::ClientSessionControlNativeSavePresent;
  if(jsonBoolField(payload, "db_save_snapshot_requested").value_or(false))
    packet.flags |= Net::ClientSessionControlDbSaveSnapshotRequested;

  return packet;
}

std::optional<Net::ClientDialogStatePacket> makeClientDialogStatePacket(const SemanticActionEnvelope& envelope,
                                                                        std::string_view sessionKey) {
  if(!Net::isDialogStatePacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientDialogStatePacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalJsonString(payload, "target_key", envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalJsonString(payload, "actor_key");
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.npcKey = optionalFirstJsonString(payload, {"npc_key", "speaker_npc_key", "speaker_key"});
  packet.npcSymbolName = optionalFirstJsonString(payload, {"npc_symbol_name", "speaker_symbol_name"});
  packet.infoKey = optionalFirstJsonString(payload, {"info_key", "dialog_key", "known_dialog_key"});
  packet.infoSymbolName = optionalFirstJsonString(payload, {"info_symbol_name", "dialog_symbol_name"});
  packet.conversationKey = optionalFirstJsonString(payload, {"conversation_key", "dialog_session_key"});
  packet.syncGroup = optionalFirstJsonString(payload, {"sync_group", "conversation_key"});
  packet.speakerKey = optionalFirstJsonString(payload, {"speaker_key", "npc_key"});
  packet.listenerKey = optionalFirstJsonString(payload, {"listener_key", "character_key"});
  packet.outputName = optionalJsonString(payload, "output_name");
  packet.messageName = optionalFirstJsonString(payload, {"message_name", "line_name"});
  packet.subtitleText = optionalFirstJsonString(payload, {"subtitle_text", "text", "line_text"});
  packet.dialogState = optionalFirstJsonString(payload, {"dialog_state", "state", "status"});
  packet.topicKey = optionalFirstJsonString(payload, {"topic_key", "topic"});

  packet.npcSymbol = optionalJsonI64(payload, "npc_symbol", -1);
  packet.infoSymbol = optionalJsonI64(payload, "info_symbol", -1);
  packet.lineIndex = optionalJsonI64(payload, "line_index", -1);
  packet.outputIndex = optionalJsonI64(payload, "output_index", -1);
  packet.durationMs = optionalJsonI64(payload, "duration_ms", 0);

  if(jsonBoolField(payload, "known").value_or(envelope.kind == SemanticActionKind::SetKnownDialog))
    packet.flags |= Net::ClientDialogStateKnown;
  if(jsonBoolField(payload, "player_line").value_or(false))
    packet.flags |= Net::ClientDialogStatePlayerLine;
  if(jsonBoolField(payload, "npc_line").value_or(envelope.kind == SemanticActionKind::RecordNpcDialogLine))
    packet.flags |= Net::ClientDialogStateNpcLine;
  if(jsonBoolField(payload, "important").value_or(false))
    packet.flags |= Net::ClientDialogStateImportant;
  if(jsonBoolField(payload, "ambient").value_or(false))
    packet.flags |= Net::ClientDialogStateAmbient;
  if(jsonNumberTextField(payload, "line_index").has_value())
    packet.flags |= Net::ClientDialogStateHasLineIndex;
  if(jsonNumberTextField(payload, "output_index").has_value())
    packet.flags |= Net::ClientDialogStateHasOutputIndex;

  return packet;
}

std::optional<Net::ClientCharacterEventPacket> makeClientCharacterEventPacket(const SemanticActionEnvelope& envelope,
                                                                              std::string_view sessionKey) {
  if(!Net::isCharacterEventPacketAction(envelope.kind))
    return std::nullopt;

  const std::string_view payload = envelope.payloadJson;
  if(payload.empty() || payload.front() != '{')
    return std::nullopt;

  Net::ClientCharacterEventPacket packet;
  packet.kind = envelope.kind;
  packet.packetSequence = envelope.localSequence;
  packet.clientTick = envelope.clientTick;
  packet.localSequence = envelope.localSequence;
  packet.flags = Net::ClientCharacterEventServerMustCalculate;
  packet.sessionKey = std::string(sessionKey);
  packet.targetKey = optionalFirstJsonString(payload, {"target_key", "character_key", "resource_key", "progression_key"}, envelope.targetKey);
  packet.idempotencyKey = envelope.idempotencyKey;
  packet.source = optionalJsonString(payload, "source");
  packet.actorKey = optionalFirstJsonString(payload, {"actor_key", "character_key"});
  packet.characterKey = optionalJsonString(payload, "character_key");
  packet.world = optionalJsonString(payload, "world");
  packet.reason = optionalJsonString(payload, "reason");
  packet.resourceKey = optionalJsonString(payload,
                                          "resource_key",
                                          envelope.kind == SemanticActionKind::ConsumeMana ? "mana" : "");
  packet.resourceDisplayName = optionalJsonString(payload,
                                                  "resource_display_name",
                                                  packet.resourceKey == "mana" ? "Mana" : "");
  packet.progressionKey = optionalFirstJsonString(payload, {"progression_key", "skill_key", "attribute_key"});
  packet.rewardKey = optionalFirstJsonString(payload, {"reward_key", "reward_source", "experience_reward_key"});
  packet.sourceActorKey = optionalFirstJsonString(payload, {"source_actor_key", "source_npc_key"});
  packet.sourceEntityKey = optionalFirstJsonString(payload, {"source_entity_key", "source_actor_entity_key"});

  packet.requestedDelta = optionalJsonI64(payload,
                                          "requested_delta",
                                          optionalJsonI64(payload,
                                                          "delta_amount",
                                                          optionalJsonI64(payload, "delta", 0)));
  packet.requestedAmount = optionalJsonI64(payload, "amount", packet.requestedDelta);
  packet.manaAmount = optionalJsonI64(payload, "mana_amount", optionalJsonI64(payload, "mana_cost", packet.requestedAmount));
  packet.experienceReward = optionalJsonI64(payload,
                                            "reward_experience",
                                            optionalJsonI64(payload,
                                                            "experience_reward",
                                                            optionalJsonI64(payload, "experience_delta", packet.requestedDelta)));
  packet.learningPointsReward = optionalJsonI64(payload, "reward_learning_points",
                                                optionalJsonI64(payload, "learning_points_delta", 0));
  packet.attributeSymbol = optionalJsonI64(payload, "attribute_symbol", -1);
  packet.skillSymbol = optionalJsonI64(payload, "skill_symbol", -1);

  if(fillVec3FromObject(payload, "actor_position", packet.actorPosX, packet.actorPosY, packet.actorPosZ))
    packet.flags |= Net::ClientCharacterEventHasActorPosition;
  if(jsonNumberTextField(payload, "requested_delta").has_value() ||
     jsonNumberTextField(payload, "delta_amount").has_value() ||
     jsonNumberTextField(payload, "delta").has_value())
    packet.flags |= Net::ClientCharacterEventHasRequestedDelta;
  if(jsonNumberTextField(payload, "amount").has_value())
    packet.flags |= Net::ClientCharacterEventHasRequestedAmount;
  if(envelope.kind == SemanticActionKind::ConsumeMana ||
     jsonNumberTextField(payload, "mana_amount").has_value() ||
     jsonNumberTextField(payload, "mana_cost").has_value())
    packet.flags |= Net::ClientCharacterEventHasManaAmount;
  if(envelope.kind == SemanticActionKind::ApplyExperienceReward ||
     jsonNumberTextField(payload, "reward_experience").has_value() ||
     jsonNumberTextField(payload, "experience_reward").has_value())
    packet.flags |= Net::ClientCharacterEventHasExperienceReward;
  if(jsonNumberTextField(payload, "reward_learning_points").has_value() ||
     jsonNumberTextField(payload, "learning_points_delta").has_value())
    packet.flags |= Net::ClientCharacterEventHasLearningReward;
  if(!packet.resourceKey.empty() || envelope.kind == SemanticActionKind::ApplyCharacterResourceDelta)
    packet.flags |= Net::ClientCharacterEventHasExplicitResource;

  return packet;
}

constexpr std::size_t MaxQueuedServerLiveDeltas = 512;
std::mutex serverLiveDeltaMutex;
std::vector<Net::ServerLiveDeltaPacket> serverLiveDeltaInbox;

const char* liveDeltaDomainName(Net::ServerLiveDeltaKind kind) noexcept {
  switch(kind) {
    case Net::ServerLiveDeltaKind::MovementCorrection: return "movement";
    case Net::ServerLiveDeltaKind::CharacterStats:     return "character";
    case Net::ServerLiveDeltaKind::Inventory:          return "inventory";
    case Net::ServerLiveDeltaKind::Equipment:          return "equipment";
    case Net::ServerLiveDeltaKind::WorldItem:          return "world_item";
    case Net::ServerLiveDeltaKind::InteractiveState:   return "interactive";
    case Net::ServerLiveDeltaKind::Combat:             return "combat";
    case Net::ServerLiveDeltaKind::Story:              return "story";
    case Net::ServerLiveDeltaKind::PerceptionReaction: return "npc_perception";
    case Net::ServerLiveDeltaKind::Generic:            return "generic";
  }
  return "generic";
}

std::string liveDeltaDebugJson(const Net::ServerLiveDeltaPacket& delta) {
  std::string out;
  out.reserve(delta.debugJson.size() + delta.actionKind.size() + 512);
  out += "{\"schema\":\"mmo.client_live_delta_debug.v1\"";
  out += ",\"kind\":";
  out += std::to_string(static_cast<unsigned>(delta.kind));
  out += ",\"domain\":";
  out += jsonEscape(liveDeltaDomainName(delta.kind));
  out += ",\"action_kind\":";
  out += jsonEscape(delta.actionKind);
  out += ",\"packet_sequence\":";
  out += std::to_string(delta.packetSequence);
  out += ",\"local_sequence\":";
  out += std::to_string(delta.localSequence);
  out += ",\"server_tick\":";
  out += std::to_string(delta.serverTick);
  out += ",\"flags\":";
  out += std::to_string(delta.flags);
  out += ",\"has_position\":";
  out += ((delta.flags & Net::ServerLiveDeltaHasPosition) != 0) ? "true" : "false";
  out += ",\"has_stats\":";
  out += ((delta.flags & Net::ServerLiveDeltaHasStats) != 0) ? "true" : "false";
  out += ",\"requires_snapshot_refresh\":";
  out += ((delta.flags & Net::ServerLiveDeltaRequiresSnapshotRefresh) != 0) ? "true" : "false";
  out += ",\"position\":{\"x\":";
  out += std::to_string(delta.posX);
  out += ",\"y\":";
  out += std::to_string(delta.posY);
  out += ",\"z\":";
  out += std::to_string(delta.posZ);
  out += ",\"yaw\":";
  out += std::to_string(delta.yaw);
  out += "},\"stats\":{\"level\":";
  out += std::to_string(delta.level);
  out += ",\"experience\":";
  out += std::to_string(delta.experience);
  out += ",\"experience_next\":";
  out += std::to_string(delta.experienceNext);
  out += ",\"learning_points\":";
  out += std::to_string(delta.learningPoints);
  out += ",\"health_current\":";
  out += std::to_string(delta.healthCurrent);
  out += ",\"health_max\":";
  out += std::to_string(delta.healthMax);
  out += ",\"mana_current\":";
  out += std::to_string(delta.manaCurrent);
  out += ",\"mana_max\":";
  out += std::to_string(delta.manaMax);
  out += ",\"strength\":";
  out += std::to_string(delta.strength);
  out += ",\"dexterity\":";
  out += std::to_string(delta.dexterity);
  out += ",\"guild\":";
  out += std::to_string(delta.guild);
  out += ",\"true_guild\":";
  out += std::to_string(delta.trueGuild);
  out += "}";
  if(!delta.debugJson.empty()) {
    out += ",\"debug_payload\":";
    out += delta.debugJson;
  }
  out += "}";
  return out;
}

void enqueueServerLiveDelta(Net::ServerLiveDeltaPacket delta) noexcept {
  std::lock_guard<std::mutex> lock(serverLiveDeltaMutex);
  if(serverLiveDeltaInbox.size() >= MaxQueuedServerLiveDeltas)
    serverLiveDeltaInbox.erase(serverLiveDeltaInbox.begin());
  serverLiveDeltaInbox.emplace_back(std::move(delta));
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
          if(auto combatDamage = makeClientCombatDamagePacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientCombatDamagePacket(*combatDamage);
          else if(auto movement = makeClientMovementPacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientMovementPacket(*movement);
          else if(auto inventory = makeClientInventoryPacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientInventoryPacket(*inventory);
          else if(auto dialogState = makeClientDialogStatePacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientDialogStatePacket(*dialogState);
          else if(auto characterEvent = makeClientCharacterEventPacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientCharacterEventPacket(*characterEvent);
          else if(auto worldState = makeClientWorldStatePacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientWorldStatePacket(*worldState);
          else if(auto npcState = makeClientNpcStatePacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientNpcStatePacket(*npcState);
          else if(auto economy = makeClientEconomyPacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientEconomyPacket(*economy);
          else if(auto sessionControl = makeClientSessionControlPacket(envelope, configuredSessionKey))
            action.serverPacket = Net::encodeClientSessionControlPacket(*sessionControl);
          else
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
      std::uint64_t contentManifestDiagnostics = 0;
      std::uint64_t snapshotChunks = 0;
      std::uint64_t snapshotBytes = 0;
      std::uint64_t liveDeltas = 0;
      std::uint64_t liveDeltaBytes = 0;
      std::uint64_t lastLiveDeltaSeq = 0;
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

      const auto error = optionalJsonString(message, "error");
      if(error != "client_content_manifest_rejected")
        return out;

      out.structured = true;
      out.reason = optionalJsonString(message, "reason", out.reason);
      out.phase = optionalJsonString(message, "phase");
      out.clientHash = optionalJsonString(message, "client_content_manifest_hash");
      out.serverRequiredHash = optionalJsonString(message, "server_required_content_hash");
      out.contentRevisionKey = optionalJsonString(message, "content_revision_key");
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
          logServerDiagnostic(diag.diagnostic);
          continue;
        }

        if(auto delta = Net::decodeServerLiveDeltaPacket(packet); delta.ok()) {
          ++serverStats.liveDeltas;
          serverStats.liveDeltaBytes += static_cast<std::uint64_t>(delta.liveDelta.debugJson.size());
          serverStats.lastLiveDeltaSeq = delta.liveDelta.packetSequence;
          acceptLiveDelta(std::move(delta.liveDelta));
          maybeLogServerAckSummary(false);
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
                      " content_manifest_diagnostics=", serverStats.contentManifestDiagnostics,
                      " snapshot_chunks=", serverStats.snapshotChunks,
                      " snapshot_bytes=", serverStats.snapshotBytes,
                      " live_deltas=", serverStats.liveDeltas,
                      " live_delta_bytes=", serverStats.liveDeltaBytes,
                      " last_live_delta_seq=", serverStats.lastLiveDeltaSeq,
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
            << "  \"live_deltas_seen\": " << serverStats.liveDeltas << "\n"
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

    void acceptLiveDelta(Net::ServerLiveDeltaPacket delta) noexcept {
      try {
        enqueueServerLiveDelta(delta);
        std::filesystem::create_directories("runtime");
        std::ofstream out("runtime/mmo_server_live_deltas.jsonl", std::ios::out | std::ios::app | std::ios::binary);
        if(!out.is_open()) {
          Tempest::Log::e("MMO server live delta: unable to open runtime/mmo_server_live_deltas.jsonl");
          return;
        }

        const auto envelope = liveDeltaDebugJson(delta);
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

std::vector<Net::ServerLiveDeltaPacket> drainServerLiveDeltas() noexcept {
  std::lock_guard<std::mutex> lock(serverLiveDeltaMutex);
  std::vector<Net::ServerLiveDeltaPacket> out;
  out.swap(serverLiveDeltaInbox);
  return out;
}

} // namespace Mmo



















