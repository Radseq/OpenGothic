#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Mmo::RestoreSnapshot {

struct Item final {
  std::size_t symbolIndex = std::size_t(-1);
  std::size_t count = 0;
  bool        equipped = false;
  };

struct CharacterPosition final {
  bool   present = false;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double yaw = 0.0;
  std::size_t serverTick = 0;
  };

struct WorldClock final {
  bool         present = false;
  std::string worldName;
  std::int64_t currentWorldTimeMs = 0;
  std::size_t currentTick = 0;
  };

struct KnownDialog final {
  std::size_t npcSymbol = std::size_t(-1);
  std::size_t infoSymbol = std::size_t(-1);
  bool        known = true;
  };

struct Quest final {
  std::string              name;
  std::uint8_t             section = 0;
  std::uint8_t             status = 1;
  std::vector<std::string> entries;
  };

struct ScriptInt final {
  std::size_t  symbolIndex = std::size_t(-1);
  std::uint16_t valueIndex = 0;
  std::int32_t  value = 0;
  };

struct WorldEntityDelta final {
  std::string entityKey;
  std::string entityKind;
  std::string lifecycleState;
  std::size_t persistentId = std::size_t(-1);
  std::size_t symbolIndex = std::size_t(-1);

  [[nodiscard]] bool isRemovedWorldItem() const noexcept {
    return entityKind == "item" &&
           (lifecycleState == "removed" || lifecycleState == "consumed" ||
            lifecycleState == "archived" || lifecycleState == "disabled" || lifecycleState == "dead");
  }
  };

struct WorldInventoryItem final {
  std::string ownerKey;
  std::string lifecycleState;
  std::size_t persistentId = std::size_t(-1);
  std::size_t symbolIndex = std::size_t(-1);
  std::size_t amount = 1;
  bool        hasPosition = false;
  double      x = 0.0;
  double      y = 0.0;
  double      z = 0.0;

  [[nodiscard]] bool isActiveWorldItem() const noexcept {
    return !ownerKey.empty() && symbolIndex != std::size_t(-1) && hasPosition &&
           (lifecycleState.empty() || lifecycleState == "active");
  }
  };

struct InteractiveState final {
  std::string entityKey;
  std::string lifecycleState;
  std::size_t slotId = std::size_t(-1);
  bool        hasStateId = false;
  std::int32_t stateId = 0;
  bool        hasLocked = false;
  bool        locked = false;
  bool        hasCracked = false;
  bool        cracked = false;
  };

struct NpcLifecycleState final {
  std::string entityKey;
  std::string entityKind;
  std::string lifecycleState;
  std::size_t persistentId = std::size_t(-1);
  std::size_t symbolIndex = std::size_t(-1);
  bool        hasHealthCurrent = false;
  std::int32_t healthCurrent = 0;
  bool        hasHealthMax = false;
  std::int32_t healthMax = 0;
  bool        hasPosition = false;
  double      x = 0.0;
  double      y = 0.0;
  double      z = 0.0;

  [[nodiscard]] bool hasStableIdentity() const noexcept {
    return persistentId != std::size_t(-1) || symbolIndex != std::size_t(-1);
  }

  [[nodiscard]] bool isLifecycleRelevant() const noexcept {
    return lifecycleState == "dead" || lifecycleState == "removed" || lifecycleState == "disabled" ||
           lifecycleState == "archived" || (hasHealthCurrent && hasHealthMax && healthCurrent < healthMax);
  }
  };

struct NearbyNpc final {
  std::string entityKey;
  std::string entityKind;
  std::string lifecycleState;
  std::string scriptName;
  std::string displayName;
  std::string currentWaypoint;
  std::string routineWaypoint;
  std::string aiStateName;
  std::size_t symbolIndex = std::size_t(-1);
  std::size_t scriptId = std::size_t(-1);
  bool        hasHealthCurrent = false;
  std::int32_t healthCurrent = 0;
  bool        hasHealthMax = false;
  std::int32_t healthMax = 0;
  bool        hasPosition = false;
  double      x = 0.0;
  double      y = 0.0;
  double      z = 0.0;
  bool        hasDistance = false;
  double      distance = 0.0;
  };

struct NearbyNpcKnownDialog final {
  std::string npcKey;
  std::string infoKey;
  std::string availabilityState;
  std::string nearbyEntityKey;
  bool        known = true;
  bool        permanent = false;
  bool        hasDistance = false;
  double      nearbyDistance = 0.0;
  };

struct NearbyWaypoint final {
  std::string waypointKey;
  std::string waypointName;
  std::string kindKey;
  bool        hasPosition = false;
  double      x = 0.0;
  double      y = 0.0;
  double      z = 0.0;
  bool        hasDistance = false;
  double      distance = 0.0;
  };

struct MoverState final {
  std::string moverKey;
  std::string stateAfterName;
  std::int32_t stateAfter = 0;
  bool        hasFrameIndex = false;
  std::int32_t frameIndex = 0;
  bool        hasTargetFrameIndex = false;
  std::int32_t targetFrameIndex = 0;
  std::size_t rowVersion = 0;
  std::size_t lastServerTick = 0;
  };

struct ServerCheckpointManifest final {
  bool        present = false;
  std::string manifestUuid;
  std::string manifestKey;
  std::string saveSlotKey;
  std::string nativeSavePath;
  std::string displayName;
  std::string clientWorldName;
  bool        nativeSavePresent = false;
  std::string checkpointKind;
  std::string reason;
  std::size_t serverTick = 0;
  std::size_t latestCheckpointTick = 0;
  std::size_t recentEventSeq = 0;
  std::size_t inventoryRows = 0;
  std::size_t equipmentRows = 0;
  std::size_t questRows = 0;
  std::size_t knownDialogRows = 0;
  std::size_t scriptStateRows = 0;
  std::size_t worldItemRows = 0;
  std::size_t worldInventoryRows = 0;
  std::size_t interactiveRows = 0;
  std::size_t npcLifecycleRows = 0;
  std::size_t moverRows = 0;
  std::size_t rowVersion = 0;
  };

struct RecentAction final {
  std::string eventType;
  std::string eventClass;
  std::string entityKey;
  std::string subjectKey;
  std::string occurredAt;
  std::size_t eventSeq = 0;
  std::size_t serverTick = 0;
  };

struct CharacterStats final {
  static constexpr std::int32_t Missing = std::numeric_limits<std::int32_t>::min();

  bool         present = false;
  std::int32_t level = Missing;
  std::int32_t experience = Missing;
  std::int32_t experienceNext = Missing;
  std::int32_t learningPoints = Missing;
  std::int32_t healthCurrent = Missing;
  std::int32_t healthMax = Missing;
  std::int32_t manaCurrent = Missing;
  std::int32_t manaMax = Missing;
  std::int32_t strength = Missing;
  std::int32_t dexterity = Missing;
  std::int32_t guild = Missing;
  std::int32_t trueGuild = Missing;
  };

