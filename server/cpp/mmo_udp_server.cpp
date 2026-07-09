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
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <iostream>
#include <limits>

#include "../../game/game/mmonetprotocol.h"
#include "mmo_server_snapshot_limits.h"
#include "mmo_server_types.h"
#include "mmo_server_identity.h"
#include "mmo_world_instance_content_cache.h"
#include "mmo_world_instance_ai_scheduler_boundary.h"
#include "mmo_content_build_active_read_model_selection.h"
#include "mmo_runtime_npc_identity_materialization_plan.h"
#include "mmo_outbound_gameplay_delivery_state.h"
#include "mmo_server_gameplay_fanout.h"
#include "mmo_server_conversation_session_boundary.h"
#include "mmo_server_conversation_resume_packet_boundary.h"
#include "mmo_server_conversation_resume_send_gate.h"
#include "mmo_server_conversation_resume_runtime_send.h"
#include "mmo_server_conversation_resume_delivery_registration_boundary.h"
#include "mmo_server_conversation_resume_dispatch_envelope.h"
#include "mmo_server_conversation_resume_mutation_guard.h"
#include "mmo_server_conversation_resume_send_failure_dead_letter_guard.h"
#include "mmo_server_conversation_resume_commit_preflight.h"
#include "mmo_ai_dialog_intent_delivery_persistence_bridge.h"
#include "mmo_ai_dialog_intent_delivery_persistence_execution_boundary.h"
#include "mmo_ai_dialog_intent_delivery_persistence_executor_adapter.h"
#include "mmo_ai_dialog_intent_delivery_persistence_dry_run_report_gate.h"
#include "mmo_ai_dialog_intent_delivery_persistence_outcome_policy.h"
#include "mmo_ai_dialog_intent_delivery_persistence_outcome_observability.h"
#include "mmo_ai_dialog_intent_delivery_persistence_activation_preflight.h"
#include "mmo_ai_dialog_intent_delivery_persistence_runtime_storage.h"
#include "mmo_ai_dialog_intent_delivery_mark_applied_gate.h"

namespace {

constexpr int DbBridgeVersion = 7;
constexpr std::string_view MysqlSessionPreamble =
    "SET SESSION group_concat_max_len=104857600; "
    "SET SESSION max_execution_time=0; ";
std::atomic_bool gRunning {true};

[[nodiscard]] FILE* openProcessPipe(const std::string& cmd) {
#if defined(_WIN32)
  return ::_popen(cmd.c_str(), "r");
#else
  return ::popen(cmd.c_str(), "r");
#endif
}

int closeProcessPipe(FILE* pipe) {
#if defined(_WIN32)
  return ::_pclose(pipe);
#else
  return ::pclose(pipe);
#endif
}

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

struct ClientRoutePosition final {
  bool valid = false;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct ClientRouteEntry final {
  asio::ip::udp::endpoint endpoint;
  std::string sessionUuid;
  std::string characterKey;
  std::string worldName;
  ClientRoutePosition position;
  std::uint64_t lastSeenAtMs = 0;
};

struct ClientRouteRegistry final {
  std::unordered_map<std::string, ClientRouteEntry> bySessionUuid;

  void upsert(std::string_view sessionUuid,
              const asio::ip::udp::endpoint& endpoint,
              std::string_view characterKey = {},
              std::string_view worldName = {},
              ClientRoutePosition position = {},
              std::uint64_t lastSeenAtMs = 0) {
    if(sessionUuid.empty())
      return;

    auto& entry = bySessionUuid[std::string(sessionUuid)];
    entry.endpoint = endpoint;
    entry.sessionUuid = std::string(sessionUuid);
    if(!characterKey.empty())
      entry.characterKey = std::string(characterKey);
    if(!worldName.empty())
      entry.worldName = std::string(worldName);
    if(position.valid)
      entry.position = position;
    if(lastSeenAtMs != 0)
      entry.lastSeenAtMs = lastSeenAtMs;
  }

  [[nodiscard]] std::optional<asio::ip::udp::endpoint> find(std::string_view sessionUuid) const {
    if(sessionUuid.empty())
      return std::nullopt;
    const auto it = bySessionUuid.find(std::string(sessionUuid));
    if(it == bySessionUuid.end())
      return std::nullopt;
    return it->second.endpoint;
  }

  [[nodiscard]] std::vector<Mmo::Server::GameplayFanoutObserver> observers() const {
    std::vector<Mmo::Server::GameplayFanoutObserver> out;
    out.reserve(bySessionUuid.size());
    for(const auto& [_, entry] : bySessionUuid) {
      Mmo::Server::GameplayFanoutObserver observer;
      observer.sessionUuid = entry.sessionUuid;
      observer.characterKey = entry.characterKey;
      observer.worldName = entry.worldName;
      observer.position = {entry.position.x, entry.position.y, entry.position.z};
      observer.lastSeenAtMs = entry.lastSeenAtMs;
      observer.hasPosition = entry.position.valid;
      observer.endpointAvailable = true;
      out.push_back(std::move(observer));
    }
    return out;
  }
};

[[nodiscard]] std::string endpointText(const asio::ip::udp::endpoint& endpoint) {
  return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

[[nodiscard]] bool isStep282LateObserverReplayActionId(std::string_view actionId) noexcept {
  constexpr std::string_view Prefix = "server-npc-dialog-resume-intent:";
  return actionId.size() >= Prefix.size() && actionId.substr(0, Prefix.size()) == Prefix;
}

[[nodiscard]] const char* gameplayAckStatusName(Mmo::Net::ClientGameplayAckStatus status) noexcept {
  switch(status) {
    case Mmo::Net::ClientGameplayAckStatus::Ack:  return "ack";
    case Mmo::Net::ClientGameplayAckStatus::Nack: return "nack";
  }
  return "unknown";
}

[[nodiscard]] const char* gameplayObservationStatusName(Mmo::Net::ClientGameplayObservationStatus status) noexcept {
  switch(status) {
    case Mmo::Net::ClientGameplayObservationStatus::Observed: return "observed";
    case Mmo::Net::ClientGameplayObservationStatus::Skipped:  return "skipped";
  }
  return "unknown";
}

[[nodiscard]] Mmo::Server::GameplayFanoutMode parseGameplayFanoutMode(std::string_view value) noexcept {
  if(value == "aoi" || value == "area" || value == "area_of_interest")
    return Mmo::Server::GameplayFanoutMode::AreaOfInterest;
  return Mmo::Server::GameplayFanoutMode::TargetSessionOnly;
}

[[nodiscard]] const char* gameplayFanoutModeConfigName(std::string_view value) noexcept {
  return Mmo::Server::gameplayFanoutModeName(parseGameplayFanoutMode(value));
}

[[nodiscard]] std::uint64_t monotonicNowMs() noexcept {
  using Clock = std::chrono::steady_clock;
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}


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
using Mmo::WorldInstanceContent::WorldInstanceContentCache;
using Mmo::WorldInstanceContent::WorldInstanceContentCacheOptions;
using Mmo::ContentBuild::ActiveReadModelSelectionRequest;
using Mmo::ContentBuild::ActiveReadModelSelectionResult;
using Mmo::NpcPerceptionRuntime::RuntimeNpcIdentityMaterializationPlan;
using Mmo::WorldInstanceAiTick::WorldInstanceAiSchedulerBoundaryOptions;
using Mmo::WorldInstanceAiTick::WorldInstanceAiSchedulerBoundaryResult;
using Mmo::WorldInstanceAiTick::WorldInstanceAiSchedulerPlan;
using Mmo::WorldInstanceAiTick::WorldInstanceAiSchedulerPlanOptions;

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

[[nodiscard]] std::uint64_t parseU64OrZero(std::string_view text) noexcept {
  std::uint64_t value = 0;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return 0;
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

[[nodiscard]] std::string normalizedWorldName(std::string_view worldName) {
  std::string out(worldName);
  for(char& ch : out) {
    if(ch == '/' || ch == '\\')
      ch = '.';
    else
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return out;
}

[[nodiscard]] std::string shellQuote(std::string_view text) {
#if defined(_WIN32)
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('"');
  std::size_t backslashes = 0;
  for(char ch : text) {
    if(ch == '\\') {
      ++backslashes;
      continue;
    }
    if(ch == '"') {
      out.append(backslashes * 2 + 1, '\\');
      out.push_back('"');
      backslashes = 0;
      continue;
    }
    out.append(backslashes, '\\');
    backslashes = 0;
    if(ch == '%')
      out += "%%";
    else
      out.push_back(ch);
  }
  out.append(backslashes * 2, '\\');
  out.push_back('"');
  return out;
#else
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('\'');
  for(char ch : text) {
    if(ch == '\'')
      out += "'\\''";
    else
      out.push_back(ch);
  }
  out.push_back('\'');
  return out;
#endif
}

[[nodiscard]] std::string mysqlExecutable() {
#if defined(_WIN32)
  if(const char* exe = std::getenv("GOTHIC_MMO_MYSQL_EXE"); exe != nullptr && *exe != '\0')
    return exe;
  if(const char* exe = std::getenv("MYSQL_EXE"); exe != nullptr && *exe != '\0')
    return exe;
#endif
  return "mysql";
}

#if defined(_WIN32)
[[nodiscard]] std::string cmdSetEnvPrefix(std::string_view name, std::string_view value) {
  std::string out = "set \"" + std::string(name) + "=";
  for(char ch : value) {
    if(ch == '%')
      out += "%%";
    else
      out.push_back(ch);
  }
  out += "\" && ";
  return out;
}
#endif

[[nodiscard]] std::string sqlLiteral(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  out.push_back('\'');
  for(char ch : text) {
    if(ch == '\\')
      out += "\\\\";
    else if(ch == '\'')
      out += "''";
    else
      out.push_back(ch);
  }
  out.push_back('\'');
  return out;
}

[[nodiscard]] const char* sqlBool(bool value) noexcept {
  return value ? "TRUE" : "FALSE";
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

[[nodiscard]] std::string optionalJsonPositionDoubleSql(std::string_view json,
                                                        std::string_view nestedKey,
                                                        std::string_view flatKey0,
                                                        std::string_view flatKey1,
                                                        std::string_view flatKey2) {
  if(auto item = jsonObjectField(json, "item_position")) {
    if(auto text = jsonNumberTextField(*item, nestedKey)) {
      if(auto value = parseDouble(*text))
        return std::to_string(finiteOrThrow(*value, nestedKey));
    }
  }
  if(auto actor = jsonObjectField(json, "actor_position")) {
    if(auto text = jsonNumberTextField(*actor, nestedKey)) {
      if(auto value = parseDouble(*text))
        return std::to_string(finiteOrThrow(*value, nestedKey));
    }
  }
  return optionalJsonDoubleSql(json, flatKey0, flatKey1, flatKey2);
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

void appendJsonSizeField(std::string& out, std::string_view key, std::size_t value) {
  appendJsonNumberField(out, key, static_cast<std::uint64_t>(value));
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

[[nodiscard]] std::string logToken(std::string_view text);

[[nodiscard]] bool worldInstanceContentCacheEnabled(const Options& opt) noexcept {
  return !opt.runtimeReadModelPath.empty();
}

[[nodiscard]] ActiveReadModelSelectionRequest makeActiveReadModelSelectionRequest(const Options& opt) {
  ActiveReadModelSelectionRequest request;
  request.contentRevisionKey = opt.contentRevisionKey;
  request.worldInstanceKey = opt.worldInstanceKey;
  request.worldName = opt.worldName;
  request.explicitRuntimeReadModelPath = opt.runtimeReadModelPath;
  return request;
}

void printActiveReadModelSelectionPlan(const ActiveReadModelSelectionResult& result) {
  std::cout << "[active_read_model_selection_plan]"
            << " status=" << logToken(result.status)
            << " selected=" << (result.selected ? 1 : 0)
            << " explicit_path_used=" << (result.explicitPathUsed ? 1 : 0)
            << " db_lookup_required=" << (result.dbLookupRequired ? 1 : 0)
            << " db_mutated=" << (result.dbMutated ? 1 : 0)
            << " sql_generated=" << (result.sqlGenerated ? 1 : 0)
            << " server_sql_touched=" << (result.serverSqlTouched ? 1 : 0)
            << " selected_path=" << logToken(result.selectedPath)
            << " content_revision_key=" << logToken(result.contentRevisionKey)
            << " world_instance_key=" << logToken(result.worldInstanceKey)
            << " world_name=" << logToken(result.worldName)
            << " issues=" << result.issues.size()
            << "\n";
}

[[nodiscard]] WorldInstanceContentCache loadWorldInstanceContentCache(const Options& opt) {
  WorldInstanceContentCacheOptions cacheOptions;
  cacheOptions.runtimeReadModelPath = opt.runtimeReadModelPath;
  cacheOptions.contentRevisionKey = opt.contentRevisionKey;
  cacheOptions.worldInstanceKey = opt.worldInstanceKey;
  cacheOptions.worldName = opt.worldName;
  cacheOptions.requireContentRevisionMatch = opt.requireRuntimeReadModelContentRevisionMatch;
  return WorldInstanceContentCache::load(cacheOptions);
}

[[nodiscard]] std::string worldInstanceContentCacheSnapshotJson(
    const WorldInstanceContentCache& cache,
    std::string_view requestedWorldName) {
  const auto& stats = cache.stats();
  std::string out;
  out.reserve(768);
  out += "{\"status\":";
  out += jsonEscape(requestedWorldName == cache.worldName() ? "ready" : "world_mismatch");
  appendJsonField(out, "content_revision_key", cache.contentRevisionKey());
  appendJsonField(out, "read_model_content_revision_key", cache.readModel().inspection.contentRevisionKey);
  appendJsonField(out, "world_instance_key", cache.worldInstanceKey());
  appendJsonField(out, "world_name", cache.worldName());
  appendJsonField(out, "requested_world_name", requestedWorldName);
  appendJsonSizeField(out, "world_zen_entities_in_world", stats.worldZenEntitiesInWorld);
  appendJsonSizeField(out, "waypoint_edges_in_world", stats.waypointEdgesInWorld);
  appendJsonSizeField(out, "npc_templates", stats.npcTemplates);
  appendJsonSizeField(out, "item_templates", stats.itemTemplates);
  appendJsonSizeField(out, "routines", stats.routines);
  appendJsonSizeField(out, "perception_bindings", stats.perceptionBindings);
  appendJsonSizeField(out, "dialog_infos", stats.dialogInfos);
  appendJsonSizeField(out, "dialog_outputs", stats.dialogOutputs);
  appendJsonSizeField(out, "warnings", cache.readModel().inspection.warnings.size());
  out.push_back('}');
  return out;
}

void appendWorldInstanceContentCacheSnapshotField(
    std::string& snapshotJson,
    const WorldInstanceContentCache& cache,
    std::string_view requestedWorldName) {
  appendJsonRawFieldBeforeFinalObjectBrace(
      snapshotJson,
      "world_instance_content_cache",
      worldInstanceContentCacheSnapshotJson(cache, requestedWorldName));
}

void printWorldInstanceContentCacheReady(const WorldInstanceContentCache& cache) {
  const auto& stats = cache.stats();
  std::cout << "[world_instance_content_cache_ready]"
            << " content_revision_key=" << cache.contentRevisionKey()
            << " read_model_content_revision_key=" << cache.readModel().inspection.contentRevisionKey
            << " world_instance_key=" << cache.worldInstanceKey()
            << " world_name=" << cache.worldName()
            << " world_zen_entities_in_world=" << stats.worldZenEntitiesInWorld
            << " waypoint_edges_in_world=" << stats.waypointEdgesInWorld
            << " npc_templates=" << stats.npcTemplates
            << " item_templates=" << stats.itemTemplates
            << " routines=" << stats.routines
            << " perception_bindings=" << stats.perceptionBindings
            << " dialog_infos=" << stats.dialogInfos
            << " dialog_outputs=" << stats.dialogOutputs
            << " warnings=" << cache.readModel().inspection.warnings.size()
            << "\n";
}

[[nodiscard]] bool worldInstanceAiStartupDryRunEnabled(const Options& opt) noexcept {
  return opt.worldInstanceAiStartupDryRun || opt.worldInstanceAiStartupFailOnEvidence;
}

[[nodiscard]] WorldInstanceAiSchedulerPlanOptions makeWorldInstanceAiSchedulerPlanOptions(const Options& opt) {
  WorldInstanceAiSchedulerPlanOptions options;
  options.enabled = opt.worldInstanceAiSchedulerPlanOnly;
  options.intervalMs = opt.worldInstanceAiSchedulerIntervalMs;
  options.maxWorldInstances = 1;
  options.tickOncePerWorldInstance = true;
  options.allowWrites = false;
  return options;
}

void printWorldInstanceAiSchedulerPlan(const WorldInstanceAiSchedulerPlan& plan) {
  std::cout << "[world_instance_ai_scheduler_plan]"
            << " status=" << logToken(plan.status)
            << " enabled=" << (plan.enabled ? 1 : 0)
            << " mode=" << logToken(plan.schedulerMode)
            << " interval_ms=" << plan.intervalMs
            << " max_world_instances=" << plan.maxWorldInstances
            << " tick_once_per_world_instance=" << (plan.tickOncePerWorldInstance ? 1 : 0)
            << " timer_scheduled=" << (plan.timerScheduled ? 1 : 0)
            << " tick_executed=" << (plan.tickExecuted ? 1 : 0)
            << " db_mutated=" << (plan.dbMutated ? 1 : 0)
            << " write_allowed=" << (plan.writeAllowed ? 1 : 0)
            << " issues=" << plan.issues.size()
            << "\n";
}

void printRuntimeNpcIdentityMaterializationPlan(
    const RuntimeNpcIdentityMaterializationPlan& plan) {
  std::cout << "[runtime_npc_identity_materialization_plan]"
            << " status=" << logToken(plan.status)
            << " built=" << (plan.built ? 1 : 0)
            << " read_model_npc_templates=" << plan.readModelNpcTemplates
            << " runtime_rows=" << plan.runtimeRows
            << " ready_rows=" << plan.readyRows
            << " db_write_required_rows=" << plan.dbWriteRequiredRows
            << " missing_npc_instance_rows=" << plan.missingNpcInstanceRows
            << " missing_read_model_template_rows=" << plan.missingReadModelTemplateRows
            << " entity_key_repair_rows=" << plan.entityKeyRepairRows
            << " db_mutated=" << (plan.dbMutated ? 1 : 0)
            << " sql_generated=" << (plan.sqlGenerated ? 1 : 0)
            << " materialization_executed=" << (plan.materializationExecuted ? 1 : 0)
            << " issues=" << plan.issues.size()
            << "\n";
}

[[nodiscard]] std::size_t parseSizeOr(std::string_view text, std::size_t fallback) noexcept {
  const auto parsed = parseU64OrZero(text);
  if(parsed == 0 && text != "0")
    return fallback;
  return static_cast<std::size_t>(parsed);
}

[[nodiscard]] std::string logToken(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for(const unsigned char c : text) {
    if(c <= ' ' || c == '"' || c == '\'' || c == '`') {
      out.push_back('_');
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

[[nodiscard]] WorldInstanceAiSchedulerBoundaryOptions makeWorldInstanceAiStartupOptions(const Options& opt) {
  WorldInstanceAiSchedulerBoundaryOptions options;
  options.enabled = true;
  options.tick.runtimeReadModelPath = opt.runtimeReadModelPath;
  options.tick.contentRevisionKey = opt.contentRevisionKey;
  options.tick.worldInstanceKey = opt.worldInstanceKey;
  options.tick.worldName = opt.worldName;
  options.tick.aiDatabaseName = opt.worldInstanceAiDatabaseName;
  options.tick.perceptionKind = opt.worldInstanceAiPerceptionKind;
  options.tick.maxDistance = opt.worldInstanceAiMaxDistance;
  options.tick.serverTick = opt.worldInstanceAiServerTick;
  options.tick.cooldownTicks = opt.worldInstanceAiCooldownTicks;
  options.tick.priorityValue = opt.worldInstanceAiPriorityValue;
  options.tick.maxNpcs = opt.worldInstanceAiMaxNpcs;
  options.tick.maxPlayers = opt.worldInstanceAiMaxPlayers;
  options.tick.maxRecords = 0;
  options.tick.repairWeakNpcEntityKeys = opt.worldInstanceAiRepairWeakNpcEntityKeys;
  options.tick.includeWeakNpcIdentity = opt.worldInstanceAiIncludeWeakNpcIdentity;
  options.tick.enqueueAction = false;
  options.tick.dryRun = true;

  options.evidence.minAcceptedNpcs = opt.worldInstanceAiMinAcceptedNpcs;
  options.evidence.minPlayerActors = opt.worldInstanceAiMinPlayerActors;
  options.evidence.minDecisions = opt.worldInstanceAiMinDecisions;
  options.evidence.maxSkippedWeakEntityKeys = opt.worldInstanceAiMaxSkippedWeakNpcIdentity;
  options.evidence.maxSkippedMissingNpcInstance = opt.worldInstanceAiMaxSkippedMissingNpcInstance;
  options.evidence.requireActorPair = opt.worldInstanceAiRequireActorPair;
  options.evidence.requireDecision = opt.worldInstanceAiRequireDecision;
  options.evidence.requireCleanNpcIdentity = opt.worldInstanceAiRequireCleanNpcIdentity;
  options.evidence.requireNoRecordLimitSkip = opt.worldInstanceAiRequireNoRecordLimitSkip;
  return options;
}

void printWorldInstanceAiStartupDryRunResult(const WorldInstanceAiSchedulerBoundaryResult& result) {
  const auto& evidence = result.evidence;
  const auto& tick = result.dryRunTick;
  std::cout << "[world_instance_ai_startup_dry_run]"
            << " enabled=" << (result.enabled ? 1 : 0)
            << " executed=" << (result.executed ? 1 : 0)
            << " write_executed=" << (result.writeExecuted ? 1 : 0)
            << " accepted=" << (evidence.accepted ? 1 : 0)
            << " status=" << logToken(evidence.status)
            << " reason=" << logToken(evidence.reason)
            << " world_instance_uuid=" << logToken(tick.world.worldInstanceUuid)
            << " world_instance_key=" << logToken(tick.world.worldInstanceKey)
            << " world_name=" << logToken(tick.world.worldName)
            << " server_tick=" << tick.world.serverTick
            << " accepted_npcs=" << evidence.acceptedNpcs
            << " player_actors=" << evidence.playerActors
            << " decisions=" << evidence.decisions
            << " weak_identity_skips=" << evidence.weakIdentitySkips
            << " missing_npc_instance_skips=" << evidence.missingNpcInstanceSkips
            << " record_limit_skips=" << evidence.recordLimitSkips
            << " accepted_npc_ratio=" << evidence.acceptedNpcRatio
            << "\n";
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

[[nodiscard]] std::string runCommand(std::string_view cmd) {
  std::array<char, 4096> buffer {};
  std::string output;
  FILE* pipe = openProcessPipe(std::string(cmd));
  if(pipe == nullptr)
    throw std::runtime_error("popen failed");
  while(std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    output += buffer.data();
  const int rc = closeProcessPipe(pipe);
  if(rc != 0) {
    const auto message = trim(output);
    if(message.empty())
      throw std::runtime_error("command failed");
    throw std::runtime_error("command failed: " + message);
  }
  return trim(output);
}

[[nodiscard]] MySqlTarget parseMysqlUrl(std::string_view url) {
  constexpr std::string_view mysql = "mysql://";
  constexpr std::string_view pymysql = "mysql+pymysql://";
  if(startsWith(url, pymysql))
    url.remove_prefix(pymysql.size());
  else if(startsWith(url, mysql))
    url.remove_prefix(mysql.size());
  else
    throw std::runtime_error("expected mysql:// URL");

  const auto slash = url.find('/');
  const auto at = url.substr(0, slash).rfind('@');
  if(slash == std::string_view::npos || at == std::string_view::npos)
    throw std::runtime_error("mysql URL must include user@host/database");

  const auto authHost = url.substr(0, slash);
  const auto auth = authHost.substr(0, at);
  const auto hostPort = authHost.substr(at + 1);
  const auto authColon = auth.find(':');
  const auto hostColon = hostPort.rfind(':');

  MySqlTarget out;
  out.user = std::string(auth.substr(0, authColon));
  if(authColon != std::string_view::npos)
    out.password = std::string(auth.substr(authColon + 1));
  out.host = std::string(hostColon == std::string_view::npos ? hostPort : hostPort.substr(0, hostColon));
  if(hostColon != std::string_view::npos)
    out.port = parseInt(hostPort.substr(hostColon + 1)).value_or(3306);
  out.database = std::string(url.substr(slash + 1));
  if(out.host.empty())
    out.host = "127.0.0.1";
  if(out.user.empty() || out.database.empty())
    throw std::runtime_error("mysql URL must include user and database");
  return out;
}

[[nodiscard]] std::string mysqlBaseCommand(const MySqlTarget& target) {
  std::string cmd = shellQuote(mysqlExecutable()) + " --default-character-set=utf8mb4 ";
  cmd += "--init-command=" + shellQuote("SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci") + " ";
  cmd += "--batch --raw --skip-column-names ";
  cmd += "--host=" + shellQuote(target.host) + " ";
  cmd += "--port=" + shellQuote(std::to_string(target.port)) + " ";
  cmd += "--user=" + shellQuote(target.user) + " ";
  if(!target.password.empty()) {
#if defined(_WIN32)
    cmd = cmdSetEnvPrefix("MYSQL_PWD", target.password) + cmd;
#else
    cmd = "MYSQL_PWD=" + shellQuote(target.password) + " " + cmd;
#endif
  }
  cmd += shellQuote(target.database);
  return cmd;
}

[[nodiscard]] std::string runMysql(const MySqlTarget& target, std::string_view sql) {
  std::string statement;
  statement.reserve(MysqlSessionPreamble.size() + sql.size());
  statement.append(MysqlSessionPreamble);
  statement.append(sql.data(), sql.size());
  return runCommand(mysqlBaseCommand(target) + " --execute " + shellQuote(statement) + " 2>&1");
}

[[nodiscard]] bool isFailOpenNpcObservationAction(Mmo::SemanticActionKind kind) noexcept {
  return kind == Mmo::SemanticActionKind::RecordNpcRoutineState ||
         kind == Mmo::SemanticActionKind::RecordNpcAiState ||
         kind == Mmo::SemanticActionKind::RecordNpcPathState ||
         kind == Mmo::SemanticActionKind::RecordNpcFightState ||
         kind == Mmo::SemanticActionKind::RecordCombatIntent ||
         kind == Mmo::SemanticActionKind::RecordNpcActionState ||
         kind == Mmo::SemanticActionKind::RecordNpcDialogLine;
}

[[nodiscard]] std::vector<std::string> splitMysqlLastRow(std::string_view raw) {
  std::string line = trim(raw);
  const auto nl = line.rfind('\n');
  if(nl != std::string::npos)
    line.erase(0, nl + 1);

  std::vector<std::string> parts;
  std::string_view view = line;
  while(true) {
    const auto tab = view.find('\t');
    if(tab == std::string_view::npos) {
      parts.emplace_back(view);
      break;
    }
    parts.emplace_back(view.substr(0, tab));
    view.remove_prefix(tab + 1);
  }
  return parts;
}

[[nodiscard]] std::string mysqlSingleField(const MySqlTarget& target, std::string_view sql) {
  const auto parts = splitMysqlLastRow(runMysql(target, sql));
  if(parts.empty())
    return {};
  return parts.front() == "NULL" ? std::string() : std::string(parts.front());
}

void tryRunAiDialogIntentDurableMarkAppliedGate(const MySqlTarget& target,
                                                const Options& opt,
                                                std::string_view actionId,
                                                std::string_view ackKey,
                                                std::string_view trigger,
                                                std::string_view source) noexcept {
  if(!opt.aiDialogIntentStep273DurableMarkAppliedGate)
    return;

  Mmo::Server::AiDialogIntentDeliveryMarkAppliedGateRequest gateRequest;
  gateRequest.enabled = true;
  gateRequest.allowMysqlExecution = true;
  gateRequest.requireObservation = true;
  gateRequest.aiDatabaseName = opt.worldInstanceAiDatabaseName;
  gateRequest.actionId = std::string(actionId);
  gateRequest.ackKey = std::string(ackKey);
  gateRequest.workerId = "mmo_udp_server_step283";
  gateRequest.trigger = std::string(trigger);
  gateRequest.source = std::string(source);

  const auto gateSql = Mmo::Server::buildAiDialogIntentDeliveryMarkAppliedGateSql(gateRequest);
  if(!gateSql.ready) {
    std::cerr << "[server_ai_dialog_intent_step283_durable_mark_applied_gate_skipped]"
              << " action_id=" << actionId
              << " ack_key=" << ackKey
              << " status=" << Mmo::Server::aiDialogIntentDeliveryMarkAppliedGateStatusName(gateSql.status)
              << " reason=" << gateSql.reason
              << " execute_mysql=0"
              << " mutated_db=0"
              << " mark_applied=off"
              << "\n";
    return;
  }

  try {
    const auto rawGate = runMysql(target, gateSql.sql);
    const auto gateParts = splitMysqlLastRow(rawGate);
    const auto gateStatus = gateParts.empty() ? std::string() : gateParts[0];
    const auto actionStatus = gateParts.size() > 1 ? gateParts[1] : std::string();
    const auto actionQueueUuid = gateParts.size() > 2 ? gateParts[2] : std::string();
    const bool applied = gateStatus == "applied";
    std::cout << "[server_ai_dialog_intent_step283_durable_mark_applied_gate]"
              << " action_id=" << actionId
              << " ack_key=" << ackKey
              << " trigger=" << trigger
              << " source=" << source
              << " gate_status=" << gateStatus
              << " action_status=" << actionStatus
              << " action_queue_uuid=" << actionQueueUuid
              << " require_observation=1"
              << " execute_mysql=1"
              << " mutated_db=" << (applied ? 1 : 0)
              << " client_ui_audio=off"
              << " mark_applied=" << (applied ? "on" : "off")
              << "\n";
  } catch(const std::exception& error) {
    std::cerr << "[server_ai_dialog_intent_step283_durable_mark_applied_gate_failed]"
              << " action_id=" << actionId
              << " ack_key=" << ackKey
              << " trigger=" << trigger
              << " source=" << source
              << " error=" << error.what()
              << " execute_mysql=1"
              << " mutated_db=0"
              << " mark_applied=off"
              << "\n";
  }
}

[[nodiscard]] std::string sqlJson(std::string_view json) {
  return "CAST(" + sqlLiteral(json) + " AS JSON)";
}

[[nodiscard]] std::string concatenateJsonArrays(std::initializer_list<std::string_view> arrays) {
  std::size_t capacity = 2;
  for(const auto array : arrays)
    capacity += array.size() + 1;

  std::string out;
  out.reserve(capacity);
  out.push_back('[');
  bool first = true;

  for(auto array : arrays) {
    while(!array.empty() && std::isspace(static_cast<unsigned char>(array.front())))
      array.remove_prefix(1);
    while(!array.empty() && std::isspace(static_cast<unsigned char>(array.back())))
      array.remove_suffix(1);
    if(array.size() < 2 || array.front() != '[' || array.back() != ']')
      continue;

    array.remove_prefix(1);
    array.remove_suffix(1);
    while(!array.empty() && std::isspace(static_cast<unsigned char>(array.front())))
      array.remove_prefix(1);
    while(!array.empty() && std::isspace(static_cast<unsigned char>(array.back())))
      array.remove_suffix(1);
    if(array.empty())
      continue;

    if(!first)
      out.push_back(',');
    out.append(array.data(), array.size());
    first = false;
  }

  out.push_back(']');
  return out;
}

[[nodiscard]] std::string mysqlJsonOr(const MySqlTarget& target,
                                      std::string_view sql,
                                      std::string_view fallbackJson) {
  auto out = mysqlSingleField(target, sql);
  if(out.empty())
    return std::string(fallbackJson);
  return out;
}

[[nodiscard]] std::string mysqlJsonOrWithDiagnostic(const MySqlTarget& target,
                                                    std::string_view sql,
                                                    std::string_view fallbackJson,
                                                    std::string_view label) {
  try {
    return mysqlJsonOr(target, sql, fallbackJson);
  } catch(const std::exception& e) {
    std::cout << '[' << label << "_failed] error=" << e.what() << "\n";
    return std::string(fallbackJson);
  }
}

[[nodiscard]] std::string mysqlSingleFieldWithDiagnostic(const MySqlTarget& target,
                                                         std::string_view sql,
                                                         std::string_view label) {
  try {
    return mysqlSingleField(target, sql);
  } catch(const std::exception& e) {
    std::cout << '[' << label << "_failed] error=" << e.what() << "\n";
    return {};
  }
}


[[nodiscard]] std::string buildSaveCheckpointBootstrapSnapshotJson(const MySqlTarget& target,
                                                                   std::string_view sessionUuid) {
  if(sessionUuid.empty())
    return {};

  std::string query;
  query += "SET @mmo_bootstrap_snapshot = NULL; ";
  query += "CALL mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(UUID_TO_BIN(";
  query += sqlLiteral(sessionUuid);
  query += ",1), @mmo_bootstrap_snapshot); ";
  query += "SELECT COALESCE(@mmo_bootstrap_snapshot, '');";
  auto out = mysqlSingleFieldWithDiagnostic(target, query, "bootstrap_db_save_checkpoint_restore");
  if(out == "NULL")
    out.clear();
  if(!out.empty()) {
    const std::string sessionSql = sqlLiteral(sessionUuid);

    std::string npcRoutineStateQuery;
    npcRoutineStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
    npcRoutineStateQuery += "SELECT JSON_OBJECT('npc_entity_key',rs.npc_entity_key,'routine_state',rs.routine_state,";
    npcRoutineStateQuery += "'schedule_key',rs.schedule_key,'current_waypoint_key',rs.current_waypoint_key,'target_waypoint_key',rs.target_waypoint_key,";
    npcRoutineStateQuery += "'last_server_tick',rs.last_server_tick,'row_version',rs.row_version,'updated_at',DATE_FORMAT(rs.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
    npcRoutineStateQuery += "FROM server_sessions ss JOIN mmo_npc_routine_state_current rs ON rs.world_instance_id=ss.world_instance_id ";
    npcRoutineStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY rs.last_server_tick DESC,rs.npc_entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcAuthorityRows);
    npcRoutineStateQuery += ") rows_json), JSON_ARRAY());";
    appendJsonRawFieldBeforeFinalObjectBrace(out, Mmo::Server::BootstrapNpcRoutineStateSection,
                                             mysqlJsonOrWithDiagnostic(target, npcRoutineStateQuery, "[]", "bootstrap_db_checkpoint_npc_routine_state"));

    std::string npcAiStateQuery;
    npcAiStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
    npcAiStateQuery += "SELECT JSON_OBJECT('npc_entity_key',ais.npc_entity_key,'ai_state',ais.ai_state,'ai_intent',ais.ai_intent,";
    npcAiStateQuery += "'target_key',ais.target_key,'perception_state',ais.perception_state,'last_server_tick',ais.last_server_tick,";
    npcAiStateQuery += "'row_version',ais.row_version,'updated_at',DATE_FORMAT(ais.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
    npcAiStateQuery += "FROM server_sessions ss JOIN mmo_npc_ai_state_current ais ON ais.world_instance_id=ss.world_instance_id ";
    npcAiStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ais.last_server_tick DESC,ais.npc_entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcAuthorityRows);
    npcAiStateQuery += ") rows_json), JSON_ARRAY());";
    appendJsonRawFieldBeforeFinalObjectBrace(out, Mmo::Server::BootstrapNpcAiStateSection,
                                             mysqlJsonOrWithDiagnostic(target, npcAiStateQuery, "[]", "bootstrap_db_checkpoint_npc_ai_state"));

    std::string npcPathStateQuery;
    npcPathStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
    npcPathStateQuery += "SELECT JSON_OBJECT('npc_entity_key',ps.npc_entity_key,'path_state',ps.path_state,'route_key',ps.route_key,";
    npcPathStateQuery += "'current_waypoint_key',ps.current_waypoint_key,'next_waypoint_key',ps.next_waypoint_key,'target_waypoint_key',ps.target_waypoint_key,";
    npcPathStateQuery += "'pos_x',ps.pos_x,'pos_y',ps.pos_y,'pos_z',ps.pos_z,'last_server_tick',ps.last_server_tick,";
    npcPathStateQuery += "'row_version',ps.row_version,'updated_at',DATE_FORMAT(ps.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
    npcPathStateQuery += "FROM server_sessions ss JOIN mmo_npc_path_state_current ps ON ps.world_instance_id=ss.world_instance_id ";
    npcPathStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ps.last_server_tick DESC,ps.npc_entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcAuthorityRows);
    npcPathStateQuery += ") rows_json), JSON_ARRAY());";
    appendJsonRawFieldBeforeFinalObjectBrace(out, Mmo::Server::BootstrapNpcPathStateSection,
                                             mysqlJsonOrWithDiagnostic(target, npcPathStateQuery, "[]", "bootstrap_db_checkpoint_npc_path_state"));

    std::string npcFightStateQuery;
    npcFightStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
    npcFightStateQuery += "SELECT JSON_OBJECT('npc_entity_key',fs.npc_entity_key,'opponent_key',fs.opponent_key,'fight_state',fs.fight_state,";
    npcFightStateQuery += "'attack_state',fs.attack_state,'combo_index',fs.combo_index,'last_server_tick',fs.last_server_tick,";
    npcFightStateQuery += "'row_version',fs.row_version,'updated_at',DATE_FORMAT(fs.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
    npcFightStateQuery += "FROM server_sessions ss JOIN mmo_npc_fight_state_current fs ON fs.world_instance_id=ss.world_instance_id ";
    npcFightStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY fs.last_server_tick DESC,fs.npc_entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcAuthorityRows);
    npcFightStateQuery += ") rows_json), JSON_ARRAY());";
    appendJsonRawFieldBeforeFinalObjectBrace(out, Mmo::Server::BootstrapNpcFightStateSection,
                                             mysqlJsonOrWithDiagnostic(target, npcFightStateQuery, "[]", "bootstrap_db_checkpoint_npc_fight_state"));
  }
  return out;
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
  const std::string characterSql = sqlLiteral(characterKey);

  std::string characterListQuery;
  characterListQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  characterListQuery += "SELECT JSON_OBJECT('character_key',c.character_key,'display_name',c.character_name,";
  characterListQuery += "'world_name',COALESCE(cwt.world_name,rwi.world_instance_key,''),";
  characterListQuery += "'lifecycle_state',c.lifecycle_state,'selected',JSON_EXTRACT(IF(c.character_key=" + characterSql + ",'true','false'),'$'),";
  characterListQuery += "'updated_at',DATE_FORMAT(c.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  characterListQuery += "FROM server_sessions ss JOIN characters active_c ON active_c.character_id=ss.character_id ";
  characterListQuery += "JOIN characters c ON c.account_id=active_c.account_id AND c.realm_id=active_c.realm_id ";
  characterListQuery += "LEFT JOIN realm_world_instances rwi ON rwi.world_instance_id=c.current_world_instance_id ";
  characterListQuery += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  characterListQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND c.lifecycle_state IN ('creating','active') ";
  characterListQuery += "ORDER BY CASE WHEN c.character_key=" + characterSql + " THEN 0 ELSE 1 END,c.updated_at DESC,c.character_name LIMIT 20";
  characterListQuery += ") rows_json), JSON_ARRAY());";
  const auto characterList = mysqlJsonOrWithDiagnostic(target, characterListQuery, "[]", "bootstrap_character_list");

  std::string characterQuery;
  characterQuery += "SELECT COALESCE((SELECT JSON_OBJECT(";
  characterQuery += "'character_key',c.character_key,'display_name',c.character_name,'world_name',";
  characterQuery += "COALESCE(cwt.world_name,rwi.world_instance_key," + worldSql + "),";
  characterQuery += "'position',JSON_OBJECT('x',cp.pos_x,'y',cp.pos_y,'z',cp.pos_z,'yaw',cp.rotation_yaw,'waypoint',cp.current_waypoint_key,'server_tick',cp.server_tick),";
  characterQuery += "'stats',JSON_OBJECT('level',cs.level,'experience',cs.experience,'experience_next',cs.experience_next,'learning_points',cs.learning_points,";
  characterQuery += "'health_current',cs.health_current,'health_max',cs.health_max,'mana_current',cs.mana_current,'mana_max',cs.mana_max,";
  characterQuery += "'strength',cs.strength,'dexterity',cs.dexterity,'guild',cs.guild,'true_guild',cs.true_guild),";
  characterQuery += "'lifecycle_state',c.lifecycle_state,'updated_at',DATE_FORMAT(c.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) ";
  characterQuery += "FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id ";
  characterQuery += "LEFT JOIN realm_world_instances rwi ON rwi.world_instance_id=ss.world_instance_id ";
  characterQuery += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  characterQuery += "LEFT JOIN character_stats cs ON cs.character_id=c.character_id ";
  characterQuery += "LEFT JOIN character_positions cp ON cp.character_id=c.character_id ";
  characterQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) LIMIT 1), JSON_OBJECT());";
  const auto character = mysqlJsonOr(target, characterQuery, "{}");

  std::string inventoryQuery;
  inventoryQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  inventoryQuery += "SELECT JSON_OBJECT('item_instance_uuid',BIN_TO_UUID(ii.item_instance_id,1),'item_instance_key',ii.item_instance_key,";
  inventoryQuery += "'item_template_key',cit.item_template_key,'symbol_index',cit.symbol_index,'script_name',cit.script_name,'display_name',cit.display_name,";
  inventoryQuery += "'classification',cit.classification,'stack_policy',cit.stack_policy,'amount',ci.amount,'bag_index',ci.bag_index,";
  inventoryQuery += "'equipped_slot',ce.equipment_slot,'lifecycle_state',ii.lifecycle_state,'updated_at',DATE_FORMAT(ci.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  inventoryQuery += "FROM server_sessions ss JOIN character_inventory ci ON ci.character_id=ss.character_id ";
  inventoryQuery += "JOIN item_instances ii ON ii.item_instance_id=ci.item_instance_id ";
  inventoryQuery += "JOIN content_item_templates cit ON cit.item_template_id=ii.item_template_id ";
  inventoryQuery += "LEFT JOIN character_equipment ce ON ce.character_id=ss.character_id AND ce.item_instance_id=ci.item_instance_id ";
  inventoryQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY COALESCE(ci.bag_index,999999),ii.item_instance_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapInventoryRows);
  inventoryQuery += ") rows_json), JSON_ARRAY());";
  const auto inventory = mysqlJsonOr(target, inventoryQuery, "[]");

  std::string equipmentQuery;
  equipmentQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  equipmentQuery += "SELECT JSON_OBJECT('slot',ce.equipment_slot,'item_instance_uuid',BIN_TO_UUID(ii.item_instance_id,1),";
  equipmentQuery += "'item_instance_key',ii.item_instance_key,'item_template_key',cit.item_template_key,'symbol_index',cit.symbol_index,";
  equipmentQuery += "'display_name',cit.display_name,'updated_at',DATE_FORMAT(ce.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  equipmentQuery += "FROM server_sessions ss JOIN character_equipment ce ON ce.character_id=ss.character_id ";
  equipmentQuery += "JOIN item_instances ii ON ii.item_instance_id=ce.item_instance_id ";
  equipmentQuery += "JOIN content_item_templates cit ON cit.item_template_id=ii.item_template_id ";
  equipmentQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ce.equipment_slot LIMIT " + std::to_string(Mmo::Server::MaxBootstrapEquipmentRows);
  equipmentQuery += ") rows_json), JSON_ARRAY());";
  const auto equipment = mysqlJsonOr(target, equipmentQuery, "[]");

  std::string dialogsQuery;
  dialogsQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  dialogsQuery += "SELECT JSON_OBJECT('npc_key',npc_key,'info_key',info_key,'known',known,'permanent',permanent,";
  dialogsQuery += "'availability_state',availability_state,'updated_at',DATE_FORMAT(updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  dialogsQuery += "FROM server_sessions ss JOIN character_known_dialogs d ON d.character_id=ss.character_id ";
  dialogsQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY npc_key,info_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapKnownDialogRows);
  dialogsQuery += ") rows_json), JSON_ARRAY());";
  const auto dialogs = mysqlJsonOr(target, dialogsQuery, "[]");

  std::string questsQuery;
  questsQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  questsQuery += "SELECT JSON_OBJECT('quest_key',quest_key,'section',section,'status',status,'entry_order',entry_order,";
  questsQuery += "'text_entries',text_entries,'updated_at',DATE_FORMAT(updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  questsQuery += "FROM server_sessions ss JOIN character_quests q ON q.character_id=ss.character_id ";
  questsQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY quest_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapQuestRows);
  questsQuery += ") rows_json), JSON_ARRAY());";
  const auto quests = mysqlJsonOr(target, questsQuery, "[]");

  std::string scriptQuery;
  scriptQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  scriptQuery += "SELECT JSON_OBJECT('script_key',script_key,'symbol_index',symbol_index,'value_type',value_type,'value_index',value_index,";
  scriptQuery += "'value_int',value_int,'updated_at',DATE_FORMAT(updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  scriptQuery += "FROM server_sessions ss JOIN character_script_state s ON s.character_id=ss.character_id ";
  scriptQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND s.value_type IN ('int','array_int') ORDER BY script_key,value_index LIMIT " + std::to_string(Mmo::Server::MaxBootstrapScriptStateRows);
  scriptQuery += ") rows_json), JSON_ARRAY());";
  const auto scriptState = mysqlJsonOr(target, scriptQuery, "[]");

  std::string worldSampleQuery;
  worldSampleQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  worldSampleQuery += "SELECT JSON_OBJECT('entity_key',wes.entity_key,'entity_kind',wes.entity_kind,'lifecycle_state',wes.lifecycle_state,";
  worldSampleQuery += "'persistent_id',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED),";
  worldSampleQuery += "'symbol_index',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED),";
  worldSampleQuery += "'exists_in_world',JSON_EXTRACT(wes.state_json,'$.exists_in_world'),";
  worldSampleQuery += "'updated_at',DATE_FORMAT(wes.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  worldSampleQuery += "FROM server_sessions ss JOIN world_entity_state wes ON wes.world_instance_id=ss.world_instance_id ";
  worldSampleQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND wes.entity_kind='item' AND wes.lifecycle_state<>'active' ORDER BY wes.updated_at DESC LIMIT " + std::to_string(Mmo::Server::MaxBootstrapWorldDeltaRows);
  worldSampleQuery += ") rows_json), JSON_ARRAY());";
  const auto worldDeltas = mysqlJsonOr(target, worldSampleQuery, "[]");

  std::string worldClockQuery;
  worldClockQuery += "SELECT COALESCE((SELECT JSON_OBJECT(";
  worldClockQuery += "'world_instance_uuid',BIN_TO_UUID(rwi.world_instance_id,1),'world_instance_key',rwi.world_instance_key,";
  worldClockQuery += "'world_name',COALESCE(cwt.world_name," + worldSql + "),'current_tick',rwi.current_tick,";
  worldClockQuery += "'current_world_time_ms',rwi.current_world_time_ms,'updated_at',DATE_FORMAT(rwi.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) ";
  worldClockQuery += "FROM server_sessions ss JOIN realm_world_instances rwi ON rwi.world_instance_id=ss.world_instance_id ";
  worldClockQuery += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  worldClockQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) LIMIT 1), JSON_OBJECT());";
  const auto worldClock = mysqlJsonOr(target, worldClockQuery, "{}");

