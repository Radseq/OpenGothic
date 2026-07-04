#include "mmo_server_persistence.h"

#include <array>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "mmo_server_snapshot_limits.h"
#include "mmo_server_world_clock.h"

namespace Mmo::Server {

namespace {

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

[[nodiscard]] bool startsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
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
  out.reserve(text.size() + 2);
  out.push_back('"');
  for(char ch : text) {
    switch(ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
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
        out.push_back(ch);
        break;
    }
  }
  out.push_back('"');
  return out;
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
  while(!out.empty() && std::isspace(static_cast<unsigned char>(out.back())) != 0)
    out.pop_back();
  if(out.empty() || out.back() != '}')
    return;
  out.pop_back();
  appendJsonRawField(out, key, value);
  out.push_back('}');
}

[[nodiscard]] std::string sourceRowsToOrderedJsonArrayQuery(std::string_view sourceSql, std::size_t limit) {
  std::string query;
  query += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (SELECT row_json FROM (";
  query += sourceSql;
  query += ") source_rows ORDER BY dist_sq ASC,owner_key LIMIT " + std::to_string(limit);
  query += ") ordered_rows), JSON_ARRAY());";
  return query;
}

[[nodiscard]] std::string activeHeroBootstrapSubquery(std::string_view sessionSql) {
  std::string out;
  out += "(SELECT ss.character_id,ss.realm_id,ss.world_instance_id,COALESCE(cp.pos_x,cca.pos_x,0) AS hx,";
  out += "COALESCE(cp.pos_y,cca.pos_y,0) AS hy,COALESCE(cp.pos_z,cca.pos_z,0) AS hz ";
  out += "FROM server_sessions ss ";
  out += "LEFT JOIN character_positions cp ON cp.character_id=ss.character_id ";
  out += "LEFT JOIN character_checkpoint_audit cca ON cca.checkpoint_id=(SELECT ca.checkpoint_id FROM character_checkpoint_audit ca WHERE ca.session_id=ss.session_id ORDER BY ca.created_at DESC LIMIT 1) ";
  out += "WHERE ss.session_id=UUID_TO_BIN(";
  out += sessionSql;
  out += ",1) LIMIT 1) h";
  return out;
}

[[nodiscard]] std::string sqlNullableDouble(std::optional<double> value) {
  if(!value)
    return "NULL";
  return std::to_string(*value);
}

[[nodiscard]] int parsePortOrDefault(std::string_view text) noexcept {
  int value = 3306;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return 3306;
  return value;
}

[[nodiscard]] std::uint64_t parseU64OrZero(std::string_view text) noexcept {
  std::uint64_t value = 0;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if(r.ec != std::errc{} || r.ptr != text.data() + text.size())
    return 0;
  return value;
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
  const auto envValue = [](const char* name) {
    char* raw = nullptr;
    std::size_t size = 0;
    if(_dupenv_s(&raw, &size, name) != 0 || raw == nullptr)
      return std::string();
    std::string out(raw, size > 0 ? size - 1 : 0);
    std::free(raw);
    return out;
  };
  if(auto exe = envValue("GOTHIC_MMO_MYSQL_EXE"); !exe.empty())
    return exe;
  if(auto exe = envValue("MYSQL_EXE"); !exe.empty())
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

[[nodiscard]] std::string dbSessionKeyForCharacter(const Options& opt) {
  std::string out = opt.sessionKey;
  out.push_back(':');
  out.append(opt.characterKey);
  return out;
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

} // namespace

std::string sqlLiteral(std::string_view text) {
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

std::string sqlJson(std::string_view json) {
  return "CAST(" + sqlLiteral(json) + " AS JSON)";
}

const char* sqlBool(bool value) noexcept {
  return value ? "TRUE" : "FALSE";
}

MySqlTarget parseMysqlUrl(std::string_view url) {
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
    out.port = parsePortOrDefault(hostPort.substr(hostColon + 1));
  out.database = std::string(url.substr(slash + 1));
  if(out.host.empty())
    out.host = "127.0.0.1";
  if(out.user.empty() || out.database.empty())
    throw std::runtime_error("mysql URL must include user and database");
  return out;
}

std::string runMysql(const MySqlTarget& target, std::string_view sql) {
  std::string statement;
  statement.reserve(MysqlSessionPreamble.size() + sql.size());
  statement.append(MysqlSessionPreamble);
  statement.append(sql.data(), sql.size());
  return runCommand(mysqlBaseCommand(target) + " --execute " + shellQuote(statement) + " 2>&1");
}

std::vector<std::string> splitMysqlLastRow(std::string_view raw) {
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

std::string mysqlSingleField(const MySqlTarget& target, std::string_view sql) {
  const auto parts = splitMysqlLastRow(runMysql(target, sql));
  if(parts.empty())
    return {};
  return parts.front() == "NULL" ? std::string() : std::string(parts.front());
}

std::string mysqlSingleFieldWithDiagnostic(const MySqlTarget& target,
                                           std::string_view sql,
                                           std::string_view label) {
  try {
    return mysqlSingleField(target, sql);
  } catch(const std::exception& e) {
    std::cout << '[' << label << "_failed] error=" << e.what() << "\n";
    return {};
  }
}

std::string mysqlJsonOr(const MySqlTarget& target,
                        std::string_view sql,
                        std::string_view fallbackJson) {
  auto out = mysqlSingleField(target, sql);
  if(out.empty())
    return std::string(fallbackJson);
  return out;
}

std::string mysqlJsonOrWithDiagnostic(const MySqlTarget& target,
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

std::string concatenateJsonArrays(std::initializer_list<std::string_view> arrays) {
  std::size_t capacity = 2;
  for(const auto array : arrays)
    capacity += array.size() + 1;

  std::string out;
  out.reserve(capacity);
  out.push_back('[');
  bool first = true;

  for(auto array : arrays) {
    while(!array.empty() && static_cast<unsigned char>(array.front()) <= ' ')
      array.remove_prefix(1);
    while(!array.empty() && static_cast<unsigned char>(array.back()) <= ' ')
      array.remove_suffix(1);
    if(array.size() < 2 || array.front() != '[' || array.back() != ']')
      continue;

    array.remove_prefix(1);
    array.remove_suffix(1);
    while(!array.empty() && static_cast<unsigned char>(array.front()) <= ' ')
      array.remove_prefix(1);
    while(!array.empty() && static_cast<unsigned char>(array.back()) <= ' ')
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

std::string dbLogin(const MySqlTarget& target, const Options& opt) {
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

bool isActiveDbSession(const MySqlTarget& target, std::string_view sessionUuid) {
  if(sessionUuid.empty())
    return false;
  std::string sql;
  sql += "SELECT COUNT(*) FROM server_sessions ";
  sql += "WHERE session_id=UUID_TO_BIN(" + sqlLiteral(sessionUuid) + ",1) ";
  sql += "AND lifecycle_state='active';";
  return parseU64OrZero(mysqlSingleField(target, sql)) > 0;
}

bool ensureActiveDbSession(const MySqlTarget& target,
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

BootstrapReadiness readBootstrapReadiness(const MySqlTarget& target,
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

  const auto parts = splitMysqlLastRow(runMysql(target, sql));
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

BootstrapReadiness readBootstrapReadinessWithFallback(const MySqlTarget& target,
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

CharacterBootstrapSnapshotSlices readCharacterBootstrapSnapshotSlices(const MySqlTarget& target,
                                                                      std::string_view sessionUuid,
                                                                      std::string_view characterKey,
                                                                      std::string_view worldName) {
  CharacterBootstrapSnapshotSlices out;
  if(sessionUuid.empty())
    return out;

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
  out.characterListJson = mysqlJsonOrWithDiagnostic(target, characterListQuery, "[]", "bootstrap_character_list");

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
  out.characterJson = mysqlJsonOr(target, characterQuery, "{}");

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
  inventoryQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY COALESCE(ci.bag_index,999999),ii.item_instance_key LIMIT " + std::to_string(MaxBootstrapInventoryRows);
  inventoryQuery += ") rows_json), JSON_ARRAY());";
  out.inventoryJson = mysqlJsonOr(target, inventoryQuery, "[]");

  std::string equipmentQuery;
  equipmentQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  equipmentQuery += "SELECT JSON_OBJECT('slot',ce.equipment_slot,'item_instance_uuid',BIN_TO_UUID(ii.item_instance_id,1),";
  equipmentQuery += "'item_instance_key',ii.item_instance_key,'item_template_key',cit.item_template_key,'symbol_index',cit.symbol_index,";
  equipmentQuery += "'display_name',cit.display_name,'updated_at',DATE_FORMAT(ce.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  equipmentQuery += "FROM server_sessions ss JOIN character_equipment ce ON ce.character_id=ss.character_id ";
  equipmentQuery += "JOIN item_instances ii ON ii.item_instance_id=ce.item_instance_id ";
  equipmentQuery += "JOIN content_item_templates cit ON cit.item_template_id=ii.item_template_id ";
  equipmentQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ce.equipment_slot LIMIT " + std::to_string(MaxBootstrapEquipmentRows);
  equipmentQuery += ") rows_json), JSON_ARRAY());";
  out.equipmentJson = mysqlJsonOr(target, equipmentQuery, "[]");

  std::string dialogsQuery;
  dialogsQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  dialogsQuery += "SELECT JSON_OBJECT('npc_key',npc_key,'info_key',info_key,'known',known,'permanent',permanent,";
  dialogsQuery += "'availability_state',availability_state,'updated_at',DATE_FORMAT(updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  dialogsQuery += "FROM server_sessions ss JOIN character_known_dialogs d ON d.character_id=ss.character_id ";
  dialogsQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY npc_key,info_key LIMIT " + std::to_string(MaxBootstrapKnownDialogRows);
  dialogsQuery += ") rows_json), JSON_ARRAY());";
  out.knownDialogsJson = mysqlJsonOr(target, dialogsQuery, "[]");

  std::string questsQuery;
  questsQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  questsQuery += "SELECT JSON_OBJECT('quest_key',quest_key,'section',section,'status',status,'entry_order',entry_order,";
  questsQuery += "'text_entries',text_entries,'updated_at',DATE_FORMAT(updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  questsQuery += "FROM server_sessions ss JOIN character_quests q ON q.character_id=ss.character_id ";
  questsQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY quest_key LIMIT " + std::to_string(MaxBootstrapQuestRows);
  questsQuery += ") rows_json), JSON_ARRAY());";
  out.questsJson = mysqlJsonOr(target, questsQuery, "[]");

  std::string scriptQuery;
  scriptQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  scriptQuery += "SELECT JSON_OBJECT('script_key',script_key,'symbol_index',symbol_index,'value_type',value_type,'value_index',value_index,";
  scriptQuery += "'value_int',value_int,'updated_at',DATE_FORMAT(updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  scriptQuery += "FROM server_sessions ss JOIN character_script_state s ON s.character_id=ss.character_id ";
  scriptQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND s.value_type IN ('int','array_int') ORDER BY script_key,value_index LIMIT " + std::to_string(MaxBootstrapScriptStateRows);
  scriptQuery += ") rows_json), JSON_ARRAY());";
  out.scriptStateJson = mysqlJsonOr(target, scriptQuery, "[]");

  return out;
}

WorldBootstrapSnapshotSlices readWorldBootstrapSnapshotSlices(const MySqlTarget& target,
                                                              std::string_view sessionUuid,
                                                              std::string_view worldName) {
  WorldBootstrapSnapshotSlices out;
  if(sessionUuid.empty())
    return out;

  const std::string sessionSql = sqlLiteral(sessionUuid);
  const std::string worldSql = sqlLiteral(worldName);

  std::string worldSampleQuery;
  worldSampleQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  worldSampleQuery += "SELECT JSON_OBJECT('entity_key',wes.entity_key,'entity_kind',wes.entity_kind,'lifecycle_state',wes.lifecycle_state,";
  worldSampleQuery += "'persistent_id',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED),";
  worldSampleQuery += "'symbol_index',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED),";
  worldSampleQuery += "'exists_in_world',JSON_EXTRACT(wes.state_json,'$.exists_in_world'),";
  worldSampleQuery += "'updated_at',DATE_FORMAT(wes.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  worldSampleQuery += "FROM server_sessions ss JOIN world_entity_state wes ON wes.world_instance_id=ss.world_instance_id ";
  worldSampleQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND wes.entity_kind='item' AND wes.lifecycle_state<>'active' ORDER BY wes.updated_at DESC LIMIT " + std::to_string(MaxBootstrapWorldDeltaRows);
  worldSampleQuery += ") rows_json), JSON_ARRAY());";
  out.worldDeltasJson = mysqlJsonOr(target, worldSampleQuery, "[]");

  out.worldClockJson = mysqlJsonOr(target, buildWorldClockSnapshotQuery(sessionSql, worldSql), "{}");

  std::string interactiveQuery;
  interactiveQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  interactiveQuery += "SELECT JSON_OBJECT('entity_key',wes.entity_key,'lifecycle_state',wes.lifecycle_state,";
  interactiveQuery += "'state_id',CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.state_id')) AS SIGNED),";
  interactiveQuery += "'locked',JSON_EXTRACT(wes.state_json,'$.locked'),'cracked',JSON_EXTRACT(wes.state_json,'$.cracked'),";
  interactiveQuery += "'updated_at',DATE_FORMAT(wes.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  interactiveQuery += "FROM server_sessions ss JOIN world_entity_state wes ON wes.world_instance_id=ss.world_instance_id ";
  interactiveQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND wes.entity_kind='interactive' ";
  interactiveQuery += "ORDER BY wes.updated_at DESC,wes.entity_key LIMIT " + std::to_string(MaxBootstrapInteractiveSampleRows);
  interactiveQuery += ") rows_json), JSON_ARRAY());";
  out.interactiveStateJson = mysqlJsonOr(target, interactiveQuery, "[]");

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
  npcLifecycleQuery += "ORDER BY CASE WHEN wes.lifecycle_state='dead' THEN 0 WHEN wes.lifecycle_state<>'active' THEN 1 ELSE 2 END,wes.updated_at DESC,wes.entity_key LIMIT " + std::to_string(MaxBootstrapNpcLifecycleRows);
  npcLifecycleQuery += ") rows_json), JSON_ARRAY());";
  out.npcLifecycleJson = mysqlJsonOr(target, npcLifecycleQuery, "[]");

  std::string recentEventsQuery;
  recentEventsQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  recentEventsQuery += "SELECT JSON_OBJECT('event_seq',wej.event_seq,'event_type',wej.event_type,'event_class',wej.event_class,";
  recentEventsQuery += "'entity_key',wej.entity_key,'subject_key',wej.subject_key,'server_tick',wej.server_tick,";
  recentEventsQuery += "'occurred_at',DATE_FORMAT(wej.occurred_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  recentEventsQuery += "FROM server_sessions ss JOIN world_event_journal wej ON wej.world_instance_id=ss.world_instance_id ";
  recentEventsQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY wej.event_seq DESC LIMIT " + std::to_string(MaxBootstrapRecentEventRows);
  recentEventsQuery += ") rows_json), JSON_ARRAY());";
  out.recentEventsJson = mysqlJsonOr(target, recentEventsQuery, "[]");

  std::string moverStateQuery;
  moverStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  moverStateQuery += "SELECT JSON_OBJECT('mover_key',ms.mover_key,'state_after',ms.state_after,'state_after_name',ms.state_after_name,";
  moverStateQuery += "'frame_index',ms.frame_index,'target_frame_index',ms.target_frame_index,'last_server_tick',ms.last_server_tick,";
  moverStateQuery += "'row_version',ms.row_version,'updated_at',DATE_FORMAT(ms.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  moverStateQuery += "FROM server_sessions ss JOIN mmo_world_mover_state_current ms ON ms.world_instance_id=ss.world_instance_id ";
  moverStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ms.last_server_tick DESC,ms.mover_key LIMIT " + std::to_string(MaxBootstrapMoverStateRows);
  moverStateQuery += ") rows_json), JSON_ARRAY());";
  out.moverStateJson = mysqlJsonOrWithDiagnostic(target, moverStateQuery, "[]", "bootstrap_mover_state");

  std::string triggerQueueQuery;
  triggerQueueQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  triggerQueueQuery += "SELECT JSON_OBJECT('trigger_key',tq.trigger_key,'queue_state',tq.queue_state,'event_type_name',tq.event_type_name,";
  triggerQueueQuery += "'scheduled_server_tick',tq.scheduled_server_tick,'last_server_tick',tq.last_server_tick,";
  triggerQueueQuery += "'row_version',tq.row_version,'updated_at',DATE_FORMAT(tq.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  triggerQueueQuery += "FROM server_sessions ss JOIN mmo_world_trigger_queue_current tq ON tq.world_instance_id=ss.world_instance_id ";
  triggerQueueQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY tq.scheduled_server_tick ASC,tq.trigger_key LIMIT " + std::to_string(MaxBootstrapTriggerQueueRows);
  triggerQueueQuery += ") rows_json), JSON_ARRAY());";
  out.triggerQueueJson = mysqlJsonOrWithDiagnostic(target, triggerQueueQuery, "[]", "bootstrap_trigger_queue");

  std::string worldTransitionStateQuery;
  worldTransitionStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  worldTransitionStateQuery += "SELECT JSON_OBJECT('character_key',c.character_key,'from_world_key',ts.from_world_key,'to_world_key',ts.to_world_key,";
  worldTransitionStateQuery += "'transition_state',ts.transition_state,'chapter_key',ts.chapter_key,'visited',ts.visited,";
  worldTransitionStateQuery += "'last_server_tick',ts.last_server_tick,'row_version',ts.row_version,'updated_at',DATE_FORMAT(ts.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  worldTransitionStateQuery += "FROM server_sessions ss JOIN characters c ON c.character_id=ss.character_id ";
  worldTransitionStateQuery += "JOIN mmo_character_world_transition_state_current ts ON ts.character_id=ss.character_id ";
  worldTransitionStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ts.last_server_tick DESC,ts.to_world_key LIMIT " + std::to_string(MaxBootstrapWorldTransitionRows);
  worldTransitionStateQuery += ") rows_json), JSON_ARRAY());";
  out.worldTransitionStateJson = mysqlJsonOrWithDiagnostic(target, worldTransitionStateQuery, "[]", "bootstrap_world_transition_state");

  std::string clientCorrectionsQuery;
  clientCorrectionsQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  clientCorrectionsQuery += "SELECT JSON_OBJECT('action_kind',cc.action_kind,'client_local_sequence',cc.client_local_sequence,'correction_kind',cc.correction_kind,";
  clientCorrectionsQuery += "'reason',cc.reason,'acknowledged',cc.acknowledged,'rejected_server_tick',cc.rejected_server_tick,";
  clientCorrectionsQuery += "'authoritative_server_tick',cc.authoritative_server_tick,'authoritative_pos_x',cc.authoritative_pos_x,";
  clientCorrectionsQuery += "'authoritative_pos_y',cc.authoritative_pos_y,'authoritative_pos_z',cc.authoritative_pos_z,'authoritative_yaw',cc.authoritative_yaw,";
  clientCorrectionsQuery += "'updated_at',DATE_FORMAT(cc.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  clientCorrectionsQuery += "FROM server_sessions ss JOIN mmo_client_action_correction_current cc ON cc.session_id=ss.session_id ";
  clientCorrectionsQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) AND cc.acknowledged=FALSE ORDER BY cc.updated_at DESC LIMIT " + std::to_string(MaxBootstrapClientCorrectionRows);
  clientCorrectionsQuery += ") rows_json), JSON_ARRAY());";
  out.clientCorrectionsJson = mysqlJsonOrWithDiagnostic(target, clientCorrectionsQuery, "[]", "bootstrap_client_corrections");

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
  out.checkpointManifestJson = mysqlJsonOrWithDiagnostic(target, checkpointManifestQuery, "{}", "bootstrap_save_checkpoint_manifest");

  return out;
}

PositionedBootstrapSnapshotSlices readPositionedBootstrapSnapshotSlices(const MySqlTarget& target,
                                                                        std::string_view sessionUuid,
                                                                        std::string_view worldName) {
  PositionedBootstrapSnapshotSlices out;
  if(sessionUuid.empty())
    return out;

  const std::string sessionSql = sqlLiteral(sessionUuid);
  const std::string worldSql = sqlLiteral(worldName);
  const std::string activeItemRadiusSql = std::to_string(BootstrapActiveWorldItemRadius);
  const std::string nearbyNpcRadiusSql = std::to_string(BootstrapNearbyNpcRadius);
  const std::string nearbyWaypointRadiusSql = std::to_string(BootstrapNearbyWaypointRadius);
  const std::string activeHeroSubquery = activeHeroBootstrapSubquery(sessionSql);

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

  const auto activeWorldInventoryItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToOrderedJsonArrayQuery(activeWorldInventorySource, MaxBootstrapActiveWorldItemRows),
      "[]",
      "bootstrap_active_world_inventory_items");
  const auto activeWorldEntityItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToOrderedJsonArrayQuery(activeWorldEntitySource, MaxBootstrapActiveWorldItemRows),
      "[]",
      "bootstrap_active_world_entity_items");
  const auto activeReadModelItems = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToOrderedJsonArrayQuery(activeReadModelSource, MaxBootstrapActiveWorldItemRows),
      "[]",
      "bootstrap_active_world_read_model_items");
  out.activeWorldItemsJson = concatenateJsonArrays({activeWorldInventoryItems, activeWorldEntityItems, activeReadModelItems});

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
  std::cout << "[bootstrap_active_world_items] bytes=" << out.activeWorldItemsJson.size()
            << " world_inventory_bytes=" << activeWorldInventoryItems.size()
            << " world_entity_bytes=" << activeWorldEntityItems.size()
            << " read_model_bytes=" << activeReadModelItems.size();
  if(!activeWorldItemDebug.empty())
    std::cout << ' ' << activeWorldItemDebug;
  std::cout << "\n";

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

  out.nearbyNpcsJson = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToOrderedJsonArrayQuery(nearbyNpcSource, MaxBootstrapNearbyNpcRows),
      "[]",
      "bootstrap_nearby_npcs");
  out.nearbyNpcKnownDialogsJson = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToOrderedJsonArrayQuery(nearbyNpcDialogSource, MaxBootstrapNearbyNpcKnownDialogRows),
      "[]",
      "bootstrap_nearby_npc_known_dialogs");
  out.nearbyWaypointsJson = mysqlJsonOrWithDiagnostic(
      target,
      sourceRowsToOrderedJsonArrayQuery(nearbyWaypointSource, MaxBootstrapNearbyWaypointRows),
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
  std::cout << "[bootstrap_nearby_npcs] bytes=" << out.nearbyNpcsJson.size()
            << " known_dialog_bytes=" << out.nearbyNpcKnownDialogsJson.size()
            << " waypoint_bytes=" << out.nearbyWaypointsJson.size();
  if(!nearbyNpcDebug.empty())
    std::cout << ' ' << nearbyNpcDebug;
  std::cout << "\n";

  return out;
}

NpcAuthoritySnapshotSlices readNpcAuthoritySnapshotSlices(const MySqlTarget& target,
                                                          std::string_view sessionUuid,
                                                          std::string_view diagnosticPrefix) {
  NpcAuthoritySnapshotSlices out;
  if(sessionUuid.empty())
    return out;

  const std::string sessionSql = sqlLiteral(sessionUuid);
  const std::string prefix(diagnosticPrefix);

  std::string npcRoutineStateQuery;
  npcRoutineStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcRoutineStateQuery += "SELECT JSON_OBJECT('npc_entity_key',rs.npc_entity_key,'routine_state',rs.routine_state,";
  npcRoutineStateQuery += "'schedule_key',rs.schedule_key,'current_waypoint_key',rs.current_waypoint_key,'target_waypoint_key',rs.target_waypoint_key,";
  npcRoutineStateQuery += "'last_server_tick',rs.last_server_tick,'row_version',rs.row_version,'updated_at',DATE_FORMAT(rs.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcRoutineStateQuery += "FROM server_sessions ss JOIN mmo_npc_routine_state_current rs ON rs.world_instance_id=ss.world_instance_id ";
  npcRoutineStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY rs.last_server_tick DESC,rs.npc_entity_key LIMIT " + std::to_string(MaxBootstrapNpcAuthorityRows);
  npcRoutineStateQuery += ") rows_json), JSON_ARRAY());";
  out.routineStateJson = mysqlJsonOrWithDiagnostic(target, npcRoutineStateQuery, "[]", prefix + "_npc_routine_state");

  std::string npcAiStateQuery;
  npcAiStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcAiStateQuery += "SELECT JSON_OBJECT('npc_entity_key',ais.npc_entity_key,'ai_state',ais.ai_state,'ai_intent',ais.ai_intent,";
  npcAiStateQuery += "'target_key',ais.target_key,'perception_state',ais.perception_state,'last_server_tick',ais.last_server_tick,";
  npcAiStateQuery += "'row_version',ais.row_version,'updated_at',DATE_FORMAT(ais.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcAiStateQuery += "FROM server_sessions ss JOIN mmo_npc_ai_state_current ais ON ais.world_instance_id=ss.world_instance_id ";
  npcAiStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ais.last_server_tick DESC,ais.npc_entity_key LIMIT " + std::to_string(MaxBootstrapNpcAuthorityRows);
  npcAiStateQuery += ") rows_json), JSON_ARRAY());";
  out.aiStateJson = mysqlJsonOrWithDiagnostic(target, npcAiStateQuery, "[]", prefix + "_npc_ai_state");

  std::string npcPathStateQuery;
  npcPathStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcPathStateQuery += "SELECT JSON_OBJECT('npc_entity_key',ps.npc_entity_key,'path_state',ps.path_state,'route_key',ps.route_key,";
  npcPathStateQuery += "'current_waypoint_key',ps.current_waypoint_key,'next_waypoint_key',ps.next_waypoint_key,'target_waypoint_key',ps.target_waypoint_key,";
  npcPathStateQuery += "'pos_x',ps.pos_x,'pos_y',ps.pos_y,'pos_z',ps.pos_z,'last_server_tick',ps.last_server_tick,";
  npcPathStateQuery += "'row_version',ps.row_version,'updated_at',DATE_FORMAT(ps.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcPathStateQuery += "FROM server_sessions ss JOIN mmo_npc_path_state_current ps ON ps.world_instance_id=ss.world_instance_id ";
  npcPathStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY ps.last_server_tick DESC,ps.npc_entity_key LIMIT " + std::to_string(MaxBootstrapNpcAuthorityRows);
  npcPathStateQuery += ") rows_json), JSON_ARRAY());";
  out.pathStateJson = mysqlJsonOrWithDiagnostic(target, npcPathStateQuery, "[]", prefix + "_npc_path_state");

  std::string npcFightStateQuery;
  npcFightStateQuery += "SELECT COALESCE((SELECT JSON_ARRAYAGG(row_json) FROM (";
  npcFightStateQuery += "SELECT JSON_OBJECT('npc_entity_key',fs.npc_entity_key,'opponent_key',fs.opponent_key,'fight_state',fs.fight_state,";
  npcFightStateQuery += "'attack_state',fs.attack_state,'combo_index',fs.combo_index,'last_server_tick',fs.last_server_tick,";
  npcFightStateQuery += "'row_version',fs.row_version,'updated_at',DATE_FORMAT(fs.updated_at,'%Y-%m-%dT%H:%i:%s.%fZ')) AS row_json ";
  npcFightStateQuery += "FROM server_sessions ss JOIN mmo_npc_fight_state_current fs ON fs.world_instance_id=ss.world_instance_id ";
  npcFightStateQuery += "WHERE ss.session_id=UUID_TO_BIN(" + sessionSql + ",1) ORDER BY fs.last_server_tick DESC,fs.npc_entity_key LIMIT " + std::to_string(MaxBootstrapNpcAuthorityRows);
  npcFightStateQuery += ") rows_json), JSON_ARRAY());";
  out.fightStateJson = mysqlJsonOrWithDiagnostic(target, npcFightStateQuery, "[]", prefix + "_npc_fight_state");

  return out;
}

std::string buildSaveCheckpointBootstrapSnapshotJson(const MySqlTarget& target,
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
  if(out.empty())
    return out;

  const auto npcAuthority = readNpcAuthoritySnapshotSlices(target, sessionUuid, "bootstrap_db_checkpoint");
  appendJsonRawFieldBeforeFinalObjectBrace(out, BootstrapNpcRoutineStateSection, npcAuthority.routineStateJson);
  appendJsonRawFieldBeforeFinalObjectBrace(out, BootstrapNpcAiStateSection, npcAuthority.aiStateJson);
  appendJsonRawFieldBeforeFinalObjectBrace(out, BootstrapNpcPathStateSection, npcAuthority.pathStateJson);
  appendJsonRawFieldBeforeFinalObjectBrace(out, BootstrapNpcFightStateSection, npcAuthority.fightStateJson);

  return out;
}

void enqueueOutboxAction(const MySqlTarget& target, const OutboxActionRecord& record) {
  std::string sql;
  sql += "SET @action_id = NULL;";
  sql += "SET @status = NULL;";
  sql += "CALL mmo_enqueue_server_action(";
  sql += "UUID_TO_BIN(";
  sql += sqlLiteral(record.sessionUuid);
  sql += ", 1),";
  sql += sqlLiteral(record.actionName) + ",";
  sql += sqlLiteral(record.targetKey) + ",";
  sql += sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",";
  sql += std::to_string(record.priority) + ",";
  sql += std::to_string(record.maxAttempts) + ",";
  sql += "@action_id,@status);";
  sql += "SELECT CONCAT(BIN_TO_UUID(@action_id, 1), '\\t', @status);";
  (void)runMysql(target, sql);
}

void recordCharacterCheckpoint(const MySqlTarget& target, const CharacterCheckpointRecord& record) {
  std::string sql;
  sql += "SET @event_id = NULL;";
  sql += "CALL mmo_checkpoint_character_state(";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ", 1),";
  sql += std::to_string(record.serverTick) + ",";
  sql += std::to_string(record.posX) + ",";
  sql += std::to_string(record.posY) + ",";
  sql += std::to_string(record.posZ) + ",";
  sql += std::to_string(record.rotationYaw) + ",";
  sql += sqlLiteral(record.waypoint) + ",";
  sql += std::to_string(record.level) + ",";
  sql += std::to_string(record.experience) + ",";
  sql += std::to_string(record.experienceNext) + ",";
  sql += std::to_string(record.learningPoints) + ",";
  sql += std::to_string(record.healthCurrent) + ",";
  sql += std::to_string(record.healthMax) + ",";
  sql += std::to_string(record.manaCurrent) + ",";
  sql += std::to_string(record.manaMax) + ",";
  sql += std::to_string(record.strength) + ",";
  sql += std::to_string(record.dexterity) + ",";
  sql += std::to_string(record.guild) + ",";
  sql += std::to_string(record.trueGuild) + ",";
  sql += std::to_string(record.permanentAttitude) + ",";
  sql += std::to_string(record.temporaryAttitude) + ",";
  sql += sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",";
  sql += "@event_id);";
  sql += "SELECT BIN_TO_UUID(@event_id, 1);";
  (void)runMysql(target, sql);
}

void createSaveCheckpointManifest(const MySqlTarget& target, const SaveCheckpointManifestRecord& record) {
  std::string sql;
  sql += "SET @manifest_id=NULL; SET @event_id=NULL; SET @row_version_after=NULL;";
  sql += "CALL mmo_create_db_save_checkpoint_v1(";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.manifestKey) + ",";
  sql += sqlLiteral(record.checkpointKind) + ",";
  sql += sqlLiteral(record.reason) + ",";
  sql += std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",";
  sql += "@manifest_id,@event_id,@row_version_after);";
  sql += "SELECT CONCAT(BIN_TO_UUID(@manifest_id,1),'\\t',BIN_TO_UUID(@event_id,1),'\\t',@row_version_after);";
  (void)runMysql(target, sql);
}

void recordClientActionCorrection(const MySqlTarget& target, const ClientActionCorrectionRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @correction_id=NULL;";
  sql += "CALL mmo_record_client_action_correction(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.actionName) + ",";
  sql += std::to_string(record.localSequence) + ",";
  sql += sqlLiteral(record.correctionKind) + ",";
  sql += sqlLiteral(record.reason) + ",";
  sql += std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",";
  sql += "@event_id,@correction_id);";
  sql += "SELECT BIN_TO_UUID(@correction_id,1);";
  (void)runMysql(target, sql);
}

void setCharacterScriptInt(const MySqlTarget& target, const ScriptIntRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @value_after=NULL;";
  sql += "CALL mmo_set_character_script_int(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.scriptKey) + "," + std::to_string(record.symbolIndex) + ",";
  sql += std::to_string(record.valueIndex) + "," + std::to_string(record.valueAfter) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@value_after);";
  (void)runMysql(target, sql);
}

void updateCharacterQuest(const MySqlTarget& target, const QuestUpdateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL;";
  sql += "CALL mmo_update_character_quest(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.questKey) + "," + sqlLiteral(record.questName) + ",";
  sql += sqlLiteral(record.status) + "," + std::to_string(record.entryCount) + ",JSON_ARRAY(),";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id);";
  (void)runMysql(target, sql);
}

void setCharacterKnownDialog(const MySqlTarget& target, const KnownDialogRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL;";
  sql += "CALL mmo_set_character_known_dialog(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.infoKey) + ",";
  sql += sqlBool(record.known);
  sql += ",";
  sql += sqlBool(record.permanent);
  sql += "," + sqlLiteral(record.availability) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id);";
  (void)runMysql(target, sql);
}

void adjustCharacterProgression(const MySqlTarget& target, const ProgressionAdjustmentRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @experience_after=NULL; SET @learning_points_after=NULL;";
  sql += "CALL mmo_adjust_character_progression(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += std::to_string(record.experienceDelta) + "," + std::to_string(record.learningPointsDelta) + ",";
  sql += sqlLiteral(record.reason) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey);
  sql += ",@event_id,@experience_after,@learning_points_after);";
  (void)runMysql(target, sql);
}

void applyCharacterExperienceReward(const MySqlTarget& target, const ExperienceRewardRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @experience_after=NULL;";
  sql += "CALL mmo_apply_character_experience_reward(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += std::to_string(record.experienceDelta) + "," + sqlLiteral(record.reason) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@experience_after);";
  (void)runMysql(target, sql);
}

void applyCharacterDamage(const MySqlTarget& target, const CharacterDamageRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @health_after=NULL;";
  sql += "CALL mmo_apply_character_damage(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.characterKey) + "," + std::to_string(record.damage) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@health_after);";
  (void)runMysql(target, sql);
}

void applyWorldEntityDamage(const MySqlTarget& target, const WorldEntityDamageRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @health_after=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_apply_world_entity_damage(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.entityKey) + "," + std::to_string(record.damage) + ",";
  sql += sqlBool(record.fatal);
  sql += "," + std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@health_after,@row_after);";
  (void)runMysql(target, sql);
}

void markNpcDead(const MySqlTarget& target, const MarkNpcDeadRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_mark_npc_dead(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.entityKey) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordCharacterResourceDelta(const MySqlTarget& target, const CharacterResourceDeltaRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_character_resource_delta(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.characterKey) + "," + sqlLiteral(record.resourceKey) + ",";
  sql += std::to_string(record.delta) + "," + std::to_string(record.valueBefore) + ",";
  sql += std::to_string(record.valueAfter) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordTriggerEvent(const MySqlTarget& target, const TriggerEventRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL;";
  sql += "CALL mmo_record_trigger_event(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.triggerKey) + "," + sqlLiteral(record.eventTypeName) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id);";
  (void)runMysql(target, sql);
}

void recordMoverState(const MySqlTarget& target, const MoverStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_mover_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.moverKey) + "," + std::to_string(record.stateBefore) + ",";
  sql += std::to_string(record.stateAfter) + "," + sqlLiteral(record.stateAfterName) + ",";
  sql += std::to_string(record.frame) + "," + std::to_string(record.targetFrame) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordNpcRoutineState(const MySqlTarget& target, const NpcRoutineStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_routine_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.routineState) + ",";
  sql += sqlLiteral(record.scheduleKey) + "," + sqlLiteral(record.currentWaypoint) + ",";
  sql += sqlLiteral(record.targetWaypoint) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordNpcAiState(const MySqlTarget& target, const NpcAiStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_ai_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.aiState) + ",";
  sql += sqlLiteral(record.aiIntent) + "," + sqlLiteral(record.targetEntity) + ",";
  sql += sqlLiteral(record.perceptionState) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordNpcPathState(const MySqlTarget& target, const NpcPathStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_path_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.pathState) + ",";
  sql += sqlLiteral(record.routeKey) + "," + sqlLiteral(record.currentWaypoint) + ",";
  sql += sqlLiteral(record.nextWaypoint) + "," + sqlLiteral(record.targetWaypoint) + ",";
  sql += sqlNullableDouble(record.posX) + "," + sqlNullableDouble(record.posY) + "," + sqlNullableDouble(record.posZ) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordNpcFightState(const MySqlTarget& target, const NpcFightStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_fight_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.npcKey) + "," + sqlLiteral(record.opponentKey) + ",";
  sql += sqlLiteral(record.fightState) + "," + sqlLiteral(record.attackState) + ",";
  sql += std::to_string(record.comboIndex) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordTriggerQueueState(const MySqlTarget& target, const TriggerQueueStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_trigger_queue_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.triggerKey) + "," + sqlLiteral(record.queueState) + ",";
  sql += sqlLiteral(record.eventTypeName) + "," + std::to_string(record.scheduledServerTick) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void recordWorldTransitionState(const MySqlTarget& target, const WorldTransitionStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_world_transition_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.fromWorld) + "," + sqlLiteral(record.toWorld) + ",";
  sql += sqlLiteral(record.transitionState) + "," + sqlLiteral(record.chapterKey) + ",";
  sql += sqlBool(record.visited);
  sql += "," + std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void ackClientActionCorrection(const MySqlTarget& target, const ClientCorrectionAckRecord& record) {
  std::string sql;
  sql += "SET @row_after=NULL;";
  sql += "CALL mmo_ack_client_action_correction(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.actionKind) + "," + std::to_string(record.localSequence) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@row_after);";
  (void)runMysql(target, sql);
}

void recordInteractiveUse(const MySqlTarget& target, const InteractiveUseRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_interactive_use(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.interactiveKey) + "," + std::to_string(record.stateAfter) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void updateInteractiveState(const MySqlTarget& target, const InteractiveStateUpdateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_version_after=NULL;";
  sql += "CALL mmo_update_interactive_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.interactiveKey) + "," + std::to_string(record.stateAfter) + ",";
  sql += std::to_string(record.stateCount) + "," + std::to_string(record.stateMask) + ",";
  sql += sqlBool(record.locked);
  sql += ",";
  sql += sqlBool(record.cracked);
  sql += "," + sqlLiteral(record.lifecycle) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@row_version_after);";
  (void)runMysql(target, sql);
}

void recordNpcWeaponState(const MySqlTarget& target, const NpcWeaponStateRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @row_after=NULL;";
  sql += "CALL mmo_record_npc_weapon_state(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.actorKey) + "," + sqlLiteral(record.weaponState) + ",";
  sql += sqlBool(record.ready);
  sql += "," + std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@row_after);";
  (void)runMysql(target, sql);
}

void transferCharacterItem(const MySqlTarget& target, const TransferCharacterItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @target_character_id=NULL; SET @amount_transferred=NULL;";
  sql += "CALL mmo_transfer_character_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.itemUuid) + ",1)," + sqlLiteral(record.targetCharacterKey) + ",";
  sql += std::to_string(record.amount) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey);
  sql += ",@event_id,@target_character_id,@amount_transferred);";
  (void)runMysql(target, sql);
}

void lootWorldInventoryItem(const MySqlTarget& target, const LootWorldInventoryRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @source_amount_remaining=NULL; SET @amount_looted=NULL;";
  sql += "CALL mmo_loot_npc_inventory(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.sourceEntityKey) + ",UUID_TO_BIN(" + sqlLiteral(record.itemUuid) + ",1),";
  sql += std::to_string(record.amount) + "," + std::to_string(record.bagIndex) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@source_amount_remaining,@amount_looted);";
  (void)runMysql(target, sql);
}

