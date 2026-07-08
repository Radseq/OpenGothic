#include "mmo_npc_perception_runtime_source.h"

#include "mmo_server_persistence.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Mmo::NpcPerceptionRuntime {
namespace {

[[nodiscard]] std::string lowerAscii(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for(const unsigned char c : value) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

[[nodiscard]] std::string nullToEmpty(std::string_view value) {
  return lowerAscii(value) == "null" ? std::string() : std::string(value);
}

[[nodiscard]] std::string normalizedDbText(std::string_view value) {
  std::string out = nullToEmpty(value);
  while(!out.empty() && std::isspace(static_cast<unsigned char>(out.front())) != 0) {
    out.erase(out.begin());
  }
  while(!out.empty() && std::isspace(static_cast<unsigned char>(out.back())) != 0) {
    out.pop_back();
  }
  return out;
}

[[nodiscard]] std::vector<std::string> splitRow(std::string_view row) {
  std::vector<std::string> out;
  while(true) {
    const auto tab = row.find('\t');
    if(tab == std::string_view::npos) {
      out.push_back(normalizedDbText(row));
      break;
    }
    out.push_back(normalizedDbText(row.substr(0, tab)));
    row.remove_prefix(tab + 1);
  }
  return out;
}

[[nodiscard]] std::vector<std::vector<std::string>> splitRows(std::string_view raw) {
  std::vector<std::vector<std::string>> out;
  while(!raw.empty()) {
    const auto nl = raw.find('\n');
    const auto line = raw.substr(0, nl);
    if(!line.empty()) {
      out.push_back(splitRow(line));
    }
    if(nl == std::string_view::npos) {
      break;
    }
    raw.remove_prefix(nl + 1);
  }
  return out;
}

[[nodiscard]] double parseDoubleOrZero(std::string_view text) noexcept {
  std::string owned(text);
  char* end = nullptr;
  const double value = std::strtod(owned.c_str(), &end);
  if(end == nullptr || *end != '\0') {
    return 0.0;
  }
  return value;
}

[[nodiscard]] std::uint64_t parseU64OrZero(std::string_view text) noexcept {
  std::uint64_t value = 0;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size()) {
    return 0;
  }
  return value;
}

[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool endsWith(std::string_view text, std::string_view suffix) noexcept {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

[[nodiscard]] bool isWeakIdentityToken(std::string_view value) {
  const std::string lower = lowerAscii(value);
  return lower.empty() || lower == "none" || lower == "null" || lower == "undefined" || lower == "0" || lower == "-1";
}

[[nodiscard]] bool isWeakEntityKey(std::string_view entityKey) {
  const std::string lower = lowerAscii(entityKey);
  if(isWeakIdentityToken(lower)) {
    return true;
  }
  if(lower == "npc:none" || lower == "creature:none" || lower == "npc:null" || lower == "creature:null") {
    return true;
  }
  if(endsWith(lower, ":none") || endsWith(lower, ":null") || endsWith(lower, ":undefined")) {
    return true;
  }
  return startsWith(lower, "npc:pid:-1") || startsWith(lower, "creature:pid:-1");
}

[[nodiscard]] std::string firstStrongText(std::initializer_list<std::string_view> values) {
  for(const auto value : values) {
    const std::string normalized = normalizedDbText(value);
    if(!isWeakIdentityToken(normalized)) {
      return normalized;
    }
  }
  return {};
}

[[nodiscard]] std::string sanitizeIdentityComponent(std::string value) {
  if(value.empty()) {
    return value;
  }
  for(char& ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if(std::isalnum(c) == 0 && ch != '_' && ch != '-' && ch != '.') {
      ch = '_';
    }
  }
  return value;
}

[[nodiscard]] std::string repairedEntityKey(
    std::string_view entityKind,
    std::string_view templateKey,
    std::string_view npcInstance,
    std::string_view worldEntityStateUuid) {
  const std::string uuid = normalizedDbText(worldEntityStateUuid);
  if(isWeakIdentityToken(uuid)) {
    return {};
  }

  const std::string kind = isWeakIdentityToken(entityKind) ? "npc" : sanitizeIdentityComponent(std::string(entityKind));
  const std::string stableTemplate = sanitizeIdentityComponent(firstStrongText({templateKey, npcInstance}));
  if(stableTemplate.empty()) {
    return {};
  }

  std::string out;
  out.reserve(kind.size() + stableTemplate.size() + uuid.size() + 24);
  out += kind;
  out += ":template:";
  out += stableTemplate;
  out += ":state:";
  out += uuid;
  return out;
}

[[nodiscard]] std::string worldInstanceMatchPredicate(std::string_view worldInstanceKey, std::string_view worldName) {
  const std::string keySql = Server::sqlLiteral(worldInstanceKey);
  const std::string nameSql = Server::sqlLiteral(worldName);
  std::string out;
  out += "(rwi.world_instance_key=" + keySql;
  out += " OR rwi.world_instance_key=" + nameSql;
  out += " OR LOWER(rwi.world_instance_key)=LOWER(" + keySql + ")";
  out += " OR LOWER(rwi.world_instance_key)=LOWER(" + nameSql + ")";
  out += " OR LOWER(cwt.world_name)=LOWER(" + nameSql + ")";
  out += " OR LOWER(REPLACE(cwt.world_name,'.ZEN',''))=LOWER(" + nameSql + ")";
  out += " OR LOWER(REPLACE(cwt.world_name,'.zen',''))=LOWER(" + nameSql + ")";
  out += " OR LOWER(rwi.world_instance_key) LIKE CONCAT('%',LOWER(" + nameSql + "),'%'))";
  return out;
}

[[nodiscard]] std::string worldInstanceOrder(std::string_view worldInstanceKey, std::string_view worldName) {
  const std::string keySql = Server::sqlLiteral(worldInstanceKey);
  const std::string nameSql = Server::sqlLiteral(worldName);
  std::string out;
  out += "CASE ";
  out += "WHEN rwi.world_instance_key=" + keySql + " THEN 0 ";
  out += "WHEN rwi.world_instance_key=" + nameSql + " THEN 1 ";
  out += "WHEN LOWER(cwt.world_name)=LOWER(" + nameSql + ") THEN 2 ";
  out += "ELSE 10 END,";
  out += "rwi.updated_at DESC";
  return out;
}

} // namespace

RuntimeWorldInstance resolveRuntimeWorldInstance(
    const Server::MySqlTarget& target,
    std::string_view worldInstanceKey,
    std::string_view worldName) {
  std::string sql;
  sql += "SELECT BIN_TO_UUID(rwi.world_instance_id,1),rwi.world_instance_key,";
  sql += "COALESCE(cwt.world_name,rwi.world_instance_key,''),COALESCE(rwi.current_tick,0) ";
  sql += "FROM realm_world_instances rwi ";
  sql += "LEFT JOIN content_world_templates cwt ON cwt.world_template_id=rwi.world_template_id ";
  sql += "WHERE rwi.lifecycle_state IN ('active','paused') AND ";
  sql += worldInstanceMatchPredicate(worldInstanceKey, worldName);
  sql += " ORDER BY ";
  sql += worldInstanceOrder(worldInstanceKey, worldName);
  sql += " LIMIT 1;";

  const auto rows = splitRows(Server::runMysql(target, sql));
  if(rows.empty() || rows.front().size() < 4 || rows.front()[0].empty()) {
    throw std::runtime_error("runtime world instance not found for key/name");
  }

  RuntimeWorldInstance out;
  out.worldInstanceUuid = rows.front()[0];
  out.worldInstanceKey = rows.front()[1];
  out.worldName = rows.front()[2];
  out.serverTick = parseU64OrZero(rows.front()[3]);
  return out;
}

RuntimeActorSnapshot loadRuntimeActors(
    const Server::MySqlTarget& target,
    const RuntimeActorQueryOptions& options) {
  RuntimeActorSnapshot out;
  out.world = resolveRuntimeWorldInstance(target, options.worldInstanceKey, options.worldName);

  const std::string worldUuidSql = Server::sqlLiteral(out.world.worldInstanceUuid);

  std::string playerSql;
  playerSql += "SELECT BIN_TO_UUID(ss.session_id,1),BIN_TO_UUID(c.character_id,1),c.character_key,";
  playerSql += "cp.pos_x,cp.pos_y,cp.pos_z,COALESCE(cp.server_tick,0) ";
  playerSql += "FROM server_sessions ss ";
  playerSql += "JOIN characters c ON c.character_id=ss.character_id ";
  playerSql += "JOIN character_positions cp ON cp.character_id=c.character_id ";
  playerSql += "WHERE ss.world_instance_id=UUID_TO_BIN(" + worldUuidSql + ",1) ";
  playerSql += "AND cp.world_instance_id=ss.world_instance_id ";
  playerSql += "AND ss.lifecycle_state='active' ";
  playerSql += "AND c.lifecycle_state IN ('creating','active') ";
  playerSql += "AND cp.pos_x IS NOT NULL AND cp.pos_y IS NOT NULL AND cp.pos_z IS NOT NULL ";
  playerSql += "ORDER BY ss.last_seen_at DESC,c.character_key LIMIT " + std::to_string(options.maxPlayers) + ";";

  std::uint64_t maxPlayerTick = out.world.serverTick;
  for(const auto& row : splitRows(Server::runMysql(target, playerSql))) {
    if(row.size() < 7 || row[2].empty()) {
      continue;
    }
    NpcPerception::PlayerActor actor;
    actor.targetKey = row[2];
    actor.characterKey = row[2];
    actor.position = {
        parseDoubleOrZero(row[3]),
        parseDoubleOrZero(row[4]),
        parseDoubleOrZero(row[5]),
    };
    actor.active = true;
    actor.sessionUuid = row[0];
    actor.characterUuid = row[1];
    out.players.push_back(std::move(actor));
    const std::uint64_t tick = parseU64OrZero(row[6]);
    if(tick > maxPlayerTick) {
      maxPlayerTick = tick;
    }
  }
  out.world.serverTick = maxPlayerTick;

  std::string npcSql;
  npcSql += "SELECT BIN_TO_UUID(wes.world_entity_state_id,1),wes.entity_key,wes.entity_kind,";
  npcSql += "COALESCE(cet.engine_template_key,''),COALESCE(cet.script_name,''),";
  npcSql += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.npc_instance')),''),";
  npcSql += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_name')),''),";
  npcSql += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_name')),''),";
  npcSql += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_name')),''),";
  npcSql += "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.creature_template_key')),''),";
  npcSql += "wes.pos_x,wes.pos_y,wes.pos_z ";
  npcSql += "FROM world_entity_state wes ";
  npcSql += "LEFT JOIN content_entity_templates cet ON cet.entity_template_id=wes.entity_template_id ";
  npcSql += "WHERE wes.world_instance_id=UUID_TO_BIN(" + worldUuidSql + ",1) ";
  npcSql += "AND wes.entity_kind IN ('npc','creature') ";
  npcSql += "AND wes.lifecycle_state='active' ";
  npcSql += "AND wes.pos_x IS NOT NULL AND wes.pos_y IS NOT NULL AND wes.pos_z IS NOT NULL ";
  npcSql += "ORDER BY wes.updated_at DESC,wes.entity_key LIMIT " + std::to_string(options.maxNpcs) + ";";

  for(const auto& row : splitRows(Server::runMysql(target, npcSql))) {
    ++out.npcIdentity.npcRowsRead;
    if(row.size() < 13) {
      ++out.npcIdentity.skippedWeakEntityKeys;
      continue;
    }

    const std::string npcInstance = firstStrongText({row[4], row[5], row[6], row[7], row[8], row[9], row[3]});
    if(npcInstance.empty() && !options.includeWeakNpcIdentity) {
      ++out.npcIdentity.skippedMissingNpcInstance;
      continue;
    }

    std::string entityKey = row[1];
    if(isWeakEntityKey(entityKey)) {
      std::string candidate;
      if(options.repairWeakNpcEntityKeys) {
        candidate = repairedEntityKey(row[2], row[3], npcInstance, row[0]);
        if(!candidate.empty()) {
          entityKey = std::move(candidate);
          ++out.npcIdentity.repairedEntityKeys;
        }
      }
      if(isWeakEntityKey(entityKey) && !options.includeWeakNpcIdentity) {
        ++out.npcIdentity.skippedWeakEntityKeys;
        continue;
      }
    }

    NpcPerception::NpcActor actor;
    actor.entityKey = entityKey;
    actor.npcInstance = npcInstance;
    actor.position = {
        parseDoubleOrZero(row[10]),
        parseDoubleOrZero(row[11]),
        parseDoubleOrZero(row[12]),
    };
    actor.active = true;
    out.npcs.push_back(std::move(actor));
    ++out.npcIdentity.acceptedNpcs;
  }

  return out;
}

} // namespace Mmo::NpcPerceptionRuntime