struct Result final {
  bool              ok = false;
  bool              fileRead = false;
  bool              stepMatches = false;
  bool              characterMatches = false;
  bool              sessionMatches = false;
  bool              integrityOk = false;
  bool              bootstrapSchemaMatches = false;
  bool              serverReady = false;
  std::string       source;
  std::string       snapshotSource;
  std::string       dbSaveCheckpointManifestUuid;
  std::string       worldName;
  std::size_t              inventoryCount = 0;
  std::size_t              equipmentCount = 0;
  std::size_t              knownDialogCount = 0;
  std::size_t              questCount = 0;
  std::size_t              scriptIntCount = 0;
  std::size_t              worldItemDeltaCount = 0;
  std::size_t              interactiveStateCount = 0;
  std::size_t              npcLifecycleStateCount = 0;
  std::size_t              nearbyNpcCount = 0;
  std::size_t              nearbyNpcKnownDialogCount = 0;
  std::size_t              nearbyWaypointCount = 0;
  std::size_t              recentActionCount = 0;
  std::size_t              moverStateCount = 0;
  bool                     serverCheckpointManifestPresent = false;
  bool                     nearbyNpcWindowPresent = false;
  double                   nearbyNpcRadius = 0.0;
  bool                     nearbyWaypointWindowPresent = false;
  double                   nearbyWaypointRadius = 0.0;
  bool                     activeWorldItemWindowPresent = false;
  double                   activeWorldItemRadius = 0.0;
  bool                     scriptStateTruncated = false;
  std::vector<Item>        items;
  std::vector<KnownDialog> knownDialogs;
  std::vector<Quest>       quests;
  std::vector<ScriptInt>   scriptInts;
  std::vector<WorldEntityDelta> worldEntityDeltas;
  std::vector<WorldInventoryItem> activeWorldItems;
  std::vector<InteractiveState> interactiveStates;
  std::vector<NpcLifecycleState> npcLifecycleStates;
  std::vector<NearbyNpc> nearbyNpcs;
  std::vector<NearbyNpcKnownDialog> nearbyNpcKnownDialogs;
  std::vector<NearbyWaypoint> nearbyWaypoints;
  std::vector<MoverState> moverStates;
  std::vector<RecentAction> recentActions;
  ServerCheckpointManifest serverCheckpointManifest;
  WorldClock              worldClock;
  CharacterPosition        position;
  CharacterStats           stats;
  std::string              message;
  };

namespace Detail {

constexpr std::uintmax_t MaxSnapshotBytes = 8u * 1024u * 1024u;
constexpr std::string_view ExpectedStep = "63_guarded_inventory_equipment_restore_snapshot";

inline void fail(Result& result, std::string message) {
  result.ok = false;
  result.message = std::move(message);
}

inline std::string readWholeFile(std::string_view path, Result& result) {
  const std::filesystem::path fsPath{std::string(path)};
  std::error_code ec;
  const auto size = std::filesystem::file_size(fsPath, ec);
  if(ec) {
    fail(result, "snapshot file is not readable");
    return {};
    }
  if(size > MaxSnapshotBytes) {
    fail(result, "snapshot file is too large");
    return {};
    }

  std::ifstream in(fsPath, std::ios::binary);
  if(!in) {
    fail(result, "snapshot file could not be opened");
    return {};
    }

  std::string text;
  text.resize(static_cast<std::size_t>(size));
  if(!text.empty())
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
  if(!in && !text.empty()) {
    fail(result, "snapshot file read failed");
    return {};
    }

  result.fileRead = true;
  return text;
}

inline std::size_t skipWs(std::string_view text, std::size_t pos) noexcept {
  while(pos < text.size()) {
    const char c = text[pos];
    if(c != ' ' && c != '\n' && c != '\r' && c != '\t')
      break;
    ++pos;
    }
  return pos;
}

inline std::size_t findKey(std::string_view text, std::string_view key, std::size_t from = 0) {
  const std::string needle = "\"" + std::string(key) + "\"";
  return text.find(needle, from);
}

inline std::string_view valueSpanForKey(std::string_view text, std::string_view key) {
  const auto keyPos = findKey(text, key);
  if(keyPos == std::string_view::npos)
    return {};
  auto pos = text.find(':', keyPos);
  if(pos == std::string_view::npos)
    return {};
  pos = skipWs(text, pos + 1);
  if(pos >= text.size())
    return {};
  return text.substr(pos);
}

inline int hexNibble(char c) noexcept {
  if(c >= '0' && c <= '9')
    return c - '0';
  if(c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if(c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

inline bool readJsonHex4(std::string_view text, std::size_t pos, std::uint32_t& out) noexcept {
  if(pos + 4 > text.size())
    return false;
  std::uint32_t value = 0;
  for(std::size_t i = 0; i < 4; ++i) {
    const int nibble = hexNibble(text[pos + i]);
    if(nibble < 0)
      return false;
    value = (value << 4) | static_cast<std::uint32_t>(nibble);
    }
  out = value;
  return true;
}

inline void appendUtf8(std::string& out, std::uint32_t cp) {
  if(cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
    return;
    }
  if(cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6u)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    return;
    }
  if(cp >= 0xD800u && cp <= 0xDFFFu)
    cp = 0xFFFDu;
  if(cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12u)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    return;
    }
  if(cp > 0x10FFFFu)
    cp = 0xFFFDu;
  out.push_back(static_cast<char>(0xF0u | (cp >> 18u)));
  out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
  out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
  out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
}

inline bool appendJsonEscapedChar(std::string& out, std::string_view value, std::size_t& i) {
  if(i + 1 >= value.size())
    return false;
  const char esc = value[++i];
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
      std::uint32_t cp = 0;
      if(!readJsonHex4(value, i + 1, cp))
        return false;
      i += 4;
      if(cp >= 0xD800u && cp <= 0xDBFFu && i + 6 < value.size() && value[i + 1] == '\\' && value[i + 2] == 'u') {
        std::uint32_t lo = 0;
        if(readJsonHex4(value, i + 3, lo) && lo >= 0xDC00u && lo <= 0xDFFFu) {
          cp = 0x10000u + (((cp - 0xD800u) << 10u) | (lo - 0xDC00u));
          i += 6;
          }
        }
      appendUtf8(out, cp);
      return true;
      }
    default:
      out.push_back(esc);
      return true;
    }
}

inline std::string stringValueForKey(std::string_view text, std::string_view key) {
  auto value = valueSpanForKey(text, key);
  if(value.empty() || value.front() != '"')
    return {};

  std::string out;
  for(std::size_t i = 1; i < value.size(); ++i) {
    const char c = value[i];
    if(c == '"')
      return out;
    if(c == '\\') {
      if(!appendJsonEscapedChar(out, value, i))
        return {};
      continue;
      }
    out.push_back(c);
    }
  return {};
}