void grantCharacterItemBySymbol(const MySqlTarget& target, const GrantCharacterItemBySymbolRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @item_id=NULL; SET @amount_granted=NULL;";
  sql += "CALL mmo_grant_character_item_by_symbol(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += std::to_string(record.itemSymbol) + "," + std::to_string(record.amount) + ",";
  sql += std::to_string(record.bagIndex) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey);
  sql += ",@event_id,@item_id,@amount_granted);";
  (void)runMysql(target, sql);
}

void pickupWorldItem(const MySqlTarget& target, const PickupWorldItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @item_id=NULL; SET @amount_picked=NULL;";
  sql += "CALL mmo_pickup_world_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.entityKey) + "," + std::to_string(record.amount) + ",";
  sql += std::to_string(record.bagIndex) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey);
  sql += ",@event_id,@item_id,@amount_picked);";
  (void)runMysql(target, sql);
}

void removeWorldItem(const MySqlTarget& target, const RemoveWorldItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @item_id=NULL;";
  sql += "CALL mmo_remove_world_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.entityKey) + "," + sqlLiteral(record.reason) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@item_id);";
  (void)runMysql(target, sql);
}

void equipCharacterItem(const MySqlTarget& target, const EquipCharacterItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL;";
  sql += "CALL mmo_equip_character_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.itemUuid) + ",1)," + sqlLiteral(record.equipmentSlot) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id);";
  (void)runMysql(target, sql);
}