  const std::string activeItemRadiusSql = std::to_string(Mmo::Server::BootstrapActiveWorldItemRadius);

  const std::string activeHeroSubquery =
      "(SELECT ss.character_id,ss.realm_id,ss.world_instance_id,COALESCE(cp.pos_x,cca.pos_x,0) AS hx,"
      "COALESCE(cp.pos_y,cca.pos_y,0) AS hy,COALESCE(cp.pos_z,cca.pos_z,0) AS hz "
      "FROM server_sessions ss "
      "LEFT JOIN character_positions cp ON cp.character_id=ss.character_id "
      "LEFT JOIN character_checkpoint_audit cca ON cca.checkpoint_id=(SELECT ca.checkpoint_id FROM character_checkpoint_audit ca WHERE ca.session_id=ss.session_id ORDER BY ca.created_at DESC LIMIT 1) "
      "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) LIMIT 1) h";

  std::string activeWorldInventorySource;
  activeWorldInventorySource += "SELECT JSON_OBJECT('owner_key',wi.owner_entity_key,'source','world_inventory','item_instance_uuid',BIN_TO_UUID(ii.item_instance_id,1),";
  activeWorldInventorySource += "'item_instance_key',ii.item_instance_key,'item_template_key',cit.item_template_key,'symbol_index',cit.symbol_index,";
  activeWorldInventorySource += "'display_name',cit.display_name,'amount',wi.amount,'lifecycle_state',wes.lifecycle_state,";
  activeWorldInventorySource += "'persistent_id',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED),";
  activeWorldInventorySource += "'pos_x',wes.pos_x,'pos_y',wes.pos_y,'pos_z',wes.pos_z,";
  activeWorldInventorySource += "'distance',SQRT(((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))),";
  activeWorldInventorySource += "'updated_at',DATE_FORMAT(GREATEST(wi.updated_at,wes.updated_at),'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,";
  activeWorldInventorySource += "((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz)) AS dist_sq,wi.owner_entity_key AS owner_key ";
  activeWorldInventorySource += "FROM " + activeHeroSubquery + " JOIN world_inventory wi ON wi.world_instance_id=h.world_instance_id ";
  activeWorldInventorySource += "JOIN item_instances ii ON ii.item_instance_id=wi.item_instance_id AND ii.lifecycle_state='active' ";
  activeWorldInventorySource += "JOIN content_item_templates cit ON cit.item_template_id=ii.item_template_id ";
  activeWorldInventorySource += "JOIN world_entity_state wes ON wes.world_instance_id=wi.world_instance_id AND wes.entity_key=wi.owner_entity_key ";
  activeWorldInventorySource += "WHERE wes.entity_kind='item' AND wes.lifecycle_state='active' AND wi.amount>0 AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL ";
  activeWorldInventorySource += "AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")";

  std::string activeWorldEntitySource;
  activeWorldEntitySource += "SELECT JSON_OBJECT('owner_key',src.entity_key,'source','world_entity_state','item_instance_uuid',CAST(NULL AS CHAR),";
  activeWorldEntitySource += "'item_instance_key',src.entity_key,'item_template_key',cit.item_template_key,'symbol_index',src.symbol_index,";
  activeWorldEntitySource += "'display_name',COALESCE(cit.display_name,src.display_name),'amount',src.amount,'lifecycle_state',src.lifecycle_state,";
  activeWorldEntitySource += "'persistent_id',src.persistent_id,'pos_x',src.pos_x,'pos_y',src.pos_y,'pos_z',src.pos_z,";
  activeWorldEntitySource += "'distance',SQRT(((src.pos_x-h.hx)*(src.pos_x-h.hx))+((src.pos_y-h.hy)*(src.pos_y-h.hy))+((src.pos_z-h.hz)*(src.pos_z-h.hz))),";
  activeWorldEntitySource += "'updated_at',DATE_FORMAT(src.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,";
  activeWorldEntitySource += "((src.pos_x-h.hx)*(src.pos_x-h.hx))+((src.pos_y-h.hy)*(src.pos_y-h.hy))+((src.pos_z-h.hz)*(src.pos_z-h.hz)) AS dist_sq,src.entity_key AS owner_key ";
  activeWorldEntitySource += "FROM " + activeHeroSubquery + " JOIN realm_realms rr ON rr.realm_id=h.realm_id JOIN (";
  activeWorldEntitySource += "SELECT wes.world_instance_id,wes.entity_key,wes.lifecycle_state,wes.pos_x,wes.pos_y,wes.pos_z,wes.updated_at,";
  activeWorldEntitySource += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.display_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.name'))) AS display_name,";
  activeWorldEntitySource += "CAST(COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol'))) AS SIGNED) AS symbol_index,";
  activeWorldEntitySource += "CAST(COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.pid'))) AS SIGNED) AS persistent_id,";
  activeWorldEntitySource += "GREATEST(1,COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.amount')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.quantity')) AS SIGNED),1)) AS amount ";
  activeWorldEntitySource += "FROM world_entity_state wes WHERE wes.entity_kind='item' AND wes.lifecycle_state='active' AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL";
  activeWorldEntitySource += ") src ON src.world_instance_id=h.world_instance_id ";
  activeWorldEntitySource += "LEFT JOIN content_item_templates cit ON cit.content_revision_id=rr.active_content_revision_id AND cit.symbol_index=src.symbol_index ";
  activeWorldEntitySource += "WHERE src.symbol_index IS NOT NULL AND src.symbol_index>=0 ";
  activeWorldEntitySource += "AND NOT EXISTS (SELECT 1 FROM world_inventory wi WHERE wi.world_instance_id=src.world_instance_id AND wi.owner_entity_key=src.entity_key) ";
  activeWorldEntitySource += "AND (((src.pos_x-h.hx)*(src.pos_x-h.hx))+((src.pos_y-h.hy)*(src.pos_y-h.hy))+((src.pos_z-h.hz)*(src.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")";

  std::string activeReadModelSource;
  activeReadModelSource += "SELECT JSON_OBJECT('owner_key',rm.owner_key,'source','read_model','item_instance_uuid',CAST(NULL AS CHAR),";
  activeReadModelSource += "'item_instance_key',rm.item_instance_key,'item_template_key',rm.item_template_key,'symbol_index',cit.symbol_index,";
  activeReadModelSource += "'display_name',COALESCE(rm.display_name,cit.display_name),'amount',GREATEST(1,CAST(rm.amount AS SIGNED)),'lifecycle_state',rm.lifecycle_state,";
  activeReadModelSource += "'persistent_id',CAST(NULL AS SIGNED),'pos_x',rm.pos_x,'pos_y',rm.pos_y,'pos_z',rm.pos_z,";
  activeReadModelSource += "'distance',SQRT(((rm.pos_x-h.hx)*(rm.pos_x-h.hx))+((rm.pos_y-h.hy)*(rm.pos_y-h.hy))+((rm.pos_z-h.hz)*(rm.pos_z-h.hz))),";
  activeReadModelSource += "'updated_at',DATE_FORMAT(rm.materialized_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,";
  activeReadModelSource += "((rm.pos_x-h.hx)*(rm.pos_x-h.hx))+((rm.pos_y-h.hy)*(rm.pos_y-h.hy))+((rm.pos_z-h.hz)*(rm.pos_z-h.hz)) AS dist_sq,rm.owner_key AS owner_key ";
  activeReadModelSource += "FROM " + activeHeroSubquery + " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  activeReadModelSource += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  activeReadModelSource += "JOIN realm_realms rr ON rr.realm_id=h.realm_id ";
  activeReadModelSource += "JOIN mmo_server_world_inventory_read_model rm ON rm.world_name=COALESCE(cwt.world_name,rwi.world_instance_key," + worldSql + ") ";
  activeReadModelSource += "LEFT JOIN content_item_templates cit ON cit.content_revision_id=rr.active_content_revision_id AND (cit.item_template_key=rm.item_template_key OR (rm.item_template_key REGEXP '^-?[0-9]+$' AND cit.symbol_index=CAST(rm.item_template_key AS SIGNED))) ";
  activeReadModelSource += "WHERE rm.lifecycle_state='active' AND rm.amount>0 AND rm.pos_x IS NOT NULL AND rm.pos_y IS NOT NULL AND rm.pos_z IS NOT NULL ";
  activeReadModelSource += "AND rm.owner_kind IN ('world','world_item','item') AND cit.symbol_index IS NOT NULL ";
  activeReadModelSource += "AND (((rm.pos_x-h.hx)*(rm.pos_x-h.hx))+((rm.pos_y-h.hy)*(rm.pos_y-h.hy))+((rm.pos_z-h.hz)*(rm.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")";

  const auto sourceRowsToJsonArray = [](const std::string& sourceSql) {
    std::string query;
    query += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (SELECT row_json FROM (";
    query += sourceSql;
    query += ") source_rows ORDER BY dist_sq ASC,owner_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapActiveWorldItemRows);
    query += ") ordered_rows), JSON_ARRAY());";
    return query;
  };

  const auto activeWorldInventoryItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArray(activeWorldInventorySource),
      "[]",
      "bootstrap_active_world_inventory_items");
  const auto activeWorldEntityItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArray(activeWorldEntitySource),
      "[]",
      "bootstrap_active_world_entity_items");
  const auto activeReadModelItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArray(activeReadModelSource),
      "[]",
      "bootstrap_active_world_read_model_items");
  const auto activeWorldItems = concatenateJsonArrays({activeWorldInventoryItems, activeWorldEntityItems, activeReadModelItems});

  std::string activeWorldItemDebugQuery;
  activeWorldItemDebugQuery += "SELECT CONCAT('center=',ROUND(h.hx,2),',',ROUND(h.hy,2),',',ROUND(h.hz,2),' radius='," + activeItemRadiusSql + ",";
  activeWorldItemDebugQuery += "' wes_item_total=',(SELECT COUNT(*) FROM world_entity_state wes WHERE wes.world_instance_id=h.world_instance_id AND wes.entity_kind='item' AND wes.lifecycle_state='active'),";
  activeWorldItemDebugQuery += "' wes_item_near=',(SELECT COUNT(*) FROM world_entity_state wes WHERE wes.world_instance_id=h.world_instance_id AND wes.entity_kind='item' AND wes.lifecycle_state='active' AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")),";
  activeWorldItemDebugQuery += "' world_inventory_total=',(SELECT COUNT(*) FROM world_inventory wi WHERE wi.world_instance_id=h.world_instance_id),";
  activeWorldItemDebugQuery += "' world_inventory_item_near=',(SELECT COUNT(*) FROM world_inventory wi JOIN world_entity_state wes ON wes.world_instance_id=wi.world_instance_id AND wes.entity_key=wi.owner_entity_key WHERE wi.world_instance_id=h.world_instance_id AND wes.entity_kind='item' AND wes.lifecycle_state='active' AND wi.amount>0 AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + ")),";
  activeWorldItemDebugQuery += "' read_model_near=',(SELECT COUNT(*) FROM mmo_server_world_inventory_read_model rm WHERE rm.lifecycle_state='active' AND rm.amount>0 AND rm.pos_x IS NOT NULL AND rm.pos_y IS NOT NULL AND rm.pos_z IS NOT NULL AND rm.world_name=COALESCE(cwt.world_name,rwi.world_instance_key," + worldSql + ") AND (((rm.pos_x-h.hx)*(rm.pos_x-h.hx))+((rm.pos_y-h.hy)*(rm.pos_y-h.hy))+((rm.pos_z-h.hz)*(rm.pos_z-h.hz))) <= (" + activeItemRadiusSql + "*" + activeItemRadiusSql + "))) ";
  activeWorldItemDebugQuery += "FROM " + activeHeroSubquery + " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  activeWorldItemDebugQuery += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id;";
  const auto activeWorldItemDebug = mysqlSingleFieldWithDiagnostic(target, activeWorldItemDebugQuery, "bootstrap_active_world_items_debug");
  std::cout << "[bootstrap_active_world_items] bytes=" << activeWorldItems.size()
            << " world_inventory_bytes=" << activeWorldInventoryItems.size()
            << " world_entity_bytes=" << activeWorldEntityItems.size()
            << " read_model_bytes=" << activeReadModelItems.size();
  if(!activeWorldItemDebug.empty())
    std::cout << ' ' << activeWorldItemDebug;
  std::cout << "\n";

  const std::string nearbyNpcRadiusSql = std::to_string(Mmo::Server::BootstrapNearbyNpcRadius);
  const std::string nearbyWaypointRadiusSql = std::to_string(Mmo::Server::BootstrapNearbyWaypointRadius);

  std::string nearbyNpcIdentitySource;
  nearbyNpcIdentitySource += "SELECT h.character_id,wes.world_instance_id,wes.entity_key,wes.entity_kind,wes.lifecycle_state,wes.pos_x,wes.pos_y,wes.pos_z,wes.rotation_yaw,wes.health_current,wes.health_max,wes.updated_at,";
  nearbyNpcIdentitySource += "cet.engine_template_key AS entity_template_key,";
  nearbyNpcIdentitySource += "COALESCE(cet.symbol_index,CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)) AS symbol_index,";
  nearbyNpcIdentitySource += "COALESCE(cet.script_id,CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED)) AS script_id,";
  nearbyNpcIdentitySource += "COALESCE(cet.script_name,JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_name'))) AS script_name,";
  nearbyNpcIdentitySource += "COALESCE(cet.display_name,JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.display_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.name')),wes.entity_key) AS display_name,";
  nearbyNpcIdentitySource += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.current_waypoint_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.current_waypoint_key')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.current_waypoint')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.waypoint'))) AS current_waypoint,";
  nearbyNpcIdentitySource += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.routine_waypoint_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.routine_waypoint')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.path_next_waypoint_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.move_target_waypoint_name'))) AS routine_waypoint,";
  nearbyNpcIdentitySource += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.ai_state_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.state_name')),JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.move_hint'))) AS ai_state_name,";
  nearbyNpcIdentitySource += "((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz)) AS dist_sq ";
  nearbyNpcIdentitySource += "FROM " + activeHeroSubquery + " JOIN world_entity_state wes ON wes.world_instance_id=h.world_instance_id ";
  nearbyNpcIdentitySource += "LEFT JOIN content_entity_templates cet ON cet.entity_template_id=wes.entity_template_id ";
  nearbyNpcIdentitySource += "WHERE wes.entity_kind IN ('npc','creature') AND wes.lifecycle_state IN ('active','dead','disabled') ";
  nearbyNpcIdentitySource += "AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL ";
  nearbyNpcIdentitySource += "AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + nearbyNpcRadiusSql + "*" + nearbyNpcRadiusSql + ")";

  std::string nearbyNpcSource;
  nearbyNpcSource += "SELECT JSON_OBJECT('entity_key',src.entity_key,'entity_kind',src.entity_kind,'lifecycle_state',src.lifecycle_state,";
  nearbyNpcSource += "'entity_template_key',src.entity_template_key,'symbol_index',src.symbol_index,'script_id',src.script_id,'script_name',src.script_name,'display_name',src.display_name,";
  nearbyNpcSource += "'health_current',src.health_current,'health_max',src.health_max,'pos_x',src.pos_x,'pos_y',src.pos_y,'pos_z',src.pos_z,'rotation_yaw',src.rotation_yaw,";
  nearbyNpcSource += "'current_waypoint',src.current_waypoint,'routine_waypoint',src.routine_waypoint,'ai_state_name',src.ai_state_name,";
  nearbyNpcSource += "'distance',SQRT(src.dist_sq),'updated_at',DATE_FORMAT(src.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,src.dist_sq,src.entity_key AS owner_key ";
  nearbyNpcSource += "FROM (" + nearbyNpcIdentitySource + ") src";

  std::string nearbyNpcDialogSource;
  nearbyNpcDialogSource += "SELECT JSON_OBJECT('npc_key',d.npc_key,'info_key',d.info_key,'known',d.known,'permanent',d.permanent,'availability_state',d.availability_state,";
  nearbyNpcDialogSource += "'nearby_entity_key',npc.entity_key,'nearby_display_name',npc.display_name,'nearby_distance',SQRT(npc.dist_sq),'updated_at',DATE_FORMAT(d.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,npc.dist_sq,CONCAT(d.npc_key,':',d.info_key) AS owner_key ";
  nearbyNpcDialogSource += "FROM (" + nearbyNpcIdentitySource + ") npc JOIN character_known_dialogs d ON d.character_id=npc.character_id ";
  nearbyNpcDialogSource += "WHERE d.npc_key IN (npc.entity_key,npc.script_name,npc.display_name,CAST(npc.symbol_index AS CHAR),CAST(npc.script_id AS CHAR))";

  std::string nearbyWaypointSource;
  nearbyWaypointSource += "SELECT JSON_OBJECT('waypoint_key',wp.waypoint_key,'waypoint_name',wp.waypoint_name,'kind_key',wp.kind_key,'pos_x',wp.pos_x,'pos_y',wp.pos_y,'pos_z',wp.pos_z,";
  nearbyWaypointSource += "'distance',SQRT(((wp.pos_x-h.hx)*(wp.pos_x-h.hx))+((wp.pos_y-h.hy)*(wp.pos_y-h.hy))+((wp.pos_z-h.hz)*(wp.pos_z-h.hz))),'updated_at',DATE_FORMAT(wp.materialized_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json,";
  nearbyWaypointSource += "((wp.pos_x-h.hx)*(wp.pos_x-h.hx))+((wp.pos_y-h.hy)*(wp.pos_y-h.hy))+((wp.pos_z-h.hz)*(wp.pos_z-h.hz)) AS dist_sq,wp.waypoint_key AS owner_key ";
  nearbyWaypointSource += "FROM " + activeHeroSubquery + " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  nearbyWaypointSource += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  nearbyWaypointSource += "JOIN mmo_server_waypoint_read_model wp ON wp.world_name=COALESCE(cwt.world_name,rwi.world_instance_key," + worldSql + ") ";
  nearbyWaypointSource += "WHERE wp.pos_x IS NOT NULL AND wp.pos_y IS NOT NULL AND wp.pos_z IS NOT NULL ";
  nearbyWaypointSource += "AND (((wp.pos_x-h.hx)*(wp.pos_x-h.hx))+((wp.pos_y-h.hy)*(wp.pos_y-h.hy))+((wp.pos_z-h.hz)*(wp.pos_z-h.hz))) <= (" + nearbyWaypointRadiusSql + "*" + nearbyWaypointRadiusSql + ")";

  const auto sourceRowsToJsonArrayWithLimit = [](const std::string& sourceSql, std::size_t limit) {
    std::string query;
    query += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (SELECT row_json FROM (";
    query += sourceSql;
    query += ") source_rows ORDER BY dist_sq ASC,owner_key LIMIT " + std::to_string(limit);
    query += ") ordered_rows), JSON_ARRAY());";
    return query;
  };

  const auto nearbyNpcs = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArrayWithLimit(nearbyNpcSource, Mmo::Server::MaxBootstrapNearbyNpcRows),
      "[]",
      "bootstrap_nearby_npcs");
  const auto nearbyNpcKnownDialogs = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArrayWithLimit(nearbyNpcDialogSource, Mmo::Server::MaxBootstrapNearbyNpcKnownDialogRows),
      "[]",
      "bootstrap_nearby_npc_known_dialogs");
  const auto nearbyWaypoints = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToJsonArrayWithLimit(nearbyWaypointSource, Mmo::Server::MaxBootstrapNearbyWaypointRows),
      "[]",
      "bootstrap_nearby_waypoints");

  std::string nearbyNpcDebugQuery;
  nearbyNpcDebugQuery += "SELECT CONCAT('center=',ROUND(h.hx,2),',',ROUND(h.hy,2),',',ROUND(h.hz,2),' radius='," + nearbyNpcRadiusSql + ",";
  nearbyNpcDebugQuery += "' npc_total=',(SELECT COUNT(*) FROM world_entity_state wes WHERE wes.world_instance_id=h.world_instance_id AND wes.entity_kind IN ('npc','creature')),";
  nearbyNpcDebugQuery += "' npc_near=',(SELECT COUNT(*) FROM world_entity_state wes WHERE wes.world_instance_id=h.world_instance_id AND wes.entity_kind IN ('npc','creature') AND wes.lifecycle_state IN ('active','dead','disabled') AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL AND (((wes.pos_x-h.hx)*(wes.pos_x-h.hx))+((wes.pos_y-h.hy)*(wes.pos_y-h.hy))+((wes.pos_z-h.hz)*(wes.pos_z-h.hz))) <= (" + nearbyNpcRadiusSql + "*" + nearbyNpcRadiusSql + ")),";
  nearbyNpcDebugQuery += "' waypoint_near=',(SELECT COUNT(*) FROM mmo_server_waypoint_read_model wp WHERE wp.world_name=COALESCE(cwt.world_name,rwi.world_instance_key," + worldSql + ") AND wp.pos_x IS NOT NULL AND wp.pos_y IS NOT NULL AND wp.pos_z IS NOT NULL AND (((wp.pos_x-h.hx)*(wp.pos_x-h.hx))+((wp.pos_y-h.hy)*(wp.pos_y-h.hy))+((wp.pos_z-h.hz)*(wp.pos_z-h.hz))) <= (" + nearbyWaypointRadiusSql + "*" + nearbyWaypointRadiusSql + "))) ";
  nearbyNpcDebugQuery += "FROM " + activeHeroSubquery + " JOIN realm_world_instances rwi ON rwi.world_instance_id=h.world_instance_id ";
  nearbyNpcDebugQuery += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id;";
  const auto nearbyNpcDebug = mysqlSingleFieldWithDiagnostic(target, nearbyNpcDebugQuery, "bootstrap_nearby_npcs_debug");
  std::cout << "[bootstrap_nearby_npcs] bytes=" << nearbyNpcs.size()
            << " known_dialog_bytes=" << nearbyNpcKnownDialogs.size()
            << " waypoint_bytes=" << nearbyWaypoints.size();
  if(!nearbyNpcDebug.empty())
    std::cout << ' ' << nearbyNpcDebug;
  std::cout << "\n";

  std::string interactiveQuery;
  interactiveQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  interactiveQuery += "SELECT JSON_OBJECT('entity_key',wes.entity_key,'lifecycle_state',wes.lifecycle_state,";
  interactiveQuery += "'state_id',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.state_id')) AS SIGNED),";
  interactiveQuery += "'locked',JSON_EXTRACT(wes.state_json,'$.locked'),'cracked',JSON_EXTRACT(wes.state_json,'$.cracked'),";
  interactiveQuery += "'updated_at',DATE_FORMAT(wes.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  interactiveQuery += "FROM server_sessions ss JOIN world_entity_state wes ON wes.world_instance_id=ss.world_instance_id ";
  interactiveQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND wes.entity_kind='interactive' ";
  interactiveQuery += "ORDER BY wes.updated_at DESC,wes.entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapInteractiveSampleRows);
  interactiveQuery += ") rows_json), JSON_ARRAY());";
  const auto interactivesSample = mysqlJsonOr(target, interactiveQuery, "[]");

  std::string npcLifecycleQuery;
  npcLifecycleQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcLifecycleQuery += "SELECT JSON_OBJECT('entity_key',wes.entity_key,'entity_kind',wes.entity_kind,'lifecycle_state',wes.lifecycle_state,";
  npcLifecycleQuery += "'persistent_id',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED),";
  npcLifecycleQuery += "'symbol_index',COALESCE(CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED),CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED)),";
  npcLifecycleQuery += "'health_current',wes.health_current,'health_max',wes.health_max,";
  npcLifecycleQuery += "'pos_x',wes.pos_x,'pos_y',wes.pos_y,'pos_z',wes.pos_z,";
  npcLifecycleQuery += "'updated_at',DATE_FORMAT(wes.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcLifecycleQuery += "FROM server_sessions ss JOIN world_entity_state wes ON wes.world_instance_id=ss.world_instance_id ";
  npcLifecycleQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND wes.entity_kind IN ('npc','creature') ";
  npcLifecycleQuery += "AND (wes.lifecycle_state<>'active' OR (wes.health_current IS NOT NULL AND wes.health_max IS NOT NULL AND wes.health_current<wes.health_max)) ";
  npcLifecycleQuery += "ORDER BY CASE WHEN wes.lifecycle_state='dead' THEN 0 WHEN wes.lifecycle_state<>'active' THEN 1 ELSE 2 END,wes.updated_at DESC,wes.entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcLifecycleRows);
  npcLifecycleQuery += ") rows_json), JSON_ARRAY());";
  const auto npcLifecycle = mysqlJsonOr(target, npcLifecycleQuery, "[]");

  std::string recentEventsQuery;
  recentEventsQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  recentEventsQuery += "SELECT JSON_OBJECT('event_seq',wej.event_seq,'event_type',wej.event_type,'event_class',wej.event_class,";
  recentEventsQuery += "'entity_key',wej.entity_key,'subject_key',wej.subject_key,'server_tick',wej.server_tick,";
  recentEventsQuery += "'occurred_at',DATE_FORMAT(wej.occurred_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  recentEventsQuery += "FROM server_sessions ss JOIN world_event_journal wej ON wej.world_instance_id=ss.world_instance_id ";
  recentEventsQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY wej.event_seq DESC LIMIT " + std::to_string(Mmo::Server::MaxBootstrapRecentEventRows);
  recentEventsQuery += ") rows_json), JSON_ARRAY());";
  const auto recentEvents = mysqlJsonOr(target, recentEventsQuery, "[]");

  std::string moverStateQuery;
  moverStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  moverStateQuery += "SELECT JSON_OBJECT('mover_key',ms.mover_key,'state_after',ms.state_after,'state_after_name',ms.state_after_name,";
  moverStateQuery += "'frame_index',ms.frame_index,'target_frame_index',ms.target_frame_index,'last_server_tick',ms.last_server_tick,";
  moverStateQuery += "'row_version',ms.row_version,'updated_at',DATE_FORMAT(ms.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  moverStateQuery += "FROM server_sessions ss JOIN mmo_world_mover_state_current ms ON ms.world_instance_id=ss.world_instance_id ";
  moverStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ms.last_server_tick DESC,ms.mover_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapMoverStateRows);
  moverStateQuery += ") rows_json), JSON_ARRAY());";
  const auto moverState = mysqlJsonOrWithDiagnostic(target, moverStateQuery, "[]", "bootstrap_mover_state");

  std::string npcRoutineStateQuery;
  npcRoutineStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcRoutineStateQuery += "SELECT JSON_OBJECT('npc_entity_key',rs.npc_entity_key,'routine_state',rs.routine_state,";
  npcRoutineStateQuery += "'schedule_key',rs.schedule_key,'current_waypoint_key',rs.current_waypoint_key,'target_waypoint_key',rs.target_waypoint_key,";
  npcRoutineStateQuery += "'last_server_tick',rs.last_server_tick,'row_version',rs.row_version,'updated_at',DATE_FORMAT(rs.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcRoutineStateQuery += "FROM server_sessions ss JOIN mmo_npc_routine_state_current rs ON rs.world_instance_id=ss.world_instance_id ";
  npcRoutineStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY rs.last_server_tick DESC,rs.npc_entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcAuthorityRows);
  npcRoutineStateQuery += ") rows_json), JSON_ARRAY());";
  const auto npcRoutineState = mysqlJsonOrWithDiagnostic(target, npcRoutineStateQuery, "[]", "bootstrap_npc_routine_state");

  std::string npcAiStateQuery;
  npcAiStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcAiStateQuery += "SELECT JSON_OBJECT('npc_entity_key',ais.npc_entity_key,'ai_state',ais.ai_state,'ai_intent',ais.ai_intent,";
  npcAiStateQuery += "'target_key',ais.target_key,'perception_state',ais.perception_state,'last_server_tick',ais.last_server_tick,";
  npcAiStateQuery += "'row_version',ais.row_version,'updated_at',DATE_FORMAT(ais.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcAiStateQuery += "FROM server_sessions ss JOIN mmo_npc_ai_state_current ais ON ais.world_instance_id=ss.world_instance_id ";
  npcAiStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ais.last_server_tick DESC,ais.npc_entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcAuthorityRows);
  npcAiStateQuery += ") rows_json), JSON_ARRAY());";
  const auto npcAiState = mysqlJsonOrWithDiagnostic(target, npcAiStateQuery, "[]", "bootstrap_npc_ai_state");

  std::string npcPathStateQuery;
  npcPathStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcPathStateQuery += "SELECT JSON_OBJECT('npc_entity_key',ps.npc_entity_key,'path_state',ps.path_state,'route_key',ps.route_key,";
  npcPathStateQuery += "'current_waypoint_key',ps.current_waypoint_key,'next_waypoint_key',ps.next_waypoint_key,'target_waypoint_key',ps.target_waypoint_key,";
  npcPathStateQuery += "'pos_x',ps.pos_x,'pos_y',ps.pos_y,'pos_z',ps.pos_z,'last_server_tick',ps.last_server_tick,";
  npcPathStateQuery += "'row_version',ps.row_version,'updated_at',DATE_FORMAT(ps.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcPathStateQuery += "FROM server_sessions ss JOIN mmo_npc_path_state_current ps ON ps.world_instance_id=ss.world_instance_id ";
  npcPathStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ps.last_server_tick DESC,ps.npc_entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcAuthorityRows);
  npcPathStateQuery += ") rows_json), JSON_ARRAY());";
  const auto npcPathState = mysqlJsonOrWithDiagnostic(target, npcPathStateQuery, "[]", "bootstrap_npc_path_state");

  std::string npcFightStateQuery;
  npcFightStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcFightStateQuery += "SELECT JSON_OBJECT('npc_entity_key',fs.npc_entity_key,'opponent_key',fs.opponent_key,'fight_state',fs.fight_state,";
  npcFightStateQuery += "'attack_state',fs.attack_state,'combo_index',fs.combo_index,'last_server_tick',fs.last_server_tick,";
  npcFightStateQuery += "'row_version',fs.row_version,'updated_at',DATE_FORMAT(fs.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcFightStateQuery += "FROM server_sessions ss JOIN mmo_npc_fight_state_current fs ON fs.world_instance_id=ss.world_instance_id ";
  npcFightStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY fs.last_server_tick DESC,fs.npc_entity_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapNpcAuthorityRows);
  npcFightStateQuery += ") rows_json), JSON_ARRAY());";
  const auto npcFightState = mysqlJsonOrWithDiagnostic(target, npcFightStateQuery, "[]", "bootstrap_npc_fight_state");

  std::string triggerQueueQuery;
  triggerQueueQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  triggerQueueQuery += "SELECT JSON_OBJECT('trigger_key',tq.trigger_key,'queue_state',tq.queue_state,'event_type_name',tq.event_type_name,";
  triggerQueueQuery += "'scheduled_server_tick',tq.scheduled_server_tick,'last_server_tick',tq.last_server_tick,";
  triggerQueueQuery += "'row_version',tq.row_version,'updated_at',DATE_FORMAT(tq.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  triggerQueueQuery += "FROM server_sessions ss JOIN mmo_world_trigger_queue_current tq ON tq.world_instance_id=ss.world_instance_id ";
  triggerQueueQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY tq.scheduled_server_tick ASC,tq.trigger_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapTriggerQueueRows);
  triggerQueueQuery += ") rows_json), JSON_ARRAY());";
  const auto triggerQueue = mysqlJsonOrWithDiagnostic(target, triggerQueueQuery, "[]", "bootstrap_trigger_queue");

  std::string worldTransitionStateQuery;
  worldTransitionStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  worldTransitionStateQuery += "SELECT JSON_OBJECT('character_key',c.character_key,'from_world_key',ts.from_world_key,'to_world_key',ts.to_world_key,";
  worldTransitionStateQuery += "'transition_state',ts.transition_state,'chapter_key',ts.chapter_key,'visited',ts.visited,";
  worldTransitionStateQuery += "'last_server_tick',ts.last_server_tick,'row_version',ts.row_version,'updated_at',DATE_FORMAT(ts.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  worldTransitionStateQuery += "FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id ";
  worldTransitionStateQuery += "JOIN mmo_character_world_transition_state_current ts ON ts.character_id=ss.character_id ";
  worldTransitionStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ts.last_server_tick DESC,ts.to_world_key LIMIT " + std::to_string(Mmo::Server::MaxBootstrapWorldTransitionRows);
  worldTransitionStateQuery += ") rows_json), JSON_ARRAY());";
  const auto worldTransitionState = mysqlJsonOrWithDiagnostic(target, worldTransitionStateQuery, "[]", "bootstrap_world_transition_state");

  std::string clientCorrectionsQuery;
  clientCorrectionsQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  clientCorrectionsQuery += "SELECT JSON_OBJECT('action_kind',cc.action_kind,'client_local_sequence',cc.client_local_sequence,'correction_kind',cc.correction_kind,";
  clientCorrectionsQuery += "'reason',cc.reason,'acknowledged',cc.acknowledged,'rejected_server_tick',cc.rejected_server_tick,";
  clientCorrectionsQuery += "'authoritative_server_tick',cc.authoritative_server_tick,'authoritative_pos_x',cc.authoritative_pos_x,";
  clientCorrectionsQuery += "'authoritative_pos_y',cc.authoritative_pos_y,'authoritative_pos_z',cc.authoritative_pos_z,'authoritative_yaw',cc.authoritative_yaw,";
  clientCorrectionsQuery += "'updated_at',DATE_FORMAT(cc.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  clientCorrectionsQuery += "FROM server_sessions ss JOIN mmo_client_action_correction_current cc ON cc.session_id=ss.session_id ";
  clientCorrectionsQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND cc.acknowledged=FALSE ORDER BY cc.updated_at DESC LIMIT " + std::to_string(Mmo::Server::MaxBootstrapClientCorrectionRows);
  clientCorrectionsQuery += ") rows_json), JSON_ARRAY());";
  const auto clientCorrections = mysqlJsonOrWithDiagnostic(target, clientCorrectionsQuery, "[]", "bootstrap_client_corrections");

  std::string checkpointManifestQuery;
  checkpointManifestQuery += "SELECT COALESCE((SELECT JSON_OBJECT(";
  checkpointManifestQuery += "'manifest_uuid',BIN_TO_UUID(sm.manifest_id,1),'manifest_key',sm.manifest_key,'save_slot_key',sm.save_slot_key,'native_save_path',sm.native_save_path,";
  checkpointManifestQuery += "'display_name',sm.display_name,'client_world_name',sm.client_world_name,'native_save_present',JSON_EXTRACT(IF(sm.native_save_present<>0,'true','false'),'$'),";
  checkpointManifestQuery += "'checkpoint_kind',sm.checkpoint_kind,'reason',sm.reason,";
  checkpointManifestQuery += "'server_tick',sm.server_tick,'latest_checkpoint_tick',sm.latest_checkpoint_tick,'recent_event_seq',sm.recent_event_seq,";
  checkpointManifestQuery += "'inventory_rows',sm.inventory_rows,'equipment_rows',sm.equipment_rows,'quest_rows',sm.quest_rows,'known_dialog_rows',sm.known_dialog_rows,";
  checkpointManifestQuery += "'script_state_rows',sm.script_state_rows,'world_item_rows',sm.world_item_rows,'world_inventory_rows',sm.world_inventory_rows,";
  checkpointManifestQuery += "'interactive_rows',sm.interactive_rows,'npc_lifecycle_rows',sm.npc_lifecycle_rows,'mover_rows',sm.mover_rows,";
  checkpointManifestQuery += "'row_version',sm.row_version,'created_at',DATE_FORMAT(sm.created_at,'%Y-%m-%dT%H:%i:%s.%fZ')) ";
  checkpointManifestQuery += "FROM server_sessions ss JOIN mmo_save_checkpoint_manifests sm ON sm.character_id=ss.character_id AND sm.world_instance_id=ss.world_instance_id ";
  checkpointManifestQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY sm.created_at DESC LIMIT 1), JSON_OBJECT());";
  const auto checkpointManifest = mysqlJsonOrWithDiagnostic(target, checkpointManifestQuery, "{}", "bootstrap_save_checkpoint_manifest");

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

