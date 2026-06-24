#include "mmoruntimesqlite.h"

#include <Tempest/Log>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "game/gamesession.h"
#include "game/constants.h"
#include "game/gamescript.h"
#include "game/inventory.h"
#include "game/questlog.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/objects/npc.h"
#include "world/waypoint.h"
#include "world/world.h"

#if defined(OPENGOTHIC_HAVE_SQLITE)
#include <sqlite3.h>
#endif

namespace {

constexpr const char* HeroKey = "PC_HERO";

std::string worldName(GameSession& game) {
  auto world = game.world();
  if(world==nullptr)
    return {};
  return std::string(world->name());
  }

#if defined(OPENGOTHIC_HAVE_SQLITE)

bool exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if(rc==SQLITE_OK)
    return true;
  Tempest::Log::e("mmo sqlite exec failed: ", err!=nullptr ? err : sqlite3_errmsg(db));
  sqlite3_free(err);
  return false;
  }

bool bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  return sqlite3_bind_text(stmt, index, value.c_str(), int(value.size()), SQLITE_TRANSIENT)==SQLITE_OK;
  }

bool bindText(sqlite3_stmt* stmt, int index, const char* value) {
  return sqlite3_bind_text(stmt, index, value, -1, SQLITE_STATIC)==SQLITE_OK;
  }

bool bindInt(sqlite3_stmt* stmt, int index, int64_t value) {
  return sqlite3_bind_int64(stmt, index, sqlite3_int64(value))==SQLITE_OK;
  }

bool bindReal(sqlite3_stmt* stmt, int index, double value) {
  return sqlite3_bind_double(stmt, index, value)==SQLITE_OK;
  }

std::string nonEmpty(std::string_view value, const std::string& fallback) {
  if(!value.empty())
    return std::string(value);
  return fallback;
  }

struct InventoryRow final {
  std::string itemKey;
  std::string displayName;
  int64_t     symbolIndex = 0;
  int64_t     amount = 0;
  int64_t     iteratorCount = 0;
  int64_t     equipped = 0;
  int64_t     equipCount = 0;
  int64_t     slot = 255;
  int64_t     mainFlag = 0;
  int64_t     itemFlags = 0;
  int64_t     value = 0;
  int64_t     spellId = 0;
  };

struct InventoryTotal final {
  std::string displayName;
  int64_t     count = 0;
  int64_t     equippedCount = 0;
  };

struct NpcPrevious final {
  double  posX = 0.0;
  double  posY = 0.0;
  double  posZ = 0.0;
  int64_t hp = 0;
  int64_t mana = 0;
  int64_t level = 0;
  int64_t experience = 0;
  int64_t dead = 0;
  };

struct NpcStatPrevious final {
  int64_t value = 0;
  };

struct NpcAiPrevious final {
  std::string stateName;
  std::string targetKey;
  std::string relationKind;
  };

struct NpcRow final {
  std::string entityKey;
  std::string displayName;
  std::string waypoint;
  std::string aiStateName;
  std::string targetKey;
  std::string targetName;
  std::string relationKind;
  int64_t     slotId = 0;
  int64_t     persistentId = 0;
  int64_t     symbolIndex = 0;
  int64_t     scriptId = 0;
  double      posX = 0.0;
  double      posY = 0.0;
  double      posZ = 0.0;
  double      rotation = 0.0;
  int64_t     guild = 0;
  int64_t     trueGuild = 0;
  int64_t     hp = 0;
  int64_t     hpMax = 0;
  int64_t     mana = 0;
  int64_t     manaMax = 0;
  int64_t     level = 0;
  int64_t     experience = 0;
  int64_t     dead = 0;
  int64_t     player = 0;
  int64_t     aiStateFunction = -1;
  int64_t     targetSymbolIndex = -1;
  std::array<int64_t, ATR_MAX> attributes = {};
  std::array<int64_t, PROT_MAX> protections = {};
  std::array<int64_t, TALENT_MAX_G2> talentSkills = {};
  std::array<int64_t, TALENT_MAX_G2> talentValues = {};
  std::array<int64_t, TALENT_MAX_G2> hitChances = {};
  };

struct QuestPrevious final {
  int64_t status = 0;
  int64_t entryCount = 0;
  };

struct WorldItemPrevious final {
  double  posX = 0.0;
  double  posY = 0.0;
  double  posZ = 0.0;
  int64_t amount = 0;
  };

struct WorldItemRow final {
  std::string entityKey;
  std::string displayName;
  std::string visual;
  int64_t     slotId = 0;
  int64_t     persistentId = 0;
  int64_t     symbolIndex = 0;
  int64_t     scriptId = 0;
  int64_t     amount = 0;
  int64_t     mainFlag = 0;
  int64_t     itemFlags = 0;
  int64_t     value = 0;
  double      posX = 0.0;
  double      posY = 0.0;
  double      posZ = 0.0;
  };

struct MobsiPrevious final {
  int64_t state = 0;
  int64_t locked = 0;
  int64_t cracked = 0;
  };

struct MobsiRow final {
  std::string entityKey;
  std::string displayName;
  std::string tag;
  std::string focusName;
  std::string scheme;
  int64_t     slotId = 0;
  int64_t     vobId = 0;
  double      posX = 0.0;
  double      posY = 0.0;
  double      posZ = 0.0;
  int64_t     state = 0;
  int64_t     stateCount = 0;
  int64_t     stateMask = 0;
  int64_t     container = 0;
  int64_t     door = 0;
  int64_t     ladder = 0;
  int64_t     locked = 0;
  int64_t     cracked = 0;
  };

struct ScriptGlobalPrevious final {
  std::string valueText;
  };

struct ScriptGlobalRow final {
  std::string globalKey;
  std::string symbolName;
  std::string valueType;
  std::string category;
  std::string valueText;
  int64_t     symbolIndex = 0;
  int64_t     valueCount = 0;
  };

std::string questStatusLabel(int64_t status) {
  switch(status) {
    case int64_t(QuestLog::Status::Running):
      return "running";
    case int64_t(QuestLog::Status::Success):
      return "success";
    case int64_t(QuestLog::Status::Failed):
      return "failed";
    case int64_t(QuestLog::Status::Obsolete):
      return "obsolete";
    default:
      return "unknown";
    }
  }

std::string questLifecycleState(int64_t status) {
  switch(status) {
    case int64_t(QuestLog::Status::Running):
      return "in_progress";
    case int64_t(QuestLog::Status::Success):
      return "completed_success";
    case int64_t(QuestLog::Status::Failed):
      return "completed_failed";
    case int64_t(QuestLog::Status::Obsolete):
      return "obsolete";
    default:
      return "unknown";
    }
  }

std::string attributeKey(int64_t id) {
  switch(id) {
    case ATR_HITPOINTS:      return "hitpoints";
    case ATR_HITPOINTSMAX:   return "hitpoints_max";
    case ATR_MANA:           return "mana";
    case ATR_MANAMAX:        return "mana_max";
    case ATR_STRENGTH:       return "strength";
    case ATR_DEXTERITY:      return "dexterity";
    case ATR_REGENERATEHP:   return "regenerate_hp";
    case ATR_REGENERATEMANA: return "regenerate_mana";
    default:                 return "attribute:" + std::to_string(id);
    }
  }

std::string protectionKey(int64_t id) {
  switch(id) {
    case PROT_BARRIER: return "barrier";
    case PROT_BLUNT:   return "blunt";
    case PROT_EDGE:    return "edge";
    case PROT_FIRE:    return "fire";
    case PROT_FLY:     return "fly";
    case PROT_MAGIC:   return "magic";
    case PROT_POINT:   return "point";
    case PROT_FALL:    return "fall";
    default:           return "protection:" + std::to_string(id);
    }
  }

std::string talentKey(int64_t id) {
  switch(id) {
    case TALENT_UNKNOWN:          return "unknown";
    case TALENT_1H:               return "one_handed";
    case TALENT_2H:               return "two_handed";
    case TALENT_BOW:              return "bow";
    case TALENT_CROSSBOW:         return "crossbow";
    case TALENT_PICKLOCK:         return "picklock";
    case TALENT_MAGE:             return "mage";
    case TALENT_SNEAK:            return "sneak";
    case TALENT_REGENERATE:       return "regenerate";
    case TALENT_FIREMASTER:       return "firemaster";
    case TALENT_ACROBAT:          return "acrobat";
    case TALENT_PICKPOCKET:       return "pickpocket";
    case TALENT_SMITH:            return "smith";
    case TALENT_RUNES:            return "runes";
    case TALENT_ALCHEMY:          return "alchemy";
    case TALENT_TAKEANIMALTROPHY: return "take_animal_trophy";
    case TALENT_FOREIGNLANGUAGE:  return "foreign_language";
    case TALENT_WISPDETECTOR:     return "wisp_detector";
    case TALENT_C:                return "talent_c";
    case TALENT_D:                return "talent_d";
    case TALENT_E:                return "talent_e";
    default:                      return "talent:" + std::to_string(id);
    }
  }

std::string lowerAscii(std::string_view text) {
  std::string ret(text);
  for(char& c : ret)
    if('A'<=c && c<='Z')
      c = char(c - 'A' + 'a');
  return ret;
  }

std::string npcRelationKind(const NpcRow& row) {
  if(row.targetKey.empty())
    return "none";
  const std::string state = lowerAscii(row.aiStateName);
  if(state.find("follow")!=std::string::npos)
    return "following_target";
  if(state.find("escort")!=std::string::npos || state.find("guide")!=std::string::npos)
    return "escort_or_guide";
  if(state.find("talk")!=std::string::npos)
    return "talking_to_target";
  if(state.find("attack")!=std::string::npos)
    return "attacking_target";
  return "targeting";
  }

std::string npcStatKey(const std::string& entityKey, std::string_view group, int64_t id) {
  return entityKey + "|" + std::string(group) + "|" + std::to_string(id);
  }

int64_t npcSymbolIndex(Npc& npc) {
  return int64_t(npc.handle().symbol_index());
  }

std::string npcDisplayName(Npc& npc) {
  return nonEmpty(npc.displayName(), "npc:" + std::to_string(npcSymbolIndex(npc)));
  }

std::string npcEntityKey(GameSession& game, Npc& npc) {
  const auto& h = npc.handle();
  return "npc:" + worldName(game) + ":" + std::to_string(npc.persistentId()) + ":" +
         std::to_string(h.symbol_index()) + ":" + std::to_string(h.id);
  }

int64_t dialogInfoSymbol(GameScript& script, zenkit::IInfo* handle, std::string& infoName) {
  if(handle==nullptr) {
    infoName = "info:unknown";
    return -1;
    }
  for(auto& infoPtr : script.dialogInfos()) {
    if(infoPtr==nullptr || infoPtr.get()!=handle)
      continue;
    if(auto* sym = script.getVm().find_symbol_by_instance(infoPtr)) {
      infoName = nonEmpty(sym->name(), "info:" + std::to_string(sym->index()));
      return int64_t(sym->index());
      }
    }
  infoName = "info:unknown";
  return -1;
  }

int64_t dialogPermanent(const GameScript::DlgChoice& choice) {
  return choice.handle!=nullptr ? int64_t(choice.handle->permanent) : 0;
  }

int64_t dialogImportant(const GameScript::DlgChoice& choice) {
  return choice.handle!=nullptr ? int64_t(choice.handle->important) : 0;
  }

std::string scriptGlobalCategory(std::string_view name) {
  auto startsWith = [](std::string_view s, std::string_view prefix) {
    return s.size()>=prefix.size() && s.substr(0, prefix.size())==prefix;
    };

  if(startsWith(name, "DIA_") || startsWith(name, "INFO_"))
    return "dialog";
  if(startsWith(name, "MIS_") || startsWith(name, "LOG_"))
    return "quest";
  if(name.find("BOOK")!=std::string_view::npos || name.find("BUCH")!=std::string_view::npos ||
     name.find("READ")!=std::string_view::npos || name.find("LEARN")!=std::string_view::npos)
    return "knowledge";
  if(name.find("XP")!=std::string_view::npos || name.find("EXP")!=std::string_view::npos ||
     name.find("BONUS")!=std::string_view::npos)
    return "reward";
  return "script";
  }

std::string scriptGlobalTypeName(zenkit::DaedalusDataType type) {
  switch(type) {
    case zenkit::DaedalusDataType::INT:
      return "int";
    case zenkit::DaedalusDataType::FLOAT:
      return "float";
    case zenkit::DaedalusDataType::STRING:
      return "string";
    default:
      return "unknown";
    }
  }

#endif

}

struct MmoRuntimeSqlite::Impl {
  std::string path;
  uint64_t    intervalMs = 5000;
  uint64_t    untilFlush = 0;
  bool        restoreState = true;
  bool        warnedNoBackend = false;
  bool        opened = false;

#if defined(OPENGOTHIC_HAVE_SQLITE)
  sqlite3* db = nullptr;
  int64_t  sessionId = 0;
#endif
  };

MmoRuntimeSqlite::MmoRuntimeSqlite(std::string path, uint64_t intervalMs, bool restoreState)
  : impl(new Impl()) {
  impl->path         = std::move(path);
  impl->intervalMs   = std::max<uint64_t>(250, intervalMs);
  impl->untilFlush   = 0;
  impl->restoreState = restoreState;
  }

MmoRuntimeSqlite::~MmoRuntimeSqlite() {
#if defined(OPENGOTHIC_HAVE_SQLITE)
  if(impl->db!=nullptr) {
    sqlite3_close(impl->db);
    impl->db = nullptr;
    }
#endif
  }