void unequipCharacterItem(const MySqlTarget& target, const UnequipCharacterItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @item_id=NULL;";
  sql += "CALL mmo_unequip_character_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += sqlLiteral(record.equipmentSlot) + "," + std::to_string(record.serverTick) + ",";
  sql += sqlJson(record.dbPayload) + "," + sqlLiteral(record.idempotencyKey) + ",@event_id,@item_id);";
  (void)runMysql(target, sql);
}

void dropCharacterItem(const MySqlTarget& target, const DropCharacterItemRecord& record) {
  std::string sql;
  sql += "SET @event_id=NULL; SET @amount_remaining=NULL; SET @amount_dropped=NULL;";
  sql += "CALL mmo_drop_character_item(UUID_TO_BIN(" + sqlLiteral(record.sessionUuid) + ",1),";
  sql += "UUID_TO_BIN(" + sqlLiteral(record.itemUuid) + ",1),";
  sql += std::to_string(record.amount) + "," + sqlLiteral(record.entityKey) + ",";
  sql += sqlNullableDouble(record.posX) + "," + sqlNullableDouble(record.posY) + "," + sqlNullableDouble(record.posZ) + ",";
  sql += std::to_string(record.serverTick) + "," + sqlJson(record.dbPayload) + ",";
  sql += sqlLiteral(record.idempotencyKey) + ",@event_id,@amount_remaining,@amount_dropped);";
  (void)runMysql(target, sql);
}

} // namespace Mmo::Server