void applyLiveBootstrapFallback(const MySqlTarget& target,
                                std::string_view sessionUuid,
                                BootstrapReadiness& readiness) {
  if(sessionUuid.empty())
    return;

  std::string sql;
  sql += "SELECT ";
  sql += "(SELECT COUNT(*) FROM characters c JOIN server_sessions ss ON ss.character_id=c.character_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1)),";
  sql += "(SELECT COUNT(*) FROM character_inventory ci JOIN server_sessions ss ON ss.character_id=ci.character_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1)),";
  sql += "(SELECT COUNT(*) FROM character_quests q JOIN server_sessions ss ON ss.character_id=q.character_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1)),";
  sql += "(SELECT COUNT(*) FROM character_known_dialogs d JOIN server_sessions ss ON ss.character_id=d.character_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1)),";
  sql += "(SELECT COUNT(*) FROM character_script_state s JOIN server_sessions ss ON ss.character_id=s.character_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1)),";
  sql += "(SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1)),";
  sql += "(SELECT COUNT(*) FROM world_inventory wi JOIN server_sessions ss ON ss.world_instance_id=wi.world_instance_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1)),";
  sql += "(SELECT COUNT(*) FROM world_entity_state wes JOIN server_sessions ss ON ss.world_instance_id=wes.world_instance_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1) AND wes.entity_kind='interactive'),";
  sql += "(SELECT COUNT(*) FROM realm_world_instances wi JOIN server_sessions ss ON ss.world_instance_id=wi.world_instance_id WHERE ss.session_id=UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ",1))";

  const auto parts = splitMysqlLastRow(runMysql(target, sql));
  if(parts.size() < 9)
    return;
  const std::uint64_t liveCharacterRows = parseU64OrZero(parts[0]);
  const std::uint64_t liveInventoryRows = parseU64OrZero(parts[1]);
  const std::uint64_t liveQuestRows = parseU64OrZero(parts[2]);
  const std::uint64_t liveDialogRows = parseU64OrZero(parts[3]);
  const std::uint64_t liveScriptRows = parseU64OrZero(parts[4]);
  const std::uint64_t liveWorldEntityRows = parseU64OrZero(parts[5]);
  const std::uint64_t liveWorldInventoryRows = parseU64OrZero(parts[6]);
  const std::uint64_t liveInteractiveRows = parseU64OrZero(parts[7]);
  const std::uint64_t liveClockRows = parseU64OrZero(parts[8]);

  if(readiness.characterRows == 0)
    readiness.characterRows = liveCharacterRows;
  if(readiness.characterInventoryRows == 0)
    readiness.characterInventoryRows = liveInventoryRows;
  if(readiness.questRows == 0)
    readiness.questRows = liveQuestRows;
  if(readiness.knownDialogRows == 0)
    readiness.knownDialogRows = liveDialogRows;
  if(readiness.scriptIntRows == 0)
    readiness.scriptIntRows = liveScriptRows;
  if(readiness.worldEntityRows == 0)
    readiness.worldEntityRows = liveWorldEntityRows;
  if(readiness.worldInventoryRows == 0)
    readiness.worldInventoryRows = liveWorldInventoryRows;
  if(readiness.interactiveRows == 0)
    readiness.interactiveRows = liveInteractiveRows;
  if(readiness.worldClockRows == 0)
    readiness.worldClockRows = liveClockRows;

  readiness.ready = readiness.characterRows > 0 && readiness.worldEntityRows > 0;
}

[[nodiscard]] BootstrapReadiness readBootstrapReadiness(const MySqlTarget& target,
                                                        std::string_view characterKey,
                                                        std::string_view worldName) {
  std::string sql;
  const std::string characterSql = sqlLiteral(characterKey);
  const std::string worldSql = sqlLiteral(worldName);
  sql += "SELECT ";
  sql += "(SELECT COUNT(*) FROM mmo_server_read_model_meta),";
  sql += "(SELECT COUNT(*) FROM mmo_server_character_read_model WHERE character_key=";
  sql += characterSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_world_entity_read_model WHERE world_name=";
  sql += worldSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_character_inventory_read_model WHERE character_key=";
  sql += characterSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_character_quest_read_model WHERE character_key=";
  sql += characterSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_known_dialog_read_model WHERE character_key=";
  sql += characterSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_script_int_read_model),";
  sql += "(SELECT COUNT(*) FROM mmo_server_waypoint_read_model WHERE world_name=";
  sql += worldSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_waypoint_edge_read_model WHERE world_name=";
  sql += worldSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_world_inventory_read_model WHERE world_name=";
  sql += worldSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_interactive_read_model WHERE world_name=";
  sql += worldSql;
  sql += "),";
  sql += "(SELECT COUNT(*) FROM mmo_server_world_clock_read_model WHERE world_name=";
  sql += worldSql;
  sql += ")";

  const auto raw = runMysql(target, sql);
  std::vector<std::string_view> parts;
  std::string_view line = raw;
  const auto nl = line.rfind('\n');
  if(nl != std::string_view::npos)
    line.remove_prefix(nl + 1);
  while(true) {
    const auto tab = line.find('\t');
    if(tab == std::string_view::npos) {
      parts.push_back(line);
      break;
    }
    parts.push_back(line.substr(0, tab));
    line.remove_prefix(tab + 1);
  }

  BootstrapReadiness out;
  if(parts.size() >= 12) {
    out.metaRows = parseU64OrZero(parts[0]);
    out.characterRows = parseU64OrZero(parts[1]);
    out.worldEntityRows = parseU64OrZero(parts[2]);
    out.characterInventoryRows = parseU64OrZero(parts[3]);
    out.questRows = parseU64OrZero(parts[4]);
    out.knownDialogRows = parseU64OrZero(parts[5]);
    out.scriptIntRows = parseU64OrZero(parts[6]);
    out.waypointRows = parseU64OrZero(parts[7]);
    out.waypointEdgeRows = parseU64OrZero(parts[8]);
    out.worldInventoryRows = parseU64OrZero(parts[9]);
    out.interactiveRows = parseU64OrZero(parts[10]);
    out.worldClockRows = parseU64OrZero(parts[11]);
  }
  out.ready = out.metaRows > 0 && out.characterRows > 0 && out.worldEntityRows > 0;
  return out;
}