bool MmoRuntimeSqlite::open(GameSession& game) {
  if(impl->path.empty())
    return false;

#if !defined(OPENGOTHIC_HAVE_SQLITE)
  if(!impl->warnedNoBackend) {
    Tempest::Log::e("mmo sqlite requested, but this build was compiled without SQLite3 support");
    impl->warnedNoBackend = true;
    }
  (void)game;
  return false;
#else
  if(impl->opened)
    return true;

  std::filesystem::path dbPath(impl->path);
  if(dbPath.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(dbPath.parent_path(), ec);
    if(ec)
      Tempest::Log::e("mmo sqlite failed to create directory: ", dbPath.parent_path().string());
    }

  if(sqlite3_open(impl->path.c_str(), &impl->db)!=SQLITE_OK) {
    Tempest::Log::e("mmo sqlite open failed: ", sqlite3_errmsg(impl->db));
    return false;
    }

  const char* schema = R"SQL(
    PRAGMA journal_mode=WAL;
    PRAGMA synchronous=NORMAL;
    CREATE TABLE IF NOT EXISTS runtime_schema_meta (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
    INSERT OR REPLACE INTO runtime_schema_meta(key, value) VALUES
      ('schema_name', 'opengothic_runtime_mmo'),
      ('schema_version', '10');
    CREATE TABLE IF NOT EXISTS runtime_realms (
      realm_key TEXT PRIMARY KEY,
      display_name TEXT NOT NULL DEFAULT '',
      world_name TEXT NOT NULL DEFAULT '',
      ruleset TEXT NOT NULL DEFAULT 'local-sqlite',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_accounts (
      account_key TEXT PRIMARY KEY,
      display_name TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_character_bindings (
      character_key TEXT PRIMARY KEY,
      account_key TEXT NOT NULL,
      realm_key TEXT NOT NULL,
      current_world_name TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_sessions (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      last_seen_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      target TEXT,
      world_name TEXT,
      tick_count INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS runtime_characters (
      character_key TEXT PRIMARY KEY,
      display_name TEXT NOT NULL DEFAULT '',
      world_name TEXT,
      tick_count INTEGER NOT NULL,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      hp INTEGER,
      hp_max INTEGER,
      mana INTEGER,
      mana_max INTEGER,
      level INTEGER,
      experience INTEGER,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_character_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      character_key TEXT NOT NULL,
      world_name TEXT,
      tick_count INTEGER NOT NULL,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      hp INTEGER,
      mana INTEGER,
      level INTEGER,
      experience INTEGER,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_character_inventory (
      character_key TEXT NOT NULL,
      item_key TEXT NOT NULL,
      symbol_index INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      iterator_count INTEGER NOT NULL,
      equipped INTEGER NOT NULL,
      equip_count INTEGER NOT NULL,
      slot INTEGER NOT NULL,
      main_flag INTEGER NOT NULL,
      item_flags INTEGER NOT NULL,
      value INTEGER NOT NULL,
      spell_id INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(character_key, item_key)
    );
    CREATE TABLE IF NOT EXISTS runtime_character_inventory_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      character_key TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      item_key TEXT NOT NULL,
      symbol_index INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      iterator_count INTEGER NOT NULL,
      equipped INTEGER NOT NULL,
      equip_count INTEGER NOT NULL,
      slot INTEGER NOT NULL,
      main_flag INTEGER NOT NULL,
      item_flags INTEGER NOT NULL,
      value INTEGER NOT NULL,
      spell_id INTEGER NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_character_inventory_history_tick
      ON runtime_character_inventory_history(character_key, tick_count);
    CREATE TABLE IF NOT EXISTS runtime_events (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      session_id INTEGER,
      event_type TEXT NOT NULL,
      entity_key TEXT,
      subject_key TEXT,
      world_name TEXT,
      tick_count INTEGER NOT NULL,
      value_before REAL,
      value_after REAL,
      delta REAL,
      data_text TEXT,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_events_type_tick
      ON runtime_events(event_type, tick_count);
    CREATE INDEX IF NOT EXISTS idx_runtime_events_subject
      ON runtime_events(subject_key);
  )SQL";
  if(!exec(impl->db, schema))
    return false;

  const char* schemaWorld = R"SQL(
    CREATE TABLE IF NOT EXISTS runtime_world_npcs (
      entity_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      slot_id INTEGER NOT NULL,
      persistent_id INTEGER NOT NULL,
      symbol_index INTEGER NOT NULL,
      script_id INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      guild INTEGER,
      true_guild INTEGER,
      hp INTEGER,
      hp_max INTEGER,
      mana INTEGER,
      mana_max INTEGER,
      level INTEGER,
      experience INTEGER,
      dead INTEGER NOT NULL,
      player INTEGER NOT NULL,
      waypoint TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_world_npcs_world_symbol
      ON runtime_world_npcs(world_name, symbol_index);
    CREATE INDEX IF NOT EXISTS idx_runtime_world_npcs_dead
      ON runtime_world_npcs(dead);
    CREATE TABLE IF NOT EXISTS runtime_world_npc_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      entity_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      symbol_index INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      hp INTEGER,
      mana INTEGER,
      level INTEGER,
      experience INTEGER,
      dead INTEGER NOT NULL,
      changed_fields TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_world_npc_history_entity_tick
      ON runtime_world_npc_history(entity_key, tick_count);
    CREATE TABLE IF NOT EXISTS runtime_quests (
      quest_key TEXT PRIMARY KEY,
      name TEXT NOT NULL DEFAULT '',
      section INTEGER NOT NULL,
      status INTEGER NOT NULL,
      entry_count INTEGER NOT NULL,
      entries_text TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_quest_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      quest_key TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      name TEXT NOT NULL DEFAULT '',
      section INTEGER NOT NULL,
      status INTEGER NOT NULL,
      entry_count INTEGER NOT NULL,
      entries_text TEXT NOT NULL DEFAULT '',
      changed_fields TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_known_dialogs (
      npc_symbol_index INTEGER NOT NULL,
      info_symbol_index INTEGER NOT NULL,
      npc_symbol_name TEXT NOT NULL DEFAULT '',
      info_symbol_name TEXT NOT NULL DEFAULT '',
      first_seen_tick INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(npc_symbol_index, info_symbol_index)
    );
    CREATE TABLE IF NOT EXISTS runtime_known_dialog_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      npc_symbol_index INTEGER NOT NULL,
      info_symbol_index INTEGER NOT NULL,
      npc_symbol_name TEXT NOT NULL DEFAULT '',
      info_symbol_name TEXT NOT NULL DEFAULT '',
      tick_count INTEGER NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_dialog_catalog (
      info_symbol_index INTEGER PRIMARY KEY,
      info_symbol_name TEXT NOT NULL DEFAULT '',
      npc_symbol_index INTEGER NOT NULL,
      npc_symbol_name TEXT NOT NULL DEFAULT '',
      description TEXT NOT NULL DEFAULT '',
      sort_order INTEGER NOT NULL,
      important INTEGER NOT NULL,
      permanent INTEGER NOT NULL,
      trade INTEGER NOT NULL,
      information_symbol_index INTEGER NOT NULL,
      information_symbol_name TEXT NOT NULL DEFAULT '',
      condition_symbol_index INTEGER NOT NULL,
      condition_symbol_name TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_dialog_catalog_npc
      ON runtime_dialog_catalog(npc_symbol_index, sort_order);
    CREATE TABLE IF NOT EXISTS runtime_dialog_choice_snapshots (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      session_id INTEGER NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      player_key TEXT NOT NULL DEFAULT 'PC_HERO',
      npc_key TEXT NOT NULL,
      npc_symbol_index INTEGER NOT NULL,
      npc_display_name TEXT NOT NULL DEFAULT '',
      phase TEXT NOT NULL DEFAULT '',
      include_important INTEGER NOT NULL,
      choice_count INTEGER NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_dialog_choice_snapshots_tick
      ON runtime_dialog_choice_snapshots(tick_count, npc_symbol_index);
    CREATE TABLE IF NOT EXISTS runtime_dialog_choice_rows (
      snapshot_id INTEGER NOT NULL,
      choice_index INTEGER NOT NULL,
      info_symbol_index INTEGER NOT NULL,
      info_symbol_name TEXT NOT NULL DEFAULT '',
      script_function_index INTEGER NOT NULL,
      script_function_name TEXT NOT NULL DEFAULT '',
      title TEXT NOT NULL DEFAULT '',
      sort_order INTEGER NOT NULL,
      trade INTEGER NOT NULL,
      important INTEGER NOT NULL,
      permanent INTEGER NOT NULL,
      PRIMARY KEY(snapshot_id, choice_index)
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_dialog_choice_rows_info
      ON runtime_dialog_choice_rows(info_symbol_index);
    CREATE TABLE IF NOT EXISTS runtime_dialog_selections (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      session_id INTEGER NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      player_key TEXT NOT NULL DEFAULT 'PC_HERO',
      npc_key TEXT NOT NULL,
      npc_symbol_index INTEGER NOT NULL,
      npc_display_name TEXT NOT NULL DEFAULT '',
      phase TEXT NOT NULL DEFAULT '',
      info_symbol_index INTEGER NOT NULL,
      info_symbol_name TEXT NOT NULL DEFAULT '',
      script_function_index INTEGER NOT NULL,
      script_function_name TEXT NOT NULL DEFAULT '',
      title TEXT NOT NULL DEFAULT '',
      sort_order INTEGER NOT NULL,
      trade INTEGER NOT NULL,
      important INTEGER NOT NULL,
      permanent INTEGER NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_dialog_selections_tick
      ON runtime_dialog_selections(tick_count, npc_symbol_index);
    CREATE TABLE IF NOT EXISTS runtime_world_items (
      entity_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      slot_id INTEGER NOT NULL,
      persistent_id INTEGER NOT NULL,
      symbol_index INTEGER NOT NULL,
      script_id INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      visual TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      main_flag INTEGER NOT NULL,
      item_flags INTEGER NOT NULL,
      value INTEGER NOT NULL,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_world_items_symbol
      ON runtime_world_items(world_name, symbol_index);
    CREATE TABLE IF NOT EXISTS runtime_world_item_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      entity_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      symbol_index INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      changed_fields TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_world_mobsi (
      entity_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      slot_id INTEGER NOT NULL,
      vob_id INTEGER NOT NULL,
      tag TEXT NOT NULL DEFAULT '',
      focus_name TEXT NOT NULL DEFAULT '',
      display_name TEXT NOT NULL DEFAULT '',
      scheme TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      state INTEGER NOT NULL,
      state_count INTEGER NOT NULL,
      state_mask INTEGER NOT NULL,
      container INTEGER NOT NULL,
      door INTEGER NOT NULL,
      ladder INTEGER NOT NULL,
      locked INTEGER NOT NULL,
      cracked INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_world_mobsi_kind
      ON runtime_world_mobsi(container, door, ladder);
    CREATE TABLE IF NOT EXISTS runtime_world_mobsi_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      entity_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      state INTEGER NOT NULL,
      locked INTEGER NOT NULL,
      cracked INTEGER NOT NULL,
      changed_fields TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_world_mobsi_inventory (
      owner_key TEXT NOT NULL,
      item_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      owner_display_name TEXT NOT NULL DEFAULT '',
      symbol_index INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      iterator_count INTEGER NOT NULL,
      value INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(owner_key, item_key)
    );
    CREATE TABLE IF NOT EXISTS runtime_script_globals (
      global_key TEXT PRIMARY KEY,
      symbol_index INTEGER NOT NULL,
      symbol_name TEXT NOT NULL DEFAULT '',
      value_type TEXT NOT NULL DEFAULT '',
      category TEXT NOT NULL DEFAULT '',
      value_count INTEGER NOT NULL,
      value_text TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_script_globals_category
      ON runtime_script_globals(category);
    CREATE TABLE IF NOT EXISTS runtime_script_global_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      global_key TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      symbol_index INTEGER NOT NULL,
      symbol_name TEXT NOT NULL DEFAULT '',
      value_type TEXT NOT NULL DEFAULT '',
      category TEXT NOT NULL DEFAULT '',
      value_count INTEGER NOT NULL,
      value_before TEXT NOT NULL DEFAULT '',
      value_after TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    UPDATE runtime_characters SET display_name='' WHERE display_name IS NULL;
    UPDATE runtime_character_inventory SET display_name='' WHERE display_name IS NULL;
    UPDATE runtime_character_inventory_history SET display_name='' WHERE display_name IS NULL;
    UPDATE runtime_quests SET entries_text='(no entries)' WHERE entries_text IS NULL OR entries_text='';
    UPDATE runtime_quest_history SET entries_text='(no entries)' WHERE entries_text IS NULL OR entries_text='';
    UPDATE runtime_script_globals SET value_text='(empty)' WHERE value_text IS NULL OR value_text='';
    UPDATE runtime_script_global_history SET value_before='(empty)' WHERE value_before IS NULL OR value_before='';
    UPDATE runtime_script_global_history SET value_after='(empty)' WHERE value_after IS NULL OR value_after='';
    UPDATE runtime_world_mobsi
       SET focus_name=CASE
         WHEN tag IS NOT NULL AND tag!='' THEN tag
         WHEN display_name IS NOT NULL AND display_name!='' THEN display_name
         ELSE 'mobsi:' || vob_id
       END
     WHERE focus_name IS NULL OR focus_name='';
    UPDATE runtime_dialog_catalog SET info_symbol_name='info:' || info_symbol_index WHERE info_symbol_name IS NULL OR info_symbol_name='';
    UPDATE runtime_dialog_catalog SET npc_symbol_name='npc:' || npc_symbol_index WHERE npc_symbol_name IS NULL OR npc_symbol_name='';
    UPDATE runtime_dialog_catalog SET description='(no description)' WHERE description IS NULL OR description='';
    UPDATE runtime_dialog_catalog SET information_symbol_name='function:' || information_symbol_index WHERE information_symbol_name IS NULL OR information_symbol_name='';
    UPDATE runtime_dialog_catalog SET condition_symbol_name='(no condition)' WHERE condition_symbol_name IS NULL OR condition_symbol_name='';
    UPDATE runtime_dialog_choice_rows SET title='(no title)' WHERE title IS NULL OR title='';
    UPDATE runtime_dialog_choice_rows SET info_symbol_name='info:' || info_symbol_index WHERE info_symbol_name IS NULL OR info_symbol_name='';
    UPDATE runtime_dialog_choice_rows SET script_function_name='function:' || script_function_index WHERE script_function_name IS NULL OR script_function_name='';
    UPDATE runtime_dialog_selections SET title='(no title)' WHERE title IS NULL OR title='';
    UPDATE runtime_dialog_selections SET info_symbol_name='info:' || info_symbol_index WHERE info_symbol_name IS NULL OR info_symbol_name='';
    UPDATE runtime_dialog_selections SET script_function_name='function:' || script_function_index WHERE script_function_name IS NULL OR script_function_name='';
  )SQL";
  if(!exec(impl->db, schemaWorld))
    return false;

  const char* schemaNpcState = R"SQL(
    CREATE TABLE IF NOT EXISTS runtime_npc_stats (
      entity_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      player INTEGER NOT NULL,
      stat_group TEXT NOT NULL,
      stat_id INTEGER NOT NULL,
      stat_key TEXT NOT NULL DEFAULT '',
      value INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(entity_key, stat_group, stat_id)
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_stats_lookup
      ON runtime_npc_stats(world_name, stat_group, stat_key);
    CREATE TABLE IF NOT EXISTS runtime_npc_stat_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      entity_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      player INTEGER NOT NULL,
      stat_group TEXT NOT NULL,
      stat_id INTEGER NOT NULL,
      stat_key TEXT NOT NULL DEFAULT '',
      value_before INTEGER,
      value_after INTEGER NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_stat_history_entity_tick
      ON runtime_npc_stat_history(entity_key, tick_count);
    CREATE TABLE IF NOT EXISTS runtime_npc_ai_state (
      entity_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      player INTEGER NOT NULL,
      ai_state_function INTEGER NOT NULL,
      ai_state_name TEXT NOT NULL DEFAULT '',
      target_key TEXT NOT NULL DEFAULT '',
      target_symbol_index INTEGER NOT NULL,
      target_display_name TEXT NOT NULL DEFAULT '',
      relation_kind TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_ai_state_relation
      ON runtime_npc_ai_state(world_name, relation_kind, target_key);
    CREATE TABLE IF NOT EXISTS runtime_npc_ai_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      entity_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      player INTEGER NOT NULL,
      ai_state_function INTEGER NOT NULL,
      ai_state_name TEXT NOT NULL DEFAULT '',
      target_key TEXT NOT NULL DEFAULT '',
      target_symbol_index INTEGER NOT NULL,
      target_display_name TEXT NOT NULL DEFAULT '',
      relation_kind TEXT NOT NULL DEFAULT '',
      changed_fields TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_ai_history_entity_tick
      ON runtime_npc_ai_history(entity_key, tick_count);
    UPDATE runtime_npc_stats SET display_name='npc:' || entity_key WHERE display_name IS NULL OR display_name='';
    UPDATE runtime_npc_stats SET stat_key=stat_group || ':' || stat_id WHERE stat_key IS NULL OR stat_key='';
    UPDATE runtime_npc_stat_history SET display_name='npc:' || entity_key WHERE display_name IS NULL OR display_name='';
    UPDATE runtime_npc_stat_history SET stat_key=stat_group || ':' || stat_id WHERE stat_key IS NULL OR stat_key='';
    UPDATE runtime_npc_ai_state SET display_name='npc:' || entity_key WHERE display_name IS NULL OR display_name='';
    UPDATE runtime_npc_ai_state SET ai_state_name='(no state)' WHERE ai_state_name IS NULL OR ai_state_name='';
    UPDATE runtime_npc_ai_state SET relation_kind='none' WHERE relation_kind IS NULL OR relation_kind='';
    UPDATE runtime_npc_ai_history SET display_name='npc:' || entity_key WHERE display_name IS NULL OR display_name='';
    UPDATE runtime_npc_ai_history SET ai_state_name='(no state)' WHERE ai_state_name IS NULL OR ai_state_name='';
    UPDATE runtime_npc_ai_history SET relation_kind='none' WHERE relation_kind IS NULL OR relation_kind='';
  )SQL";
  if(!exec(impl->db, schemaNpcState))
    return false;

  const char* schemaViews = R"SQL(
    DROP VIEW IF EXISTS v_runtime_quest_state;
    DROP VIEW IF EXISTS v_runtime_quest_lifecycle;
    DROP VIEW IF EXISTS v_runtime_dialog_state;
    DROP VIEW IF EXISTS v_runtime_dialog_availability;
    DROP VIEW IF EXISTS v_runtime_dialog_choice_timeline;
    DROP VIEW IF EXISTS v_runtime_dialog_selection_timeline;
    DROP VIEW IF EXISTS v_runtime_npc_character_sheet;
    DROP VIEW IF EXISTS v_runtime_player_stats;
    DROP VIEW IF EXISTS v_runtime_npc_follow_relations;
    CREATE VIEW IF NOT EXISTS v_runtime_character_sheet AS
      SELECT c.character_key, c.display_name, b.account_key, b.realm_key,
             c.world_name, c.tick_count,
             c.pos_x, c.pos_y, c.pos_z, c.rotation,
             c.hp, c.hp_max, c.mana, c.mana_max, c.level, c.experience,
             c.updated_at
        FROM runtime_characters c
        LEFT JOIN runtime_character_bindings b ON b.character_key = c.character_key;
    CREATE VIEW IF NOT EXISTS v_runtime_character_inventory_totals AS
      SELECT character_key, symbol_index, display_name,
             SUM(iterator_count) AS total_count,
             SUM(CASE WHEN equipped != 0 THEN iterator_count ELSE 0 END) AS equipped_count,
             SUM(CASE WHEN equipped = 0 THEN iterator_count ELSE 0 END) AS bag_count,
             MAX(value) AS unit_value
        FROM runtime_character_inventory
       GROUP BY character_key, symbol_index, display_name;
    CREATE VIEW IF NOT EXISTS v_runtime_character_equipment AS
      SELECT character_key, symbol_index, display_name, iterator_count, slot, value, spell_id
        FROM runtime_character_inventory
       WHERE equipped != 0;
    CREATE VIEW IF NOT EXISTS v_runtime_world_population AS
      SELECT world_name,
             COUNT(*) AS npc_count,
             SUM(CASE WHEN player != 0 THEN 1 ELSE 0 END) AS player_count,
             SUM(CASE WHEN dead != 0 THEN 1 ELSE 0 END) AS dead_count,
             SUM(CASE WHEN dead = 0 THEN 1 ELSE 0 END) AS alive_count
        FROM runtime_world_npcs
       GROUP BY world_name;
    CREATE VIEW IF NOT EXISTS v_runtime_dead_npcs AS
      SELECT entity_key, world_name, display_name, symbol_index, hp, hp_max,
             pos_x, pos_y, pos_z, updated_at
        FROM runtime_world_npcs
       WHERE dead != 0;
    CREATE VIEW IF NOT EXISTS v_runtime_npc_character_sheet AS
      SELECT n.entity_key,
             n.world_name,
             n.display_name,
             n.player,
             n.guild,
             n.true_guild,
             n.level,
             n.experience,
             n.dead,
             n.hp,
             n.hp_max,
             n.mana,
             n.mana_max,
             MAX(CASE WHEN s.stat_group='attribute' AND s.stat_key='strength' THEN s.value END) AS strength,
             MAX(CASE WHEN s.stat_group='attribute' AND s.stat_key='dexterity' THEN s.value END) AS dexterity,
             ai.ai_state_name,
             ai.target_display_name,
             ai.relation_kind,
             n.updated_at
        FROM runtime_world_npcs n
        LEFT JOIN runtime_npc_stats s ON s.entity_key = n.entity_key
        LEFT JOIN runtime_npc_ai_state ai ON ai.entity_key = n.entity_key
       GROUP BY n.entity_key;
    CREATE VIEW IF NOT EXISTS v_runtime_player_stats AS
      SELECT entity_key, display_name, stat_group, stat_key, value, updated_at
        FROM runtime_npc_stats
       WHERE player != 0;
    CREATE VIEW IF NOT EXISTS v_runtime_npc_follow_relations AS
      SELECT entity_key,
             world_name,
             display_name,
             player,
             ai_state_name,
             target_key,
             target_display_name,
             relation_kind,
             tick_count,
             updated_at
        FROM runtime_npc_ai_state
       WHERE relation_kind != 'none';
    CREATE VIEW IF NOT EXISTS v_runtime_world_item_totals AS
      SELECT world_name, symbol_index, display_name,
             COUNT(*) AS stack_count,
             SUM(amount) AS total_amount,
             MAX(value) AS unit_value
        FROM runtime_world_items
       GROUP BY world_name, symbol_index, display_name;
    CREATE VIEW IF NOT EXISTS v_runtime_container_inventory AS
      SELECT owner_key, owner_display_name, symbol_index, display_name,
             SUM(iterator_count) AS total_count,
             MAX(value) AS unit_value
        FROM runtime_world_mobsi_inventory
       GROUP BY owner_key, owner_display_name, symbol_index, display_name;
    CREATE VIEW IF NOT EXISTS v_runtime_interactives AS
      SELECT entity_key, world_name, display_name, focus_name, scheme,
             state, state_count, state_mask,
             container, door, ladder, locked, cracked,
             pos_x, pos_y, pos_z, updated_at
        FROM runtime_world_mobsi;
    CREATE VIEW IF NOT EXISTS v_runtime_quest_state AS
      SELECT quest_key,
             name,
             section,
             CASE section
               WHEN 0 THEN 'mission'
               WHEN 1 THEN 'note'
               ELSE 'unknown'
             END AS section_label,
             status,
             CASE status
               WHEN 1 THEN 'running'
               WHEN 2 THEN 'success'
               WHEN 3 THEN 'failed'
               WHEN 4 THEN 'obsolete'
               ELSE 'unknown'
             END AS status_label,
             CASE status
               WHEN 1 THEN 'in_progress'
               WHEN 2 THEN 'completed_success'
               WHEN 3 THEN 'completed_failed'
               WHEN 4 THEN 'obsolete'
               ELSE 'unknown'
             END AS lifecycle_state,
             entry_count,
             entries_text,
             updated_at
        FROM runtime_quests;
    CREATE VIEW IF NOT EXISTS v_runtime_quest_lifecycle AS
      SELECT lifecycle_state, COUNT(*) AS quest_count
        FROM v_runtime_quest_state
       GROUP BY lifecycle_state;
    CREATE VIEW IF NOT EXISTS v_runtime_dialog_state AS
      SELECT c.npc_symbol_index,
             c.info_symbol_index,
             c.npc_symbol_name,
             c.info_symbol_name,
             c.description,
             c.sort_order,
             c.important,
             c.permanent,
             c.trade,
             c.information_symbol_index,
             c.information_symbol_name,
             c.condition_symbol_index,
             c.condition_symbol_name,
             CASE WHEN k.info_symbol_index IS NULL THEN 0 ELSE 1 END AS known,
             k.first_seen_tick,
             CASE
               WHEN k.info_symbol_index IS NOT NULL AND c.permanent = 0 THEN 'consumed_hidden'
               WHEN k.info_symbol_index IS NOT NULL AND c.permanent != 0 THEN 'repeatable_known'
               WHEN k.info_symbol_index IS NULL AND c.permanent != 0 THEN 'repeatable_not_seen'
               ELSE 'one_shot_not_seen'
             END AS availability_state,
             c.updated_at
        FROM runtime_dialog_catalog c
        LEFT JOIN runtime_known_dialogs k
          ON k.npc_symbol_index = c.npc_symbol_index
         AND k.info_symbol_index = c.info_symbol_index;
    CREATE VIEW IF NOT EXISTS v_runtime_dialog_availability AS
      SELECT availability_state, COUNT(*) AS dialog_count
        FROM v_runtime_dialog_state
       GROUP BY availability_state;
    CREATE VIEW IF NOT EXISTS v_runtime_dialog_choice_timeline AS
      SELECT s.id AS snapshot_id,
             s.session_id,
             s.world_name,
             s.tick_count,
             s.npc_key,
             s.npc_symbol_index,
             s.npc_display_name,
             s.phase,
             s.include_important,
             s.choice_count,
             r.choice_index,
             r.info_symbol_index,
             r.info_symbol_name,
             r.script_function_index,
             r.script_function_name,
             r.title,
             r.sort_order,
             r.trade,
             r.important,
             r.permanent,
             s.created_at
        FROM runtime_dialog_choice_snapshots s
        LEFT JOIN runtime_dialog_choice_rows r ON r.snapshot_id = s.id;
    CREATE VIEW IF NOT EXISTS v_runtime_dialog_selection_timeline AS
      SELECT id,
             session_id,
             world_name,
             tick_count,
             npc_key,
             npc_symbol_index,
             npc_display_name,
             phase,
             info_symbol_index,
             info_symbol_name,
             script_function_index,
             script_function_name,
             title,
             sort_order,
             trade,
             important,
             permanent,
             created_at
        FROM runtime_dialog_selections;
    CREATE VIEW IF NOT EXISTS v_runtime_script_global_categories AS
      SELECT category, value_type, COUNT(*) AS global_count
        FROM runtime_script_globals
       GROUP BY category, value_type;
    CREATE VIEW IF NOT EXISTS v_runtime_event_counts AS
      SELECT event_type, COUNT(*) AS event_count,
             MIN(tick_count) AS first_tick,
             MAX(tick_count) AS last_tick,
             SUM(delta) AS delta_sum
        FROM runtime_events
       GROUP BY event_type;
    CREATE VIEW IF NOT EXISTS v_runtime_persistence_summary AS
      SELECT 'characters' AS area, COUNT(*) AS row_count FROM runtime_characters
      UNION ALL SELECT 'character_inventory', COUNT(*) FROM runtime_character_inventory
      UNION ALL SELECT 'world_npcs', COUNT(*) FROM runtime_world_npcs
      UNION ALL SELECT 'world_items', COUNT(*) FROM runtime_world_items
      UNION ALL SELECT 'npc_stats', COUNT(*) FROM runtime_npc_stats
      UNION ALL SELECT 'npc_ai_state', COUNT(*) FROM runtime_npc_ai_state
      UNION ALL SELECT 'world_mobsi', COUNT(*) FROM runtime_world_mobsi
      UNION ALL SELECT 'mobsi_inventory', COUNT(*) FROM runtime_world_mobsi_inventory
      UNION ALL SELECT 'quests', COUNT(*) FROM runtime_quests
      UNION ALL SELECT 'known_dialogs', COUNT(*) FROM runtime_known_dialogs
      UNION ALL SELECT 'dialog_catalog', COUNT(*) FROM runtime_dialog_catalog
      UNION ALL SELECT 'dialog_choice_snapshots', COUNT(*) FROM runtime_dialog_choice_snapshots
      UNION ALL SELECT 'dialog_selections', COUNT(*) FROM runtime_dialog_selections
      UNION ALL SELECT 'script_globals', COUNT(*) FROM runtime_script_globals
      UNION ALL SELECT 'events', COUNT(*) FROM runtime_events;
  )SQL";
  if(!exec(impl->db, schemaViews))
    return false;

  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(impl->db,
                        "INSERT INTO runtime_sessions(target, world_name, tick_count) VALUES(?1, ?2, ?3)",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, "local-sqlite");
    bindText(stmt, 2, worldName(game));
    bindInt(stmt, 3, int64_t(game.tickCount()));
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite failed to insert session: ", sqlite3_errmsg(impl->db));
    else
      impl->sessionId = int64_t(sqlite3_last_insert_rowid(impl->db));
    sqlite3_finalize(stmt);
    }

  if(sqlite3_prepare_v2(impl->db,
                        "INSERT INTO runtime_realms(realm_key, display_name, world_name, updated_at) "
                        "VALUES(?1, ?2, ?3, CURRENT_TIMESTAMP) "
                        "ON CONFLICT(realm_key) DO UPDATE SET world_name=excluded.world_name, updated_at=CURRENT_TIMESTAMP",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, "local-g2notr");
    bindText(stmt, 2, "Local Gothic II NotR");
    bindText(stmt, 3, worldName(game));
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite failed to upsert realm: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }

  if(sqlite3_prepare_v2(impl->db,
                        "INSERT INTO runtime_accounts(account_key, display_name, updated_at) "
                        "VALUES(?1, ?2, CURRENT_TIMESTAMP) "
                        "ON CONFLICT(account_key) DO UPDATE SET display_name=excluded.display_name, updated_at=CURRENT_TIMESTAMP",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, "local-account");
    bindText(stmt, 2, "Local Player");
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite failed to upsert account: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }

  impl->opened = true;
  Tempest::Log::i("mmo sqlite runtime opened: ", impl->path);

  if(impl->restoreState) {
    Npc* pl = game.player();
    if(pl!=nullptr && sqlite3_prepare_v2(impl->db,
        "SELECT world_name,pos_x,pos_y,pos_z,hp,mana FROM runtime_characters WHERE character_key=?1",
        -1, &stmt, nullptr)==SQLITE_OK) {
      bindText(stmt, 1, HeroKey);
      if(sqlite3_step(stmt)==SQLITE_ROW) {
        const auto* rawWorld = sqlite3_column_text(stmt, 0);
        const std::string savedWorld = rawWorld!=nullptr ? reinterpret_cast<const char*>(rawWorld) : "";
        const std::string currentWorld = worldName(game);
        if(savedWorld==currentWorld) {
          const float x = float(sqlite3_column_double(stmt, 1));
          const float y = float(sqlite3_column_double(stmt, 2));
          const float z = float(sqlite3_column_double(stmt, 3));
          const int hp   = sqlite3_column_int(stmt, 4);
          const int mana = sqlite3_column_int(stmt, 5);
          pl->setPosition(x, y, z);
          pl->changeAttribute(ATR_HITPOINTS, hp - pl->attribute(ATR_HITPOINTS), false);
          pl->changeAttribute(ATR_MANA,      mana - pl->attribute(ATR_MANA),      false);
          Tempest::Log::i("mmo sqlite restored hero state from runtime DB");
          }
        }
      sqlite3_finalize(stmt);
      }
    }

  flush(game);
  return true;
#endif
  }

void MmoRuntimeSqlite::recordDialogChoices(GameSession& game, Npc& player, Npc& npc,
                                           const std::vector<GameScript::DlgChoice>& choices,
                                           std::string_view phase, bool includeImportant) {
#if defined(OPENGOTHIC_HAVE_SQLITE)
  (void)player;
  if(impl->db==nullptr || !impl->opened)
    return;
  GameScript* script = game.script();
  if(script==nullptr)
    return;

  const std::string world = worldName(game);
  const std::string npcKey = npcEntityKey(game, npc);
  const int64_t npcSymbol = npcSymbolIndex(npc);
  const std::string npcName = npcDisplayName(npc);
  const std::string phaseText = nonEmpty(phase, "choices");

  sqlite3_stmt* snapshotStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db,
      "INSERT INTO runtime_dialog_choice_snapshots("
      "session_id, world_name, tick_count, player_key, npc_key, npc_symbol_index, npc_display_name, "
      "phase, include_important, choice_count"
      ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
      -1, &snapshotStmt, nullptr)!=SQLITE_OK) {
    Tempest::Log::e("mmo sqlite dialog choices snapshot prepare failed: ", sqlite3_errmsg(impl->db));
    return;
    }

  bindInt (snapshotStmt, 1, impl->sessionId);
  bindText(snapshotStmt, 2, world);
  bindInt (snapshotStmt, 3, int64_t(game.tickCount()));
  bindText(snapshotStmt, 4, HeroKey);
  bindText(snapshotStmt, 5, npcKey);
  bindInt (snapshotStmt, 6, npcSymbol);
  bindText(snapshotStmt, 7, npcName);
  bindText(snapshotStmt, 8, phaseText);
  bindInt (snapshotStmt, 9, includeImportant ? 1 : 0);
  bindInt (snapshotStmt,10, int64_t(choices.size()));
  if(sqlite3_step(snapshotStmt)!=SQLITE_DONE) {
    Tempest::Log::e("mmo sqlite dialog choices snapshot insert failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(snapshotStmt);
    return;
    }
  const int64_t snapshotId = int64_t(sqlite3_last_insert_rowid(impl->db));
  sqlite3_finalize(snapshotStmt);

  sqlite3_stmt* rowStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db,
      "INSERT INTO runtime_dialog_choice_rows("
      "snapshot_id, choice_index, info_symbol_index, info_symbol_name, "
      "script_function_index, script_function_name, title, sort_order, trade, important, permanent"
      ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)",
      -1, &rowStmt, nullptr)!=SQLITE_OK) {
    Tempest::Log::e("mmo sqlite dialog choices row prepare failed: ", sqlite3_errmsg(impl->db));
    return;
    }

  for(size_t i=0; i<choices.size(); ++i) {
    const auto& choice = choices[i];
    std::string infoName;
    const int64_t infoSymbol = dialogInfoSymbol(*script, choice.handle, infoName);
    const int64_t functionIndex = int64_t(choice.scriptFn);
    std::string functionName = "function:" + std::to_string(functionIndex);
    if(auto* sym = script->findSymbol(size_t(functionIndex)))
      functionName = nonEmpty(sym->name(), functionName);

    sqlite3_reset(rowStmt);
    sqlite3_clear_bindings(rowStmt);
    bindInt (rowStmt, 1, snapshotId);
    bindInt (rowStmt, 2, int64_t(i));
    bindInt (rowStmt, 3, infoSymbol);
    bindText(rowStmt, 4, infoName);
    bindInt (rowStmt, 5, functionIndex);
    bindText(rowStmt, 6, functionName);
    bindText(rowStmt, 7, nonEmpty(choice.title, "(no title)"));
    bindInt (rowStmt, 8, int64_t(choice.sort));
    bindInt (rowStmt, 9, choice.isTrade ? 1 : 0);
    bindInt (rowStmt,10, dialogImportant(choice));
    bindInt (rowStmt,11, dialogPermanent(choice));
    if(sqlite3_step(rowStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite dialog choices row insert failed: ", sqlite3_errmsg(impl->db));
    }
  sqlite3_finalize(rowStmt);
#else
  (void)game;
  (void)player;
  (void)npc;
  (void)choices;
  (void)phase;
  (void)includeImportant;
#endif
  }

void MmoRuntimeSqlite::recordDialogSelection(GameSession& game, Npc& player, Npc& npc,
                                             const GameScript::DlgChoice& choice,
                                             std::string_view phase) {
#if defined(OPENGOTHIC_HAVE_SQLITE)
  (void)player;
  if(impl->db==nullptr || !impl->opened)
    return;
  GameScript* script = game.script();
  if(script==nullptr)
    return;

  std::string infoName;
  const int64_t infoSymbol = dialogInfoSymbol(*script, choice.handle, infoName);
  const int64_t functionIndex = int64_t(choice.scriptFn);
  std::string functionName = "function:" + std::to_string(functionIndex);
  if(auto* sym = script->findSymbol(size_t(functionIndex)))
    functionName = nonEmpty(sym->name(), functionName);

  const std::string world = worldName(game);
  const std::string npcKey = npcEntityKey(game, npc);
  const int64_t npcSymbol = npcSymbolIndex(npc);
  const std::string npcName = npcDisplayName(npc);
  const std::string title = nonEmpty(choice.title, "(no title)");
  const std::string phaseText = nonEmpty(phase, "select");

  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(impl->db,
      "INSERT INTO runtime_dialog_selections("
      "session_id, world_name, tick_count, player_key, npc_key, npc_symbol_index, npc_display_name, "
      "phase, info_symbol_index, info_symbol_name, script_function_index, script_function_name, "
      "title, sort_order, trade, important, permanent"
      ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)",
      -1, &stmt, nullptr)!=SQLITE_OK) {
    Tempest::Log::e("mmo sqlite dialog selection prepare failed: ", sqlite3_errmsg(impl->db));
    return;
    }
  bindInt (stmt, 1, impl->sessionId);
  bindText(stmt, 2, world);
  bindInt (stmt, 3, int64_t(game.tickCount()));
  bindText(stmt, 4, HeroKey);
  bindText(stmt, 5, npcKey);
  bindInt (stmt, 6, npcSymbol);
  bindText(stmt, 7, npcName);
  bindText(stmt, 8, phaseText);
  bindInt (stmt, 9, infoSymbol);
  bindText(stmt,10, infoName);
  bindInt (stmt,11, functionIndex);
  bindText(stmt,12, functionName);
  bindText(stmt,13, title);
  bindInt (stmt,14, int64_t(choice.sort));
  bindInt (stmt,15, choice.isTrade ? 1 : 0);
  bindInt (stmt,16, dialogImportant(choice));
  bindInt (stmt,17, dialogPermanent(choice));
  if(sqlite3_step(stmt)!=SQLITE_DONE)
    Tempest::Log::e("mmo sqlite dialog selection insert failed: ", sqlite3_errmsg(impl->db));
  sqlite3_finalize(stmt);

  if(sqlite3_prepare_v2(impl->db,
      "INSERT INTO runtime_events(session_id, event_type, entity_key, subject_key, tick_count, before_value, after_value, delta, data_text) "
      "VALUES(?1, 'dialog_selected', ?2, ?3, ?4, 0, 1, 1, ?5)",
      -1, &stmt, nullptr)==SQLITE_OK) {
    bindInt (stmt, 1, impl->sessionId);
    bindText(stmt, 2, npcKey);
    bindText(stmt, 3, infoName);
    bindInt (stmt, 4, int64_t(game.tickCount()));
    bindText(stmt, 5, title);
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite dialog selection event insert failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }
#else
  (void)game;
  (void)player;
  (void)npc;
  (void)choice;
  (void)phase;
#endif
  }

void MmoRuntimeSqlite::tick(GameSession& game, uint64_t dt) {
  if(impl->path.empty())
    return;
  if(!impl->opened && !open(game))
    return;
  if(dt>=impl->untilFlush) {
    impl->untilFlush = impl->intervalMs;
    flush(game);
    return;
    }
  impl->untilFlush -= dt;
  }

void MmoRuntimeSqlite::flush(GameSession& game) {
#if !defined(OPENGOTHIC_HAVE_SQLITE)
  (void)game;
#else
  if(impl->db==nullptr)
    return;
  Npc* pl = game.player();
  if(pl==nullptr)
    return;

  exec(impl->db, "BEGIN IMMEDIATE TRANSACTION");

  const auto pos = pl->position();
  const std::string name = nonEmpty(pl->displayName(), HeroKey);
  const std::string world = worldName(game);
  sqlite3_stmt* stmt = nullptr;

  auto insertEvent = [&](const char* eventType,
                         const std::string& entityKey,
                         const std::string& subjectKey,
                         double before,
                         double after,
                         const std::string& dataText) {
    sqlite3_stmt* eventStmt = nullptr;
    const char* sql = R"SQL(
      INSERT INTO runtime_events(
        session_id, event_type, entity_key, subject_key, world_name, tick_count,
        value_before, value_after, delta, data_text
      )
      VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
    )SQL";
    if(sqlite3_prepare_v2(impl->db, sql, -1, &eventStmt, nullptr)!=SQLITE_OK) {
      Tempest::Log::e("mmo sqlite event prepare failed: ", sqlite3_errmsg(impl->db));
      return;
      }
    bindInt (eventStmt, 1, impl->sessionId);
    bindText(eventStmt, 2, eventType);
    bindText(eventStmt, 3, entityKey);
    bindText(eventStmt, 4, subjectKey);
    bindText(eventStmt, 5, world);
    bindInt (eventStmt, 6, int64_t(game.tickCount()));
    bindReal(eventStmt, 7, before);
    bindReal(eventStmt, 8, after);
    bindReal(eventStmt, 9, after - before);
    bindText(eventStmt,10, dataText);
    if(sqlite3_step(eventStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite event insert failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(eventStmt);
    };

  struct HeroPrevious final {
    bool    valid = false;
    double  posX = 0.0;
    double  posY = 0.0;
    double  posZ = 0.0;
    int64_t hp = 0;
    int64_t mana = 0;
    int64_t level = 0;
    int64_t experience = 0;
    };
  HeroPrevious prevHero;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT pos_x,pos_y,pos_z,hp,mana,level,experience FROM runtime_characters WHERE character_key=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    if(sqlite3_step(stmt)==SQLITE_ROW) {
      prevHero.valid      = true;
      prevHero.posX       = sqlite3_column_double(stmt, 0);
      prevHero.posY       = sqlite3_column_double(stmt, 1);
      prevHero.posZ       = sqlite3_column_double(stmt, 2);
      prevHero.hp         = sqlite3_column_int64(stmt, 3);
      prevHero.mana       = sqlite3_column_int64(stmt, 4);
      prevHero.level      = sqlite3_column_int64(stmt, 5);
      prevHero.experience = sqlite3_column_int64(stmt, 6);
      }
    sqlite3_finalize(stmt);
    }

  std::map<int64_t, InventoryTotal> previousInventory;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT symbol_index, COALESCE(MAX(display_name), ''), SUM(iterator_count), "
                        "SUM(CASE WHEN equipped != 0 THEN iterator_count ELSE 0 END) "
                        "FROM runtime_character_inventory WHERE character_key=?1 GROUP BY symbol_index",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      InventoryTotal total;
      const auto* rawName = sqlite3_column_text(stmt, 1);
      total.displayName   = rawName!=nullptr ? reinterpret_cast<const char*>(rawName) : "";
      total.count         = sqlite3_column_int64(stmt, 2);
      total.equippedCount = sqlite3_column_int64(stmt, 3);
      previousInventory[sqlite3_column_int64(stmt, 0)] = std::move(total);
      }
    sqlite3_finalize(stmt);
    }

  std::vector<InventoryRow> currentRows;
  std::map<int64_t, InventoryTotal> currentInventory;
  auto invIt = pl->inventory().iterator(Inventory::T_Inventory);
  while(invIt.isValid()) {
    const Item& item = *invIt;
    InventoryRow row;
    row.symbolIndex   = int64_t(item.clsId());
    row.itemKey       = std::to_string(row.symbolIndex) + ":" +
                        std::to_string(uint32_t(invIt.slot())) + ":" +
                        std::to_string(invIt.isEquipped() ? 1 : 0);
    row.displayName   = nonEmpty(item.displayName(), "item:" + std::to_string(row.symbolIndex));
    row.amount        = int64_t(item.count());
    row.iteratorCount = int64_t(invIt.count());
    row.equipped      = invIt.isEquipped() ? 1 : 0;
    row.equipCount    = invIt.isEquipped() ? int64_t(item.equipCount()) : 0;
    row.slot          = int64_t(invIt.slot());
    row.mainFlag      = int64_t(item.mainFlag());
    row.itemFlags     = int64_t(item.itemFlag());
    row.value         = int64_t(item.cost());
    row.spellId       = int64_t(item.spellId());

    auto& total = currentInventory[row.symbolIndex];
    total.displayName = row.displayName;
    total.count += row.iteratorCount;
    if(row.equipped!=0)
      total.equippedCount += row.iteratorCount;
    currentRows.emplace_back(std::move(row));
    ++invIt;
    }

  std::map<std::string, NpcPrevious> previousNpcs;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,pos_x,pos_y,pos_z,hp,mana,level,experience,dead FROM runtime_world_npcs",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      NpcPrevious prev;
      prev.posX       = sqlite3_column_double(stmt, 1);
      prev.posY       = sqlite3_column_double(stmt, 2);
      prev.posZ       = sqlite3_column_double(stmt, 3);
      prev.hp         = sqlite3_column_int64(stmt, 4);
      prev.mana       = sqlite3_column_int64(stmt, 5);
      prev.level      = sqlite3_column_int64(stmt, 6);
      prev.experience = sqlite3_column_int64(stmt, 7);
      prev.dead       = sqlite3_column_int64(stmt, 8);
      previousNpcs[reinterpret_cast<const char*>(rawKey)] = prev;
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousNpcs = !previousNpcs.empty();

  std::map<std::string, NpcStatPrevious> previousNpcStats;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,stat_group,stat_id,value FROM runtime_npc_stats",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      const auto* rawGroup = sqlite3_column_text(stmt, 1);
      if(rawKey==nullptr || rawGroup==nullptr)
        continue;
      NpcStatPrevious prev;
      prev.value = sqlite3_column_int64(stmt, 3);
      previousNpcStats[npcStatKey(reinterpret_cast<const char*>(rawKey),
                                  reinterpret_cast<const char*>(rawGroup),
                                  sqlite3_column_int64(stmt, 2))] = prev;
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousNpcStats = !previousNpcStats.empty();

  std::map<std::string, NpcAiPrevious> previousNpcAi;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,ai_state_name,target_key,relation_kind FROM runtime_npc_ai_state",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      const auto* rawState = sqlite3_column_text(stmt, 1);
      const auto* rawTarget = sqlite3_column_text(stmt, 2);
      const auto* rawRelation = sqlite3_column_text(stmt, 3);
      NpcAiPrevious prev;
      prev.stateName    = rawState!=nullptr ? reinterpret_cast<const char*>(rawState) : "";
      prev.targetKey    = rawTarget!=nullptr ? reinterpret_cast<const char*>(rawTarget) : "";
      prev.relationKind = rawRelation!=nullptr ? reinterpret_cast<const char*>(rawRelation) : "";
      previousNpcAi[reinterpret_cast<const char*>(rawKey)] = std::move(prev);
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousNpcAi = !previousNpcAi.empty();

  std::vector<NpcRow> currentNpcs;
  if(auto* wrld = game.world()) {
    for(uint32_t i=0; i<wrld->npcCount(); ++i) {
      Npc* npc = wrld->npcById(i);
      if(npc==nullptr)
        continue;
      const auto& h = npc->handle();
      const auto npcPos = npc->position();
      NpcRow row;
      row.slotId       = i;
      row.persistentId = npc->persistentId();
      row.symbolIndex  = int64_t(h.symbol_index());
      row.scriptId     = h.id;
      row.entityKey    = "npc:" + world + ":" + std::to_string(row.persistentId) + ":" +
                         std::to_string(row.symbolIndex) + ":" + std::to_string(row.scriptId);
      row.displayName  = nonEmpty(npc->displayName(), "npc:" + std::to_string(row.symbolIndex));
      row.posX         = npcPos.x;
      row.posY         = npcPos.y;
      row.posZ         = npcPos.z;
      row.rotation     = npc->rotation();
      row.guild        = npc->guild();
      row.trueGuild    = npc->trueGuild();
      row.hp           = npc->attribute(ATR_HITPOINTS);
      row.hpMax        = npc->attribute(ATR_HITPOINTSMAX);
      row.mana         = npc->attribute(ATR_MANA);
      row.manaMax      = npc->attribute(ATR_MANAMAX);
      row.level        = npc->level();
      row.experience   = npc->experience();
      row.dead         = npc->isDead() ? 1 : 0;
      row.player       = npc->isPlayer() ? 1 : 0;
      row.aiStateFunction = int64_t(npc->currentAiStateFunction());
      row.aiStateName     = nonEmpty(npc->currentAiStateName(), "(no state)");
      if(auto* wp = npc->currentWayPoint())
        row.waypoint = std::string(wp->name);
      for(int64_t a=0; a<ATR_MAX; ++a)
        row.attributes[size_t(a)] = npc->attribute(Attribute(a));
      for(int64_t p=0; p<PROT_MAX; ++p)
        row.protections[size_t(p)] = npc->protection(Protection(p));
      for(int64_t t=0; t<TALENT_MAX_G2; ++t) {
        row.talentSkills[size_t(t)] = npc->talentSkill(Talent(t));
        row.talentValues[size_t(t)] = npc->talentValue(Talent(t));
        row.hitChances[size_t(t)]   = npc->hitChance(Talent(t));
        }
      if(Npc* target = npc->target()) {
        row.targetSymbolIndex = int64_t(target->handle().symbol_index());
        row.targetName = npcDisplayName(*target);
        const auto& th = target->handle();
        row.targetKey = "npc:" + world + ":" + std::to_string(target->persistentId()) + ":" +
                        std::to_string(th.symbol_index()) + ":" + std::to_string(th.id);
        }
      row.relationKind = npcRelationKind(row);
      currentNpcs.emplace_back(std::move(row));
      }
    }

  std::map<std::string, QuestPrevious> previousQuests;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT quest_key,status,entry_count FROM runtime_quests",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      QuestPrevious prev;
      prev.status     = sqlite3_column_int64(stmt, 1);
      prev.entryCount = sqlite3_column_int64(stmt, 2);
      previousQuests[reinterpret_cast<const char*>(rawKey)] = prev;
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousQuests = !previousQuests.empty();

  std::set<std::pair<int64_t,int64_t>> previousDialogs;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT npc_symbol_index,info_symbol_index FROM runtime_known_dialogs",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW)
      previousDialogs.emplace(sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1));
    sqlite3_finalize(stmt);
    }

  std::map<std::string, WorldItemPrevious> previousWorldItems;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,pos_x,pos_y,pos_z,amount FROM runtime_world_items",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      WorldItemPrevious prev;
      prev.posX   = sqlite3_column_double(stmt, 1);
      prev.posY   = sqlite3_column_double(stmt, 2);
      prev.posZ   = sqlite3_column_double(stmt, 3);
      prev.amount = sqlite3_column_int64(stmt, 4);
      previousWorldItems[reinterpret_cast<const char*>(rawKey)] = prev;
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousWorldItems = !previousWorldItems.empty();

  std::map<std::string, MobsiPrevious> previousMobsi;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,state,locked,cracked FROM runtime_world_mobsi",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      MobsiPrevious prev;
      prev.state   = sqlite3_column_int64(stmt, 1);
      prev.locked  = sqlite3_column_int64(stmt, 2);
      prev.cracked = sqlite3_column_int64(stmt, 3);
      previousMobsi[reinterpret_cast<const char*>(rawKey)] = prev;
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousMobsi = !previousMobsi.empty();

  std::map<std::string, ScriptGlobalPrevious> previousGlobals;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT global_key,value_text FROM runtime_script_globals",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      const auto* rawVal = sqlite3_column_text(stmt, 1);
      if(rawKey==nullptr)
        continue;
      ScriptGlobalPrevious prev;
      prev.valueText = rawVal!=nullptr ? reinterpret_cast<const char*>(rawVal) : "";
      previousGlobals[reinterpret_cast<const char*>(rawKey)] = std::move(prev);
      }
    sqlite3_finalize(stmt);
    }

  std::vector<WorldItemRow> currentWorldItems;
  std::vector<MobsiRow> currentMobsi;
  std::vector<ScriptGlobalRow> currentGlobals;
  if(auto* wrld = game.world()) {
    for(uint32_t i=0;; ++i) {
      Item* item = wrld->itmById(i);
      if(item==nullptr)
        break;
      const auto& h = item->handle();
      const auto itemPos = item->position();
      WorldItemRow row;
      row.slotId       = i;
      row.persistentId = item->persistentId();
      row.symbolIndex  = int64_t(h.symbol_index());
      row.scriptId     = h.id;
      row.entityKey    = "world_item:" + world + ":" + std::to_string(row.persistentId) + ":" +
                         std::to_string(row.symbolIndex) + ":" + std::to_string(i);
      row.displayName  = nonEmpty(item->displayName(), "item:" + std::to_string(row.symbolIndex));
      row.visual       = std::string(h.visual);
      row.amount       = int64_t(item->count());
      row.mainFlag     = int64_t(h.main_flag);
      row.itemFlags    = int64_t(h.flags);
      row.value        = int64_t(h.value);
      row.posX         = itemPos.x;
      row.posY         = itemPos.y;
      row.posZ         = itemPos.z;
      currentWorldItems.emplace_back(std::move(row));
      }

    for(uint32_t i=0;; ++i) {
      Interactive* mobsi = wrld->mobsiById(i);
      if(mobsi==nullptr)
        break;
      const auto mobPos = mobsi->position();
      MobsiRow row;
      row.slotId      = i;
      row.vobId       = mobsi->getId();
      row.tag         = std::string(mobsi->tag());
      row.focusName   = nonEmpty(mobsi->focusName(), nonEmpty(mobsi->tag(), "mobsi:" + std::to_string(row.vobId)));
      row.displayName = nonEmpty(mobsi->displayName(), "mobsi:" + std::to_string(row.vobId));
      row.scheme      = std::string(mobsi->schemeName());
      row.entityKey   = "mobsi:" + world + ":" + std::to_string(row.slotId) + ":" +
                        std::to_string(row.vobId) + ":" + row.focusName;
      row.posX        = mobPos.x;
      row.posY        = mobPos.y;
      row.posZ        = mobPos.z;
      row.state       = mobsi->stateId();
      row.stateCount  = mobsi->stateCount();
      row.stateMask   = mobsi->stateMask();
      row.container   = mobsi->isContainer() ? 1 : 0;
      row.door        = mobsi->isDoor() ? 1 : 0;
      row.ladder      = mobsi->isLadder() ? 1 : 0;
      row.locked      = mobsi->isLocked() ? 1 : 0;
      row.cracked     = mobsi->isCracked() ? 1 : 0;
      currentMobsi.emplace_back(std::move(row));
      }
    }

  if(auto* script = game.script()) {
    for(size_t i=0; i<script->symbolsCount(); ++i) {
      auto* sym = script->findSymbol(i);
      if(sym==nullptr || sym->is_member() || sym->is_const() || sym->count()==0)
        continue;
      const auto type = sym->type();
      if(type!=zenkit::DaedalusDataType::INT &&
         type!=zenkit::DaedalusDataType::FLOAT &&
         type!=zenkit::DaedalusDataType::STRING)
        continue;

      ScriptGlobalRow row;
      row.symbolIndex = int64_t(i);
      row.symbolName  = nonEmpty(sym->name(), "symbol:" + std::to_string(i));
      row.valueType   = scriptGlobalTypeName(type);
      row.category    = scriptGlobalCategory(row.symbolName);
      row.valueCount  = int64_t(sym->count());
      row.globalKey   = "global:" + std::to_string(i) + ":" + row.symbolName;
      for(uint32_t j=0; j<sym->count(); ++j) {
        if(j!=0)
          row.valueText += "|";
        switch(type) {
          case zenkit::DaedalusDataType::INT:
            row.valueText += std::to_string(sym->get_int(uint16_t(j)));
            break;
          case zenkit::DaedalusDataType::FLOAT:
            row.valueText += std::to_string(sym->get_float(uint16_t(j)));
            break;
          case zenkit::DaedalusDataType::STRING:
            row.valueText += std::string(sym->get_string(uint16_t(j)));
            break;
          default:
            break;
          }
        }
      if(row.valueText.empty())
        row.valueText = "(empty)";
      currentGlobals.emplace_back(std::move(row));
      }
    }

  if(prevHero.valid) {
    const double moved = std::abs(prevHero.posX - pos.x) +
                         std::abs(prevHero.posY - pos.y) +
                         std::abs(prevHero.posZ - pos.z);
    if(moved>1.0)
      insertEvent("character_moved", HeroKey, HeroKey, 0.0, moved, "position_delta");
    if(prevHero.hp!=pl->attribute(ATR_HITPOINTS))
      insertEvent("character_hp_changed", HeroKey, HeroKey, double(prevHero.hp), double(pl->attribute(ATR_HITPOINTS)), "hp");
    if(prevHero.mana!=pl->attribute(ATR_MANA))
      insertEvent("character_mana_changed", HeroKey, HeroKey, double(prevHero.mana), double(pl->attribute(ATR_MANA)), "mana");
    if(prevHero.level!=pl->level())
      insertEvent("character_level_changed", HeroKey, HeroKey, double(prevHero.level), double(pl->level()), "level");
    if(prevHero.experience!=pl->experience())
      insertEvent("character_experience_changed", HeroKey, HeroKey, double(prevHero.experience), double(pl->experience()), "experience");
    }

  for(auto& current : currentInventory) {
    const auto prev = previousInventory.find(current.first);
    if(prev==previousInventory.end()) {
      insertEvent("item_added", HeroKey, std::to_string(current.first), 0.0, double(current.second.count), current.second.displayName);
      if(current.second.equippedCount>0)
        insertEvent("item_equipped", HeroKey, std::to_string(current.first), 0.0, double(current.second.equippedCount), current.second.displayName);
      continue;
      }
    if(prev->second.count!=current.second.count)
      insertEvent("item_quantity_changed", HeroKey, std::to_string(current.first), double(prev->second.count), double(current.second.count), current.second.displayName);
    if(prev->second.equippedCount!=current.second.equippedCount) {
      insertEvent(current.second.equippedCount>prev->second.equippedCount ? "item_equipped" : "item_unequipped",
                  HeroKey, std::to_string(current.first),
                  double(prev->second.equippedCount), double(current.second.equippedCount),
                  current.second.displayName);
      }
    }
  for(auto& previous : previousInventory) {
    if(currentInventory.find(previous.first)!=currentInventory.end())
      continue;
    insertEvent("item_removed", HeroKey, std::to_string(previous.first), double(previous.second.count), 0.0, previous.second.displayName);
    }

  for(const NpcRow& row : currentNpcs) {
    const auto prev = previousNpcs.find(row.entityKey);
    if(prev==previousNpcs.end())
      continue;
    const double moved = std::abs(prev->second.posX - row.posX) +
                         std::abs(prev->second.posY - row.posY) +
                         std::abs(prev->second.posZ - row.posZ);
    if(moved>100.0 && row.player==0)
      insertEvent("npc_moved", row.entityKey, std::to_string(row.symbolIndex), 0.0, moved, row.displayName);
    if(prev->second.hp!=row.hp && row.player==0)
      insertEvent("npc_hp_changed", row.entityKey, std::to_string(row.symbolIndex), double(prev->second.hp), double(row.hp), row.displayName);
    if(prev->second.dead==0 && row.dead!=0 && row.player==0)
      insertEvent("npc_killed", row.entityKey, std::to_string(row.symbolIndex), 0.0, 1.0, row.displayName);
    if(prev->second.experience!=row.experience && row.player==0)
      insertEvent("npc_experience_changed", row.entityKey, std::to_string(row.symbolIndex), double(prev->second.experience), double(row.experience), row.displayName);
    }

  std::set<std::string> currentWorldItemKeys;
  for(const WorldItemRow& row : currentWorldItems) {
    currentWorldItemKeys.insert(row.entityKey);
    const auto prev = previousWorldItems.find(row.entityKey);
    if(prev==previousWorldItems.end()) {
      if(hadPreviousWorldItems)
        insertEvent("world_item_spawned", row.entityKey, std::to_string(row.symbolIndex), 0.0, double(row.amount), row.displayName);
      continue;
      }
    const double moved = std::abs(prev->second.posX - row.posX) +
                         std::abs(prev->second.posY - row.posY) +
                         std::abs(prev->second.posZ - row.posZ);
    if(moved>1.0)
      insertEvent("world_item_moved", row.entityKey, std::to_string(row.symbolIndex), 0.0, moved, row.displayName);
    if(prev->second.amount!=row.amount)
      insertEvent("world_item_amount_changed", row.entityKey, std::to_string(row.symbolIndex), double(prev->second.amount), double(row.amount), row.displayName);
    }
  for(const auto& prev : previousWorldItems) {
    if(currentWorldItemKeys.find(prev.first)!=currentWorldItemKeys.end())
      continue;
    if(!hadPreviousWorldItems)
      continue;
    insertEvent("world_item_removed", prev.first, prev.first, double(prev.second.amount), 0.0, "removed");
    }

  for(const MobsiRow& row : currentMobsi) {
    const auto prev = previousMobsi.find(row.entityKey);
    if(prev==previousMobsi.end())
      continue;
    if(prev->second.state!=row.state)
      insertEvent("mobsi_state_changed", row.entityKey, row.entityKey, double(prev->second.state), double(row.state), row.displayName);
    if(prev->second.locked!=row.locked)
      insertEvent("mobsi_lock_changed", row.entityKey, row.entityKey, double(prev->second.locked), double(row.locked), row.displayName);
    if(prev->second.cracked!=row.cracked)
      insertEvent("mobsi_cracked_changed", row.entityKey, row.entityKey, double(prev->second.cracked), double(row.cracked), row.displayName);
    }

  for(const ScriptGlobalRow& row : currentGlobals) {
    const auto prev = previousGlobals.find(row.globalKey);
    if(prev==previousGlobals.end())
      continue;
    if(row.category!="script" && prev->second.valueText!=row.valueText)
      insertEvent("script_global_changed", HeroKey, row.globalKey, 0.0, 1.0, row.category + ":" + row.symbolName);
    }

  const char* upsert = R"SQL(
    INSERT INTO runtime_characters(
      character_key, display_name, world_name, tick_count,
      pos_x, pos_y, pos_z, rotation,
      hp, hp_max, mana, mana_max, level, experience, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, CURRENT_TIMESTAMP)
    ON CONFLICT(character_key) DO UPDATE SET
      display_name=excluded.display_name,
      world_name=excluded.world_name,
      tick_count=excluded.tick_count,
      pos_x=excluded.pos_x,
      pos_y=excluded.pos_y,
      pos_z=excluded.pos_z,
      rotation=excluded.rotation,
      hp=excluded.hp,
      hp_max=excluded.hp_max,
      mana=excluded.mana,
      mana_max=excluded.mana_max,
      level=excluded.level,
      experience=excluded.experience,
      updated_at=CURRENT_TIMESTAMP
  )SQL";
  if(sqlite3_prepare_v2(impl->db, upsert, -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    bindText(stmt, 2, name);
    bindText(stmt, 3, world);
    bindInt (stmt, 4, int64_t(game.tickCount()));
    bindReal(stmt, 5, pos.x);
    bindReal(stmt, 6, pos.y);
    bindReal(stmt, 7, pos.z);
    bindReal(stmt, 8, pl->rotation());
    bindInt (stmt, 9, pl->attribute(ATR_HITPOINTS));
    bindInt (stmt,10, pl->attribute(ATR_HITPOINTSMAX));
    bindInt (stmt,11, pl->attribute(ATR_MANA));
    bindInt (stmt,12, pl->attribute(ATR_MANAMAX));
    bindInt (stmt,13, pl->level());
    bindInt (stmt,14, pl->experience());
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite upsert hero failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }

  if(sqlite3_prepare_v2(impl->db,
                        "INSERT INTO runtime_character_bindings(character_key, account_key, realm_key, current_world_name, updated_at) "
                        "VALUES(?1, ?2, ?3, ?4, CURRENT_TIMESTAMP) "
                        "ON CONFLICT(character_key) DO UPDATE SET "
                        "account_key=excluded.account_key, realm_key=excluded.realm_key, "
                        "current_world_name=excluded.current_world_name, updated_at=CURRENT_TIMESTAMP",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    bindText(stmt, 2, "local-account");
    bindText(stmt, 3, "local-g2notr");
    bindText(stmt, 4, world);
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite upsert character binding failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }

  const char* history = R"SQL(
    INSERT INTO runtime_character_history(
      character_key, world_name, tick_count, pos_x, pos_y, pos_z,
      rotation, hp, mana, level, experience
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)
  )SQL";
  if(sqlite3_prepare_v2(impl->db, history, -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    bindText(stmt, 2, world);
    bindInt (stmt, 3, int64_t(game.tickCount()));
    bindReal(stmt, 4, pos.x);
    bindReal(stmt, 5, pos.y);
    bindReal(stmt, 6, pos.z);
    bindReal(stmt, 7, pl->rotation());
    bindInt (stmt, 8, pl->attribute(ATR_HITPOINTS));
    bindInt (stmt, 9, pl->attribute(ATR_MANA));
    bindInt (stmt,10, pl->level());
    bindInt (stmt,11, pl->experience());
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite insert history failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }

  if(sqlite3_prepare_v2(impl->db,
                        "DELETE FROM runtime_character_inventory WHERE character_key=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite clear inventory failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }

  const char* invInsert = R"SQL(
    INSERT INTO runtime_character_inventory(
      character_key, item_key, symbol_index, display_name,
      amount, iterator_count, equipped, equip_count, slot,
      main_flag, item_flags, value, spell_id, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, CURRENT_TIMESTAMP)
  )SQL";
  const char* invHistory = R"SQL(
    INSERT INTO runtime_character_inventory_history(
      character_key, tick_count, item_key, symbol_index, display_name,
      amount, iterator_count, equipped, equip_count, slot,
      main_flag, item_flags, value, spell_id
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)
  )SQL";

  sqlite3_stmt* invStmt = nullptr;
  sqlite3_stmt* histStmt = nullptr;
  const bool invPrepared = sqlite3_prepare_v2(impl->db, invInsert, -1, &invStmt, nullptr)==SQLITE_OK;
  const bool histPrepared = sqlite3_prepare_v2(impl->db, invHistory, -1, &histStmt, nullptr)==SQLITE_OK;
  if(!invPrepared)
    Tempest::Log::e("mmo sqlite inventory prepare failed: ", sqlite3_errmsg(impl->db));
  if(!histPrepared)
    Tempest::Log::e("mmo sqlite inventory history prepare failed: ", sqlite3_errmsg(impl->db));

  for(const InventoryRow& row : currentRows) {
    if(invStmt!=nullptr) {
      sqlite3_reset(invStmt);
      sqlite3_clear_bindings(invStmt);
      bindText(invStmt, 1, HeroKey);
      bindText(invStmt, 2, row.itemKey);
      bindInt (invStmt, 3, row.symbolIndex);
      bindText(invStmt, 4, row.displayName);
      bindInt (invStmt, 5, row.amount);
      bindInt (invStmt, 6, row.iteratorCount);
      bindInt (invStmt, 7, row.equipped);
      bindInt (invStmt, 8, row.equipCount);
      bindInt (invStmt, 9, row.slot);
      bindInt (invStmt,10, row.mainFlag);
      bindInt (invStmt,11, row.itemFlags);
      bindInt (invStmt,12, row.value);
      bindInt (invStmt,13, row.spellId);
      if(sqlite3_step(invStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite inventory insert failed: ", sqlite3_errmsg(impl->db));
      }

    if(histStmt!=nullptr) {
      sqlite3_reset(histStmt);
      sqlite3_clear_bindings(histStmt);
      bindText(histStmt, 1, HeroKey);
      bindInt (histStmt, 2, int64_t(game.tickCount()));
      bindText(histStmt, 3, row.itemKey);
      bindInt (histStmt, 4, row.symbolIndex);
      bindText(histStmt, 5, row.displayName);
      bindInt (histStmt, 6, row.amount);
      bindInt (histStmt, 7, row.iteratorCount);
      bindInt (histStmt, 8, row.equipped);
      bindInt (histStmt, 9, row.equipCount);
      bindInt (histStmt,10, row.slot);
      bindInt (histStmt,11, row.mainFlag);
      bindInt (histStmt,12, row.itemFlags);
      bindInt (histStmt,13, row.value);
      bindInt (histStmt,14, row.spellId);
      if(sqlite3_step(histStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite inventory history insert failed: ", sqlite3_errmsg(impl->db));
      }
    }
  if(invStmt!=nullptr)
    sqlite3_finalize(invStmt);
  if(histStmt!=nullptr)
    sqlite3_finalize(histStmt);

  const char* npcUpsert = R"SQL(
    INSERT INTO runtime_world_npcs(
      entity_key, world_name, tick_count, slot_id, persistent_id, symbol_index, script_id,
      display_name, pos_x, pos_y, pos_z, rotation, guild, true_guild,
      hp, hp_max, mana, mana_max, level, experience, dead, player, waypoint, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, CURRENT_TIMESTAMP)
    ON CONFLICT(entity_key) DO UPDATE SET
      world_name=excluded.world_name,
      tick_count=excluded.tick_count,
      slot_id=excluded.slot_id,
      persistent_id=excluded.persistent_id,
      symbol_index=excluded.symbol_index,
      script_id=excluded.script_id,
      display_name=excluded.display_name,
      pos_x=excluded.pos_x,
      pos_y=excluded.pos_y,
      pos_z=excluded.pos_z,
      rotation=excluded.rotation,
      guild=excluded.guild,
      true_guild=excluded.true_guild,
      hp=excluded.hp,
      hp_max=excluded.hp_max,
      mana=excluded.mana,
      mana_max=excluded.mana_max,
      level=excluded.level,
      experience=excluded.experience,
      dead=excluded.dead,
      player=excluded.player,
      waypoint=excluded.waypoint,
      updated_at=CURRENT_TIMESTAMP
  )SQL";
  const char* npcHistory = R"SQL(
    INSERT INTO runtime_world_npc_history(
      entity_key, world_name, tick_count, symbol_index, display_name,
      pos_x, pos_y, pos_z, hp, mana, level, experience, dead, changed_fields
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)
  )SQL";

  sqlite3_stmt* npcStmt = nullptr;
  sqlite3_stmt* npcHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, npcUpsert, -1, &npcStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, npcHistory, -1, &npcHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc history prepare failed: ", sqlite3_errmsg(impl->db));

  for(const NpcRow& row : currentNpcs) {
    if(npcStmt!=nullptr) {
      sqlite3_reset(npcStmt);
      sqlite3_clear_bindings(npcStmt);
      bindText(npcStmt, 1, row.entityKey);
      bindText(npcStmt, 2, world);
      bindInt (npcStmt, 3, int64_t(game.tickCount()));
      bindInt (npcStmt, 4, row.slotId);
      bindInt (npcStmt, 5, row.persistentId);
      bindInt (npcStmt, 6, row.symbolIndex);
      bindInt (npcStmt, 7, row.scriptId);
      bindText(npcStmt, 8, row.displayName);
      bindReal(npcStmt, 9, row.posX);
      bindReal(npcStmt,10, row.posY);
      bindReal(npcStmt,11, row.posZ);
      bindReal(npcStmt,12, row.rotation);
      bindInt (npcStmt,13, row.guild);
      bindInt (npcStmt,14, row.trueGuild);
      bindInt (npcStmt,15, row.hp);
      bindInt (npcStmt,16, row.hpMax);
      bindInt (npcStmt,17, row.mana);
      bindInt (npcStmt,18, row.manaMax);
      bindInt (npcStmt,19, row.level);
      bindInt (npcStmt,20, row.experience);
      bindInt (npcStmt,21, row.dead);
      bindInt (npcStmt,22, row.player);
      bindText(npcStmt,23, row.waypoint);
      if(sqlite3_step(npcStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite npc upsert failed: ", sqlite3_errmsg(impl->db));
      }

    std::string changed;
    const auto prev = previousNpcs.find(row.entityKey);
    if(prev==previousNpcs.end()) {
      if(hadPreviousNpcs)
        changed = "spawned";
      } else {
      const double moved = std::abs(prev->second.posX - row.posX) +
                           std::abs(prev->second.posY - row.posY) +
                           std::abs(prev->second.posZ - row.posZ);
      if(moved>100.0)
        changed += "pos,";
      if(prev->second.hp!=row.hp)
        changed += "hp,";
      if(prev->second.mana!=row.mana)
        changed += "mana,";
      if(prev->second.level!=row.level)
        changed += "level,";
      if(prev->second.experience!=row.experience)
        changed += "experience,";
      if(prev->second.dead!=row.dead)
        changed += "dead,";
      if(!changed.empty() && changed.back()==',')
        changed.pop_back();
      }

    if(!changed.empty() && npcHistStmt!=nullptr) {
      sqlite3_reset(npcHistStmt);
      sqlite3_clear_bindings(npcHistStmt);
      bindText(npcHistStmt, 1, row.entityKey);
      bindText(npcHistStmt, 2, world);
      bindInt (npcHistStmt, 3, int64_t(game.tickCount()));
      bindInt (npcHistStmt, 4, row.symbolIndex);
      bindText(npcHistStmt, 5, row.displayName);
      bindReal(npcHistStmt, 6, row.posX);
      bindReal(npcHistStmt, 7, row.posY);
      bindReal(npcHistStmt, 8, row.posZ);
      bindInt (npcHistStmt, 9, row.hp);
      bindInt (npcHistStmt,10, row.mana);
      bindInt (npcHistStmt,11, row.level);
      bindInt (npcHistStmt,12, row.experience);
      bindInt (npcHistStmt,13, row.dead);
      bindText(npcHistStmt,14, changed);
      if(sqlite3_step(npcHistStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite npc history insert failed: ", sqlite3_errmsg(impl->db));
      }
    }
  if(npcStmt!=nullptr)
    sqlite3_finalize(npcStmt);
  if(npcHistStmt!=nullptr)
    sqlite3_finalize(npcHistStmt);

  exec(impl->db, "DELETE FROM runtime_npc_stats WHERE world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))");
  const char* npcStatInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_npc_stats(
      entity_key, world_name, tick_count, display_name, player,
      stat_group, stat_id, stat_key, value, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, CURRENT_TIMESTAMP)
  )SQL";
  const char* npcStatHistory = R"SQL(
    INSERT INTO runtime_npc_stat_history(
      entity_key, world_name, tick_count, display_name, player,
      stat_group, stat_id, stat_key, value_before, value_after
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
  )SQL";
  sqlite3_stmt* npcStatStmt = nullptr;
  sqlite3_stmt* npcStatHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, npcStatInsert, -1, &npcStatStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc stat prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, npcStatHistory, -1, &npcStatHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc stat history prepare failed: ", sqlite3_errmsg(impl->db));

  auto writeNpcStat = [&](const NpcRow& row, std::string_view group, int64_t id, const std::string& key, int64_t value) {
    if(npcStatStmt!=nullptr) {
      sqlite3_reset(npcStatStmt);
      sqlite3_clear_bindings(npcStatStmt);
      bindText(npcStatStmt, 1, row.entityKey);
      bindText(npcStatStmt, 2, world);
      bindInt (npcStatStmt, 3, int64_t(game.tickCount()));
      bindText(npcStatStmt, 4, row.displayName);
      bindInt (npcStatStmt, 5, row.player);
      bindText(npcStatStmt, 6, std::string(group));
      bindInt (npcStatStmt, 7, id);
      bindText(npcStatStmt, 8, key);
      bindInt (npcStatStmt, 9, value);
      if(sqlite3_step(npcStatStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite npc stat insert failed: ", sqlite3_errmsg(impl->db));
      }

    const std::string previousKey = npcStatKey(row.entityKey, group, id);
    const auto prev = previousNpcStats.find(previousKey);
    const bool changed = prev!=previousNpcStats.end() && prev->second.value!=value;
    const bool spawned = hadPreviousNpcStats && prev==previousNpcStats.end();
    if((changed || spawned) && npcStatHistStmt!=nullptr) {
      sqlite3_reset(npcStatHistStmt);
      sqlite3_clear_bindings(npcStatHistStmt);
      bindText(npcStatHistStmt, 1, row.entityKey);
      bindText(npcStatHistStmt, 2, world);
      bindInt (npcStatHistStmt, 3, int64_t(game.tickCount()));
      bindText(npcStatHistStmt, 4, row.displayName);
      bindInt (npcStatHistStmt, 5, row.player);
      bindText(npcStatHistStmt, 6, std::string(group));
      bindInt (npcStatHistStmt, 7, id);
      bindText(npcStatHistStmt, 8, key);
      if(prev!=previousNpcStats.end())
        bindInt(npcStatHistStmt, 9, prev->second.value);
      else
        sqlite3_bind_null(npcStatHistStmt, 9);
      bindInt (npcStatHistStmt,10, value);
      if(sqlite3_step(npcStatHistStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite npc stat history insert failed: ", sqlite3_errmsg(impl->db));
      }
    };

  for(const NpcRow& row : currentNpcs) {
    for(int64_t a=0; a<ATR_MAX; ++a)
      writeNpcStat(row, "attribute", a, attributeKey(a), row.attributes[size_t(a)]);
    for(int64_t p=0; p<PROT_MAX; ++p)
      writeNpcStat(row, "protection", p, protectionKey(p), row.protections[size_t(p)]);
    for(int64_t t=0; t<TALENT_MAX_G2; ++t) {
      writeNpcStat(row, "talent_skill", t, talentKey(t), row.talentSkills[size_t(t)]);
      writeNpcStat(row, "talent_value", t, talentKey(t), row.talentValues[size_t(t)]);
      writeNpcStat(row, "hit_chance", t, talentKey(t), row.hitChances[size_t(t)]);
      }
    }
  if(npcStatStmt!=nullptr)
    sqlite3_finalize(npcStatStmt);
  if(npcStatHistStmt!=nullptr)
    sqlite3_finalize(npcStatHistStmt);

  exec(impl->db, "DELETE FROM runtime_npc_ai_state WHERE world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))");
  const char* npcAiInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_npc_ai_state(
      entity_key, world_name, tick_count, display_name, player,
      ai_state_function, ai_state_name, target_key, target_symbol_index, target_display_name,
      relation_kind, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, CURRENT_TIMESTAMP)
  )SQL";
  const char* npcAiHistory = R"SQL(
    INSERT INTO runtime_npc_ai_history(
      entity_key, world_name, tick_count, display_name, player,
      ai_state_function, ai_state_name, target_key, target_symbol_index, target_display_name,
      relation_kind, changed_fields
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)
  )SQL";
  sqlite3_stmt* npcAiStmt = nullptr;
  sqlite3_stmt* npcAiHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, npcAiInsert, -1, &npcAiStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc ai prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, npcAiHistory, -1, &npcAiHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc ai history prepare failed: ", sqlite3_errmsg(impl->db));
  for(const NpcRow& row : currentNpcs) {
    if(npcAiStmt!=nullptr) {
      sqlite3_reset(npcAiStmt);
      sqlite3_clear_bindings(npcAiStmt);
      bindText(npcAiStmt, 1, row.entityKey);
      bindText(npcAiStmt, 2, world);
      bindInt (npcAiStmt, 3, int64_t(game.tickCount()));
      bindText(npcAiStmt, 4, row.displayName);
      bindInt (npcAiStmt, 5, row.player);
      bindInt (npcAiStmt, 6, row.aiStateFunction);
      bindText(npcAiStmt, 7, row.aiStateName);
      bindText(npcAiStmt, 8, row.targetKey);
      bindInt (npcAiStmt, 9, row.targetSymbolIndex);
      bindText(npcAiStmt,10, row.targetName);
      bindText(npcAiStmt,11, row.relationKind);
      if(sqlite3_step(npcAiStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite npc ai insert failed: ", sqlite3_errmsg(impl->db));
      }

    std::string changed;
    const auto prev = previousNpcAi.find(row.entityKey);
    if(prev==previousNpcAi.end()) {
      if(hadPreviousNpcAi && row.relationKind!="none")
        changed = "spawned";
      } else {
      if(prev->second.stateName!=row.aiStateName)
        changed += "ai_state,";
      if(prev->second.targetKey!=row.targetKey)
        changed += "target,";
      if(prev->second.relationKind!=row.relationKind)
        changed += "relation_kind,";
      if(!changed.empty() && changed.back()==',')
        changed.pop_back();
      }
    if(!changed.empty() && npcAiHistStmt!=nullptr) {
      sqlite3_reset(npcAiHistStmt);
      sqlite3_clear_bindings(npcAiHistStmt);
      bindText(npcAiHistStmt, 1, row.entityKey);
      bindText(npcAiHistStmt, 2, world);
      bindInt (npcAiHistStmt, 3, int64_t(game.tickCount()));
      bindText(npcAiHistStmt, 4, row.displayName);
      bindInt (npcAiHistStmt, 5, row.player);
      bindInt (npcAiHistStmt, 6, row.aiStateFunction);
      bindText(npcAiHistStmt, 7, row.aiStateName);
      bindText(npcAiHistStmt, 8, row.targetKey);
      bindInt (npcAiHistStmt, 9, row.targetSymbolIndex);
      bindText(npcAiHistStmt,10, row.targetName);
      bindText(npcAiHistStmt,11, row.relationKind);
      bindText(npcAiHistStmt,12, changed);
      if(sqlite3_step(npcAiHistStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite npc ai history insert failed: ", sqlite3_errmsg(impl->db));
      }
    }
  if(npcAiStmt!=nullptr)
    sqlite3_finalize(npcAiStmt);
  if(npcAiHistStmt!=nullptr)
    sqlite3_finalize(npcAiHistStmt);

  exec(impl->db, "DELETE FROM runtime_world_items WHERE world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))");
  const char* worldItemInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_world_items(
      entity_key, world_name, tick_count, slot_id, persistent_id, symbol_index, script_id,
      display_name, visual, amount, main_flag, item_flags, value, pos_x, pos_y, pos_z, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, CURRENT_TIMESTAMP)
  )SQL";
  const char* worldItemHistory = R"SQL(
    INSERT INTO runtime_world_item_history(
      entity_key, world_name, tick_count, symbol_index, display_name, amount, pos_x, pos_y, pos_z, changed_fields
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
  )SQL";
  sqlite3_stmt* worldItemStmt = nullptr;
  sqlite3_stmt* worldItemHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, worldItemInsert, -1, &worldItemStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite world item prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, worldItemHistory, -1, &worldItemHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite world item history prepare failed: ", sqlite3_errmsg(impl->db));
  for(const WorldItemRow& row : currentWorldItems) {
    if(worldItemStmt!=nullptr) {
      sqlite3_reset(worldItemStmt);
      sqlite3_clear_bindings(worldItemStmt);
      bindText(worldItemStmt, 1, row.entityKey);
      bindText(worldItemStmt, 2, world);
      bindInt (worldItemStmt, 3, int64_t(game.tickCount()));
      bindInt (worldItemStmt, 4, row.slotId);
      bindInt (worldItemStmt, 5, row.persistentId);
      bindInt (worldItemStmt, 6, row.symbolIndex);
      bindInt (worldItemStmt, 7, row.scriptId);
      bindText(worldItemStmt, 8, row.displayName);
      bindText(worldItemStmt, 9, row.visual);
      bindInt (worldItemStmt,10, row.amount);
      bindInt (worldItemStmt,11, row.mainFlag);
      bindInt (worldItemStmt,12, row.itemFlags);
      bindInt (worldItemStmt,13, row.value);
      bindReal(worldItemStmt,14, row.posX);
      bindReal(worldItemStmt,15, row.posY);
      bindReal(worldItemStmt,16, row.posZ);
      if(sqlite3_step(worldItemStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite world item insert failed: ", sqlite3_errmsg(impl->db));
      }

    std::string changed;
    const auto prev = previousWorldItems.find(row.entityKey);
    if(prev==previousWorldItems.end()) {
      if(hadPreviousWorldItems)
        changed = "spawned";
      } else {
      const double moved = std::abs(prev->second.posX - row.posX) +
                           std::abs(prev->second.posY - row.posY) +
                           std::abs(prev->second.posZ - row.posZ);
      if(moved>1.0)
        changed += "pos,";
      if(prev->second.amount!=row.amount)
        changed += "amount,";
      if(!changed.empty() && changed.back()==',')
        changed.pop_back();
      }
    if(!changed.empty() && worldItemHistStmt!=nullptr) {
      sqlite3_reset(worldItemHistStmt);
      sqlite3_clear_bindings(worldItemHistStmt);
      bindText(worldItemHistStmt, 1, row.entityKey);
      bindText(worldItemHistStmt, 2, world);
      bindInt (worldItemHistStmt, 3, int64_t(game.tickCount()));
      bindInt (worldItemHistStmt, 4, row.symbolIndex);
      bindText(worldItemHistStmt, 5, row.displayName);
      bindInt (worldItemHistStmt, 6, row.amount);
      bindReal(worldItemHistStmt, 7, row.posX);
      bindReal(worldItemHistStmt, 8, row.posY);
      bindReal(worldItemHistStmt, 9, row.posZ);
      bindText(worldItemHistStmt,10, changed);
      if(sqlite3_step(worldItemHistStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite world item history insert failed: ", sqlite3_errmsg(impl->db));
      }
    }
  if(worldItemStmt!=nullptr)
    sqlite3_finalize(worldItemStmt);
  if(worldItemHistStmt!=nullptr)
    sqlite3_finalize(worldItemHistStmt);

  exec(impl->db, "DELETE FROM runtime_world_mobsi");
  exec(impl->db, "DELETE FROM runtime_world_mobsi_inventory");
  const char* mobsiInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_world_mobsi(
      entity_key, world_name, tick_count, slot_id, vob_id, tag, focus_name, display_name, scheme,
      pos_x, pos_y, pos_z, state, state_count, state_mask, container, door, ladder, locked, cracked, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, CURRENT_TIMESTAMP)
  )SQL";
  const char* mobsiHistory = R"SQL(
    INSERT INTO runtime_world_mobsi_history(entity_key, world_name, tick_count, display_name, state, locked, cracked, changed_fields)
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
  )SQL";
  sqlite3_stmt* mobsiStmt = nullptr;
  sqlite3_stmt* mobsiHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, mobsiInsert, -1, &mobsiStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite mobsi prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, mobsiHistory, -1, &mobsiHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite mobsi history prepare failed: ", sqlite3_errmsg(impl->db));

  const char* mobsiInvInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_world_mobsi_inventory(
      owner_key, item_key, world_name, owner_display_name, symbol_index, display_name, amount, iterator_count, value, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, CURRENT_TIMESTAMP)
  )SQL";
  sqlite3_stmt* mobsiInvStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, mobsiInvInsert, -1, &mobsiInvStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite mobsi inventory prepare failed: ", sqlite3_errmsg(impl->db));

  for(const MobsiRow& row : currentMobsi) {
    if(mobsiStmt!=nullptr) {
      sqlite3_reset(mobsiStmt);
      sqlite3_clear_bindings(mobsiStmt);
      bindText(mobsiStmt, 1, row.entityKey);
      bindText(mobsiStmt, 2, world);
      bindInt (mobsiStmt, 3, int64_t(game.tickCount()));
      bindInt (mobsiStmt, 4, row.slotId);
      bindInt (mobsiStmt, 5, row.vobId);
      bindText(mobsiStmt, 6, row.tag);
      bindText(mobsiStmt, 7, row.focusName);
      bindText(mobsiStmt, 8, row.displayName);
      bindText(mobsiStmt, 9, row.scheme);
      bindReal(mobsiStmt,10, row.posX);
      bindReal(mobsiStmt,11, row.posY);
      bindReal(mobsiStmt,12, row.posZ);
      bindInt (mobsiStmt,13, row.state);
      bindInt (mobsiStmt,14, row.stateCount);
      bindInt (mobsiStmt,15, row.stateMask);
      bindInt (mobsiStmt,16, row.container);
      bindInt (mobsiStmt,17, row.door);
      bindInt (mobsiStmt,18, row.ladder);
      bindInt (mobsiStmt,19, row.locked);
      bindInt (mobsiStmt,20, row.cracked);
      if(sqlite3_step(mobsiStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite mobsi insert failed: ", sqlite3_errmsg(impl->db));
      }
    std::string changed;
    const auto prev = previousMobsi.find(row.entityKey);
    if(prev==previousMobsi.end()) {
      if(hadPreviousMobsi)
        changed = "spawned";
      } else {
      if(prev->second.state!=row.state)
        changed += "state,";
      if(prev->second.locked!=row.locked)
        changed += "locked,";
      if(prev->second.cracked!=row.cracked)
        changed += "cracked,";
      if(!changed.empty() && changed.back()==',')
        changed.pop_back();
      }
    if(!changed.empty() && mobsiHistStmt!=nullptr) {
      sqlite3_reset(mobsiHistStmt);
      sqlite3_clear_bindings(mobsiHistStmt);
      bindText(mobsiHistStmt, 1, row.entityKey);
      bindText(mobsiHistStmt, 2, world);
      bindInt (mobsiHistStmt, 3, int64_t(game.tickCount()));
      bindText(mobsiHistStmt, 4, row.displayName);
      bindInt (mobsiHistStmt, 5, row.state);
      bindInt (mobsiHistStmt, 6, row.locked);
      bindInt (mobsiHistStmt, 7, row.cracked);
      bindText(mobsiHistStmt, 8, changed);
      if(sqlite3_step(mobsiHistStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite mobsi history insert failed: ", sqlite3_errmsg(impl->db));
      }
    }

  if(auto* wrld = game.world()) {
    for(uint32_t i=0; i<currentMobsi.size(); ++i) {
      Interactive* mobsi = wrld->mobsiById(i);
      if(mobsi==nullptr || mobsiInvStmt==nullptr)
        continue;
      auto it = mobsi->inventory().iterator(Inventory::T_Inventory);
      for(; it.isValid(); ++it) {
        const Item& item = *it;
        const int64_t symbolIndex = int64_t(item.clsId());
        const std::string itemKey = std::to_string(symbolIndex) + ":" + std::to_string(uint32_t(it.slot())) + ":" + std::to_string(it.isEquipped() ? 1 : 0);
        const std::string itemName = nonEmpty(item.displayName(), "item:" + std::to_string(symbolIndex));
        sqlite3_reset(mobsiInvStmt);
        sqlite3_clear_bindings(mobsiInvStmt);
        bindText(mobsiInvStmt, 1, currentMobsi[i].entityKey);
        bindText(mobsiInvStmt, 2, itemKey);
        bindText(mobsiInvStmt, 3, world);
        bindText(mobsiInvStmt, 4, currentMobsi[i].displayName);
        bindInt (mobsiInvStmt, 5, symbolIndex);
        bindText(mobsiInvStmt, 6, itemName);
        bindInt (mobsiInvStmt, 7, int64_t(item.count()));
        bindInt (mobsiInvStmt, 8, int64_t(it.count()));
        bindInt (mobsiInvStmt, 9, int64_t(item.cost()));
        if(sqlite3_step(mobsiInvStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite mobsi inventory insert failed: ", sqlite3_errmsg(impl->db));
        }
      }
    }
  if(mobsiStmt!=nullptr)
    sqlite3_finalize(mobsiStmt);
  if(mobsiHistStmt!=nullptr)
    sqlite3_finalize(mobsiHistStmt);
  if(mobsiInvStmt!=nullptr)
    sqlite3_finalize(mobsiInvStmt);

  const char* globalUpsert = R"SQL(
    INSERT INTO runtime_script_globals(
      global_key, symbol_index, symbol_name, value_type, category, value_count, value_text, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, CURRENT_TIMESTAMP)
    ON CONFLICT(global_key) DO UPDATE SET
      symbol_index=excluded.symbol_index,
      symbol_name=excluded.symbol_name,
      value_type=excluded.value_type,
      category=excluded.category,
      value_count=excluded.value_count,
      value_text=excluded.value_text,
      updated_at=CURRENT_TIMESTAMP
  )SQL";
  const char* globalHistory = R"SQL(
    INSERT INTO runtime_script_global_history(
      global_key, tick_count, symbol_index, symbol_name, value_type, category, value_count, value_before, value_after
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
  )SQL";
  sqlite3_stmt* globalStmt = nullptr;
  sqlite3_stmt* globalHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, globalUpsert, -1, &globalStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite script global prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, globalHistory, -1, &globalHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite script global history prepare failed: ", sqlite3_errmsg(impl->db));
  for(const ScriptGlobalRow& row : currentGlobals) {
    if(globalStmt!=nullptr) {
      sqlite3_reset(globalStmt);
      sqlite3_clear_bindings(globalStmt);
      bindText(globalStmt, 1, row.globalKey);
      bindInt (globalStmt, 2, row.symbolIndex);
      bindText(globalStmt, 3, row.symbolName);
      bindText(globalStmt, 4, row.valueType);
      bindText(globalStmt, 5, row.category);
      bindInt (globalStmt, 6, row.valueCount);
      bindText(globalStmt, 7, row.valueText);
      if(sqlite3_step(globalStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite script global upsert failed: ", sqlite3_errmsg(impl->db));
      }

    const auto prev = previousGlobals.find(row.globalKey);
    const bool changed = prev!=previousGlobals.end() &&
                         row.category!="script" &&
                         prev->second.valueText!=row.valueText;
    if(changed && globalHistStmt!=nullptr) {
      sqlite3_reset(globalHistStmt);
      sqlite3_clear_bindings(globalHistStmt);
      bindText(globalHistStmt, 1, row.globalKey);
      bindInt (globalHistStmt, 2, int64_t(game.tickCount()));
      bindInt (globalHistStmt, 3, row.symbolIndex);
      bindText(globalHistStmt, 4, row.symbolName);
      bindText(globalHistStmt, 5, row.valueType);
      bindText(globalHistStmt, 6, row.category);
      bindInt (globalHistStmt, 7, row.valueCount);
      bindText(globalHistStmt, 8, prev->second.valueText);
      bindText(globalHistStmt, 9, row.valueText);
      if(sqlite3_step(globalHistStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite script global history insert failed: ", sqlite3_errmsg(impl->db));
      }
    }
  if(globalStmt!=nullptr)
    sqlite3_finalize(globalStmt);
  if(globalHistStmt!=nullptr)
    sqlite3_finalize(globalHistStmt);

  auto questEntriesText = [](const QuestLog::Quest& quest) {
    std::string ret;
    for(size_t i=0; i<quest.entry.size(); ++i) {
      if(i!=0)
        ret += "\n---\n";
      ret += quest.entry[i];
      }
    if(ret.empty())
      ret = "(no entries)";
    return ret;
    };

  exec(impl->db, "DELETE FROM runtime_quests");
  const char* questInsert = R"SQL(
    INSERT INTO runtime_quests(quest_key, name, section, status, entry_count, entries_text, updated_at)
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, CURRENT_TIMESTAMP)
  )SQL";
  const char* questHistory = R"SQL(
    INSERT INTO runtime_quest_history(quest_key, tick_count, name, section, status, entry_count, entries_text, changed_fields)
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)
  )SQL";
  sqlite3_stmt* questStmt = nullptr;
  sqlite3_stmt* questHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, questInsert, -1, &questStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite quest prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, questHistory, -1, &questHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite quest history prepare failed: ", sqlite3_errmsg(impl->db));

  if(auto* script = game.script()) {
    const auto& quests = script->questLog();
    for(size_t i=0; i<quests.questCount(); ++i) {
      const auto& quest = quests.quest(i);
      const std::string questName = nonEmpty(quest.name, "quest:" + std::to_string(i));
      const std::string questKey = "quest:" + questName;
      const std::string entries = questEntriesText(quest);
      const int64_t section = int64_t(quest.section);
      const int64_t status = int64_t(quest.status);
      const int64_t entryCount = int64_t(quest.entry.size());

      if(questStmt!=nullptr) {
        sqlite3_reset(questStmt);
        sqlite3_clear_bindings(questStmt);
        bindText(questStmt, 1, questKey);
        bindText(questStmt, 2, questName);
        bindInt (questStmt, 3, section);
        bindInt (questStmt, 4, status);
        bindInt (questStmt, 5, entryCount);
        bindText(questStmt, 6, entries);
        if(sqlite3_step(questStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite quest insert failed: ", sqlite3_errmsg(impl->db));
        }

      std::string changed;
      const auto prev = previousQuests.find(questKey);
      if(prev==previousQuests.end()) {
        if(hadPreviousQuests) {
          changed = "added";
          insertEvent("quest_added", HeroKey, questKey, 0.0, double(entryCount), questName);
          }
        } else {
        if(prev->second.status!=status) {
          changed += "status,";
          insertEvent("quest_status_changed", HeroKey, questKey, double(prev->second.status), double(status), questName);
          }
        if(prev->second.entryCount!=entryCount) {
          changed += "entry_count,";
          insertEvent("quest_entry_added", HeroKey, questKey, double(prev->second.entryCount), double(entryCount), questName);
          }
        if(!changed.empty() && changed.back()==',')
          changed.pop_back();
        }

      if(!changed.empty() && questHistStmt!=nullptr) {
        sqlite3_reset(questHistStmt);
        sqlite3_clear_bindings(questHistStmt);
        bindText(questHistStmt, 1, questKey);
        bindInt (questHistStmt, 2, int64_t(game.tickCount()));
        bindText(questHistStmt, 3, questName);
        bindInt (questHistStmt, 4, section);
        bindInt (questHistStmt, 5, status);
        bindInt (questHistStmt, 6, entryCount);
        bindText(questHistStmt, 7, entries);
        bindText(questHistStmt, 8, changed);
        if(sqlite3_step(questHistStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite quest history insert failed: ", sqlite3_errmsg(impl->db));
        }
      }

    exec(impl->db, "DELETE FROM runtime_dialog_catalog");
    const char* dialogCatalogInsert = R"SQL(
      INSERT OR REPLACE INTO runtime_dialog_catalog(
        info_symbol_index, info_symbol_name, npc_symbol_index, npc_symbol_name,
        description, sort_order, important, permanent, trade,
        information_symbol_index, information_symbol_name,
        condition_symbol_index, condition_symbol_name, updated_at
      )
      VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, CURRENT_TIMESTAMP)
    )SQL";
    sqlite3_stmt* dialogCatalogStmt = nullptr;
    if(sqlite3_prepare_v2(impl->db, dialogCatalogInsert, -1, &dialogCatalogStmt, nullptr)!=SQLITE_OK)
      Tempest::Log::e("mmo sqlite dialog catalog prepare failed: ", sqlite3_errmsg(impl->db));

    for(auto& infoPtr : script->dialogInfos()) {
      if(infoPtr==nullptr)
        continue;
      const zenkit::IInfo& info = *infoPtr;
      int64_t infoSymbol = -1;
      std::string infoName = "info:unknown";
      if(auto* sym = script->getVm().find_symbol_by_instance(infoPtr)) {
        infoSymbol = int64_t(sym->index());
        infoName   = nonEmpty(sym->name(), "info:" + std::to_string(infoSymbol));
        }
      if(infoSymbol<0)
        continue;

      const int64_t npcSymbol = int64_t(info.npc);
      std::string npcName = "npc:" + std::to_string(npcSymbol);
      if(auto* sym = script->findSymbol(size_t(npcSymbol)))
        npcName = nonEmpty(sym->name(), npcName);

      const int64_t informationSymbol = int64_t(info.information);
      std::string informationName = "function:" + std::to_string(informationSymbol);
      if(auto* sym = script->findSymbol(size_t(informationSymbol)))
        informationName = nonEmpty(sym->name(), informationName);

      const int64_t conditionSymbol = int64_t(info.condition);
      std::string conditionName = "(no condition)";
      if(conditionSymbol!=0) {
        conditionName = "condition:" + std::to_string(conditionSymbol);
        if(auto* sym = script->findSymbol(size_t(conditionSymbol)))
          conditionName = nonEmpty(sym->name(), conditionName);
        }

      const std::string description = nonEmpty(info.description, "(no description)");
      if(dialogCatalogStmt!=nullptr) {
        sqlite3_reset(dialogCatalogStmt);
        sqlite3_clear_bindings(dialogCatalogStmt);
        bindInt (dialogCatalogStmt, 1, infoSymbol);
        bindText(dialogCatalogStmt, 2, infoName);
        bindInt (dialogCatalogStmt, 3, npcSymbol);
        bindText(dialogCatalogStmt, 4, npcName);
        bindText(dialogCatalogStmt, 5, description);
        bindInt (dialogCatalogStmt, 6, int64_t(info.nr));
        bindInt (dialogCatalogStmt, 7, int64_t(info.important));
        bindInt (dialogCatalogStmt, 8, int64_t(info.permanent));
        bindInt (dialogCatalogStmt, 9, int64_t(info.trade));
        bindInt (dialogCatalogStmt,10, informationSymbol);
        bindText(dialogCatalogStmt,11, informationName);
        bindInt (dialogCatalogStmt,12, conditionSymbol);
        bindText(dialogCatalogStmt,13, conditionName);
        if(sqlite3_step(dialogCatalogStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite dialog catalog insert failed: ", sqlite3_errmsg(impl->db));
        }
      }
    if(dialogCatalogStmt!=nullptr)
      sqlite3_finalize(dialogCatalogStmt);

    const char* dialogInsert = R"SQL(
      INSERT OR REPLACE INTO runtime_known_dialogs(
        npc_symbol_index, info_symbol_index, npc_symbol_name, info_symbol_name, first_seen_tick, updated_at
      )
      VALUES(?1, ?2, ?3, ?4,
        COALESCE((SELECT first_seen_tick FROM runtime_known_dialogs WHERE npc_symbol_index=?1 AND info_symbol_index=?2), ?5),
        CURRENT_TIMESTAMP)
    )SQL";
    const char* dialogHistory = R"SQL(
      INSERT INTO runtime_known_dialog_history(npc_symbol_index, info_symbol_index, npc_symbol_name, info_symbol_name, tick_count)
      VALUES(?1, ?2, ?3, ?4, ?5)
    )SQL";
    sqlite3_stmt* dialogStmt = nullptr;
    sqlite3_stmt* dialogHistStmt = nullptr;
    if(sqlite3_prepare_v2(impl->db, dialogInsert, -1, &dialogStmt, nullptr)!=SQLITE_OK)
      Tempest::Log::e("mmo sqlite dialog prepare failed: ", sqlite3_errmsg(impl->db));
    if(sqlite3_prepare_v2(impl->db, dialogHistory, -1, &dialogHistStmt, nullptr)!=SQLITE_OK)
      Tempest::Log::e("mmo sqlite dialog history prepare failed: ", sqlite3_errmsg(impl->db));

    for(auto& known : script->knownDialogInfos()) {
      const int64_t npcSymbol = int64_t(known.first);
      const int64_t infoSymbol = int64_t(known.second);
      std::string npcName = "symbol:" + std::to_string(npcSymbol);
      std::string infoName = "symbol:" + std::to_string(infoSymbol);
      if(auto* sym = script->findSymbol(size_t(npcSymbol)))
        npcName = nonEmpty(sym->name(), npcName);
      if(auto* sym = script->findSymbol(size_t(infoSymbol)))
        infoName = nonEmpty(sym->name(), infoName);

      if(dialogStmt!=nullptr) {
        sqlite3_reset(dialogStmt);
        sqlite3_clear_bindings(dialogStmt);
        bindInt (dialogStmt, 1, npcSymbol);
        bindInt (dialogStmt, 2, infoSymbol);
        bindText(dialogStmt, 3, npcName);
        bindText(dialogStmt, 4, infoName);
        bindInt (dialogStmt, 5, int64_t(game.tickCount()));
        if(sqlite3_step(dialogStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite dialog insert failed: ", sqlite3_errmsg(impl->db));
        }

      if(previousDialogs.find({npcSymbol, infoSymbol})==previousDialogs.end()) {
        insertEvent("dialog_known", HeroKey, std::to_string(infoSymbol), 0.0, 1.0, infoName);
        if(dialogHistStmt!=nullptr) {
          sqlite3_reset(dialogHistStmt);
          sqlite3_clear_bindings(dialogHistStmt);
          bindInt (dialogHistStmt, 1, npcSymbol);
          bindInt (dialogHistStmt, 2, infoSymbol);
          bindText(dialogHistStmt, 3, npcName);
          bindText(dialogHistStmt, 4, infoName);
          bindInt (dialogHistStmt, 5, int64_t(game.tickCount()));
          if(sqlite3_step(dialogHistStmt)!=SQLITE_DONE)
            Tempest::Log::e("mmo sqlite dialog history insert failed: ", sqlite3_errmsg(impl->db));
          }
        }
      }
    if(dialogStmt!=nullptr)
      sqlite3_finalize(dialogStmt);
    if(dialogHistStmt!=nullptr)
      sqlite3_finalize(dialogHistStmt);
    }
  if(questStmt!=nullptr)
    sqlite3_finalize(questStmt);
  if(questHistStmt!=nullptr)
    sqlite3_finalize(questHistStmt);

  exec(impl->db, "COMMIT");
#endif
  }