inline bool quotedValueEquals(std::string_view text, std::string_view key, std::string_view value) {
  return stringValueForKey(text, key) == value;
}

inline bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

inline std::size_t unsignedFromToken(std::string_view text) noexcept {
  if(text.empty())
    return std::size_t(-1);
  std::size_t value = 0;
  bool any = false;
  for(char c : text) {
    if(c < '0' || c > '9')
      return std::size_t(-1);
    any = true;
    value = value * 10 + std::size_t(c - '0');
    }
  return any ? value : std::size_t(-1);
}

inline std::size_t symbolFromKey(std::string_view key, std::string_view prefix) noexcept {
  if(startsWith(key, prefix))
    return unsignedFromToken(key.substr(prefix.size()));
  if(const auto colon = key.rfind(':'); colon != std::string_view::npos && colon + 1 < key.size()) {
    const auto suffix = unsignedFromToken(key.substr(colon + 1));
    if(suffix != std::size_t(-1))
      return suffix;
    }
  return unsignedFromToken(key);
}



inline bool boolValueEquals(std::string_view text, std::string_view key, bool value) {
  auto span = valueSpanForKey(text, key);
  if(span.empty())
    return false;
  constexpr std::string_view TrueValue = "true";
  constexpr std::string_view FalseValue = "false";
  const auto expected = value ? TrueValue : FalseValue;
  return span.substr(0, expected.size()) == expected;
}

inline bool boolValueForKey(std::string_view text, std::string_view key, bool fallback) {
  auto span = valueSpanForKey(text, key);
  if(span.empty())
    return fallback;
  constexpr std::string_view TrueValue = "true";
  constexpr std::string_view FalseValue = "false";
  if(span.substr(0, TrueValue.size()) == TrueValue)
    return true;
  if(span.substr(0, FalseValue.size()) == FalseValue)
    return false;
  return fallback;
}

inline std::size_t unsignedValueForKey(std::string_view text, std::string_view key) {
  auto span = valueSpanForKey(text, key);
  if(span.empty())
    return 0;
  span = span.substr(0, span.find_first_of(",}] \r\n\t"));
  std::size_t value = 0;
  bool any = false;
  for(char c : span) {
    if(c < '0' || c > '9')
      break;
    any = true;
    value = value * 10 + std::size_t(c - '0');
    }
  return any ? value : 0;
}