[[nodiscard]] BootstrapReadiness readBootstrapReadinessWithFallback(const MySqlTarget& target,
                                                                    std::string_view characterKey,
                                                                    std::string_view worldName,
                                                                    std::string_view sessionUuid,
                                                                    std::string& selectedWorldName) {
  selectedWorldName = std::string(worldName);
  BootstrapReadiness readiness = readBootstrapReadiness(target, characterKey, selectedWorldName);
  applyLiveBootstrapFallback(target, sessionUuid, readiness);
  if(readiness.worldEntityRows > 0)
    return readiness;

  const std::string normalized = normalizedWorldName(worldName);
  if(normalized != selectedWorldName) {
    BootstrapReadiness normalizedReadiness = readBootstrapReadiness(target, characterKey, normalized);
    applyLiveBootstrapFallback(target, sessionUuid, normalizedReadiness);
    if(normalizedReadiness.worldEntityRows > readiness.worldEntityRows) {
      selectedWorldName = normalized;
      return normalizedReadiness;
    }
  }
  return readiness;
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

void logConversationLateObserverResumePlans(asio::ip::udp::socket& socket,
                                            Mmo::Server::ConversationSessionRuntime& conversationRuntime,
                                            const ClientRouteRegistry& routes,
                                            Mmo::Server::OutboundGameplayDeliveryState& outboundGameplayDelivery,
                                            Mmo::Server::ConversationResumeSendGate& resumeSendGate,
                                            Mmo::Server::ConversationResumeDeliveryRegistrationBoundary& resumeDeliveryRegistrationBoundary,
                                            Mmo::Server::ConversationResumeDispatchEnvelope& resumeDispatchEnvelope,
                                            Mmo::Server::ConversationResumeMutationGuard& resumeMutationGuard,
                                            Mmo::Server::ConversationResumeSendFailureDeadLetterGuard& resumeSendFailureDeadLetterGuard,
                                            Mmo::Server::ConversationResumeCommitPreflight& resumeCommitPreflight,
                                            const MySqlTarget* aiRuntimeTarget,
                                            const Options& opt,
                                            std::string_view sessionUuid,
                                            std::string_view characterKey,
                                            std::string_view worldName,
                                            const Mmo::Net::ClientActionPacket& packet,
                                            std::uint64_t& nextDialogIntentSequence) noexcept {
  if(!opt.aiDialogIntentConversationSessionProbe || !opt.aiDialogIntentLateObserverResumePlan)
    return;

  try {
    Mmo::Server::ConversationLateObserverResumeRequest request;
    request.sessionUuid = std::string(sessionUuid);
    request.characterKey = std::string(characterKey);
    request.worldName = std::string(worldName);
    request.nowMs = monotonicNowMs();
    request.serverTick = packetServerTick(packet);

    const auto plans = conversationRuntime.planLateObserverResume(std::move(request));
    std::uint64_t previewPacketSequence = nextDialogIntentSequence + 1;
    for(const auto& plan : plans) {
      if(!plan.canResume)
        continue;
      std::cout << "[server_conversation_late_observer_resume_plan]"
                << " conversation_id=" << plan.conversationId
                << " observer_session_uuid=" << plan.sessionUuid
                << " character=" << plan.characterKey
                << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                << " status=" << Mmo::Server::conversationLateObserverResumeStatusName(plan.status)
                << " can_resume=" << (plan.canResume ? 1 : 0)
                << " speaker=" << plan.speakerEntityKey
                << " speaker_npc_instance_uuid=" << plan.speakerNpcInstanceUuid
                << " line=" << plan.lineId
                << " audio_ref=" << plan.audioRef
                << " elapsed_ms=" << plan.elapsedMs
                << " remaining_ms=" << plan.remainingMs
                << " duration_ms=" << plan.durationMs
                << " line_start_tick=" << plan.lineStartTick
                << " observer_server_tick=" << plan.observerServerTick
                << " known_observers=" << plan.knownObservers
                << " packet_boundary=" << (opt.aiDialogIntentLateObserverResumePacketBoundary ? "on" : "off")
                << " send_resume_packet=off"
                << " db_conversation_storage=off"
                << " mark_applied=off"
                << "\n";

      if(opt.aiDialogIntentLateObserverResumePacketBoundary) {
        Mmo::Server::ConversationResumePacketBuildRequest buildRequest;
        buildRequest.resume = plan;
        buildRequest.targetCharacterKey = plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey;
        buildRequest.worldInstanceUuid = opt.worldInstanceKey;
        buildRequest.text = opt.aiDialogIntentText;
        buildRequest.packetSequence = opt.aiDialogIntentLateObserverResumeRuntimeSend ? ++nextDialogIntentSequence : previewPacketSequence++;
        buildRequest.localSequence = packet.localSequence;
        buildRequest.serverTick = packetServerTick(packet);
        const auto packetBoundary = Mmo::Server::buildConversationResumeDialogIntentPacketNoSend(std::move(buildRequest));
        std::cout << "[server_conversation_late_observer_resume_packet_boundary]"
                  << " conversation_id=" << plan.conversationId
                  << " observer_session_uuid=" << plan.sessionUuid
                  << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                  << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                  << " status=" << Mmo::Server::conversationResumePacketBoundaryStatusName(packetBoundary.status)
                  << " buildable=" << (packetBoundary.buildable ? 1 : 0)
                  << " action_id=" << packetBoundary.packet.actionId
                  << " ack_key=" << packetBoundary.packet.ackKey
                  << " packet_sequence=" << packetBoundary.packet.packetSequence
                  << " server_tick=" << packetBoundary.packet.serverTick
                  << " start_tick=" << packetBoundary.packet.startTick
                  << " remaining_duration_ms=" << packetBoundary.packet.durationMs
                  << " original_line_start_tick=" << plan.lineStartTick
                  << " elapsed_ms=" << plan.elapsedMs
                  << " encoded_bytes=" << packetBoundary.encodedBytes
                  << " reason=" << packetBoundary.reason
                  << " send_gate=" << (opt.aiDialogIntentLateObserverResumeSendGate ? "on" : "off")
                  << " delivery_registration_boundary=" << (opt.aiDialogIntentLateObserverResumeDeliveryRegistrationBoundary ? "on" : "off")
                  << " dispatch_envelope=" << (opt.aiDialogIntentLateObserverResumeDispatchEnvelope ? "on" : "off")
                  << " mutation_guard=" << (opt.aiDialogIntentLateObserverResumeMutationGuard ? "on" : "off")
                  << " send_failure_dead_letter_guard=" << (opt.aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard ? "on" : "off")
                  << " commit_preflight=" << (opt.aiDialogIntentLateObserverResumeCommitPreflight ? "on" : "off")
                  << " persistence_execution_boundary=" << (opt.aiDialogIntentStep273PersistenceExecutionBoundary ? "on" : "off")
                  << " persistence_executor_adapter=" << (opt.aiDialogIntentStep273PersistenceExecutorAdapter ? "on" : "off")
                  << " persistence_dry_run_report_gate=" << (opt.aiDialogIntentStep273PersistenceDryRunReportGate ? "on" : "off")
                  << " outcome_policy=" << (opt.aiDialogIntentStep273PersistenceOutcomePolicy ? "on" : "off")
                  << " outcome_observability=" << (opt.aiDialogIntentStep273PersistenceOutcomeObservability ? "on" : "off")
                  << " activation_preflight=" << (opt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off")
                  << " send_resume_packet=off"
                  << " delivery_registry=off"
                  << " db_conversation_storage=off"
                  << " mark_applied=off"
                  << "\n";

        if(opt.aiDialogIntentLateObserverResumeSendGate) {
          const auto route = routes.find(plan.sessionUuid);
          Mmo::Server::ConversationResumeSendGateRequest gateRequest;
          gateRequest.packetBoundary = &packetBoundary;
          gateRequest.gateEnabled = true;
          gateRequest.endpointAvailable = route.has_value();
          gateRequest.routeSessionUuid = route.has_value() ? plan.sessionUuid : std::string();
          gateRequest.nowMs = monotonicNowMs();
          const auto gate = resumeSendGate.classifyNoSend(gateRequest);
          std::cout << "[server_conversation_late_observer_resume_send_gate]"
                    << " conversation_id=" << plan.conversationId
                    << " observer_session_uuid=" << plan.sessionUuid
                    << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                    << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                    << " status=" << Mmo::Server::conversationResumeSendGateStatusName(gate.status)
                    << " eligible=" << (gate.eligible ? 1 : 0)
                    << " duplicate=" << (gate.duplicate ? 1 : 0)
                    << " endpoint_available=" << (route.has_value() ? 1 : 0)
                    << " action_id=" << gate.actionId
                    << " ack_key=" << gate.ackKey
                    << " packet_sequence=" << gate.packetSequence
                    << " encoded_bytes=" << gate.encodedBytes
                    << " would_send_udp=" << (gate.wouldSendUdp ? 1 : 0)
                    << " would_register_delivery=" << (gate.wouldRegisterDelivery ? 1 : 0)
                    << " would_register_conversation_observer=" << (gate.wouldRegisterConversationObserver ? 1 : 0)
                    << " reason=" << gate.reason
                    << " send_resume_packet=off"
                    << " delivery_registry=off"
                    << " dispatch_envelope=" << (opt.aiDialogIntentLateObserverResumeDispatchEnvelope ? "on" : "off")
                    << " mutation_guard=" << (opt.aiDialogIntentLateObserverResumeMutationGuard ? "on" : "off")
                    << " send_failure_dead_letter_guard=" << (opt.aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard ? "on" : "off")
                    << " commit_preflight=" << (opt.aiDialogIntentLateObserverResumeCommitPreflight ? "on" : "off")
                    << " persistence_execution_boundary=" << (opt.aiDialogIntentStep273PersistenceExecutionBoundary ? "on" : "off")
                    << " persistence_executor_adapter=" << (opt.aiDialogIntentStep273PersistenceExecutorAdapter ? "on" : "off")
                    << " persistence_dry_run_report_gate=" << (opt.aiDialogIntentStep273PersistenceDryRunReportGate ? "on" : "off")
                    << " outcome_policy=" << (opt.aiDialogIntentStep273PersistenceOutcomePolicy ? "on" : "off")
                    << " outcome_observability=" << (opt.aiDialogIntentStep273PersistenceOutcomeObservability ? "on" : "off")
                  << " activation_preflight=" << (opt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off")
                    << " db_conversation_storage=off"
                    << " mark_applied=off"
                    << "\n";

          if(opt.aiDialogIntentLateObserverResumeRuntimeSend) {
            Mmo::Server::ConversationResumeRuntimeSendRequest runtimeSendRequest;
            runtimeSendRequest.enabled = true;
            runtimeSendRequest.packetBoundary = &packetBoundary;
            runtimeSendRequest.sendGate = &gate;
            runtimeSendRequest.endpointAvailable = route.has_value();
            runtimeSendRequest.durableStorageEnabled = opt.aiDialogIntentStep273PersistenceRuntimeStorage;
            runtimeSendRequest.mysqlTargetAvailable = aiRuntimeTarget != nullptr;
            const auto runtimeSend = Mmo::Server::buildConversationResumeRuntimeSendPreflight(runtimeSendRequest);
            std::cout << "[server_conversation_late_observer_resume_runtime_send_preflight]"
                      << " conversation_id=" << plan.conversationId
                      << " observer_session_uuid=" << plan.sessionUuid
                      << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                      << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                      << " status=" << Mmo::Server::conversationResumeRuntimeSendStatusName(runtimeSend.status)
                      << " ready=" << (runtimeSend.ready ? 1 : 0)
                      << " action_id=" << runtimeSend.actionId
                      << " ack_key=" << runtimeSend.ackKey
                      << " packet_sequence=" << runtimeSend.packetSequence
                      << " encoded_bytes=" << runtimeSend.encodedBytes
                      << " will_send_udp=" << (runtimeSend.willSendUdp ? 1 : 0)
                      << " will_register_delivery=" << (runtimeSend.willRegisterDelivery ? 1 : 0)
                      << " will_register_conversation_observer=" << (runtimeSend.willRegisterConversationObserver ? 1 : 0)
                      << " will_persist_sent_storage=" << (runtimeSend.willPersistSentStorage ? 1 : 0)
                      << " requires_durable_sent_storage=" << (runtimeSend.requiresDurableSentStorage ? 1 : 0)
                      << " reason=" << runtimeSend.reason
                      << " step281_runtime_storage=" << (opt.aiDialogIntentStep273PersistenceRuntimeStorage ? "on" : "off")
                      << " client_ui_audio=off"
                      << " mark_applied=off"
                      << "\n";

            if(runtimeSend.ready && route.has_value()) {
              const auto encoded = Mmo::Net::encodeServerNpcDialogIntentPacket(packetBoundary.packet);
              asio::error_code sendError;
              socket.send_to(asio::buffer(encoded), *route, 0, sendError);
              if(sendError) {
                std::cerr << "[server_conversation_late_observer_resume_runtime_send_failed]"
                          << " conversation_id=" << plan.conversationId
                          << " observer_session_uuid=" << plan.sessionUuid
                          << " endpoint=" << endpointText(*route)
                          << " action_id=" << packetBoundary.packet.actionId
                          << " ack_key=" << packetBoundary.packet.ackKey
                          << " error=" << sendError.message()
                          << " db_sent_storage=not_written"
                          << " mark_applied=off"
                          << "\n";
              } else {
                Mmo::Server::OutboundGameplayAttempt attempt;
                attempt.actionId = packetBoundary.packet.actionId;
                attempt.ackKey = packetBoundary.packet.ackKey;
                attempt.sessionUuid = packetBoundary.packet.sessionUuid;
                attempt.characterKey = packetBoundary.packet.targetCharacterKey;
                attempt.packetSequence = packetBoundary.packet.packetSequence;
                attempt.localSequence = packetBoundary.packet.localSequence;
                attempt.serverTick = packetBoundary.packet.serverTick;
                attempt.sentAtMs = monotonicNowMs();
                attempt.ackDeadlineMs = attempt.sentAtMs + opt.aiDialogIntentAckTimeoutMs;
                const auto registry = outboundGameplayDelivery.recordSent(std::move(attempt));

                Mmo::Server::ConversationObserverSent observer;
                observer.sessionUuid = packetBoundary.packet.sessionUuid;
                observer.characterKey = packetBoundary.packet.targetCharacterKey;
                observer.actionId = packetBoundary.packet.actionId;
                observer.ackKey = packetBoundary.packet.ackKey;
                observer.packetSequence = packetBoundary.packet.packetSequence;
                observer.localSequence = packetBoundary.packet.localSequence;
                observer.sentAtMs = monotonicNowMs();
                observer.ackDeadlineMs = observer.sentAtMs + opt.aiDialogIntentAckTimeoutMs;
                observer.distanceSquared = 0.0;
                observer.target = false;
                observer.hasPosition = false;
                const auto observerRegistration = conversationRuntime.recordObserverSent(plan.conversationId, std::move(observer));

                bool stored = false;
                std::string deliveryUuid;
                std::string deliveryStatus;
                std::string storageError;
                if(aiRuntimeTarget != nullptr) {
                  Mmo::Server::AiDialogIntentDeliveryPersistenceSentStorageRequest storageRequest;
                  storageRequest.enabled = true;
                  storageRequest.allowMysqlExecution = true;
                  storageRequest.aiDatabaseName = opt.worldInstanceAiDatabaseName;
                  storageRequest.source = "step282_late_observer_replay_send";
                  storageRequest.deliveryKind = "resume";
                  storageRequest.worldInstanceUuid = opt.worldInstanceKey;
                  storageRequest.worldName = plan.worldName.empty() ? std::string(worldName) : plan.worldName;
                  storageRequest.contentRevisionKey = opt.contentRevisionKey;
                  storageRequest.conversationId = plan.conversationId;
                  storageRequest.speakerEntityKey = packetBoundary.packet.speakerEntityKey;
                  storageRequest.speakerNpcInstanceUuid = packetBoundary.packet.speakerNpcInstanceUuid;
                  storageRequest.lineId = packetBoundary.packet.lineId;
                  storageRequest.audioRef = packetBoundary.packet.audioRef;
                  storageRequest.text = packetBoundary.packet.text;
                  storageRequest.reason = packetBoundary.packet.reason;
                  storageRequest.actionId = packetBoundary.packet.actionId;
                  storageRequest.ackKey = packetBoundary.packet.ackKey;
                  storageRequest.targetSessionUuid = packetBoundary.packet.sessionUuid;
                  storageRequest.targetCharacterKey = packetBoundary.packet.targetCharacterKey;
                  storageRequest.endpointText = endpointText(*route);
                  storageRequest.packetSequence = packetBoundary.packet.packetSequence;
                  storageRequest.localSequence = packetBoundary.packet.localSequence;
                  storageRequest.serverTick = packetBoundary.packet.serverTick;
                  storageRequest.startTick = packetBoundary.packet.startTick;
                  storageRequest.durationMs = packetBoundary.packet.durationMs;
                  storageRequest.plannedRecipients = 1;
                  storageRequest.payloadBytes = encoded.size();
                  storageRequest.ackTimeoutMs = opt.aiDialogIntentAckTimeoutMs;
                  const auto storageSql = Mmo::Server::buildAiDialogIntentDeliveryPersistenceSentStorageSql(storageRequest);
                  if(storageSql.ready) {
                    try {
                      const auto rawStorage = runMysql(*aiRuntimeTarget, storageSql.sql);
                      const auto storageParts = splitMysqlLastRow(rawStorage);
                      stored = true;
                      deliveryUuid = storageParts.empty() ? std::string() : storageParts[0];
                      deliveryStatus = storageParts.size() > 1 ? storageParts[1] : std::string();
                    } catch(const std::exception& error) {
                      storageError = error.what();
                    }
                  } else {
                    storageError = storageSql.reason;
                  }
                }

                std::cout << "[server_conversation_late_observer_resume_runtime_sent]"
                          << " conversation_id=" << plan.conversationId
                          << " observer_session_uuid=" << plan.sessionUuid
                          << " endpoint=" << endpointText(*route)
                          << " action_id=" << packetBoundary.packet.actionId
                          << " ack_key=" << packetBoundary.packet.ackKey
                          << " packet_sequence=" << packetBoundary.packet.packetSequence
                          << " bytes=" << encoded.size()
                          << " delivery_registry=" << registry.state
                          << " delivery_registry_accepted=" << (registry.accepted ? 1 : 0)
                          << " conversation_observer_registered=" << (observerRegistration.accepted ? 1 : 0)
                          << " conversation_observer_status=" << observerRegistration.state
                          << " db_sent_storage=" << (stored ? "step282" : "failed")
                          << " delivery_uuid=" << deliveryUuid
                          << " delivery_status=" << deliveryStatus
                          << " storage_error=" << storageError
                          << " client_ui_audio=off"
                          << " mark_applied=off"
                          << "\n";
              }
            }
          }

          if(opt.aiDialogIntentLateObserverResumeDeliveryRegistrationBoundary) {
            Mmo::Server::ConversationResumeDeliveryRegistrationRequest registrationRequest;
            registrationRequest.sendGate = &gate;
            registrationRequest.packetBoundary = &packetBoundary;
            registrationRequest.deliveryState = &outboundGameplayDelivery;
            registrationRequest.conversationRuntime = &conversationRuntime;
            registrationRequest.boundaryEnabled = true;
            registrationRequest.nowMs = monotonicNowMs();
            registrationRequest.ackTimeoutMs = opt.aiDialogIntentAckTimeoutMs;
            const auto registration = resumeDeliveryRegistrationBoundary.classifyNoRegister(registrationRequest);
            std::cout << "[server_conversation_late_observer_resume_delivery_registration_boundary]"
                      << " conversation_id=" << plan.conversationId
                      << " observer_session_uuid=" << plan.sessionUuid
                      << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                      << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                      << " status=" << Mmo::Server::conversationResumeDeliveryRegistrationStatusName(registration.status)
                      << " eligible=" << (registration.eligible ? 1 : 0)
                      << " duplicate=" << (registration.duplicate ? 1 : 0)
                      << " action_id=" << registration.actionId
                      << " ack_key=" << registration.ackKey
                      << " packet_sequence=" << registration.packetSequence
                      << " sent_at_ms=" << registration.sentAtMs
                      << " ack_deadline_ms=" << registration.ackDeadlineMs
                      << " would_register_delivery=" << (registration.wouldRegisterDelivery ? 1 : 0)
                      << " would_register_conversation_observer=" << (registration.wouldRegisterConversationObserver ? 1 : 0)
                      << " would_mutate_runtime=" << (registration.wouldMutateRuntime ? 1 : 0)
                      << " reason=" << registration.reason
                      << " dispatch_envelope=" << (opt.aiDialogIntentLateObserverResumeDispatchEnvelope ? "on" : "off")
                      << " mutation_guard=" << (opt.aiDialogIntentLateObserverResumeMutationGuard ? "on" : "off")
                      << " send_failure_dead_letter_guard=" << (opt.aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard ? "on" : "off")
                      << " commit_preflight=" << (opt.aiDialogIntentLateObserverResumeCommitPreflight ? "on" : "off")
                      << " persistence_execution_boundary=" << (opt.aiDialogIntentStep273PersistenceExecutionBoundary ? "on" : "off")
                      << " persistence_executor_adapter=" << (opt.aiDialogIntentStep273PersistenceExecutorAdapter ? "on" : "off")
                      << " persistence_dry_run_report_gate=" << (opt.aiDialogIntentStep273PersistenceDryRunReportGate ? "on" : "off")
                      << " outcome_policy=" << (opt.aiDialogIntentStep273PersistenceOutcomePolicy ? "on" : "off")
                      << " outcome_observability=" << (opt.aiDialogIntentStep273PersistenceOutcomeObservability ? "on" : "off")
                  << " activation_preflight=" << (opt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off")
                      << " send_resume_packet=off"
                      << " delivery_registry=off"
                      << " conversation_observer_registry=off"
                      << " db_conversation_storage=off"
                      << " mark_applied=off"
                      << "\n";

            if(opt.aiDialogIntentLateObserverResumeDispatchEnvelope) {
              Mmo::Server::ConversationResumeDispatchEnvelopeRequest envelopeRequest;
              envelopeRequest.registrationBoundary = &registration;
              envelopeRequest.packetBoundary = &packetBoundary;
              envelopeRequest.envelopeEnabled = true;
              envelopeRequest.endpointAvailable = route.has_value();
              envelopeRequest.endpointText = route.has_value() ? endpointText(*route) : std::string();
              envelopeRequest.nowMs = monotonicNowMs();
              const auto envelope = resumeDispatchEnvelope.classifyNoDispatch(envelopeRequest);
              std::cout << "[server_conversation_late_observer_resume_dispatch_envelope]"
                        << " conversation_id=" << plan.conversationId
                        << " observer_session_uuid=" << plan.sessionUuid
                        << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                        << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                        << " status=" << Mmo::Server::conversationResumeDispatchEnvelopeStatusName(envelope.status)
                        << " eligible=" << (envelope.eligible ? 1 : 0)
                        << " duplicate=" << (envelope.duplicate ? 1 : 0)
                        << " endpoint_available=" << (route.has_value() ? 1 : 0)
                        << " endpoint=" << envelope.endpointText
                        << " action_id=" << envelope.actionId
                        << " ack_key=" << envelope.ackKey
                        << " packet_sequence=" << envelope.packetSequence
                        << " encoded_bytes=" << envelope.encodedBytes
                        << " sent_at_ms=" << envelope.sentAtMs
                        << " ack_deadline_ms=" << envelope.ackDeadlineMs
                        << " would_dispatch_udp=" << (envelope.wouldDispatchUdp ? 1 : 0)
                        << " would_register_delivery=" << (envelope.wouldRegisterDelivery ? 1 : 0)
                        << " would_register_conversation_observer=" << (envelope.wouldRegisterConversationObserver ? 1 : 0)
                        << " would_mutate_runtime=" << (envelope.wouldMutateRuntime ? 1 : 0)
                        << " reason=" << envelope.reason
                        << " send_resume_packet=off"
                        << " delivery_registry=off"
                        << " conversation_observer_registry=off"
                        << " db_conversation_storage=off"
                        << " mark_applied=off"
                        << "\n";

              if(opt.aiDialogIntentLateObserverResumeMutationGuard) {
                Mmo::Server::ConversationResumeMutationGuardRequest mutationRequest;
                mutationRequest.dispatchEnvelope = &envelope;
                mutationRequest.registrationBoundary = &registration;
                mutationRequest.deliveryState = &outboundGameplayDelivery;
                mutationRequest.conversationRuntime = &conversationRuntime;
                mutationRequest.guardEnabled = true;
                mutationRequest.nowMs = monotonicNowMs();
                const auto mutation = resumeMutationGuard.classifyNoMutation(mutationRequest);
                std::cout << "[server_conversation_late_observer_resume_mutation_guard]"
                          << " conversation_id=" << plan.conversationId
                          << " observer_session_uuid=" << plan.sessionUuid
                          << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                          << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                          << " status=" << Mmo::Server::conversationResumeMutationGuardStatusName(mutation.status)
                          << " ready=" << (mutation.ready ? 1 : 0)
                          << " duplicate=" << (mutation.duplicate ? 1 : 0)
                          << " endpoint=" << mutation.endpointText
                          << " action_id=" << mutation.actionId
                          << " ack_key=" << mutation.ackKey
                          << " packet_sequence=" << mutation.packetSequence
                          << " encoded_bytes=" << mutation.encodedBytes
                          << " sent_at_ms=" << mutation.sentAtMs
                          << " ack_deadline_ms=" << mutation.ackDeadlineMs
                          << " guarded_at_ms=" << mutation.guardedAtMs
                          << " would_dispatch_udp=" << (mutation.wouldDispatchUdp ? 1 : 0)
                          << " would_register_delivery=" << (mutation.wouldRegisterDelivery ? 1 : 0)
                          << " would_register_conversation_observer=" << (mutation.wouldRegisterConversationObserver ? 1 : 0)
                          << " would_mutate_runtime=" << (mutation.wouldMutateRuntime ? 1 : 0)
                          << " mutated_runtime=" << (mutation.mutatedRuntime ? 1 : 0)
                          << " requires_durable_send_receipt=" << (mutation.requiresDurableSendReceipt ? 1 : 0)
                          << " requires_durable_observer_receipt=" << (mutation.requiresDurableObserverReceipt ? 1 : 0)
                          << " commit_order=" << mutation.commitOrder
                          << " reason=" << mutation.reason
                          << " send_resume_packet=off"
                          << " delivery_registry=off"
                          << " conversation_observer_registry=off"
                          << " db_conversation_storage=off"
                          << " mark_applied=off"
                          << "\n";

                Mmo::Server::ConversationResumeSendFailureDeadLetterGuardResult deadLetter;
                bool deadLetterEvaluated = false;
                if(opt.aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard) {
                  Mmo::Server::ConversationResumeSendFailureDeadLetterGuardRequest deadLetterRequest;
                  deadLetterRequest.mutationGuard = &mutation;
                  deadLetterRequest.deliveryState = &outboundGameplayDelivery;
                  deadLetterRequest.conversationRuntime = &conversationRuntime;
                  deadLetterRequest.guardEnabled = true;
                  deadLetterRequest.nowMs = monotonicNowMs();
                  deadLetterRequest.failureReason = "udp_send_failure_dead_letter_preview_no_socket_send";
                  deadLetterRequest.failureMessage = "future_late_observer_resume_udp_send_failure_would_terminalize_without_retry_worker_yet";
                  deadLetter = resumeSendFailureDeadLetterGuard.classifyNoMutation(deadLetterRequest);
                  deadLetterEvaluated = true;
                  std::cout << "[server_conversation_late_observer_resume_send_failure_dead_letter_guard]"
                            << " conversation_id=" << plan.conversationId
                            << " observer_session_uuid=" << plan.sessionUuid
                            << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                            << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                            << " status=" << Mmo::Server::conversationResumeSendFailureDeadLetterGuardStatusName(deadLetter.status)
                            << " ready=" << (deadLetter.ready ? 1 : 0)
                            << " duplicate=" << (deadLetter.duplicate ? 1 : 0)
                            << " endpoint=" << deadLetter.endpointText
                            << " action_id=" << deadLetter.actionId
                            << " ack_key=" << deadLetter.ackKey
                            << " packet_sequence=" << deadLetter.packetSequence
                            << " encoded_bytes=" << deadLetter.encodedBytes
                            << " sent_at_ms=" << deadLetter.sentAtMs
                            << " ack_deadline_ms=" << deadLetter.ackDeadlineMs
                            << " guarded_at_ms=" << deadLetter.guardedAtMs
                            << " failure_reason=" << deadLetter.failureReason
                            << " would_dispatch_udp=" << (deadLetter.wouldDispatchUdp ? 1 : 0)
                            << " would_register_delivery=" << (deadLetter.wouldRegisterDelivery ? 1 : 0)
                            << " would_register_conversation_observer=" << (deadLetter.wouldRegisterConversationObserver ? 1 : 0)
                            << " would_terminalize_outbound_delivery=" << (deadLetter.wouldTerminalizeOutboundDelivery ? 1 : 0)
                            << " would_terminalize_conversation_observer=" << (deadLetter.wouldTerminalizeConversationObserver ? 1 : 0)
                            << " would_dead_letter=" << (deadLetter.wouldDeadLetter ? 1 : 0)
                            << " would_mutate_runtime=" << (deadLetter.wouldMutateRuntime ? 1 : 0)
                            << " mutated_runtime=" << (deadLetter.mutatedRuntime ? 1 : 0)
                            << " requires_durable_send_failure_receipt=" << (deadLetter.requiresDurableSendFailureReceipt ? 1 : 0)
                            << " requires_durable_dead_letter_state=" << (deadLetter.requiresDurableDeadLetterState ? 1 : 0)
                            << " requires_durable_observer_terminal_receipt=" << (deadLetter.requiresDurableObserverTerminalReceipt ? 1 : 0)
                            << " commit_order=" << deadLetter.commitOrder
                            << " reason=" << deadLetter.reason
                            << " send_resume_packet=off"
                            << " delivery_registry=off"
                            << " conversation_observer_registry=off"
                            << " dead_letter_registry=off"
                            << " db_conversation_storage=off"
                            << " mark_applied=off"
                            << "\n";

                }

                if(opt.aiDialogIntentLateObserverResumeCommitPreflight) {
                  Mmo::Server::ConversationResumeCommitPreflightRequest commitRequest;
                  commitRequest.mutationGuard = &mutation;
                  commitRequest.sendFailureGuard = deadLetterEvaluated ? &deadLetter : nullptr;
                  commitRequest.deliveryState = &outboundGameplayDelivery;
                  commitRequest.conversationRuntime = &conversationRuntime;
                  commitRequest.preflightEnabled = true;
                  commitRequest.nowMs = monotonicNowMs();
                  const auto commit = resumeCommitPreflight.classifyNoMutation(commitRequest);
                  std::cout << "[server_conversation_late_observer_resume_commit_preflight]"
                            << " conversation_id=" << plan.conversationId
                            << " observer_session_uuid=" << plan.sessionUuid
                            << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                            << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                            << " status=" << Mmo::Server::conversationResumeCommitPreflightStatusName(commit.status)
                            << " ready=" << (commit.ready ? 1 : 0)
                            << " duplicate=" << (commit.duplicate ? 1 : 0)
                            << " endpoint=" << commit.endpointText
                            << " action_id=" << commit.actionId
                            << " ack_key=" << commit.ackKey
                            << " packet_sequence=" << commit.packetSequence
                            << " local_sequence=" << commit.localSequence
                            << " server_tick=" << commit.serverTick
                            << " encoded_bytes=" << commit.encodedBytes
                            << " sent_at_ms=" << commit.sentAtMs
                            << " ack_deadline_ms=" << commit.ackDeadlineMs
                            << " preflight_at_ms=" << commit.preflightAtMs
                            << " plan_steps=" << commit.planSteps.size()
                            << " would_register_delivery=" << (commit.wouldRegisterDelivery ? 1 : 0)
                            << " would_register_conversation_observer=" << (commit.wouldRegisterConversationObserver ? 1 : 0)
                            << " would_dispatch_udp=" << (commit.wouldDispatchUdp ? 1 : 0)
                            << " would_await_client_transport_ack=" << (commit.wouldAwaitClientTransportAck ? 1 : 0)
                            << " would_handle_send_failure=" << (commit.wouldHandleSendFailure ? 1 : 0)
                            << " would_terminalize_outbound_delivery_on_send_failure=" << (commit.wouldTerminalizeOutboundDeliveryOnSendFailure ? 1 : 0)
                            << " would_terminalize_conversation_observer_on_send_failure=" << (commit.wouldTerminalizeConversationObserverOnSendFailure ? 1 : 0)
                            << " would_dead_letter_on_send_failure=" << (commit.wouldDeadLetterOnSendFailure ? 1 : 0)
                            << " would_use_single_atomic_runtime_transaction=" << (commit.wouldUseSingleAtomicRuntimeTransaction ? 1 : 0)
                            << " would_mutate_runtime=" << (commit.wouldMutateRuntime ? 1 : 0)
                            << " mutated_runtime=" << (commit.mutatedRuntime ? 1 : 0)
                            << " requires_durable_send_receipt=" << (commit.requiresDurableSendReceipt ? 1 : 0)
                            << " requires_durable_observer_receipt=" << (commit.requiresDurableObserverReceipt ? 1 : 0)
                            << " requires_durable_ack_receipt=" << (commit.requiresDurableAckReceipt ? 1 : 0)
                            << " requires_durable_send_failure_receipt=" << (commit.requiresDurableSendFailureReceipt ? 1 : 0)
                            << " requires_durable_dead_letter_state=" << (commit.requiresDurableDeadLetterState ? 1 : 0)
                            << " success_commit_order=" << commit.successCommitOrder
                            << " failure_commit_order=" << commit.failureCommitOrder
                            << " reason=" << commit.reason
                            << " send_resume_packet=off"
                            << " delivery_registry=off"
                            << " conversation_observer_registry=off"
                            << " dead_letter_registry=off"
                            << " db_conversation_storage=off"
                            << " mark_applied=off"
                            << "\n";


                  if(opt.aiDialogIntentStep273PersistencePreview) {
                    Mmo::Server::AiDialogIntentDeliveryPersistencePreviewRequest persistenceRequest;
                    persistenceRequest.enabled = true;
                    persistenceRequest.aiDatabaseName = opt.worldInstanceAiDatabaseName;
                    persistenceRequest.worldInstanceUuid = opt.worldInstanceKey;
                    persistenceRequest.worldName = plan.worldName.empty() ? std::string(worldName) : plan.worldName;
                    persistenceRequest.contentRevisionKey = opt.contentRevisionKey;
                    persistenceRequest.ackTimeoutMs = opt.aiDialogIntentAckTimeoutMs;
                    persistenceRequest.resumePlan = &plan;
                    persistenceRequest.packetBoundary = &packetBoundary;
                    persistenceRequest.deliveryRegistration = &registration;
                    persistenceRequest.dispatchEnvelope = &envelope;
                    persistenceRequest.commitPreflight = &commit;
                    const auto persistence = Mmo::Server::buildAiDialogIntentDeliveryPersistencePreview(persistenceRequest);
                    std::cout << "[server_ai_dialog_intent_step273_persistence_preview]"
                              << " conversation_id=" << plan.conversationId
                              << " observer_session_uuid=" << plan.sessionUuid
                              << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                              << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                              << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistencePreviewStatusName(persistence.status)
                              << " ready=" << (persistence.ready ? 1 : 0)
                              << " ai_db=" << persistence.aiDatabaseName
                              << " action_id=" << persistence.actionId
                              << " ack_key=" << persistence.ackKey
                              << " packet_sequence=" << persistence.packetSequence
                              << " local_sequence=" << persistence.localSequence
                              << " server_tick=" << persistence.serverTick
                              << " payload_bytes=" << persistence.payloadBytes
                              << " statements=" << persistence.statements.size()
                              << " statement_names=" << Mmo::Server::aiDialogIntentDeliveryPersistenceStatementNamesCsv(persistence)
                              << " requires_step273_schema=" << (persistence.requiresExistingStep273Schema ? 1 : 0)
                              << " contains_mutating_sql_preview=" << (persistence.containsMutatingSql ? 1 : 0)
                              << " execute_mysql=" << (persistence.executeMysql ? 1 : 0)
                              << " mutated_db=" << (persistence.mutatedDb ? 1 : 0)
                              << " reason=" << persistence.reason
                              << " execution_boundary=" << (opt.aiDialogIntentStep273PersistenceExecutionBoundary ? "on" : "off")
                              << " real_udp_send=off"
                              << " mark_applied=off"
                              << "\n";

                    if(opt.aiDialogIntentStep273PersistenceExecutionBoundary) {
                      Mmo::Server::AiDialogIntentDeliveryPersistenceExecutionBoundaryRequest executionRequest;
                      executionRequest.enabled = true;
                      executionRequest.allowMysqlExecution = false;
                      executionRequest.preview = &persistence;
                      const auto execution =
                          Mmo::Server::buildAiDialogIntentDeliveryPersistenceExecutionBoundary(executionRequest);
                      std::cout << "[server_ai_dialog_intent_step273_persistence_execution_boundary]"
                                << " conversation_id=" << plan.conversationId
                                << " observer_session_uuid=" << plan.sessionUuid
                                << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                                << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                                << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutionBoundaryStatusName(execution.status)
                                << " ready=" << (execution.ready ? 1 : 0)
                                << " action_id=" << execution.actionId
                                << " ack_key=" << execution.ackKey
                                << " statements=" << execution.statementCount
                                << " mutating_statements=" << execution.mutatingStatementCount
                                << " procedure_calls=" << execution.procedureCallCount
                                << " output_slots=" << execution.outputSlotCount
                                << " output_slot_names=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutionOutputSlotsCsv(execution)
                                << " plan_steps=" << execution.planSteps.size()
                                << " plan_statement_names=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutionPlanStatementNamesCsv(execution)
                                << " failure_classes=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutionFailureClassesCsv(execution)
                                << " requires_step273_schema=" << (execution.requiresStep273Schema ? 1 : 0)
                                << " transaction_required=" << (execution.transactionRequired ? 1 : 0)
                                << " rollback_required_on_failure=" << (execution.rollbackRequiredOnFailure ? 1 : 0)
                                << " would_open_mysql_connection=" << (execution.wouldOpenMysqlConnection ? 1 : 0)
                                << " would_mutate_db=" << (execution.wouldMutateDb ? 1 : 0)
                                << " execute_mysql=" << (execution.executeMysql ? 1 : 0)
                                << " mutated_db=" << (execution.mutatedDb ? 1 : 0)
                                << " reason=" << execution.reason
                                << " executor_adapter=" << (opt.aiDialogIntentStep273PersistenceExecutorAdapter ? "on" : "off")
                                << " dry_run_report_gate=" << (opt.aiDialogIntentStep273PersistenceDryRunReportGate ? "on" : "off")
                                << " outcome_policy=" << (opt.aiDialogIntentStep273PersistenceOutcomePolicy ? "on" : "off")
                                << " outcome_observability=" << (opt.aiDialogIntentStep273PersistenceOutcomeObservability ? "on" : "off")
                  << " activation_preflight=" << (opt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off")
                                << " real_udp_send=off"
                                << " mark_applied=off"
                                << "\n";

                      if(opt.aiDialogIntentStep273PersistenceExecutorAdapter) {
                        Mmo::Server::AiDialogIntentDeliveryPersistenceExecutorAdapterRequest adapterRequest;
                        adapterRequest.enabled = true;
                        adapterRequest.allowMysqlExecution = false;
                        adapterRequest.preview = &persistence;
                        adapterRequest.execution = &execution;
                        const auto adapter =
                            Mmo::Server::buildAiDialogIntentDeliveryPersistenceExecutorAdapter(adapterRequest);
                        std::cout << "[server_ai_dialog_intent_step273_persistence_executor_adapter]"
                                  << " conversation_id=" << plan.conversationId
                                  << " observer_session_uuid=" << plan.sessionUuid
                                  << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                                  << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                                  << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutorAdapterStatusName(adapter.status)
                                  << " ready=" << (adapter.ready ? 1 : 0)
                                  << " action_id=" << adapter.actionId
                                  << " ack_key=" << adapter.ackKey
                                  << " statements=" << adapter.statementCount
                                  << " procedure_calls=" << adapter.procedureCallCount
                                  << " output_slots=" << adapter.outputSlotCount
                                  << " output_slot_names=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutorOutputSlotsCsv(adapter)
                                  << " executor_steps=" << adapter.executorStepCount
                                  << " executor_step_kinds=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutorStepKindsCsv(adapter)
                                  << " executor_statement_names=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutorPlanStepNamesCsv(adapter)
                                  << " rollback_failure_classes=" << Mmo::Server::aiDialogIntentDeliveryPersistenceExecutorRollbackFailureClassesCsv(adapter)
                                  << " can_parse_result_rows=" << (adapter.canParseResultRows ? 1 : 0)
                                  << " can_classify_rollback=" << (adapter.canClassifyRollback ? 1 : 0)
                                  << " transaction_required=" << (adapter.transactionRequired ? 1 : 0)
                                  << " rollback_required_on_failure=" << (adapter.rollbackRequiredOnFailure ? 1 : 0)
                                  << " would_open_mysql_connection=" << (adapter.wouldOpenMysqlConnection ? 1 : 0)
                                  << " would_call_runmysql=" << (adapter.wouldCallRunMysql ? 1 : 0)
                                  << " would_mutate_db=" << (adapter.wouldMutateDb ? 1 : 0)
                                  << " execute_mysql=" << (adapter.executeMysql ? 1 : 0)
                                  << " mutated_db=" << (adapter.mutatedDb ? 1 : 0)
                                  << " reason=" << adapter.reason
                                  << " dry_run_report_gate=" << (opt.aiDialogIntentStep273PersistenceDryRunReportGate ? "on" : "off")
                                  << " outcome_policy=" << (opt.aiDialogIntentStep273PersistenceOutcomePolicy ? "on" : "off")
                                  << " outcome_observability=" << (opt.aiDialogIntentStep273PersistenceOutcomeObservability ? "on" : "off")
                  << " activation_preflight=" << (opt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off")
                                  << " real_udp_send=off"
                                  << " mark_applied=off"
                                  << "\n";

                        if(opt.aiDialogIntentStep273PersistenceDryRunReportGate) {
                          Mmo::Server::AiDialogIntentDeliveryPersistenceDryRunReportRequest dryRunRequest;
                          dryRunRequest.enabled = true;
                          dryRunRequest.allowMysqlExecution = false;
                          dryRunRequest.adapter = &adapter;
                          const auto dryRun =
                              Mmo::Server::buildAiDialogIntentDeliveryPersistenceDryRunReportGate(dryRunRequest);
                          std::cout << "[server_ai_dialog_intent_step273_persistence_dry_run_report_gate]"
                                    << " conversation_id=" << plan.conversationId
                                    << " observer_session_uuid=" << plan.sessionUuid
                                    << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                                    << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                                    << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistenceDryRunReportStatusName(dryRun.status)
                                    << " ready=" << (dryRun.ready ? 1 : 0)
                                    << " action_id=" << dryRun.actionId
                                    << " ack_key=" << dryRun.ackKey
                                    << " statements=" << dryRun.statementCount
                                    << " procedure_calls=" << dryRun.procedureCallCount
                                    << " executor_steps=" << dryRun.executorStepCount
                                    << " dry_run_steps=" << dryRun.dryRunStepCount
                                    << " dry_run_step_kinds=" << Mmo::Server::aiDialogIntentDeliveryPersistenceDryRunStepKindsCsv(dryRun)
                                    << " dry_run_step_statuses=" << Mmo::Server::aiDialogIntentDeliveryPersistenceDryRunStepStatusesCsv(dryRun)
                                    << " dry_run_statement_names=" << Mmo::Server::aiDialogIntentDeliveryPersistenceDryRunStepNamesCsv(dryRun)
                                    << " mutating_preview_steps=" << dryRun.mutatingPreviewStepCount
                                    << " output_parser_steps=" << dryRun.outputParserStepCount
                                    << " failure_branch_steps=" << dryRun.failureBranchStepCount
                                    << " rollback_failure_classes=" << Mmo::Server::aiDialogIntentDeliveryPersistenceDryRunRollbackFailureClassesCsv(dryRun)
                                    << " dead_letter_failure_classes=" << Mmo::Server::aiDialogIntentDeliveryPersistenceDryRunDeadLetterFailureClassesCsv(dryRun)
                                    << " transaction_dry_run_ready=" << (dryRun.transactionDryRunReady ? 1 : 0)
                                    << " output_parser_dry_run_ready=" << (dryRun.outputParserDryRunReady ? 1 : 0)
                                    << " rollback_report_ready=" << (dryRun.rollbackReportReady ? 1 : 0)
                                    << " dead_letter_report_ready=" << (dryRun.deadLetterReportReady ? 1 : 0)
                                    << " would_open_mysql_connection=" << (dryRun.wouldOpenMysqlConnection ? 1 : 0)
                                    << " would_call_runmysql=" << (dryRun.wouldCallRunMysql ? 1 : 0)
                                    << " would_mutate_db=" << (dryRun.wouldMutateDb ? 1 : 0)
                                    << " execute_mysql=" << (dryRun.executeMysql ? 1 : 0)
                                    << " mutated_db=" << (dryRun.mutatedDb ? 1 : 0)
                                    << " runmysql_disabled=" << (dryRun.runMysqlDisabled ? 1 : 0)
                                    << " replay_udp_send_disabled=" << (dryRun.replayUdpSendDisabled ? 1 : 0)
                                    << " mark_applied_disabled=" << (dryRun.markAppliedDisabled ? 1 : 0)
                                    << " reason=" << dryRun.reason
                                    << " real_udp_send=off"
                                    << " mark_applied=off"
                                    << "\n";

                          if(opt.aiDialogIntentStep273PersistenceOutcomePolicy) {
                            Mmo::Server::AiDialogIntentDeliveryPersistenceOutcomePolicyRequest outcomeRequest;
                            outcomeRequest.enabled = true;
                            outcomeRequest.allowMysqlExecution = false;
                            outcomeRequest.allowReplayUdpSend = false;
                            outcomeRequest.allowMarkApplied = false;
                            outcomeRequest.dryRun = &dryRun;
                            const auto outcome =
                                Mmo::Server::buildAiDialogIntentDeliveryPersistenceOutcomePolicy(outcomeRequest);
                            std::cout << "[server_ai_dialog_intent_step273_persistence_outcome_policy]"
                                      << " conversation_id=" << plan.conversationId
                                      << " observer_session_uuid=" << plan.sessionUuid
                                      << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                                      << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                                      << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistenceOutcomePolicyStatusName(outcome.status)
                                      << " ready=" << (outcome.ready ? 1 : 0)
                                      << " action_id=" << outcome.actionId
                                      << " ack_key=" << outcome.ackKey
                                      << " statements=" << outcome.statementCount
                                      << " procedure_calls=" << outcome.procedureCallCount
                                      << " dry_run_steps=" << outcome.dryRunStepCount
                                      << " policy_steps=" << outcome.policyStepCount
                                      << " policy_step_kinds=" << Mmo::Server::aiDialogIntentDeliveryPersistenceOutcomePolicyStepKindsCsv(outcome)
                                      << " policy_step_statuses=" << Mmo::Server::aiDialogIntentDeliveryPersistenceOutcomePolicyStepStatusesCsv(outcome)
                                      << " policy_step_names=" << Mmo::Server::aiDialogIntentDeliveryPersistenceOutcomePolicyStepNamesCsv(outcome)
                                      << " rollback_failure_classes=" << Mmo::Server::aiDialogIntentDeliveryPersistenceOutcomePolicyRollbackFailureClassesCsv(outcome)
                                      << " dead_letter_failure_classes=" << Mmo::Server::aiDialogIntentDeliveryPersistenceOutcomePolicyDeadLetterFailureClassesCsv(outcome)
                                      << " delivery_sent_policy_ready=" << (outcome.deliverySentPolicyReady ? 1 : 0)
                                      << " terminal_receipt_policy_ready=" << (outcome.terminalReceiptPolicyReady ? 1 : 0)
                                      << " observation_receipt_policy_ready=" << (outcome.observationReceiptPolicyReady ? 1 : 0)
                                      << " timeout_dead_letter_policy_ready=" << (outcome.timeoutDeadLetterPolicyReady ? 1 : 0)
                                      << " action_apply_blocked_until_durable_terminal=" << (outcome.actionApplyBlockedUntilDurableTerminal ? 1 : 0)
                                      << " durable_terminal_state_required=" << (outcome.durableTerminalStateRequired ? 1 : 0)
                                      << " requires_client_ack_or_observation=" << (outcome.requiresClientAckOrObservation ? 1 : 0)
                                      << " would_open_mysql_connection=" << (outcome.wouldOpenMysqlConnection ? 1 : 0)
                                      << " would_call_runmysql=" << (outcome.wouldCallRunMysql ? 1 : 0)
                                      << " would_mutate_db=" << (outcome.wouldMutateDb ? 1 : 0)
                                      << " execute_mysql=" << (outcome.executeMysql ? 1 : 0)
                                      << " mutated_db=" << (outcome.mutatedDb ? 1 : 0)
                                      << " replay_udp_send_enabled=" << (outcome.replayUdpSendEnabled ? 1 : 0)
                                      << " replay_udp_send_disabled=" << (outcome.replayUdpSendDisabled ? 1 : 0)
                                      << " mark_applied_enabled=" << (outcome.markAppliedEnabled ? 1 : 0)
                                      << " mark_applied_disabled=" << (outcome.markAppliedDisabled ? 1 : 0)
                                      << " reason=" << outcome.reason
                                      << " outcome_observability=" << (opt.aiDialogIntentStep273PersistenceOutcomeObservability ? "on" : "off")
                  << " activation_preflight=" << (opt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off")
                                      << " real_udp_send=off"
                                      << " mark_applied=off"
                                      << "\n";

                            if(opt.aiDialogIntentStep273PersistenceOutcomeObservability) {
                              Mmo::Server::AiDialogIntentDeliveryPersistenceOutcomeObservabilityRequest observabilityRequest;
                              observabilityRequest.enabled = true;
                              observabilityRequest.outcome = &outcome;
                              const auto observability =
                                  Mmo::Server::buildAiDialogIntentDeliveryPersistenceOutcomeObservability(observabilityRequest);
                              std::cout << "[server_ai_dialog_intent_step273_persistence_outcome_observability]"
                                        << " conversation_id=" << plan.conversationId
                                        << " observer_session_uuid=" << plan.sessionUuid
                                        << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                                        << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                                        << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistenceOutcomeObservabilityStatusName(observability.status)
                                        << " ready=" << (observability.ready ? 1 : 0)
                                        << " action_id=" << observability.actionId
                                        << " ack_key=" << observability.ackKey
                                        << " policy_steps=" << observability.policyStepCount
                                        << " durable_storage_steps=" << observability.durableStorageStepCount
                                        << " client_receipt_steps=" << observability.clientReceiptStepCount
                                        << " dead_letter_steps=" << observability.deadLetterStepCount
                                        << " delivery_sent_observed=" << (observability.deliverySentObserved ? 1 : 0)
                                        << " client_ack_wait_observed=" << (observability.clientAckWaitObserved ? 1 : 0)
                                        << " terminal_receipt_observed=" << (observability.terminalReceiptObserved ? 1 : 0)
                                        << " observation_receipt_observed=" << (observability.observationReceiptObserved ? 1 : 0)
                                        << " timeout_dead_letter_observed=" << (observability.timeoutDeadLetterObserved ? 1 : 0)
                                        << " action_apply_block_observed=" << (observability.actionApplyBlockObserved ? 1 : 0)
                                        << " durable_terminal_policy_observed=" << (observability.durableTerminalPolicyObserved ? 1 : 0)
                                        << " all_no_execute_invariants_hold=" << (observability.allNoExecuteInvariantsHold ? 1 : 0)
                                        << " unsafe_mysql_steps=" << observability.unsafeMysqlStepCount
                                        << " unsafe_replay_udp_steps=" << observability.unsafeReplayUdpStepCount
                                        << " unsafe_mark_applied_steps=" << observability.unsafeMarkAppliedStepCount
                                        << " would_open_mysql_connection=" << (observability.wouldOpenMysqlConnection ? 1 : 0)
                                        << " would_call_runmysql=" << (observability.wouldCallRunMysql ? 1 : 0)
                                        << " would_mutate_db=" << (observability.wouldMutateDb ? 1 : 0)
                                        << " execute_mysql=" << (observability.executeMysql ? 1 : 0)
                                        << " mutated_db=" << (observability.mutatedDb ? 1 : 0)
                                        << " replay_udp_send_enabled=" << (observability.replayUdpSendEnabled ? 1 : 0)
                                        << " replay_udp_send_disabled=" << (observability.replayUdpSendDisabled ? 1 : 0)
                                        << " mark_applied_enabled=" << (observability.markAppliedEnabled ? 1 : 0)
                                        << " mark_applied_disabled=" << (observability.markAppliedDisabled ? 1 : 0)
                                        << " reason=" << observability.reason
                                        << " activation_preflight=" << (opt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off")
                                        << " real_udp_send=off"
                                        << " mark_applied=off"
                                        << "\n";

                              if(opt.aiDialogIntentStep273PersistenceActivationPreflight) {
                                Mmo::Server::AiDialogIntentDeliveryPersistenceActivationPreflightRequest preflightRequest;
                                preflightRequest.enabled = true;
                                preflightRequest.allowRuntimeActivation = false;
                                preflightRequest.observability = &observability;
                                const auto activationPreflight =
                                    Mmo::Server::buildAiDialogIntentDeliveryPersistenceActivationPreflight(preflightRequest);
                                std::cout << "[server_ai_dialog_intent_step273_persistence_activation_preflight]"
                                          << " conversation_id=" << plan.conversationId
                                          << " observer_session_uuid=" << plan.sessionUuid
                                          << " character=" << (plan.characterKey.empty() ? std::string(characterKey) : plan.characterKey)
                                          << " world=" << (plan.worldName.empty() ? std::string("UNKNOWN") : plan.worldName)
                                          << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistenceActivationPreflightStatusName(activationPreflight.status)
                                          << " ready=" << (activationPreflight.ready ? 1 : 0)
                                          << " action_id=" << activationPreflight.actionId
                                          << " ack_key=" << activationPreflight.ackKey
                                          << " policy_steps=" << activationPreflight.policyStepCount
                                          << " durable_storage_steps=" << activationPreflight.durableStorageStepCount
                                          << " client_receipt_steps=" << activationPreflight.clientReceiptStepCount
                                          << " dead_letter_steps=" << activationPreflight.deadLetterStepCount
                                          << " policy_coverage_ready=" << (activationPreflight.policyCoverageReady ? 1 : 0)
                                          << " durable_storage_coverage_ready=" << (activationPreflight.durableStorageCoverageReady ? 1 : 0)
                                          << " client_receipt_coverage_ready=" << (activationPreflight.clientReceiptCoverageReady ? 1 : 0)
                                          << " dead_letter_coverage_ready=" << (activationPreflight.deadLetterCoverageReady ? 1 : 0)
                                          << " runtime_identity_present=" << (activationPreflight.runtimeIdentityPresent ? 1 : 0)
                                          << " durable_terminal_policy_observed=" << (activationPreflight.durableTerminalPolicyObserved ? 1 : 0)
                                          << " all_no_execute_invariants_hold=" << (activationPreflight.allNoExecuteInvariantsHold ? 1 : 0)
                                          << " activation_allowed=" << (activationPreflight.activationAllowed ? 1 : 0)
                                          << " activation_blocked_until_final_batch=" << (activationPreflight.activationBlockedUntilFinalBatch ? 1 : 0)
                                          << " future_db_batch_required=" << (activationPreflight.futureDbBatchRequired ? 1 : 0)
                                          << " future_tools_batch_required=" << (activationPreflight.futureToolsBatchRequired ? 1 : 0)
                                          << " would_open_mysql_connection=" << (activationPreflight.wouldOpenMysqlConnection ? 1 : 0)
                                          << " would_call_runmysql=" << (activationPreflight.wouldCallRunMysql ? 1 : 0)
                                          << " would_mutate_db=" << (activationPreflight.wouldMutateDb ? 1 : 0)
                                          << " execute_mysql=" << (activationPreflight.executeMysql ? 1 : 0)
                                          << " mutated_db=" << (activationPreflight.mutatedDb ? 1 : 0)
                                          << " replay_udp_send_enabled=" << (activationPreflight.replayUdpSendEnabled ? 1 : 0)
                                          << " replay_udp_send_disabled=" << (activationPreflight.replayUdpSendDisabled ? 1 : 0)
                                          << " mark_applied_enabled=" << (activationPreflight.markAppliedEnabled ? 1 : 0)
                                          << " mark_applied_disabled=" << (activationPreflight.markAppliedDisabled ? 1 : 0)
                                          << " reason=" << activationPreflight.reason
                                          << " real_udp_send=off"
                                          << " mark_applied=off"
                                          << "\n";
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  } catch(const std::exception& exc) {
    std::cerr << "[server_conversation_late_observer_resume_plan_failed]"
              << " session_uuid=" << sessionUuid
              << " error=" << exc.what()
              << " db_conversation_storage=off"
              << "\n";
  } catch(...) {
    std::cerr << "[server_conversation_late_observer_resume_plan_failed]"
              << " session_uuid=" << sessionUuid
              << " error=unknown"
              << " db_conversation_storage=off"
              << "\n";
  }
}

std::size_t sendNpcDialogIntentProbe(asio::ip::udp::socket& socket,
                                       const ClientRouteRegistry& routes,
                                       Mmo::Server::OutboundGameplayDeliveryState& deliveryState,
                                       Mmo::Server::ConversationSessionRuntime* conversationRuntime,
                                       const MySqlTarget* aiRuntimeTarget,
                                       const Options& opt,
                                       std::string_view sessionUuid,
                                       const Mmo::Net::ClientActionPacket& request,
                                       std::uint64_t& nextDialogIntentSequence) {
  constexpr std::uint32_t DiagnosticDialogIntentDurationMs = 3000;

  const auto targetCharacterKey = jsonStringField(request.payloadJson, "character_key").value_or(opt.characterKey);
  const auto worldName = jsonStringField(request.payloadJson, "world").value_or(opt.worldName);
  const auto lineServerTick = packetServerTick(request);
  const std::string conversationId = "server-diagnostic-conversation:" + std::string(sessionUuid) + ":" + std::to_string(request.localSequence);

  Mmo::Server::GameplayFanoutRequest fanoutRequest;
  fanoutRequest.mode = parseGameplayFanoutMode(opt.aiDialogIntentFanoutMode);
  fanoutRequest.targetSessionUuid = std::string(sessionUuid);
  fanoutRequest.targetCharacterKey = targetCharacterKey;
  fanoutRequest.worldName = worldName;
  fanoutRequest.radius = opt.aiDialogIntentAoiRadius;
  fanoutRequest.maxRecipients = opt.aiDialogIntentMaxRecipients;
  fanoutRequest.requireEndpoint = true;
  fanoutRequest.includeTargetWithoutPosition = true;
  if(const auto pos = movementToPosition(request.payloadJson)) {
    fanoutRequest.origin = {pos->x, pos->y, pos->z};
    fanoutRequest.hasOrigin = true;
  }

  const auto candidates = routes.observers();
  const auto selection = Mmo::Server::selectGameplayFanoutRecipients(candidates, std::move(fanoutRequest));

  std::cout << "[server_npc_dialog_intent_fanout_selected]"
            << " mode=" << Mmo::Server::gameplayFanoutModeName(selection.mode)
            << " status=" << selection.status
            << " target_session_uuid=" << sessionUuid
            << " target_character=" << targetCharacterKey
            << " world=" << (worldName.empty() ? "UNKNOWN" : worldName)
            << " candidates=" << selection.candidateCount
            << " recipients=" << selection.recipients.size()
            << " rejected=" << selection.rejected.size()
            << " endpoint_missing=" << selection.endpointMissingCount
            << " world_mismatch=" << selection.worldMismatchCount
            << " outside_radius=" << selection.outsideRadiusCount
            << " limit_skipped=" << selection.recipientLimitSkippedCount
            << " used_target_position_as_origin=" << (selection.usedTargetPositionAsOrigin ? 1 : 0)
            << " aoi_radius=" << opt.aiDialogIntentAoiRadius
            << " max_recipients=" << opt.aiDialogIntentMaxRecipients
            << " db_fanout_storage=off"
            << " conversation_session_probe=" << (conversationRuntime != nullptr ? "on" : "off")
            << "\n";

  bool conversationSessionStarted = false;
  if(conversationRuntime != nullptr && !selection.recipients.empty()) {
    Mmo::Server::ConversationLineStart line;
    line.conversationId = conversationId;
    line.worldName = worldName;
    line.worldInstanceUuid = opt.worldInstanceKey;
    line.speakerEntityKey = opt.aiDialogIntentSpeakerEntityKey;
    line.lineId = opt.aiDialogIntentLineId;
    line.serverTick = lineServerTick;
    line.startTick = lineServerTick;
    line.durationMs = DiagnosticDialogIntentDurationMs;
    line.plannedRecipients = selection.recipients.size();
    const auto result = conversationRuntime->startLine(std::move(line), monotonicNowMs());
    conversationSessionStarted = result.accepted;
    std::cout << "[server_conversation_session_started]"
              << " conversation_id=" << conversationId
              << " status=" << result.state
              << " accepted=" << (result.accepted ? 1 : 0)
              << " planned_recipients=" << selection.recipients.size()
              << " total_conversations=" << result.conversationCount
              << " db_conversation_storage=off"
              << " mark_applied=off"
              << "\n";
  }

  std::size_t sent = 0;
  for(const auto& recipient : selection.recipients) {
    const auto endpoint = routes.find(recipient.sessionUuid);
    if(!endpoint) {
      std::cerr << "[server_npc_dialog_intent_route_missing]"
                << " session_uuid=" << recipient.sessionUuid
                << " reason=selected_recipient_without_udp_endpoint"
                << "\n";
      continue;
    }

    Mmo::Net::ServerNpcDialogIntentPacket intent;
    intent.packetSequence = ++nextDialogIntentSequence;
    intent.localSequence = request.localSequence;
    intent.serverTick = lineServerTick;
    intent.startTick = lineServerTick;
    intent.durationMs = DiagnosticDialogIntentDurationMs;
    intent.flags = Mmo::Net::ServerNpcDialogIntentDiagnostic;
    intent.sessionUuid = recipient.sessionUuid;
    intent.targetCharacterKey = recipient.characterKey.empty() ? targetCharacterKey : recipient.characterKey;
    intent.actionId = "server-npc-dialog-intent:" + recipient.sessionUuid + ":" + std::to_string(intent.packetSequence);
    intent.ackKey = intent.actionId + ":client-ack";
    intent.conversationId = conversationId;
    intent.speakerEntityKey = opt.aiDialogIntentSpeakerEntityKey;
    intent.lineId = opt.aiDialogIntentLineId;
    intent.text = opt.aiDialogIntentText;
    intent.reason = std::string("bootstrap_transport_probe_no_db_terminal_storage;fanout=") +
                    Mmo::Server::gameplayFanoutModeName(selection.mode);

    const auto encoded = Mmo::Net::encodeServerNpcDialogIntentPacket(intent);
    if(encoded.empty())
      throw std::runtime_error("failed to encode ServerNpcDialogIntent packet");

    asio::error_code ec;
    socket.send_to(asio::buffer(encoded), *endpoint, 0, ec);
    if(ec) {
      std::cerr << "[server_npc_dialog_intent_send_failed]"
                << " session_uuid=" << recipient.sessionUuid
                << " endpoint=" << endpointText(*endpoint)
                << " action_id=" << intent.actionId
                << " error=" << ec.message()
                << " db_terminal_storage=off"
                << "\n";
      continue;
    }

    Mmo::Server::OutboundGameplayAttempt attempt;
    attempt.actionId = intent.actionId;
    attempt.ackKey = intent.ackKey;
    attempt.sessionUuid = intent.sessionUuid;
    attempt.characterKey = intent.targetCharacterKey;
    attempt.packetSequence = intent.packetSequence;
    attempt.localSequence = intent.localSequence;
    attempt.serverTick = intent.serverTick;
    attempt.sentAtMs = monotonicNowMs();
    attempt.ackDeadlineMs = attempt.sentAtMs + opt.aiDialogIntentAckTimeoutMs;
    const auto registry = deliveryState.recordSent(std::move(attempt));

    if(conversationRuntime != nullptr && conversationSessionStarted) {
      Mmo::Server::ConversationObserverSent observer;
      observer.sessionUuid = intent.sessionUuid;
      observer.characterKey = intent.targetCharacterKey;
      observer.actionId = intent.actionId;
      observer.ackKey = intent.ackKey;
      observer.packetSequence = intent.packetSequence;
      observer.localSequence = intent.localSequence;
      observer.sentAtMs = monotonicNowMs();
      observer.ackDeadlineMs = observer.sentAtMs + opt.aiDialogIntentAckTimeoutMs;
      observer.distanceSquared = recipient.distanceSquared;
      observer.target = recipient.isTarget;
      observer.hasPosition = recipient.hasPosition;
      const auto observerRegistration = conversationRuntime->recordObserverSent(conversationId, std::move(observer));
      std::cout << "[server_conversation_observer_registered]"
                << " conversation_id=" << conversationId
                << " session_uuid=" << intent.sessionUuid
                << " action_id=" << intent.actionId
                << " ack_key=" << intent.ackKey
                << " accepted=" << (observerRegistration.accepted ? 1 : 0)
                << " status=" << observerRegistration.state
                << " session_status=" << Mmo::Server::conversationSessionStatusName(observerRegistration.sessionStatus)
                << " observers=" << observerRegistration.observerCount
                << " db_conversation_storage=off"
                << "\n";
    }

    if(opt.aiDialogIntentStep273PersistenceRuntimeStorage && aiRuntimeTarget != nullptr) {
      Mmo::Server::AiDialogIntentDeliveryPersistenceSentStorageRequest storageRequest;
      storageRequest.enabled = true;
      storageRequest.allowMysqlExecution = true;
      storageRequest.aiDatabaseName = opt.worldInstanceAiDatabaseName;
      storageRequest.worldInstanceUuid = opt.worldInstanceKey;
      storageRequest.worldName = worldName;
      storageRequest.contentRevisionKey = opt.contentRevisionKey;
      storageRequest.conversationId = conversationId;
      storageRequest.speakerEntityKey = intent.speakerEntityKey;
      storageRequest.lineId = intent.lineId;
      storageRequest.text = intent.text;
      storageRequest.reason = intent.reason;
      storageRequest.actionId = intent.actionId;
      storageRequest.ackKey = intent.ackKey;
      storageRequest.targetSessionUuid = intent.sessionUuid;
      storageRequest.targetCharacterKey = intent.targetCharacterKey;
      storageRequest.endpointText = endpointText(*endpoint);
      storageRequest.packetSequence = intent.packetSequence;
      storageRequest.localSequence = intent.localSequence;
      storageRequest.serverTick = intent.serverTick;
      storageRequest.startTick = intent.startTick;
      storageRequest.durationMs = intent.durationMs;
      storageRequest.plannedRecipients = selection.recipients.size();
      storageRequest.payloadBytes = encoded.size();
      storageRequest.ackTimeoutMs = opt.aiDialogIntentAckTimeoutMs;
      storageRequest.distanceSquared = recipient.distanceSquared;
      storageRequest.recipientIsTarget = recipient.isTarget;
      storageRequest.recipientHasPosition = recipient.hasPosition;
      const auto storageSql = Mmo::Server::buildAiDialogIntentDeliveryPersistenceSentStorageSql(storageRequest);
      if(storageSql.ready) {
        try {
          const auto rawStorage = runMysql(*aiRuntimeTarget, storageSql.sql);
          const auto storageParts = splitMysqlLastRow(rawStorage);
          std::cout << "[server_ai_dialog_intent_step281_runtime_storage_sent]"
                    << " action_id=" << intent.actionId
                    << " ack_key=" << intent.ackKey
                    << " session_uuid=" << intent.sessionUuid
                    << " character=" << intent.targetCharacterKey
                    << " stored=1"
                    << " delivery_uuid=" << (storageParts.empty() ? std::string() : storageParts[0])
                    << " delivery_status=" << (storageParts.size() > 1 ? storageParts[1] : std::string())
                    << " execute_mysql=1"
                    << " mutated_db=1"
                    << " reason=" << storageSql.reason
                    << " mark_applied=off"
                    << "\n";
        } catch(const std::exception& error) {
          std::cerr << "[server_ai_dialog_intent_step281_runtime_storage_sent_failed]"
                    << " action_id=" << intent.actionId
                    << " ack_key=" << intent.ackKey
                    << " session_uuid=" << intent.sessionUuid
                    << " stored=0"
                    << " error=" << error.what()
                    << " mark_applied=off"
                    << "\n";
        }
      } else {
        std::cerr << "[server_ai_dialog_intent_step281_runtime_storage_sent_skipped]"
                  << " action_id=" << intent.actionId
                  << " ack_key=" << intent.ackKey
                  << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistenceRuntimeStorageStatusName(storageSql.status)
                  << " reason=" << storageSql.reason
                  << " mark_applied=off"
                  << "\n";
      }
    }

    ++sent;
    std::cout << "[server_npc_dialog_intent_sent]"
              << " session_uuid=" << recipient.sessionUuid
              << " target_session_uuid=" << sessionUuid
              << " endpoint=" << endpointText(*endpoint)
              << " action_id=" << intent.actionId
              << " ack_key=" << intent.ackKey
              << " speaker=" << intent.speakerEntityKey
              << " line=" << intent.lineId
              << " fanout_mode=" << Mmo::Server::gameplayFanoutModeName(selection.mode)
              << " recipient_is_target=" << (recipient.isTarget ? 1 : 0)
              << " recipient_has_position=" << (recipient.hasPosition ? 1 : 0)
              << " distance_squared=" << recipient.distanceSquared
              << " bytes=" << encoded.size()
              << " ack_timeout_ms=" << opt.aiDialogIntentAckTimeoutMs
              << " conversation_id=" << conversationId
              << " conversation_session_probe=" << (conversationRuntime != nullptr ? "on" : "off")
              << " terminal_registry=" << registry.state
              << " terminal_registry_accepted=" << (registry.accepted ? 1 : 0)
              << " db_terminal_storage=" << (opt.aiDialogIntentStep273PersistenceRuntimeStorage ? "step281" : "off")
              << "\n";
  }
  return sent;
}


[[nodiscard]] std::string dbSessionKeyForCharacter(const Options& opt) {
  std::string out = opt.sessionKey;
  out.push_back(':');
  out.append(opt.characterKey);
  return out;
}

void ensureCharacterExistsFromTemplate(const MySqlTarget& target, const Options& opt) {
  std::string sql;
  const auto accountSql = sqlLiteral(opt.accountName);
  const auto characterSql = sqlLiteral(opt.characterKey);
  const auto displayName = opt.characterDisplayName.empty() ? opt.characterKey : opt.characterDisplayName;
  const auto nameSql = sqlLiteral(displayName);
  sql += "SET @account_id=(SELECT account_id FROM account_accounts WHERE account_name=" + accountSql + " LIMIT 1);";
  sql += "SET @template_character_id=(SELECT character_id FROM characters WHERE account_id=@account_id AND character_key='PC_HERO' LIMIT 1);";
  sql += "SET @existing_character_id=(SELECT character_id FROM characters WHERE account_id=@account_id AND character_key=" + characterSql + " LIMIT 1);";
  sql += "INSERT INTO characters(account_id,realm_id,current_world_instance_id,character_key,character_name,lifecycle_state,metadata) ";
  sql += "SELECT tpl.account_id,tpl.realm_id,COALESCE(tpl.current_world_instance_id,cp.world_instance_id),";
  sql += characterSql + "," + nameSql + ",'active',";
  sql += "JSON_OBJECT('created_by','mmo_udp_server_cpp','template_character_key','PC_HERO') ";
  sql += "FROM characters tpl LEFT JOIN character_positions cp ON cp.character_id=tpl.character_id ";
  sql += "WHERE tpl.character_id=@template_character_id AND @existing_character_id IS NULL;";
  sql += "SET @new_character_id=(SELECT character_id FROM characters WHERE account_id=@account_id AND character_key=" + characterSql + " LIMIT 1);";
  sql += "INSERT IGNORE INTO character_positions(character_id,world_instance_id,pos_x,pos_y,pos_z,rotation_yaw,current_waypoint_key,server_tick,row_version) ";
  sql += "SELECT @new_character_id,world_instance_id,pos_x,pos_y,pos_z,rotation_yaw,current_waypoint_key,0,0 FROM character_positions WHERE character_id=@template_character_id AND @new_character_id<>@template_character_id;";
  sql += "INSERT IGNORE INTO character_stats(character_id,level,experience,experience_next,learning_points,health_current,health_max,mana_current,mana_max,strength,dexterity,guild,true_guild,permanent_attitude,temporary_attitude,raw_stats,row_version) ";
  sql += "SELECT @new_character_id,0,0,experience_next,0,health_max,health_max,mana_max,mana_max,strength,dexterity,guild,true_guild,permanent_attitude,temporary_attitude,";
  sql += "JSON_SET(COALESCE(raw_stats,JSON_OBJECT()),'$.created_from_template','PC_HERO'),0 FROM character_stats WHERE character_id=@template_character_id AND @new_character_id<>@template_character_id;";
  sql += "INSERT IGNORE INTO character_script_state(character_id,script_key,symbol_index,value_type,value_index,value_int,value_real,value_text) ";
  sql += "SELECT @new_character_id,script_key,symbol_index,value_type,value_index,value_int,value_real,value_text FROM character_script_state WHERE character_id=@template_character_id AND @new_character_id<>@template_character_id;";
  (void)runMysql(target, sql);
}

[[nodiscard]] std::string dbLogin(const MySqlTarget& target, const Options& opt) {
  ensureCharacterExistsFromTemplate(target, opt);
  const auto dbSessionKey = dbSessionKeyForCharacter(opt);
  std::string sql;
  sql += "SET @session_id = NULL;";
  sql += "CALL mmo_login_character(";
  sql += sqlLiteral(opt.accountName) + ",";
  sql += sqlLiteral(opt.characterKey) + ",";
  sql += sqlLiteral(dbSessionKey) + ",";
  sql += sqlLiteral("mmo_udp_server_cpp") + ",";
  sql += sqlLiteral("asio-udp-server") + ",";
  sql += "JSON_OBJECT('tool','mmo_udp_server_cpp','db_bridge_version'," + std::to_string(DbBridgeVersion) + ",'client_session_key'," + sqlLiteral(opt.sessionKey) + "),";
  sql += "@session_id);";
  sql += "SELECT BIN_TO_UUID(@session_id, 1);";
  const auto raw = runMysql(target, sql);
  if(raw.empty() || raw == "NULL")
    throw std::runtime_error("mmo_login_character returned no session id");
  return raw.substr(raw.rfind('\n') == std::string::npos ? 0 : raw.rfind('\n') + 1);
}

[[nodiscard]] bool isActiveDbSession(const MySqlTarget& target, std::string_view sessionUuid) {
  if(sessionUuid.empty())
    return false;
  std::string sql;
  sql += "SELECT COUNT(*) FROM server_sessions ";
  sql += "WHERE session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  sql += "AND lifecycle_state='active';";
  return parseU64OrZero(mysqlSingleField(target, sql)) > 0;
}

[[nodiscard]] bool ensureActiveDbSession(const MySqlTarget& target,
                                         const Options& opt,
                                         std::string& sessionUuid,
                                         std::string_view reason) {
  if(isActiveDbSession(target, sessionUuid))
    return false;

  const std::string oldSession = sessionUuid.empty() ? std::string("<empty>") : sessionUuid;
  sessionUuid = dbLogin(target, opt);
  std::cout << "[db_session_recovered]"
            << " reason=" << reason
            << " old=" << oldSession
            << " new=" << sessionUuid
            << "\n";
  return true;
}

void enqueueOutbox(const MySqlTarget& target,
                   std::string_view sessionUuid,
                   const Mmo::Net::ClientActionPacket& packet,
                   std::string_view dbPayload,
                   int priority,
                   int maxAttempts) {
  const auto* def = Mmo::findSemanticAction(packet.kind);
  const std::string_view actionName = def ? def->actionKind : std::string_view("unknown");
  std::string sql;
  sql += "SET @action_id = NULL;";
  sql += "SET @status = NULL;";
  sql += "CALL mmo_enqueue_server_action(";
  sql += "UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ", 1),";
  sql += sqlLiteral(actionName) + ",";
  sql += sqlLiteral(packet.targetKey) + ",";
  sql += "CAST(";
  sql += sqlLiteral(dbPayload);
  sql += " AS JSON),";
  sql += sqlLiteral(packet.idempotencyKey) + ",";
  sql += std::to_string(priority) + ",";
  sql += std::to_string(maxAttempts) + ",";
  sql += "@action_id,@status);";
  sql += "SELECT CONCAT(BIN_TO_UUID(@action_id, 1), '\\t', @status);";
  (void)runMysql(target, sql);
}

void applyCharacterCheckpoint(const MySqlTarget& target,
                              std::string_view sessionUuid,
                              const Mmo::Net::ClientActionPacket& packet,
                              std::string_view dbPayload) {
  const std::string_view payload = packet.payloadJson;
  const auto serverTick = packetServerTick(packet);
  const auto waypoint = jsonStringField(payload, "current_waypoint_key").value_or("");

  std::string sql;
  sql += "SET @event_id = NULL;";
  sql += "CALL mmo_checkpoint_character_state(";
  sql += "UUID_TO_BIN(";
  sql += sqlLiteral(sessionUuid);
  sql += ", 1),";
  sql += std::to_string(serverTick) + ",";
  sql += std::to_string(requiredJsonDouble(payload, "pos_x")) + ",";
  sql += std::to_string(requiredJsonDouble(payload, "pos_y")) + ",";
  sql += std::to_string(requiredJsonDouble(payload, "pos_z")) + ",";
  sql += std::to_string(requiredJsonDouble(payload, "rotation_yaw")) + ",";
  sql += sqlLiteral(waypoint) + ",";
  sql += std::to_string(requiredJsonI64(payload, "level")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "experience")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "experience_next")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "learning_points")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "health_current")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "health_max")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "mana_current")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "mana_max")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "strength")) + ",";
  sql += std::to_string(requiredJsonI64(payload, "dexterity")) + ",";
  sql += std::to_string(optionalJsonI64(payload, "guild", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "true_guild", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "permanent_attitude", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "temporary_attitude", 0)) + ",";
  sql += "CAST(";
  sql += sqlLiteral(dbPayload);
  sql += " AS JSON),";
  sql += sqlLiteral(packet.idempotencyKey) + ",";
  sql += "@event_id);";
  sql += "SELECT BIN_TO_UUID(@event_id, 1);";
  (void)runMysql(target, sql);
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

  std::string sql;
  sql += "SET @manifest_id=NULL; SET @event_id=NULL; SET @row_version_after=NULL;";
  sql += "CALL mmo_create_db_save_checkpoint_v1(";
  sql += "UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
  sql += sqlLiteral(manifestKey) + ",";
  sql += sqlLiteral(checkpointKind) + ",";
  sql += sqlLiteral(reason) + ",";
  sql += std::to_string(serverTick) + ",";
  sql += sqlJson(dbPayload) + ",";
  sql += sqlLiteral(packet.idempotencyKey) + ",";
  sql += "@manifest_id,@event_id,@row_version_after);";
  sql += "SELECT CONCAT(BIN_TO_UUID(@manifest_id,1),'\\t',BIN_TO_UUID(@event_id,1),'\\t',@row_version_after);";
  (void)runMysql(target, sql);
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

  std::string sql;
  sql += "SET @event_id = NULL;";
  sql += "CALL mmo_checkpoint_character_state(";
  sql += "UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ", 1),";
  sql += std::to_string(serverTick) + ",";
  sql += std::to_string(posX) + ",";
  sql += std::to_string(posY) + ",";
  sql += std::to_string(posZ) + ",";
  sql += std::to_string(rotationYaw) + ",";
  sql += sqlLiteral(waypoint) + ",";
  sql += std::to_string(optionalJsonI64(payload, "level", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "experience", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "experience_next", 500)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "learning_points", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "health_current", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "health_max", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "mana_current", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "mana_max", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "strength", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "dexterity", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "guild", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "true_guild", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "permanent_attitude", 0)) + ",";
  sql += std::to_string(optionalJsonI64(payload, "temporary_attitude", 0)) + ",";
  sql += sqlJson(dbPayload) + ",";
  sql += sqlLiteral(packet.idempotencyKey) + ",";
  sql += "@event_id);";
  sql += "SELECT BIN_TO_UUID(@event_id, 1);";
  (void)runMysql(target, sql);
}

[[nodiscard]] DirectApplyResult applyMovementProposal(const MySqlTarget& target,
                                                      std::string_view sessionUuid,
                                                      const Mmo::Net::ClientActionPacket& packet,
                                                      std::string_view dbPayload) {
  constexpr double MaxCoordAbs = 10000000.0;
  constexpr std::int64_t MinDeltaMs = 1;
  // Dev-authority tolerance: the client may emit the first movement proposal
  // after a long idle/script/weapon-state gap. Reject spatially impossible
  // proposals, but do not NACK a slow, tiny delta just because delta_ms is old.
  constexpr std::int64_t MaxDeltaMs = 60000;
  constexpr double MaxStepDistance = 2500.0;
  constexpr double MaxStaleTinyDistance = 300.0;
  constexpr double MaxStaleTinyVerticalDelta = 300.0;
  constexpr double MaxHorizontalSpeed = 2500.0;
  constexpr double MaxVerticalDelta = 1600.0;
  constexpr double MaxVerticalSpeed = 3500.0;
  constexpr double MaxFallDelta = 6000.0;
  constexpr double MaxFallSpeed = 12000.0;

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

  const auto coordOk = [](double v) noexcept { return std::isfinite(v) && std::abs(v) <= MaxCoordAbs; };
  bool ok = coordOk(fromX) && coordOk(fromY) && coordOk(fromZ) && coordOk(toX) && coordOk(toY) && coordOk(toZ);

  const double dx = toX - fromX;
  const double dy = toY - fromY;
  const double dz = toZ - fromZ;
  const double horizontal = std::sqrt(dx * dx + dz * dz);
  const double total = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double seconds = deltaMs > 0 ? static_cast<double>(deltaMs) / 1000.0 : 0.0;
  const double verticalAbs = std::abs(dy);
  const double horizontalSpeed = seconds > 0.0 ? horizontal / seconds : MaxHorizontalSpeed + 1.0;
  const double verticalSpeed = seconds > 0.0 ? verticalAbs / seconds : MaxVerticalSpeed + 1.0;
  const bool staleTinyDelta = deltaMs > MaxDeltaMs && total <= MaxStaleTinyDistance && verticalAbs <= MaxStaleTinyVerticalDelta;
  ok = ok && deltaMs >= MinDeltaMs && (deltaMs <= MaxDeltaMs || staleTinyDelta);
  ok = ok && total <= MaxStepDistance && horizontalSpeed <= MaxHorizontalSpeed;
  ok = ok && ((verticalAbs <= MaxVerticalDelta && verticalSpeed <= MaxVerticalSpeed) ||
              (dy < 0.0 && verticalAbs <= MaxFallDelta && verticalSpeed <= MaxFallSpeed));

  if(!ok) {
    std::cerr << "[movement_rejected]"
              << " delta_ms=" << deltaMs
              << " total=" << total
              << " horizontal_speed=" << horizontalSpeed
              << " vertical_delta=" << dy
              << " vertical_speed=" << verticalSpeed
              << " stale_tiny=" << (staleTinyDelta ? 1 : 0)
              << "\n";
    return {true, false, false, "movement_rejected"};
  }

  if(staleTinyDelta) {
    std::cout << "[movement_stale_delta_accepted]"
              << " delta_ms=" << deltaMs
              << " total=" << total
              << " vertical_delta=" << dy
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

  std::string sql;
  sql += "SET @event_id=NULL; SET @correction_id=NULL;";
  sql += "CALL mmo_record_client_action_correction(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
  sql += sqlLiteral(actionName) + ",";
  sql += std::to_string(packet.localSequence) + ",";
  sql += sqlLiteral("rollback_to_authoritative_position") + ",";
  sql += sqlLiteral(reason) + ",";
  sql += std::to_string(tick) + ",";
  sql += sqlJson(dbPayload) + ",";
  sql += sqlLiteral(idempotency) + ",";
  sql += "@event_id,@correction_id);";
  sql += "SELECT BIN_TO_UUID(@correction_id,1);";
  (void)runMysql(target, sql);
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

  std::string sql;
  if(packet.kind == Mmo::SemanticActionKind::SetScriptInt) {
    const auto scriptKey = scriptKeyFromPayload(packet);
    const auto symbolIndex = optionalJsonI64(payload, "symbol_index", 0);
    const auto valueIndex = optionalJsonI64(payload, "value_index", 0);
    const auto valueAfter = optionalJsonI64(payload, "value_after", optionalJsonI64(payload, "value", 0));
    sql += "SET @event_id=NULL; SET @value_after=NULL;";
    sql += "CALL mmo_set_character_script_int(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(scriptKey) + "," + std::to_string(symbolIndex) + "," + std::to_string(valueIndex) + ",";
    sql += std::to_string(valueAfter) + "," + std::to_string(tick) + "," + sqlJson(dbPayload) + ",";
    sql += sqlLiteral(packet.idempotencyKey) + ",@event_id,@value_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::UpdateQuest) {
    const auto questKey = optionalJsonString(payload, "quest_key", optionalJsonString(payload, "topic", packet.targetKey));
    const auto questName = optionalJsonString(payload, "quest_name", optionalJsonString(payload, "name", questKey));
    const auto status = questStatus(payload);
    const auto entryCount = optionalJsonI64(payload, "entry_count", 0);
    sql += "SET @event_id=NULL;";
    sql += "CALL mmo_update_character_quest(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(questKey) + "," + sqlLiteral(questName) + "," + sqlLiteral(status) + ",";
    sql += std::to_string(entryCount) + ",JSON_ARRAY()," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id);";
  } else if(packet.kind == Mmo::SemanticActionKind::SetKnownDialog) {
    const auto npcKey = optionalJsonString(payload, "npc_key", optionalJsonString(payload, "npc_symbol_name"));
    const auto infoKey = optionalJsonString(payload, "info_key", optionalJsonString(payload, "info_symbol_name", packet.targetKey));
    const bool known = optionalJsonBool(payload, "known", true);
    bool permanent = optionalJsonBool(payload, "permanent", optionalJsonBool(payload, "repeatable", false));
    if(!jsonBoolField(payload, "permanent") && !jsonBoolField(payload, "repeatable") && jsonBoolField(payload, "removed"))
      permanent = !optionalJsonBool(payload, "removed", false);
    const std::string availability = optionalJsonString(payload, "availability_state",
      known && permanent ? "repeatable_known" : (known ? "consumed_hidden" : (permanent ? "repeatable_not_seen" : "one_shot_not_seen")));
    sql += "SET @event_id=NULL;";
    sql += "CALL mmo_set_character_known_dialog(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(npcKey) + "," + sqlLiteral(infoKey) + "," + sqlBool(known) + "," + sqlBool(permanent) + ",";
    sql += sqlLiteral(availability) + "," + std::to_string(tick) + "," + sqlJson(dbPayload) + ",";
    sql += sqlLiteral(packet.idempotencyKey) + ",@event_id);";
  } else if(packet.kind == Mmo::SemanticActionKind::AdjustProgression) {
    const auto xp = optionalJsonI64(payload, "experience_delta", optionalJsonI64(payload, "xp_delta", optionalJsonI64(payload, "delta", 0)));
    const auto lp = optionalJsonI64(payload, "learning_points_delta", optionalJsonI64(payload, "lp_delta", 0));
    const auto reason = optionalJsonString(payload, "reason", "script_progression");
    sql += "SET @event_id=NULL; SET @experience_after=NULL; SET @learning_points_after=NULL;";
    sql += "CALL mmo_adjust_character_progression(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += std::to_string(xp) + "," + std::to_string(lp) + "," + sqlLiteral(reason) + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
    sql += ",@event_id,@experience_after,@learning_points_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::ApplyExperienceReward) {
    const auto xp = optionalJsonI64(payload, "experience_delta", optionalJsonI64(payload, "xp_delta", optionalJsonI64(payload, "delta", 0)));
    const auto reason = optionalJsonString(payload, "reason", "script_experience_reward");
    sql += "SET @event_id=NULL; SET @experience_after=NULL;";
    sql += "CALL mmo_apply_character_experience_reward(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += std::to_string(xp) + "," + sqlLiteral(reason) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@experience_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::ApplyCharacterDamage) {
    const auto characterKey = optionalJsonString(payload, "target_character_key",
                            optionalJsonString(payload, "character_key", "PC_HERO"));
    const auto damage = damageAmountFromPayload(payload);
    sql += "SET @event_id=NULL; SET @health_after=NULL;";
    sql += "CALL mmo_apply_character_damage(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(characterKey) + "," + std::to_string(damage) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@health_after);";
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
    sql += "SET @event_id=NULL; SET @health_after=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_apply_world_entity_damage(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(npc.entityKey) + "," + std::to_string(damage) + "," + sqlBool(fatal) + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
    sql += ",@event_id,@health_after,@row_after);";
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
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_mark_npc_dead(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(npc.entityKey) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
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
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_character_resource_delta(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(characterKey) + "," + sqlLiteral(resourceKey) + "," + std::to_string(delta) + ",";
    sql += std::to_string(valueBefore) + "," + std::to_string(valueAfter) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::TriggerEvent) {
    const auto triggerKey = optionalJsonString(payload, "trigger_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto eventTypeName = optionalJsonString(payload, "event_type_name", optionalJsonString(payload, "reason", "trigger_event"));
    sql += "SET @event_id=NULL;";
    sql += "CALL mmo_record_trigger_event(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(triggerKey) + "," + sqlLiteral(eventTypeName) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id);";
  } else if(packet.kind == Mmo::SemanticActionKind::MoverStateChanged) {
    const auto moverKey = optionalJsonString(payload, "mover_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto stateBefore = optionalJsonI64(payload, "state_before", 0);
    const auto stateAfter = optionalJsonI64(payload, "state_after", stateBefore);
    const auto stateAfterName = optionalJsonString(payload, "state_after_name", "");
    const auto frame = optionalJsonI64(payload, "frame", optionalJsonI64(payload, "frame_index", 0));
    const auto targetFrame = optionalJsonI64(payload, "target_frame", optionalJsonI64(payload, "target_frame_index", frame));
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_mover_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(moverKey) + "," + std::to_string(stateBefore) + "," + std::to_string(stateAfter) + ",";
    sql += sqlLiteral(stateAfterName) + "," + std::to_string(frame) + "," + std::to_string(targetFrame) + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
    sql += ",@event_id,@row_after);";
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
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_npc_routine_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(npcKey) + "," + sqlLiteral(routineState) + "," + sqlLiteral(scheduleKey) + ",";
    sql += sqlLiteral(currentWaypoint) + "," + sqlLiteral(targetWaypoint) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
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
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_npc_ai_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(npcKey) + "," + sqlLiteral(aiState) + "," + sqlLiteral(aiIntent) + ",";
    sql += sqlLiteral(targetEntity) + "," + sqlLiteral(perceptionState) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
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
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_npc_path_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(npcKey) + "," + sqlLiteral(pathState) + "," + sqlLiteral(routeKey) + ",";
    sql += sqlLiteral(currentWaypoint) + "," + sqlLiteral(nextWaypoint) + "," + sqlLiteral(targetWaypoint) + ",";
    sql += optionalJsonDoubleSql(payload, "pos_x") + "," + optionalJsonDoubleSql(payload, "pos_y") + "," + optionalJsonDoubleSql(payload, "pos_z") + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::RecordNpcFightState) {
    const auto npcKey = optionalJsonString(payload, "npc_entity_key",
                        optionalJsonString(payload, "actor_npc_key",
                        optionalJsonString(payload, "target_key", packet.targetKey)));
    const auto opponentKey = optionalJsonString(payload, "opponent_key", optionalJsonString(payload, "target_entity_key"));
    const auto fightState = optionalJsonString(payload, "fight_state", "unknown");
    const auto attackState = optionalJsonString(payload, "attack_state", "");
    const auto comboIndex = optionalJsonI64(payload, "combo_index", 0);
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_npc_fight_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(npcKey) + "," + sqlLiteral(opponentKey) + "," + sqlLiteral(fightState) + ",";
    sql += sqlLiteral(attackState) + "," + std::to_string(comboIndex) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::RecordCombatIntent ||
            packet.kind == Mmo::SemanticActionKind::RecordNpcActionState ||
            packet.kind == Mmo::SemanticActionKind::RecordNpcDialogLine) {
    std::cout << "[npc_observation_ignored]"
              << " action=" << Mmo::actionKindName(packet.kind)
              << " target=" << packet.targetKey
              << " reason=content_authority_db_contract_pending"
              << "\n";
    return {true, true, true, "content_authority_db_contract_pending"};
  } else if(packet.kind == Mmo::SemanticActionKind::RecordTriggerQueueState) {
    const auto triggerKey = optionalJsonString(payload, "trigger_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto queueState = optionalJsonString(payload, "queue_state", "queued");
    const auto eventTypeName = optionalJsonString(payload, "event_type_name", optionalJsonString(payload, "reason", "trigger_event"));
    const auto scheduledTick = optionalJsonI64(payload, "scheduled_server_tick", optionalJsonI64(payload, "execute_at_tick", tick));
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_trigger_queue_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(triggerKey) + "," + sqlLiteral(queueState) + "," + sqlLiteral(eventTypeName) + ",";
    sql += std::to_string(scheduledTick) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::RecordWorldTransitionState) {
    const auto fromWorld = optionalJsonString(payload, "from_world_key", optionalJsonString(payload, "from_world", ""));
    const auto toWorld = optionalJsonString(payload, "to_world_key", optionalJsonString(payload, "to_world", optionalJsonString(payload, "world", "")));
    const auto transitionState = optionalJsonString(payload, "transition_state", "visited");
    const auto chapterKey = optionalJsonString(payload, "chapter_key", optionalJsonString(payload, "chapter", ""));
    const bool visited = optionalJsonBool(payload, "visited", true);
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_world_transition_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(fromWorld) + "," + sqlLiteral(toWorld) + "," + sqlLiteral(transitionState) + ",";
    sql += sqlLiteral(chapterKey) + "," + sqlBool(visited) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::ClientCorrectionAck) {
    const auto actionKind = optionalJsonString(payload, "action_kind", "");
    const auto localSequence = optionalJsonI64(payload, "client_local_sequence", 0);
    sql += "SET @row_after=NULL;";
    sql += "CALL mmo_ack_client_action_correction(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(actionKind) + "," + std::to_string(localSequence) + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@row_after);";
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
    sql += "SET @event_id=NULL; SET @target_character_id=NULL; SET @amount_transferred=NULL;";
    sql += "CALL mmo_transfer_character_item(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += "UUID_TO_BIN(" + sqlLiteral(itemUuid) + ",1)," + sqlLiteral(targetCharacter) + ",";
    sql += std::to_string(amount) + "," + std::to_string(tick) + "," + sqlJson(dbPayload) + ",";
    sql += sqlLiteral(packet.idempotencyKey) + ",@event_id,@target_character_id,@amount_transferred);";
  } else if(packet.kind == Mmo::SemanticActionKind::LootNpcInventory ||
            packet.kind == Mmo::SemanticActionKind::TakeContainerItem) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    auto bagIndex = optionalJsonI64(payload, "server_bag_index", -1);
    if(bagIndex < 0)
      bagIndex = nextBagIndex(target, sessionUuid);
    try {
      const auto sourceEntityKey = resolveWorldInventoryOwnerEntityKey(target, sessionUuid, packet);
      const auto itemUuid = resolveNpcInventoryItemUuid(target, sessionUuid, sourceEntityKey, packet);
      sql += "SET @event_id=NULL; SET @source_amount_remaining=NULL; SET @amount_looted=NULL;";
      sql += "CALL mmo_loot_npc_inventory(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
      sql += sqlLiteral(sourceEntityKey) + ",UUID_TO_BIN(" + sqlLiteral(itemUuid) + ",1),";
      sql += std::to_string(amount) + "," + std::to_string(bagIndex) + "," + std::to_string(tick) + ",";
      sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
      sql += ",@event_id,@source_amount_remaining,@amount_looted);";
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
        sql += "SET @event_id=NULL; SET @source_amount_remaining=NULL; SET @amount_looted=NULL;";
        sql += "CALL mmo_loot_npc_inventory(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
        sql += sqlLiteral(sourceEntityKey) + ",UUID_TO_BIN(" + sqlLiteral(itemUuid) + ",1),";
        sql += std::to_string(amount) + "," + std::to_string(bagIndex) + "," + std::to_string(tick) + ",";
        sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
        sql += ",@event_id,@source_amount_remaining,@amount_looted);";
      } catch(const std::exception& materializeError) {
        std::cerr << "[npc_loot_grant_fallback] target=" << packet.targetKey
                  << " symbol=" << symbol
                  << " amount=" << amount
                  << " resolve_reason=" << resolveError.what()
                  << " materialize_reason=" << materializeError.what() << "\n";
        sql += "SET @event_id=NULL; SET @item_id=NULL; SET @amount_granted=NULL;";
        sql += "CALL mmo_grant_character_item_by_symbol(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
        sql += std::to_string(symbol) + "," + std::to_string(amount) + "," + std::to_string(bagIndex) + ",";
        sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
        sql += ",@event_id,@item_id,@amount_granted);";
      }
    }
  } else if(packet.kind == Mmo::SemanticActionKind::PickupWorldItem) {
    const auto amount = optionalJsonI64(payload, "amount", 1);
    auto bagIndex = optionalJsonI64(payload, "server_bag_index", -1);
    if(bagIndex < 0)
      bagIndex = nextBagIndex(target, sessionUuid);
    try {
      const auto entityKey = resolveWorldItemEntityKey(target, sessionUuid, packet);
      sql += "SET @event_id=NULL; SET @item_id=NULL; SET @amount_picked=NULL;";
      sql += "CALL mmo_pickup_world_item(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
      sql += sqlLiteral(entityKey) + "," + std::to_string(amount) + "," + std::to_string(bagIndex) + ",";
      sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
      sql += ",@event_id,@item_id,@amount_picked);";
    } catch(const std::exception& resolveError) {
      const auto symbol = itemSymbolFromPayload(payload);
      if(symbol < 0)
        throw;
      try {
        const auto entityKey = materializeObservedWorldItem(target, sessionUuid, packet, dbPayload);
        sql += "SET @event_id=NULL; SET @item_id=NULL; SET @amount_picked=NULL;";
        sql += "CALL mmo_pickup_world_item(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
        sql += sqlLiteral(entityKey) + "," + std::to_string(amount) + "," + std::to_string(bagIndex) + ",";
        sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
        sql += ",@event_id,@item_id,@amount_picked);";
      } catch(const std::exception& materializeError) {
        std::cerr << "[world_item_pickup_grant_fallback] target=" << packet.targetKey
                  << " symbol=" << symbol
                  << " amount=" << amount
                  << " resolve_reason=" << resolveError.what()
                  << " materialize_reason=" << materializeError.what() << "\n";
        sql += "SET @event_id=NULL; SET @item_id=NULL; SET @amount_granted=NULL;";
        sql += "CALL mmo_grant_character_item_by_symbol(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
        sql += std::to_string(symbol) + "," + std::to_string(amount) + "," + std::to_string(bagIndex) + ",";
        sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
        sql += ",@event_id,@item_id,@amount_granted);";
      }
    }
  } else if(packet.kind == Mmo::SemanticActionKind::RemoveWorldItem) {
    const auto entityKey = resolveWorldItemEntityKey(target, sessionUuid, packet);
    const auto reason = optionalJsonString(payload, "reason", "semantic_action");
    sql += "SET @event_id=NULL; SET @item_id=NULL;";
    sql += "CALL mmo_remove_world_item(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(entityKey) + "," + sqlLiteral(reason) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@item_id);";
  } else if(packet.kind == Mmo::SemanticActionKind::EquipCharacterItem) {
    const auto itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet);
    const auto slot = normalizedEquipmentSlot(payload);
    sql += "SET @event_id=NULL;";
    sql += "CALL mmo_equip_character_item(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += "UUID_TO_BIN(" + sqlLiteral(itemUuid) + ",1)," + sqlLiteral(slot) + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id);";
  } else if(packet.kind == Mmo::SemanticActionKind::UnequipCharacterItem) {
    const auto slot = normalizedEquipmentSlot(payload);
    sql += "SET @event_id=NULL; SET @item_id=NULL;";
    sql += "CALL mmo_unequip_character_item(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(slot) + "," + std::to_string(tick) + "," + sqlJson(dbPayload) + ",";
    sql += sqlLiteral(packet.idempotencyKey) + ",@event_id,@item_id);";
  } else if(packet.kind == Mmo::SemanticActionKind::DropCharacterItem) {
    const auto itemUuid = resolveCharacterItemUuid(target, sessionUuid, packet);
    const auto amount = optionalJsonI64(payload, "amount", 1);
    const auto entityKey = optionalJsonString(payload, "world_item_entity_key",
                           optionalJsonString(payload, "engine_world_item_key",
                           optionalJsonString(payload, "dropped_world_item_key",
                           optionalJsonString(payload, "target_key", packet.targetKey))));
    sql += "SET @event_id=NULL; SET @amount_remaining=NULL; SET @amount_dropped=NULL;";
    sql += "CALL mmo_drop_character_item(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += "UUID_TO_BIN(" + sqlLiteral(itemUuid) + ",1),";
    sql += std::to_string(amount) + "," + sqlLiteral(entityKey) + ",";
    sql += optionalJsonPositionDoubleSql(payload, "x", "pos_x", "world_pos_x", "actor_pos_x") + ",";
    sql += optionalJsonPositionDoubleSql(payload, "y", "pos_y", "world_pos_y", "actor_pos_y") + ",";
    sql += optionalJsonPositionDoubleSql(payload, "z", "pos_z", "world_pos_z", "actor_pos_z") + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
    sql += ",@event_id,@amount_remaining,@amount_dropped);";
  } else if(packet.kind == Mmo::SemanticActionKind::UseInteractive) {
    const auto key = optionalJsonString(payload, "interactive_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto state = optionalJsonI64(payload, "state_after", optionalJsonI64(payload, "state", 0));
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_interactive_use(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(key) + "," + std::to_string(state) + "," + std::to_string(tick) + ",";
    sql += sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey) + ",@event_id,@row_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::UpdateInteractiveState) {
    const auto key = optionalJsonString(payload, "interactive_key", optionalJsonString(payload, "target_key", packet.targetKey));
    const auto state = optionalJsonI64(payload, "state_after", optionalJsonI64(payload, "state", 0));
    const auto count = optionalJsonI64(payload, "state_count", 0);
    const auto mask = optionalJsonI64(payload, "state_mask", 0);
    const bool locked = optionalJsonBool(payload, "locked_after", optionalJsonBool(payload, "locked", false));
    const bool cracked = optionalJsonBool(payload, "cracked_after", optionalJsonBool(payload, "cracked", false));
    const auto lifecycle = optionalJsonString(payload, "lifecycle_state", "active");
    sql += "SET @event_id=NULL; SET @row_version_after=NULL;";
    sql += "CALL mmo_update_interactive_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(key) + "," + std::to_string(state) + "," + std::to_string(count) + "," + std::to_string(mask) + ",";
    sql += sqlBool(locked) + std::string(",") + sqlBool(cracked) + "," + sqlLiteral(lifecycle) + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
    sql += ",@event_id,@row_version_after);";
  } else if(packet.kind == Mmo::SemanticActionKind::ReadyWeapon || packet.kind == Mmo::SemanticActionKind::HolsterWeapon) {
    const auto actorKey = optionalJsonString(payload, "actor_key", optionalJsonString(payload, "actor_entity_key", packet.targetKey));
    const auto state = optionalJsonString(payload, "new_weapon_state",
                       optionalJsonString(payload, "weapon_state",
                       packet.kind == Mmo::SemanticActionKind::HolsterWeapon ? "no_weapon" : "ready_weapon"));
    const bool ready = optionalJsonBool(payload, "ready", packet.kind == Mmo::SemanticActionKind::ReadyWeapon);
    sql += "SET @event_id=NULL; SET @row_after=NULL;";
    sql += "CALL mmo_record_npc_weapon_state(UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1),";
    sql += sqlLiteral(actorKey) + "," + sqlLiteral(state) + "," + sqlBool(ready) + ",";
    sql += std::to_string(tick) + "," + sqlJson(dbPayload) + "," + sqlLiteral(packet.idempotencyKey);
    sql += ",@event_id,@row_after);";
  } else {
    return {false, true, false, "unhandled"};
  }

  (void)runMysql(target, sql);
  return {true, true, true, "direct_applied"};
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
    else if(arg == "--runtime-read-model" || arg == "--runtime-read-model-path") opt.runtimeReadModelPath = need(i, arg);
    else if(arg == "--runtime-read-model-active-export-plan-only") opt.runtimeReadModelActiveExportPlanOnly = true;
    else if(arg == "--content-revision-key") opt.contentRevisionKey = need(i, arg);
    else if(arg == "--world-instance-key") opt.worldInstanceKey = need(i, arg);
    else if(arg == "--world-name") opt.worldName = need(i, arg);
    else if(arg == "--world-instance-ai-db-name") opt.worldInstanceAiDatabaseName = need(i, arg);
    else if(arg == "--world-instance-ai-perception-kind") opt.worldInstanceAiPerceptionKind = need(i, arg);
    else if(arg == "--enable-ai-dialog-intent-send") opt.enableAiDialogIntentSend = true;
    else if(arg == "--no-enable-ai-dialog-intent-send") opt.enableAiDialogIntentSend = false;
    else if(arg == "--ai-dialog-intent-conversation-session-probe" ||
            arg == "--enable-ai-dialog-intent-conversation-session-probe") opt.aiDialogIntentConversationSessionProbe = true;
    else if(arg == "--no-ai-dialog-intent-conversation-session-probe" ||
            arg == "--no-enable-ai-dialog-intent-conversation-session-probe") opt.aiDialogIntentConversationSessionProbe = false;
    else if(arg == "--ai-dialog-intent-late-observer-resume-plan" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-plan") opt.aiDialogIntentLateObserverResumePlan = true;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-plan" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-plan") opt.aiDialogIntentLateObserverResumePlan = false;
    else if(arg == "--ai-dialog-intent-late-observer-resume-packet-boundary" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-packet-boundary") opt.aiDialogIntentLateObserverResumePacketBoundary = true;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-packet-boundary" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-packet-boundary") opt.aiDialogIntentLateObserverResumePacketBoundary = false;
    else if(arg == "--ai-dialog-intent-late-observer-resume-send-gate" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-send-gate") opt.aiDialogIntentLateObserverResumeSendGate = true;
    else if(arg == "--ai-dialog-intent-late-observer-resume-runtime-send" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-runtime-send") opt.aiDialogIntentLateObserverResumeRuntimeSend = true;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-send-gate" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-send-gate") opt.aiDialogIntentLateObserverResumeSendGate = false;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-runtime-send" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-runtime-send") opt.aiDialogIntentLateObserverResumeRuntimeSend = false;
    else if(arg == "--ai-dialog-intent-late-observer-resume-delivery-registration-boundary" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-delivery-registration-boundary") opt.aiDialogIntentLateObserverResumeDeliveryRegistrationBoundary = true;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-delivery-registration-boundary" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-delivery-registration-boundary") opt.aiDialogIntentLateObserverResumeDeliveryRegistrationBoundary = false;
    else if(arg == "--ai-dialog-intent-late-observer-resume-dispatch-envelope" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-dispatch-envelope") opt.aiDialogIntentLateObserverResumeDispatchEnvelope = true;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-dispatch-envelope" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-dispatch-envelope") opt.aiDialogIntentLateObserverResumeDispatchEnvelope = false;
    else if(arg == "--ai-dialog-intent-late-observer-resume-mutation-guard" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-mutation-guard") opt.aiDialogIntentLateObserverResumeMutationGuard = true;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-mutation-guard" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-mutation-guard") opt.aiDialogIntentLateObserverResumeMutationGuard = false;
    else if(arg == "--ai-dialog-intent-late-observer-resume-send-failure-dead-letter-guard" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-send-failure-dead-letter-guard") opt.aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard = true;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-send-failure-dead-letter-guard" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-send-failure-dead-letter-guard") opt.aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard = false;
    else if(arg == "--ai-dialog-intent-late-observer-resume-commit-preflight" ||
            arg == "--enable-ai-dialog-intent-late-observer-resume-commit-preflight") opt.aiDialogIntentLateObserverResumeCommitPreflight = true;
    else if(arg == "--no-ai-dialog-intent-late-observer-resume-commit-preflight" ||
            arg == "--no-enable-ai-dialog-intent-late-observer-resume-commit-preflight") opt.aiDialogIntentLateObserverResumeCommitPreflight = false;
    else if(arg == "--ai-dialog-intent-observation-receipt-probe" ||
            arg == "--enable-ai-dialog-intent-observation-receipt-probe") opt.aiDialogIntentObservationReceiptProbe = true;
    else if(arg == "--no-ai-dialog-intent-observation-receipt-probe" ||
            arg == "--no-enable-ai-dialog-intent-observation-receipt-probe") opt.aiDialogIntentObservationReceiptProbe = false;
    else if(arg == "--ai-dialog-intent-step273-persistence-preview" ||
            arg == "--enable-ai-dialog-intent-step273-persistence-preview") opt.aiDialogIntentStep273PersistencePreview = true;
    else if(arg == "--no-ai-dialog-intent-step273-persistence-preview" ||
            arg == "--no-enable-ai-dialog-intent-step273-persistence-preview") opt.aiDialogIntentStep273PersistencePreview = false;
    else if(arg == "--ai-dialog-intent-step273-persistence-execution-boundary" ||
            arg == "--enable-ai-dialog-intent-step273-persistence-execution-boundary") opt.aiDialogIntentStep273PersistenceExecutionBoundary = true;
    else if(arg == "--no-ai-dialog-intent-step273-persistence-execution-boundary" ||
            arg == "--no-enable-ai-dialog-intent-step273-persistence-execution-boundary") opt.aiDialogIntentStep273PersistenceExecutionBoundary = false;
    else if(arg == "--ai-dialog-intent-step273-persistence-executor-adapter" ||
            arg == "--enable-ai-dialog-intent-step273-persistence-executor-adapter") opt.aiDialogIntentStep273PersistenceExecutorAdapter = true;
    else if(arg == "--no-ai-dialog-intent-step273-persistence-executor-adapter" ||
            arg == "--no-enable-ai-dialog-intent-step273-persistence-executor-adapter") opt.aiDialogIntentStep273PersistenceExecutorAdapter = false;
    else if(arg == "--ai-dialog-intent-step273-persistence-dry-run-report-gate" ||
            arg == "--enable-ai-dialog-intent-step273-persistence-dry-run-report-gate") opt.aiDialogIntentStep273PersistenceDryRunReportGate = true;
    else if(arg == "--no-ai-dialog-intent-step273-persistence-dry-run-report-gate" ||
            arg == "--no-enable-ai-dialog-intent-step273-persistence-dry-run-report-gate") opt.aiDialogIntentStep273PersistenceDryRunReportGate = false;
    else if(arg == "--ai-dialog-intent-step273-persistence-outcome-policy" ||
            arg == "--enable-ai-dialog-intent-step273-persistence-outcome-policy") opt.aiDialogIntentStep273PersistenceOutcomePolicy = true;
    else if(arg == "--no-ai-dialog-intent-step273-persistence-outcome-policy" ||
            arg == "--no-enable-ai-dialog-intent-step273-persistence-outcome-policy") opt.aiDialogIntentStep273PersistenceOutcomePolicy = false;
    else if(arg == "--ai-dialog-intent-step273-persistence-outcome-observability" ||
            arg == "--enable-ai-dialog-intent-step273-persistence-outcome-observability") opt.aiDialogIntentStep273PersistenceOutcomeObservability = true;
    else if(arg == "--no-ai-dialog-intent-step273-persistence-outcome-observability" ||
            arg == "--no-enable-ai-dialog-intent-step273-persistence-outcome-observability") opt.aiDialogIntentStep273PersistenceOutcomeObservability = false;
    else if(arg == "--ai-dialog-intent-step273-persistence-activation-preflight" ||
            arg == "--enable-ai-dialog-intent-step273-persistence-activation-preflight") opt.aiDialogIntentStep273PersistenceActivationPreflight = true;
    else if(arg == "--no-ai-dialog-intent-step273-persistence-activation-preflight" ||
            arg == "--no-enable-ai-dialog-intent-step273-persistence-activation-preflight") opt.aiDialogIntentStep273PersistenceActivationPreflight = false;
    else if(arg == "--ai-dialog-intent-step273-persistence-runtime-storage" ||
            arg == "--enable-ai-dialog-intent-step273-persistence-runtime-storage") opt.aiDialogIntentStep273PersistenceRuntimeStorage = true;
    else if(arg == "--no-ai-dialog-intent-step273-persistence-runtime-storage" ||
            arg == "--no-enable-ai-dialog-intent-step273-persistence-runtime-storage") opt.aiDialogIntentStep273PersistenceRuntimeStorage = false;
    else if(arg == "--ai-dialog-intent-step273-durable-mark-applied-gate" ||
            arg == "--enable-ai-dialog-intent-step273-durable-mark-applied-gate") opt.aiDialogIntentStep273DurableMarkAppliedGate = true;
    else if(arg == "--no-ai-dialog-intent-step273-durable-mark-applied-gate" ||
            arg == "--no-enable-ai-dialog-intent-step273-durable-mark-applied-gate") opt.aiDialogIntentStep273DurableMarkAppliedGate = false;
    else if(arg == "--ai-dialog-intent-text") opt.aiDialogIntentText = need(i, arg);
    else if(arg == "--ai-dialog-intent-speaker-entity-key") opt.aiDialogIntentSpeakerEntityKey = need(i, arg);
    else if(arg == "--ai-dialog-intent-line-id") opt.aiDialogIntentLineId = need(i, arg);
    else if(arg == "--ai-dialog-intent-fanout-mode") {
      opt.aiDialogIntentFanoutMode = need(i, arg);
      if(parseGameplayFanoutMode(opt.aiDialogIntentFanoutMode) == Mmo::Server::GameplayFanoutMode::TargetSessionOnly &&
         opt.aiDialogIntentFanoutMode != "target_session" && opt.aiDialogIntentFanoutMode != "target") {
        throw std::runtime_error("--ai-dialog-intent-fanout-mode expects target_session or aoi");
      }
    }
    else if(arg == "--ai-dialog-intent-aoi-radius") opt.aiDialogIntentAoiRadius = parseDouble(need(i, arg)).value_or(opt.aiDialogIntentAoiRadius);
    else if(arg == "--ai-dialog-intent-max-recipients") opt.aiDialogIntentMaxRecipients = parseSizeOr(need(i, arg), opt.aiDialogIntentMaxRecipients);
    else if(arg == "--ai-dialog-intent-ack-timeout-ms") {
      const auto parsed = parseU64OrZero(need(i, arg));
      opt.aiDialogIntentAckTimeoutMs = parsed == 0 ? opt.aiDialogIntentAckTimeoutMs : parsed;
    }
    else if(arg == "--world-instance-ai-max-distance") opt.worldInstanceAiMaxDistance = parseDouble(need(i, arg)).value_or(opt.worldInstanceAiMaxDistance);
    else if(arg == "--world-instance-ai-server-tick") opt.worldInstanceAiServerTick = parseU64OrZero(need(i, arg));
    else if(arg == "--world-instance-ai-cooldown-ticks") opt.worldInstanceAiCooldownTicks = parseU64OrZero(need(i, arg));
    else if(arg == "--world-instance-ai-priority") opt.worldInstanceAiPriorityValue = parseInt(need(i, arg)).value_or(opt.worldInstanceAiPriorityValue);
    else if(arg == "--world-instance-ai-scheduler-plan-only") opt.worldInstanceAiSchedulerPlanOnly = true;
    else if(arg == "--world-instance-ai-scheduler-interval-ms") opt.worldInstanceAiSchedulerIntervalMs = parseU64OrZero(need(i, arg));
    else if(arg == "--world-instance-ai-max-npcs") opt.worldInstanceAiMaxNpcs = parseSizeOr(need(i, arg), opt.worldInstanceAiMaxNpcs);
    else if(arg == "--world-instance-ai-max-players") opt.worldInstanceAiMaxPlayers = parseSizeOr(need(i, arg), opt.worldInstanceAiMaxPlayers);
    else if(arg == "--world-instance-ai-min-accepted-npcs") opt.worldInstanceAiMinAcceptedNpcs = parseSizeOr(need(i, arg), opt.worldInstanceAiMinAcceptedNpcs);
    else if(arg == "--world-instance-ai-min-player-actors") opt.worldInstanceAiMinPlayerActors = parseSizeOr(need(i, arg), opt.worldInstanceAiMinPlayerActors);
    else if(arg == "--world-instance-ai-min-decisions") opt.worldInstanceAiMinDecisions = parseSizeOr(need(i, arg), opt.worldInstanceAiMinDecisions);
    else if(arg == "--world-instance-ai-max-skipped-weak-npc-identity") opt.worldInstanceAiMaxSkippedWeakNpcIdentity = parseSizeOr(need(i, arg), opt.worldInstanceAiMaxSkippedWeakNpcIdentity);
    else if(arg == "--world-instance-ai-max-skipped-missing-npc-instance") opt.worldInstanceAiMaxSkippedMissingNpcInstance = parseSizeOr(need(i, arg), opt.worldInstanceAiMaxSkippedMissingNpcInstance);
    else if(arg == "--world-instance-ai-startup-dry-run") opt.worldInstanceAiStartupDryRun = true;
    else if(arg == "--world-instance-ai-startup-fail-on-evidence") { opt.worldInstanceAiStartupDryRun = true; opt.worldInstanceAiStartupFailOnEvidence = true; }
    else if(arg == "--world-instance-ai-startup-strict-evidence") {
      opt.worldInstanceAiStartupDryRun = true;
      opt.worldInstanceAiStartupFailOnEvidence = true;
      opt.worldInstanceAiRequireActorPair = true;
      opt.worldInstanceAiRequireDecision = true;
      opt.worldInstanceAiRequireCleanNpcIdentity = true;
      opt.worldInstanceAiMinAcceptedNpcs = std::max<std::size_t>(opt.worldInstanceAiMinAcceptedNpcs, 1);
      opt.worldInstanceAiMinPlayerActors = std::max<std::size_t>(opt.worldInstanceAiMinPlayerActors, 1);
      opt.worldInstanceAiMinDecisions = std::max<std::size_t>(opt.worldInstanceAiMinDecisions, 1);
    }
    else if(arg == "--world-instance-ai-require-actor-pair") {
      opt.worldInstanceAiRequireActorPair = true;
      opt.worldInstanceAiMinAcceptedNpcs = std::max<std::size_t>(opt.worldInstanceAiMinAcceptedNpcs, 1);
      opt.worldInstanceAiMinPlayerActors = std::max<std::size_t>(opt.worldInstanceAiMinPlayerActors, 1);
    }
    else if(arg == "--world-instance-ai-require-decision") {
      opt.worldInstanceAiRequireDecision = true;
      opt.worldInstanceAiMinDecisions = std::max<std::size_t>(opt.worldInstanceAiMinDecisions, 1);
    }
    else if(arg == "--world-instance-ai-require-clean-npc-identity") opt.worldInstanceAiRequireCleanNpcIdentity = true;
    else if(arg == "--world-instance-ai-require-no-record-limit-skip") opt.worldInstanceAiRequireNoRecordLimitSkip = true;
    else if(arg == "--world-instance-ai-no-repair-weak-npc-entity-keys") opt.worldInstanceAiRepairWeakNpcEntityKeys = false;
    else if(arg == "--world-instance-ai-include-weak-npc-identity") opt.worldInstanceAiIncludeWeakNpcIdentity = true;
    else if(arg == "--world-instance-npc-identity-materialization-plan-only") opt.worldInstanceNpcIdentityMaterializationPlanOnly = true;
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
    else if(arg == "--require-runtime-read-model-content-revision-match") opt.requireRuntimeReadModelContentRevisionMatch = true;
    else if(arg == "--no-require-runtime-read-model-content-revision-match") opt.requireRuntimeReadModelContentRevisionMatch = false;
    else if(arg == "--startup-check-only") opt.startupCheckOnly = true;
    else if(arg == "--help" || arg == "-h") {
      std::cout << "Usage: mmo_udp_server --bind 127.0.0.1:29777 --mysql-url mysql://user:pass@host:3306/db [--session-key local-dev-PC_HERO_TEST] [--enqueue-outbox] [--no-direct-db] [--runtime-read-model PATH --content-revision-key KEY --world-instance-key KEY --world-name NAME] [--runtime-read-model-active-export-plan-only] [--world-instance-ai-startup-dry-run|--world-instance-ai-startup-strict-evidence] [--world-instance-ai-scheduler-plan-only] [--world-instance-npc-identity-materialization-plan-only] [--enable-ai-dialog-intent-send] [--ai-dialog-intent-conversation-session-probe] [--ai-dialog-intent-fanout-mode target_session|aoi] [--ai-dialog-intent-aoi-radius 1800] [--ai-dialog-intent-ack-timeout-ms 5000] [--ai-dialog-intent-late-observer-resume-delivery-registration-boundary] [--ai-dialog-intent-late-observer-resume-dispatch-envelope] [--ai-dialog-intent-late-observer-resume-mutation-guard] [--ai-dialog-intent-late-observer-resume-send-failure-dead-letter-guard] [--ai-dialog-intent-late-observer-resume-commit-preflight] [--ai-dialog-intent-step273-persistence-preview] [--ai-dialog-intent-step273-persistence-execution-boundary] [--ai-dialog-intent-step273-persistence-executor-adapter] [--ai-dialog-intent-step273-persistence-dry-run-report-gate] [--ai-dialog-intent-step273-persistence-outcome-policy] [--ai-dialog-intent-step273-persistence-outcome-observability] [--ai-dialog-intent-step273-persistence-activation-preflight] [--ai-dialog-intent-step273-persistence-runtime-storage] [--ai-dialog-intent-step273-durable-mark-applied-gate] [--startup-check-only] [--require-db-save-checkpoint-restore]\n";
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
    if(opt.directDb || opt.enqueueOutbox || opt.aiDialogIntentStep273PersistenceRuntimeStorage || opt.aiDialogIntentLateObserverResumeRuntimeSend || opt.aiDialogIntentStep273DurableMarkAppliedGate) {
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
                << " ai_dialog_intent_send=" << (opt.enableAiDialogIntentSend ? "on" : "off")
                << " ai_dialog_intent_conversation_session_probe=" << (opt.aiDialogIntentConversationSessionProbe ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_plan=" << (opt.aiDialogIntentLateObserverResumePlan ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_packet_boundary=" << (opt.aiDialogIntentLateObserverResumePacketBoundary ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_send_gate=" << (opt.aiDialogIntentLateObserverResumeSendGate ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_delivery_registration_boundary=" << (opt.aiDialogIntentLateObserverResumeDeliveryRegistrationBoundary ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_dispatch_envelope=" << (opt.aiDialogIntentLateObserverResumeDispatchEnvelope ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_mutation_guard=" << (opt.aiDialogIntentLateObserverResumeMutationGuard ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_send_failure_dead_letter_guard=" << (opt.aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_commit_preflight=" << (opt.aiDialogIntentLateObserverResumeCommitPreflight ? "on" : "off")
                << " ai_dialog_intent_observation_receipt_probe=" << (opt.aiDialogIntentObservationReceiptProbe ? "on" : "off")
                << " ai_dialog_intent_step273_persistence_preview=" << (opt.aiDialogIntentStep273PersistencePreview ? "on" : "off")
                << " ai_dialog_intent_step273_persistence_execution_boundary=" << (opt.aiDialogIntentStep273PersistenceExecutionBoundary ? "on" : "off")
                << " ai_dialog_intent_step273_persistence_executor_adapter=" << (opt.aiDialogIntentStep273PersistenceExecutorAdapter ? "on" : "off")
                << " ai_dialog_intent_step273_persistence_dry_run_report_gate=" << (opt.aiDialogIntentStep273PersistenceDryRunReportGate ? "on" : "off")
                << " ai_dialog_intent_step273_persistence_outcome_policy=" << (opt.aiDialogIntentStep273PersistenceOutcomePolicy ? "on" : "off")
                << " ai_dialog_intent_step273_persistence_outcome_observability=" << (opt.aiDialogIntentStep273PersistenceOutcomeObservability ? "on" : "off")
                << " ai_dialog_intent_step273_persistence_activation_preflight=" << (opt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off")
                << " ai_dialog_intent_step273_persistence_runtime_storage=" << (opt.aiDialogIntentStep273PersistenceRuntimeStorage ? "on" : "off")
                << " ai_dialog_intent_step273_durable_mark_applied_gate=" << (opt.aiDialogIntentStep273DurableMarkAppliedGate ? "on" : "off")
                << " ai_dialog_intent_late_observer_resume_runtime_send=" << (opt.aiDialogIntentLateObserverResumeRuntimeSend ? "on" : "off")
                << " ai_dialog_intent_fanout_mode=" << gameplayFanoutModeConfigName(opt.aiDialogIntentFanoutMode)
                << " ai_dialog_intent_aoi_radius=" << opt.aiDialogIntentAoiRadius
                << " ai_dialog_intent_max_recipients=" << opt.aiDialogIntentMaxRecipients
                << " ai_dialog_intent_ack_timeout_ms=" << opt.aiDialogIntentAckTimeoutMs
                << "\n";
    }

    std::optional<ActiveReadModelSelectionResult> activeReadModelSelectionPlan;
    if(opt.runtimeReadModelActiveExportPlanOnly) {
      activeReadModelSelectionPlan.emplace(
          Mmo::ContentBuild::planActiveReadModelSelection(makeActiveReadModelSelectionRequest(opt)));
      printActiveReadModelSelectionPlan(*activeReadModelSelectionPlan);
    }

    std::optional<WorldInstanceAiSchedulerPlan> worldInstanceAiSchedulerPlan;
    if(opt.worldInstanceAiSchedulerPlanOnly) {
      worldInstanceAiSchedulerPlan.emplace(
          Mmo::WorldInstanceAiTick::buildWorldInstanceAiSchedulerPlan(makeWorldInstanceAiSchedulerPlanOptions(opt)));
      printWorldInstanceAiSchedulerPlan(*worldInstanceAiSchedulerPlan);
    }

    std::optional<WorldInstanceContentCache> worldContentCache;
    if(worldInstanceContentCacheEnabled(opt)) {
      worldContentCache.emplace(loadWorldInstanceContentCache(opt));
      printWorldInstanceContentCacheReady(*worldContentCache);
    }

    std::optional<RuntimeNpcIdentityMaterializationPlan> npcIdentityMaterializationPlan;
    if(opt.worldInstanceNpcIdentityMaterializationPlanOnly) {
      if(!worldContentCache) {
        throw std::runtime_error("--runtime-read-model, --content-revision-key, --world-instance-key and --world-name are required for --world-instance-npc-identity-materialization-plan-only");
      }
      npcIdentityMaterializationPlan.emplace(
          Mmo::NpcPerceptionRuntime::buildRuntimeNpcIdentityMaterializationReadinessPlan(*worldContentCache));
      printRuntimeNpcIdentityMaterializationPlan(*npcIdentityMaterializationPlan);
    }

    std::optional<WorldInstanceAiSchedulerBoundaryResult> worldInstanceAiStartupDryRun;
    if(worldInstanceAiStartupDryRunEnabled(opt)) {
      if(opt.mysqlUrl.empty()) {
        throw std::runtime_error("--mysql-url is required for --world-instance-ai-startup-dry-run");
      }
      if(!worldContentCache) {
        throw std::runtime_error("--runtime-read-model, --content-revision-key, --world-instance-key and --world-name are required for --world-instance-ai-startup-dry-run");
      }
      const auto aiTarget = mysql ? *mysql : parseMysqlUrl(opt.mysqlUrl);
      worldInstanceAiStartupDryRun.emplace(
          Mmo::WorldInstanceAiTick::runWorldInstanceAiSchedulerBoundary(aiTarget, makeWorldInstanceAiStartupOptions(opt)));
      printWorldInstanceAiStartupDryRunResult(*worldInstanceAiStartupDryRun);
      if(opt.worldInstanceAiStartupFailOnEvidence && !worldInstanceAiStartupDryRun->evidence.accepted) {
        std::cout << "startup_check=evidence_failed"
                  << " world_instance_ai_status=" << logToken(worldInstanceAiStartupDryRun->evidence.status)
                  << " world_instance_ai_reason=" << logToken(worldInstanceAiStartupDryRun->evidence.reason)
                  << "\n";
        return 3;
      }
    }

    if(opt.startupCheckOnly) {
      std::cout << "startup_check=ok"
                << " direct_db=" << (opt.directDb ? "on" : "off")
                << " enqueue_outbox=" << (opt.enqueueOutbox ? "on" : "off")
                << " active_read_model_selection_plan=" << (activeReadModelSelectionPlan ? "on" : "off")
                << " world_instance_content_cache=" << (worldContentCache ? "ready" : "off")
                << " world_instance_npc_identity_materialization_plan=" << (npcIdentityMaterializationPlan ? "on" : "off")
                << " world_instance_ai_scheduler_plan="
                << (!worldInstanceAiSchedulerPlan ? "off" : (worldInstanceAiSchedulerPlan->issues.empty() ? "accepted" : "invalid"))
                << " world_instance_ai_startup_dry_run="
                << (!worldInstanceAiStartupDryRun ? "off" : (worldInstanceAiStartupDryRun->evidence.accepted ? "accepted" : "evidence_failed"))
                << "\n";
      return 0;
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
    std::uint64_t gameplayAcks = 0;
    std::uint64_t gameplayNacks = 0;
    std::uint64_t dialogIntentSent = 0;
    std::uint64_t dialogIntentFailed = 0;
    std::uint64_t nextDialogIntentSequence = 0;
    std::uint32_t nextSnapshotId = 1;
    ServerPacketLogState logState;
    LiveWorldSnapshotState liveWorldSnapshotState;
    ClientRouteRegistry clientRoutes;
    Mmo::Server::OutboundGameplayDeliveryState outboundGameplayDelivery;
    Mmo::Server::ConversationSessionRuntime conversationSessions;
    Mmo::Server::ConversationResumeSendGate conversationResumeSendGate;
    Mmo::Server::ConversationResumeDeliveryRegistrationBoundary conversationResumeDeliveryRegistrationBoundary;
    Mmo::Server::ConversationResumeDispatchEnvelope conversationResumeDispatchEnvelope;
    Mmo::Server::ConversationResumeMutationGuard conversationResumeMutationGuard;
    Mmo::Server::ConversationResumeSendFailureDeadLetterGuard conversationResumeSendFailureDeadLetterGuard;
    Mmo::Server::ConversationResumeCommitPreflight conversationResumeCommitPreflight;
    std::unordered_set<std::string> dialogIntentBootstrapSentSessions;
    std::uint64_t lastOutboundDeliveryExpiryScanMs = 0;

    auto scanOutboundDeliveryTimeouts = [&]() noexcept {
      const auto nowMs = monotonicNowMs();
      if(lastOutboundDeliveryExpiryScanMs != 0 && nowMs < lastOutboundDeliveryExpiryScanMs + 500)
        return;
      lastOutboundDeliveryExpiryScanMs = nowMs;
      for(const auto& expired : outboundGameplayDelivery.expirePending(nowMs)) {
        std::cout << "[server_outbound_gameplay_delivery_timeout]"
                  << " session_uuid=" << expired.sessionUuid
                  << " character=" << expired.characterKey
                  << " action_id=" << expired.actionId
                  << " ack_key=" << expired.ackKey
                  << " packet_sequence=" << expired.packetSequence
                  << " deadline_ms=" << expired.deadlineMs
                  << " retry=deferred"
                  << " db_terminal_storage=" << (activeOpt.aiDialogIntentStep273PersistenceRuntimeStorage ? "step281" : "off")
                  << "\n";
      }
      if(activeOpt.aiDialogIntentStep273PersistenceRuntimeStorage && mysql) {
        Mmo::Server::AiDialogIntentDeliveryPersistenceTimeoutStorageRequest timeoutRequest;
        timeoutRequest.enabled = true;
        timeoutRequest.allowMysqlExecution = true;
        timeoutRequest.aiDatabaseName = activeOpt.worldInstanceAiDatabaseName;
        const auto timeoutSql = Mmo::Server::buildAiDialogIntentDeliveryPersistenceTimeoutStorageSql(timeoutRequest);
        if(timeoutSql.ready) {
          try {
            const auto rawStorage = runMysql(*mysql, timeoutSql.sql);
            const auto storageParts = splitMysqlLastRow(rawStorage);
            const auto timedOutCount = storageParts.empty() ? std::string("0") : storageParts[0];
            if(timedOutCount != "0") {
              std::cout << "[server_ai_dialog_intent_step281_runtime_storage_timeout]"
                        << " timed_out_count=" << timedOutCount
                        << " execute_mysql=1"
                        << " mutated_db=1"
                        << " mark_applied=off"
                        << "\n";
            }
          } catch(const std::exception& error) {
            std::cerr << "[server_ai_dialog_intent_step281_runtime_storage_timeout_failed]"
                      << " stored=0"
                      << " error=" << error.what()
                      << " mark_applied=off"
                      << "\n";
          }
        }
      }
      if(activeOpt.aiDialogIntentLateObserverResumeRuntimeSend && activeOpt.aiDialogIntentStep273PersistenceRuntimeStorage && mysql) {
        Mmo::Server::AiDialogIntentDeliveryPersistenceTimeoutStorageRequest timeoutRequest;
        timeoutRequest.enabled = true;
        timeoutRequest.allowMysqlExecution = true;
        timeoutRequest.aiDatabaseName = activeOpt.worldInstanceAiDatabaseName;
        timeoutRequest.source = "step282_late_observer_replay_send";
        const auto timeoutSql = Mmo::Server::buildAiDialogIntentDeliveryPersistenceTimeoutStorageSql(timeoutRequest);
        if(timeoutSql.ready) {
          try {
            const auto rawStorage = runMysql(*mysql, timeoutSql.sql);
            const auto storageParts = splitMysqlLastRow(rawStorage);
            const auto timedOutCount = storageParts.empty() ? std::string("0") : storageParts[0];
            if(timedOutCount != "0") {
              std::cout << "[server_ai_dialog_intent_step282_late_observer_replay_timeout]"
                        << " timed_out_count=" << timedOutCount
                        << " execute_mysql=1"
                        << " mutated_db=1"
                        << " mark_applied=off"
                        << "\n";
            }
          } catch(const std::exception& error) {
            std::cerr << "[server_ai_dialog_intent_step282_late_observer_replay_timeout_failed]"
                      << " stored=0"
                      << " error=" << error.what()
                      << " mark_applied=off"
                      << "\n";
          }
        }
      }
      if(activeOpt.aiDialogIntentConversationSessionProbe) {
        for(const auto& expired : conversationSessions.expirePending(nowMs)) {
          std::cout << "[server_conversation_observer_timeout]"
                    << " conversation_id=" << expired.conversationId
                    << " session_uuid=" << expired.sessionUuid
                    << " character=" << expired.characterKey
                    << " action_id=" << expired.actionId
                    << " ack_key=" << expired.ackKey
                    << " packet_sequence=" << expired.packetSequence
                    << " deadline_ms=" << expired.deadlineMs
                    << " session_status=" << Mmo::Server::conversationSessionStatusName(expired.sessionStatus)
                    << " retry=deferred"
                    << " db_conversation_storage=off"
                    << "\n";
        }
      }
    };

    std::cout << "listening udp://" << opt.bind << " binary_protocol=v1\n";
    while(gRunning.load(std::memory_order_relaxed)) {
      scanOutboundDeliveryTimeouts();
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

      const std::string_view datagram(buffer.data(), n);
      if(auto gameplayAck = Mmo::Net::decodeClientGameplayAckPacket(datagram); gameplayAck.ok()) {
        clientRoutes.upsert(gameplayAck.ack.sessionUuid, remote, gameplayAck.ack.characterKey);
        if(gameplayAck.ack.status == Mmo::Net::ClientGameplayAckStatus::Ack)
          ++gameplayAcks;
        else
          ++gameplayNacks;

        const bool acked = gameplayAck.ack.status == Mmo::Net::ClientGameplayAckStatus::Ack;
        const auto receipt = outboundGameplayDelivery.recordReceipt(gameplayAck.ack.actionId,
                                                                    gameplayAck.ack.ackKey,
                                                                    acked,
                                                                    gameplayAck.ack.reason,
                                                                    gameplayAck.ack.message);
        if(activeOpt.aiDialogIntentConversationSessionProbe) {
          const auto conversationReceipt = conversationSessions.recordReceipt(gameplayAck.ack.actionId,
                                                                             gameplayAck.ack.ackKey,
                                                                             acked,
                                                                             gameplayAck.ack.reason,
                                                                             gameplayAck.ack.message,
                                                                             monotonicNowMs());
          std::cout << "[server_conversation_observer_ack_received]"
                    << " conversation_id=" << conversationReceipt.conversationId
                    << " session_uuid=" << conversationReceipt.sessionUuid
                    << " action_id=" << gameplayAck.ack.actionId
                    << " ack_key=" << gameplayAck.ack.ackKey
                    << " receipt=" << Mmo::Server::conversationReceiptKindName(conversationReceipt.kind)
                    << " previous_observer_status=" << Mmo::Server::conversationObserverStatusName(conversationReceipt.previousObserverStatus)
                    << " current_observer_status=" << Mmo::Server::conversationObserverStatusName(conversationReceipt.currentObserverStatus)
                    << " previous_session_status=" << Mmo::Server::conversationSessionStatusName(conversationReceipt.previousSessionStatus)
                    << " current_session_status=" << Mmo::Server::conversationSessionStatusName(conversationReceipt.currentSessionStatus)
                    << " reason=" << conversationReceipt.reason
                    << " db_conversation_storage=off"
                    << " mark_applied=off"
                    << "\n";
        }
        if(activeOpt.aiDialogIntentStep273PersistenceRuntimeStorage && mysql) {
          Mmo::Server::AiDialogIntentDeliveryPersistenceReceiptStorageRequest receiptRequest;
          receiptRequest.enabled = true;
          receiptRequest.allowMysqlExecution = true;
          receiptRequest.aiDatabaseName = activeOpt.worldInstanceAiDatabaseName;
          receiptRequest.source = isStep282LateObserverReplayActionId(gameplayAck.ack.actionId)
              ? "step282_late_observer_replay_send"
              : "step281_runtime_storage";
          receiptRequest.actionId = gameplayAck.ack.actionId;
          receiptRequest.ackKey = gameplayAck.ack.ackKey;
          receiptRequest.receiptKind = acked ? "acked" : "nacked";
          receiptRequest.sessionUuid = gameplayAck.ack.sessionUuid;
          receiptRequest.characterKey = gameplayAck.ack.characterKey;
          receiptRequest.clientObservationStatus = "none";
          receiptRequest.reason = gameplayAck.ack.reason;
          receiptRequest.messageText = gameplayAck.ack.message;
          receiptRequest.flags = gameplayAck.ack.flags;
          const auto receiptSql = Mmo::Server::buildAiDialogIntentDeliveryPersistenceReceiptStorageSql(receiptRequest);
          if(receiptSql.ready) {
            try {
              const auto rawStorage = runMysql(*mysql, receiptSql.sql);
              const auto storageParts = splitMysqlLastRow(rawStorage);
              std::cout << "[server_ai_dialog_intent_step281_runtime_storage_receipt]"
                        << " action_id=" << gameplayAck.ack.actionId
                        << " ack_key=" << gameplayAck.ack.ackKey
                        << " session_uuid=" << gameplayAck.ack.sessionUuid
                        << " stored=1"
                        << " receipt_kind=" << (storageParts.size() > 1 ? storageParts[1] : std::string())
                        << " delivery_status=" << (storageParts.size() > 2 ? storageParts[2] : std::string())
                        << " execute_mysql=1"
                        << " mutated_db=1"
                        << " mark_applied=" << (activeOpt.aiDialogIntentStep273DurableMarkAppliedGate ? "gate" : "off")
                        << "\n";
              tryRunAiDialogIntentDurableMarkAppliedGate(*mysql,
                                                          activeOpt,
                                                          gameplayAck.ack.actionId,
                                                          gameplayAck.ack.ackKey,
                                                          "client_ack_receipt",
                                                          receiptRequest.source);
            } catch(const std::exception& error) {
              std::cerr << "[server_ai_dialog_intent_step281_runtime_storage_receipt_failed]"
                        << " action_id=" << gameplayAck.ack.actionId
                        << " ack_key=" << gameplayAck.ack.ackKey
                        << " session_uuid=" << gameplayAck.ack.sessionUuid
                        << " stored=0"
                        << " error=" << error.what()
                        << " mark_applied=off"
                        << "\n";
            }
          } else {
            std::cerr << "[server_ai_dialog_intent_step281_runtime_storage_receipt_skipped]"
                      << " action_id=" << gameplayAck.ack.actionId
                      << " ack_key=" << gameplayAck.ack.ackKey
                      << " status=" << Mmo::Server::aiDialogIntentDeliveryPersistenceRuntimeStorageStatusName(receiptSql.status)
                      << " reason=" << receiptSql.reason
                      << " mark_applied=off"
                      << "\n";
          }
        }

        const auto deliveryStats = outboundGameplayDelivery.stats();
        std::cout << "[client_gameplay_ack_received]"
                  << " status=" << gameplayAckStatusName(gameplayAck.ack.status)
                  << " session_uuid=" << gameplayAck.ack.sessionUuid
                  << " character=" << gameplayAck.ack.characterKey
                  << " action_id=" << gameplayAck.ack.actionId
                  << " ack_key=" << gameplayAck.ack.ackKey
                  << " reason=" << gameplayAck.ack.reason
                  << " terminal_receipt=" << Mmo::Server::outboundGameplayReceiptKindName(receipt.kind)
                  << " previous_delivery_status=" << Mmo::Server::outboundGameplayDeliveryStatusName(receipt.previousStatus)
                  << " current_delivery_status=" << Mmo::Server::outboundGameplayDeliveryStatusName(receipt.currentStatus)
                  << " delivery_pending=" << deliveryStats.pending
                  << " delivery_acked=" << deliveryStats.acked
                  << " delivery_nacked=" << deliveryStats.nacked
                  << " delivery_timed_out=" << deliveryStats.timedOut
                  << " remote=" << remote
                  << " db_terminal_storage=" << (activeOpt.aiDialogIntentStep273PersistenceRuntimeStorage ? "step281" : "off")
                  << " mark_applied=off"
                  << "\n";
        continue;
      }

      if(auto observation = Mmo::Net::decodeClientGameplayObservationPacket(datagram); observation.ok()) {
        clientRoutes.upsert(observation.observation.sessionUuid, remote, observation.observation.characterKey);
        const bool observedOnMainThread = observation.observation.status == Mmo::Net::ClientGameplayObservationStatus::Observed;
        const bool uiApplied = (observation.observation.flags & Mmo::Net::ClientGameplayObservationUiApplied) != 0;
        const bool audioApplied = (observation.observation.flags & Mmo::Net::ClientGameplayObservationAudioApplied) != 0;

        if(activeOpt.aiDialogIntentStep273PersistenceRuntimeStorage && mysql) {
          Mmo::Server::AiDialogIntentDeliveryPersistenceReceiptStorageRequest receiptRequest;
          receiptRequest.enabled = true;
          receiptRequest.allowMysqlExecution = true;
          receiptRequest.aiDatabaseName = activeOpt.worldInstanceAiDatabaseName;
          receiptRequest.source = isStep282LateObserverReplayActionId(observation.observation.actionId)
              ? "step282_late_observer_replay_send"
              : "step281_runtime_storage";
          receiptRequest.actionId = observation.observation.actionId;
          receiptRequest.ackKey = observation.observation.ackKey;
          receiptRequest.receiptKind = observedOnMainThread ? "observed" : "skipped";
          receiptRequest.sessionUuid = observation.observation.sessionUuid;
          receiptRequest.characterKey = observation.observation.characterKey;
          receiptRequest.clientObservationStatus = observedOnMainThread ? "observed" : "skipped";
          receiptRequest.reason = observation.observation.reason;
          receiptRequest.messageText = observation.observation.message;
          receiptRequest.flags = observation.observation.flags;
          receiptRequest.uiApplied = uiApplied;
          receiptRequest.audioApplied = audioApplied;
          const auto receiptSql = Mmo::Server::buildAiDialogIntentDeliveryPersistenceReceiptStorageSql(receiptRequest);
          if(receiptSql.ready) {
            try {
              const auto rawStorage = runMysql(*mysql, receiptSql.sql);
              const auto storageParts = splitMysqlLastRow(rawStorage);
              std::cout << "[server_ai_dialog_intent_step281_runtime_storage_observation]"
                        << " action_id=" << observation.observation.actionId
                        << " ack_key=" << observation.observation.ackKey
                        << " session_uuid=" << observation.observation.sessionUuid
                        << " stored=1"
                        << " receipt_kind=" << (storageParts.size() > 1 ? storageParts[1] : std::string())
                        << " delivery_status=" << (storageParts.size() > 2 ? storageParts[2] : std::string())
                        << " execute_mysql=1"
                        << " mutated_db=1"
                        << " ui_applied=" << (uiApplied ? 1 : 0)
                        << " audio_applied=" << (audioApplied ? 1 : 0)
                        << " mark_applied=" << (activeOpt.aiDialogIntentStep273DurableMarkAppliedGate ? "gate" : "off")
                        << "\n";
              tryRunAiDialogIntentDurableMarkAppliedGate(*mysql,
                                                          activeOpt,
                                                          observation.observation.actionId,
                                                          observation.observation.ackKey,
                                                          "client_observation_receipt",
                                                          receiptRequest.source);
            } catch(const std::exception& error) {
              std::cerr << "[server_ai_dialog_intent_step281_runtime_storage_observation_failed]"
                        << " action_id=" << observation.observation.actionId
                        << " ack_key=" << observation.observation.ackKey
                        << " session_uuid=" << observation.observation.sessionUuid
                        << " stored=0"
                        << " error=" << error.what()
                        << " mark_applied=off"
                        << "\n";
            }
          }
        }

        std::cout << "[client_gameplay_observation_received]"
                  << " status=" << gameplayObservationStatusName(observation.observation.status)
                  << " session_uuid=" << observation.observation.sessionUuid
                  << " character=" << observation.observation.characterKey
                  << " action_id=" << observation.observation.actionId
                  << " ack_key=" << observation.observation.ackKey
                  << " reason=" << observation.observation.reason
                  << " flags=" << observation.observation.flags
                  << " ui_applied=" << (uiApplied ? 1 : 0)
                  << " audio_applied=" << (audioApplied ? 1 : 0)
                  << " remote=" << remote
                  << " observation_probe=" << (activeOpt.aiDialogIntentObservationReceiptProbe ? "on" : "off")
                  << " db_observation_storage=" << (activeOpt.aiDialogIntentStep273PersistenceRuntimeStorage ? "step281" : "off")
                  << " mark_applied=off"
                  << "\n";

        if(activeOpt.aiDialogIntentConversationSessionProbe && activeOpt.aiDialogIntentObservationReceiptProbe) {
          const auto result = conversationSessions.recordObservation(observation.observation.actionId,
                                                                    observation.observation.ackKey,
                                                                    observedOnMainThread,
                                                                    uiApplied,
                                                                    audioApplied,
                                                                    observation.observation.reason,
                                                                    observation.observation.message,
                                                                    monotonicNowMs());
          const auto stats = conversationSessions.stats();
          std::cout << "[server_conversation_observer_main_thread_receipt]"
                    << " conversation_id=" << result.conversationId
                    << " session_uuid=" << result.sessionUuid
                    << " action_id=" << observation.observation.actionId
                    << " ack_key=" << observation.observation.ackKey
                    << " receipt=" << Mmo::Server::conversationObservationReceiptKindName(result.kind)
                    << " previous_observation_status=" << Mmo::Server::conversationObservationStatusName(result.previousObservationStatus)
                    << " current_observation_status=" << Mmo::Server::conversationObservationStatusName(result.currentObservationStatus)
                    << " transport_observer_status=" << Mmo::Server::conversationObserverStatusName(result.transportObserverStatus)
                    << " session_status=" << Mmo::Server::conversationSessionStatusName(result.sessionStatus)
                    << " ui_applied=" << (result.uiApplied ? 1 : 0)
                    << " audio_applied=" << (result.audioApplied ? 1 : 0)
                    << " reason=" << result.reason
                    << " observation_receipts=" << stats.observationReceipts
                    << " observed_receipts=" << stats.observedReceipts
                    << " skipped_observation_receipts=" << stats.skippedObservationReceipts
                    << " duplicate_observation_receipts=" << stats.duplicateObservationReceipts
                    << " db_observation_storage=off"
                    << " mark_applied=off"
                    << "\n";
        }
        continue;
      }

      const auto decoded = Mmo::Net::decodeClientActionPacket(datagram);
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
      ClientRoutePosition routePosition;
      if(const auto pos = movementToPosition(packet.payloadJson)) {
        routePosition = {true, pos->x, pos->y, pos->z};
      }
      const std::string routeCharacterKey = jsonStringField(packet.payloadJson, "character_key").value_or(activeOpt.characterKey);
      const std::string routeWorldName = jsonStringField(packet.payloadJson, "world").value_or(activeOpt.worldName);
      clientRoutes.upsert(sessionUuid,
                          remote,
                          routeCharacterKey,
                          routeWorldName,
                          routePosition,
                          monotonicNowMs());
      logConversationLateObserverResumePlans(socket,
                                             conversationSessions,
                                             clientRoutes,
                                             outboundGameplayDelivery,
                                             conversationResumeSendGate,
                                             conversationResumeDeliveryRegistrationBoundary,
                                             conversationResumeDispatchEnvelope,
                                             conversationResumeMutationGuard,
                                             conversationResumeSendFailureDeadLetterGuard,
                                             conversationResumeCommitPreflight,
                                             mysql ? &*mysql : nullptr,
                                             activeOpt,
                                             sessionUuid,
                                             routeCharacterKey,
                                             routeWorldName,
                                             packet,
                                             nextDialogIntentSequence);
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
            clientRoutes.upsert(sessionUuid, remote, characterKey, worldName, routePosition, monotonicNowMs());
            readiness = readBootstrapReadinessWithFallback(*mysql, characterKey, worldName, sessionUuid, worldName);
            packetReady = readiness.ready;
            printBootstrapAck(packet, characterKey, worldName, readiness, true);
            if(packetReady) {
              try {
                bootstrapSnapshotJson = buildBootstrapSnapshotJson(*mysql, sessionUuid, characterKey, worldName, readiness, true, opt.requireDbSaveCheckpointRestore);
                if(worldContentCache)
                  appendWorldInstanceContentCacheSnapshotField(bootstrapSnapshotJson, *worldContentCache, worldName);
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

      clientRoutes.upsert(sessionUuid, remote);
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
            if(worldContentCache)
              appendWorldInstanceContentCacheSnapshotField(liveWorldSnapshotJson, *worldContentCache, worldName);
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
            if(worldContentCache)
              appendWorldInstanceContentCacheSnapshotField(liveWorldSnapshotJson, *worldContentCache, worldName);
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

      if(mysql && opt.enqueueOutbox && !direct.handled && isFailOpenNpcObservationAction(packet.kind)) {
        direct.handled = true;
        direct.accepted = true;
        direct.ready = true;
        packetAccepted = true;
        packetReady = true;
        std::cerr << "[outbox_observation_ignored]"
                  << " action=" << actionName
                  << " target=" << packet.targetKey
                  << " reason=content_authority_db_contract_pending"
                  << "\n";
      } else if(mysql && opt.enqueueOutbox && !direct.handled && (!isBootstrap || opt.forwardBootstrapOutbox)) {
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
        if(isFailOpenNpcObservationAction(packet.kind)) {
          direct.handled = true;
          direct.accepted = true;
          direct.ready = true;
          packetAccepted = true;
          packetReady = true;
          std::cerr << "[direct_db_observation_unhandled_accepted]"
                    << " action=" << actionName
                    << " target=" << packet.targetKey
                    << " payload=" << packet.payloadJson
                    << "\n";
        } else {
          packetAccepted = false;
          ++unhandled;
          diagnosticSeverity = 2;
          diagnosticReason = "direct_db_unhandled";
          diagnosticMessage = "semantic action has no direct C++ DB handler and outbox fallback is disabled";
          std::cerr << "[direct_db_unhandled] action=" << actionName << "\n";
        }
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
      if(opt.enableAiDialogIntentSend && isBootstrap && packetAccepted && packetReady && !sessionUuid.empty()) {
        if(!dialogIntentBootstrapSentSessions.contains(sessionUuid)) {
          try {
            const auto sentRecipients = sendNpcDialogIntentProbe(socket, clientRoutes, outboundGameplayDelivery, activeOpt.aiDialogIntentConversationSessionProbe ? &conversationSessions : nullptr, mysql ? &*mysql : nullptr, activeOpt, sessionUuid, packet, nextDialogIntentSequence);
            if(sentRecipients != 0) {
              dialogIntentSent += sentRecipients;
              dialogIntentBootstrapSentSessions.insert(sessionUuid);
            } else {
              ++dialogIntentFailed;
            }
          } catch(const std::exception& exc) {
            ++dialogIntentFailed;
            std::cerr << "[server_npc_dialog_intent_failed]"
                      << " session_uuid=" << sessionUuid
                      << " error=" << exc.what()
                      << " db_terminal_storage=off"
                      << "\n";
            sendServerDiagnostic(socket, remote, packet, 1, actionName, "server_npc_dialog_intent_failed", exc.what());
          }
        }
      }
      printPacketProgress(logState, accepted, received, invalid, duplicate, enqueued, directDb, unhandled, failed,
                          actionName, packetAccepted, !diagnosticReason.empty(), snapshotSent, isMovement, isWeaponState);
    }

    scanOutboundDeliveryTimeouts();
    const auto deliveryStats = outboundGameplayDelivery.stats();
    const auto conversationStats = conversationSessions.stats();
    const auto resumeSendGateStats = conversationResumeSendGate.stats();
    const auto resumeDeliveryRegistrationStats = conversationResumeDeliveryRegistrationBoundary.stats();
    const auto resumeDispatchEnvelopeStats = conversationResumeDispatchEnvelope.stats();
    const auto resumeMutationGuardStats = conversationResumeMutationGuard.stats();
    const auto resumeSendFailureDeadLetterGuardStats = conversationResumeSendFailureDeadLetterGuard.stats();
    const auto resumeCommitPreflightStats = conversationResumeCommitPreflight.stats();
    std::cout << "summary:\n"
              << "received=" << received << "\n"
              << "accepted=" << accepted << "\n"
              << "invalid=" << invalid << "\n"
              << "duplicate=" << duplicate << "\n"
              << "enqueued=" << enqueued << "\n"
              << "direct_db=" << directDb << "\n"
              << "unhandled=" << unhandled << "\n"
              << "failed=" << failed << "\n"
              << "client_gameplay_ack=" << gameplayAcks << "\n"
              << "client_gameplay_nack=" << gameplayNacks << "\n"
              << "dialog_intent_sent=" << dialogIntentSent << "\n"
              << "dialog_intent_failed=" << dialogIntentFailed << "\n"
              << "outbound_delivery_pending=" << deliveryStats.pending << "\n"
              << "outbound_delivery_acked=" << deliveryStats.acked << "\n"
              << "outbound_delivery_nacked=" << deliveryStats.nacked << "\n"
              << "outbound_delivery_timed_out=" << deliveryStats.timedOut << "\n"
              << "outbound_delivery_unknown_acks=" << deliveryStats.unknownAcks << "\n"
              << "outbound_delivery_duplicate_receipts=" << deliveryStats.duplicateReceipts << "\n"
              << "outbound_delivery_conflicting_receipts=" << deliveryStats.conflictingReceipts << "\n"
              << "outbound_delivery_late_after_timeout_receipts=" << deliveryStats.lateAfterTimeoutReceipts << "\n"
              << "conversation_session_probe=" << (activeOpt.aiDialogIntentConversationSessionProbe ? "on" : "off") << "\n"
              << "conversation_sessions=" << conversationStats.sessions << "\n"
              << "conversation_observers=" << conversationStats.observers << "\n"
              << "conversation_observers_pending=" << conversationStats.pendingObservers << "\n"
              << "conversation_observers_acked=" << conversationStats.ackedObservers << "\n"
              << "conversation_observers_nacked=" << conversationStats.nackedObservers << "\n"
              << "conversation_observers_timed_out=" << conversationStats.timedOutObservers << "\n"
              << "conversation_late_observer_resume_plan=" << (activeOpt.aiDialogIntentLateObserverResumePlan ? "on" : "off") << "\n"
              << "conversation_late_observer_resume_packet_boundary=" << (activeOpt.aiDialogIntentLateObserverResumePacketBoundary ? "on" : "off") << "\n"
              << "conversation_late_observer_resume_send_gate=" << (activeOpt.aiDialogIntentLateObserverResumeSendGate ? "on" : "off") << "\n"
              << "conversation_resume_send_gate_requests=" << resumeSendGateStats.requests << "\n"
              << "conversation_resume_send_gate_eligible=" << resumeSendGateStats.eligible << "\n"
              << "conversation_resume_send_gate_disabled=" << resumeSendGateStats.disabled << "\n"
              << "conversation_resume_send_gate_packet_not_buildable=" << resumeSendGateStats.packetNotBuildable << "\n"
              << "conversation_resume_send_gate_missing_identity=" << resumeSendGateStats.missingIdentity << "\n"
              << "conversation_resume_send_gate_missing_route=" << resumeSendGateStats.missingRoute << "\n"
              << "conversation_resume_send_gate_route_mismatch=" << resumeSendGateStats.routeMismatch << "\n"
              << "conversation_resume_send_gate_packet_too_large=" << resumeSendGateStats.packetTooLarge << "\n"
              << "conversation_resume_send_gate_duplicate_action_id=" << resumeSendGateStats.duplicateActionId << "\n"
              << "conversation_resume_send_gate_duplicate_ack_key=" << resumeSendGateStats.duplicateAckKey << "\n"
              << "conversation_late_observer_resume_delivery_registration_boundary=" << (activeOpt.aiDialogIntentLateObserverResumeDeliveryRegistrationBoundary ? "on" : "off") << "\n"
              << "conversation_resume_delivery_registration_requests=" << resumeDeliveryRegistrationStats.requests << "\n"
              << "conversation_resume_delivery_registration_eligible=" << resumeDeliveryRegistrationStats.eligible << "\n"
              << "conversation_resume_delivery_registration_disabled=" << resumeDeliveryRegistrationStats.disabled << "\n"
              << "conversation_resume_delivery_registration_gate_not_eligible=" << resumeDeliveryRegistrationStats.sendGateNotEligible << "\n"
              << "conversation_resume_delivery_registration_packet_not_buildable=" << resumeDeliveryRegistrationStats.packetNotBuildable << "\n"
              << "conversation_resume_delivery_registration_missing_runtime=" << resumeDeliveryRegistrationStats.missingRuntime << "\n"
              << "conversation_resume_delivery_registration_missing_identity=" << resumeDeliveryRegistrationStats.missingIdentity << "\n"
              << "conversation_resume_delivery_registration_gate_packet_mismatch=" << resumeDeliveryRegistrationStats.gatePacketMismatch << "\n"
              << "conversation_resume_delivery_registration_missing_conversation_session=" << resumeDeliveryRegistrationStats.missingConversationSession << "\n"
              << "conversation_resume_delivery_registration_already_delivery=" << resumeDeliveryRegistrationStats.alreadyDelivery << "\n"
              << "conversation_resume_delivery_registration_already_observer_ack_key=" << resumeDeliveryRegistrationStats.alreadyObserverAckKey << "\n"
              << "conversation_resume_delivery_registration_already_observer_session=" << resumeDeliveryRegistrationStats.alreadyObserverSession << "\n"
              << "conversation_resume_delivery_registration_deadline_overflow=" << resumeDeliveryRegistrationStats.deadlineOverflow << "\n"
              << "conversation_resume_delivery_registration_duplicate_action_id=" << resumeDeliveryRegistrationStats.duplicateBoundaryActionId << "\n"
              << "conversation_resume_delivery_registration_duplicate_ack_key=" << resumeDeliveryRegistrationStats.duplicateBoundaryAckKey << "\n"
              << "conversation_late_observer_resume_dispatch_envelope=" << (activeOpt.aiDialogIntentLateObserverResumeDispatchEnvelope ? "on" : "off") << "\n"
              << "conversation_resume_dispatch_envelope_requests=" << resumeDispatchEnvelopeStats.requests << "\n"
              << "conversation_resume_dispatch_envelope_eligible=" << resumeDispatchEnvelopeStats.eligible << "\n"
              << "conversation_resume_dispatch_envelope_disabled=" << resumeDispatchEnvelopeStats.disabled << "\n"
              << "conversation_resume_dispatch_envelope_registration_not_eligible=" << resumeDispatchEnvelopeStats.registrationNotEligible << "\n"
              << "conversation_resume_dispatch_envelope_packet_not_buildable=" << resumeDispatchEnvelopeStats.packetNotBuildable << "\n"
              << "conversation_resume_dispatch_envelope_identity_mismatch=" << resumeDispatchEnvelopeStats.identityMismatch << "\n"
              << "conversation_resume_dispatch_envelope_missing_identity=" << resumeDispatchEnvelopeStats.missingIdentity << "\n"
              << "conversation_resume_dispatch_envelope_missing_endpoint=" << resumeDispatchEnvelopeStats.missingEndpoint << "\n"
              << "conversation_resume_dispatch_envelope_encode_failed=" << resumeDispatchEnvelopeStats.encodeFailed << "\n"
              << "conversation_resume_dispatch_envelope_datagram_too_large=" << resumeDispatchEnvelopeStats.datagramTooLarge << "\n"
              << "conversation_resume_dispatch_envelope_duplicate_action_id=" << resumeDispatchEnvelopeStats.duplicateEnvelopeActionId << "\n"
              << "conversation_resume_dispatch_envelope_duplicate_ack_key=" << resumeDispatchEnvelopeStats.duplicateEnvelopeAckKey << "\n"
              << "conversation_late_observer_resume_mutation_guard=" << (activeOpt.aiDialogIntentLateObserverResumeMutationGuard ? "on" : "off") << "\n"
              << "conversation_resume_mutation_guard_requests=" << resumeMutationGuardStats.requests << "\n"
              << "conversation_resume_mutation_guard_ready=" << resumeMutationGuardStats.ready << "\n"
              << "conversation_resume_mutation_guard_disabled=" << resumeMutationGuardStats.disabled << "\n"
              << "conversation_resume_mutation_guard_dispatch_not_eligible=" << resumeMutationGuardStats.dispatchNotEligible << "\n"
              << "conversation_resume_mutation_guard_registration_not_eligible=" << resumeMutationGuardStats.registrationNotEligible << "\n"
              << "conversation_resume_mutation_guard_identity_mismatch=" << resumeMutationGuardStats.identityMismatch << "\n"
              << "conversation_resume_mutation_guard_missing_runtime=" << resumeMutationGuardStats.missingRuntime << "\n"
              << "conversation_resume_mutation_guard_missing_identity=" << resumeMutationGuardStats.missingIdentity << "\n"
              << "conversation_resume_mutation_guard_missing_endpoint=" << resumeMutationGuardStats.missingEndpoint << "\n"
              << "conversation_resume_mutation_guard_missing_datagram=" << resumeMutationGuardStats.missingDatagram << "\n"
              << "conversation_resume_mutation_guard_datagram_too_large=" << resumeMutationGuardStats.datagramTooLarge << "\n"
              << "conversation_resume_mutation_guard_missing_conversation_session=" << resumeMutationGuardStats.missingConversationSession << "\n"
              << "conversation_resume_mutation_guard_already_delivery=" << resumeMutationGuardStats.alreadyDelivery << "\n"
              << "conversation_resume_mutation_guard_already_observer_ack_key=" << resumeMutationGuardStats.alreadyObserverAckKey << "\n"
              << "conversation_resume_mutation_guard_already_observer_session=" << resumeMutationGuardStats.alreadyObserverSession << "\n"
              << "conversation_resume_mutation_guard_registration_attempt_mismatch=" << resumeMutationGuardStats.registrationAttemptMismatch << "\n"
              << "conversation_resume_mutation_guard_registration_observer_mismatch=" << resumeMutationGuardStats.registrationObserverMismatch << "\n"
              << "conversation_resume_mutation_guard_duplicate_action_id=" << resumeMutationGuardStats.duplicateGuardActionId << "\n"
              << "conversation_resume_mutation_guard_duplicate_ack_key=" << resumeMutationGuardStats.duplicateGuardAckKey << "\n"
              << "conversation_late_observer_resume_send_failure_dead_letter_guard=" << (activeOpt.aiDialogIntentLateObserverResumeSendFailureDeadLetterGuard ? "on" : "off") << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_requests=" << resumeSendFailureDeadLetterGuardStats.requests << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_ready=" << resumeSendFailureDeadLetterGuardStats.ready << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_disabled=" << resumeSendFailureDeadLetterGuardStats.disabled << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_mutation_guard_not_ready=" << resumeSendFailureDeadLetterGuardStats.mutationGuardNotReady << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_missing_runtime=" << resumeSendFailureDeadLetterGuardStats.missingRuntime << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_missing_identity=" << resumeSendFailureDeadLetterGuardStats.missingIdentity << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_missing_endpoint=" << resumeSendFailureDeadLetterGuardStats.missingEndpoint << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_missing_datagram=" << resumeSendFailureDeadLetterGuardStats.missingDatagram << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_missing_failure_reason=" << resumeSendFailureDeadLetterGuardStats.missingFailureReason << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_missing_conversation_session=" << resumeSendFailureDeadLetterGuardStats.missingConversationSession << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_already_delivery=" << resumeSendFailureDeadLetterGuardStats.alreadyDelivery << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_already_observer_ack_key=" << resumeSendFailureDeadLetterGuardStats.alreadyObserverAckKey << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_already_observer_session=" << resumeSendFailureDeadLetterGuardStats.alreadyObserverSession << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_duplicate_action_id=" << resumeSendFailureDeadLetterGuardStats.duplicateDeadLetterActionId << "\n"
              << "conversation_resume_send_failure_dead_letter_guard_duplicate_ack_key=" << resumeSendFailureDeadLetterGuardStats.duplicateDeadLetterAckKey << "\n"
              << "conversation_late_observer_resume_commit_preflight=" << (activeOpt.aiDialogIntentLateObserverResumeCommitPreflight ? "on" : "off") << "\n"
              << "conversation_resume_commit_preflight_requests=" << resumeCommitPreflightStats.requests << "\n"
              << "conversation_resume_commit_preflight_ready=" << resumeCommitPreflightStats.ready << "\n"
              << "conversation_resume_commit_preflight_disabled=" << resumeCommitPreflightStats.disabled << "\n"
              << "conversation_resume_commit_preflight_mutation_guard_not_ready=" << resumeCommitPreflightStats.mutationGuardNotReady << "\n"
              << "conversation_resume_commit_preflight_send_failure_guard_not_ready=" << resumeCommitPreflightStats.sendFailureGuardNotReady << "\n"
              << "conversation_resume_commit_preflight_identity_mismatch=" << resumeCommitPreflightStats.identityMismatch << "\n"
              << "conversation_resume_commit_preflight_missing_runtime=" << resumeCommitPreflightStats.missingRuntime << "\n"
              << "conversation_resume_commit_preflight_missing_identity=" << resumeCommitPreflightStats.missingIdentity << "\n"
              << "conversation_resume_commit_preflight_missing_endpoint=" << resumeCommitPreflightStats.missingEndpoint << "\n"
              << "conversation_resume_commit_preflight_missing_datagram=" << resumeCommitPreflightStats.missingDatagram << "\n"
              << "conversation_resume_commit_preflight_missing_conversation_session=" << resumeCommitPreflightStats.missingConversationSession << "\n"
              << "conversation_resume_commit_preflight_already_delivery=" << resumeCommitPreflightStats.alreadyDelivery << "\n"
              << "conversation_resume_commit_preflight_already_observer_ack_key=" << resumeCommitPreflightStats.alreadyObserverAckKey << "\n"
              << "conversation_resume_commit_preflight_already_observer_session=" << resumeCommitPreflightStats.alreadyObserverSession << "\n"
              << "conversation_resume_commit_preflight_missing_commit_plan_step=" << resumeCommitPreflightStats.missingCommitPlanStep << "\n"
              << "conversation_resume_commit_preflight_duplicate_action_id=" << resumeCommitPreflightStats.duplicatePreflightActionId << "\n"
              << "conversation_resume_commit_preflight_duplicate_ack_key=" << resumeCommitPreflightStats.duplicatePreflightAckKey << "\n"
              << "conversation_observation_receipt_probe=" << (activeOpt.aiDialogIntentObservationReceiptProbe ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_persistence_preview=" << (activeOpt.aiDialogIntentStep273PersistencePreview ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_persistence_execution_boundary=" << (activeOpt.aiDialogIntentStep273PersistenceExecutionBoundary ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_persistence_executor_adapter=" << (activeOpt.aiDialogIntentStep273PersistenceExecutorAdapter ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_persistence_dry_run_report_gate=" << (activeOpt.aiDialogIntentStep273PersistenceDryRunReportGate ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_persistence_outcome_policy=" << (activeOpt.aiDialogIntentStep273PersistenceOutcomePolicy ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_persistence_outcome_observability=" << (activeOpt.aiDialogIntentStep273PersistenceOutcomeObservability ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_persistence_activation_preflight=" << (activeOpt.aiDialogIntentStep273PersistenceActivationPreflight ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_persistence_runtime_storage=" << (activeOpt.aiDialogIntentStep273PersistenceRuntimeStorage ? "on" : "off") << "\n"
              << "ai_dialog_intent_step273_durable_mark_applied_gate=" << (activeOpt.aiDialogIntentStep273DurableMarkAppliedGate ? "on" : "off") << "\n"
              << "ai_dialog_intent_late_observer_resume_runtime_send=" << (activeOpt.aiDialogIntentLateObserverResumeRuntimeSend ? "on" : "off") << "\n"
              << "conversation_late_resume_plans=" << conversationStats.lateResumePlans << "\n"
              << "conversation_late_resume_skipped_already_observer=" << conversationStats.lateResumeSkippedAlreadyObserver << "\n"
              << "conversation_late_resume_skipped_already_planned=" << conversationStats.lateResumeSkippedAlreadyPlanned << "\n"
              << "conversation_late_resume_skipped_world_mismatch=" << conversationStats.lateResumeSkippedWorldMismatch << "\n"
              << "conversation_late_resume_skipped_expired=" << conversationStats.lateResumeSkippedExpired << "\n"
              << "conversation_observation_receipts=" << conversationStats.observationReceipts << "\n"
              << "conversation_observed_receipts=" << conversationStats.observedReceipts << "\n"
              << "conversation_skipped_observation_receipts=" << conversationStats.skippedObservationReceipts << "\n"
              << "conversation_duplicate_observation_receipts=" << conversationStats.duplicateObservationReceipts << "\n"
              << "conversation_unknown_observation_receipts=" << conversationStats.unknownObservationReceipts << "\n"
              << "conversation_conflicting_observation_receipts=" << conversationStats.conflictingObservationReceipts << "\n"
              << "conversation_invalid_observation_receipts=" << conversationStats.invalidObservationReceipts << "\n"
              << "conversation_late_observation_receipts=" << conversationStats.lateObservationReceipts << "\n"
              << "conversation_unknown_receipts=" << conversationStats.unknownReceipts << "\n"
              << "conversation_duplicate_receipts=" << conversationStats.duplicateReceipts << "\n"
              << "conversation_conflicting_receipts=" << conversationStats.conflictingReceipts << "\n"
              << "conversation_late_receipts=" << conversationStats.lateReceipts << "\n";
    return invalid == 0 && failed == 0 && unhandled == 0 ? 0 : 2;
  } catch(const std::exception& exc) {
    std::cerr << "ERROR: " << exc.what() << "\n";
    return 2;
  }
}




















