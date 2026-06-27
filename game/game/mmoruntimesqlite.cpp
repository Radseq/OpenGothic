#include "mmoruntimesqlite.h"

#include <Tempest/Log>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
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

std::string saveSlotKey(std::string_view slotPath) {
  if(slotPath.empty())
    return {};
  return "legacy-save-slot:" + std::string(slotPath);
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

void appendUtf8(std::string& out, uint32_t codepoint) {
  if(codepoint<=0x7F) {
    out.push_back(char(codepoint));
    return;
    }
  if(codepoint<=0x7FF) {
    out.push_back(char(0xC0 | (codepoint >> 6)));
    out.push_back(char(0x80 | (codepoint & 0x3F)));
    return;
    }
  if(codepoint<=0xFFFF) {
    out.push_back(char(0xE0 | (codepoint >> 12)));
    out.push_back(char(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(char(0x80 | (codepoint & 0x3F)));
    return;
    }
  out.push_back(char(0xF0 | (codepoint >> 18)));
  out.push_back(char(0x80 | ((codepoint >> 12) & 0x3F)));
  out.push_back(char(0x80 | ((codepoint >> 6) & 0x3F)));
  out.push_back(char(0x80 | (codepoint & 0x3F)));
  }

bool isValidUtf8(std::string_view text) {
  const auto* data = reinterpret_cast<const uint8_t*>(text.data());
  const size_t size = text.size();
  for(size_t i=0; i<size;) {
    const uint8_t c = data[i];
    if(c<0x80) {
      ++i;
      continue;
      }

    uint32_t codepoint = 0;
    size_t continuation = 0;
    if((c & 0xE0)==0xC0) {
      codepoint = uint32_t(c & 0x1F);
      continuation = 1;
      if(codepoint==0)
        return false;
      }
    else if((c & 0xF0)==0xE0) {
      codepoint = uint32_t(c & 0x0F);
      continuation = 2;
      }
    else if((c & 0xF8)==0xF0) {
      codepoint = uint32_t(c & 0x07);
      continuation = 3;
      }
    else {
      return false;
      }

    if(i+continuation>=size)
      return false;
    for(size_t j=1; j<=continuation; ++j) {
      const uint8_t cc = data[i+j];
      if((cc & 0xC0)!=0x80)
        return false;
      codepoint = (codepoint << 6) | uint32_t(cc & 0x3F);
      }
    if((continuation==1 && codepoint<0x80) ||
       (continuation==2 && codepoint<0x800) ||
       (continuation==3 && codepoint<0x10000) ||
       codepoint>0x10FFFF ||
       (codepoint>=0xD800 && codepoint<=0xDFFF))
      return false;
    i += continuation + 1;
    }
  return true;
  }

uint32_t windows1250Codepoint(uint8_t byte) {
  static constexpr uint16_t map[128] = {
    0x20AC, 0xFFFD, 0x201A, 0xFFFD, 0x201E, 0x2026, 0x2020, 0x2021,
    0xFFFD, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0xFFFD, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
    0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
    0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
    0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
    0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
    };
  if(byte<0x80)
    return byte;
  return map[byte-0x80];
  }

std::string windows1250ToUtf8(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for(const uint8_t byte : std::string_view(text))
    appendUtf8(out, windows1250Codepoint(byte));
  return out;
  }

std::string sqliteText(std::string_view value) {
  if(value.empty() || isValidUtf8(value))
    return std::string(value);
  return windows1250ToUtf8(value);
  }

std::string columnRawText(sqlite3_stmt* stmt, int column) {
  const auto* raw = sqlite3_column_text(stmt, column);
  const int bytes = sqlite3_column_bytes(stmt, column);
  if(raw==nullptr || bytes<=0)
    return {};
  return std::string(reinterpret_cast<const char*>(raw), size_t(bytes));
  }

bool bindText(sqlite3_stmt* stmt, int index, std::string_view value) {
  const std::string text = sqliteText(value);
  return sqlite3_bind_text(stmt, index, text.c_str(), int(text.size()), SQLITE_TRANSIENT)==SQLITE_OK;
  }

bool bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  return bindText(stmt, index, std::string_view(value));
  }

bool bindText(sqlite3_stmt* stmt, int index, const char* value) {
  return bindText(stmt, index, value!=nullptr ? std::string_view(value) : std::string_view());
  }

bool bindInt(sqlite3_stmt* stmt, int index, int64_t value) {
  return sqlite3_bind_int64(stmt, index, sqlite3_int64(value))==SQLITE_OK;
  }

bool bindReal(sqlite3_stmt* stmt, int index, double value) {
  return sqlite3_bind_double(stmt, index, value)==SQLITE_OK;
  }

std::string sqliteIdentifier(std::string_view value) {
  std::string out = "\"";
  for(char c : value) {
    if(c=='"')
      out += "\"\""; else
      out.push_back(c);
    }
  out.push_back('"');
  return out;
  }

bool sqliteTextAffinity(std::string_view typeName) {
  std::string upper;
  upper.reserve(typeName.size());
  for(char c : typeName) {
    if(c>='a' && c<='z')
      upper.push_back(char(c-'a'+'A')); else
      upper.push_back(c);
    }
  return upper.find("TEXT")!=std::string::npos ||
         upper.find("CHAR")!=std::string::npos ||
         upper.find("CLOB")!=std::string::npos;
  }

bool tableColumnExists(sqlite3* db, std::string_view table, std::string_view column) {
  const std::string pragma = "PRAGMA table_info(" + sqliteIdentifier(table) + ")";
  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(db, pragma.c_str(), -1, &stmt, nullptr)!=SQLITE_OK)
    return false;

  const std::string expected(column);
  bool found = false;
  while(sqlite3_step(stmt)==SQLITE_ROW) {
    if(columnRawText(stmt, 1)==expected) {
      found = true;
      break;
      }
    }
  sqlite3_finalize(stmt);
  return found;
  }

bool ensureColumn(sqlite3* db, std::string_view table, std::string_view column, std::string_view definition) {
  if(tableColumnExists(db, table, column))
    return true;
  const std::string sql = "ALTER TABLE " + sqliteIdentifier(table) +
                          " ADD COLUMN " + sqliteIdentifier(column) +
                          " " + std::string(definition);
  return exec(db, sql.c_str());
  }

std::string runtimeMetaValue(sqlite3* db, std::string_view key) {
  sqlite3_stmt* stmt = nullptr;
  std::string value;
  if(sqlite3_prepare_v2(db,
      "SELECT value FROM runtime_schema_meta WHERE key=?1",
      -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, key);
    if(sqlite3_step(stmt)==SQLITE_ROW)
      value = columnRawText(stmt, 0);
    sqlite3_finalize(stmt);
    }
  return value;
  }

bool setRuntimeMetaValue(sqlite3* db, std::string_view key, std::string_view value) {
  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(db,
      "INSERT INTO runtime_schema_meta(key, value) VALUES(?1, ?2) "
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
      -1, &stmt, nullptr)!=SQLITE_OK)
    return false;
  bindText(stmt, 1, key);
  bindText(stmt, 2, value);
  const bool ok = sqlite3_step(stmt)==SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
  }

bool normalizeSqliteTextStorage(sqlite3* db) {
  constexpr const char* versionKey = "text_encoding_version";
  constexpr const char* versionValue = "utf8-v1";
  if(runtimeMetaValue(db, versionKey)==versionValue)
    return true;

  struct TextColumn final {
    std::string table;
    std::string column;
    };
  std::vector<TextColumn> columns;

  sqlite3_stmt* tableStmt = nullptr;
  if(sqlite3_prepare_v2(db,
      "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name",
      -1, &tableStmt, nullptr)!=SQLITE_OK)
    return false;
  while(sqlite3_step(tableStmt)==SQLITE_ROW) {
    const std::string table = columnRawText(tableStmt, 0);
    const std::string pragma = "PRAGMA table_info(" + sqliteIdentifier(table) + ")";
    sqlite3_stmt* columnStmt = nullptr;
    if(sqlite3_prepare_v2(db, pragma.c_str(), -1, &columnStmt, nullptr)!=SQLITE_OK)
      continue;
    while(sqlite3_step(columnStmt)==SQLITE_ROW) {
      const std::string column = columnRawText(columnStmt, 1);
      const std::string type = columnRawText(columnStmt, 2);
      if(sqliteTextAffinity(type))
        columns.push_back({table, column});
      }
    sqlite3_finalize(columnStmt);
    }
  sqlite3_finalize(tableStmt);

  if(!exec(db, "BEGIN IMMEDIATE TRANSACTION"))
    return false;

  int64_t changed = 0;
  bool ok = true;
  for(const TextColumn& column : columns) {
    const std::string tableSql = sqliteIdentifier(column.table);
    const std::string columnSql = sqliteIdentifier(column.column);
    const std::string selectSql = "SELECT rowid, " + columnSql + " FROM " + tableSql +
                                  " WHERE typeof(" + columnSql + ")='text'";
    sqlite3_stmt* selectStmt = nullptr;
    if(sqlite3_prepare_v2(db, selectSql.c_str(), -1, &selectStmt, nullptr)!=SQLITE_OK)
      continue;

    std::vector<std::pair<int64_t, std::string>> updates;
    while(sqlite3_step(selectStmt)==SQLITE_ROW) {
      const int64_t rowid = sqlite3_column_int64(selectStmt, 0);
      const std::string raw = columnRawText(selectStmt, 1);
      const std::string normalized = sqliteText(raw);
      if(normalized!=raw)
        updates.push_back({rowid, normalized});
      }
    sqlite3_finalize(selectStmt);
    if(updates.empty())
      continue;

    const std::string updateSql = "UPDATE " + tableSql + " SET " + columnSql + "=?1 WHERE rowid=?2";
    sqlite3_stmt* updateStmt = nullptr;
    if(sqlite3_prepare_v2(db, updateSql.c_str(), -1, &updateStmt, nullptr)!=SQLITE_OK) {
      ok = false;
      break;
      }
    for(const auto& update : updates) {
      sqlite3_reset(updateStmt);
      sqlite3_clear_bindings(updateStmt);
      bindText(updateStmt, 1, update.second);
      bindInt(updateStmt, 2, update.first);
      if(sqlite3_step(updateStmt)!=SQLITE_DONE) {
        ok = false;
        break;
        }
      ++changed;
      }
    sqlite3_finalize(updateStmt);
    if(!ok)
      break;
    }

  if(ok)
    ok = setRuntimeMetaValue(db, versionKey, versionValue);
  if(ok)
    ok = exec(db, "COMMIT");
  if(!ok) {
    exec(db, "ROLLBACK");
    return false;
    }

  if(changed>0)
    Tempest::Log::i("mmo sqlite normalized legacy text rows to UTF-8: ", changed);
  return true;
  }

std::string dialogSelectionEventType(std::string_view phase) {
  if(phase=="exec")
    return "dialog_choice_executed";
  if(phase=="update")
    return "dialog_choice_updated";
  return "dialog_choice_selected";
  }

bool normalizeDialogEventTypes(sqlite3* db) {
  constexpr const char* versionKey = "dialog_event_type_version";
  constexpr const char* versionValue = "phase-v1";
  if(runtimeMetaValue(db, versionKey)==versionValue)
    return true;

  if(!exec(db, "BEGIN IMMEDIATE TRANSACTION"))
    return false;

  sqlite3_stmt* selectStmt = nullptr;
  sqlite3_stmt* eventStmt = nullptr;
  sqlite3_stmt* updateStmt = nullptr;
  bool ok = true;
  int64_t changed = 0;

  if(sqlite3_prepare_v2(db,
      "SELECT session_id,npc_key,info_symbol_name,tick_count,title,phase "
      "FROM runtime_dialog_selections ORDER BY id",
      -1, &selectStmt, nullptr)!=SQLITE_OK) {
    ok = false;
    }
  if(ok && sqlite3_prepare_v2(db,
      "SELECT id FROM runtime_events "
      "WHERE event_type='dialog_selected' AND session_id=?1 AND entity_key=?2 AND subject_key=?3 "
      "AND tick_count=?4 AND data_text=?5 ORDER BY id LIMIT 1",
      -1, &eventStmt, nullptr)!=SQLITE_OK) {
    ok = false;
    }
  if(ok && sqlite3_prepare_v2(db,
      "UPDATE runtime_events SET event_type=?1 WHERE id=?2",
      -1, &updateStmt, nullptr)!=SQLITE_OK) {
    ok = false;
    }

  while(ok && sqlite3_step(selectStmt)==SQLITE_ROW) {
    const int64_t sessionId = sqlite3_column_int64(selectStmt, 0);
    const std::string npcKey = columnRawText(selectStmt, 1);
    const std::string infoName = columnRawText(selectStmt, 2);
    const int64_t tick = sqlite3_column_int64(selectStmt, 3);
    const std::string title = columnRawText(selectStmt, 4);
    const std::string phase = columnRawText(selectStmt, 5);
    const std::string eventType = dialogSelectionEventType(phase);

    sqlite3_reset(eventStmt);
    sqlite3_clear_bindings(eventStmt);
    bindInt (eventStmt, 1, sessionId);
    bindText(eventStmt, 2, npcKey);
    bindText(eventStmt, 3, infoName);
    bindInt (eventStmt, 4, tick);
    bindText(eventStmt, 5, title);
    if(sqlite3_step(eventStmt)!=SQLITE_ROW)
      continue;
    const int64_t eventId = sqlite3_column_int64(eventStmt, 0);

    sqlite3_reset(updateStmt);
    sqlite3_clear_bindings(updateStmt);
    bindText(updateStmt, 1, eventType);
    bindInt (updateStmt, 2, eventId);
    if(sqlite3_step(updateStmt)!=SQLITE_DONE) {
      ok = false;
      break;
      }
    ++changed;
    }

  if(selectStmt!=nullptr)
    sqlite3_finalize(selectStmt);
  if(eventStmt!=nullptr)
    sqlite3_finalize(eventStmt);
  if(updateStmt!=nullptr)
    sqlite3_finalize(updateStmt);

  if(ok)
    ok = setRuntimeMetaValue(db, versionKey, versionValue);
  if(ok)
    ok = exec(db, "COMMIT");
  if(!ok) {
    exec(db, "ROLLBACK");
    return false;
    }

  if(changed>0)
    Tempest::Log::i("mmo sqlite normalized dialog event types: ", changed);
  return true;
  }

int64_t saveSlotSnapshotId(sqlite3* db, std::string_view slotPath) {
  const std::string key = saveSlotKey(slotPath);
  if(key.empty())
    return 0;

  sqlite3_stmt* stmt = nullptr;
  int64_t snapshotId = 0;
  if(sqlite3_prepare_v2(db,
      "SELECT current_snapshot_id FROM mmo_save_slots WHERE slot_key=?1 OR source_slot_path=?2",
      -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, key);
    bindText(stmt, 2, std::string(slotPath));
    if(sqlite3_step(stmt)==SQLITE_ROW)
      snapshotId = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    }
  return snapshotId;
  }

bool restoreSaveSlotSnapshot(sqlite3* db, std::string_view slotPath) {
  const int64_t snapshotId = saveSlotSnapshotId(db, slotPath);
  if(snapshotId<=0)
    return false;

  const std::string id = std::to_string(snapshotId);
  if(!exec(db, "BEGIN IMMEDIATE TRANSACTION"))
    return false;

  std::string sql = R"SQL(
    DELETE FROM mmo_unit_stat_current;
    DELETE FROM mmo_unit_stat_sheet_current;
    DELETE FROM mmo_characters_current;
    DELETE FROM mmo_character_inventory_current;
    DELETE FROM mmo_character_wallet_current;
    DELETE FROM mmo_character_quests_current;
    DELETE FROM mmo_character_known_dialogs_current;
    DELETE FROM mmo_character_story_progress_current;
    DELETE FROM mmo_world_clock_current;
    DELETE FROM mmo_creature_spawns_current;
    DELETE FROM mmo_creature_inventory_current;
    DELETE FROM mmo_creature_inventory_snapshots_current;
    DELETE FROM mmo_creature_relations_current;
    DELETE FROM mmo_world_items_current;
    DELETE FROM mmo_world_interactives_current;
    DELETE FROM mmo_world_container_inventory_current;
    DELETE FROM mmo_script_globals_current;
    DELETE FROM mmo_script_global_values_current;
    DELETE FROM mmo_guild_attitudes_current;
  )SQL";
  sql += "INSERT OR REPLACE INTO mmo_unit_stat_current SELECT unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id, display_name, player, stat_domain, stat_family, stat_group, stat_id, stat_key, value_kind, persistence_hint, display_order, value, updated_at, persistence_class FROM mmo_save_slot_unit_stat WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_unit_stat_sheet_current SELECT unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id, display_name, player, guild, true_guild, level, experience, experience_next, learning_points, permanent_attitude, temporary_attitude, dead, pos_x, pos_y, pos_z, rotation, waypoint, health_current, health_max, mana_current, mana_max, strength, dexterity, regenerate_hp, regenerate_mana, resist_barrier, resist_blunt, resist_edge, resist_fire, resist_fly, resist_magic, resist_point, resist_fall, one_handed_skill, two_handed_skill, bow_skill, crossbow_skill, one_handed_hit_chance, two_handed_hit_chance, bow_hit_chance, crossbow_hit_chance, picklock_skill, sneak_skill, pickpocket_skill, smith_skill, alchemy_skill, take_animal_trophy_skill, foreign_language_skill, acrobat_skill, mage_skill, runes_skill, firemaster_skill, regenerate_skill, wisp_detector_skill, updated_at, persistence_class FROM mmo_save_slot_unit_stat_sheet WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_characters_current SELECT character_key, account_key, realm_key, world_name, tick_count, display_name, pos_x, pos_y, pos_z, rotation, health_current, health_max, mana_current, mana_max, level, experience, updated_at, persistence_class FROM mmo_save_slot_characters WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_character_inventory_current SELECT character_key, item_instance_key, item_template_symbol, item_display_name, amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id, updated_at, persistence_class FROM mmo_save_slot_character_inventory WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_character_wallet_current SELECT character_key, currency_key, currency_display_name, item_template_symbol, amount, updated_at, persistence_class FROM mmo_save_slot_character_wallet WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_character_quests_current SELECT character_key, quest_key, quest_name, section, status, entry_count, entries_text, updated_at, persistence_class FROM mmo_save_slot_character_quests WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_character_known_dialogs_current SELECT character_key, npc_symbol_index, info_symbol_index, npc_symbol_name, info_symbol_name, description, permanent, first_seen_tick, updated_at, persistence_class FROM mmo_save_slot_character_known_dialogs WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_character_story_progress_current SELECT character_key, world_name, tick_count, chapter_number, chapter_key, source_global_key, source_symbol_index, source_symbol_name, updated_at, persistence_class FROM mmo_save_slot_character_story_progress WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_world_clock_current SELECT world_name, tick_count, world_time_millis, world_day, world_hour, world_minute, updated_at, persistence_class FROM mmo_save_slot_world_clock WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_creature_spawns_current SELECT creature_spawn_key, creature_template_id, world_name, tick_count, display_name, pos_x, pos_y, pos_z, rotation, waypoint, dead, level, experience, health_current, health_max, mana_current, mana_max, strength, dexterity, current_waypoint_name, routine_waypoint_name, move_hint, move_target_waypoint_name, updated_at, persistence_class FROM mmo_save_slot_creature_spawns WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_creature_inventory_current SELECT creature_spawn_key, item_instance_key, world_name, item_template_symbol, item_display_name, amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id, updated_at, persistence_class FROM mmo_save_slot_creature_inventory WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_creature_inventory_snapshots_current SELECT creature_spawn_key, world_name, tick_count, item_row_count, updated_at, persistence_class FROM mmo_save_slot_creature_inventory_snapshots WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_creature_relations_current SELECT creature_spawn_key, world_name, tick_count, relation_kind, target_key, other_key, victim_key, ai_state_function, ai_state_name, state_elapsed_millis, updated_at, persistence_class FROM mmo_save_slot_creature_relations WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_world_items_current SELECT item_spawn_key, world_name, tick_count, slot_id, persistent_id, item_template_symbol, script_id, item_display_name, visual, amount, main_flag, item_flags, value, pos_x, pos_y, pos_z, exists_in_world, updated_at, persistence_class FROM mmo_save_slot_world_items WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_world_interactives_current SELECT interactive_key, world_name, tick_count, slot_id, vob_id, tag, focus_name, display_name, scheme, pos_x, pos_y, pos_z, state, state_count, state_mask, container, door, ladder, locked, cracked, updated_at, persistence_class FROM mmo_save_slot_world_interactives WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_world_container_inventory_current SELECT owner_key, item_instance_key, world_name, owner_display_name, item_template_symbol, item_display_name, amount, iterator_count, value, updated_at, persistence_class FROM mmo_save_slot_world_container_inventory WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_script_globals_current SELECT global_key, symbol_index, symbol_name, value_type, category, value_count, value_text, updated_at, persistence_class FROM mmo_save_slot_script_globals WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_script_global_values_current SELECT global_key, value_index, value_int, value_real, value_text, updated_at FROM mmo_save_slot_script_global_values WHERE snapshot_id=" + id + ";\n";
  sql += "INSERT OR REPLACE INTO mmo_guild_attitudes_current SELECT realm_key, from_guild, to_guild, attitude, updated_at FROM mmo_save_slot_guild_attitudes WHERE snapshot_id=" + id + ";\n";

  if(!exec(db, sql.c_str())) {
    exec(db, "ROLLBACK");
    return false;
    }
  if(!exec(db, "COMMIT")) {
    exec(db, "ROLLBACK");
    return false;
    }

  Tempest::Log::i("mmo sqlite restored save-slot snapshot: ", slotPath, " snapshot=", snapshotId);
  return true;
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
  std::string stateOtherKey;
  std::string stateVictimKey;
  std::string relationKind;
  };

struct NpcNavigationPrevious final {
  std::string currentWaypointKey;
  std::string routineWaypointKey;
  std::string moveHint;
  std::string moveTargetWaypointKey;
  std::string pathNextWaypointKey;
  std::string pathFinalWaypointKey;
  int64_t     pathRemainingCount = 0;
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
  int64_t     experienceNext = 0;
  int64_t     learningPoints = 0;
  int64_t     permanentAttitude = ATT_NULL;
  int64_t     temporaryAttitude = ATT_NULL;
  int64_t     dead = 0;
  int64_t     player = 0;
  int64_t     aiStateFunction = -1;
  int64_t     aiStateElapsedMillis = 0;
  int64_t     targetSymbolIndex = -1;
  std::string stateOtherKey;
  std::string stateVictimKey;
  std::array<int64_t, ATR_MAX> attributes = {};
  std::array<int64_t, PROT_MAX> protections = {};
  std::array<int64_t, TALENT_MAX_G2> talentSkills = {};
  std::array<int64_t, TALENT_MAX_G2> talentValues = {};
  std::array<int64_t, TALENT_MAX_G2> hitChances = {};
  std::array<int64_t, zenkit::INpc::mission_count> missions = {};
  std::array<int64_t, zenkit::INpc::aivar_count> aiVariables = {};
  };

void appendNpcStatSignature(std::string& signature, int64_t value) {
  signature += std::to_string(value);
  signature.push_back(';');
  }

std::string npcStatSignature(const NpcRow& row) {
  std::string signature;
  signature.reserve(1024);
  for(const int64_t value : row.attributes)
    appendNpcStatSignature(signature, value);
  for(const int64_t value : row.protections)
    appendNpcStatSignature(signature, value);
  for(const int64_t value : row.talentSkills)
    appendNpcStatSignature(signature, value);
  for(const int64_t value : row.talentValues)
    appendNpcStatSignature(signature, value);
  for(const int64_t value : row.hitChances)
    appendNpcStatSignature(signature, value);
  appendNpcStatSignature(signature, row.experienceNext);
  appendNpcStatSignature(signature, row.learningPoints);
  appendNpcStatSignature(signature, row.permanentAttitude);
  appendNpcStatSignature(signature, row.temporaryAttitude);
  for(const int64_t value : row.missions)
    appendNpcStatSignature(signature, value);
  for(const int64_t value : row.aiVariables)
    appendNpcStatSignature(signature, value);
  return signature;
  }

int64_t minuteOfDay(gtime time) {
  auto dayTime = time.timeInDay();
  return dayTime.hour()*60 + dayTime.minute();
  }

std::string waypointKey(std::string_view world, std::string_view kind, size_t index, std::string_view name) {
  return "waypoint:" + std::string(world) + ":" + std::string(kind) + ":" +
         std::to_string(index) + ":" + std::string(name);
  }

std::string moveHintName(Npc::GoToHint hint) {
  switch(hint) {
    case Npc::GT_No:      return "none";
    case Npc::GT_Way:     return "way";
    case Npc::GT_NextFp:  return "next_freepoint";
    case Npc::GT_Enemy:   return "enemy";
    case Npc::GT_Item:    return "item";
    case Npc::GT_Point:   return "point";
    case Npc::GT_EnemyG:  return "enemy_deprecated";
    case Npc::GT_Flee:    return "flee";
    }
  return "unknown";
  }

std::string waypointName(const WayPoint* point) {
  return point!=nullptr ? std::string(point->name) : std::string();
  }

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
  int64_t state      = 0;
  int64_t stateCount = 0;
  int64_t stateMask  = 0;
  int64_t locked     = 0;
  int64_t cracked    = 0;
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

struct ScriptGlobalValueRow final {
  int64_t     valueIndex = 0;
  int64_t     valueInt = 0;
  double      valueReal = 0.0;
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
  std::vector<ScriptGlobalValueRow> values;
  };

struct StoryProgressPrevious final {
  bool    valid = false;
  int64_t chapterNumber = 0;
  };

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

int64_t parseInt64(std::string_view text, int64_t fallback = 0) {
  int64_t value = fallback;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, value);
  if(result.ec!=std::errc())
    return fallback;
  return value;
  }

std::string npcRelationKind(const NpcRow& row) {
  const std::string state = lowerAscii(row.aiStateName);
  if(state.find("follow")!=std::string::npos)
    return "following_target";
  if(state.find("escort")!=std::string::npos || state.find("guide")!=std::string::npos)
    return "escort_or_guide";
  if(row.targetKey.empty())
    return "none";
  if(state.find("talk")!=std::string::npos)
    return "talking_to_target";
  if(state.find("attack")!=std::string::npos)
    return "attacking_target";
  return "targeting";
  }

bool npcStateTargetsPlayer(std::string_view aiStateName) {
  const std::string state = lowerAscii(aiStateName);
  return state.find("follow_player")!=std::string::npos ||
         state.find("guide_player")!=std::string::npos ||
         state.find("escort_player")!=std::string::npos;
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
  bool        captureBaseline = false;
  std::string saveSlotPath;
  bool        warnedNoBackend = false;
  bool        opened = false;

#if defined(OPENGOTHIC_HAVE_SQLITE)
  sqlite3* db = nullptr;
  int64_t  sessionId = 0;
#endif
  };