inline bool intValueForKey(std::string_view text, std::string_view key, std::int32_t& out) {
  auto span = valueSpanForKey(text, key);
  if(span.empty())
    return false;
  span = span.substr(0, span.find_first_of(",}] \r\n\t"));
  if(span.empty() || span == "null")
    return false;

  bool negative = false;
  std::size_t pos = 0;
  if(span.front() == '-') {
    negative = true;
    pos = 1;
    }

  std::int64_t value = 0;
  bool any = false;
  for(; pos < span.size(); ++pos) {
    const char c = span[pos];
    if(c < '0' || c > '9')
      return false;
    any = true;
    value = value * 10 + std::int64_t(c - '0');
    if(value > std::int64_t(std::numeric_limits<std::int32_t>::max()) + 1)
      return false;
    }
  if(!any)
    return false;
  if(negative)
    value = -value;
  if(value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
    return false;
  out = static_cast<std::int32_t>(value);
  return true;
}

inline bool int64ValueForKey(std::string_view text, std::string_view key, std::int64_t& out) {
  auto span = valueSpanForKey(text, key);
  if(span.empty())
    return false;
  span = span.substr(0, span.find_first_of(",}] \r\n\t"));
  if(span.empty() || span == "null")
    return false;

  bool negative = false;
  std::size_t pos = 0;
  if(span.front() == '-') {
    negative = true;
    pos = 1;
    }

  std::int64_t value = 0;
  bool any = false;
  for(; pos < span.size(); ++pos) {
    const char c = span[pos];
    if(c < '0' || c > '9')
      return false;
    any = true;
    const auto digit = std::int64_t(c - '0');
    if(value > (std::numeric_limits<std::int64_t>::max() - digit) / 10)
      return false;
    value = value * 10 + digit;
    }
  if(!any)
    return false;
  if(negative)
    value = -value;
  out = value;
  return true;
}

inline std::size_t sizeValueForKey(std::string_view text, std::string_view key) {
  std::int32_t value = 0;
  if(!intValueForKey(text, key, value) || value < 0)
    return std::size_t(-1);
  return static_cast<std::size_t>(value);
}

inline bool realValueForKey(std::string_view text, std::string_view key, double& out) {
  auto span = valueSpanForKey(text, key);
  if(span.empty())
    return false;
  span = span.substr(0, span.find_first_of(",}] \r\n\t"));
  if(span.empty() || span == "null")
    return false;

  std::string token(span);
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(token.c_str(), &end);
  if(end == token.c_str() || errno == ERANGE)
    return false;
  out = value;
  return true;
}

inline std::string_view bracketedSpanForKey(std::string_view text, std::string_view key, char open, char close) {
  const auto keyPos = findKey(text, key);
  if(keyPos == std::string_view::npos)
    return {};

  auto pos = text.find(open, keyPos);
  if(pos == std::string_view::npos)
    return {};

  bool inString = false;
  bool escaped = false;
  std::size_t depth = 0;
  for(std::size_t i = pos; i < text.size(); ++i) {
    const char c = text[i];
    if(inString) {
      if(escaped) {
        escaped = false;
        continue;
        }
      if(c == '\\') {
        escaped = true;
        continue;
        }
      if(c == '"')
        inString = false;
      continue;
      }
    if(c == '"') {
      inString = true;
      continue;
      }
    if(c == open) {
      ++depth;
      continue;
      }
    if(c == close) {
      if(depth == 0)
        return {};
      --depth;
      if(depth == 0)
        return text.substr(pos, i - pos + 1);
      }
    }
  return {};
}

inline std::string_view objectForKey(std::string_view text, std::string_view key) {
  return bracketedSpanForKey(text, key, '{', '}');
}

inline std::string_view arrayForKey(std::string_view text, std::string_view key) {
  return bracketedSpanForKey(text, key, '[', ']');
}

inline CharacterPosition restorePosition(std::string_view characterObject) {
  CharacterPosition out;
  const auto position = objectForKey(characterObject, "position");
  if(position.empty())
    return out;

  double x = 0.0, y = 0.0, z = 0.0;
  if(!realValueForKey(position, "x", x) || !realValueForKey(position, "y", y) || !realValueForKey(position, "z", z))
    return out;

  out.present = true;
  out.x = x;
  out.y = y;
  out.z = z;
  (void)realValueForKey(position, "yaw", out.yaw);
  out.serverTick = unsignedValueForKey(position, "server_tick");
  return out;
}

inline WorldClock restoreWorldClock(std::string_view clockObject) {
  WorldClock out;
  if(clockObject.empty())
    return out;

  std::int64_t currentWorldTimeMs = 0;
  if(!int64ValueForKey(clockObject, "current_world_time_ms", currentWorldTimeMs) || currentWorldTimeMs < 0)
    return out;

  out.present = true;
  out.worldName = stringValueForKey(clockObject, "world_name");
  out.currentWorldTimeMs = currentWorldTimeMs;
  out.currentTick = unsignedValueForKey(clockObject, "current_tick");
  return out;
}

inline CharacterStats restoreStats(std::string_view characterObject) {
  CharacterStats out;
  const auto stats = objectForKey(characterObject, "stats");
  if(stats.empty())
    return out;

  out.present = true;
  (void)intValueForKey(stats, "level", out.level);
  (void)intValueForKey(stats, "experience", out.experience);
  (void)intValueForKey(stats, "experience_next", out.experienceNext);
  (void)intValueForKey(stats, "learning_points", out.learningPoints);
  (void)intValueForKey(stats, "health_current", out.healthCurrent);
  (void)intValueForKey(stats, "health_max", out.healthMax);
  (void)intValueForKey(stats, "mana_current", out.manaCurrent);
  (void)intValueForKey(stats, "mana_max", out.manaMax);
  (void)intValueForKey(stats, "strength", out.strength);
  (void)intValueForKey(stats, "dexterity", out.dexterity);
  (void)intValueForKey(stats, "guild", out.guild);
  (void)intValueForKey(stats, "true_guild", out.trueGuild);
  return out;
}

inline std::vector<std::string_view> objectSpansInArray(std::string_view array) {
  std::vector<std::string_view> out;
  bool inString = false;
  bool escaped = false;
  std::size_t depth = 0;
  std::size_t objectBegin = std::string_view::npos;

  for(std::size_t i = 0; i < array.size(); ++i) {
    const char c = array[i];
    if(inString) {
      if(escaped) {
        escaped = false;
        continue;
        }
      if(c == '\\') {
        escaped = true;
        continue;
        }
      if(c == '"')
        inString = false;
      continue;
      }
    if(c == '"') {
      inString = true;
      continue;
      }
    if(c == '{') {
      if(depth == 0)
        objectBegin = i;
      ++depth;
      continue;
      }
    if(c == '}') {
      if(depth == 0)
        continue;
      --depth;
      if(depth == 0 && objectBegin != std::string_view::npos) {
        out.push_back(array.substr(objectBegin, i - objectBegin + 1));
        objectBegin = std::string_view::npos;
        }
      }
    }
  return out;
}


inline bool boolValueMaybeForKey(std::string_view text, std::string_view key, bool& out) {
  auto span = valueSpanForKey(text, key);
  if(!span.empty() && span.front() == '"') {
    const auto quoted = stringValueForKey(text, key);
    if(quoted == "true" || quoted == "1") {
      out = true;
      return true;
      }
    if(quoted == "false" || quoted == "0") {
      out = false;
      return true;
      }
    return false;
    }
  if(span.empty())
    return false;
  if(span.substr(0, 4) == "true" || span.substr(0, 1) == "1") {
    out = true;
    return true;
    }
  if(span.substr(0, 5) == "false" || span.substr(0, 1) == "0") {
    out = false;
    return true;
    }
  return false;
}

inline bool parseWorldItemKey(std::string_view key, std::size_t& persistentId, std::size_t& symbolIndex) noexcept {
  constexpr std::string_view HookPrefix = "world-item:";
  if(startsWith(key, HookPrefix)) {
    const auto pid = key.find(":pid:");
    const auto sym = key.find(":sym:");
    if(pid != std::string_view::npos && sym != std::string_view::npos && pid < sym) {
      const auto parsedPid = unsignedFromToken(key.substr(pid + 5, sym - (pid + 5)));
      const auto parsedSym = unsignedFromToken(key.substr(sym + 5));
      if(parsedPid != std::size_t(-1))
        persistentId = parsedPid;
      if(parsedSym != std::size_t(-1))
        symbolIndex = parsedSym;
      return persistentId != std::size_t(-1) || symbolIndex != std::size_t(-1);
      }
    }

  constexpr std::string_view DbPrefix = "world_item:";
  if(!startsWith(key, DbPrefix))
    return false;
  auto rest = key.substr(DbPrefix.size());
  const auto first = rest.find(':');
  if(first == std::string_view::npos)
    return false;
  const auto second = rest.find(':', first + 1);
  if(second == std::string_view::npos)
    return false;
  const auto third = rest.find(':', second + 1);
  const auto symEnd = third == std::string_view::npos ? rest.size() : third;
  const auto parsedPid = unsignedFromToken(rest.substr(first + 1, second - first - 1));
  const auto parsedSym = unsignedFromToken(rest.substr(second + 1, symEnd - second - 1));
  if(parsedPid != std::size_t(-1))
    persistentId = parsedPid;
  if(parsedSym != std::size_t(-1))
    symbolIndex = parsedSym;
  return persistentId != std::size_t(-1) || symbolIndex != std::size_t(-1);
}

inline bool parseNpcKey(std::string_view key, std::size_t& persistentId, std::size_t& symbolIndex) noexcept {
  constexpr std::string_view Prefix = "npc:";
  if(!startsWith(key, Prefix))
    return false;

  const auto pid = key.find(":pid:");
  const auto sym = key.find(":sym:");
  if(pid != std::string_view::npos && sym != std::string_view::npos && pid < sym) {
    const auto parsedPid = unsignedFromToken(key.substr(pid + 5, sym - (pid + 5)));
    const auto parsedSym = unsignedFromToken(key.substr(sym + 5));
    if(parsedPid != std::size_t(-1))
      persistentId = parsedPid;
    if(parsedSym != std::size_t(-1))
      symbolIndex = parsedSym;
    return persistentId != std::size_t(-1) || symbolIndex != std::size_t(-1);
    }

  auto rest = key.substr(Prefix.size());
  const auto first = rest.find(':');
  if(first == std::string_view::npos)
    return false;
  const auto second = rest.find(':', first + 1);
  if(second == std::string_view::npos)
    return false;
  const auto third = rest.find(':', second + 1);
  const auto symEnd = third == std::string_view::npos ? rest.size() : third;
  const auto parsedPid = unsignedFromToken(rest.substr(first + 1, second - first - 1));
  const auto parsedSym = unsignedFromToken(rest.substr(second + 1, symEnd - second - 1));
  if(parsedPid != std::size_t(-1))
    persistentId = parsedPid;
  if(parsedSym != std::size_t(-1))
    symbolIndex = parsedSym;
  return persistentId != std::size_t(-1) || symbolIndex != std::size_t(-1);
}

inline std::size_t parseMobsiSlotFromKey(std::string_view key) noexcept {
  constexpr std::string_view Prefix = "mobsi:";
  if(!startsWith(key, Prefix))
    return std::size_t(-1);
  auto rest = key.substr(Prefix.size());
  const auto first = rest.find(':');
  if(first == std::string_view::npos)
    return std::size_t(-1);
  const auto second = rest.find(':', first + 1);
  if(second == std::string_view::npos)
    return std::size_t(-1);
  return unsignedFromToken(rest.substr(first + 1, second - first - 1));
}

inline std::vector<WorldEntityDelta> restoreWorldEntityDeltas(std::string_view deltaArray) {
  std::vector<WorldEntityDelta> out;
  for(auto object : objectSpansInArray(deltaArray)) {
    WorldEntityDelta delta;
    delta.entityKey = stringValueForKey(object, "entity_key");
    delta.entityKind = stringValueForKey(object, "entity_kind");
    delta.lifecycleState = stringValueForKey(object, "lifecycle_state");
    delta.persistentId = sizeValueForKey(object, "persistent_id");
    delta.symbolIndex = sizeValueForKey(object, "symbol_index");

    if(!delta.entityKey.empty())
      (void)parseWorldItemKey(delta.entityKey, delta.persistentId, delta.symbolIndex);

    if(!delta.entityKey.empty() && !delta.entityKind.empty() && !delta.lifecycleState.empty())
      out.push_back(std::move(delta));
    }
  return out;
}

inline std::vector<WorldInventoryItem> restoreWorldInventoryItems(std::string_view inventoryArray) {
  std::vector<WorldInventoryItem> out;
  for(auto object : objectSpansInArray(inventoryArray)) {
    WorldInventoryItem item;
    item.ownerKey = stringValueForKey(object, "owner_key");
    item.lifecycleState = stringValueForKey(object, "lifecycle_state");
    item.persistentId = sizeValueForKey(object, "persistent_id");
    item.symbolIndex = sizeValueForKey(object, "symbol_index");
    item.amount = unsignedValueForKey(object, "amount");
    if(item.amount == 0)
      item.amount = 1;

    if(!item.ownerKey.empty())
      (void)parseWorldItemKey(item.ownerKey, item.persistentId, item.symbolIndex);

    double x = 0.0, y = 0.0, z = 0.0;
    item.hasPosition = realValueForKey(object, "pos_x", x) &&
                       realValueForKey(object, "pos_y", y) &&
                       realValueForKey(object, "pos_z", z);
    if(item.hasPosition) {
      item.x = x;
      item.y = y;
      item.z = z;
      }

    if(item.isActiveWorldItem())
      out.push_back(std::move(item));
    }
  return out;
}

inline std::vector<InteractiveState> restoreInteractiveStates(std::string_view interactiveArray) {
  std::vector<InteractiveState> out;
  for(auto object : objectSpansInArray(interactiveArray)) {
    InteractiveState state;
    state.entityKey = stringValueForKey(object, "entity_key");
    state.lifecycleState = stringValueForKey(object, "lifecycle_state");
    state.slotId = sizeValueForKey(object, "slot_id");
    if(state.slotId == std::size_t(-1) && !state.entityKey.empty())
      state.slotId = parseMobsiSlotFromKey(state.entityKey);

    std::int32_t value = 0;
    state.hasStateId = intValueForKey(object, "state_id", value);
    if(state.hasStateId)
      state.stateId = value;

    bool b = false;
    state.hasLocked = boolValueMaybeForKey(object, "locked", b);
    if(state.hasLocked)
      state.locked = b;
    state.hasCracked = boolValueMaybeForKey(object, "cracked", b);
    if(state.hasCracked)
      state.cracked = b;

    const auto stateJson = objectForKey(object, "state_json");
    if(!stateJson.empty()) {
      if(!state.hasStateId && intValueForKey(stateJson, "state_id", value)) {
        state.hasStateId = true;
        state.stateId = value;
        }
      if(!state.hasLocked && boolValueMaybeForKey(stateJson, "locked", b)) {
        state.hasLocked = true;
        state.locked = b;
        }
      if(!state.hasCracked && boolValueMaybeForKey(stateJson, "cracked", b)) {
        state.hasCracked = true;
        state.cracked = b;
        }
      }

    if(state.slotId != std::size_t(-1) && (state.hasStateId || state.hasLocked || state.hasCracked))
      out.push_back(std::move(state));
    }
  return out;
}

inline std::vector<NpcLifecycleState> restoreNpcLifecycleStates(std::string_view npcArray) {
  std::vector<NpcLifecycleState> out;
  for(auto object : objectSpansInArray(npcArray)) {
    NpcLifecycleState state;
    state.entityKey = stringValueForKey(object, "entity_key");
    state.entityKind = stringValueForKey(object, "entity_kind");
    state.lifecycleState = stringValueForKey(object, "lifecycle_state");
    state.persistentId = sizeValueForKey(object, "persistent_id");
    state.symbolIndex = sizeValueForKey(object, "symbol_index");

    if(!state.entityKey.empty())
      (void)parseNpcKey(state.entityKey, state.persistentId, state.symbolIndex);

    std::int32_t value = 0;
    state.hasHealthCurrent = intValueForKey(object, "health_current", value);
    if(state.hasHealthCurrent)
      state.healthCurrent = value;
    state.hasHealthMax = intValueForKey(object, "health_max", value);
    if(state.hasHealthMax)
      state.healthMax = value;

    double x = 0.0, y = 0.0, z = 0.0;
    state.hasPosition = realValueForKey(object, "pos_x", x) &&
                        realValueForKey(object, "pos_y", y) &&
                        realValueForKey(object, "pos_z", z);
    if(state.hasPosition) {
      state.x = x;
      state.y = y;
      state.z = z;
      }

    if(state.hasStableIdentity() && state.isLifecycleRelevant())
      out.push_back(std::move(state));
    }
  return out;
}

inline std::vector<NearbyNpc> restoreNearbyNpcs(std::string_view npcArray) {
  std::vector<NearbyNpc> out;
  for(auto object : objectSpansInArray(npcArray)) {
    NearbyNpc npc;
    npc.entityKey = stringValueForKey(object, "entity_key");
    npc.entityKind = stringValueForKey(object, "entity_kind");
    npc.lifecycleState = stringValueForKey(object, "lifecycle_state");
    npc.scriptName = stringValueForKey(object, "script_name");
    npc.displayName = stringValueForKey(object, "display_name");
    npc.currentWaypoint = stringValueForKey(object, "current_waypoint");
    npc.routineWaypoint = stringValueForKey(object, "routine_waypoint");
    npc.aiStateName = stringValueForKey(object, "ai_state_name");
    npc.symbolIndex = sizeValueForKey(object, "symbol_index");
    npc.scriptId = sizeValueForKey(object, "script_id");

    std::int32_t value = 0;
    npc.hasHealthCurrent = intValueForKey(object, "health_current", value);
    if(npc.hasHealthCurrent)
      npc.healthCurrent = value;
    npc.hasHealthMax = intValueForKey(object, "health_max", value);
    if(npc.hasHealthMax)
      npc.healthMax = value;

    double real = 0.0;
    npc.hasDistance = realValueForKey(object, "distance", real);
    if(npc.hasDistance)
      npc.distance = real;

    double x = 0.0, y = 0.0, z = 0.0;
    npc.hasPosition = realValueForKey(object, "pos_x", x) &&
                      realValueForKey(object, "pos_y", y) &&
                      realValueForKey(object, "pos_z", z);
    if(npc.hasPosition) {
      npc.x = x;
      npc.y = y;
      npc.z = z;
      }

    if(!npc.entityKey.empty() && !npc.entityKind.empty())
      out.push_back(std::move(npc));
    }
  return out;
}

inline std::vector<NearbyNpcKnownDialog> restoreNearbyNpcKnownDialogs(std::string_view dialogArray) {
  std::vector<NearbyNpcKnownDialog> out;
  for(auto object : objectSpansInArray(dialogArray)) {
    NearbyNpcKnownDialog dialog;
    dialog.npcKey = stringValueForKey(object, "npc_key");
    dialog.infoKey = stringValueForKey(object, "info_key");
    dialog.availabilityState = stringValueForKey(object, "availability_state");
    dialog.nearbyEntityKey = stringValueForKey(object, "nearby_entity_key");
    dialog.known = boolValueForKey(object, "known", true);
    dialog.permanent = boolValueForKey(object, "permanent", false);

    double distance = 0.0;
    dialog.hasDistance = realValueForKey(object, "nearby_distance", distance);
    if(dialog.hasDistance)
      dialog.nearbyDistance = distance;

    if(!dialog.npcKey.empty() && !dialog.infoKey.empty())
      out.push_back(std::move(dialog));
    }
  return out;
}

inline std::vector<NearbyWaypoint> restoreNearbyWaypoints(std::string_view waypointArray) {
  std::vector<NearbyWaypoint> out;
  for(auto object : objectSpansInArray(waypointArray)) {
    NearbyWaypoint waypoint;
    waypoint.waypointKey = stringValueForKey(object, "waypoint_key");
    waypoint.waypointName = stringValueForKey(object, "waypoint_name");
    waypoint.kindKey = stringValueForKey(object, "kind_key");

    double distance = 0.0;
    waypoint.hasDistance = realValueForKey(object, "distance", distance);
    if(waypoint.hasDistance)
      waypoint.distance = distance;

    double x = 0.0, y = 0.0, z = 0.0;
    waypoint.hasPosition = realValueForKey(object, "pos_x", x) &&
                           realValueForKey(object, "pos_y", y) &&
                           realValueForKey(object, "pos_z", z);
    if(waypoint.hasPosition) {
      waypoint.x = x;
      waypoint.y = y;
      waypoint.z = z;
      }

    if(!waypoint.waypointKey.empty())
      out.push_back(std::move(waypoint));
    }
  return out;
}

inline std::vector<MoverState> restoreMoverStates(std::string_view moverArray) {
  std::vector<MoverState> out;
  for(auto object : objectSpansInArray(moverArray)) {
    MoverState state;
    state.moverKey = stringValueForKey(object, "mover_key");
    state.stateAfterName = stringValueForKey(object, "state_after_name");
    (void)intValueForKey(object, "state_after", state.stateAfter);
    state.hasFrameIndex = intValueForKey(object, "frame_index", state.frameIndex);
    state.hasTargetFrameIndex = intValueForKey(object, "target_frame_index", state.targetFrameIndex);
    state.rowVersion = unsignedValueForKey(object, "row_version");
    state.lastServerTick = unsignedValueForKey(object, "last_server_tick");
    if(!state.moverKey.empty())
      out.push_back(std::move(state));
    }
  return out;
}

inline ServerCheckpointManifest restoreServerCheckpointManifest(std::string_view manifestObject) {
  ServerCheckpointManifest out;
  if(manifestObject.empty())
    return out;
  out.manifestUuid = stringValueForKey(manifestObject, "manifest_uuid");
  out.manifestKey = stringValueForKey(manifestObject, "manifest_key");
  out.saveSlotKey = stringValueForKey(manifestObject, "save_slot_key");
  out.nativeSavePath = stringValueForKey(manifestObject, "native_save_path");
  out.displayName = stringValueForKey(manifestObject, "display_name");
  out.clientWorldName = stringValueForKey(manifestObject, "client_world_name");
  out.nativeSavePresent = boolValueForKey(manifestObject, "native_save_present", false);
  out.checkpointKind = stringValueForKey(manifestObject, "checkpoint_kind");
  out.reason = stringValueForKey(manifestObject, "reason");
  out.serverTick = unsignedValueForKey(manifestObject, "server_tick");
  out.latestCheckpointTick = unsignedValueForKey(manifestObject, "latest_checkpoint_tick");
  out.recentEventSeq = unsignedValueForKey(manifestObject, "recent_event_seq");
  out.inventoryRows = unsignedValueForKey(manifestObject, "inventory_rows");
  out.equipmentRows = unsignedValueForKey(manifestObject, "equipment_rows");
  out.questRows = unsignedValueForKey(manifestObject, "quest_rows");
  out.knownDialogRows = unsignedValueForKey(manifestObject, "known_dialog_rows");
  out.scriptStateRows = unsignedValueForKey(manifestObject, "script_state_rows");
  out.worldItemRows = unsignedValueForKey(manifestObject, "world_item_rows");
  out.worldInventoryRows = unsignedValueForKey(manifestObject, "world_inventory_rows");
  out.interactiveRows = unsignedValueForKey(manifestObject, "interactive_rows");
  out.npcLifecycleRows = unsignedValueForKey(manifestObject, "npc_lifecycle_rows");
  out.moverRows = unsignedValueForKey(manifestObject, "mover_rows");
  out.rowVersion = unsignedValueForKey(manifestObject, "row_version");
  out.present = !out.manifestUuid.empty() || !out.manifestKey.empty() || !out.saveSlotKey.empty() || out.recentEventSeq != 0;
  return out;
}

inline std::vector<RecentAction> restoreRecentActions(std::string_view eventArray) {
  std::vector<RecentAction> out;
  for(auto object : objectSpansInArray(eventArray)) {
    RecentAction action;
    action.eventType = stringValueForKey(object, "event_type");
    action.eventClass = stringValueForKey(object, "event_class");
    action.entityKey = stringValueForKey(object, "entity_key");
    action.subjectKey = stringValueForKey(object, "subject_key");
    action.occurredAt = stringValueForKey(object, "occurred_at");
    action.eventSeq = sizeValueForKey(object, "event_seq");
    action.serverTick = sizeValueForKey(object, "server_tick");
    if(!action.eventType.empty() || action.eventSeq != 0)
      out.push_back(std::move(action));
    }
  return out;
}

inline std::vector<Item> restoreItems(std::string_view inventoryArray, std::string_view equipmentArray) {
  std::unordered_set<std::string> equippedIds;
  for(auto object : objectSpansInArray(equipmentArray)) {
    auto uuid = stringValueForKey(object, "item_instance_uuid");
    if(!uuid.empty())
      equippedIds.insert(std::move(uuid));
    }

  std::vector<Item> out;
  for(auto object : objectSpansInArray(inventoryArray)) {
    const auto symbol = unsignedValueForKey(object, "symbol_index");
    if(symbol == 0)
      continue;

    auto amount = unsignedValueForKey(object, "amount");
    if(amount == 0)
      amount = unsignedValueForKey(object, "source_iterator_count");
    if(amount == 0)
      amount = unsignedValueForKey(object, "instance_quantity");
    if(amount == 0)
      amount = 1;

    const auto uuid = stringValueForKey(object, "item_instance_uuid");
    const bool equipped = !uuid.empty() && equippedIds.find(uuid) != equippedIds.end();

    Item* item = nullptr;
    for(auto& existing : out) {
      if(existing.symbolIndex == symbol) {
        item = &existing;
        break;
        }
      }
    if(item == nullptr) {
      out.push_back({symbol, 0, false});
      item = &out.back();
      }
    item->count += amount;
    item->equipped = item->equipped || equipped;
    }
  return out;
}

inline std::vector<std::string> stringValuesInArray(std::string_view array) {
  std::vector<std::string> out;
  bool inString = false;
  std::string value;

  for(std::size_t i = 0; i < array.size(); ++i) {
    const char c = array[i];
    if(!inString) {
      if(c == '"') {
        inString = true;
        value.clear();
        }
      continue;
      }

    if(c == '\\') {
      if(!appendJsonEscapedChar(value, array, i)) {
        inString = false;
        value.clear();
        }
      continue;
      }
    if(c == '"') {
      out.push_back(value);
      inString = false;
      continue;
      }
    value.push_back(c);
    }
  return out;
}

inline std::uint8_t questStatusFromText(std::string_view status) noexcept {
  if(status == "success")
    return 2;
  if(status == "failed")
    return 3;
  if(status == "obsolete")
    return 4;
  return 1;
}

inline std::uint8_t questSectionFromText(std::string_view section) noexcept {
  return section == "note" ? 1 : 0;
}

inline std::vector<KnownDialog> restoreKnownDialogs(std::string_view dialogsArray) {
  std::vector<KnownDialog> out;
  for(auto object : objectSpansInArray(dialogsArray)) {
    KnownDialog dialog;
    dialog.npcSymbol = sizeValueForKey(object, "npc_symbol");
    dialog.infoSymbol = sizeValueForKey(object, "info_symbol");
    if(dialog.npcSymbol == std::size_t(-1))
      dialog.npcSymbol = symbolFromKey(stringValueForKey(object, "npc_key"), "npc-symbol:");
    if(dialog.infoSymbol == std::size_t(-1))
      dialog.infoSymbol = symbolFromKey(stringValueForKey(object, "info_key"), "dialog-info:");
    dialog.known = boolValueForKey(object, "known", true);
    if(dialog.known && dialog.npcSymbol != std::size_t(-1) && dialog.infoSymbol != std::size_t(-1))
      out.push_back(dialog);
    }
  return out;
}


inline std::vector<ScriptInt> restoreScriptInts(std::string_view scriptArray) {
  std::vector<ScriptInt> out;
  for(auto object : objectSpansInArray(scriptArray)) {
    ScriptInt value;
    value.symbolIndex = sizeValueForKey(object, "symbol_index");

    std::int32_t index = 0;
    if(intValueForKey(object, "value_index", index) && index >= 0 && index <= std::numeric_limits<std::uint16_t>::max())
      value.valueIndex = static_cast<std::uint16_t>(index);
    else
      continue;

    if(value.symbolIndex == std::size_t(-1) || !intValueForKey(object, "value_int", value.value))
      continue;

    out.push_back(value);
    }
  return out;
}

inline std::vector<Quest> restoreQuests(std::string_view questsArray) {
  std::vector<Quest> out;
  for(auto object : objectSpansInArray(questsArray)) {
    Quest quest;
    quest.name = stringValueForKey(object, "quest_key");
    if(quest.name.empty())
      quest.name = stringValueForKey(object, "name");
    if(quest.name.empty())
      continue;

    quest.section = questSectionFromText(stringValueForKey(object, "section"));
    quest.status = questStatusFromText(stringValueForKey(object, "status"));
    const auto entriesArray = arrayForKey(object, "text_entries");
    if(!entriesArray.empty())
      quest.entries = stringValuesInArray(entriesArray);
    if(quest.entries.empty()) {
      auto single = stringValueForKey(object, "entry_text");
      if(!single.empty())
        quest.entries.push_back(std::move(single));
      }
    out.push_back(std::move(quest));
    }
  return out;
}

} // namespace Detail

inline Result loadAndValidateBootstrapSnapshot(std::string_view path,
                                                   std::string_view expectedCharacterKey) {
  Result result;
  auto text = Detail::readWholeFile(path, result);
  if(!result.fileRead)
    return result;

  result.bootstrapSchemaMatches = Detail::quotedValueEquals(text, "schema", "mmo_bootstrap_snapshot_v1");
  result.source = Detail::stringValueForKey(text, "source");
  result.snapshotSource = Detail::stringValueForKey(text, "snapshot_source");
  result.dbSaveCheckpointManifestUuid = Detail::stringValueForKey(text, "db_save_checkpoint_manifest_uuid");
  result.worldName = Detail::stringValueForKey(text, "world_name");
  result.characterMatches = Detail::quotedValueEquals(text, "character_key", expectedCharacterKey);
  result.serverReady = Detail::boolValueEquals(text, "ready", true);

  const auto character = Detail::objectForKey(text, "character");
  if(result.worldName.empty())
    result.worldName = Detail::stringValueForKey(character, "world_name");
  result.position = Detail::restorePosition(character);
  result.stats = Detail::restoreStats(character);

  const auto inventory = Detail::arrayForKey(text, "inventory");
  const auto equipment = Detail::arrayForKey(text, "equipment");
  const auto inventoryObjects = Detail::objectSpansInArray(inventory);
  const auto equipmentObjects = Detail::objectSpansInArray(equipment);
  result.inventoryCount = inventoryObjects.size();
  result.equipmentCount = equipmentObjects.size();
  result.items = Detail::restoreItems(inventory, equipment);

  const auto knownDialogs = Detail::arrayForKey(text, "known_dialogs");
  const auto quests = Detail::arrayForKey(text, "quests");
  auto scriptState = Detail::arrayForKey(text, "script_state");
  if(scriptState.empty())
    scriptState = Detail::arrayForKey(text, "script_state_sample");
  result.knownDialogs = Detail::restoreKnownDialogs(knownDialogs);
  result.quests = Detail::restoreQuests(quests);
  result.scriptInts = Detail::restoreScriptInts(scriptState);

  auto worldItemDeltas = Detail::arrayForKey(text, "world_item_deltas");
  if(worldItemDeltas.empty())
    worldItemDeltas = Detail::arrayForKey(text, "world_entity_delta_sample");
  auto activeWorldItems = Detail::arrayForKey(text, "active_world_items");
  if(activeWorldItems.empty())
    activeWorldItems = Detail::arrayForKey(text, "world_inventory_sample");
  auto interactiveState = Detail::arrayForKey(text, "interactive_state");
  if(interactiveState.empty())
    interactiveState = Detail::arrayForKey(text, "interactive_sample");
  auto npcLifecycleState = Detail::arrayForKey(text, "npc_lifecycle_state");
  const auto nearbyNpcs = Detail::arrayForKey(text, "nearby_npcs");
  const auto nearbyNpcKnownDialogs = Detail::arrayForKey(text, "nearby_npc_known_dialogs");
  const auto nearbyWaypoints = Detail::arrayForKey(text, "nearby_waypoints");
  auto recentActions = Detail::arrayForKey(text, "recent_actions");
  if(recentActions.empty())
    recentActions = Detail::arrayForKey(text, "recent_events_sample");
  const auto moverState = Detail::arrayForKey(text, "mover_state");
  const auto worldClock = Detail::objectForKey(text, "world_clock");
  const auto serverCheckpointManifest = Detail::objectForKey(text, "server_checkpoint_manifest");
  result.worldEntityDeltas = Detail::restoreWorldEntityDeltas(worldItemDeltas);
  result.activeWorldItems = Detail::restoreWorldInventoryItems(activeWorldItems);
  result.interactiveStates = Detail::restoreInteractiveStates(interactiveState);
  result.npcLifecycleStates = Detail::restoreNpcLifecycleStates(npcLifecycleState);
  result.nearbyNpcs = Detail::restoreNearbyNpcs(nearbyNpcs);
  result.nearbyNpcKnownDialogs = Detail::restoreNearbyNpcKnownDialogs(nearbyNpcKnownDialogs);
  result.nearbyWaypoints = Detail::restoreNearbyWaypoints(nearbyWaypoints);
  result.moverStates = Detail::restoreMoverStates(moverState);
  result.worldClock = Detail::restoreWorldClock(worldClock);
  result.recentActions = Detail::restoreRecentActions(recentActions);
  result.serverCheckpointManifest = Detail::restoreServerCheckpointManifest(serverCheckpointManifest);
  if(result.worldName.empty())
    result.worldName = result.serverCheckpointManifest.clientWorldName;

  result.knownDialogCount = Detail::objectSpansInArray(knownDialogs).size();
  result.questCount = Detail::objectSpansInArray(quests).size();
  result.scriptIntCount = Detail::objectSpansInArray(scriptState).size();
  result.worldItemDeltaCount = Detail::objectSpansInArray(worldItemDeltas).size();
  result.interactiveStateCount = Detail::objectSpansInArray(interactiveState).size();
  result.npcLifecycleStateCount = Detail::objectSpansInArray(npcLifecycleState).size();
  result.nearbyNpcCount = Detail::objectSpansInArray(nearbyNpcs).size();
  result.nearbyNpcKnownDialogCount = Detail::objectSpansInArray(nearbyNpcKnownDialogs).size();
  result.nearbyWaypointCount = Detail::objectSpansInArray(nearbyWaypoints).size();
  result.recentActionCount = Detail::objectSpansInArray(recentActions).size();
  result.moverStateCount = Detail::objectSpansInArray(moverState).size();
  result.serverCheckpointManifestPresent = result.serverCheckpointManifest.present;
  result.nearbyNpcWindowPresent = Detail::realValueForKey(text, "nearby_npc_radius", result.nearbyNpcRadius) &&
                                  result.nearbyNpcRadius > 0.0;
  result.nearbyWaypointWindowPresent = Detail::realValueForKey(text, "nearby_waypoint_radius", result.nearbyWaypointRadius) &&
                                       result.nearbyWaypointRadius > 0.0;
  result.activeWorldItemWindowPresent = Detail::realValueForKey(text, "active_world_item_radius", result.activeWorldItemRadius) &&
                                         result.activeWorldItemRadius > 0.0;
  result.scriptStateTruncated = Detail::boolValueForKey(text, "script_state_truncated", false);

  if(!result.bootstrapSchemaMatches) {
    Detail::fail(result, "snapshot schema marker does not match mmo_bootstrap_snapshot_v1");
    return result;
    }
  if(!result.characterMatches) {
    Detail::fail(result, "snapshot character_key does not match PC_HERO");
    return result;
    }
  if(!result.serverReady) {
    Detail::fail(result, "snapshot ready is not true");
    return result;
    }

  result.ok = true;
  result.message = "bootstrap snapshot validated";
  return result;
}