MmoRuntimeSqlite::MmoRuntimeSqlite(std::string path, uint64_t intervalMs, bool restoreState,
                                   bool captureBaseline, std::string saveSlotPath)
  : impl(new Impl()) {
  impl->path         = std::move(path);
  impl->intervalMs   = std::max<uint64_t>(250, intervalMs);
  impl->untilFlush   = 0;
  impl->restoreState = restoreState;
  impl->captureBaseline = captureBaseline;
  impl->saveSlotPath = std::move(saveSlotPath);
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
  const int64_t previousSchemaVersion = parseInt64(runtimeMetaValue(impl->db, "schema_version"), 0);

  const char* schema = R"SQL(
    PRAGMA journal_mode=WAL;
    PRAGMA synchronous=NORMAL;
    CREATE TABLE IF NOT EXISTS runtime_schema_meta (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
    INSERT OR REPLACE INTO runtime_schema_meta(key, value) VALUES
      ('schema_name', 'opengothic_runtime_mmo'),
      ('schema_version', '25');
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
    CREATE TABLE IF NOT EXISTS runtime_character_wallet (
      character_key TEXT NOT NULL,
      currency_key TEXT NOT NULL,
      currency_display_name TEXT NOT NULL DEFAULT '',
      item_template_symbol INTEGER NOT NULL,
      amount INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(character_key, currency_key)
    );
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
    CREATE TABLE IF NOT EXISTS runtime_story_progress_current (
      character_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL DEFAULT '',
      tick_count INTEGER NOT NULL,
      chapter_number INTEGER NOT NULL,
      chapter_key TEXT NOT NULL DEFAULT '',
      source_global_key TEXT NOT NULL DEFAULT '',
      source_symbol_index INTEGER NOT NULL,
      source_symbol_name TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_story_progress_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      character_key TEXT NOT NULL,
      world_name TEXT NOT NULL DEFAULT '',
      tick_count INTEGER NOT NULL,
      chapter_before INTEGER,
      chapter_after INTEGER NOT NULL,
      chapter_key TEXT NOT NULL DEFAULT '',
      source_global_key TEXT NOT NULL DEFAULT '',
      source_symbol_index INTEGER NOT NULL,
      source_symbol_name TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_story_progress_history_character
      ON runtime_story_progress_history(character_key, tick_count);
    CREATE TABLE IF NOT EXISTS runtime_chapter_intro_events (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      session_id INTEGER,
      world_name TEXT NOT NULL DEFAULT '',
      tick_count INTEGER NOT NULL,
      title TEXT NOT NULL DEFAULT '',
      subtitle TEXT NOT NULL DEFAULT '',
      image TEXT NOT NULL DEFAULT '',
      sound TEXT NOT NULL DEFAULT '',
      duration INTEGER NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_chapter_intro_events_tick
      ON runtime_chapter_intro_events(session_id, tick_count);
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
    CREATE TABLE IF NOT EXISTS runtime_world_clock (
      world_name TEXT PRIMARY KEY,
      tick_count INTEGER NOT NULL,
      world_time_millis INTEGER NOT NULL,
      world_day INTEGER NOT NULL,
      world_hour INTEGER NOT NULL,
      world_minute INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS runtime_world_npc_inventory (
      owner_key TEXT NOT NULL,
      item_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      owner_display_name TEXT NOT NULL DEFAULT '',
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
      PRIMARY KEY(owner_key, item_key)
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_world_npc_inventory_owner
      ON runtime_world_npc_inventory(world_name, owner_key);
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
    CREATE TABLE IF NOT EXISTS runtime_script_global_values (
      global_key TEXT NOT NULL,
      value_index INTEGER NOT NULL,
      value_int INTEGER,
      value_real REAL,
      value_text TEXT,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(global_key, value_index)
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_script_global_values_key
      ON runtime_script_global_values(global_key);
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
    CREATE TABLE IF NOT EXISTS runtime_guild_attitudes (
      from_guild INTEGER NOT NULL,
      to_guild INTEGER NOT NULL,
      attitude INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(from_guild, to_guild)
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
    CREATE TABLE IF NOT EXISTS runtime_npc_stat_capture_state (
      entity_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      stat_signature TEXT NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_stat_capture_state_world
      ON runtime_npc_stat_capture_state(world_name);
    CREATE TABLE IF NOT EXISTS mmo_stat_definitions (
      stat_group TEXT NOT NULL,
      stat_id INTEGER NOT NULL,
      stat_key TEXT NOT NULL,
      stat_domain TEXT NOT NULL,
      stat_family TEXT NOT NULL,
      value_kind TEXT NOT NULL,
      persistence_hint TEXT NOT NULL,
      display_order INTEGER NOT NULL,
      PRIMARY KEY(stat_group, stat_id)
    );
    DELETE FROM mmo_stat_definitions;
    INSERT OR REPLACE INTO mmo_stat_definitions(
      stat_group, stat_id, stat_key, stat_domain, stat_family, value_kind, persistence_hint, display_order
    ) VALUES
      ('attribute', 0, 'hitpoints',        'resource',          'health',     'current',  'runtime_current',       10),
      ('attribute', 1, 'hitpoints_max',    'resource',          'health',     'maximum',  'content_or_character',  11),
      ('attribute', 2, 'mana',             'resource',          'mana',       'current',  'runtime_current',       20),
      ('attribute', 3, 'mana_max',         'resource',          'mana',       'maximum',  'content_or_character',  21),
      ('attribute', 4, 'strength',         'primary_attribute', 'attribute',  'absolute', 'content_or_character',  30),
      ('attribute', 5, 'dexterity',        'primary_attribute', 'attribute',  'absolute', 'content_or_character',  31),
      ('attribute', 6, 'regenerate_hp',    'regeneration',      'health',     'absolute', 'content_or_character',  40),
      ('attribute', 7, 'regenerate_mana',  'regeneration',      'mana',       'absolute', 'content_or_character',  41),
      ('protection', 0, 'barrier',         'resistance',        'protection', 'absolute', 'content_or_character', 100),
      ('protection', 1, 'blunt',           'resistance',        'protection', 'absolute', 'content_or_character', 101),
      ('protection', 2, 'edge',            'resistance',        'protection', 'absolute', 'content_or_character', 102),
      ('protection', 3, 'fire',            'resistance',        'protection', 'absolute', 'content_or_character', 103),
      ('protection', 4, 'fly',             'resistance',        'protection', 'absolute', 'content_or_character', 104),
      ('protection', 5, 'magic',           'resistance',        'protection', 'absolute', 'content_or_character', 105),
      ('protection', 6, 'point',           'resistance',        'protection', 'absolute', 'content_or_character', 106),
      ('protection', 7, 'fall',            'resistance',        'protection', 'absolute', 'content_or_character', 107);
    WITH talent_defs(stat_id, stat_key, stat_domain, display_order) AS (
      VALUES
        (0,  'unknown',            'reserved',        200),
        (1,  'one_handed',         'weapon_skill',    201),
        (2,  'two_handed',         'weapon_skill',    202),
        (3,  'bow',                'weapon_skill',    203),
        (4,  'crossbow',           'weapon_skill',    204),
        (5,  'picklock',           'trade_skill',     205),
        (6,  'talent:6',           'reserved',        206),
        (7,  'mage',               'magic_skill',     207),
        (8,  'sneak',              'utility_skill',   208),
        (9,  'regenerate',         'passive_skill',   209),
        (10, 'firemaster',         'magic_skill',     210),
        (11, 'acrobat',            'passive_skill',   211),
        (12, 'pickpocket',         'trade_skill',     212),
        (13, 'smith',              'trade_skill',     213),
        (14, 'runes',              'magic_skill',     214),
        (15, 'alchemy',            'trade_skill',     215),
        (16, 'take_animal_trophy', 'trade_skill',     216),
        (17, 'foreign_language',   'knowledge_skill', 217),
        (18, 'wisp_detector',      'addon_skill',     218),
        (19, 'talent_c',           'reserved',        219),
        (20, 'talent_d',           'reserved',        220),
        (21, 'talent_e',           'reserved',        221)
    )
    INSERT OR REPLACE INTO mmo_stat_definitions(
      stat_group, stat_id, stat_key, stat_domain, stat_family, value_kind, persistence_hint, display_order
    )
    SELECT 'talent_skill', stat_id, stat_key, stat_domain, 'talent', 'rank', 'content_or_character', display_order
      FROM talent_defs;
    WITH talent_defs(stat_id, stat_key, stat_domain, display_order) AS (
      VALUES
        (0,  'unknown',            'reserved',        300),
        (1,  'one_handed',         'weapon_skill',    301),
        (2,  'two_handed',         'weapon_skill',    302),
        (3,  'bow',                'weapon_skill',    303),
        (4,  'crossbow',           'weapon_skill',    304),
        (5,  'picklock',           'trade_skill',     305),
        (6,  'talent:6',           'reserved',        306),
        (7,  'mage',               'magic_skill',     307),
        (8,  'sneak',              'utility_skill',   308),
        (9,  'regenerate',         'passive_skill',   309),
        (10, 'firemaster',         'magic_skill',     310),
        (11, 'acrobat',            'passive_skill',   311),
        (12, 'pickpocket',         'trade_skill',     312),
        (13, 'smith',              'trade_skill',     313),
        (14, 'runes',              'magic_skill',     314),
        (15, 'alchemy',            'trade_skill',     315),
        (16, 'take_animal_trophy', 'trade_skill',     316),
        (17, 'foreign_language',   'knowledge_skill', 317),
        (18, 'wisp_detector',      'addon_skill',     318),
        (19, 'talent_c',           'reserved',        319),
        (20, 'talent_d',           'reserved',        320),
        (21, 'talent_e',           'reserved',        321)
    )
    INSERT OR REPLACE INTO mmo_stat_definitions(
      stat_group, stat_id, stat_key, stat_domain, stat_family, value_kind, persistence_hint, display_order
    )
    SELECT 'talent_value', stat_id, stat_key, stat_domain, 'talent', 'value', 'content_or_character', display_order
      FROM talent_defs;
    WITH talent_defs(stat_id, stat_key, stat_domain, display_order) AS (
      VALUES
        (0,  'unknown',            'reserved',      400),
        (1,  'one_handed',         'weapon_rating', 401),
        (2,  'two_handed',         'weapon_rating', 402),
        (3,  'bow',                'weapon_rating', 403),
        (4,  'crossbow',           'weapon_rating', 404),
        (5,  'picklock',           'trade_rating',  405),
        (6,  'talent:6',           'reserved',      406),
        (7,  'mage',               'magic_rating',  407),
        (8,  'sneak',              'utility_rating',408),
        (9,  'regenerate',         'passive_rating',409),
        (10, 'firemaster',         'magic_rating',  410),
        (11, 'acrobat',            'passive_rating',411),
        (12, 'pickpocket',         'trade_rating',  412),
        (13, 'smith',              'trade_rating',  413),
        (14, 'runes',              'magic_rating',  414),
        (15, 'alchemy',            'trade_rating',  415),
        (16, 'take_animal_trophy', 'trade_rating',  416),
        (17, 'foreign_language',   'knowledge_rating',417),
        (18, 'wisp_detector',      'addon_rating',  418),
        (19, 'talent_c',           'reserved',      419),
        (20, 'talent_d',           'reserved',      420),
        (21, 'talent_e',           'reserved',      421)
    )
    INSERT OR REPLACE INTO mmo_stat_definitions(
      stat_group, stat_id, stat_key, stat_domain, stat_family, value_kind, persistence_hint, display_order
    )
    SELECT 'hit_chance', stat_id, stat_key, stat_domain, 'combat_rating', 'percent', 'content_or_character', display_order
      FROM talent_defs;
    INSERT OR REPLACE INTO mmo_stat_definitions(
      stat_group, stat_id, stat_key, stat_domain, stat_family, value_kind, persistence_hint, display_order
    ) VALUES
      ('progression', 0, 'experience_next', 'progression', 'experience', 'absolute', 'content_or_character', 500),
      ('progression', 1, 'learning_points',  'progression', 'learning_points', 'absolute', 'character_current', 501),
      ('attitude',    0, 'permanent',        'relation',    'attitude', 'enum', 'world_checkpoint', 510),
      ('attitude',    1, 'temporary',        'relation',    'attitude', 'enum', 'runtime_current', 511);
    WITH RECURSIVE mission_defs(stat_id) AS (
      SELECT 0
      UNION ALL SELECT stat_id+1 FROM mission_defs WHERE stat_id<4
    )
    INSERT OR REPLACE INTO mmo_stat_definitions(
      stat_group, stat_id, stat_key, stat_domain, stat_family, value_kind, persistence_hint, display_order
    )
    SELECT 'mission', stat_id, 'slot_' || stat_id, 'script_state', 'mission', 'function_id', 'world_checkpoint', 520+stat_id
      FROM mission_defs;
    WITH RECURSIVE aivar_defs(stat_id) AS (
      SELECT 0
      UNION ALL SELECT stat_id+1 FROM aivar_defs WHERE stat_id<99
    )
    INSERT OR REPLACE INTO mmo_stat_definitions(
      stat_group, stat_id, stat_key, stat_domain, stat_family, value_kind, persistence_hint, display_order
    )
    SELECT 'aivar', stat_id, 'aivar_' || stat_id, 'script_state', 'ai_variable', 'absolute', 'world_checkpoint', 600+stat_id
      FROM aivar_defs;
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
      state_other_key TEXT NOT NULL DEFAULT '',
      state_victim_key TEXT NOT NULL DEFAULT '',
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
      state_other_key TEXT NOT NULL DEFAULT '',
      state_victim_key TEXT NOT NULL DEFAULT '',
      relation_kind TEXT NOT NULL DEFAULT '',
      changed_fields TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_ai_history_entity_tick
      ON runtime_npc_ai_history(entity_key, tick_count);
    CREATE TABLE IF NOT EXISTS runtime_npc_relation_checkpoints (
      entity_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      target_key TEXT NOT NULL DEFAULT '',
      other_key TEXT NOT NULL DEFAULT '',
      victim_key TEXT NOT NULL DEFAULT '',
      ai_state_function INTEGER NOT NULL,
      ai_state_name TEXT NOT NULL DEFAULT '',
      state_elapsed_millis INTEGER NOT NULL,
      relation_kind TEXT NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_relation_checkpoints_world
      ON runtime_npc_relation_checkpoints(world_name, relation_kind, target_key);
    CREATE TABLE IF NOT EXISTS runtime_waypoints (
      waypoint_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      kind TEXT NOT NULL,
      waypoint_index INTEGER NOT NULL,
      name TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      ground_x REAL,
      ground_y REAL,
      ground_z REAL,
      dir_x REAL,
      dir_y REAL,
      dir_z REAL,
      underwater INTEGER NOT NULL,
      free_point INTEGER NOT NULL,
      connected INTEGER NOT NULL,
      use_count INTEGER NOT NULL,
      ladder_key TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_waypoints_world_name
      ON runtime_waypoints(world_name, name);
    CREATE INDEX IF NOT EXISTS idx_runtime_waypoints_kind
      ON runtime_waypoints(world_name, kind);
    CREATE TABLE IF NOT EXISTS runtime_waypoint_edges (
      edge_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      from_waypoint_key TEXT NOT NULL,
      to_waypoint_key TEXT NOT NULL,
      from_name TEXT NOT NULL DEFAULT '',
      to_name TEXT NOT NULL DEFAULT '',
      distance INTEGER NOT NULL,
      ladder INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_waypoint_edges_from
      ON runtime_waypoint_edges(world_name, from_waypoint_key);
    CREATE INDEX IF NOT EXISTS idx_runtime_waypoint_edges_to
      ON runtime_waypoint_edges(world_name, to_waypoint_key);
    CREATE TABLE IF NOT EXISTS runtime_npc_routines (
      entity_key TEXT NOT NULL,
      routine_index INTEGER NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      start_minute INTEGER NOT NULL,
      end_minute INTEGER NOT NULL,
      callback_symbol_index INTEGER NOT NULL,
      callback_symbol_name TEXT NOT NULL DEFAULT '',
      waypoint_key TEXT NOT NULL DEFAULT '',
      waypoint_name TEXT NOT NULL DEFAULT '',
      active INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(entity_key, routine_index)
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_routines_waypoint
      ON runtime_npc_routines(world_name, waypoint_key);
    CREATE TABLE IF NOT EXISTS runtime_npc_navigation_state (
      entity_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      current_waypoint_key TEXT NOT NULL DEFAULT '',
      current_waypoint_name TEXT NOT NULL DEFAULT '',
      routine_waypoint_key TEXT NOT NULL DEFAULT '',
      routine_waypoint_name TEXT NOT NULL DEFAULT '',
      move_hint TEXT NOT NULL DEFAULT '',
      move_target_waypoint_key TEXT NOT NULL DEFAULT '',
      move_target_waypoint_name TEXT NOT NULL DEFAULT '',
      path_next_waypoint_key TEXT NOT NULL DEFAULT '',
      path_next_waypoint_name TEXT NOT NULL DEFAULT '',
      path_final_waypoint_key TEXT NOT NULL DEFAULT '',
      path_final_waypoint_name TEXT NOT NULL DEFAULT '',
      path_remaining_count INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_navigation_waypoint
      ON runtime_npc_navigation_state(world_name, current_waypoint_key, routine_waypoint_key, move_target_waypoint_key);
    CREATE TABLE IF NOT EXISTS runtime_npc_navigation_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      entity_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      current_waypoint_name TEXT NOT NULL DEFAULT '',
      routine_waypoint_name TEXT NOT NULL DEFAULT '',
      move_hint TEXT NOT NULL DEFAULT '',
      move_target_waypoint_name TEXT NOT NULL DEFAULT '',
      path_next_waypoint_name TEXT NOT NULL DEFAULT '',
      path_remaining_count INTEGER NOT NULL,
      changed_fields TEXT NOT NULL DEFAULT '',
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_runtime_npc_navigation_history_entity_tick
      ON runtime_npc_navigation_history(entity_key, tick_count);
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
    UPDATE runtime_waypoints SET name='waypoint:' || waypoint_index WHERE name IS NULL OR name='';
    UPDATE runtime_npc_routines SET display_name='npc:' || entity_key WHERE display_name IS NULL OR display_name='';
    UPDATE runtime_npc_routines SET callback_symbol_name='function:' || callback_symbol_index WHERE callback_symbol_name IS NULL OR callback_symbol_name='';
    UPDATE runtime_npc_navigation_state SET display_name='npc:' || entity_key WHERE display_name IS NULL OR display_name='';
    UPDATE runtime_npc_navigation_state SET move_hint='none' WHERE move_hint IS NULL OR move_hint='';
    UPDATE runtime_npc_navigation_history SET display_name='npc:' || entity_key WHERE display_name IS NULL OR display_name='';
    UPDATE runtime_npc_navigation_history SET move_hint='none' WHERE move_hint IS NULL OR move_hint='';
  )SQL";
  if(!exec(impl->db, schemaNpcState))
    return false;
  if(!ensureColumn(impl->db, "runtime_npc_ai_state", "state_other_key", "TEXT NOT NULL DEFAULT ''"))
    return false;
  if(!ensureColumn(impl->db, "runtime_npc_ai_state", "state_victim_key", "TEXT NOT NULL DEFAULT ''"))
    return false;
  if(!ensureColumn(impl->db, "runtime_npc_ai_history", "state_other_key", "TEXT NOT NULL DEFAULT ''"))
    return false;
  if(!ensureColumn(impl->db, "runtime_npc_ai_history", "state_victim_key", "TEXT NOT NULL DEFAULT ''"))
    return false;
  if(!exec(impl->db,
      "CREATE INDEX IF NOT EXISTS idx_runtime_npc_ai_state_other "
      "ON runtime_npc_ai_state(world_name, state_other_key)"))
    return false;

  const char* schemaMmoCurrent = R"SQL(
    CREATE TABLE IF NOT EXISTS mmo_unit_stat_current (
      unit_key TEXT NOT NULL,
      unit_type TEXT NOT NULL,
      character_key TEXT NOT NULL DEFAULT '',
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      template_symbol_index INTEGER,
      script_id INTEGER,
      display_name TEXT NOT NULL DEFAULT '',
      player INTEGER NOT NULL,
      stat_domain TEXT NOT NULL DEFAULT '',
      stat_family TEXT NOT NULL DEFAULT '',
      stat_group TEXT NOT NULL,
      stat_id INTEGER NOT NULL,
      stat_key TEXT NOT NULL,
      value_kind TEXT NOT NULL DEFAULT '',
      persistence_hint TEXT NOT NULL DEFAULT '',
      display_order INTEGER NOT NULL DEFAULT 9999,
      value INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(unit_key, stat_group, stat_id)
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_unit_stat_current_domain
      ON mmo_unit_stat_current(world_name, unit_type, stat_domain, stat_key);
    CREATE INDEX IF NOT EXISTS idx_mmo_unit_stat_current_character
      ON mmo_unit_stat_current(character_key, stat_group, stat_id);
    CREATE INDEX IF NOT EXISTS idx_mmo_unit_stat_current_template
      ON mmo_unit_stat_current(world_name, template_symbol_index);
    CREATE TABLE IF NOT EXISTS mmo_unit_stat_sheet_current (
      unit_key TEXT PRIMARY KEY,
      unit_type TEXT NOT NULL,
      character_key TEXT NOT NULL DEFAULT '',
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      template_symbol_index INTEGER,
      script_id INTEGER,
      display_name TEXT NOT NULL DEFAULT '',
      player INTEGER NOT NULL,
      guild INTEGER,
      true_guild INTEGER,
      level INTEGER,
      experience INTEGER,
      experience_next INTEGER,
      learning_points INTEGER,
      permanent_attitude INTEGER,
      temporary_attitude INTEGER,
      dead INTEGER NOT NULL DEFAULT 0,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      waypoint TEXT NOT NULL DEFAULT '',
      health_current INTEGER,
      health_max INTEGER,
      mana_current INTEGER,
      mana_max INTEGER,
      strength INTEGER,
      dexterity INTEGER,
      regenerate_hp INTEGER,
      regenerate_mana INTEGER,
      resist_barrier INTEGER,
      resist_blunt INTEGER,
      resist_edge INTEGER,
      resist_fire INTEGER,
      resist_fly INTEGER,
      resist_magic INTEGER,
      resist_point INTEGER,
      resist_fall INTEGER,
      one_handed_skill INTEGER,
      two_handed_skill INTEGER,
      bow_skill INTEGER,
      crossbow_skill INTEGER,
      one_handed_hit_chance INTEGER,
      two_handed_hit_chance INTEGER,
      bow_hit_chance INTEGER,
      crossbow_hit_chance INTEGER,
      picklock_skill INTEGER,
      sneak_skill INTEGER,
      pickpocket_skill INTEGER,
      smith_skill INTEGER,
      alchemy_skill INTEGER,
      take_animal_trophy_skill INTEGER,
      foreign_language_skill INTEGER,
      acrobat_skill INTEGER,
      mage_skill INTEGER,
      runes_skill INTEGER,
      firemaster_skill INTEGER,
      regenerate_skill INTEGER,
      wisp_detector_skill INTEGER,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_unit_stat_sheet_current_world
      ON mmo_unit_stat_sheet_current(world_name, unit_type, template_symbol_index);
    CREATE INDEX IF NOT EXISTS idx_mmo_unit_stat_sheet_current_character
      ON mmo_unit_stat_sheet_current(character_key);
    CREATE TABLE IF NOT EXISTS mmo_creature_templates_current (
      world_name TEXT NOT NULL,
      creature_template_key TEXT NOT NULL,
      creature_template_id INTEGER NOT NULL,
      script_id INTEGER,
      display_name TEXT NOT NULL DEFAULT '',
      guild INTEGER,
      true_guild INTEGER,
      spawn_count INTEGER NOT NULL,
      min_level INTEGER,
      max_level INTEGER,
      base_health_max INTEGER,
      base_mana_max INTEGER,
      base_strength INTEGER,
      base_dexterity INTEGER,
      resist_blunt INTEGER,
      resist_edge INTEGER,
      resist_fire INTEGER,
      resist_magic INTEGER,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(world_name, creature_template_key)
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_creature_templates_current_entry
      ON mmo_creature_templates_current(world_name, creature_template_id);
    CREATE TABLE IF NOT EXISTS mmo_creature_spawns_current (
      creature_spawn_key TEXT PRIMARY KEY,
      creature_template_id INTEGER NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      waypoint TEXT NOT NULL DEFAULT '',
      dead INTEGER NOT NULL DEFAULT 0,
      level INTEGER,
      experience INTEGER,
      health_current INTEGER,
      health_max INTEGER,
      mana_current INTEGER,
      mana_max INTEGER,
      strength INTEGER,
      dexterity INTEGER,
      current_waypoint_name TEXT NOT NULL DEFAULT '',
      routine_waypoint_name TEXT NOT NULL DEFAULT '',
      move_hint TEXT NOT NULL DEFAULT '',
      move_target_waypoint_name TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_creature_spawns_current_template
      ON mmo_creature_spawns_current(world_name, creature_template_id);
    CREATE INDEX IF NOT EXISTS idx_mmo_creature_spawns_current_alive
      ON mmo_creature_spawns_current(world_name, dead);
    CREATE TABLE IF NOT EXISTS mmo_world_clock_current (
      world_name TEXT PRIMARY KEY,
      tick_count INTEGER NOT NULL,
      world_time_millis INTEGER NOT NULL,
      world_day INTEGER NOT NULL,
      world_hour INTEGER NOT NULL,
      world_minute INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE TABLE IF NOT EXISTS mmo_creature_inventory_current (
      creature_spawn_key TEXT NOT NULL,
      item_instance_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
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
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(creature_spawn_key, item_instance_key)
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_creature_inventory_current_template
      ON mmo_creature_inventory_current(world_name, creature_spawn_key, item_template_symbol);
    CREATE TABLE IF NOT EXISTS mmo_creature_inventory_snapshots_current (
      creature_spawn_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      item_row_count INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_creature_inventory_snapshots_current_world
      ON mmo_creature_inventory_snapshots_current(world_name, item_row_count);
    CREATE TABLE IF NOT EXISTS mmo_creature_relations_current (
      creature_spawn_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      relation_kind TEXT NOT NULL,
      target_key TEXT NOT NULL DEFAULT '',
      other_key TEXT NOT NULL DEFAULT '',
      victim_key TEXT NOT NULL DEFAULT '',
      ai_state_function INTEGER NOT NULL,
      ai_state_name TEXT NOT NULL DEFAULT '',
      state_elapsed_millis INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_creature_relations_current_world
      ON mmo_creature_relations_current(world_name, relation_kind, target_key);
    CREATE TABLE IF NOT EXISTS mmo_characters_current (
      character_key TEXT PRIMARY KEY,
      account_key TEXT NOT NULL,
      realm_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      health_current INTEGER,
      health_max INTEGER,
      mana_current INTEGER,
      mana_max INTEGER,
      level INTEGER,
      experience INTEGER,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_characters_current_realm
      ON mmo_characters_current(realm_key, world_name);
    CREATE TABLE IF NOT EXISTS mmo_character_inventory_current (
      character_key TEXT NOT NULL,
      item_instance_key TEXT NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
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
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(character_key, item_instance_key)
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_character_inventory_current_template
      ON mmo_character_inventory_current(character_key, item_template_symbol);
    CREATE INDEX IF NOT EXISTS idx_mmo_character_inventory_current_equipped
      ON mmo_character_inventory_current(character_key, equipped, slot);
    CREATE TABLE IF NOT EXISTS mmo_character_wallet_current (
      character_key TEXT NOT NULL,
      currency_key TEXT NOT NULL,
      currency_display_name TEXT NOT NULL DEFAULT '',
      item_template_symbol INTEGER NOT NULL,
      amount INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(character_key, currency_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_character_quests_current (
      character_key TEXT NOT NULL,
      quest_key TEXT NOT NULL,
      quest_name TEXT NOT NULL DEFAULT '',
      section INTEGER NOT NULL,
      status INTEGER NOT NULL,
      entry_count INTEGER NOT NULL,
      entries_text TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(character_key, quest_key)
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_character_quests_current_status
      ON mmo_character_quests_current(character_key, status);
    CREATE TABLE IF NOT EXISTS mmo_character_known_dialogs_current (
      character_key TEXT NOT NULL,
      npc_symbol_index INTEGER NOT NULL,
      info_symbol_index INTEGER NOT NULL,
      npc_symbol_name TEXT NOT NULL DEFAULT '',
      info_symbol_name TEXT NOT NULL DEFAULT '',
      description TEXT NOT NULL DEFAULT '',
      permanent INTEGER,
      first_seen_tick INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(character_key, npc_symbol_index, info_symbol_index)
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_character_known_dialogs_current_info
      ON mmo_character_known_dialogs_current(character_key, info_symbol_index);
    CREATE TABLE IF NOT EXISTS mmo_character_story_progress_current (
      character_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL DEFAULT '',
      tick_count INTEGER NOT NULL,
      chapter_number INTEGER NOT NULL,
      chapter_key TEXT NOT NULL DEFAULT '',
      source_global_key TEXT NOT NULL DEFAULT '',
      source_symbol_index INTEGER NOT NULL,
      source_symbol_name TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_character_story_progress_current_chapter
      ON mmo_character_story_progress_current(chapter_number, world_name);
    CREATE TABLE IF NOT EXISTS mmo_world_items_current (
      item_spawn_key TEXT PRIMARY KEY,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      slot_id INTEGER NOT NULL,
      persistent_id INTEGER NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      script_id INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
      visual TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      main_flag INTEGER NOT NULL,
      item_flags INTEGER NOT NULL,
      value INTEGER NOT NULL,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      exists_in_world INTEGER NOT NULL DEFAULT 1,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_world_items_current_world
      ON mmo_world_items_current(world_name, exists_in_world, item_template_symbol);
    CREATE TABLE IF NOT EXISTS mmo_world_interactives_current (
      interactive_key TEXT PRIMARY KEY,
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
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_world_interactives_current_world
      ON mmo_world_interactives_current(world_name, slot_id);
    CREATE TABLE IF NOT EXISTS mmo_world_container_inventory_current (
      owner_key TEXT NOT NULL,
      item_instance_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      owner_display_name TEXT NOT NULL DEFAULT '',
      item_template_symbol INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      iterator_count INTEGER NOT NULL,
      value INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(owner_key, item_instance_key)
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_world_container_inventory_current_world
      ON mmo_world_container_inventory_current(world_name, owner_key);
    CREATE TABLE IF NOT EXISTS mmo_script_globals_current (
      global_key TEXT PRIMARY KEY,
      symbol_index INTEGER NOT NULL,
      symbol_name TEXT NOT NULL DEFAULT '',
      value_type TEXT NOT NULL,
      category TEXT NOT NULL DEFAULT '',
      value_count INTEGER NOT NULL,
      value_text TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_script_globals_current_category
      ON mmo_script_globals_current(category, value_type);
    CREATE TABLE IF NOT EXISTS mmo_script_global_values_current (
      global_key TEXT NOT NULL,
      value_index INTEGER NOT NULL,
      value_int INTEGER,
      value_real REAL,
      value_text TEXT,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(global_key, value_index)
    );
    CREATE TABLE IF NOT EXISTS mmo_guild_attitudes_current (
      realm_key TEXT NOT NULL,
      from_guild INTEGER NOT NULL,
      to_guild INTEGER NOT NULL,
      attitude INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(realm_key, from_guild, to_guild)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_templates (
      world_template_key TEXT PRIMARY KEY,
      game_target TEXT NOT NULL,
      content_revision_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      baseline_tick INTEGER NOT NULL,
      baseline_world_time_millis INTEGER NOT NULL,
      baseline_captured_at TEXT,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      UNIQUE(game_target, content_revision_key, world_name)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_instances (
      world_instance_key TEXT PRIMARY KEY,
      realm_key TEXT NOT NULL,
      world_template_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      lifecycle_state TEXT NOT NULL DEFAULT 'active',
      baseline_tick INTEGER NOT NULL,
      current_tick INTEGER NOT NULL,
      current_world_time_millis INTEGER NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_world_instances_realm
      ON mmo_world_instances(realm_key, world_template_key);
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_creature_templates (
      world_template_key TEXT NOT NULL,
      creature_template_key TEXT NOT NULL,
      creature_template_id INTEGER NOT NULL,
      script_id INTEGER,
      display_name TEXT NOT NULL DEFAULT '',
      guild INTEGER,
      true_guild INTEGER,
      spawn_count INTEGER NOT NULL,
      min_level INTEGER,
      max_level INTEGER,
      base_health_max INTEGER,
      base_mana_max INTEGER,
      base_strength INTEGER,
      base_dexterity INTEGER,
      resist_blunt INTEGER,
      resist_edge INTEGER,
      resist_fire INTEGER,
      resist_magic INTEGER,
      PRIMARY KEY(world_template_key, creature_template_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_creatures (
      world_template_key TEXT NOT NULL,
      creature_spawn_key TEXT NOT NULL,
      creature_template_id INTEGER NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      waypoint TEXT NOT NULL DEFAULT '',
      dead INTEGER NOT NULL,
      level INTEGER,
      experience INTEGER,
      health_current INTEGER,
      health_max INTEGER,
      mana_current INTEGER,
      mana_max INTEGER,
      strength INTEGER,
      dexterity INTEGER,
      current_waypoint_name TEXT NOT NULL DEFAULT '',
      routine_waypoint_name TEXT NOT NULL DEFAULT '',
      move_hint TEXT NOT NULL DEFAULT '',
      move_target_waypoint_name TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(world_template_key, creature_spawn_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_creature_stats (
      world_template_key TEXT NOT NULL,
      creature_spawn_key TEXT NOT NULL,
      stat_group TEXT NOT NULL,
      stat_id INTEGER NOT NULL,
      stat_key TEXT NOT NULL,
      value INTEGER NOT NULL,
      PRIMARY KEY(world_template_key, creature_spawn_key, stat_group, stat_id)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_creature_inventory (
      world_template_key TEXT NOT NULL,
      creature_spawn_key TEXT NOT NULL,
      item_instance_key TEXT NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      iterator_count INTEGER NOT NULL,
      equipped INTEGER NOT NULL,
      equip_count INTEGER NOT NULL,
      slot INTEGER NOT NULL,
      main_flag INTEGER NOT NULL,
      item_flags INTEGER NOT NULL,
      value INTEGER NOT NULL,
      spell_id INTEGER NOT NULL,
      PRIMARY KEY(world_template_key, creature_spawn_key, item_instance_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_creature_inventory_snapshots (
      world_template_key TEXT NOT NULL,
      creature_spawn_key TEXT NOT NULL,
      item_row_count INTEGER NOT NULL,
      PRIMARY KEY(world_template_key, creature_spawn_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_items (
      world_template_key TEXT NOT NULL,
      item_spawn_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      slot_id INTEGER NOT NULL,
      persistent_id INTEGER NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      script_id INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
      visual TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      main_flag INTEGER NOT NULL,
      item_flags INTEGER NOT NULL,
      value INTEGER NOT NULL,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      PRIMARY KEY(world_template_key, item_spawn_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_interactives (
      world_template_key TEXT NOT NULL,
      interactive_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
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
      PRIMARY KEY(world_template_key, interactive_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_container_inventory (
      world_template_key TEXT NOT NULL,
      owner_key TEXT NOT NULL,
      item_instance_key TEXT NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      iterator_count INTEGER NOT NULL,
      value INTEGER NOT NULL,
      PRIMARY KEY(world_template_key, owner_key, item_instance_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_script_globals (
      world_template_key TEXT NOT NULL,
      global_key TEXT NOT NULL,
      symbol_index INTEGER NOT NULL,
      symbol_name TEXT NOT NULL DEFAULT '',
      value_type TEXT NOT NULL,
      category TEXT NOT NULL DEFAULT '',
      value_count INTEGER NOT NULL,
      value_text TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(world_template_key, global_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_script_global_values (
      world_template_key TEXT NOT NULL,
      global_key TEXT NOT NULL,
      value_index INTEGER NOT NULL,
      value_int INTEGER,
      value_real REAL,
      value_text TEXT,
      PRIMARY KEY(world_template_key, global_key, value_index)
    );
    CREATE TABLE IF NOT EXISTS mmo_world_baseline_guild_attitudes (
      world_template_key TEXT NOT NULL,
      from_guild INTEGER NOT NULL,
      to_guild INTEGER NOT NULL,
      attitude INTEGER NOT NULL,
      PRIMARY KEY(world_template_key, from_guild, to_guild)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slots (
      slot_key TEXT PRIMARY KEY,
      account_key TEXT NOT NULL,
      realm_key TEXT NOT NULL,
      character_key TEXT NOT NULL,
      source_slot_path TEXT NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      world_name TEXT NOT NULL DEFAULT '',
      tick_count INTEGER NOT NULL DEFAULT 0,
      world_time_millis INTEGER NOT NULL DEFAULT 0,
      schema_version INTEGER NOT NULL DEFAULT 25,
      legacy_save_file INTEGER NOT NULL DEFAULT 1,
      current_snapshot_id INTEGER,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      last_saved_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE UNIQUE INDEX IF NOT EXISTS idx_mmo_save_slots_source
      ON mmo_save_slots(source_slot_path);
    CREATE INDEX IF NOT EXISTS idx_mmo_save_slots_character
      ON mmo_save_slots(realm_key, account_key, character_key, updated_at);
    CREATE TABLE IF NOT EXISTS mmo_save_slot_snapshots (
      snapshot_id INTEGER PRIMARY KEY AUTOINCREMENT,
      slot_key TEXT NOT NULL,
      account_key TEXT NOT NULL,
      realm_key TEXT NOT NULL,
      character_key TEXT NOT NULL,
      source_slot_path TEXT NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      world_name TEXT NOT NULL DEFAULT '',
      tick_count INTEGER NOT NULL DEFAULT 0,
      world_time_millis INTEGER NOT NULL DEFAULT 0,
      schema_version INTEGER NOT NULL DEFAULT 25,
      session_id INTEGER,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_mmo_save_slot_snapshots_slot
      ON mmo_save_slot_snapshots(slot_key, snapshot_id);
    CREATE TABLE IF NOT EXISTS mmo_save_slot_characters (
      snapshot_id INTEGER NOT NULL,
      character_key TEXT NOT NULL,
      account_key TEXT NOT NULL,
      realm_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      health_current INTEGER,
      health_max INTEGER,
      mana_current INTEGER,
      mana_max INTEGER,
      level INTEGER,
      experience INTEGER,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, character_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_unit_stat (
      snapshot_id INTEGER NOT NULL,
      unit_key TEXT NOT NULL,
      unit_type TEXT NOT NULL,
      character_key TEXT NOT NULL DEFAULT '',
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      template_symbol_index INTEGER,
      script_id INTEGER,
      display_name TEXT NOT NULL DEFAULT '',
      player INTEGER NOT NULL,
      stat_domain TEXT NOT NULL DEFAULT '',
      stat_family TEXT NOT NULL DEFAULT '',
      stat_group TEXT NOT NULL,
      stat_id INTEGER NOT NULL,
      stat_key TEXT NOT NULL,
      value_kind TEXT NOT NULL DEFAULT '',
      persistence_hint TEXT NOT NULL DEFAULT '',
      display_order INTEGER NOT NULL DEFAULT 9999,
      value INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, unit_key, stat_group, stat_id)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_unit_stat_sheet (
      snapshot_id INTEGER NOT NULL,
      unit_key TEXT NOT NULL,
      unit_type TEXT NOT NULL,
      character_key TEXT NOT NULL DEFAULT '',
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      template_symbol_index INTEGER,
      script_id INTEGER,
      display_name TEXT NOT NULL DEFAULT '',
      player INTEGER NOT NULL,
      guild INTEGER,
      true_guild INTEGER,
      level INTEGER,
      experience INTEGER,
      experience_next INTEGER,
      learning_points INTEGER,
      permanent_attitude INTEGER,
      temporary_attitude INTEGER,
      dead INTEGER NOT NULL DEFAULT 0,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      waypoint TEXT NOT NULL DEFAULT '',
      health_current INTEGER,
      health_max INTEGER,
      mana_current INTEGER,
      mana_max INTEGER,
      strength INTEGER,
      dexterity INTEGER,
      regenerate_hp INTEGER,
      regenerate_mana INTEGER,
      resist_barrier INTEGER,
      resist_blunt INTEGER,
      resist_edge INTEGER,
      resist_fire INTEGER,
      resist_fly INTEGER,
      resist_magic INTEGER,
      resist_point INTEGER,
      resist_fall INTEGER,
      one_handed_skill INTEGER,
      two_handed_skill INTEGER,
      bow_skill INTEGER,
      crossbow_skill INTEGER,
      one_handed_hit_chance INTEGER,
      two_handed_hit_chance INTEGER,
      bow_hit_chance INTEGER,
      crossbow_hit_chance INTEGER,
      picklock_skill INTEGER,
      sneak_skill INTEGER,
      pickpocket_skill INTEGER,
      smith_skill INTEGER,
      alchemy_skill INTEGER,
      take_animal_trophy_skill INTEGER,
      foreign_language_skill INTEGER,
      acrobat_skill INTEGER,
      mage_skill INTEGER,
      runes_skill INTEGER,
      firemaster_skill INTEGER,
      regenerate_skill INTEGER,
      wisp_detector_skill INTEGER,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, unit_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_character_inventory (
      snapshot_id INTEGER NOT NULL,
      character_key TEXT NOT NULL,
      item_instance_key TEXT NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
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
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, character_key, item_instance_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_character_wallet (
      snapshot_id INTEGER NOT NULL,
      character_key TEXT NOT NULL,
      currency_key TEXT NOT NULL,
      currency_display_name TEXT NOT NULL DEFAULT '',
      item_template_symbol INTEGER NOT NULL,
      amount INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, character_key, currency_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_character_quests (
      snapshot_id INTEGER NOT NULL,
      character_key TEXT NOT NULL,
      quest_key TEXT NOT NULL,
      quest_name TEXT NOT NULL DEFAULT '',
      section INTEGER NOT NULL,
      status INTEGER NOT NULL,
      entry_count INTEGER NOT NULL,
      entries_text TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, character_key, quest_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_character_known_dialogs (
      snapshot_id INTEGER NOT NULL,
      character_key TEXT NOT NULL,
      npc_symbol_index INTEGER NOT NULL,
      info_symbol_index INTEGER NOT NULL,
      npc_symbol_name TEXT NOT NULL DEFAULT '',
      info_symbol_name TEXT NOT NULL DEFAULT '',
      description TEXT NOT NULL DEFAULT '',
      permanent INTEGER,
      first_seen_tick INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, character_key, npc_symbol_index, info_symbol_index)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_character_story_progress (
      snapshot_id INTEGER NOT NULL,
      character_key TEXT NOT NULL,
      world_name TEXT NOT NULL DEFAULT '',
      tick_count INTEGER NOT NULL,
      chapter_number INTEGER NOT NULL,
      chapter_key TEXT NOT NULL DEFAULT '',
      source_global_key TEXT NOT NULL DEFAULT '',
      source_symbol_index INTEGER NOT NULL,
      source_symbol_name TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, character_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_world_clock (
      snapshot_id INTEGER NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      world_time_millis INTEGER NOT NULL,
      world_day INTEGER NOT NULL,
      world_hour INTEGER NOT NULL,
      world_minute INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, world_name)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_creature_spawns (
      snapshot_id INTEGER NOT NULL,
      creature_spawn_key TEXT NOT NULL,
      creature_template_id INTEGER NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      rotation REAL,
      waypoint TEXT NOT NULL DEFAULT '',
      dead INTEGER NOT NULL DEFAULT 0,
      level INTEGER,
      experience INTEGER,
      health_current INTEGER,
      health_max INTEGER,
      mana_current INTEGER,
      mana_max INTEGER,
      strength INTEGER,
      dexterity INTEGER,
      current_waypoint_name TEXT NOT NULL DEFAULT '',
      routine_waypoint_name TEXT NOT NULL DEFAULT '',
      move_hint TEXT NOT NULL DEFAULT '',
      move_target_waypoint_name TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, creature_spawn_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_creature_inventory (
      snapshot_id INTEGER NOT NULL,
      creature_spawn_key TEXT NOT NULL,
      item_instance_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
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
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, creature_spawn_key, item_instance_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_creature_inventory_snapshots (
      snapshot_id INTEGER NOT NULL,
      creature_spawn_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      item_row_count INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, creature_spawn_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_creature_relations (
      snapshot_id INTEGER NOT NULL,
      creature_spawn_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      relation_kind TEXT NOT NULL,
      target_key TEXT NOT NULL DEFAULT '',
      other_key TEXT NOT NULL DEFAULT '',
      victim_key TEXT NOT NULL DEFAULT '',
      ai_state_function INTEGER NOT NULL,
      ai_state_name TEXT NOT NULL DEFAULT '',
      state_elapsed_millis INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, creature_spawn_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_world_items (
      snapshot_id INTEGER NOT NULL,
      item_spawn_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      tick_count INTEGER NOT NULL,
      slot_id INTEGER NOT NULL,
      persistent_id INTEGER NOT NULL,
      item_template_symbol INTEGER NOT NULL,
      script_id INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
      visual TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      main_flag INTEGER NOT NULL,
      item_flags INTEGER NOT NULL,
      value INTEGER NOT NULL,
      pos_x REAL,
      pos_y REAL,
      pos_z REAL,
      exists_in_world INTEGER NOT NULL DEFAULT 1,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, item_spawn_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_world_interactives (
      snapshot_id INTEGER NOT NULL,
      interactive_key TEXT NOT NULL,
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
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, interactive_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_world_container_inventory (
      snapshot_id INTEGER NOT NULL,
      owner_key TEXT NOT NULL,
      item_instance_key TEXT NOT NULL,
      world_name TEXT NOT NULL,
      owner_display_name TEXT NOT NULL DEFAULT '',
      item_template_symbol INTEGER NOT NULL,
      item_display_name TEXT NOT NULL DEFAULT '',
      amount INTEGER NOT NULL,
      iterator_count INTEGER NOT NULL,
      value INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, owner_key, item_instance_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_script_globals (
      snapshot_id INTEGER NOT NULL,
      global_key TEXT NOT NULL,
      symbol_index INTEGER NOT NULL,
      symbol_name TEXT NOT NULL DEFAULT '',
      value_type TEXT NOT NULL,
      category TEXT NOT NULL DEFAULT '',
      value_count INTEGER NOT NULL,
      value_text TEXT NOT NULL DEFAULT '',
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      persistence_class TEXT NOT NULL DEFAULT '',
      PRIMARY KEY(snapshot_id, global_key)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_script_global_values (
      snapshot_id INTEGER NOT NULL,
      global_key TEXT NOT NULL,
      value_index INTEGER NOT NULL,
      value_int INTEGER,
      value_real REAL,
      value_text TEXT,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(snapshot_id, global_key, value_index)
    );
    CREATE TABLE IF NOT EXISTS mmo_save_slot_guild_attitudes (
      snapshot_id INTEGER NOT NULL,
      realm_key TEXT NOT NULL,
      from_guild INTEGER NOT NULL,
      to_guild INTEGER NOT NULL,
      attitude INTEGER NOT NULL,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      PRIMARY KEY(snapshot_id, realm_key, from_guild, to_guild)
    );
  )SQL";
  if(!exec(impl->db, schemaMmoCurrent))
    return false;
  for(const char* table : {"mmo_unit_stat_sheet_current", "mmo_save_slot_unit_stat_sheet"}) {
    if(!ensureColumn(impl->db, table, "experience_next", "INTEGER"))
      return false;
    if(!ensureColumn(impl->db, table, "learning_points", "INTEGER"))
      return false;
    if(!ensureColumn(impl->db, table, "permanent_attitude", "INTEGER"))
      return false;
    if(!ensureColumn(impl->db, table, "temporary_attitude", "INTEGER"))
      return false;
    }
  if(previousSchemaVersion<25 && !exec(impl->db, R"SQL(
      UPDATE mmo_unit_stat_sheet_current
         SET experience_next = (
               SELECT value FROM mmo_unit_stat_current s
                WHERE s.unit_key=mmo_unit_stat_sheet_current.unit_key
                  AND s.stat_group='progression' AND s.stat_key='experience_next'
             ),
             learning_points = (
               SELECT value FROM mmo_unit_stat_current s
                WHERE s.unit_key=mmo_unit_stat_sheet_current.unit_key
                  AND s.stat_group='progression' AND s.stat_key='learning_points'
             ),
             permanent_attitude = (
               SELECT value FROM mmo_unit_stat_current s
                WHERE s.unit_key=mmo_unit_stat_sheet_current.unit_key
                  AND s.stat_group='attitude' AND s.stat_key='permanent'
             ),
             temporary_attitude = (
               SELECT value FROM mmo_unit_stat_current s
                WHERE s.unit_key=mmo_unit_stat_sheet_current.unit_key
                  AND s.stat_group='attitude' AND s.stat_key='temporary'
             );
      UPDATE mmo_save_slot_unit_stat_sheet
         SET experience_next = (
               SELECT value FROM mmo_save_slot_unit_stat s
                WHERE s.snapshot_id=mmo_save_slot_unit_stat_sheet.snapshot_id
                  AND s.unit_key=mmo_save_slot_unit_stat_sheet.unit_key
                  AND s.stat_group='progression' AND s.stat_key='experience_next'
             ),
             learning_points = (
               SELECT value FROM mmo_save_slot_unit_stat s
                WHERE s.snapshot_id=mmo_save_slot_unit_stat_sheet.snapshot_id
                  AND s.unit_key=mmo_save_slot_unit_stat_sheet.unit_key
                  AND s.stat_group='progression' AND s.stat_key='learning_points'
             ),
             permanent_attitude = (
               SELECT value FROM mmo_save_slot_unit_stat s
                WHERE s.snapshot_id=mmo_save_slot_unit_stat_sheet.snapshot_id
                  AND s.unit_key=mmo_save_slot_unit_stat_sheet.unit_key
                  AND s.stat_group='attitude' AND s.stat_key='permanent'
             ),
             temporary_attitude = (
               SELECT value FROM mmo_save_slot_unit_stat s
                WHERE s.snapshot_id=mmo_save_slot_unit_stat_sheet.snapshot_id
                  AND s.unit_key=mmo_save_slot_unit_stat_sheet.unit_key
                  AND s.stat_group='attitude' AND s.stat_key='temporary'
             );
    )SQL"))
    return false;

  const char* schemaViews = R"SQL(
    DROP VIEW IF EXISTS v_mmo_persistence_contract;
    DROP VIEW IF EXISTS v_mmo_restore_readiness;
    DROP VIEW IF EXISTS v_mmo_event_journal;
    DROP VIEW IF EXISTS v_mmo_world_script_global_deltas;
    DROP VIEW IF EXISTS v_mmo_world_interactive_deltas;
    DROP VIEW IF EXISTS v_mmo_world_item_deltas;
    DROP VIEW IF EXISTS v_mmo_world_creature_deltas;
    DROP VIEW IF EXISTS v_mmo_world_baseline_status;
    DROP VIEW IF EXISTS v_mmo_runtime_npc_navigation;
    DROP VIEW IF EXISTS v_mmo_npc_relations;
    DROP VIEW IF EXISTS v_mmo_npc_routines;
    DROP VIEW IF EXISTS v_mmo_waypoint_graph;
    DROP VIEW IF EXISTS v_mmo_creature_spawns;
    DROP VIEW IF EXISTS v_mmo_creature_templates;
    DROP VIEW IF EXISTS v_mmo_creature_stat_sheet;
    DROP VIEW IF EXISTS v_mmo_character_stat_sheet;
    DROP VIEW IF EXISTS v_mmo_unit_stat_sheet;
    DROP VIEW IF EXISTS v_mmo_unit_stats;
    DROP VIEW IF EXISTS v_mmo_world_script_state;
    DROP VIEW IF EXISTS v_mmo_world_container_inventory;
    DROP VIEW IF EXISTS v_mmo_world_interactives;
    DROP VIEW IF EXISTS v_mmo_world_items;
    DROP VIEW IF EXISTS v_mmo_world_entity_directory;
    DROP VIEW IF EXISTS v_mmo_world_entities;
    DROP VIEW IF EXISTS v_mmo_character_known_dialogs;
    DROP VIEW IF EXISTS v_mmo_character_quests;
    DROP VIEW IF EXISTS v_mmo_character_equipment;
    DROP VIEW IF EXISTS v_mmo_character_inventory;
    DROP VIEW IF EXISTS v_mmo_character_stats;
    DROP VIEW IF EXISTS v_mmo_character_current;
    DROP VIEW IF EXISTS v_runtime_character_sheet;
    DROP VIEW IF EXISTS v_runtime_character_inventory_totals;
    DROP VIEW IF EXISTS v_runtime_character_equipment;
    DROP VIEW IF EXISTS v_runtime_world_population;
    DROP VIEW IF EXISTS v_runtime_dead_npcs;
    DROP VIEW IF EXISTS v_runtime_quest_state;
    DROP VIEW IF EXISTS v_runtime_quest_lifecycle;
    DROP VIEW IF EXISTS v_runtime_dialog_state;
    DROP VIEW IF EXISTS v_runtime_dialog_availability;
    DROP VIEW IF EXISTS v_runtime_dialog_choice_timeline;
    DROP VIEW IF EXISTS v_runtime_dialog_selection_timeline;
    DROP VIEW IF EXISTS v_runtime_npc_character_sheet;
    DROP VIEW IF EXISTS v_runtime_player_stats;
    DROP VIEW IF EXISTS v_runtime_npc_follow_relations;
    DROP VIEW IF EXISTS v_runtime_waypoint_graph;
    DROP VIEW IF EXISTS v_runtime_waypoint_users;
    DROP VIEW IF EXISTS v_runtime_npc_navigation;
    DROP VIEW IF EXISTS v_runtime_npc_routine_schedule;
    DROP VIEW IF EXISTS v_runtime_world_item_totals;
    DROP VIEW IF EXISTS v_runtime_container_inventory;
    DROP VIEW IF EXISTS v_runtime_interactives;
    DROP VIEW IF EXISTS v_runtime_script_global_categories;
    DROP VIEW IF EXISTS v_runtime_event_counts;
    DROP VIEW IF EXISTS v_runtime_persistence_summary;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_character_sheet AS
      SELECT c.character_key, c.display_name, b.account_key, b.realm_key,
             c.world_name, c.tick_count,
             c.pos_x, c.pos_y, c.pos_z, c.rotation,
             c.hp, c.hp_max, c.mana, c.mana_max, c.level, c.experience,
             c.updated_at
        FROM runtime_characters c
        LEFT JOIN runtime_character_bindings b ON b.character_key = c.character_key;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_character_inventory_totals AS
      SELECT character_key, symbol_index, display_name,
             SUM(iterator_count) AS total_count,
             SUM(CASE WHEN equipped != 0 THEN iterator_count ELSE 0 END) AS equipped_count,
             SUM(CASE WHEN equipped = 0 THEN iterator_count ELSE 0 END) AS bag_count,
             MAX(value) AS unit_value
        FROM runtime_character_inventory
       GROUP BY character_key, symbol_index, display_name;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_character_equipment AS
      SELECT character_key, symbol_index, display_name, iterator_count, slot, value, spell_id
        FROM runtime_character_inventory
       WHERE equipped != 0;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_world_population AS
      SELECT world_name,
             COUNT(*) AS npc_count,
             SUM(CASE WHEN player != 0 THEN 1 ELSE 0 END) AS player_count,
             SUM(CASE WHEN dead != 0 THEN 1 ELSE 0 END) AS dead_count,
             SUM(CASE WHEN dead = 0 THEN 1 ELSE 0 END) AS alive_count
        FROM runtime_world_npcs
       GROUP BY world_name;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_dead_npcs AS
      SELECT entity_key, world_name, display_name, symbol_index, hp, hp_max,
             pos_x, pos_y, pos_z, updated_at
        FROM runtime_world_npcs
       WHERE dead != 0;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_npc_character_sheet AS
      SELECT n.entity_key,
             n.world_name,
             n.display_name,
             n.player,
             n.guild,
             n.true_guild,
             n.level,
             n.experience,
             MAX(CASE WHEN s.stat_group='progression' AND s.stat_key='experience_next' THEN s.value END) AS experience_next,
             MAX(CASE WHEN s.stat_group='progression' AND s.stat_key='learning_points' THEN s.value END) AS learning_points,
             MAX(CASE WHEN s.stat_group='attitude' AND s.stat_key='permanent' THEN s.value END) AS permanent_attitude,
             MAX(CASE WHEN s.stat_group='attitude' AND s.stat_key='temporary' THEN s.value END) AS temporary_attitude,
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
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_player_stats AS
      SELECT entity_key, display_name, stat_group, stat_key, value, updated_at
        FROM runtime_npc_stats
       WHERE player != 0;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_npc_follow_relations AS
      SELECT ai.entity_key,
             ai.world_name,
             ai.display_name,
             ai.player,
             ai.ai_state_name,
             CASE
               WHEN ai.target_key = '' AND instr(lower(ai.ai_state_name), 'player') > 0
                    AND (instr(lower(ai.ai_state_name), 'follow') > 0
                      OR instr(lower(ai.ai_state_name), 'escort') > 0
                      OR instr(lower(ai.ai_state_name), 'guide') > 0)
                 THEN 'PC_HERO'
               ELSE ai.target_key
             END AS target_key,
             CASE
               WHEN ai.target_display_name = '' AND instr(lower(ai.ai_state_name), 'player') > 0
                    AND (instr(lower(ai.ai_state_name), 'follow') > 0
                      OR instr(lower(ai.ai_state_name), 'escort') > 0
                      OR instr(lower(ai.ai_state_name), 'guide') > 0)
                 THEN COALESCE(c.display_name, 'PC_HERO')
               ELSE ai.target_display_name
             END AS target_display_name,
             ai.state_other_key,
             ai.state_victim_key,
             CASE
               WHEN instr(lower(ai.ai_state_name), 'follow') > 0 THEN 'following_target'
               WHEN instr(lower(ai.ai_state_name), 'escort') > 0
                 OR instr(lower(ai.ai_state_name), 'guide') > 0 THEN 'escort_or_guide'
               ELSE ai.relation_kind
             END AS relation_kind,
             ai.tick_count,
             ai.updated_at
        FROM runtime_npc_ai_state ai
        LEFT JOIN runtime_characters c ON c.character_key = 'PC_HERO'
       WHERE ai.relation_kind != 'none'
          OR instr(lower(ai.ai_state_name), 'follow') > 0
          OR instr(lower(ai.ai_state_name), 'escort') > 0
          OR instr(lower(ai.ai_state_name), 'guide') > 0;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_waypoint_graph AS
      SELECT w.waypoint_key,
             w.world_name,
             w.kind,
             w.waypoint_index,
             w.name,
             w.pos_x,
             w.pos_y,
             w.pos_z,
             w.dir_x,
             w.dir_y,
             w.dir_z,
             w.underwater,
             w.connected,
             w.use_count,
             COUNT(e.edge_key) AS outgoing_edges
        FROM runtime_waypoints w
        LEFT JOIN runtime_waypoint_edges e ON e.from_waypoint_key = w.waypoint_key
       GROUP BY w.waypoint_key;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_npc_navigation AS
      SELECT n.entity_key,
             n.world_name,
             n.tick_count,
             n.display_name,
             n.current_waypoint_key,
             n.current_waypoint_name,
             n.routine_waypoint_key,
             n.routine_waypoint_name,
             n.move_hint,
             n.move_target_waypoint_key,
             n.move_target_waypoint_name,
             n.path_next_waypoint_key,
             n.path_next_waypoint_name,
             n.path_final_waypoint_key,
             n.path_final_waypoint_name,
             n.path_remaining_count,
             ai.ai_state_name,
             CASE
               WHEN ai.target_display_name = '' AND instr(lower(ai.ai_state_name), 'player') > 0
                    AND (instr(lower(ai.ai_state_name), 'follow') > 0
                      OR instr(lower(ai.ai_state_name), 'escort') > 0
                      OR instr(lower(ai.ai_state_name), 'guide') > 0)
                 THEN COALESCE(c.display_name, 'PC_HERO')
               ELSE ai.target_display_name
             END AS target_display_name,
             CASE
               WHEN instr(lower(ai.ai_state_name), 'follow') > 0 THEN 'following_target'
               WHEN instr(lower(ai.ai_state_name), 'escort') > 0
                 OR instr(lower(ai.ai_state_name), 'guide') > 0 THEN 'escort_or_guide'
               ELSE ai.relation_kind
             END AS relation_kind,
             n.updated_at
        FROM runtime_npc_navigation_state n
        LEFT JOIN runtime_npc_ai_state ai ON ai.entity_key = n.entity_key
        LEFT JOIN runtime_characters c ON c.character_key = 'PC_HERO';
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_npc_routine_schedule AS
      SELECT r.entity_key,
             r.world_name,
             r.display_name,
             r.routine_index,
             r.start_minute,
             r.end_minute,
             printf('%02d:%02d', r.start_minute / 60, r.start_minute % 60) AS start_time,
             printf('%02d:%02d', r.end_minute / 60, r.end_minute % 60) AS end_time,
             r.callback_symbol_index,
             r.callback_symbol_name,
             r.waypoint_name,
             r.active,
             r.updated_at
        FROM runtime_npc_routines r;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_waypoint_users AS
      SELECT 'current' AS usage_kind,
             current_waypoint_key AS waypoint_key,
             current_waypoint_name AS waypoint_name,
             entity_key,
             display_name,
             world_name,
             tick_count,
             move_hint,
             ai_state_name,
             relation_kind
        FROM v_runtime_npc_navigation
       WHERE current_waypoint_key != ''
      UNION ALL
      SELECT 'routine' AS usage_kind,
             routine_waypoint_key,
             routine_waypoint_name,
             entity_key,
             display_name,
             world_name,
             tick_count,
             move_hint,
             ai_state_name,
             relation_kind
        FROM v_runtime_npc_navigation
       WHERE routine_waypoint_key != ''
      UNION ALL
      SELECT 'move_target' AS usage_kind,
             move_target_waypoint_key,
             move_target_waypoint_name,
             entity_key,
             display_name,
             world_name,
             tick_count,
             move_hint,
             ai_state_name,
             relation_kind
        FROM v_runtime_npc_navigation
       WHERE move_target_waypoint_key != '';
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_world_item_totals AS
      SELECT world_name, symbol_index, display_name,
             COUNT(*) AS stack_count,
             SUM(amount) AS total_amount,
             MAX(value) AS unit_value
        FROM runtime_world_items
       GROUP BY world_name, symbol_index, display_name;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_container_inventory AS
      SELECT owner_key, owner_display_name, symbol_index, display_name,
             SUM(iterator_count) AS total_count,
             MAX(value) AS unit_value
        FROM runtime_world_mobsi_inventory
       GROUP BY owner_key, owner_display_name, symbol_index, display_name;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_interactives AS
      SELECT entity_key, world_name, display_name, focus_name, scheme,
             state, state_count, state_mask,
             container, door, ladder, locked, cracked,
             pos_x, pos_y, pos_z, updated_at
        FROM runtime_world_mobsi;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_quest_state AS
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
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_quest_lifecycle AS
      SELECT lifecycle_state, COUNT(*) AS quest_count
        FROM v_runtime_quest_state
       GROUP BY lifecycle_state;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_dialog_state AS
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
          ON k.info_symbol_index = c.info_symbol_index;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_dialog_availability AS
      SELECT availability_state, COUNT(*) AS dialog_count
        FROM v_runtime_dialog_state
       GROUP BY availability_state;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_dialog_choice_timeline AS
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
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_dialog_selection_timeline AS
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
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_script_global_categories AS
      SELECT category, value_type, COUNT(*) AS global_count
        FROM runtime_script_globals
       GROUP BY category, value_type;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_event_counts AS
      SELECT event_type, COUNT(*) AS event_count,
             MIN(tick_count) AS first_tick,
             MAX(tick_count) AS last_tick,
             SUM(delta) AS delta_sum
        FROM runtime_events
       GROUP BY event_type;
    CREATE TEMP VIEW IF NOT EXISTS v_runtime_persistence_summary AS
      SELECT 'characters' AS area, COUNT(*) AS row_count FROM runtime_characters
      UNION ALL SELECT 'character_inventory', COUNT(*) FROM runtime_character_inventory
      UNION ALL SELECT 'world_npcs', COUNT(*) FROM runtime_world_npcs
      UNION ALL SELECT 'world_clock', COUNT(*) FROM runtime_world_clock
      UNION ALL SELECT 'world_npc_inventory', COUNT(*) FROM runtime_world_npc_inventory
      UNION ALL SELECT 'world_items', COUNT(*) FROM runtime_world_items
      UNION ALL SELECT 'waypoints', COUNT(*) FROM runtime_waypoints
      UNION ALL SELECT 'waypoint_edges', COUNT(*) FROM runtime_waypoint_edges
      UNION ALL SELECT 'npc_stats', COUNT(*) FROM runtime_npc_stats
      UNION ALL SELECT 'npc_ai_state', COUNT(*) FROM runtime_npc_ai_state
      UNION ALL SELECT 'npc_relation_checkpoints', COUNT(*) FROM runtime_npc_relation_checkpoints
      UNION ALL SELECT 'npc_routines', COUNT(*) FROM runtime_npc_routines
      UNION ALL SELECT 'npc_navigation', COUNT(*) FROM runtime_npc_navigation_state
      UNION ALL SELECT 'world_mobsi', COUNT(*) FROM runtime_world_mobsi
      UNION ALL SELECT 'mobsi_inventory', COUNT(*) FROM runtime_world_mobsi_inventory
      UNION ALL SELECT 'quests', COUNT(*) FROM runtime_quests
      UNION ALL SELECT 'known_dialogs', COUNT(*) FROM runtime_known_dialogs
      UNION ALL SELECT 'dialog_catalog', COUNT(*) FROM runtime_dialog_catalog
      UNION ALL SELECT 'dialog_choice_snapshots', COUNT(*) FROM runtime_dialog_choice_snapshots
      UNION ALL SELECT 'dialog_selections', COUNT(*) FROM runtime_dialog_selections
      UNION ALL SELECT 'script_globals', COUNT(*) FROM runtime_script_globals
      UNION ALL SELECT 'events', COUNT(*) FROM runtime_events;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_character_current AS
      SELECT c.character_key,
             c.display_name,
             COALESCE(b.account_key, 'local-account') AS account_key,
             COALESCE(b.realm_key, 'local-g2notr') AS realm_key,
             c.world_name,
             c.tick_count,
             c.pos_x,
             c.pos_y,
             c.pos_z,
             c.rotation,
             c.hp,
             c.hp_max,
             c.mana,
             c.mana_max,
             c.level,
             c.experience,
             c.updated_at,
             'character_canonical' AS persistence_class
        FROM runtime_characters c
        LEFT JOIN runtime_character_bindings b ON b.character_key = c.character_key;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_unit_stats AS
      SELECT s.entity_key AS unit_key,
             CASE WHEN s.player != 0 THEN 'character' ELSE 'creature' END AS unit_type,
             CASE WHEN s.player != 0 THEN 'PC_HERO' ELSE '' END AS character_key,
             s.world_name,
             s.tick_count,
             n.symbol_index AS template_symbol_index,
             n.script_id,
             s.display_name,
             s.player,
             COALESCE(d.stat_domain, 'unknown') AS stat_domain,
             COALESCE(d.stat_family, s.stat_group) AS stat_family,
             s.stat_group,
             s.stat_id,
             s.stat_key,
             COALESCE(d.value_kind, 'absolute') AS value_kind,
             COALESCE(d.persistence_hint, 'runtime_current') AS persistence_hint,
             COALESCE(d.display_order, 9999) AS display_order,
             s.value,
             s.updated_at,
             CASE
               WHEN s.player != 0 THEN 'character_canonical'
               WHEN COALESCE(d.persistence_hint, 'runtime_current') = 'runtime_current' THEN 'creature_runtime'
               ELSE 'content_creature_template'
             END AS persistence_class
        FROM runtime_npc_stats s
        LEFT JOIN runtime_world_npcs n ON n.entity_key = s.entity_key
        LEFT JOIN mmo_stat_definitions d
          ON d.stat_group = s.stat_group AND d.stat_id = s.stat_id;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_unit_stat_sheet AS
      SELECT n.entity_key AS unit_key,
             CASE WHEN n.player != 0 THEN 'character' ELSE 'creature' END AS unit_type,
             CASE WHEN n.player != 0 THEN 'PC_HERO' ELSE '' END AS character_key,
             n.world_name,
             n.tick_count,
             n.symbol_index AS template_symbol_index,
             n.script_id,
             n.display_name,
             n.player,
             n.guild,
             n.true_guild,
             n.level,
             n.experience,
             n.dead,
             n.pos_x,
             n.pos_y,
             n.pos_z,
             n.rotation,
             n.waypoint,
             n.hp AS health_current,
             n.hp_max AS health_max,
             n.mana AS mana_current,
             n.mana_max AS mana_max,
             MAX(CASE WHEN s.stat_group='attribute' AND s.stat_key='strength' THEN s.value END) AS strength,
             MAX(CASE WHEN s.stat_group='attribute' AND s.stat_key='dexterity' THEN s.value END) AS dexterity,
             MAX(CASE WHEN s.stat_group='attribute' AND s.stat_key='regenerate_hp' THEN s.value END) AS regenerate_hp,
             MAX(CASE WHEN s.stat_group='attribute' AND s.stat_key='regenerate_mana' THEN s.value END) AS regenerate_mana,
             MAX(CASE WHEN s.stat_group='protection' AND s.stat_key='barrier' THEN s.value END) AS resist_barrier,
             MAX(CASE WHEN s.stat_group='protection' AND s.stat_key='blunt' THEN s.value END) AS resist_blunt,
             MAX(CASE WHEN s.stat_group='protection' AND s.stat_key='edge' THEN s.value END) AS resist_edge,
             MAX(CASE WHEN s.stat_group='protection' AND s.stat_key='fire' THEN s.value END) AS resist_fire,
             MAX(CASE WHEN s.stat_group='protection' AND s.stat_key='fly' THEN s.value END) AS resist_fly,
             MAX(CASE WHEN s.stat_group='protection' AND s.stat_key='magic' THEN s.value END) AS resist_magic,
             MAX(CASE WHEN s.stat_group='protection' AND s.stat_key='point' THEN s.value END) AS resist_point,
             MAX(CASE WHEN s.stat_group='protection' AND s.stat_key='fall' THEN s.value END) AS resist_fall,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='one_handed' THEN s.value END) AS one_handed_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='two_handed' THEN s.value END) AS two_handed_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='bow' THEN s.value END) AS bow_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='crossbow' THEN s.value END) AS crossbow_skill,
             MAX(CASE WHEN s.stat_group='hit_chance' AND s.stat_key='one_handed' THEN s.value END) AS one_handed_hit_chance,
             MAX(CASE WHEN s.stat_group='hit_chance' AND s.stat_key='two_handed' THEN s.value END) AS two_handed_hit_chance,
             MAX(CASE WHEN s.stat_group='hit_chance' AND s.stat_key='bow' THEN s.value END) AS bow_hit_chance,
             MAX(CASE WHEN s.stat_group='hit_chance' AND s.stat_key='crossbow' THEN s.value END) AS crossbow_hit_chance,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='picklock' THEN s.value END) AS picklock_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='sneak' THEN s.value END) AS sneak_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='pickpocket' THEN s.value END) AS pickpocket_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='smith' THEN s.value END) AS smith_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='alchemy' THEN s.value END) AS alchemy_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='take_animal_trophy' THEN s.value END) AS take_animal_trophy_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='foreign_language' THEN s.value END) AS foreign_language_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='acrobat' THEN s.value END) AS acrobat_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='mage' THEN s.value END) AS mage_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='runes' THEN s.value END) AS runes_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='firemaster' THEN s.value END) AS firemaster_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='regenerate' THEN s.value END) AS regenerate_skill,
             MAX(CASE WHEN s.stat_group='talent_skill' AND s.stat_key='wisp_detector' THEN s.value END) AS wisp_detector_skill,
             n.updated_at,
             CASE WHEN n.player != 0 THEN 'character_canonical' ELSE 'world_checkpoint' END AS persistence_class
        FROM runtime_world_npcs n
        LEFT JOIN runtime_npc_stats s ON s.entity_key = n.entity_key
       GROUP BY n.entity_key;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_character_stats AS
      SELECT 'PC_HERO' AS character_key,
             unit_key,
             world_name,
             tick_count,
             stat_domain,
             stat_family,
             stat_group,
             stat_id,
             stat_key,
             value_kind,
             value,
             display_order,
             updated_at,
             persistence_class
        FROM v_mmo_unit_stats
       WHERE unit_type = 'character';
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_character_stat_sheet AS
      SELECT 'PC_HERO' AS character_key,
             s.*
        FROM v_mmo_unit_stat_sheet s
       WHERE s.unit_type = 'character';
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_creature_stat_sheet AS
      SELECT s.unit_key AS creature_spawn_key,
             s.template_symbol_index AS creature_template_id,
             s.*
        FROM v_mmo_unit_stat_sheet s
       WHERE s.unit_type = 'creature';
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_creature_templates AS
      SELECT 'creature_template:' || world_name || ':' || template_symbol_index || ':' ||
             display_name || ':' || COALESCE(guild, -1) || ':' || COALESCE(true_guild, -1)
               AS creature_template_key,
             world_name,
             template_symbol_index AS creature_template_id,
             MIN(script_id) AS script_id,
             display_name,
             guild,
             true_guild,
             COUNT(*) AS spawn_count,
             MIN(level) AS min_level,
             MAX(level) AS max_level,
             MAX(health_max) AS base_health_max,
             MAX(mana_max) AS base_mana_max,
             MAX(strength) AS base_strength,
             MAX(dexterity) AS base_dexterity,
             MAX(resist_blunt) AS resist_blunt,
             MAX(resist_edge) AS resist_edge,
             MAX(resist_fire) AS resist_fire,
             MAX(resist_magic) AS resist_magic,
             MAX(updated_at) AS updated_at,
             'content_creature_template' AS persistence_class
        FROM v_mmo_unit_stat_sheet
       WHERE unit_type = 'creature'
       GROUP BY world_name, template_symbol_index, display_name, guild, true_guild;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_creature_spawns AS
      SELECT n.entity_key AS creature_spawn_key,
             n.symbol_index AS creature_template_id,
             n.world_name,
             n.tick_count,
             n.display_name,
             n.pos_x,
             n.pos_y,
             n.pos_z,
             n.rotation,
             n.waypoint,
             n.dead,
             s.level,
             s.experience,
             s.health_current,
             s.health_max,
             s.mana_current,
             s.mana_max,
             s.strength,
             s.dexterity,
             nav.current_waypoint_name,
             nav.routine_waypoint_name,
             nav.move_hint,
             nav.move_target_waypoint_name,
             n.updated_at,
             'world_creature_spawn' AS persistence_class
        FROM runtime_world_npcs n
        LEFT JOIN v_mmo_unit_stat_sheet s ON s.unit_key = n.entity_key
        LEFT JOIN v_runtime_npc_navigation nav ON nav.entity_key = n.entity_key
       WHERE n.player = 0;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_character_inventory AS
      SELECT character_key,
             item_key AS item_instance_key,
             symbol_index AS item_template_symbol,
             display_name AS item_display_name,
             amount,
             iterator_count,
             equipped,
             equip_count,
             slot,
             main_flag,
             item_flags,
             value,
             spell_id,
             updated_at,
             'character_canonical' AS persistence_class
        FROM runtime_character_inventory;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_character_equipment AS
      SELECT character_key,
             item_instance_key,
             item_template_symbol,
             item_display_name,
             iterator_count,
             slot,
             value,
             spell_id,
             updated_at,
             persistence_class
        FROM v_mmo_character_inventory
       WHERE equipped != 0;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_character_quests AS
      SELECT 'PC_HERO' AS character_key,
             quest_key,
             name,
             section,
             section_label,
             status,
             status_label,
             lifecycle_state,
             entry_count,
             entries_text,
             updated_at,
             'character_canonical' AS persistence_class
        FROM v_runtime_quest_state;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_character_known_dialogs AS
      SELECT 'PC_HERO' AS character_key,
             k.npc_symbol_index,
             k.info_symbol_index,
             k.npc_symbol_name,
             k.info_symbol_name,
             c.description,
             c.permanent,
             k.first_seen_tick,
             k.updated_at,
             'character_canonical' AS persistence_class
        FROM runtime_known_dialogs k
        LEFT JOIN runtime_dialog_catalog c ON c.info_symbol_index = k.info_symbol_index;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_character_story_progress AS
      SELECT character_key,
             world_name,
             tick_count,
             chapter_number,
             chapter_key,
             source_global_key,
             source_symbol_index,
             source_symbol_name,
             updated_at,
             'character_story_progress_current' AS persistence_class
        FROM runtime_story_progress_current;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_entities AS
      SELECT entity_key,
             world_name,
             CASE WHEN player != 0 THEN 'player_character' ELSE 'npc' END AS entity_type,
             tick_count,
             persistent_id,
             symbol_index,
             script_id,
             display_name,
             pos_x,
             pos_y,
             pos_z,
             rotation,
             guild,
             true_guild,
             hp,
             hp_max,
             mana,
             mana_max,
             level,
             experience,
             dead,
             player,
             waypoint,
             updated_at,
             CASE
               WHEN player != 0 THEN 'character_mirror'
               WHEN dead != 0 THEN 'world_persistent_delta'
               ELSE 'world_checkpoint'
             END AS persistence_class
        FROM runtime_world_npcs;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_items AS
      SELECT entity_key,
             world_name,
             tick_count,
             persistent_id,
             symbol_index,
             script_id,
             display_name,
             visual,
             amount,
             main_flag,
             item_flags,
             value,
             pos_x,
             pos_y,
             pos_z,
             updated_at,
             'world_checkpoint' AS persistence_class
        FROM runtime_world_items;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_interactives AS
      SELECT entity_key,
             world_name,
             tick_count,
             vob_id,
             tag,
             focus_name,
             display_name,
             scheme,
             pos_x,
             pos_y,
             pos_z,
             state,
             state_count,
             state_mask,
             container,
             door,
             ladder,
             locked,
             cracked,
             updated_at,
             'world_checkpoint' AS persistence_class
        FROM runtime_world_mobsi;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_entity_directory AS
      SELECT CASE WHEN n.player != 0 THEN 'player_character' ELSE 'npc' END AS entity_type,
             'g2notr/' || CASE n.world_name
                             WHEN 'newworld.zen' THEN 'new-world'
                             ELSE lower(replace(replace(n.world_name, '.zen', ''), '_', '-'))
                           END || '/npc/' || n.persistent_id || '/' || n.symbol_index || '/' || n.script_id AS entity_ref,
             CASE WHEN n.player != 0 THEN 'HERO' ELSE 'NPC-' || n.persistent_id END AS short_id,
             n.display_name,
             n.world_name AS world_key,
             CASE n.world_name
               WHEN 'newworld.zen' THEN 'New World'
               ELSE replace(replace(n.world_name, '.zen', ''), '_', ' ')
             END AS world_display_name,
             n.persistent_id AS source_persistent_id,
             n.symbol_index AS template_symbol_index,
             n.script_id,
             n.entity_key AS engine_key,
             n.updated_at
        FROM runtime_world_npcs n
      UNION ALL
      SELECT 'world_item',
             'g2notr/' || CASE i.world_name
                             WHEN 'newworld.zen' THEN 'new-world'
                             ELSE lower(replace(replace(i.world_name, '.zen', ''), '_', '-'))
                           END || '/item/' || i.persistent_id || '/' || i.symbol_index || '/' || i.script_id,
             'ITEM-' || i.persistent_id,
             i.display_name,
             i.world_name,
             CASE i.world_name
               WHEN 'newworld.zen' THEN 'New World'
               ELSE replace(replace(i.world_name, '.zen', ''), '_', ' ')
             END,
             i.persistent_id,
             i.symbol_index,
             i.script_id,
             i.entity_key,
             i.updated_at
        FROM runtime_world_items i
      UNION ALL
      SELECT 'interactive',
             'g2notr/' || CASE m.world_name
                             WHEN 'newworld.zen' THEN 'new-world'
                             ELSE lower(replace(replace(m.world_name, '.zen', ''), '_', '-'))
                           END || '/interactive/' || m.vob_id,
             'VOB-' || m.vob_id,
             m.display_name,
             m.world_name,
             CASE m.world_name
               WHEN 'newworld.zen' THEN 'New World'
               ELSE replace(replace(m.world_name, '.zen', ''), '_', ' ')
             END,
             m.vob_id,
             NULL,
             NULL,
             m.entity_key,
             m.updated_at
        FROM runtime_world_mobsi m;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_container_inventory AS
      SELECT owner_key,
             owner_display_name,
             world_name,
             item_key AS item_instance_key,
             symbol_index AS item_template_symbol,
             display_name AS item_display_name,
             amount,
             iterator_count,
             value,
             updated_at,
             'world_checkpoint' AS persistence_class
        FROM runtime_world_mobsi_inventory;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_script_state AS
      SELECT global_key,
             symbol_index,
             symbol_name,
             value_type,
             category,
             value_count,
             value_text,
             updated_at,
             CASE
               WHEN category IN ('quest', 'dialog') THEN 'world_persistent_delta'
               ELSE 'world_checkpoint'
             END AS persistence_class
        FROM runtime_script_globals;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_waypoint_graph AS
      SELECT waypoint_key,
             world_name,
             kind,
             waypoint_index,
             name,
             pos_x,
             pos_y,
             pos_z,
             dir_x,
             dir_y,
             dir_z,
             underwater,
             connected,
             use_count,
             outgoing_edges,
             'content_navigation' AS persistence_class
        FROM v_runtime_waypoint_graph;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_npc_routines AS
      SELECT entity_key,
             world_name,
             display_name,
             routine_index,
             start_minute,
             end_minute,
             start_time,
             end_time,
             callback_symbol_index,
             callback_symbol_name,
             waypoint_name,
             active,
             updated_at,
             'content_schedule_checkpoint' AS persistence_class
        FROM v_runtime_npc_routine_schedule;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_npc_relations AS
      SELECT entity_key,
             world_name,
             display_name,
             player,
             ai_state_name,
             target_key,
             target_display_name,
             state_other_key,
             state_victim_key,
             CASE
               WHEN target_key = 'PC_HERO' THEN 'character'
               WHEN target_key LIKE 'npc:%' THEN 'npc'
               WHEN target_key = '' THEN 'implicit'
               ELSE 'unknown'
             END AS target_kind,
             relation_kind,
             tick_count,
             updated_at,
             'runtime_relation_checkpoint' AS persistence_class
        FROM v_runtime_npc_follow_relations;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_runtime_npc_navigation AS
      SELECT entity_key,
             world_name,
             tick_count,
             display_name,
             current_waypoint_key,
             current_waypoint_name,
             routine_waypoint_key,
             routine_waypoint_name,
             move_hint,
             move_target_waypoint_key,
             move_target_waypoint_name,
             path_next_waypoint_key,
             path_next_waypoint_name,
             path_final_waypoint_key,
             path_final_waypoint_name,
             path_remaining_count,
             ai_state_name,
             target_display_name,
             relation_kind,
             updated_at,
             'runtime_transient' AS persistence_class
        FROM v_runtime_npc_navigation;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_event_journal AS
      SELECT id AS event_id,
             session_id,
             event_type,
             entity_key,
             subject_key,
             world_name,
             tick_count,
             value_before,
             value_after,
             delta,
             data_text AS payload_text,
             created_at,
             'event_journal' AS persistence_class
        FROM runtime_events;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_baseline_status AS
      SELECT t.world_template_key,
             t.game_target,
             t.content_revision_key,
             t.world_name,
             t.baseline_tick,
             t.baseline_world_time_millis,
             t.baseline_captured_at,
             i.world_instance_key,
             i.realm_key,
             i.lifecycle_state,
             i.current_tick,
             i.current_world_time_millis,
             (SELECT COUNT(*) FROM mmo_world_baseline_creatures b WHERE b.world_template_key=t.world_template_key) AS baseline_creatures,
             (SELECT COUNT(*) FROM mmo_creature_spawns_current c WHERE c.world_name=t.world_name) AS current_creatures,
             (SELECT COUNT(*) FROM mmo_world_baseline_items b WHERE b.world_template_key=t.world_template_key) AS baseline_items,
             (SELECT COUNT(*) FROM mmo_world_items_current c WHERE c.world_name=t.world_name AND c.exists_in_world!=0) AS current_items,
             (SELECT COUNT(*) FROM mmo_world_baseline_interactives b WHERE b.world_template_key=t.world_template_key) AS baseline_interactives,
             (SELECT COUNT(*) FROM mmo_world_interactives_current c WHERE c.world_name=t.world_name) AS current_interactives,
             'world_baseline' AS persistence_class
        FROM mmo_world_templates t
        LEFT JOIN mmo_world_instances i ON i.world_template_key=t.world_template_key;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_creature_deltas AS
      SELECT t.world_template_key,
             c.world_name,
             c.creature_spawn_key,
             c.display_name,
             CASE
               WHEN b.creature_spawn_key IS NULL THEN 'spawned'
               WHEN c.dead IS NOT b.dead OR c.health_current IS NOT b.health_current OR c.mana_current IS NOT b.mana_current
                 OR c.level IS NOT b.level OR c.experience IS NOT b.experience
                 OR c.pos_x IS NOT b.pos_x OR c.pos_y IS NOT b.pos_y OR c.pos_z IS NOT b.pos_z OR c.rotation IS NOT b.rotation
                 OR c.waypoint IS NOT b.waypoint THEN 'changed'
               ELSE 'unchanged'
             END AS delta_kind,
             TRIM(
               CASE WHEN b.creature_spawn_key IS NULL THEN 'spawn,' ELSE '' END ||
               CASE WHEN b.creature_spawn_key IS NOT NULL AND c.dead IS NOT b.dead THEN 'dead,' ELSE '' END ||
               CASE WHEN b.creature_spawn_key IS NOT NULL AND c.health_current IS NOT b.health_current THEN 'health,' ELSE '' END ||
               CASE WHEN b.creature_spawn_key IS NOT NULL AND c.mana_current IS NOT b.mana_current THEN 'mana,' ELSE '' END ||
               CASE WHEN b.creature_spawn_key IS NOT NULL AND (c.level IS NOT b.level OR c.experience IS NOT b.experience) THEN 'progression,' ELSE '' END ||
               CASE WHEN b.creature_spawn_key IS NOT NULL AND (c.pos_x IS NOT b.pos_x OR c.pos_y IS NOT b.pos_y OR c.pos_z IS NOT b.pos_z OR c.rotation IS NOT b.rotation OR c.waypoint IS NOT b.waypoint) THEN 'transform,' ELSE '' END,
               ','
             ) AS changed_fields,
             c.tick_count,
             c.updated_at
        FROM mmo_creature_spawns_current c
        JOIN mmo_world_templates t ON t.world_name=c.world_name AND t.baseline_captured_at IS NOT NULL
        LEFT JOIN mmo_world_baseline_creatures b
          ON b.world_template_key=t.world_template_key AND b.creature_spawn_key=c.creature_spawn_key
      UNION ALL
      SELECT b.world_template_key,
             b.world_name,
             b.creature_spawn_key,
             b.display_name,
             'despawned',
             'despawn',
             t.baseline_tick,
             NULL
        FROM mmo_world_baseline_creatures b
        JOIN mmo_world_templates t ON t.world_template_key=b.world_template_key
        LEFT JOIN mmo_creature_spawns_current c ON c.world_name=b.world_name AND c.creature_spawn_key=b.creature_spawn_key
       WHERE c.creature_spawn_key IS NULL;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_item_deltas AS
      SELECT t.world_template_key,
             c.world_name,
             c.item_spawn_key,
             c.item_display_name,
             CASE
               WHEN b.item_spawn_key IS NULL THEN 'spawned'
               WHEN c.exists_in_world=0 THEN 'removed'
               WHEN c.amount IS NOT b.amount OR c.pos_x IS NOT b.pos_x OR c.pos_y IS NOT b.pos_y OR c.pos_z IS NOT b.pos_z THEN 'changed'
               ELSE 'unchanged'
             END AS delta_kind,
             TRIM(
               CASE WHEN b.item_spawn_key IS NULL THEN 'spawn,' ELSE '' END ||
               CASE WHEN b.item_spawn_key IS NOT NULL AND c.exists_in_world=0 THEN 'removed,' ELSE '' END ||
               CASE WHEN b.item_spawn_key IS NOT NULL AND c.exists_in_world!=0 AND c.amount IS NOT b.amount THEN 'amount,' ELSE '' END ||
               CASE WHEN b.item_spawn_key IS NOT NULL AND c.exists_in_world!=0 AND (c.pos_x IS NOT b.pos_x OR c.pos_y IS NOT b.pos_y OR c.pos_z IS NOT b.pos_z) THEN 'transform,' ELSE '' END,
               ','
             ) AS changed_fields,
             c.tick_count,
             c.updated_at
        FROM mmo_world_items_current c
        JOIN mmo_world_templates t ON t.world_name=c.world_name AND t.baseline_captured_at IS NOT NULL
        LEFT JOIN mmo_world_baseline_items b
          ON b.world_template_key=t.world_template_key AND b.item_spawn_key=c.item_spawn_key
      UNION ALL
      SELECT b.world_template_key,
             b.world_name,
             b.item_spawn_key,
             b.item_display_name,
             'removed',
             'removed',
             t.baseline_tick,
             NULL
        FROM mmo_world_baseline_items b
        JOIN mmo_world_templates t ON t.world_template_key=b.world_template_key
        LEFT JOIN mmo_world_items_current c ON c.world_name=b.world_name AND c.item_spawn_key=b.item_spawn_key
       WHERE c.item_spawn_key IS NULL;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_interactive_deltas AS
      SELECT t.world_template_key,
             c.world_name,
             c.interactive_key,
             c.display_name,
             CASE
               WHEN b.interactive_key IS NULL THEN 'spawned'
               WHEN c.state IS NOT b.state OR c.locked IS NOT b.locked OR c.cracked IS NOT b.cracked THEN 'changed'
               ELSE 'unchanged'
             END AS delta_kind,
             TRIM(
               CASE WHEN b.interactive_key IS NULL THEN 'spawn,' ELSE '' END ||
               CASE WHEN b.interactive_key IS NOT NULL AND c.state IS NOT b.state THEN 'state,' ELSE '' END ||
               CASE WHEN b.interactive_key IS NOT NULL AND c.locked IS NOT b.locked THEN 'locked,' ELSE '' END ||
               CASE WHEN b.interactive_key IS NOT NULL AND c.cracked IS NOT b.cracked THEN 'cracked,' ELSE '' END,
               ','
             ) AS changed_fields,
             c.tick_count,
             c.updated_at
        FROM mmo_world_interactives_current c
        JOIN mmo_world_templates t ON t.world_name=c.world_name AND t.baseline_captured_at IS NOT NULL
        LEFT JOIN mmo_world_baseline_interactives b
          ON b.world_template_key=t.world_template_key AND b.interactive_key=c.interactive_key;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_world_script_global_deltas AS
      SELECT t.world_template_key,
             g.global_key,
             g.symbol_index,
             g.symbol_name,
             g.category,
             CASE
               WHEN b.global_key IS NULL THEN 'created'
               WHEN g.value_count IS NOT b.value_count OR g.value_text IS NOT b.value_text THEN 'changed'
               ELSE 'unchanged'
             END AS delta_kind,
             g.value_count,
             g.value_text,
             g.updated_at
        FROM mmo_script_globals_current g
        JOIN mmo_world_templates t
          ON t.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
         AND t.baseline_captured_at IS NOT NULL
        LEFT JOIN mmo_world_baseline_script_globals b
          ON b.world_template_key=t.world_template_key AND b.global_key=g.global_key;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_restore_readiness AS
      SELECT 'character_position_stats' AS restore_area,
             'implemented' AS engine_restore_status,
             COUNT(*) AS current_rows,
             'HERO transform, progression and full stat sheet restore from mmo current-state at startup' AS note
        FROM mmo_characters_current
      UNION ALL
      SELECT 'character_inventory', 'implemented', COUNT(*),
             'inventory and equipped rows restore through Npc inventory ownership API'
        FROM mmo_character_inventory_current
      UNION ALL
      SELECT 'character_quests', 'implemented', COUNT(*),
             'quest log replaces its snapshot through GameScript ownership API'
        FROM mmo_character_quests_current
      UNION ALL
      SELECT 'character_known_dialogs', 'implemented', COUNT(*),
             'known dialog set replaces its snapshot through GameScript ownership API'
        FROM mmo_character_known_dialogs_current
      UNION ALL
      SELECT 'character_story_progress', 'implemented', COUNT(*),
             'story chapter restores through Daedalus global KAPITEL and the canonical story-progress projection'
        FROM mmo_character_story_progress_current
      UNION ALL
      SELECT 'world_entities', 'implemented_checkpoint', COUNT(*),
             'NPC position, stats, death and progression restore; active AI queues remain runtime-only'
        FROM mmo_unit_stat_sheet_current
      UNION ALL
      SELECT 'world_clock', 'implemented', COUNT(*),
             'exact game-world millisecond clock restores before NPC routine checkpoint application'
        FROM mmo_world_clock_current
      UNION ALL
      SELECT 'world_baseline',
             CASE WHEN COUNT(*)=0 THEN 'awaiting_v18_baseline' ELSE 'captured_server_baseline' END,
             COUNT(*),
             'immutable new-game template and local world-instance metadata support delta, replay and future shard ownership'
        FROM mmo_world_templates
       WHERE baseline_captured_at IS NOT NULL
      UNION ALL
      SELECT 'world_npc_inventory',
             CASE WHEN COUNT(*)=0 THEN 'awaiting_v16_snapshot' ELSE 'implemented' END,
             COUNT(*),
             'all creature inventory snapshots, including empty inventories, restore through Npc inventory ownership API'
        FROM mmo_creature_inventory_snapshots_current
      UNION ALL
      SELECT 'world_items', 'implemented', COUNT(*),
             'world item state restores existing rows and durable tombstones for removed items'
        FROM mmo_world_items_current
      UNION ALL
      SELECT 'world_interactives', 'implemented', COUNT(*),
             'mobsi state, locks and cracked state restore through Interactive ownership API'
        FROM mmo_world_interactives_current
      UNION ALL
      SELECT 'world_container_inventory', 'implemented', COUNT(*),
             'container inventory restores through Inventory ownership API'
        FROM mmo_world_container_inventory_current
      UNION ALL
      SELECT 'world_script_state',
             CASE WHEN COUNT(*)=0 THEN 'awaiting_v15_snapshot' ELSE 'implemented' END,
             COUNT(*),
             'typed scalar and array globals restore from normalized value rows; legacy schema 14 needs one v15 flush'
        FROM mmo_script_global_values_current
      UNION ALL
      SELECT 'guild_attitudes',
             CASE WHEN COUNT(*)=0 THEN 'awaiting_v15_snapshot' ELSE 'implemented' END,
             COUNT(*),
             'runtime guild attitude matrix restores independently from Daedalus globals'
        FROM mmo_guild_attitudes_current
      UNION ALL
      SELECT 'world_follow_escort',
             CASE WHEN COUNT(*)=0 THEN 'awaiting_v17_snapshot' ELSE 'implemented_checkpoint' END,
             COUNT(*),
             'follow and escort state restores stable target/other/victim relations and restarts only the validated Daedalus state'
        FROM mmo_creature_relations_current
      UNION ALL
      SELECT 'npc_navigation', 'runtime_only', COUNT(*),
             'navigation/path state is captured for diagnostics and crash recovery, not canonical persistent state'
        FROM runtime_npc_navigation_state;
    CREATE TEMP VIEW IF NOT EXISTS v_mmo_persistence_contract AS
      SELECT 'content' AS state_domain, 'mmo_stat_definitions' AS view_name,
             'content_stat_definition' AS persistence_class, COUNT(*) AS row_count
        FROM mmo_stat_definitions
      UNION ALL SELECT 'unit', 'mmo_unit_stat_current', 'unit_stat_rows_current', COUNT(*) FROM mmo_unit_stat_current
      UNION ALL SELECT 'unit', 'mmo_unit_stat_sheet_current', 'unit_stat_sheet_current', COUNT(*) FROM mmo_unit_stat_sheet_current
      UNION ALL SELECT 'content', 'mmo_creature_templates_current', 'content_creature_template_current', COUNT(*) FROM mmo_creature_templates_current
      UNION ALL SELECT 'world', 'mmo_creature_spawns_current', 'world_creature_spawn_current', COUNT(*) FROM mmo_creature_spawns_current
      UNION ALL SELECT 'world', 'mmo_world_clock_current', 'world_clock_current', COUNT(*) FROM mmo_world_clock_current
      UNION ALL SELECT 'world', 'mmo_creature_inventory_current', 'world_creature_inventory_current', COUNT(*) FROM mmo_creature_inventory_current
      UNION ALL SELECT 'world', 'mmo_creature_inventory_snapshots_current', 'world_creature_inventory_snapshot_current', COUNT(*) FROM mmo_creature_inventory_snapshots_current
      UNION ALL SELECT 'world', 'mmo_creature_relations_current', 'world_creature_relation_current', COUNT(*) FROM mmo_creature_relations_current
      UNION ALL SELECT 'content', 'mmo_world_templates', 'world_template_baseline', COUNT(*) FROM mmo_world_templates
      UNION ALL SELECT 'world', 'mmo_world_instances', 'world_instance_current', COUNT(*) FROM mmo_world_instances
      UNION ALL SELECT 'content', 'mmo_world_baseline_creature_templates', 'world_template_creature_baseline', COUNT(*) FROM mmo_world_baseline_creature_templates
      UNION ALL SELECT 'content', 'mmo_world_baseline_creatures', 'world_creature_baseline', COUNT(*) FROM mmo_world_baseline_creatures
      UNION ALL SELECT 'content', 'mmo_world_baseline_creature_stats', 'world_creature_stat_baseline', COUNT(*) FROM mmo_world_baseline_creature_stats
      UNION ALL SELECT 'content', 'mmo_world_baseline_creature_inventory', 'world_creature_inventory_baseline', COUNT(*) FROM mmo_world_baseline_creature_inventory
      UNION ALL SELECT 'content', 'mmo_world_baseline_items', 'world_item_baseline', COUNT(*) FROM mmo_world_baseline_items
      UNION ALL SELECT 'content', 'mmo_world_baseline_interactives', 'world_interactive_baseline', COUNT(*) FROM mmo_world_baseline_interactives
      UNION ALL SELECT 'content', 'mmo_world_baseline_container_inventory', 'world_container_baseline', COUNT(*) FROM mmo_world_baseline_container_inventory
      UNION ALL SELECT 'content', 'mmo_world_baseline_script_globals', 'world_script_baseline', COUNT(*) FROM mmo_world_baseline_script_globals
      UNION ALL SELECT 'character', 'mmo_characters_current', 'character_current', COUNT(*) FROM mmo_characters_current
      UNION ALL SELECT 'character', 'mmo_character_inventory_current', 'character_inventory_current', COUNT(*) FROM mmo_character_inventory_current
      UNION ALL SELECT 'character', 'mmo_character_wallet_current', 'character_wallet_current', COUNT(*) FROM mmo_character_wallet_current
      UNION ALL SELECT 'character', 'mmo_character_quests_current', 'character_quest_current', COUNT(*) FROM mmo_character_quests_current
      UNION ALL SELECT 'character', 'mmo_character_known_dialogs_current', 'character_dialog_current', COUNT(*) FROM mmo_character_known_dialogs_current
      UNION ALL SELECT 'character', 'mmo_character_story_progress_current', 'character_story_progress_current', COUNT(*) FROM mmo_character_story_progress_current
      UNION ALL SELECT 'world', 'mmo_world_items_current', 'world_item_current', COUNT(*) FROM mmo_world_items_current
      UNION ALL SELECT 'world', 'mmo_world_interactives_current', 'world_interactive_current', COUNT(*) FROM mmo_world_interactives_current
      UNION ALL SELECT 'world', 'mmo_world_container_inventory_current', 'world_container_current', COUNT(*) FROM mmo_world_container_inventory_current
      UNION ALL SELECT 'world', 'mmo_script_globals_current', 'world_script_current', COUNT(*) FROM mmo_script_globals_current
      UNION ALL SELECT 'world', 'mmo_script_global_values_current', 'world_script_value_current', COUNT(*) FROM mmo_script_global_values_current
      UNION ALL SELECT 'world', 'mmo_guild_attitudes_current', 'world_guild_attitude_current', COUNT(*) FROM mmo_guild_attitudes_current
      UNION ALL SELECT 'unit', 'v_mmo_unit_stats', 'unit_stat_rows', COUNT(*) FROM v_mmo_unit_stats
      UNION ALL SELECT 'unit', 'v_mmo_unit_stat_sheet', 'unit_stat_sheet', COUNT(*) FROM v_mmo_unit_stat_sheet
      UNION ALL SELECT 'character', 'v_mmo_character_current', 'character_canonical', COUNT(*)
        FROM v_mmo_character_current
      UNION ALL SELECT 'character', 'v_mmo_character_stats', 'character_canonical', COUNT(*) FROM v_mmo_character_stats
      UNION ALL SELECT 'character', 'v_mmo_character_stat_sheet', 'character_canonical', COUNT(*) FROM v_mmo_character_stat_sheet
      UNION ALL SELECT 'character', 'v_mmo_character_inventory', 'character_canonical', COUNT(*) FROM v_mmo_character_inventory
      UNION ALL SELECT 'character', 'v_mmo_character_equipment', 'character_canonical', COUNT(*) FROM v_mmo_character_equipment
      UNION ALL SELECT 'character', 'v_mmo_character_quests', 'character_canonical', COUNT(*) FROM v_mmo_character_quests
      UNION ALL SELECT 'character', 'v_mmo_character_known_dialogs', 'character_canonical', COUNT(*) FROM v_mmo_character_known_dialogs
      UNION ALL SELECT 'character', 'v_mmo_character_story_progress', 'character_canonical', COUNT(*) FROM v_mmo_character_story_progress
      UNION ALL SELECT 'content', 'v_mmo_creature_templates', 'content_creature_template', COUNT(*) FROM v_mmo_creature_templates
      UNION ALL SELECT 'world', 'v_mmo_creature_spawns', 'world_creature_spawn', COUNT(*) FROM v_mmo_creature_spawns
      UNION ALL SELECT 'world', 'v_mmo_creature_stat_sheet', 'world_checkpoint', COUNT(*) FROM v_mmo_creature_stat_sheet
      UNION ALL SELECT 'world', 'v_mmo_world_entities', 'world_checkpoint', COUNT(*) FROM v_mmo_world_entities
      UNION ALL SELECT 'world', 'v_mmo_world_items', 'world_checkpoint', COUNT(*) FROM v_mmo_world_items
      UNION ALL SELECT 'world', 'v_mmo_world_interactives', 'world_checkpoint', COUNT(*) FROM v_mmo_world_interactives
      UNION ALL SELECT 'world', 'v_mmo_world_container_inventory', 'world_checkpoint', COUNT(*) FROM v_mmo_world_container_inventory
      UNION ALL SELECT 'world', 'v_mmo_world_script_state', 'world_checkpoint', COUNT(*) FROM v_mmo_world_script_state
      UNION ALL SELECT 'content', 'v_mmo_waypoint_graph', 'content_navigation', COUNT(*) FROM v_mmo_waypoint_graph
      UNION ALL SELECT 'content', 'v_mmo_npc_routines', 'content_schedule_checkpoint', COUNT(*) FROM v_mmo_npc_routines
      UNION ALL SELECT 'event', 'v_mmo_event_journal', 'event_journal', COUNT(*) FROM v_mmo_event_journal
      UNION ALL SELECT 'runtime', 'v_mmo_npc_relations', 'runtime_relation_checkpoint', COUNT(*) FROM v_mmo_npc_relations
      UNION ALL SELECT 'runtime', 'v_mmo_runtime_npc_navigation', 'runtime_transient', COUNT(*) FROM v_mmo_runtime_npc_navigation;
  )SQL";
  if(!exec(impl->db, schemaViews))
    return false;

  // Populate only missing materialized rows. This keeps schema upgrades from
  // overwriting a previously authoritative current-state before restore runs.
  const char* bootstrapMmoCurrent = R"SQL(
    INSERT OR IGNORE INTO mmo_unit_stat_current(
      unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id,
      display_name, player, stat_domain, stat_family, stat_group, stat_id, stat_key,
      value_kind, persistence_hint, display_order, value, updated_at, persistence_class
    )
    SELECT unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id,
           display_name, player, stat_domain, stat_family, stat_group, stat_id, stat_key,
           value_kind, persistence_hint, display_order, value, updated_at, persistence_class
      FROM v_mmo_unit_stats;
    INSERT OR IGNORE INTO mmo_unit_stat_sheet_current(
      unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id,
      display_name, player, guild, true_guild, level, experience,
      experience_next, learning_points, permanent_attitude, temporary_attitude, dead,
      pos_x, pos_y, pos_z, rotation, waypoint,
      health_current, health_max, mana_current, mana_max, strength, dexterity,
      regenerate_hp, regenerate_mana,
      resist_barrier, resist_blunt, resist_edge, resist_fire, resist_fly, resist_magic, resist_point, resist_fall,
      one_handed_skill, two_handed_skill, bow_skill, crossbow_skill,
      one_handed_hit_chance, two_handed_hit_chance, bow_hit_chance, crossbow_hit_chance,
      picklock_skill, sneak_skill, pickpocket_skill, smith_skill, alchemy_skill, take_animal_trophy_skill,
      foreign_language_skill, acrobat_skill, mage_skill, runes_skill, firemaster_skill, regenerate_skill,
      wisp_detector_skill, updated_at, persistence_class
    )
    SELECT unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id,
           display_name, player, guild, true_guild, level, experience,
           experience_next, learning_points, permanent_attitude, temporary_attitude, dead,
           pos_x, pos_y, pos_z, rotation, waypoint,
           health_current, health_max, mana_current, mana_max, strength, dexterity,
           regenerate_hp, regenerate_mana,
           resist_barrier, resist_blunt, resist_edge, resist_fire, resist_fly, resist_magic, resist_point, resist_fall,
           one_handed_skill, two_handed_skill, bow_skill, crossbow_skill,
           one_handed_hit_chance, two_handed_hit_chance, bow_hit_chance, crossbow_hit_chance,
           picklock_skill, sneak_skill, pickpocket_skill, smith_skill, alchemy_skill, take_animal_trophy_skill,
           foreign_language_skill, acrobat_skill, mage_skill, runes_skill, firemaster_skill, regenerate_skill,
           wisp_detector_skill, updated_at, persistence_class
      FROM v_mmo_unit_stat_sheet;
    INSERT OR IGNORE INTO mmo_world_clock_current(
      world_name, tick_count, world_time_millis, world_day, world_hour, world_minute, updated_at, persistence_class
    )
    SELECT world_name, tick_count, world_time_millis, world_day, world_hour, world_minute, updated_at,
           'world_clock_current'
      FROM runtime_world_clock;
    INSERT OR IGNORE INTO mmo_characters_current(
      character_key, account_key, realm_key, world_name, tick_count, display_name,
      pos_x, pos_y, pos_z, rotation, health_current, health_max, mana_current, mana_max,
      level, experience, updated_at, persistence_class
    )
    SELECT character_key, account_key, realm_key, world_name, tick_count, display_name,
           pos_x, pos_y, pos_z, rotation, hp, hp_max, mana, mana_max,
           level, experience, updated_at, persistence_class
      FROM v_mmo_character_current;
    INSERT OR IGNORE INTO mmo_character_inventory_current(
      character_key, item_instance_key, item_template_symbol, item_display_name,
      amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id,
      updated_at, persistence_class
    )
    SELECT character_key, item_instance_key, item_template_symbol, item_display_name,
           amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id,
           updated_at, persistence_class
      FROM v_mmo_character_inventory;
    INSERT OR IGNORE INTO mmo_character_wallet_current(
      character_key, currency_key, currency_display_name, item_template_symbol, amount, updated_at, persistence_class
    )
    SELECT character_key, currency_key, currency_display_name, item_template_symbol, amount, updated_at,
           'character_wallet_current'
      FROM runtime_character_wallet;
    INSERT OR IGNORE INTO mmo_character_quests_current(
      character_key, quest_key, quest_name, section, status, entry_count, entries_text, updated_at, persistence_class
    )
    SELECT character_key, quest_key, name, section, status, entry_count, entries_text, updated_at, persistence_class
      FROM v_mmo_character_quests;
    INSERT OR IGNORE INTO mmo_character_known_dialogs_current(
      character_key, npc_symbol_index, info_symbol_index, npc_symbol_name, info_symbol_name,
      description, permanent, first_seen_tick, updated_at, persistence_class
    )
    SELECT character_key, npc_symbol_index, info_symbol_index, npc_symbol_name, info_symbol_name,
           COALESCE(description, ''), permanent, first_seen_tick, updated_at, persistence_class
      FROM v_mmo_character_known_dialogs;
    INSERT OR IGNORE INTO mmo_character_story_progress_current(
      character_key, world_name, tick_count, chapter_number, chapter_key,
      source_global_key, source_symbol_index, source_symbol_name, updated_at, persistence_class
    )
    SELECT character_key, world_name, tick_count, chapter_number, chapter_key,
           source_global_key, source_symbol_index, source_symbol_name, updated_at, persistence_class
      FROM v_mmo_character_story_progress;
    INSERT OR IGNORE INTO mmo_world_items_current(
      item_spawn_key, world_name, tick_count, slot_id, persistent_id, item_template_symbol, script_id,
      item_display_name, visual, amount, main_flag, item_flags, value, pos_x, pos_y, pos_z,
      exists_in_world, updated_at, persistence_class
    )
    SELECT entity_key, world_name, tick_count, slot_id, persistent_id, symbol_index, script_id,
           display_name, visual, amount, main_flag, item_flags, value, pos_x, pos_y, pos_z,
           1, updated_at, 'world_persistent_item'
      FROM runtime_world_items;
    INSERT OR IGNORE INTO mmo_world_interactives_current(
      interactive_key, world_name, tick_count, slot_id, vob_id, tag, focus_name, display_name, scheme,
      pos_x, pos_y, pos_z, state, state_count, state_mask, container, door, ladder, locked, cracked,
      updated_at, persistence_class
    )
    SELECT entity_key, world_name, tick_count, slot_id, vob_id, tag, focus_name, display_name, scheme,
           pos_x, pos_y, pos_z, state, state_count, state_mask, container, door, ladder, locked, cracked,
           updated_at, 'world_persistent_interactive'
      FROM runtime_world_mobsi;
    INSERT OR IGNORE INTO mmo_world_container_inventory_current(
      owner_key, item_instance_key, world_name, owner_display_name, item_template_symbol, item_display_name,
      amount, iterator_count, value, updated_at, persistence_class
    )
    SELECT owner_key, item_key, world_name, owner_display_name, symbol_index, display_name,
           amount, iterator_count, value, updated_at, 'world_persistent_container_inventory'
      FROM runtime_world_mobsi_inventory;
    INSERT OR IGNORE INTO mmo_script_globals_current(
      global_key, symbol_index, symbol_name, value_type, category, value_count, value_text, updated_at, persistence_class
    )
    SELECT global_key, symbol_index, symbol_name, value_type, category, value_count, value_text, updated_at,
           CASE WHEN category IN ('quest', 'dialog') THEN 'world_persistent_delta' ELSE 'world_script_state' END
      FROM runtime_script_globals;
    INSERT OR IGNORE INTO mmo_script_global_values_current(
      global_key, value_index, value_int, value_real, value_text, updated_at
    )
    SELECT global_key, value_index, value_int, value_real, value_text, updated_at
      FROM runtime_script_global_values;
    INSERT OR IGNORE INTO mmo_guild_attitudes_current(
      realm_key, from_guild, to_guild, attitude, updated_at
    )
    SELECT 'local-g2notr', from_guild, to_guild, attitude, updated_at
      FROM runtime_guild_attitudes;
  )SQL";
  if(!exec(impl->db, bootstrapMmoCurrent))
    return false;
  if(!normalizeSqliteTextStorage(impl->db))
    return false;
  if(!normalizeDialogEventTypes(impl->db))
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

  bool restoreState = impl->restoreState;
  if(restoreState && !impl->saveSlotPath.empty()) {
    restoreState = restoreSaveSlotSnapshot(impl->db, impl->saveSlotPath);
    if(!restoreState)
      Tempest::Log::i("mmo sqlite save-slot snapshot missing, keeping legacy save state: ", impl->saveSlotPath);
    }

  if(restoreState) {
    const std::string currentWorld = worldName(game);
    auto columnText = [](sqlite3_stmt* query, int column) {
      const auto* raw = sqlite3_column_text(query, column);
      return raw!=nullptr ? std::string(reinterpret_cast<const char*>(raw)) : std::string();
      };
    auto restoreUnitState = [&](Npc& npc, const std::string& unitKey, bool restoreTransform) {
      Npc::PersistentState state;
      state.guild      = int32_t(npc.guild());
      state.trueGuild  = npc.trueGuild();
      state.level      = npc.level();
      state.experience = npc.experience();
      state.experienceNext = npc.experienceNext();
      state.learningPoints = npc.learningPoints();
      state.permanentAttitude = int32_t(npc.attitude());
      state.temporaryAttitude = int32_t(npc.tempAttitude());
      state.dead       = npc.isDead();
      const auto& npcHandle = npc.handle();
      for(size_t i=0; i<state.attributes.size(); ++i)
        state.attributes[i] = npc.attribute(Attribute(i));
      for(size_t i=0; i<state.protections.size(); ++i)
        state.protections[i] = npc.protection(Protection(i));
      for(size_t i=0; i<state.talentSkills.size(); ++i) {
        state.talentSkills[i] = npc.talentSkill(Talent(i));
        state.talentValues[i] = npc.talentValue(Talent(i));
        state.hitChances[i]   = npc.hitChance(Talent(i));
        }
      for(size_t i=0; i<state.missions.size(); ++i)
        state.missions[i] = npcHandle.mission[i];
      for(size_t i=0; i<state.aiVariables.size(); ++i)
        state.aiVariables[i] = npcHandle.aivar[i];

      bool valid = false;
      float posX = 0.f, posY = 0.f, posZ = 0.f, rotation = 0.f;
      sqlite3_stmt* query = nullptr;
      const char* unitSql = R"SQL(
        SELECT world_name, guild, true_guild, level, experience,
               experience_next, learning_points, permanent_attitude, temporary_attitude,
               dead, pos_x, pos_y, pos_z, rotation
          FROM mmo_unit_stat_sheet_current
         WHERE unit_key=?1
      )SQL";
      if(sqlite3_prepare_v2(impl->db, unitSql, -1, &query, nullptr)==SQLITE_OK) {
        bindText(query, 1, unitKey);
        if(sqlite3_step(query)==SQLITE_ROW && columnText(query, 0)==currentWorld) {
          state.guild             = sqlite3_column_int(query, 1);
          state.trueGuild         = sqlite3_column_int(query, 2);
          state.level             = sqlite3_column_int(query, 3);
          state.experience        = sqlite3_column_int(query, 4);
          state.experienceNext    = sqlite3_column_int(query, 5);
          state.learningPoints    = sqlite3_column_int(query, 6);
          state.permanentAttitude = sqlite3_column_int(query, 7);
          state.temporaryAttitude = sqlite3_column_int(query, 8);
          state.dead              = sqlite3_column_int(query, 9)!=0;
          posX                    = float(sqlite3_column_double(query, 10));
          posY                    = float(sqlite3_column_double(query, 11));
          posZ                    = float(sqlite3_column_double(query, 12));
          rotation                = float(sqlite3_column_double(query, 13));
          valid                   = true;
          }
        sqlite3_finalize(query);
        }
      if(!valid)
        return false;

      const char* statSql = R"SQL(
        SELECT stat_group, stat_id, value
          FROM mmo_unit_stat_current
         WHERE unit_key=?1
      )SQL";
      if(sqlite3_prepare_v2(impl->db, statSql, -1, &query, nullptr)==SQLITE_OK) {
        bindText(query, 1, unitKey);
        while(sqlite3_step(query)==SQLITE_ROW) {
          const std::string group = columnText(query, 0);
          const int64_t id = sqlite3_column_int64(query, 1);
          const int32_t value = sqlite3_column_int(query, 2);
          if(group=="attribute" && id>=0 && id<ATR_MAX)
            state.attributes[size_t(id)] = value;
          else if(group=="protection" && id>=0 && id<PROT_MAX)
            state.protections[size_t(id)] = value;
          else if(group=="talent_skill" && id>=0 && id<TALENT_MAX_G2)
            state.talentSkills[size_t(id)] = value;
          else if(group=="talent_value" && id>=0 && id<TALENT_MAX_G2)
            state.talentValues[size_t(id)] = value;
          else if(group=="hit_chance" && id>=0 && id<TALENT_MAX_G2)
            state.hitChances[size_t(id)] = value;
          else if(group=="progression" && id==0)
            state.experienceNext = value;
          else if(group=="progression" && id==1)
            state.learningPoints = value;
          else if(group=="attitude" && id==0)
            state.permanentAttitude = value;
          else if(group=="attitude" && id==1)
            state.temporaryAttitude = value;
          else if(group=="mission" && id>=0 && id<int64_t(state.missions.size()))
            state.missions[size_t(id)] = value;
          else if(group=="aivar" && id>=0 && id<int64_t(state.aiVariables.size()))
            state.aiVariables[size_t(id)] = value;
          }
        sqlite3_finalize(query);
        }

      npc.restorePersistentState(state);
      if(restoreTransform) {
        npc.setPosition(posX, posY, posZ);
        npc.setDirection(rotation);
        }
      return true;
      };

    auto restoreCreatureInventory = [&](Npc& npc, const std::string& creatureKey) {
      sqlite3_stmt* query = nullptr;
      bool hasSnapshot = false;
      if(sqlite3_prepare_v2(impl->db,
          "SELECT 1 FROM mmo_creature_inventory_snapshots_current "
          "WHERE creature_spawn_key=?1 AND world_name=?2",
          -1, &query, nullptr)==SQLITE_OK) {
        bindText(query, 1, creatureKey);
        bindText(query, 2, currentWorld);
        hasSnapshot = sqlite3_step(query)==SQLITE_ROW;
        sqlite3_finalize(query);
        }
      if(!hasSnapshot)
        return false;

      std::map<size_t, Npc::PersistentInventoryItem> inventory;
      if(sqlite3_prepare_v2(impl->db,
          "SELECT item_template_symbol,iterator_count,equipped FROM mmo_creature_inventory_current "
          "WHERE creature_spawn_key=?1 AND world_name=?2",
          -1, &query, nullptr)==SQLITE_OK) {
        bindText(query, 1, creatureKey);
        bindText(query, 2, currentWorld);
        while(sqlite3_step(query)==SQLITE_ROW) {
          const int64_t symbol = sqlite3_column_int64(query, 0);
          const int64_t count = sqlite3_column_int64(query, 1);
          if(symbol<0 || count<=0)
            continue;
          auto& item = inventory[size_t(symbol)];
          item.instanceSymbol = size_t(symbol);
          item.count += size_t(count);
          item.equipped = item.equipped || sqlite3_column_int(query, 2)!=0;
          }
        sqlite3_finalize(query);
        }
      std::vector<Npc::PersistentInventoryItem> rows;
      rows.reserve(inventory.size());
      for(auto& [_, item] : inventory)
        rows.emplace_back(item);
      npc.restorePersistentInventory(rows);
      return true;
      };

    Npc* player = game.player();
    bool heroSnapshot = false;
    if(player!=nullptr && sqlite3_prepare_v2(impl->db,
        "SELECT world_name,pos_x,pos_y,pos_z,rotation FROM mmo_characters_current WHERE character_key=?1",
        -1, &stmt, nullptr)==SQLITE_OK) {
      bindText(stmt, 1, HeroKey);
      if(sqlite3_step(stmt)==SQLITE_ROW && columnText(stmt, 0)==currentWorld) {
        player->setPosition(float(sqlite3_column_double(stmt, 1)),
                            float(sqlite3_column_double(stmt, 2)),
                            float(sqlite3_column_double(stmt, 3)));
        player->setDirection(float(sqlite3_column_double(stmt, 4)));
        heroSnapshot = true;
        }
      sqlite3_finalize(stmt);
      }

    if(heroSnapshot) {
      if(auto* world = game.world(); world!=nullptr && sqlite3_prepare_v2(impl->db,
          "SELECT world_time_millis FROM mmo_world_clock_current WHERE world_name=?1",
          -1, &stmt, nullptr)==SQLITE_OK) {
        bindText(stmt, 1, currentWorld);
        if(sqlite3_step(stmt)==SQLITE_ROW) {
          game.setTime(gtime::fromInt(sqlite3_column_int64(stmt, 0)));
          world->resetPositionToTA();
          }
        sqlite3_finalize(stmt);
        }
      }

    int restoredUnits = 0;
    int restoredRelations = 0;
    if(heroSnapshot && player!=nullptr) {
      std::map<size_t, Npc::PersistentInventoryItem> inventory;
      if(sqlite3_prepare_v2(impl->db,
          "SELECT item_template_symbol,iterator_count,equipped FROM mmo_character_inventory_current "
          "WHERE character_key=?1",
          -1, &stmt, nullptr)==SQLITE_OK) {
        bindText(stmt, 1, HeroKey);
        while(sqlite3_step(stmt)==SQLITE_ROW) {
          const int64_t symbol = sqlite3_column_int64(stmt, 0);
          const int64_t count = sqlite3_column_int64(stmt, 1);
          if(symbol<0 || count<=0)
            continue;
          auto& item = inventory[size_t(symbol)];
          item.instanceSymbol = size_t(symbol);
          item.count += size_t(count);
          item.equipped = item.equipped || sqlite3_column_int(stmt, 2)!=0;
          }
        sqlite3_finalize(stmt);
        }
      std::vector<Npc::PersistentInventoryItem> inventoryRows;
      inventoryRows.reserve(inventory.size());
      for(auto& [_, item] : inventory)
        inventoryRows.emplace_back(item);
      player->restorePersistentInventory(inventoryRows);

      std::string heroUnitKey;
      if(sqlite3_prepare_v2(impl->db,
          "SELECT unit_key FROM mmo_unit_stat_sheet_current "
          "WHERE character_key=?1 AND world_name=?2 LIMIT 1",
          -1, &stmt, nullptr)==SQLITE_OK) {
        bindText(stmt, 1, HeroKey);
        bindText(stmt, 2, currentWorld);
        if(sqlite3_step(stmt)==SQLITE_ROW)
          heroUnitKey = columnText(stmt, 0);
        sqlite3_finalize(stmt);
        }
      if(!heroUnitKey.empty() && restoreUnitState(*player, heroUnitKey, true))
        ++restoredUnits;
      }

    if(heroSnapshot) {
      if(auto* script = game.script()) {
        std::vector<QuestLog::Quest> quests;
        if(sqlite3_prepare_v2(impl->db,
            "SELECT quest_name,section,status,entries_text FROM mmo_character_quests_current "
            "WHERE character_key=?1 ORDER BY quest_key",
            -1, &stmt, nullptr)==SQLITE_OK) {
          bindText(stmt, 1, HeroKey);
          while(sqlite3_step(stmt)==SQLITE_ROW) {
            const int section = sqlite3_column_int(stmt, 1);
            const int status = sqlite3_column_int(stmt, 2);
            if((section!=QuestLog::Mission && section!=QuestLog::Note) ||
               status<int(QuestLog::Status::Running) || status>int(QuestLog::Status::Obsolete))
              continue;
            QuestLog::Quest quest;
            quest.name = columnText(stmt, 0);
            quest.section = QuestLog::Section(section);
            quest.status = QuestLog::Status(status);
            const std::string entries = columnText(stmt, 3);
            if(entries!="(no entries)") {
              constexpr std::string_view Separator = "\n---\n";
              size_t at = 0;
              while(at<=entries.size()) {
                const size_t next = entries.find(Separator, at);
                quest.entry.emplace_back(entries.substr(at, next==std::string::npos ? std::string::npos : next-at));
                if(next==std::string::npos)
                  break;
                at = next + Separator.size();
                }
              }
            quests.emplace_back(std::move(quest));
            }
          sqlite3_finalize(stmt);
          }
        script->restoreQuestLogForPersistence(std::move(quests));

        std::set<std::pair<size_t,size_t>> dialogs;
        if(sqlite3_prepare_v2(impl->db,
            "SELECT npc_symbol_index,info_symbol_index FROM mmo_character_known_dialogs_current "
            "WHERE character_key=?1",
            -1, &stmt, nullptr)==SQLITE_OK) {
          bindText(stmt, 1, HeroKey);
          while(sqlite3_step(stmt)==SQLITE_ROW) {
            const int64_t npcSymbol = sqlite3_column_int64(stmt, 0);
            const int64_t infoSymbol = sqlite3_column_int64(stmt, 1);
            if(npcSymbol>=0 && infoSymbol>=0)
              dialogs.emplace(size_t(npcSymbol), size_t(infoSymbol));
            }
          sqlite3_finalize(stmt);
          }
        script->restoreKnownDialogsForPersistence(std::move(dialogs));

        if(sqlite3_prepare_v2(impl->db,
            "SELECT g.symbol_index,g.value_type,v.value_index,v.value_int,v.value_real,v.value_text "
            "FROM mmo_script_globals_current g "
            "JOIN mmo_script_global_values_current v ON v.global_key=g.global_key "
            "ORDER BY g.symbol_index,v.value_index",
            -1, &stmt, nullptr)==SQLITE_OK) {
          while(sqlite3_step(stmt)==SQLITE_ROW) {
            const int64_t symbol = sqlite3_column_int64(stmt, 0);
            const std::string type = columnText(stmt, 1);
            const int64_t valueIndex = sqlite3_column_int64(stmt, 2);
            if(symbol<0 || valueIndex<0 || valueIndex>uint16_t(-1))
              continue;
            if(type=="int")
              script->restoreGlobalIntForPersistence(size_t(symbol), uint16_t(valueIndex), sqlite3_column_int(stmt, 3));
            else if(type=="float")
              script->restoreGlobalFloatForPersistence(size_t(symbol), uint16_t(valueIndex), float(sqlite3_column_double(stmt, 4)));
            else if(type=="string")
              script->restoreGlobalStringForPersistence(size_t(symbol), uint16_t(valueIndex), columnText(stmt, 5));
            }
          sqlite3_finalize(stmt);
          }

        if(sqlite3_prepare_v2(impl->db,
            "SELECT from_guild,to_guild,attitude FROM mmo_guild_attitudes_current WHERE realm_key=?1",
            -1, &stmt, nullptr)==SQLITE_OK) {
          bindText(stmt, 1, "local-g2notr");
          while(sqlite3_step(stmt)==SQLITE_ROW) {
            const int64_t from = sqlite3_column_int64(stmt, 0);
            const int64_t to = sqlite3_column_int64(stmt, 1);
            if(from>=0 && to>=0)
              script->restoreGuildAttitudeForPersistence(size_t(from), size_t(to), sqlite3_column_int(stmt, 2));
            }
          sqlite3_finalize(stmt);
          }
        }
      }

    if(auto* world = game.world(); heroSnapshot && world!=nullptr) {
      std::map<std::string, Npc*> restoredCreatures;
      for(uint32_t i=0; i<world->npcCount(); ++i) {
        Npc* npc = world->npcById(i);
        if(npc==nullptr || npc->isPlayer())
          continue;
        const std::string creatureKey = npcEntityKey(game, *npc);
        restoredCreatures[creatureKey] = npc;
        restoreCreatureInventory(*npc, creatureKey);
        if(restoreUnitState(*npc, creatureKey, true))
          ++restoredUnits;
        }

      if(sqlite3_prepare_v2(impl->db,
          "SELECT creature_spawn_key,target_key,other_key,victim_key,ai_state_function,state_elapsed_millis "
          "FROM mmo_creature_relations_current WHERE world_name=?1",
          -1, &stmt, nullptr)==SQLITE_OK) {
        bindText(stmt, 1, currentWorld);
        auto resolveRelationNpc = [&](const std::string& key) -> Npc* {
          if(key==HeroKey)
            return player;
          const auto found = restoredCreatures.find(key);
          return found==restoredCreatures.end() ? nullptr : found->second;
          };
        while(sqlite3_step(stmt)==SQLITE_ROW) {
          const auto owner = restoredCreatures.find(columnText(stmt, 0));
          if(owner==restoredCreatures.end())
            continue;
          Npc* target = resolveRelationNpc(columnText(stmt, 1));
          Npc* other  = resolveRelationNpc(columnText(stmt, 2));
          Npc* victim = resolveRelationNpc(columnText(stmt, 3));
          if(target==nullptr && other==nullptr && victim==nullptr)
            continue;
          owner->second->setTarget(target);
          owner->second->setOther(other);
          owner->second->setVictim(victim);

          const int64_t stateFunction = sqlite3_column_int64(stmt, 4);
          if(stateFunction<0)
            continue;
          if(auto* script = game.script(); script==nullptr || script->findSymbol(size_t(stateFunction))==nullptr)
            continue;
          if(owner->second->startState(ScriptFn(size_t(stateFunction)), "", gtime::endOfTime(), true)) {
            owner->second->setStateTime(std::max<int64_t>(0, sqlite3_column_int64(stmt, 5)));
            ++restoredRelations;
            }
          }
        sqlite3_finalize(stmt);
        }

      struct ContainerItem final {
        size_t symbol = size_t(-1);
        size_t count = 0;
        };
      std::map<std::string, std::vector<ContainerItem>> containerItems;
      if(sqlite3_prepare_v2(impl->db,
          "SELECT owner_key,item_template_symbol,iterator_count FROM mmo_world_container_inventory_current "
          "WHERE world_name=?1",
          -1, &stmt, nullptr)==SQLITE_OK) {
        bindText(stmt, 1, currentWorld);
        while(sqlite3_step(stmt)==SQLITE_ROW) {
          const int64_t symbol = sqlite3_column_int64(stmt, 1);
          const int64_t count = sqlite3_column_int64(stmt, 2);
          if(symbol>=0 && count>0)
            containerItems[columnText(stmt, 0)].push_back({size_t(symbol), size_t(count)});
          }
        sqlite3_finalize(stmt);
        }

      if(sqlite3_prepare_v2(impl->db,
          "SELECT interactive_key,slot_id,vob_id,state,locked,cracked FROM mmo_world_interactives_current "
          "WHERE world_name=?1",
          -1, &stmt, nullptr)==SQLITE_OK) {
        bindText(stmt, 1, currentWorld);
        while(sqlite3_step(stmt)==SQLITE_ROW) {
          const std::string key = columnText(stmt, 0);
          const int64_t slot = sqlite3_column_int64(stmt, 1);
          Interactive* interactive = slot>=0 ? world->mobsiById(uint32_t(slot)) : nullptr;
          if(interactive==nullptr || int64_t(interactive->getId())!=sqlite3_column_int64(stmt, 2))
            continue;
          interactive->restorePersistentState(sqlite3_column_int(stmt, 3),
                                              sqlite3_column_int(stmt, 4)!=0,
                                              sqlite3_column_int(stmt, 5)!=0);
          auto& inventory = interactive->inventory();
          inventory.resetForPersistence(*interactive);
          const auto found = containerItems.find(key);
          if(found!=containerItems.end()) {
            for(const auto& item : found->second)
              inventory.addItem(item.symbol, item.count, *world);
            }
          }
        sqlite3_finalize(stmt);
        }

      if(sqlite3_prepare_v2(impl->db,
          "SELECT slot_id,persistent_id,item_template_symbol,amount,pos_x,pos_y,pos_z,exists_in_world "
          "FROM mmo_world_items_current WHERE world_name=?1",
          -1, &stmt, nullptr)==SQLITE_OK) {
        bindText(stmt, 1, currentWorld);
        while(sqlite3_step(stmt)==SQLITE_ROW) {
          const int64_t slot = sqlite3_column_int64(stmt, 0);
          const int64_t persistentId = sqlite3_column_int64(stmt, 1);
          const int64_t symbol = sqlite3_column_int64(stmt, 2);
          const int64_t amount = sqlite3_column_int64(stmt, 3);
          if(slot<0 || symbol<0 || amount<0)
            continue;
          Item* item = world->itmById(uint32_t(slot));
          const bool matches = item!=nullptr && int64_t(item->clsId())==symbol &&
                               int64_t(item->persistentId())==persistentId;
          const bool exists = sqlite3_column_int(stmt, 7)!=0;
          if(!exists) {
            if(matches)
              world->removeItem(*item);
            continue;
            }
          const float x = float(sqlite3_column_double(stmt, 4));
          const float y = float(sqlite3_column_double(stmt, 5));
          const float z = float(sqlite3_column_double(stmt, 6));
          if(matches) {
            item->setPosition(x, y, z);
            item->setCount(size_t(amount));
            }
          else if(item==nullptr) {
            if(Item* spawned = world->addItem(size_t(symbol), Tempest::Vec3(x, y, z))) {
              spawned->setPersistentId(uint32_t(persistentId));
              spawned->setCount(size_t(amount));
              }
            }
          }
        sqlite3_finalize(stmt);
        }
      }

    if(heroSnapshot)
      Tempest::Log::i("mmo sqlite restored canonical state from runtime DB: units=", restoredUnits,
                      ", relations=", restoredRelations);
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

  const std::string eventType = dialogSelectionEventType(phaseText);
  if(sqlite3_prepare_v2(impl->db,
      "INSERT INTO runtime_events(session_id, event_type, entity_key, subject_key, tick_count, value_before, value_after, delta, data_text) "
      "VALUES(?1, ?2, ?3, ?4, ?5, 0, 1, 1, ?6)",
      -1, &stmt, nullptr)==SQLITE_OK) {
    bindInt (stmt, 1, impl->sessionId);
    bindText(stmt, 2, eventType);
    bindText(stmt, 3, npcKey);
    bindText(stmt, 4, infoName);
    bindInt (stmt, 5, int64_t(game.tickCount()));
    bindText(stmt, 6, title);
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

void MmoRuntimeSqlite::recordChapterIntro(GameSession& game, std::string_view title, std::string_view subtitle,
                                          std::string_view image, std::string_view sound, int time) {
#if defined(OPENGOTHIC_HAVE_SQLITE)
  if(impl->path.empty())
    return;
  if(!impl->opened && !open(game))
    return;
  if(impl->db==nullptr)
    return;

  const std::string world = worldName(game);
  sqlite3_stmt* stmt = nullptr;
  if(sqlite3_prepare_v2(impl->db,
      "INSERT INTO runtime_chapter_intro_events("
      "session_id, world_name, tick_count, title, subtitle, image, sound, duration"
      ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
      -1, &stmt, nullptr)==SQLITE_OK) {
    bindInt (stmt, 1, impl->sessionId);
    bindText(stmt, 2, world);
    bindInt (stmt, 3, int64_t(game.tickCount()));
    bindText(stmt, 4, title);
    bindText(stmt, 5, subtitle);
    bindText(stmt, 6, image);
    bindText(stmt, 7, sound);
    bindInt (stmt, 8, int64_t(time));
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite chapter intro insert failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }
  else {
    Tempest::Log::e("mmo sqlite chapter intro prepare failed: ", sqlite3_errmsg(impl->db));
    }

  if(sqlite3_prepare_v2(impl->db,
      "INSERT INTO runtime_events(session_id, event_type, entity_key, subject_key, world_name, tick_count, value_before, value_after, delta, data_text) "
      "VALUES(?1, 'story_chapter_introduced', ?2, ?3, ?4, ?5, 0, 1, 1, ?6)",
      -1, &stmt, nullptr)==SQLITE_OK) {
    bindInt (stmt, 1, impl->sessionId);
    bindText(stmt, 2, HeroKey);
    bindText(stmt, 3, title);
    bindText(stmt, 4, world);
    bindInt (stmt, 5, int64_t(game.tickCount()));
    bindText(stmt, 6, subtitle);
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite chapter intro event insert failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    }
#else
  (void)game;
  (void)title;
  (void)subtitle;
  (void)image;
  (void)sound;
  (void)time;
#endif
  }

void MmoRuntimeSqlite::tick(GameSession& game, uint64_t dt) {
  if(impl->path.empty())
    return;
  if(!impl->opened && !open(game))
    return;
  if(dt>=impl->untilFlush) {
    impl->untilFlush = impl->intervalMs;
    flush(game, false);
    return;
    }
  impl->untilFlush -= dt;
  }

void MmoRuntimeSqlite::flush(GameSession& game) {
  flush(game, true);
  }

void MmoRuntimeSqlite::recordSaveSlot(GameSession& game, std::string_view slotPath, std::string_view displayName) {
#if !defined(OPENGOTHIC_HAVE_SQLITE)
  (void)game;
  (void)slotPath;
  (void)displayName;
#else
  if(slotPath.empty())
    return;
  if(!impl->opened && !open(game))
    return;
  if(impl->db==nullptr)
    return;

  flush(game);

  const std::string slotPathText = std::string(slotPath);
  const std::string slotKey = saveSlotKey(slotPath);
  const std::string name = nonEmpty(displayName, "Legacy save slot");
  const std::string world = worldName(game);
  const int64_t tick = int64_t(game.tickCount());
  const int64_t worldTime = game.time().toInt();

  if(!exec(impl->db, "BEGIN IMMEDIATE TRANSACTION"))
    return;

  sqlite3_stmt* stmt = nullptr;
  const char* upsertSlot = R"SQL(
    INSERT INTO mmo_save_slots(
      slot_key, account_key, realm_key, character_key, source_slot_path, display_name,
      world_name, tick_count, world_time_millis, schema_version, legacy_save_file,
      current_snapshot_id, updated_at, last_saved_at
    )
    VALUES(?1, 'local-account', 'local-g2notr', 'PC_HERO', ?2, ?3,
           ?4, ?5, ?6, 24, 1, NULL, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
    ON CONFLICT(slot_key) DO UPDATE SET
      source_slot_path=excluded.source_slot_path,
      display_name=excluded.display_name,
      world_name=excluded.world_name,
      tick_count=excluded.tick_count,
      world_time_millis=excluded.world_time_millis,
      schema_version=excluded.schema_version,
      legacy_save_file=excluded.legacy_save_file,
      updated_at=CURRENT_TIMESTAMP,
      last_saved_at=CURRENT_TIMESTAMP
  )SQL";
  if(sqlite3_prepare_v2(impl->db, upsertSlot, -1, &stmt, nullptr)!=SQLITE_OK) {
    Tempest::Log::e("mmo sqlite save-slot upsert prepare failed: ", sqlite3_errmsg(impl->db));
    exec(impl->db, "ROLLBACK");
    return;
    }
  bindText(stmt, 1, slotKey);
  bindText(stmt, 2, slotPathText);
  bindText(stmt, 3, name);
  bindText(stmt, 4, world);
  bindInt (stmt, 5, tick);
  bindInt (stmt, 6, worldTime);
  if(sqlite3_step(stmt)!=SQLITE_DONE) {
    Tempest::Log::e("mmo sqlite save-slot upsert failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    exec(impl->db, "ROLLBACK");
    return;
    }
  sqlite3_finalize(stmt);

  const char* insertSnapshot = R"SQL(
    INSERT INTO mmo_save_slot_snapshots(
      slot_key, account_key, realm_key, character_key, source_slot_path, display_name,
      world_name, tick_count, world_time_millis, schema_version, session_id
    )
    VALUES(?1, 'local-account', 'local-g2notr', 'PC_HERO', ?2, ?3, ?4, ?5, ?6, 25, ?7)
  )SQL";
  if(sqlite3_prepare_v2(impl->db, insertSnapshot, -1, &stmt, nullptr)!=SQLITE_OK) {
    Tempest::Log::e("mmo sqlite save-slot snapshot prepare failed: ", sqlite3_errmsg(impl->db));
    exec(impl->db, "ROLLBACK");
    return;
    }
  bindText(stmt, 1, slotKey);
  bindText(stmt, 2, slotPathText);
  bindText(stmt, 3, name);
  bindText(stmt, 4, world);
  bindInt (stmt, 5, tick);
  bindInt (stmt, 6, worldTime);
  bindInt (stmt, 7, impl->sessionId);
  if(sqlite3_step(stmt)!=SQLITE_DONE) {
    Tempest::Log::e("mmo sqlite save-slot snapshot insert failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    exec(impl->db, "ROLLBACK");
    return;
    }
  const int64_t snapshotId = int64_t(sqlite3_last_insert_rowid(impl->db));
  sqlite3_finalize(stmt);

  const std::string id = std::to_string(snapshotId);
  std::string sql;
  sql += "INSERT INTO mmo_save_slot_unit_stat SELECT " + id + ", unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id, display_name, player, stat_domain, stat_family, stat_group, stat_id, stat_key, value_kind, persistence_hint, display_order, value, updated_at, persistence_class FROM mmo_unit_stat_current;\n";
  sql += "INSERT INTO mmo_save_slot_unit_stat_sheet SELECT " + id + ", unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id, display_name, player, guild, true_guild, level, experience, experience_next, learning_points, permanent_attitude, temporary_attitude, dead, pos_x, pos_y, pos_z, rotation, waypoint, health_current, health_max, mana_current, mana_max, strength, dexterity, regenerate_hp, regenerate_mana, resist_barrier, resist_blunt, resist_edge, resist_fire, resist_fly, resist_magic, resist_point, resist_fall, one_handed_skill, two_handed_skill, bow_skill, crossbow_skill, one_handed_hit_chance, two_handed_hit_chance, bow_hit_chance, crossbow_hit_chance, picklock_skill, sneak_skill, pickpocket_skill, smith_skill, alchemy_skill, take_animal_trophy_skill, foreign_language_skill, acrobat_skill, mage_skill, runes_skill, firemaster_skill, regenerate_skill, wisp_detector_skill, updated_at, persistence_class FROM mmo_unit_stat_sheet_current;\n";
  sql += "INSERT INTO mmo_save_slot_characters SELECT " + id + ", character_key, account_key, realm_key, world_name, tick_count, display_name, pos_x, pos_y, pos_z, rotation, health_current, health_max, mana_current, mana_max, level, experience, updated_at, persistence_class FROM mmo_characters_current;\n";
  sql += "INSERT INTO mmo_save_slot_character_inventory SELECT " + id + ", character_key, item_instance_key, item_template_symbol, item_display_name, amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id, updated_at, persistence_class FROM mmo_character_inventory_current;\n";
  sql += "INSERT INTO mmo_save_slot_character_wallet SELECT " + id + ", character_key, currency_key, currency_display_name, item_template_symbol, amount, updated_at, persistence_class FROM mmo_character_wallet_current;\n";
  sql += "INSERT INTO mmo_save_slot_character_quests SELECT " + id + ", character_key, quest_key, quest_name, section, status, entry_count, entries_text, updated_at, persistence_class FROM mmo_character_quests_current;\n";
  sql += "INSERT INTO mmo_save_slot_character_known_dialogs SELECT " + id + ", character_key, npc_symbol_index, info_symbol_index, npc_symbol_name, info_symbol_name, description, permanent, first_seen_tick, updated_at, persistence_class FROM mmo_character_known_dialogs_current;\n";
  sql += "INSERT INTO mmo_save_slot_character_story_progress SELECT " + id + ", character_key, world_name, tick_count, chapter_number, chapter_key, source_global_key, source_symbol_index, source_symbol_name, updated_at, persistence_class FROM mmo_character_story_progress_current;\n";
  sql += "INSERT INTO mmo_save_slot_world_clock SELECT " + id + ", world_name, tick_count, world_time_millis, world_day, world_hour, world_minute, updated_at, persistence_class FROM mmo_world_clock_current;\n";
  sql += "INSERT INTO mmo_save_slot_creature_spawns SELECT " + id + ", creature_spawn_key, creature_template_id, world_name, tick_count, display_name, pos_x, pos_y, pos_z, rotation, waypoint, dead, level, experience, health_current, health_max, mana_current, mana_max, strength, dexterity, current_waypoint_name, routine_waypoint_name, move_hint, move_target_waypoint_name, updated_at, persistence_class FROM mmo_creature_spawns_current;\n";
  sql += "INSERT INTO mmo_save_slot_creature_inventory SELECT " + id + ", creature_spawn_key, item_instance_key, world_name, item_template_symbol, item_display_name, amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id, updated_at, persistence_class FROM mmo_creature_inventory_current;\n";
  sql += "INSERT INTO mmo_save_slot_creature_inventory_snapshots SELECT " + id + ", creature_spawn_key, world_name, tick_count, item_row_count, updated_at, persistence_class FROM mmo_creature_inventory_snapshots_current;\n";
  sql += "INSERT INTO mmo_save_slot_creature_relations SELECT " + id + ", creature_spawn_key, world_name, tick_count, relation_kind, target_key, other_key, victim_key, ai_state_function, ai_state_name, state_elapsed_millis, updated_at, persistence_class FROM mmo_creature_relations_current;\n";
  sql += "INSERT INTO mmo_save_slot_world_items SELECT " + id + ", item_spawn_key, world_name, tick_count, slot_id, persistent_id, item_template_symbol, script_id, item_display_name, visual, amount, main_flag, item_flags, value, pos_x, pos_y, pos_z, exists_in_world, updated_at, persistence_class FROM mmo_world_items_current;\n";
  sql += "INSERT INTO mmo_save_slot_world_interactives SELECT " + id + ", interactive_key, world_name, tick_count, slot_id, vob_id, tag, focus_name, display_name, scheme, pos_x, pos_y, pos_z, state, state_count, state_mask, container, door, ladder, locked, cracked, updated_at, persistence_class FROM mmo_world_interactives_current;\n";
  sql += "INSERT INTO mmo_save_slot_world_container_inventory SELECT " + id + ", owner_key, item_instance_key, world_name, owner_display_name, item_template_symbol, item_display_name, amount, iterator_count, value, updated_at, persistence_class FROM mmo_world_container_inventory_current;\n";
  sql += "INSERT INTO mmo_save_slot_script_globals SELECT " + id + ", global_key, symbol_index, symbol_name, value_type, category, value_count, value_text, updated_at, persistence_class FROM mmo_script_globals_current;\n";
  sql += "INSERT INTO mmo_save_slot_script_global_values SELECT " + id + ", global_key, value_index, value_int, value_real, value_text, updated_at FROM mmo_script_global_values_current;\n";
  sql += "INSERT INTO mmo_save_slot_guild_attitudes SELECT " + id + ", realm_key, from_guild, to_guild, attitude, updated_at FROM mmo_guild_attitudes_current;\n";
  if(!exec(impl->db, sql.c_str())) {
    exec(impl->db, "ROLLBACK");
    return;
    }

  if(sqlite3_prepare_v2(impl->db,
      "UPDATE mmo_save_slots SET current_snapshot_id=?1, updated_at=CURRENT_TIMESTAMP, last_saved_at=CURRENT_TIMESTAMP WHERE slot_key=?2",
      -1, &stmt, nullptr)==SQLITE_OK) {
    bindInt(stmt, 1, snapshotId);
    bindText(stmt, 2, slotKey);
    if(sqlite3_step(stmt)!=SQLITE_DONE) {
      Tempest::Log::e("mmo sqlite save-slot current snapshot update failed: ", sqlite3_errmsg(impl->db));
      sqlite3_finalize(stmt);
      exec(impl->db, "ROLLBACK");
      return;
      }
    sqlite3_finalize(stmt);
    } else {
    Tempest::Log::e("mmo sqlite save-slot current snapshot update prepare failed: ", sqlite3_errmsg(impl->db));
    exec(impl->db, "ROLLBACK");
    return;
    }

  if(!exec(impl->db, "COMMIT")) {
    exec(impl->db, "ROLLBACK");
    return;
    }
  Tempest::Log::i("mmo sqlite saved DB slot snapshot: ", slotPathText, " snapshot=", snapshotId);
#endif
  }

void MmoRuntimeSqlite::flush(GameSession& game, bool materializeCurrent) {
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

  if(impl->sessionId!=0 &&
     sqlite3_prepare_v2(impl->db,
                        "UPDATE runtime_sessions "
                        "SET world_name=?1, tick_count=?2, last_seen_at=CURRENT_TIMESTAMP "
                        "WHERE id=?3",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, world);
    bindInt (stmt, 2, int64_t(game.tickCount()));
    bindInt (stmt, 3, impl->sessionId);
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite failed to update session: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    stmt = nullptr;
    }

  const gtime worldClock = game.time();
  if(sqlite3_prepare_v2(impl->db,
                        "INSERT INTO runtime_world_clock("
                        "world_name,tick_count,world_time_millis,world_day,world_hour,world_minute,updated_at) "
                        "VALUES(?1,?2,?3,?4,?5,?6,CURRENT_TIMESTAMP) "
                        "ON CONFLICT(world_name) DO UPDATE SET "
                        "tick_count=excluded.tick_count,world_time_millis=excluded.world_time_millis,"
                        "world_day=excluded.world_day,world_hour=excluded.world_hour,"
                        "world_minute=excluded.world_minute,updated_at=CURRENT_TIMESTAMP",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, world);
    bindInt (stmt, 2, int64_t(game.tickCount()));
    bindInt (stmt, 3, worldClock.toInt());
    bindInt (stmt, 4, worldClock.day());
    bindInt (stmt, 5, worldClock.hour());
    bindInt (stmt, 6, worldClock.minute());
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite world clock upsert failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(stmt);
    stmt = nullptr;
    }

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

  auto deleteWorldRows = [&](const char* sql) {
    sqlite3_stmt* deleteStmt = nullptr;
    if(sqlite3_prepare_v2(impl->db, sql, -1, &deleteStmt, nullptr)!=SQLITE_OK) {
      Tempest::Log::e("mmo sqlite world cleanup prepare failed: ", sqlite3_errmsg(impl->db));
      return;
      }
    bindText(deleteStmt, 1, world);
    if(sqlite3_step(deleteStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite world cleanup failed: ", sqlite3_errmsg(impl->db));
    sqlite3_finalize(deleteStmt);
    };

  std::map<const WayPoint*, std::string> waypointKeys;
  if(auto* wrld = game.world()) {
    if(materializeCurrent) {
      deleteWorldRows("DELETE FROM runtime_waypoint_edges WHERE world_name=?1");
      deleteWorldRows("DELETE FROM runtime_waypoints WHERE world_name=?1");
      }

    const char* waypointInsert = R"SQL(
      INSERT OR REPLACE INTO runtime_waypoints(
        waypoint_key, world_name, kind, waypoint_index, name,
        pos_x, pos_y, pos_z, ground_x, ground_y, ground_z, dir_x, dir_y, dir_z,
        underwater, free_point, connected, use_count, ladder_key, updated_at
      )
      VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, CURRENT_TIMESTAMP)
    )SQL";
    sqlite3_stmt* waypointStmt = nullptr;
    if(materializeCurrent && sqlite3_prepare_v2(impl->db, waypointInsert, -1, &waypointStmt, nullptr)!=SQLITE_OK)
      Tempest::Log::e("mmo sqlite waypoint prepare failed: ", sqlite3_errmsg(impl->db));
    wrld->forEachWayPoint([&](const WayPoint& point, size_t index, std::string_view kind) {
      const std::string key = waypointKey(world, kind, index, point.name);
      waypointKeys[&point] = key;
      if(waypointStmt==nullptr)
        return;
      std::string ladderKey;
      if(point.ladder!=nullptr)
        ladderKey = "mobsi:" + world + ":" + std::to_string(wrld->mobsiId(point.ladder));

      sqlite3_reset(waypointStmt);
      sqlite3_clear_bindings(waypointStmt);
      bindText(waypointStmt, 1, key);
      bindText(waypointStmt, 2, world);
      bindText(waypointStmt, 3, std::string(kind));
      bindInt (waypointStmt, 4, int64_t(index));
      bindText(waypointStmt, 5, point.name);
      bindReal(waypointStmt, 6, point.pos.x);
      bindReal(waypointStmt, 7, point.pos.y);
      bindReal(waypointStmt, 8, point.pos.z);
      bindReal(waypointStmt, 9, point.groundPos.x);
      bindReal(waypointStmt,10, point.groundPos.y);
      bindReal(waypointStmt,11, point.groundPos.z);
      bindReal(waypointStmt,12, point.dir.x);
      bindReal(waypointStmt,13, point.dir.y);
      bindReal(waypointStmt,14, point.dir.z);
      bindInt (waypointStmt,15, point.underWater ? 1 : 0);
      bindInt (waypointStmt,16, point.isFreePoint() ? 1 : 0);
      bindInt (waypointStmt,17, point.isConnected() ? 1 : 0);
      bindInt (waypointStmt,18, int64_t(point.useCounter()));
      bindText(waypointStmt,19, ladderKey);
      if(sqlite3_step(waypointStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite waypoint insert failed: ", sqlite3_errmsg(impl->db));
      });
    if(waypointStmt!=nullptr)
      sqlite3_finalize(waypointStmt);

    const char* edgeInsert = R"SQL(
      INSERT OR REPLACE INTO runtime_waypoint_edges(
        edge_key, world_name, from_waypoint_key, to_waypoint_key,
        from_name, to_name, distance, ladder, updated_at
      )
      VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, CURRENT_TIMESTAMP)
    )SQL";
    sqlite3_stmt* edgeStmt = nullptr;
    if(materializeCurrent && sqlite3_prepare_v2(impl->db, edgeInsert, -1, &edgeStmt, nullptr)!=SQLITE_OK)
      Tempest::Log::e("mmo sqlite waypoint edge prepare failed: ", sqlite3_errmsg(impl->db));
    if(edgeStmt!=nullptr) {
      auto insertEdge = [&](const WayPoint& from, const std::string& fromKey,
                            const WayPoint& to, const std::string& toKey,
                            int64_t distance, int64_t ladder) {
        sqlite3_reset(edgeStmt);
        sqlite3_clear_bindings(edgeStmt);
        const std::string edgeKey = "wayedge:" + fromKey + ">" + toKey;
        bindText(edgeStmt, 1, edgeKey);
        bindText(edgeStmt, 2, world);
        bindText(edgeStmt, 3, fromKey);
        bindText(edgeStmt, 4, toKey);
        bindText(edgeStmt, 5, from.name);
        bindText(edgeStmt, 6, to.name);
        bindInt (edgeStmt, 7, distance);
        bindInt (edgeStmt, 8, ladder);
        if(sqlite3_step(edgeStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite waypoint edge insert failed: ", sqlite3_errmsg(impl->db));
        };
      wrld->forEachWayEdge([&](const WayPoint& from, size_t fromIndex,
                               const WayPoint& to, size_t toIndex,
                               int32_t distance) {
        const std::string fromKey = waypointKey(world, "waypoint", fromIndex, from.name);
        const std::string toKey = waypointKey(world, "waypoint", toIndex, to.name);
        const int64_t ladder = from.hasLadderConn(&to) ? 1 : 0;
        insertEdge(from, fromKey, to, toKey, distance, ladder);
        insertEdge(to, toKey, from, fromKey, distance, ladder);
        });
      sqlite3_finalize(edgeStmt);
      }
    }

  auto waypointKeyFor = [&](const WayPoint* point) -> std::string {
    if(point==nullptr)
      return {};
    const auto it = waypointKeys.find(point);
    if(it!=waypointKeys.end())
      return it->second;
    return waypointKey(world, point->isFreePoint() ? "freepoint" : "unknown", 0, point->name);
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
                        "SELECT entity_key,pos_x,pos_y,pos_z,hp,mana,level,experience,dead FROM runtime_world_npcs WHERE world_name=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, world);
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

  std::map<std::string, std::string> previousNpcStatSignatures;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,stat_signature FROM runtime_npc_stat_capture_state WHERE world_name=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, world);
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      const auto* rawSignature = sqlite3_column_text(stmt, 1);
      if(rawKey==nullptr || rawSignature==nullptr)
        continue;
      previousNpcStatSignatures[reinterpret_cast<const char*>(rawKey)] =
        reinterpret_cast<const char*>(rawSignature);
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousNpcStats = !previousNpcStatSignatures.empty();

  std::map<std::string, NpcAiPrevious> previousNpcAi;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,ai_state_name,target_key,state_other_key,state_victim_key,relation_kind FROM runtime_npc_ai_state WHERE world_name=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, world);
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      const auto* rawState = sqlite3_column_text(stmt, 1);
      const auto* rawTarget = sqlite3_column_text(stmt, 2);
      const auto* rawOther = sqlite3_column_text(stmt, 3);
      const auto* rawVictim = sqlite3_column_text(stmt, 4);
      const auto* rawRelation = sqlite3_column_text(stmt, 5);
      NpcAiPrevious prev;
      prev.stateName      = rawState!=nullptr ? reinterpret_cast<const char*>(rawState) : "";
      prev.targetKey      = rawTarget!=nullptr ? reinterpret_cast<const char*>(rawTarget) : "";
      prev.stateOtherKey  = rawOther!=nullptr ? reinterpret_cast<const char*>(rawOther) : "";
      prev.stateVictimKey = rawVictim!=nullptr ? reinterpret_cast<const char*>(rawVictim) : "";
      prev.relationKind   = rawRelation!=nullptr ? reinterpret_cast<const char*>(rawRelation) : "";
      previousNpcAi[reinterpret_cast<const char*>(rawKey)] = std::move(prev);
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousNpcAi = !previousNpcAi.empty();

  std::map<std::string, NpcNavigationPrevious> previousNavigation;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,current_waypoint_key,routine_waypoint_key,move_hint,move_target_waypoint_key,path_next_waypoint_key,path_final_waypoint_key,path_remaining_count "
                        "FROM runtime_npc_navigation_state WHERE world_name=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, world);
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      const auto* rawCurrent = sqlite3_column_text(stmt, 1);
      const auto* rawRoutine = sqlite3_column_text(stmt, 2);
      const auto* rawHint = sqlite3_column_text(stmt, 3);
      const auto* rawMoveTarget = sqlite3_column_text(stmt, 4);
      const auto* rawPathNext = sqlite3_column_text(stmt, 5);
      const auto* rawPathFinal = sqlite3_column_text(stmt, 6);
      NpcNavigationPrevious prev;
      prev.currentWaypointKey    = rawCurrent!=nullptr ? reinterpret_cast<const char*>(rawCurrent) : "";
      prev.routineWaypointKey    = rawRoutine!=nullptr ? reinterpret_cast<const char*>(rawRoutine) : "";
      prev.moveHint              = rawHint!=nullptr ? reinterpret_cast<const char*>(rawHint) : "";
      prev.moveTargetWaypointKey = rawMoveTarget!=nullptr ? reinterpret_cast<const char*>(rawMoveTarget) : "";
      prev.pathNextWaypointKey   = rawPathNext!=nullptr ? reinterpret_cast<const char*>(rawPathNext) : "";
      prev.pathFinalWaypointKey  = rawPathFinal!=nullptr ? reinterpret_cast<const char*>(rawPathFinal) : "";
      prev.pathRemainingCount    = sqlite3_column_int64(stmt, 7);
      previousNavigation[reinterpret_cast<const char*>(rawKey)] = std::move(prev);
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousNavigation = !previousNavigation.empty();

  std::vector<NpcRow> currentNpcs;
  if(auto* wrld = game.world()) {
    Npc* player = game.player();
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
      row.experienceNext = npc->experienceNext();
      row.learningPoints = npc->learningPoints();
      row.permanentAttitude = int64_t(npc->attitude());
      row.temporaryAttitude = int64_t(npc->tempAttitude());
      row.dead         = npc->isDead() ? 1 : 0;
      row.player       = npc->isPlayer() ? 1 : 0;
      row.aiStateFunction = int64_t(npc->currentAiStateFunction());
      row.aiStateElapsedMillis = int64_t(npc->stateTime());
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
      for(size_t m=0; m<row.missions.size(); ++m)
        row.missions[m] = h.mission[m];
      for(size_t a=0; a<row.aiVariables.size(); ++a)
        row.aiVariables[a] = h.aivar[a];
      if(Npc* target = npc->target()) {
        row.targetSymbolIndex = int64_t(target->handle().symbol_index());
        row.targetName = npcDisplayName(*target);
        row.targetKey = target->isPlayer() ? HeroKey : npcEntityKey(game, *target);
        }
      else if(!row.player && npcStateTargetsPlayer(row.aiStateName)) {
        row.targetKey = HeroKey;
        if(player!=nullptr) {
          row.targetSymbolIndex = int64_t(player->handle().symbol_index());
          row.targetName = npcDisplayName(*player);
          }
        else {
          row.targetName = HeroKey;
          }
        }
      if(Npc* other = npc->stateOther())
        row.stateOtherKey = other->isPlayer() ? HeroKey : npcEntityKey(game, *other);
      if(Npc* victim = npc->stateVictim())
        row.stateVictimKey = victim->isPlayer() ? HeroKey : npcEntityKey(game, *victim);
      row.relationKind = npcRelationKind(row);
      currentNpcs.emplace_back(std::move(row));
      }
    }

  std::map<std::string, std::string> currentNpcStatSignatures;
  std::set<std::string> changedNpcStatEntities;
  for(const NpcRow& row : currentNpcs) {
    std::string signature = npcStatSignature(row);
    const auto prev = previousNpcStatSignatures.find(row.entityKey);
    if(prev==previousNpcStatSignatures.end() || prev->second!=signature)
      changedNpcStatEntities.insert(row.entityKey);
    currentNpcStatSignatures.emplace(row.entityKey, std::move(signature));
    }

  // Only rows whose complete engine stat component changed need an EAV lookup.
  std::map<std::string, NpcStatPrevious> previousNpcStats;
  sqlite3_stmt* previousNpcStatStmt = nullptr;
  if(!changedNpcStatEntities.empty() &&
     sqlite3_prepare_v2(impl->db,
                        "SELECT entity_key,stat_group,stat_id,value FROM runtime_npc_stats WHERE entity_key=?1",
                        -1, &previousNpcStatStmt, nullptr)==SQLITE_OK) {
    for(const NpcRow& row : currentNpcs) {
      if(changedNpcStatEntities.find(row.entityKey)==changedNpcStatEntities.end())
        continue;
      sqlite3_reset(previousNpcStatStmt);
      sqlite3_clear_bindings(previousNpcStatStmt);
      bindText(previousNpcStatStmt, 1, row.entityKey);
      while(sqlite3_step(previousNpcStatStmt)==SQLITE_ROW) {
        const auto* rawKey = sqlite3_column_text(previousNpcStatStmt, 0);
        const auto* rawGroup = sqlite3_column_text(previousNpcStatStmt, 1);
        if(rawKey==nullptr || rawGroup==nullptr)
          continue;
        NpcStatPrevious previous;
        previous.value = sqlite3_column_int64(previousNpcStatStmt, 3);
        previousNpcStats[npcStatKey(reinterpret_cast<const char*>(rawKey),
                                    reinterpret_cast<const char*>(rawGroup),
                                    sqlite3_column_int64(previousNpcStatStmt, 2))] = previous;
        }
      }
    sqlite3_finalize(previousNpcStatStmt);
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
                        "SELECT entity_key,state,state_count,state_mask,locked,cracked FROM runtime_world_mobsi",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      MobsiPrevious prev;
      prev.state      = sqlite3_column_int64(stmt, 1);
      prev.stateCount = sqlite3_column_int64(stmt, 2);
      prev.stateMask  = sqlite3_column_int64(stmt, 3);
      prev.locked     = sqlite3_column_int64(stmt, 4);
      prev.cracked    = sqlite3_column_int64(stmt, 5);
      previousMobsi[reinterpret_cast<const char*>(rawKey)] = prev;
      }
    sqlite3_finalize(stmt);
    }
  const bool hadPreviousMobsi = !previousMobsi.empty();

  StoryProgressPrevious previousStoryProgress;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT chapter_number FROM runtime_story_progress_current WHERE character_key=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    if(sqlite3_step(stmt)==SQLITE_ROW) {
      previousStoryProgress.valid = true;
      previousStoryProgress.chapterNumber = sqlite3_column_int64(stmt, 0);
      }
    sqlite3_finalize(stmt);
    }

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
        ScriptGlobalValueRow value;
        value.valueIndex = int64_t(j);
        if(j!=0)
          row.valueText += "|";
        switch(type) {
          case zenkit::DaedalusDataType::INT:
            value.valueInt = sym->get_int(uint16_t(j));
            row.valueText += std::to_string(value.valueInt);
            break;
          case zenkit::DaedalusDataType::FLOAT:
            value.valueReal = sym->get_float(uint16_t(j));
            row.valueText += std::to_string(value.valueReal);
            break;
          case zenkit::DaedalusDataType::STRING:
            value.valueText = std::string(sym->get_string(uint16_t(j)));
            row.valueText += value.valueText;
            break;
          default:
            break;
          }
        row.values.emplace_back(std::move(value));
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

  GameScript* gameScript = game.script();
  const auto* currencySymbol = gameScript!=nullptr ? gameScript->goldId() : nullptr;
  const int64_t currencyTemplateSymbol = currencySymbol!=nullptr ? int64_t(currencySymbol->index()) : -1;
  const std::string currencyDisplayName = gameScript!=nullptr ?
    nonEmpty(gameScript->currencyName(), "Gold") : "Gold";
  const int64_t currencyAmount = int64_t(pl->inventory().goldCount());
  const char* walletUpsert = R"SQL(
    INSERT INTO runtime_character_wallet(
      character_key, currency_key, currency_display_name, item_template_symbol, amount, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, CURRENT_TIMESTAMP)
    ON CONFLICT(character_key, currency_key) DO UPDATE SET
      currency_display_name=excluded.currency_display_name,
      item_template_symbol=excluded.item_template_symbol,
      amount=excluded.amount,
      updated_at=CURRENT_TIMESTAMP
    WHERE runtime_character_wallet.currency_display_name!=excluded.currency_display_name OR
          runtime_character_wallet.item_template_symbol!=excluded.item_template_symbol OR
          runtime_character_wallet.amount!=excluded.amount
  )SQL";
  if(sqlite3_prepare_v2(impl->db, walletUpsert, -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    bindText(stmt, 2, "g2notr:gold");
    bindText(stmt, 3, currencyDisplayName);
    bindInt (stmt, 4, currencyTemplateSymbol);
    bindInt (stmt, 5, currencyAmount);
    if(sqlite3_step(stmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite wallet upsert failed: ", sqlite3_errmsg(impl->db));
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

  const char* invInsert = R"SQL(
    INSERT INTO runtime_character_inventory(
      character_key, item_key, symbol_index, display_name,
      amount, iterator_count, equipped, equip_count, slot,
      main_flag, item_flags, value, spell_id, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, CURRENT_TIMESTAMP)
    ON CONFLICT(character_key, item_key) DO UPDATE SET
      symbol_index=excluded.symbol_index,
      display_name=excluded.display_name,
      amount=excluded.amount,
      iterator_count=excluded.iterator_count,
      equipped=excluded.equipped,
      equip_count=excluded.equip_count,
      slot=excluded.slot,
      main_flag=excluded.main_flag,
      item_flags=excluded.item_flags,
      value=excluded.value,
      spell_id=excluded.spell_id,
      updated_at=CURRENT_TIMESTAMP
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
  sqlite3_stmt* invDeleteStmt = nullptr;
  const bool invPrepared = sqlite3_prepare_v2(impl->db, invInsert, -1, &invStmt, nullptr)==SQLITE_OK;
  const bool histPrepared = sqlite3_prepare_v2(impl->db, invHistory, -1, &histStmt, nullptr)==SQLITE_OK;
  if(!invPrepared)
    Tempest::Log::e("mmo sqlite inventory prepare failed: ", sqlite3_errmsg(impl->db));
  if(!histPrepared)
    Tempest::Log::e("mmo sqlite inventory history prepare failed: ", sqlite3_errmsg(impl->db));

  std::map<std::string, InventoryRow> previousInventoryRows;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT item_key,symbol_index,display_name,amount,iterator_count,equipped,equip_count,slot,main_flag,item_flags,value,spell_id "
                        "FROM runtime_character_inventory WHERE character_key=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, HeroKey);
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* rawKey = sqlite3_column_text(stmt, 0);
      if(rawKey==nullptr)
        continue;
      InventoryRow previous;
      previous.itemKey = reinterpret_cast<const char*>(rawKey);
      previous.symbolIndex = sqlite3_column_int64(stmt, 1);
      const auto* rawName = sqlite3_column_text(stmt, 2);
      previous.displayName = rawName!=nullptr ? reinterpret_cast<const char*>(rawName) : "";
      previous.amount = sqlite3_column_int64(stmt, 3);
      previous.iteratorCount = sqlite3_column_int64(stmt, 4);
      previous.equipped = sqlite3_column_int64(stmt, 5);
      previous.equipCount = sqlite3_column_int64(stmt, 6);
      previous.slot = sqlite3_column_int64(stmt, 7);
      previous.mainFlag = sqlite3_column_int64(stmt, 8);
      previous.itemFlags = sqlite3_column_int64(stmt, 9);
      previous.value = sqlite3_column_int64(stmt, 10);
      previous.spellId = sqlite3_column_int64(stmt, 11);
      previousInventoryRows.emplace(previous.itemKey, std::move(previous));
      }
    sqlite3_finalize(stmt);
    }
  if(sqlite3_prepare_v2(impl->db,
                        "DELETE FROM runtime_character_inventory WHERE character_key=?1 AND item_key=?2",
                        -1, &invDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite inventory cleanup prepare failed: ", sqlite3_errmsg(impl->db));

  auto sameInventoryRow = [](const InventoryRow& lhs, const InventoryRow& rhs) {
    return lhs.symbolIndex==rhs.symbolIndex && lhs.displayName==rhs.displayName &&
           lhs.amount==rhs.amount && lhs.iteratorCount==rhs.iteratorCount &&
           lhs.equipped==rhs.equipped && lhs.equipCount==rhs.equipCount &&
           lhs.slot==rhs.slot && lhs.mainFlag==rhs.mainFlag && lhs.itemFlags==rhs.itemFlags &&
           lhs.value==rhs.value && lhs.spellId==rhs.spellId;
    };

  std::set<std::string> currentInventoryKeys;
  for(const InventoryRow& row : currentRows) {
    currentInventoryKeys.insert(row.itemKey);
    const auto previous = previousInventoryRows.find(row.itemKey);
    const bool changed = previous==previousInventoryRows.end() || !sameInventoryRow(previous->second, row);
    if(changed && invStmt!=nullptr) {
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

    if(changed && histStmt!=nullptr) {
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
  for(const auto& previous : previousInventoryRows) {
    if(currentInventoryKeys.find(previous.first)!=currentInventoryKeys.end() || invDeleteStmt==nullptr)
      continue;
    sqlite3_reset(invDeleteStmt);
    sqlite3_clear_bindings(invDeleteStmt);
    bindText(invDeleteStmt, 1, HeroKey);
    bindText(invDeleteStmt, 2, previous.first);
    if(sqlite3_step(invDeleteStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite inventory cleanup failed: ", sqlite3_errmsg(impl->db));
    }
  if(invStmt!=nullptr)
    sqlite3_finalize(invStmt);
  if(histStmt!=nullptr)
    sqlite3_finalize(histStmt);
  if(invDeleteStmt!=nullptr)
    sqlite3_finalize(invDeleteStmt);

  const char* npcInventoryInsert = R"SQL(
    INSERT INTO runtime_world_npc_inventory(
      owner_key, item_key, world_name, owner_display_name, symbol_index, display_name,
      amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, CURRENT_TIMESTAMP)
    ON CONFLICT(owner_key, item_key) DO UPDATE SET
      world_name=excluded.world_name,
      owner_display_name=excluded.owner_display_name,
      symbol_index=excluded.symbol_index,
      display_name=excluded.display_name,
      amount=excluded.amount,
      iterator_count=excluded.iterator_count,
      equipped=excluded.equipped,
      equip_count=excluded.equip_count,
      slot=excluded.slot,
      main_flag=excluded.main_flag,
      item_flags=excluded.item_flags,
      value=excluded.value,
      spell_id=excluded.spell_id,
      updated_at=CURRENT_TIMESTAMP
    WHERE runtime_world_npc_inventory.world_name!=excluded.world_name OR
          runtime_world_npc_inventory.owner_display_name!=excluded.owner_display_name OR
          runtime_world_npc_inventory.symbol_index!=excluded.symbol_index OR
          runtime_world_npc_inventory.display_name!=excluded.display_name OR
          runtime_world_npc_inventory.amount!=excluded.amount OR
          runtime_world_npc_inventory.iterator_count!=excluded.iterator_count OR
          runtime_world_npc_inventory.equipped!=excluded.equipped OR
          runtime_world_npc_inventory.equip_count!=excluded.equip_count OR
          runtime_world_npc_inventory.slot!=excluded.slot OR
          runtime_world_npc_inventory.main_flag!=excluded.main_flag OR
          runtime_world_npc_inventory.item_flags!=excluded.item_flags OR
          runtime_world_npc_inventory.value!=excluded.value OR
          runtime_world_npc_inventory.spell_id!=excluded.spell_id
  )SQL";
  std::set<std::pair<std::string, std::string>> previousNpcInventoryKeys;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT owner_key,item_key FROM runtime_world_npc_inventory WHERE world_name=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, world);
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* owner = sqlite3_column_text(stmt, 0);
      const auto* item = sqlite3_column_text(stmt, 1);
      if(owner!=nullptr && item!=nullptr)
        previousNpcInventoryKeys.emplace(reinterpret_cast<const char*>(owner), reinterpret_cast<const char*>(item));
      }
    sqlite3_finalize(stmt);
    }
  sqlite3_stmt* npcInventoryStmt = nullptr;
  sqlite3_stmt* npcInventoryDeleteStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, npcInventoryInsert, -1, &npcInventoryStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite NPC inventory prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db,
                        "DELETE FROM runtime_world_npc_inventory WHERE owner_key=?1 AND item_key=?2",
                        -1, &npcInventoryDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite NPC inventory cleanup prepare failed: ", sqlite3_errmsg(impl->db));
  std::set<std::pair<std::string, std::string>> currentNpcInventoryKeys;
  if(auto* wrld = game.world()) {
    for(uint32_t i=0; i<wrld->npcCount(); ++i) {
      Npc* npc = wrld->npcById(i);
      if(npc==nullptr || npc->isPlayer() || npcInventoryStmt==nullptr)
        continue;
      const std::string ownerKey = npcEntityKey(game, *npc);
      const std::string ownerName = npcDisplayName(*npc);
      auto inventory = npc->inventory().iterator(Inventory::T_Inventory);
      for(; inventory.isValid(); ++inventory) {
        const Item& item = *inventory;
        const int64_t symbolIndex = int64_t(item.clsId());
        const std::string itemKey = std::to_string(symbolIndex) + ":" +
                                    std::to_string(uint32_t(inventory.slot())) + ":" +
                                    std::to_string(inventory.isEquipped() ? 1 : 0);
        currentNpcInventoryKeys.emplace(ownerKey, itemKey);
        sqlite3_reset(npcInventoryStmt);
        sqlite3_clear_bindings(npcInventoryStmt);
        bindText(npcInventoryStmt, 1, ownerKey);
        bindText(npcInventoryStmt, 2, itemKey);
        bindText(npcInventoryStmt, 3, world);
        bindText(npcInventoryStmt, 4, ownerName);
        bindInt (npcInventoryStmt, 5, symbolIndex);
        bindText(npcInventoryStmt, 6, nonEmpty(item.displayName(), "item:" + std::to_string(symbolIndex)));
        bindInt (npcInventoryStmt, 7, int64_t(item.count()));
        bindInt (npcInventoryStmt, 8, int64_t(inventory.count()));
        bindInt (npcInventoryStmt, 9, inventory.isEquipped() ? 1 : 0);
        bindInt (npcInventoryStmt,10, inventory.isEquipped() ? int64_t(item.equipCount()) : 0);
        bindInt (npcInventoryStmt,11, int64_t(inventory.slot()));
        bindInt (npcInventoryStmt,12, int64_t(item.mainFlag()));
        bindInt (npcInventoryStmt,13, int64_t(item.itemFlag()));
        bindInt (npcInventoryStmt,14, int64_t(item.cost()));
        bindInt (npcInventoryStmt,15, int64_t(item.spellId()));
        if(sqlite3_step(npcInventoryStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite NPC inventory insert failed: ", sqlite3_errmsg(impl->db));
        }
      }
    }
  for(const auto& previous : previousNpcInventoryKeys) {
    if(currentNpcInventoryKeys.find(previous)!=currentNpcInventoryKeys.end() || npcInventoryDeleteStmt==nullptr)
      continue;
    sqlite3_reset(npcInventoryDeleteStmt);
    sqlite3_clear_bindings(npcInventoryDeleteStmt);
    bindText(npcInventoryDeleteStmt, 1, previous.first);
    bindText(npcInventoryDeleteStmt, 2, previous.second);
    if(sqlite3_step(npcInventoryDeleteStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite NPC inventory cleanup failed: ", sqlite3_errmsg(impl->db));
    }
  if(npcInventoryStmt!=nullptr)
    sqlite3_finalize(npcInventoryStmt);
  if(npcInventoryDeleteStmt!=nullptr)
    sqlite3_finalize(npcInventoryDeleteStmt);

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
    WHERE runtime_world_npcs.world_name!=excluded.world_name OR
          runtime_world_npcs.slot_id!=excluded.slot_id OR
          runtime_world_npcs.persistent_id!=excluded.persistent_id OR
          runtime_world_npcs.symbol_index!=excluded.symbol_index OR
          runtime_world_npcs.script_id!=excluded.script_id OR
          runtime_world_npcs.display_name!=excluded.display_name OR
          runtime_world_npcs.pos_x!=excluded.pos_x OR
          runtime_world_npcs.pos_y!=excluded.pos_y OR
          runtime_world_npcs.pos_z!=excluded.pos_z OR
          runtime_world_npcs.rotation!=excluded.rotation OR
          runtime_world_npcs.guild!=excluded.guild OR
          runtime_world_npcs.true_guild!=excluded.true_guild OR
          runtime_world_npcs.hp!=excluded.hp OR
          runtime_world_npcs.hp_max!=excluded.hp_max OR
          runtime_world_npcs.mana!=excluded.mana OR
          runtime_world_npcs.mana_max!=excluded.mana_max OR
          runtime_world_npcs.level!=excluded.level OR
          runtime_world_npcs.experience!=excluded.experience OR
          runtime_world_npcs.dead!=excluded.dead OR
          runtime_world_npcs.player!=excluded.player OR
          runtime_world_npcs.waypoint!=excluded.waypoint
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
  sqlite3_stmt* npcDeleteStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, "DELETE FROM runtime_world_npcs WHERE entity_key=?1", -1, &npcDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite NPC cleanup prepare failed: ", sqlite3_errmsg(impl->db));

  std::set<std::string> currentNpcKeys;
  for(const NpcRow& row : currentNpcs) {
    currentNpcKeys.insert(row.entityKey);
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
  for(const auto& previous : previousNpcs) {
    if(currentNpcKeys.find(previous.first)!=currentNpcKeys.end() || npcDeleteStmt==nullptr)
      continue;
    sqlite3_reset(npcDeleteStmt);
    sqlite3_clear_bindings(npcDeleteStmt);
    bindText(npcDeleteStmt, 1, previous.first);
    if(sqlite3_step(npcDeleteStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite NPC cleanup failed: ", sqlite3_errmsg(impl->db));
    }
  if(npcStmt!=nullptr)
    sqlite3_finalize(npcStmt);
  if(npcHistStmt!=nullptr)
    sqlite3_finalize(npcHistStmt);
  if(npcDeleteStmt!=nullptr)
    sqlite3_finalize(npcDeleteStmt);

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

  auto writeNpcStat = [&](const NpcRow& row, std::string_view group, int64_t id, const std::string& key, int64_t value,
                          bool recordSpawn = true) {
    const std::string previousKey = npcStatKey(row.entityKey, group, id);
    const auto prev = previousNpcStats.find(previousKey);
    const bool changed = prev!=previousNpcStats.end() && prev->second.value!=value;
    const bool spawned = hadPreviousNpcStats && prev==previousNpcStats.end();

    // Static NPC attributes account for most rows. Avoid rewriting them every interval.
    if(prev!=previousNpcStats.end() && !changed)
      return;

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

    if((changed || (spawned && recordSpawn)) && npcStatHistStmt!=nullptr) {
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
    if(changedNpcStatEntities.find(row.entityKey)==changedNpcStatEntities.end())
      continue;
    for(int64_t a=0; a<ATR_MAX; ++a)
      writeNpcStat(row, "attribute", a, attributeKey(a), row.attributes[size_t(a)]);
    for(int64_t p=0; p<PROT_MAX; ++p)
      writeNpcStat(row, "protection", p, protectionKey(p), row.protections[size_t(p)]);
    for(int64_t t=0; t<TALENT_MAX_G2; ++t) {
      writeNpcStat(row, "talent_skill", t, talentKey(t), row.talentSkills[size_t(t)]);
      writeNpcStat(row, "talent_value", t, talentKey(t), row.talentValues[size_t(t)]);
      writeNpcStat(row, "hit_chance", t, talentKey(t), row.hitChances[size_t(t)]);
      }
    writeNpcStat(row, "progression", 0, "experience_next", row.experienceNext);
    writeNpcStat(row, "progression", 1, "learning_points", row.learningPoints);
    writeNpcStat(row, "attitude", 0, "permanent", row.permanentAttitude);
    writeNpcStat(row, "attitude", 1, "temporary", row.temporaryAttitude);
    for(size_t m=0; m<row.missions.size(); ++m)
      writeNpcStat(row, "mission", int64_t(m), "slot_" + std::to_string(m), row.missions[m], false);
    for(size_t a=0; a<row.aiVariables.size(); ++a)
      writeNpcStat(row, "aivar", int64_t(a), "aivar_" + std::to_string(a), row.aiVariables[a], false);
    }
  if(npcStatStmt!=nullptr)
    sqlite3_finalize(npcStatStmt);
  if(npcStatHistStmt!=nullptr)
    sqlite3_finalize(npcStatHistStmt);

  const char* npcStatCaptureUpsert = R"SQL(
    INSERT INTO runtime_npc_stat_capture_state(entity_key, world_name, stat_signature, updated_at)
    VALUES(?1, ?2, ?3, CURRENT_TIMESTAMP)
    ON CONFLICT(entity_key) DO UPDATE SET
      world_name=excluded.world_name,
      stat_signature=excluded.stat_signature,
      updated_at=CURRENT_TIMESTAMP
  )SQL";
  sqlite3_stmt* npcStatCaptureStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, npcStatCaptureUpsert, -1, &npcStatCaptureStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite NPC stat capture state prepare failed: ", sqlite3_errmsg(impl->db));
  for(const auto& current : currentNpcStatSignatures) {
    if(changedNpcStatEntities.find(current.first)==changedNpcStatEntities.end() || npcStatCaptureStmt==nullptr)
      continue;
    sqlite3_reset(npcStatCaptureStmt);
    sqlite3_clear_bindings(npcStatCaptureStmt);
    bindText(npcStatCaptureStmt, 1, current.first);
    bindText(npcStatCaptureStmt, 2, world);
    bindText(npcStatCaptureStmt, 3, current.second);
    if(sqlite3_step(npcStatCaptureStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite NPC stat capture state upsert failed: ", sqlite3_errmsg(impl->db));
    }
  if(npcStatCaptureStmt!=nullptr)
    sqlite3_finalize(npcStatCaptureStmt);

  sqlite3_stmt* staleNpcStatStmt = nullptr;
  sqlite3_stmt* staleNpcStatCaptureStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, "DELETE FROM runtime_npc_stats WHERE entity_key=?1", -1, &staleNpcStatStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite stale NPC stat cleanup prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, "DELETE FROM runtime_npc_stat_capture_state WHERE entity_key=?1", -1, &staleNpcStatCaptureStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite stale NPC stat capture cleanup prepare failed: ", sqlite3_errmsg(impl->db));
  for(const auto& previous : previousNpcStatSignatures) {
    if(currentNpcStatSignatures.find(previous.first)!=currentNpcStatSignatures.end())
      continue;
    if(staleNpcStatStmt!=nullptr) {
      sqlite3_reset(staleNpcStatStmt);
      sqlite3_clear_bindings(staleNpcStatStmt);
      bindText(staleNpcStatStmt, 1, previous.first);
      if(sqlite3_step(staleNpcStatStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite stale NPC stat cleanup failed: ", sqlite3_errmsg(impl->db));
      }
    if(staleNpcStatCaptureStmt!=nullptr) {
      sqlite3_reset(staleNpcStatCaptureStmt);
      sqlite3_clear_bindings(staleNpcStatCaptureStmt);
      bindText(staleNpcStatCaptureStmt, 1, previous.first);
      if(sqlite3_step(staleNpcStatCaptureStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite stale NPC stat capture cleanup failed: ", sqlite3_errmsg(impl->db));
      }
    }
  if(staleNpcStatStmt!=nullptr)
    sqlite3_finalize(staleNpcStatStmt);
  if(staleNpcStatCaptureStmt!=nullptr)
    sqlite3_finalize(staleNpcStatCaptureStmt);

  const char* npcAiInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_npc_ai_state(
      entity_key, world_name, tick_count, display_name, player,
      ai_state_function, ai_state_name, target_key, target_symbol_index, target_display_name,
      state_other_key, state_victim_key, relation_kind, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, CURRENT_TIMESTAMP)
  )SQL";
  const char* npcAiHistory = R"SQL(
    INSERT INTO runtime_npc_ai_history(
      entity_key, world_name, tick_count, display_name, player,
      ai_state_function, ai_state_name, target_key, target_symbol_index, target_display_name,
      state_other_key, state_victim_key, relation_kind, changed_fields
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)
  )SQL";
  sqlite3_stmt* npcAiStmt = nullptr;
  sqlite3_stmt* npcAiHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, npcAiInsert, -1, &npcAiStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc ai prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, npcAiHistory, -1, &npcAiHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc ai history prepare failed: ", sqlite3_errmsg(impl->db));
  sqlite3_stmt* npcAiDeleteStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, "DELETE FROM runtime_npc_ai_state WHERE entity_key=?1", -1, &npcAiDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite NPC AI cleanup prepare failed: ", sqlite3_errmsg(impl->db));
  std::set<std::string> currentNpcAiKeys;
  for(const NpcRow& row : currentNpcs) {
    currentNpcAiKeys.insert(row.entityKey);
    const auto prev = previousNpcAi.find(row.entityKey);
    const bool stateChanged = prev==previousNpcAi.end() ||
                              prev->second.stateName!=row.aiStateName ||
                              prev->second.targetKey!=row.targetKey ||
                              prev->second.stateOtherKey!=row.stateOtherKey ||
                              prev->second.stateVictimKey!=row.stateVictimKey ||
                              prev->second.relationKind!=row.relationKind;
    if(stateChanged && npcAiStmt!=nullptr) {
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
      bindText(npcAiStmt,11, row.stateOtherKey);
      bindText(npcAiStmt,12, row.stateVictimKey);
      bindText(npcAiStmt,13, row.relationKind);
      if(sqlite3_step(npcAiStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite npc ai insert failed: ", sqlite3_errmsg(impl->db));
      }

    std::string changed;
    if(prev==previousNpcAi.end()) {
      if(hadPreviousNpcAi && row.relationKind!="none")
        changed = "spawned";
      } else {
      if(prev->second.stateName!=row.aiStateName)
        changed += "ai_state,";
      if(prev->second.targetKey!=row.targetKey)
        changed += "target,";
      if(prev->second.stateOtherKey!=row.stateOtherKey)
        changed += "state_other,";
      if(prev->second.stateVictimKey!=row.stateVictimKey)
        changed += "state_victim,";
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
      bindText(npcAiHistStmt,11, row.stateOtherKey);
      bindText(npcAiHistStmt,12, row.stateVictimKey);
      bindText(npcAiHistStmt,13, row.relationKind);
      bindText(npcAiHistStmt,14, changed);
      if(sqlite3_step(npcAiHistStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite npc ai history insert failed: ", sqlite3_errmsg(impl->db));
      }
    }
  for(const auto& previous : previousNpcAi) {
    if(currentNpcAiKeys.find(previous.first)!=currentNpcAiKeys.end() || npcAiDeleteStmt==nullptr)
      continue;
    sqlite3_reset(npcAiDeleteStmt);
    sqlite3_clear_bindings(npcAiDeleteStmt);
    bindText(npcAiDeleteStmt, 1, previous.first);
    if(sqlite3_step(npcAiDeleteStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite NPC AI cleanup failed: ", sqlite3_errmsg(impl->db));
    }
  if(npcAiStmt!=nullptr)
    sqlite3_finalize(npcAiStmt);
  if(npcAiHistStmt!=nullptr)
    sqlite3_finalize(npcAiHistStmt);
  if(npcAiDeleteStmt!=nullptr)
    sqlite3_finalize(npcAiDeleteStmt);

  deleteWorldRows("DELETE FROM runtime_npc_relation_checkpoints WHERE world_name=?1");
  const char* relationCheckpointInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_npc_relation_checkpoints(
      entity_key, world_name, tick_count, display_name, target_key, other_key, victim_key,
      ai_state_function, ai_state_name, state_elapsed_millis, relation_kind, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, CURRENT_TIMESTAMP)
  )SQL";
  sqlite3_stmt* relationCheckpointStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, relationCheckpointInsert, -1, &relationCheckpointStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite relation checkpoint prepare failed: ", sqlite3_errmsg(impl->db));
  for(const NpcRow& row : currentNpcs) {
    const bool persistentRelation = row.relationKind=="following_target" || row.relationKind=="escort_or_guide";
    if(!persistentRelation || row.player!=0 || (row.targetKey.empty() && row.stateOtherKey.empty()))
      continue;
    if(relationCheckpointStmt==nullptr)
      continue;
    sqlite3_reset(relationCheckpointStmt);
    sqlite3_clear_bindings(relationCheckpointStmt);
    bindText(relationCheckpointStmt, 1, row.entityKey);
    bindText(relationCheckpointStmt, 2, world);
    bindInt (relationCheckpointStmt, 3, int64_t(game.tickCount()));
    bindText(relationCheckpointStmt, 4, row.displayName);
    bindText(relationCheckpointStmt, 5, row.targetKey);
    bindText(relationCheckpointStmt, 6, row.stateOtherKey);
    bindText(relationCheckpointStmt, 7, row.stateVictimKey);
    bindInt (relationCheckpointStmt, 8, row.aiStateFunction);
    bindText(relationCheckpointStmt, 9, row.aiStateName);
    bindInt (relationCheckpointStmt,10, row.aiStateElapsedMillis);
    bindText(relationCheckpointStmt,11, row.relationKind);
    if(sqlite3_step(relationCheckpointStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite relation checkpoint insert failed: ", sqlite3_errmsg(impl->db));
    }
  if(relationCheckpointStmt!=nullptr)
    sqlite3_finalize(relationCheckpointStmt);

  if(materializeCurrent)
    deleteWorldRows("DELETE FROM runtime_npc_routines WHERE world_name=?1");
  const char* npcRoutineInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_npc_routines(
      entity_key, routine_index, world_name, tick_count, display_name,
      start_minute, end_minute, callback_symbol_index, callback_symbol_name,
      waypoint_key, waypoint_name, active, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, CURRENT_TIMESTAMP)
  )SQL";
  sqlite3_stmt* npcRoutineStmt = nullptr;
  if(materializeCurrent && sqlite3_prepare_v2(impl->db, npcRoutineInsert, -1, &npcRoutineStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc routine prepare failed: ", sqlite3_errmsg(impl->db));

  if(auto* wrld = game.world()) {
    for(uint32_t i=0; i<wrld->npcCount(); ++i) {
      Npc* npc = wrld->npcById(i);
      if(npc==nullptr || npcRoutineStmt==nullptr)
        continue;
      const std::string entityKey = npcEntityKey(game, *npc);
      const std::string displayName = npcDisplayName(*npc);
      const auto routines = npc->routineSnapshot();
      for(size_t r=0; r<routines.size(); ++r) {
        const auto& routine = routines[r];
        const int64_t callbackIndex = routine.callback.isValid() ? int64_t(routine.callback.ptr) : -1;
        std::string callbackName = callbackIndex>=0 ? "function:" + std::to_string(callbackIndex) : "(no function)";
        if(callbackIndex>=0) {
          if(auto* script = game.script()) {
            if(auto* sym = script->findSymbol(size_t(callbackIndex)))
              callbackName = nonEmpty(sym->name(), callbackName);
            }
          }
        const std::string pointKey = waypointKeyFor(routine.point);
        const std::string pointName = nonEmpty(routine.waypoint, waypointName(routine.point));
        const int64_t active = routine.active ? 1 : 0;

        sqlite3_reset(npcRoutineStmt);
        sqlite3_clear_bindings(npcRoutineStmt);
        bindText(npcRoutineStmt, 1, entityKey);
        bindInt (npcRoutineStmt, 2, int64_t(r));
        bindText(npcRoutineStmt, 3, world);
        bindInt (npcRoutineStmt, 4, int64_t(game.tickCount()));
        bindText(npcRoutineStmt, 5, displayName);
        bindInt (npcRoutineStmt, 6, minuteOfDay(routine.start));
        bindInt (npcRoutineStmt, 7, minuteOfDay(routine.end));
        bindInt (npcRoutineStmt, 8, callbackIndex);
        bindText(npcRoutineStmt, 9, callbackName);
        bindText(npcRoutineStmt,10, pointKey);
        bindText(npcRoutineStmt,11, pointName);
        bindInt (npcRoutineStmt,12, active);
        if(sqlite3_step(npcRoutineStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite npc routine insert failed: ", sqlite3_errmsg(impl->db));
        }
      }
    }
  if(npcRoutineStmt!=nullptr)
    sqlite3_finalize(npcRoutineStmt);

  const char* npcNavigationInsert = R"SQL(
    INSERT OR REPLACE INTO runtime_npc_navigation_state(
      entity_key, world_name, tick_count, display_name,
      current_waypoint_key, current_waypoint_name,
      routine_waypoint_key, routine_waypoint_name,
      move_hint, move_target_waypoint_key, move_target_waypoint_name,
      path_next_waypoint_key, path_next_waypoint_name,
      path_final_waypoint_key, path_final_waypoint_name,
      path_remaining_count, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, CURRENT_TIMESTAMP)
  )SQL";
  const char* npcNavigationHistory = R"SQL(
    INSERT INTO runtime_npc_navigation_history(
      entity_key, world_name, tick_count, display_name,
      current_waypoint_name, routine_waypoint_name, move_hint,
      move_target_waypoint_name, path_next_waypoint_name, path_remaining_count, changed_fields
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)
  )SQL";
  sqlite3_stmt* npcNavigationStmt = nullptr;
  sqlite3_stmt* npcNavigationHistStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, npcNavigationInsert, -1, &npcNavigationStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc navigation prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, npcNavigationHistory, -1, &npcNavigationHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite npc navigation history prepare failed: ", sqlite3_errmsg(impl->db));
  sqlite3_stmt* npcNavigationDeleteStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, "DELETE FROM runtime_npc_navigation_state WHERE entity_key=?1", -1, &npcNavigationDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite NPC navigation cleanup prepare failed: ", sqlite3_errmsg(impl->db));

  std::set<std::string> currentNavigationKeys;
  if(auto* wrld = game.world()) {
    for(uint32_t i=0; i<wrld->npcCount(); ++i) {
      Npc* npc = wrld->npcById(i);
      if(npc==nullptr)
        continue;
      const std::string entityKey = npcEntityKey(game, *npc);
      const std::string displayName = npcDisplayName(*npc);
      const WayPoint* currentPoint = npc->currentWayPoint();
      const WayPoint* routinePoint = npc->currentTaPoint();
      const WayPoint* moveTargetPoint = npc->moveTargetWayPoint();
      const WayPoint* pathNextPoint = npc->nextPathWayPoint();
      const WayPoint* pathFinalPoint = npc->finalPathWayPoint();
      const std::string currentKey = waypointKeyFor(currentPoint);
      const std::string routineKey = waypointKeyFor(routinePoint);
      const std::string moveTargetKey = waypointKeyFor(moveTargetPoint);
      const std::string pathNextKey = waypointKeyFor(pathNextPoint);
      const std::string pathFinalKey = waypointKeyFor(pathFinalPoint);
      const std::string currentName = waypointName(currentPoint);
      const std::string routineName = waypointName(routinePoint);
      const std::string moveTargetName = waypointName(moveTargetPoint);
      const std::string pathNextName = waypointName(pathNextPoint);
      const std::string pathFinalName = waypointName(pathFinalPoint);
      const std::string moveHint = moveHintName(npc->moveHint());
      const int64_t pathRemaining = int64_t(npc->remainingPathPointCount());

      currentNavigationKeys.insert(entityKey);
      const auto prev = previousNavigation.find(entityKey);
      const bool stateChanged = prev==previousNavigation.end() ||
                                prev->second.currentWaypointKey!=currentKey ||
                                prev->second.routineWaypointKey!=routineKey ||
                                prev->second.moveHint!=moveHint ||
                                prev->second.moveTargetWaypointKey!=moveTargetKey ||
                                prev->second.pathNextWaypointKey!=pathNextKey ||
                                prev->second.pathFinalWaypointKey!=pathFinalKey ||
                                prev->second.pathRemainingCount!=pathRemaining;
      if(stateChanged && npcNavigationStmt!=nullptr) {
        sqlite3_reset(npcNavigationStmt);
        sqlite3_clear_bindings(npcNavigationStmt);
        bindText(npcNavigationStmt, 1, entityKey);
        bindText(npcNavigationStmt, 2, world);
        bindInt (npcNavigationStmt, 3, int64_t(game.tickCount()));
        bindText(npcNavigationStmt, 4, displayName);
        bindText(npcNavigationStmt, 5, currentKey);
        bindText(npcNavigationStmt, 6, currentName);
        bindText(npcNavigationStmt, 7, routineKey);
        bindText(npcNavigationStmt, 8, routineName);
        bindText(npcNavigationStmt, 9, moveHint);
        bindText(npcNavigationStmt,10, moveTargetKey);
        bindText(npcNavigationStmt,11, moveTargetName);
        bindText(npcNavigationStmt,12, pathNextKey);
        bindText(npcNavigationStmt,13, pathNextName);
        bindText(npcNavigationStmt,14, pathFinalKey);
        bindText(npcNavigationStmt,15, pathFinalName);
        bindInt (npcNavigationStmt,16, pathRemaining);
        if(sqlite3_step(npcNavigationStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite npc navigation insert failed: ", sqlite3_errmsg(impl->db));
        }

      std::string changed;
      if(prev==previousNavigation.end()) {
        if(hadPreviousNavigation && (!currentKey.empty() || !routineKey.empty() || !moveTargetKey.empty()))
          changed = "spawned";
        } else {
        if(prev->second.currentWaypointKey!=currentKey)
          changed += "current_waypoint,";
        if(prev->second.routineWaypointKey!=routineKey)
          changed += "routine_waypoint,";
        if(prev->second.moveHint!=moveHint)
          changed += "move_hint,";
        if(prev->second.moveTargetWaypointKey!=moveTargetKey)
          changed += "move_target,";
        if(prev->second.pathNextWaypointKey!=pathNextKey)
          changed += "path_next,";
        if(prev->second.pathFinalWaypointKey!=pathFinalKey)
          changed += "path_final,";
        if(prev->second.pathRemainingCount!=pathRemaining)
          changed += "path_remaining,";
        if(!changed.empty() && changed.back()==',')
          changed.pop_back();
        }
      if(!changed.empty() && npcNavigationHistStmt!=nullptr) {
        sqlite3_reset(npcNavigationHistStmt);
        sqlite3_clear_bindings(npcNavigationHistStmt);
        bindText(npcNavigationHistStmt, 1, entityKey);
        bindText(npcNavigationHistStmt, 2, world);
        bindInt (npcNavigationHistStmt, 3, int64_t(game.tickCount()));
        bindText(npcNavigationHistStmt, 4, displayName);
        bindText(npcNavigationHistStmt, 5, currentName);
        bindText(npcNavigationHistStmt, 6, routineName);
        bindText(npcNavigationHistStmt, 7, moveHint);
        bindText(npcNavigationHistStmt, 8, moveTargetName);
        bindText(npcNavigationHistStmt, 9, pathNextName);
        bindInt (npcNavigationHistStmt,10, pathRemaining);
        bindText(npcNavigationHistStmt,11, changed);
        if(sqlite3_step(npcNavigationHistStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite npc navigation history insert failed: ", sqlite3_errmsg(impl->db));
        }
      }
    }
  for(const auto& previous : previousNavigation) {
    if(currentNavigationKeys.find(previous.first)!=currentNavigationKeys.end() || npcNavigationDeleteStmt==nullptr)
      continue;
    sqlite3_reset(npcNavigationDeleteStmt);
    sqlite3_clear_bindings(npcNavigationDeleteStmt);
    bindText(npcNavigationDeleteStmt, 1, previous.first);
    if(sqlite3_step(npcNavigationDeleteStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite NPC navigation cleanup failed: ", sqlite3_errmsg(impl->db));
    }
  if(npcNavigationStmt!=nullptr)
    sqlite3_finalize(npcNavigationStmt);
  if(npcNavigationHistStmt!=nullptr)
    sqlite3_finalize(npcNavigationHistStmt);
  if(npcNavigationDeleteStmt!=nullptr)
    sqlite3_finalize(npcNavigationDeleteStmt);

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
  sqlite3_stmt* worldItemDeleteStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, "DELETE FROM runtime_world_items WHERE entity_key=?1", -1, &worldItemDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite world item cleanup prepare failed: ", sqlite3_errmsg(impl->db));
  for(const WorldItemRow& row : currentWorldItems) {
    const auto prev = previousWorldItems.find(row.entityKey);
    const bool stateChanged = prev==previousWorldItems.end() ||
                              prev->second.posX!=row.posX ||
                              prev->second.posY!=row.posY ||
                              prev->second.posZ!=row.posZ ||
                              prev->second.amount!=row.amount;
    if(stateChanged && worldItemStmt!=nullptr) {
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
  for(const auto& previous : previousWorldItems) {
    if(currentWorldItemKeys.find(previous.first)!=currentWorldItemKeys.end() || worldItemDeleteStmt==nullptr)
      continue;
    sqlite3_reset(worldItemDeleteStmt);
    sqlite3_clear_bindings(worldItemDeleteStmt);
    bindText(worldItemDeleteStmt, 1, previous.first);
    if(sqlite3_step(worldItemDeleteStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite world item cleanup failed: ", sqlite3_errmsg(impl->db));
    }
  if(worldItemStmt!=nullptr)
    sqlite3_finalize(worldItemStmt);
  if(worldItemHistStmt!=nullptr)
    sqlite3_finalize(worldItemHistStmt);
  if(worldItemDeleteStmt!=nullptr)
    sqlite3_finalize(worldItemDeleteStmt);

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
  sqlite3_stmt* mobsiDeleteStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, "DELETE FROM runtime_world_mobsi WHERE entity_key=?1", -1, &mobsiDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite mobsi cleanup prepare failed: ", sqlite3_errmsg(impl->db));

  const char* mobsiInvInsert = R"SQL(
    INSERT INTO runtime_world_mobsi_inventory(
      owner_key, item_key, world_name, owner_display_name, symbol_index, display_name, amount, iterator_count, value, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, CURRENT_TIMESTAMP)
    ON CONFLICT(owner_key, item_key) DO UPDATE SET
      world_name=excluded.world_name,
      owner_display_name=excluded.owner_display_name,
      symbol_index=excluded.symbol_index,
      display_name=excluded.display_name,
      amount=excluded.amount,
      iterator_count=excluded.iterator_count,
      value=excluded.value,
      updated_at=CURRENT_TIMESTAMP
    WHERE runtime_world_mobsi_inventory.world_name!=excluded.world_name OR
          runtime_world_mobsi_inventory.owner_display_name!=excluded.owner_display_name OR
          runtime_world_mobsi_inventory.symbol_index!=excluded.symbol_index OR
          runtime_world_mobsi_inventory.display_name!=excluded.display_name OR
          runtime_world_mobsi_inventory.amount!=excluded.amount OR
          runtime_world_mobsi_inventory.iterator_count!=excluded.iterator_count OR
          runtime_world_mobsi_inventory.value!=excluded.value
  )SQL";
  std::set<std::pair<std::string, std::string>> previousMobsiInventoryKeys;
  if(sqlite3_prepare_v2(impl->db,
                        "SELECT owner_key,item_key FROM runtime_world_mobsi_inventory WHERE world_name=?1",
                        -1, &stmt, nullptr)==SQLITE_OK) {
    bindText(stmt, 1, world);
    while(sqlite3_step(stmt)==SQLITE_ROW) {
      const auto* owner = sqlite3_column_text(stmt, 0);
      const auto* item = sqlite3_column_text(stmt, 1);
      if(owner!=nullptr && item!=nullptr)
        previousMobsiInventoryKeys.emplace(reinterpret_cast<const char*>(owner), reinterpret_cast<const char*>(item));
      }
    sqlite3_finalize(stmt);
    }
  sqlite3_stmt* mobsiInvStmt = nullptr;
  sqlite3_stmt* mobsiInvDeleteStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, mobsiInvInsert, -1, &mobsiInvStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite mobsi inventory prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db,
                        "DELETE FROM runtime_world_mobsi_inventory WHERE owner_key=?1 AND item_key=?2",
                        -1, &mobsiInvDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite mobsi inventory cleanup prepare failed: ", sqlite3_errmsg(impl->db));

  std::set<std::string> currentMobsiKeys;
  for(const MobsiRow& row : currentMobsi) {
    currentMobsiKeys.insert(row.entityKey);
    const auto prev = previousMobsi.find(row.entityKey);
    const bool stateChanged = prev==previousMobsi.end() ||
                              prev->second.state!=row.state ||
                              prev->second.stateCount!=row.stateCount ||
                              prev->second.stateMask!=row.stateMask ||
                              prev->second.locked!=row.locked ||
                              prev->second.cracked!=row.cracked;
    if(stateChanged && mobsiStmt!=nullptr) {
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

  for(const auto& previous : previousMobsi) {
    if(currentMobsiKeys.find(previous.first)!=currentMobsiKeys.end() || mobsiDeleteStmt==nullptr)
      continue;
    sqlite3_reset(mobsiDeleteStmt);
    sqlite3_clear_bindings(mobsiDeleteStmt);
    bindText(mobsiDeleteStmt, 1, previous.first);
    if(sqlite3_step(mobsiDeleteStmt)!=SQLITE_DONE)
      Tempest::Log::e("mmo sqlite mobsi cleanup failed: ", sqlite3_errmsg(impl->db));
    }

  if(auto* wrld = game.world()) {
    std::set<std::pair<std::string, std::string>> currentMobsiInventoryKeys;
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
        currentMobsiInventoryKeys.emplace(currentMobsi[i].entityKey, itemKey);
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
    for(const auto& previous : previousMobsiInventoryKeys) {
      if(currentMobsiInventoryKeys.find(previous)!=currentMobsiInventoryKeys.end() || mobsiInvDeleteStmt==nullptr)
        continue;
      sqlite3_reset(mobsiInvDeleteStmt);
      sqlite3_clear_bindings(mobsiInvDeleteStmt);
      bindText(mobsiInvDeleteStmt, 1, previous.first);
      bindText(mobsiInvDeleteStmt, 2, previous.second);
      if(sqlite3_step(mobsiInvDeleteStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite mobsi inventory cleanup failed: ", sqlite3_errmsg(impl->db));
      }
    }
  if(mobsiStmt!=nullptr)
    sqlite3_finalize(mobsiStmt);
  if(mobsiHistStmt!=nullptr)
    sqlite3_finalize(mobsiHistStmt);
  if(mobsiDeleteStmt!=nullptr)
    sqlite3_finalize(mobsiDeleteStmt);
  if(mobsiInvStmt!=nullptr)
    sqlite3_finalize(mobsiInvStmt);
  if(mobsiInvDeleteStmt!=nullptr)
    sqlite3_finalize(mobsiInvDeleteStmt);

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
  const char* globalValueInsert = R"SQL(
    INSERT INTO runtime_script_global_values(
      global_key, value_index, value_int, value_real, value_text, updated_at
    )
    VALUES(?1, ?2, ?3, ?4, ?5, CURRENT_TIMESTAMP)
  )SQL";
  sqlite3_stmt* globalStmt = nullptr;
  sqlite3_stmt* globalHistStmt = nullptr;
  sqlite3_stmt* globalValueStmt = nullptr;
  sqlite3_stmt* globalValueDeleteStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, globalUpsert, -1, &globalStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite script global prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, globalHistory, -1, &globalHistStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite script global history prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, globalValueInsert, -1, &globalValueStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite script global values prepare failed: ", sqlite3_errmsg(impl->db));
  if(sqlite3_prepare_v2(impl->db, "DELETE FROM runtime_script_global_values WHERE global_key=?1", -1, &globalValueDeleteStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite script global value cleanup prepare failed: ", sqlite3_errmsg(impl->db));
  for(const ScriptGlobalRow& row : currentGlobals) {
    const auto prev = previousGlobals.find(row.globalKey);
    const bool valueChanged = prev==previousGlobals.end() || prev->second.valueText!=row.valueText;
    if(valueChanged && globalStmt!=nullptr) {
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

    if(valueChanged) {
      if(globalValueDeleteStmt!=nullptr) {
        sqlite3_reset(globalValueDeleteStmt);
        sqlite3_clear_bindings(globalValueDeleteStmt);
        bindText(globalValueDeleteStmt, 1, row.globalKey);
        if(sqlite3_step(globalValueDeleteStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite script global value cleanup failed: ", sqlite3_errmsg(impl->db));
        }
      for(const auto& value : row.values) {
        if(globalValueStmt==nullptr)
          break;
        sqlite3_reset(globalValueStmt);
        sqlite3_clear_bindings(globalValueStmt);
        bindText(globalValueStmt, 1, row.globalKey);
        bindInt (globalValueStmt, 2, value.valueIndex);
        if(row.valueType=="int")
          bindInt(globalValueStmt, 3, value.valueInt);
        else
          sqlite3_bind_null(globalValueStmt, 3);
        if(row.valueType=="float")
          bindReal(globalValueStmt, 4, value.valueReal);
        else
          sqlite3_bind_null(globalValueStmt, 4);
        if(row.valueType=="string")
          bindText(globalValueStmt, 5, value.valueText);
        else
          sqlite3_bind_null(globalValueStmt, 5);
        if(sqlite3_step(globalValueStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite script global value insert failed: ", sqlite3_errmsg(impl->db));
        }
      }

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
  if(globalValueStmt!=nullptr)
    sqlite3_finalize(globalValueStmt);
  if(globalValueDeleteStmt!=nullptr)
    sqlite3_finalize(globalValueDeleteStmt);

  const ScriptGlobalRow* chapterGlobal = nullptr;
  for(const ScriptGlobalRow& row : currentGlobals) {
    if(row.symbolName=="KAPITEL" && row.valueType=="int") {
      chapterGlobal = &row;
      break;
      }
    }
  if(chapterGlobal!=nullptr) {
    const int64_t chapterNumber = parseInt64(chapterGlobal->valueText, 0);
    const std::string chapterKey = "chapter:" + std::to_string(chapterNumber);

    sqlite3_stmt* storyStmt = nullptr;
    const char* storyUpsert = R"SQL(
      INSERT INTO runtime_story_progress_current(
        character_key, world_name, tick_count, chapter_number, chapter_key,
        source_global_key, source_symbol_index, source_symbol_name, updated_at
      )
      VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, CURRENT_TIMESTAMP)
      ON CONFLICT(character_key) DO UPDATE SET
        world_name=excluded.world_name,
        tick_count=excluded.tick_count,
        chapter_number=excluded.chapter_number,
        chapter_key=excluded.chapter_key,
        source_global_key=excluded.source_global_key,
        source_symbol_index=excluded.source_symbol_index,
        source_symbol_name=excluded.source_symbol_name,
        updated_at=CURRENT_TIMESTAMP
    )SQL";
    if(sqlite3_prepare_v2(impl->db, storyUpsert, -1, &storyStmt, nullptr)==SQLITE_OK) {
      bindText(storyStmt, 1, HeroKey);
      bindText(storyStmt, 2, world);
      bindInt (storyStmt, 3, int64_t(game.tickCount()));
      bindInt (storyStmt, 4, chapterNumber);
      bindText(storyStmt, 5, chapterKey);
      bindText(storyStmt, 6, chapterGlobal->globalKey);
      bindInt (storyStmt, 7, chapterGlobal->symbolIndex);
      bindText(storyStmt, 8, chapterGlobal->symbolName);
      if(sqlite3_step(storyStmt)!=SQLITE_DONE)
        Tempest::Log::e("mmo sqlite story progress upsert failed: ", sqlite3_errmsg(impl->db));
      sqlite3_finalize(storyStmt);
      }
    else {
      Tempest::Log::e("mmo sqlite story progress prepare failed: ", sqlite3_errmsg(impl->db));
      }

    if(previousStoryProgress.valid && previousStoryProgress.chapterNumber!=chapterNumber) {
      const char* storyHistory = R"SQL(
        INSERT INTO runtime_story_progress_history(
          character_key, world_name, tick_count, chapter_before, chapter_after, chapter_key,
          source_global_key, source_symbol_index, source_symbol_name
        )
        VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)
      )SQL";
      if(sqlite3_prepare_v2(impl->db, storyHistory, -1, &storyStmt, nullptr)==SQLITE_OK) {
        bindText(storyStmt, 1, HeroKey);
        bindText(storyStmt, 2, world);
        bindInt (storyStmt, 3, int64_t(game.tickCount()));
        bindInt (storyStmt, 4, previousStoryProgress.chapterNumber);
        bindInt (storyStmt, 5, chapterNumber);
        bindText(storyStmt, 6, chapterKey);
        bindText(storyStmt, 7, chapterGlobal->globalKey);
        bindInt (storyStmt, 8, chapterGlobal->symbolIndex);
        bindText(storyStmt, 9, chapterGlobal->symbolName);
        if(sqlite3_step(storyStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite story progress history insert failed: ", sqlite3_errmsg(impl->db));
        sqlite3_finalize(storyStmt);
        }
      else {
        Tempest::Log::e("mmo sqlite story progress history prepare failed: ", sqlite3_errmsg(impl->db));
        }
      insertEvent("story_chapter_changed", HeroKey, chapterGlobal->globalKey,
                  double(previousStoryProgress.chapterNumber), double(chapterNumber), chapterKey);
      }
    }

  const char* guildAttitudeInsert = R"SQL(
    INSERT INTO runtime_guild_attitudes(from_guild, to_guild, attitude, updated_at)
    VALUES(?1, ?2, ?3, CURRENT_TIMESTAMP)
    ON CONFLICT(from_guild, to_guild) DO UPDATE SET
      attitude=excluded.attitude,
      updated_at=CURRENT_TIMESTAMP
    WHERE runtime_guild_attitudes.attitude!=excluded.attitude
  )SQL";
  sqlite3_stmt* guildAttitudeStmt = nullptr;
  if(sqlite3_prepare_v2(impl->db, guildAttitudeInsert, -1, &guildAttitudeStmt, nullptr)!=SQLITE_OK)
    Tempest::Log::e("mmo sqlite guild attitude prepare failed: ", sqlite3_errmsg(impl->db));
  if(auto* script = game.script()) {
    const size_t guildCount = script->guildCountForPersistence();
    for(size_t from=0; from<guildCount; ++from) {
      for(size_t to=0; to<guildCount; ++to) {
        if(guildAttitudeStmt==nullptr)
          break;
        sqlite3_reset(guildAttitudeStmt);
        sqlite3_clear_bindings(guildAttitudeStmt);
        bindInt(guildAttitudeStmt, 1, int64_t(from));
        bindInt(guildAttitudeStmt, 2, int64_t(to));
        bindInt(guildAttitudeStmt, 3, script->guildAttitudeForPersistence(from, to));
        if(sqlite3_step(guildAttitudeStmt)!=SQLITE_DONE)
          Tempest::Log::e("mmo sqlite guild attitude insert failed: ", sqlite3_errmsg(impl->db));
        }
      }
    }
  if(guildAttitudeStmt!=nullptr)
    sqlite3_finalize(guildAttitudeStmt);

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

    if(materializeCurrent) {
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
    }

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

  // The canonical MMO projection joins and copies the entire world state. It is
  // intentionally kept out of the frame-time flush path and persisted on a
  // controlled full flush (startup/shutdown).
  if(!materializeCurrent) {
    exec(impl->db, "COMMIT");
    return;
    }

  deleteWorldRows("DELETE FROM mmo_unit_stat_current WHERE world_name=?1");
  deleteWorldRows("DELETE FROM mmo_unit_stat_sheet_current WHERE world_name=?1");
  deleteWorldRows("DELETE FROM mmo_creature_templates_current WHERE world_name=?1");
  deleteWorldRows("DELETE FROM mmo_creature_spawns_current WHERE world_name=?1");
  deleteWorldRows("DELETE FROM mmo_creature_inventory_current WHERE world_name=?1");
  deleteWorldRows("DELETE FROM mmo_creature_inventory_snapshots_current WHERE world_name=?1");
  deleteWorldRows("DELETE FROM mmo_creature_relations_current WHERE world_name=?1");
  deleteWorldRows("DELETE FROM mmo_world_interactives_current WHERE world_name=?1");
  deleteWorldRows("DELETE FROM mmo_world_container_inventory_current WHERE world_name=?1");
  exec(impl->db, "DELETE FROM mmo_characters_current WHERE character_key='PC_HERO'");
  exec(impl->db, "DELETE FROM mmo_character_inventory_current WHERE character_key='PC_HERO'");
  exec(impl->db, "DELETE FROM mmo_character_wallet_current WHERE character_key='PC_HERO'");
  exec(impl->db, "DELETE FROM mmo_character_quests_current WHERE character_key='PC_HERO'");
  exec(impl->db, "DELETE FROM mmo_character_known_dialogs_current WHERE character_key='PC_HERO'");
  exec(impl->db, "DELETE FROM mmo_character_story_progress_current WHERE character_key='PC_HERO'");
  exec(impl->db, "DELETE FROM mmo_script_global_values_current");
  exec(impl->db, "DELETE FROM mmo_script_globals_current");
  exec(impl->db, "DELETE FROM mmo_guild_attitudes_current WHERE realm_key='local-g2notr'");
  deleteWorldRows("UPDATE mmo_world_items_current SET exists_in_world=0, updated_at=CURRENT_TIMESTAMP WHERE world_name=?1");
  const char* materializeMmoCurrent = R"SQL(
    INSERT OR REPLACE INTO mmo_unit_stat_current(
      unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id,
      display_name, player, stat_domain, stat_family, stat_group, stat_id, stat_key,
      value_kind, persistence_hint, display_order, value, updated_at, persistence_class
    )
    SELECT unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id,
           display_name, player, stat_domain, stat_family, stat_group, stat_id, stat_key,
           value_kind, persistence_hint, display_order, value, updated_at, persistence_class
      FROM v_mmo_unit_stats
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR REPLACE INTO mmo_unit_stat_sheet_current(
      unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id,
      display_name, player, guild, true_guild, level, experience,
      experience_next, learning_points, permanent_attitude, temporary_attitude, dead,
      pos_x, pos_y, pos_z, rotation, waypoint,
      health_current, health_max, mana_current, mana_max, strength, dexterity,
      regenerate_hp, regenerate_mana,
      resist_barrier, resist_blunt, resist_edge, resist_fire, resist_fly, resist_magic, resist_point, resist_fall,
      one_handed_skill, two_handed_skill, bow_skill, crossbow_skill,
      one_handed_hit_chance, two_handed_hit_chance, bow_hit_chance, crossbow_hit_chance,
      picklock_skill, sneak_skill, pickpocket_skill, smith_skill, alchemy_skill, take_animal_trophy_skill,
      foreign_language_skill, acrobat_skill, mage_skill, runes_skill, firemaster_skill, regenerate_skill,
      wisp_detector_skill, updated_at, persistence_class
    )
    SELECT unit_key, unit_type, character_key, world_name, tick_count, template_symbol_index, script_id,
           display_name, player, guild, true_guild, level, experience,
           experience_next, learning_points, permanent_attitude, temporary_attitude, dead,
           pos_x, pos_y, pos_z, rotation, waypoint,
           health_current, health_max, mana_current, mana_max, strength, dexterity,
           regenerate_hp, regenerate_mana,
           resist_barrier, resist_blunt, resist_edge, resist_fire, resist_fly, resist_magic, resist_point, resist_fall,
           one_handed_skill, two_handed_skill, bow_skill, crossbow_skill,
           one_handed_hit_chance, two_handed_hit_chance, bow_hit_chance, crossbow_hit_chance,
           picklock_skill, sneak_skill, pickpocket_skill, smith_skill, alchemy_skill, take_animal_trophy_skill,
           foreign_language_skill, acrobat_skill, mage_skill, runes_skill, firemaster_skill, regenerate_skill,
           wisp_detector_skill, updated_at, persistence_class
      FROM v_mmo_unit_stat_sheet
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR REPLACE INTO mmo_world_clock_current(
      world_name, tick_count, world_time_millis, world_day, world_hour, world_minute, updated_at, persistence_class
    )
    SELECT world_name, tick_count, world_time_millis, world_day, world_hour, world_minute, updated_at,
           'world_clock_current'
      FROM runtime_world_clock;
    INSERT OR REPLACE INTO mmo_creature_templates_current(
      world_name, creature_template_key, creature_template_id, script_id, display_name, guild, true_guild,
      spawn_count, min_level, max_level, base_health_max, base_mana_max, base_strength, base_dexterity,
      resist_blunt, resist_edge, resist_fire, resist_magic, updated_at, persistence_class
    )
    SELECT world_name, creature_template_key, creature_template_id, script_id, display_name, guild, true_guild,
           spawn_count, min_level, max_level, base_health_max, base_mana_max, base_strength, base_dexterity,
           resist_blunt, resist_edge, resist_fire, resist_magic, updated_at, persistence_class
      FROM v_mmo_creature_templates
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR REPLACE INTO mmo_creature_spawns_current(
      creature_spawn_key, creature_template_id, world_name, tick_count, display_name,
      pos_x, pos_y, pos_z, rotation, waypoint, dead, level, experience,
      health_current, health_max, mana_current, mana_max, strength, dexterity,
      current_waypoint_name, routine_waypoint_name, move_hint, move_target_waypoint_name,
      updated_at, persistence_class
    )
    SELECT creature_spawn_key, creature_template_id, world_name, tick_count, display_name,
           pos_x, pos_y, pos_z, rotation, waypoint, dead, level, experience,
           health_current, health_max, mana_current, mana_max, strength, dexterity,
           current_waypoint_name, routine_waypoint_name, move_hint, move_target_waypoint_name,
           updated_at, persistence_class
      FROM v_mmo_creature_spawns
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR REPLACE INTO mmo_creature_inventory_current(
      creature_spawn_key, item_instance_key, world_name, item_template_symbol, item_display_name,
      amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id,
      updated_at, persistence_class
    )
    SELECT owner_key, item_key, world_name, symbol_index, display_name,
           amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id,
           updated_at, 'world_creature_inventory_current'
      FROM runtime_world_npc_inventory
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR REPLACE INTO mmo_creature_inventory_snapshots_current(
      creature_spawn_key, world_name, tick_count, item_row_count, updated_at, persistence_class
    )
    SELECT n.entity_key, n.world_name, n.tick_count, COUNT(i.item_key), n.updated_at,
           'world_creature_inventory_snapshot'
      FROM runtime_world_npcs n
      LEFT JOIN runtime_world_npc_inventory i ON i.owner_key=n.entity_key
     WHERE n.player=0
       AND n.world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
     GROUP BY n.entity_key;
    INSERT OR REPLACE INTO mmo_creature_relations_current(
      creature_spawn_key, world_name, tick_count, relation_kind, target_key, other_key, victim_key,
      ai_state_function, ai_state_name, state_elapsed_millis, updated_at, persistence_class
    )
    SELECT entity_key, world_name, tick_count, relation_kind, target_key, other_key, victim_key,
           ai_state_function, ai_state_name, state_elapsed_millis, updated_at,
           'world_creature_relation_checkpoint'
      FROM runtime_npc_relation_checkpoints
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR REPLACE INTO mmo_characters_current(
      character_key, account_key, realm_key, world_name, tick_count, display_name,
      pos_x, pos_y, pos_z, rotation, health_current, health_max, mana_current, mana_max,
      level, experience, updated_at, persistence_class
    )
    SELECT character_key, account_key, realm_key, world_name, tick_count, display_name,
           pos_x, pos_y, pos_z, rotation, hp, hp_max, mana, mana_max,
           level, experience, updated_at, persistence_class
      FROM v_mmo_character_current;
    INSERT OR REPLACE INTO mmo_character_inventory_current(
      character_key, item_instance_key, item_template_symbol, item_display_name,
      amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id,
      updated_at, persistence_class
    )
    SELECT character_key, item_instance_key, item_template_symbol, item_display_name,
           amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id,
           updated_at, persistence_class
      FROM v_mmo_character_inventory;
    INSERT OR REPLACE INTO mmo_character_wallet_current(
      character_key, currency_key, currency_display_name, item_template_symbol, amount, updated_at, persistence_class
    )
    SELECT character_key, currency_key, currency_display_name, item_template_symbol, amount, updated_at,
           'character_wallet_current'
      FROM runtime_character_wallet;
    INSERT OR REPLACE INTO mmo_character_quests_current(
      character_key, quest_key, quest_name, section, status, entry_count, entries_text, updated_at, persistence_class
    )
    SELECT character_key, quest_key, name, section, status, entry_count, entries_text, updated_at, persistence_class
      FROM v_mmo_character_quests;
    INSERT OR REPLACE INTO mmo_character_known_dialogs_current(
      character_key, npc_symbol_index, info_symbol_index, npc_symbol_name, info_symbol_name,
      description, permanent, first_seen_tick, updated_at, persistence_class
    )
    SELECT character_key, npc_symbol_index, info_symbol_index, npc_symbol_name, info_symbol_name,
           COALESCE(description, ''), permanent, first_seen_tick, updated_at, persistence_class
      FROM v_mmo_character_known_dialogs;
    INSERT OR REPLACE INTO mmo_character_story_progress_current(
      character_key, world_name, tick_count, chapter_number, chapter_key,
      source_global_key, source_symbol_index, source_symbol_name, updated_at, persistence_class
    )
    SELECT character_key, world_name, tick_count, chapter_number, chapter_key,
           source_global_key, source_symbol_index, source_symbol_name, updated_at, persistence_class
      FROM v_mmo_character_story_progress;
    INSERT INTO mmo_world_items_current(
      item_spawn_key, world_name, tick_count, slot_id, persistent_id, item_template_symbol, script_id,
      item_display_name, visual, amount, main_flag, item_flags, value, pos_x, pos_y, pos_z,
      exists_in_world, updated_at, persistence_class
    )
    SELECT entity_key, world_name, tick_count, slot_id, persistent_id, symbol_index, script_id,
           display_name, visual, amount, main_flag, item_flags, value, pos_x, pos_y, pos_z,
           1, updated_at, 'world_persistent_item'
      FROM runtime_world_items
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
    ON CONFLICT(item_spawn_key) DO UPDATE SET
      world_name=excluded.world_name,
      tick_count=excluded.tick_count,
      slot_id=excluded.slot_id,
      persistent_id=excluded.persistent_id,
      item_template_symbol=excluded.item_template_symbol,
      script_id=excluded.script_id,
      item_display_name=excluded.item_display_name,
      visual=excluded.visual,
      amount=excluded.amount,
      main_flag=excluded.main_flag,
      item_flags=excluded.item_flags,
      value=excluded.value,
      pos_x=excluded.pos_x,
      pos_y=excluded.pos_y,
      pos_z=excluded.pos_z,
      exists_in_world=1,
      updated_at=excluded.updated_at,
      persistence_class=excluded.persistence_class;
    INSERT OR REPLACE INTO mmo_world_interactives_current(
      interactive_key, world_name, tick_count, slot_id, vob_id, tag, focus_name, display_name, scheme,
      pos_x, pos_y, pos_z, state, state_count, state_mask, container, door, ladder, locked, cracked,
      updated_at, persistence_class
    )
    SELECT entity_key, world_name, tick_count, slot_id, vob_id, tag, focus_name, display_name, scheme,
           pos_x, pos_y, pos_z, state, state_count, state_mask, container, door, ladder, locked, cracked,
           updated_at, 'world_persistent_interactive'
      FROM runtime_world_mobsi
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR REPLACE INTO mmo_world_container_inventory_current(
      owner_key, item_instance_key, world_name, owner_display_name, item_template_symbol, item_display_name,
      amount, iterator_count, value, updated_at, persistence_class
    )
    SELECT owner_key, item_key, world_name, owner_display_name, symbol_index, display_name,
           amount, iterator_count, value, updated_at, 'world_persistent_container_inventory'
      FROM runtime_world_mobsi_inventory
     WHERE world_name = (SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR REPLACE INTO mmo_script_globals_current(
      global_key, symbol_index, symbol_name, value_type, category, value_count, value_text, updated_at, persistence_class
    )
    SELECT global_key, symbol_index, symbol_name, value_type, category, value_count, value_text, updated_at,
           CASE WHEN category IN ('quest', 'dialog') THEN 'world_persistent_delta' ELSE 'world_script_state' END
      FROM runtime_script_globals;
    INSERT OR REPLACE INTO mmo_script_global_values_current(
      global_key, value_index, value_int, value_real, value_text, updated_at
    )
    SELECT global_key, value_index, value_int, value_real, value_text, updated_at
      FROM runtime_script_global_values;
    INSERT OR REPLACE INTO mmo_guild_attitudes_current(
      realm_key, from_guild, to_guild, attitude, updated_at
    )
    SELECT 'local-g2notr', from_guild, to_guild, attitude, updated_at
      FROM runtime_guild_attitudes;
  )SQL";
  exec(impl->db, materializeMmoCurrent);

  const char* captureWorldBaseline = R"SQL(
    INSERT OR IGNORE INTO mmo_world_templates(
      world_template_key, game_target, content_revision_key, world_name,
      baseline_tick, baseline_world_time_millis
    )
    SELECT 'g2notr:' || world_name, 'g2notr', 'g2notr:runtime-v1', world_name,
           tick_count, world_time_millis
      FROM mmo_world_clock_current
     WHERE world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
       AND (SELECT COUNT(*) FROM runtime_sessions)=1;
    INSERT INTO mmo_world_instances(
      world_instance_key, realm_key, world_template_key, world_name, lifecycle_state,
      baseline_tick, current_tick, current_world_time_millis, created_at, updated_at
    )
    SELECT 'local-g2notr:' || c.world_name, 'local-g2notr', 'g2notr:' || c.world_name, c.world_name, 'active',
           t.baseline_tick, c.tick_count, c.world_time_millis, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP
      FROM mmo_world_clock_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
    ON CONFLICT(world_instance_key) DO UPDATE SET
      lifecycle_state=excluded.lifecycle_state,
      current_tick=excluded.current_tick,
      current_world_time_millis=excluded.current_world_time_millis,
      updated_at=CURRENT_TIMESTAMP;
    INSERT OR IGNORE INTO mmo_world_baseline_creature_templates(
      world_template_key, creature_template_key, creature_template_id, script_id, display_name, guild, true_guild,
      spawn_count, min_level, max_level, base_health_max, base_mana_max, base_strength, base_dexterity,
      resist_blunt, resist_edge, resist_fire, resist_magic
    )
    SELECT 'g2notr:' || c.world_name, c.creature_template_key, c.creature_template_id, c.script_id, c.display_name, c.guild, c.true_guild,
           c.spawn_count, c.min_level, c.max_level, c.base_health_max, c.base_mana_max, c.base_strength, c.base_dexterity,
           c.resist_blunt, c.resist_edge, c.resist_fire, c.resist_magic
      FROM mmo_creature_templates_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name AND t.baseline_captured_at IS NULL
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR IGNORE INTO mmo_world_baseline_creatures(
      world_template_key, creature_spawn_key, creature_template_id, world_name, tick_count, display_name,
      pos_x, pos_y, pos_z, rotation, waypoint, dead, level, experience, health_current, health_max,
      mana_current, mana_max, strength, dexterity, current_waypoint_name, routine_waypoint_name,
      move_hint, move_target_waypoint_name
    )
    SELECT 'g2notr:' || c.world_name, c.creature_spawn_key, c.creature_template_id, c.world_name, c.tick_count, c.display_name,
           c.pos_x, c.pos_y, c.pos_z, c.rotation, c.waypoint, c.dead, c.level, c.experience, c.health_current, c.health_max,
           c.mana_current, c.mana_max, c.strength, c.dexterity, c.current_waypoint_name, c.routine_waypoint_name,
           c.move_hint, c.move_target_waypoint_name
      FROM mmo_creature_spawns_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name AND t.baseline_captured_at IS NULL
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR IGNORE INTO mmo_world_baseline_creature_stats(
      world_template_key, creature_spawn_key, stat_group, stat_id, stat_key, value
    )
    SELECT 'g2notr:' || s.world_name, s.unit_key, s.stat_group, s.stat_id, s.stat_key, s.value
      FROM mmo_unit_stat_current s
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || s.world_name AND t.baseline_captured_at IS NULL
     WHERE s.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
       AND s.unit_type='creature';
    INSERT OR IGNORE INTO mmo_world_baseline_creature_inventory(
      world_template_key, creature_spawn_key, item_instance_key, item_template_symbol, item_display_name,
      amount, iterator_count, equipped, equip_count, slot, main_flag, item_flags, value, spell_id
    )
    SELECT 'g2notr:' || c.world_name, c.creature_spawn_key, c.item_instance_key, c.item_template_symbol, c.item_display_name,
           c.amount, c.iterator_count, c.equipped, c.equip_count, c.slot, c.main_flag, c.item_flags, c.value, c.spell_id
      FROM mmo_creature_inventory_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name AND t.baseline_captured_at IS NULL
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR IGNORE INTO mmo_world_baseline_creature_inventory_snapshots(
      world_template_key, creature_spawn_key, item_row_count
    )
    SELECT 'g2notr:' || c.world_name, c.creature_spawn_key, c.item_row_count
      FROM mmo_creature_inventory_snapshots_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name AND t.baseline_captured_at IS NULL
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR IGNORE INTO mmo_world_baseline_items(
      world_template_key, item_spawn_key, world_name, slot_id, persistent_id, item_template_symbol, script_id,
      item_display_name, visual, amount, main_flag, item_flags, value, pos_x, pos_y, pos_z
    )
    SELECT 'g2notr:' || c.world_name, c.item_spawn_key, c.world_name, c.slot_id, c.persistent_id, c.item_template_symbol, c.script_id,
           c.item_display_name, c.visual, c.amount, c.main_flag, c.item_flags, c.value, c.pos_x, c.pos_y, c.pos_z
      FROM mmo_world_items_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name AND t.baseline_captured_at IS NULL
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
       AND c.exists_in_world!=0;
    INSERT OR IGNORE INTO mmo_world_baseline_interactives(
      world_template_key, interactive_key, world_name, slot_id, vob_id, tag, focus_name, display_name, scheme,
      pos_x, pos_y, pos_z, state, state_count, state_mask, container, door, ladder, locked, cracked
    )
    SELECT 'g2notr:' || c.world_name, c.interactive_key, c.world_name, c.slot_id, c.vob_id, c.tag, c.focus_name, c.display_name, c.scheme,
           c.pos_x, c.pos_y, c.pos_z, c.state, c.state_count, c.state_mask, c.container, c.door, c.ladder, c.locked, c.cracked
      FROM mmo_world_interactives_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name AND t.baseline_captured_at IS NULL
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR IGNORE INTO mmo_world_baseline_container_inventory(
      world_template_key, owner_key, item_instance_key, item_template_symbol, item_display_name, amount, iterator_count, value
    )
    SELECT 'g2notr:' || c.world_name, c.owner_key, c.item_instance_key, c.item_template_symbol, c.item_display_name,
           c.amount, c.iterator_count, c.value
      FROM mmo_world_container_inventory_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name AND t.baseline_captured_at IS NULL
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions));
    INSERT OR IGNORE INTO mmo_world_baseline_script_globals(
      world_template_key, global_key, symbol_index, symbol_name, value_type, category, value_count, value_text
    )
    SELECT t.world_template_key, g.global_key, g.symbol_index, g.symbol_name, g.value_type, g.category, g.value_count, g.value_text
      FROM mmo_script_globals_current g
      JOIN mmo_world_templates t
        ON t.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
       AND t.baseline_captured_at IS NULL;
    INSERT OR IGNORE INTO mmo_world_baseline_script_global_values(
      world_template_key, global_key, value_index, value_int, value_real, value_text
    )
    SELECT t.world_template_key, v.global_key, v.value_index, v.value_int, v.value_real, v.value_text
      FROM mmo_script_global_values_current v
      JOIN mmo_world_templates t
        ON t.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
       AND t.baseline_captured_at IS NULL;
    INSERT OR IGNORE INTO mmo_world_baseline_guild_attitudes(
      world_template_key, from_guild, to_guild, attitude
    )
    SELECT t.world_template_key, g.from_guild, g.to_guild, g.attitude
      FROM mmo_guild_attitudes_current g
      JOIN mmo_world_templates t
        ON t.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
       AND t.baseline_captured_at IS NULL
     WHERE g.realm_key='local-g2notr';
    UPDATE mmo_world_templates
       SET baseline_captured_at=CURRENT_TIMESTAMP
     WHERE world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
       AND baseline_captured_at IS NULL;
  )SQL";
  if(impl->captureBaseline && !exec(impl->db, captureWorldBaseline))
    Tempest::Log::e("mmo sqlite failed to capture world baseline: ", sqlite3_errmsg(impl->db));

  const char* updateWorldInstance = R"SQL(
    INSERT INTO mmo_world_instances(
      world_instance_key, realm_key, world_template_key, world_name, lifecycle_state,
      baseline_tick, current_tick, current_world_time_millis, created_at, updated_at
    )
    SELECT 'local-g2notr:' || c.world_name, 'local-g2notr', t.world_template_key, c.world_name, 'active',
           t.baseline_tick, c.tick_count, c.world_time_millis, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP
      FROM mmo_world_clock_current c
      JOIN mmo_world_templates t ON t.world_template_key='g2notr:' || c.world_name
     WHERE c.world_name=(SELECT world_name FROM runtime_sessions WHERE id=(SELECT MAX(id) FROM runtime_sessions))
    ON CONFLICT(world_instance_key) DO UPDATE SET
      lifecycle_state=excluded.lifecycle_state,
      current_tick=excluded.current_tick,
      current_world_time_millis=excluded.current_world_time_millis,
      updated_at=CURRENT_TIMESTAMP;
  )SQL";
  if(!exec(impl->db, updateWorldInstance))
    Tempest::Log::e("mmo sqlite failed to update world instance: ", sqlite3_errmsg(impl->db));

  exec(impl->db, "COMMIT");
#endif
  }