inline Result loadAndValidate(std::string_view path,
                              std::string_view expectedCharacterKey,
                              std::string_view expectedSessionKey) {
  Result result;
  auto text = Detail::readWholeFile(path, result);
  if(!result.fileRead)
    return result;

  result.stepMatches = Detail::quotedValueEquals(text, "step", Detail::ExpectedStep);
  result.characterMatches = Detail::quotedValueEquals(text, "character_key", expectedCharacterKey);
  result.sessionMatches = Detail::quotedValueEquals(text, "session_key", expectedSessionKey);

  const auto integrity = Detail::objectForKey(text, "integrity");
  result.integrityOk = !integrity.empty() && Detail::boolValueEquals(integrity, "ok", true);

  const auto inventory = Detail::arrayForKey(text, "inventory");
  const auto equipment = Detail::arrayForKey(text, "equipment");
  const auto inventoryObjects = Detail::objectSpansInArray(inventory);
  const auto equipmentObjects = Detail::objectSpansInArray(equipment);
  result.inventoryCount = inventoryObjects.size();
  result.equipmentCount = equipmentObjects.size();
  result.items = Detail::restoreItems(inventory, equipment);

  if(!result.stepMatches) {
    Detail::fail(result, "snapshot step marker does not match Step63 restore contract");
    return result;
    }
  if(!result.characterMatches) {
    Detail::fail(result, "snapshot character_key does not match PC_HERO");
    return result;
    }
  if(!result.sessionMatches) {
    Detail::fail(result, "snapshot session_key does not match current MMO action session");
    return result;
    }
  if(!result.integrityOk) {
    Detail::fail(result, "snapshot integrity.ok is not true");
    return result;
    }

  result.ok = true;
  result.message = "snapshot validated";
  return result;
}

} // namespace Mmo::RestoreSnapshot
























