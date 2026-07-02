#!/usr/bin/env python3
import argparse
import sqlite3
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def decode_text(raw: bytes) -> str:
    for encoding in ("utf-8", "cp1250", "latin-1"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            pass
    return raw.decode("utf-8", errors="replace")


def table_exists(conn: sqlite3.Connection, name: str) -> bool:
    return conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (name,),
    ).fetchone() is not None


def table_columns(conn: sqlite3.Connection, name: str) -> set[str]:
    if not table_exists(conn, name):
        return set()
    return {row["name"] for row in conn.execute(f"PRAGMA table_info({quote_identifier(name)})")}


def scalar(conn: sqlite3.Connection, sql: str, params=(), default=0):
    row = conn.execute(sql, params).fetchone()
    return default if row is None else row[0]


def rows(conn: sqlite3.Connection, sql: str, params=()):
    return conn.execute(sql, params).fetchall()


def report(level: str, message: str) -> None:
    print(f"{level}: {message}")


def quote_identifier(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def text_storage_columns(conn: sqlite3.Connection):
    table_rows = conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name"
    ).fetchall()
    for table_row in table_rows:
        table = table_row["name"]
        pragma = f"PRAGMA table_info({quote_identifier(table)})"
        for column_row in conn.execute(pragma):
            declared_type = str(column_row["type"] or "").upper()
            if "TEXT" in declared_type or "CHAR" in declared_type or "CLOB" in declared_type:
                yield table, column_row["name"]


def audit_utf8_text_storage(conn: sqlite3.Connection, limit: int) -> int:
    issues = 0
    samples = []
    for table, column in text_storage_columns(conn):
        sql = (
            f"SELECT rowid AS row_id, CAST({quote_identifier(column)} AS BLOB) AS raw_value "
            f"FROM {quote_identifier(table)} WHERE typeof({quote_identifier(column)})='text'"
        )
        for row in conn.execute(sql):
            raw = row["raw_value"]
            if raw is None:
                continue
            try:
                bytes(raw).decode("utf-8")
            except UnicodeDecodeError:
                issues += 1
                if len(samples) < limit:
                    samples.append((table, column, row["row_id"], bytes(raw).hex()))
    print()
    print("Text storage encoding")
    if issues == 0:
        report("OK", "all SQLite TEXT payloads are valid UTF-8")
    else:
        report("ERROR", f"SQLite TEXT payloads with invalid UTF-8={issues}")
        for table, column, row_id, hex_value in samples:
            print(f"  {table}.{column} rowid={row_id} hex={hex_value}")
    return issues


def audit_text_columns(conn: sqlite3.Connection, limit: int) -> int:
    checks = [
        ("runtime_characters", "character_key"),
        ("runtime_characters", "display_name"),
        ("runtime_character_inventory", "item_key"),
        ("runtime_character_inventory", "display_name"),
        ("runtime_character_inventory_history", "item_key"),
        ("runtime_character_inventory_history", "display_name"),
        ("runtime_events", "event_type"),
        ("runtime_events", "entity_key"),
        ("runtime_events", "subject_key"),
        ("runtime_events", "data_text"),
        ("runtime_world_npcs", "entity_key"),
        ("runtime_world_npcs", "display_name"),
        ("runtime_world_npc_history", "entity_key"),
        ("runtime_world_npc_history", "display_name"),
        ("runtime_npc_stats", "entity_key"),
        ("runtime_npc_stats", "display_name"),
        ("runtime_npc_stats", "stat_group"),
        ("runtime_npc_stats", "stat_key"),
        ("runtime_npc_stat_history", "entity_key"),
        ("runtime_npc_stat_history", "display_name"),
        ("runtime_npc_stat_history", "stat_group"),
        ("runtime_npc_stat_history", "stat_key"),
        ("runtime_npc_ai_state", "entity_key"),
        ("runtime_npc_ai_state", "display_name"),
        ("runtime_npc_ai_state", "ai_state_name"),
        ("runtime_npc_ai_state", "relation_kind"),
        ("runtime_npc_ai_history", "entity_key"),
        ("runtime_npc_ai_history", "display_name"),
        ("runtime_npc_ai_history", "ai_state_name"),
        ("runtime_npc_ai_history", "relation_kind"),
        ("runtime_waypoints", "waypoint_key"),
        ("runtime_waypoints", "world_name"),
        ("runtime_waypoints", "kind"),
        ("runtime_waypoints", "name"),
        ("runtime_waypoint_edges", "edge_key"),
        ("runtime_waypoint_edges", "from_waypoint_key"),
        ("runtime_waypoint_edges", "to_waypoint_key"),
        ("runtime_waypoint_edges", "from_name"),
        ("runtime_waypoint_edges", "to_name"),
        ("runtime_npc_routines", "entity_key"),
        ("runtime_npc_routines", "display_name"),
        ("runtime_npc_routines", "callback_symbol_name"),
        ("runtime_npc_navigation_state", "entity_key"),
        ("runtime_npc_navigation_state", "display_name"),
        ("runtime_npc_navigation_state", "move_hint"),
        ("runtime_npc_navigation_history", "entity_key"),
        ("runtime_npc_navigation_history", "display_name"),
        ("runtime_npc_navigation_history", "move_hint"),
        ("runtime_quests", "quest_key"),
        ("runtime_quests", "name"),
        ("runtime_quests", "entries_text"),
        ("runtime_quest_history", "quest_key"),
        ("runtime_quest_history", "name"),
        ("runtime_quest_history", "entries_text"),
        ("runtime_known_dialogs", "npc_symbol_name"),
        ("runtime_known_dialogs", "info_symbol_name"),
        ("runtime_known_dialog_history", "npc_symbol_name"),
        ("runtime_known_dialog_history", "info_symbol_name"),
        ("runtime_dialog_catalog", "info_symbol_name"),
        ("runtime_dialog_catalog", "npc_symbol_name"),
        ("runtime_dialog_catalog", "description"),
        ("runtime_dialog_catalog", "information_symbol_name"),
        ("runtime_dialog_catalog", "condition_symbol_name"),
        ("runtime_dialog_choice_snapshots", "npc_key"),
        ("runtime_dialog_choice_snapshots", "npc_display_name"),
        ("runtime_dialog_choice_snapshots", "phase"),
        ("runtime_dialog_choice_rows", "info_symbol_name"),
        ("runtime_dialog_choice_rows", "script_function_name"),
        ("runtime_dialog_choice_rows", "title"),
        ("runtime_dialog_selections", "npc_key"),
        ("runtime_dialog_selections", "npc_display_name"),
        ("runtime_dialog_selections", "phase"),
        ("runtime_dialog_selections", "info_symbol_name"),
        ("runtime_dialog_selections", "script_function_name"),
        ("runtime_dialog_selections", "title"),
        ("runtime_world_items", "entity_key"),
        ("runtime_world_items", "display_name"),
        ("runtime_world_mobsi", "entity_key"),
        ("runtime_world_mobsi", "focus_name"),
        ("runtime_world_mobsi", "display_name"),
        ("runtime_world_mobsi_inventory", "owner_key"),
        ("runtime_world_mobsi_inventory", "item_key"),
        ("runtime_world_mobsi_inventory", "owner_display_name"),
        ("runtime_world_mobsi_inventory", "display_name"),
        ("runtime_script_globals", "global_key"),
        ("runtime_script_globals", "symbol_name"),
        ("runtime_script_globals", "value_text"),
        ("runtime_script_global_history", "global_key"),
        ("runtime_script_global_history", "symbol_name"),
        ("runtime_script_global_history", "value_before"),
        ("runtime_script_global_history", "value_after"),
        ("runtime_story_progress_current", "character_key"),
        ("runtime_story_progress_current", "world_name"),
        ("runtime_story_progress_current", "chapter_key"),
        ("runtime_story_progress_current", "source_global_key"),
        ("runtime_story_progress_current", "source_symbol_name"),
        ("runtime_story_progress_history", "character_key"),
        ("runtime_story_progress_history", "world_name"),
        ("runtime_story_progress_history", "chapter_key"),
        ("runtime_story_progress_history", "source_global_key"),
        ("runtime_story_progress_history", "source_symbol_name"),
    ]
    issues = 0
    print()
    print("Text quality")
    for table, column in checks:
        if not table_exists(conn, table):
            continue
        null_count = scalar(conn, f"SELECT COUNT(*) FROM {table} WHERE {column} IS NULL")
        empty_count = scalar(conn, f"SELECT COUNT(*) FROM {table} WHERE TRIM(COALESCE({column}, '')) = ''")
        if not null_count and not empty_count:
            continue
        issues += 1
        level = "ERROR" if null_count else "WARN"
        report(level, f"{table}.{column}: null={null_count} empty={empty_count}")
        sample_rows = rows(
            conn,
            f"""
            SELECT rowid AS row_id, {column} AS value
              FROM {table}
             WHERE {column} IS NULL OR TRIM(COALESCE({column}, '')) = ''
             LIMIT ?
            """,
            (limit,),
        )
        for row in sample_rows:
            print(f"  rowid={row['row_id']} value={row['value']!r}")
    if issues == 0:
        report("OK", "no NULL/empty key/name/text fields in audited runtime tables")
    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit OpenGothic runtime MMO SQLite DB.")
    parser.add_argument("--db", required=True, help="Path to runtime SQLite DB.")
    parser.add_argument("--limit", type=int, default=20, help="Rows to show for diagnostics.")
    args = parser.parse_args()

    db = Path(args.db)
    if not db.exists():
        report("ERROR", f"DB does not exist: {db}")
        return 1

    conn = sqlite3.connect(db)
    conn.text_factory = decode_text
    conn.row_factory = sqlite3.Row

    required = [
        "runtime_schema_meta",
        "runtime_realms",
        "runtime_accounts",
        "runtime_character_bindings",
        "runtime_sessions",
        "runtime_characters",
        "runtime_character_history",
        "runtime_character_inventory",
        "runtime_character_inventory_history",
        "runtime_events",
        "runtime_world_npcs",
        "runtime_world_npc_history",
        "runtime_npc_stats",
        "runtime_npc_stat_history",
        "runtime_npc_ai_state",
        "runtime_npc_ai_history",
        "runtime_quests",
        "runtime_quest_history",
        "runtime_known_dialogs",
        "runtime_known_dialog_history",
        "runtime_dialog_catalog",
        "runtime_dialog_choice_snapshots",
        "runtime_dialog_choice_rows",
        "runtime_dialog_selections",
        "runtime_world_items",
        "runtime_world_item_history",
        "runtime_world_mobsi",
        "runtime_world_mobsi_history",
        "runtime_world_mobsi_inventory",
        "runtime_script_globals",
        "runtime_script_global_history",
    ]
    missing = [name for name in required if not table_exists(conn, name)]
    if missing:
        report("ERROR", "missing tables: " + ", ".join(missing))
        return 1

    schema_version = scalar(conn, "SELECT value FROM runtime_schema_meta WHERE key='schema_version'", default="?")
    report("OK", f"schema_version={schema_version}")
    try:
        schema_version_int = int(schema_version)
    except (TypeError, ValueError):
        schema_version_int = 0
    schema11_tables = [
        "runtime_waypoints",
        "runtime_waypoint_edges",
        "runtime_npc_routines",
        "runtime_npc_navigation_state",
        "runtime_npc_navigation_history",
    ]
    missing_schema11 = [name for name in schema11_tables if not table_exists(conn, name)]
    if schema_version_int >= 11 and missing_schema11:
        report("ERROR", "schema_version >= 11 but missing tables: " + ", ".join(missing_schema11))
        return 1
    if schema_version_int < 11 and missing_schema11:
        report("INFO", "waypoint/navigation tables not present yet; run the new build once to migrate to schema_version 11")

    schema13_tables = [
        "mmo_stat_definitions",
    ]
    missing_schema13_tables = [name for name in schema13_tables if not table_exists(conn, name)]
    if schema_version_int >= 13 and missing_schema13_tables:
        report("ERROR", "schema_version >= 13 but missing production stat objects: " + ", ".join(missing_schema13_tables))
        return 1
    if schema_version_int < 13 and missing_schema13_tables:
        report("INFO", "production stat objects not present yet; run the new build once to migrate to schema_version 13")

    schema14_tables = [
        "mmo_unit_stat_current",
        "mmo_unit_stat_sheet_current",
        "mmo_creature_templates_current",
        "mmo_creature_spawns_current",
    ]
    missing_schema14_tables = [name for name in schema14_tables if not table_exists(conn, name)]
    if schema_version_int >= 14 and missing_schema14_tables:
        report("ERROR", "schema_version >= 14 but missing materialized MMO current tables: " + ", ".join(missing_schema14_tables))
        return 1
    if schema_version_int < 14 and missing_schema14_tables:
        report("INFO", "materialized MMO current tables not present yet; run the new build once to migrate to schema_version 14")

    schema15_tables = [
        "runtime_script_global_values",
        "runtime_guild_attitudes",
        "mmo_characters_current",
        "mmo_character_inventory_current",
        "mmo_character_quests_current",
        "mmo_character_known_dialogs_current",
        "mmo_world_items_current",
        "mmo_world_interactives_current",
        "mmo_world_container_inventory_current",
        "mmo_script_globals_current",
        "mmo_script_global_values_current",
        "mmo_guild_attitudes_current",
    ]
    missing_schema15_tables = [name for name in schema15_tables if not table_exists(conn, name)]
    if schema_version_int >= 15 and missing_schema15_tables:
        report("ERROR", "schema_version >= 15 but missing canonical current tables: " + ", ".join(missing_schema15_tables))
        return 1
    if schema_version_int < 15 and missing_schema15_tables:
        report("INFO", "canonical current tables not present yet; run the new build once to migrate to schema_version 15")

    schema16_tables = [
        "runtime_world_clock",
        "runtime_world_npc_inventory",
        "mmo_world_clock_current",
        "mmo_creature_inventory_current",
        "mmo_creature_inventory_snapshots_current",
    ]
    missing_schema16_tables = [name for name in schema16_tables if not table_exists(conn, name)]
    if schema_version_int >= 16 and missing_schema16_tables:
        report("ERROR", "schema_version >= 16 but missing world-clock/NPC-inventory tables: " + ", ".join(missing_schema16_tables))
        return 1
    if schema_version_int < 16 and missing_schema16_tables:
        report("INFO", "world-clock/NPC-inventory tables not present yet; run the new build once to migrate to schema_version 16")

    schema17_tables = [
        "runtime_npc_relation_checkpoints",
        "mmo_creature_relations_current",
    ]
    missing_schema17_tables = [name for name in schema17_tables if not table_exists(conn, name)]
    if schema_version_int >= 17 and missing_schema17_tables:
        report("ERROR", "schema_version >= 17 but missing follow/escort checkpoint tables: " + ", ".join(missing_schema17_tables))
        return 1
    if schema_version_int < 17 and missing_schema17_tables:
        report("INFO", "follow/escort checkpoint tables not present yet; run the new build once to migrate to schema_version 17")

    schema18_tables = [
        "mmo_world_templates",
        "mmo_world_instances",
        "mmo_world_baseline_creature_templates",
        "mmo_world_baseline_creatures",
        "mmo_world_baseline_creature_stats",
        "mmo_world_baseline_creature_inventory",
        "mmo_world_baseline_creature_inventory_snapshots",
        "mmo_world_baseline_items",
        "mmo_world_baseline_interactives",
        "mmo_world_baseline_container_inventory",
        "mmo_world_baseline_script_globals",
        "mmo_world_baseline_script_global_values",
        "mmo_world_baseline_guild_attitudes",
    ]
    missing_schema18 = [name for name in schema18_tables if not table_exists(conn, name)]
    if schema_version_int >= 18 and missing_schema18:
        report("ERROR", "schema_version >= 18 but missing world baseline objects: " + ", ".join(missing_schema18))
        return 1
    if schema_version_int < 18 and missing_schema18:
        report("INFO", "world baseline objects not present yet; run the new build once to migrate to schema_version 18")

    schema19_tables = [
        "runtime_npc_stat_capture_state",
    ]
    missing_schema19 = [name for name in schema19_tables if not table_exists(conn, name)]
    if schema_version_int >= 19 and missing_schema19:
        report("ERROR", "schema_version >= 19 but missing delta-capture tables: " + ", ".join(missing_schema19))
        return 1
    if schema_version_int < 19 and missing_schema19:
        report("INFO", "delta-capture objects not present yet; run the new build once to migrate to schema_version 19")

    schema20_tables = [
        "runtime_character_wallet",
        "mmo_character_wallet_current",
    ]
    missing_schema20 = [name for name in schema20_tables if not table_exists(conn, name)]
    if schema_version_int >= 20 and missing_schema20:
        report("ERROR", "schema_version >= 20 but missing character wallet tables: " + ", ".join(missing_schema20))
        return 1
    if schema_version_int < 20 and missing_schema20:
        report("INFO", "character wallet objects not present yet; run the new build once to migrate to schema_version 20")

    if schema_version_int >= 21:
        persisted_views = scalar(conn, "SELECT COUNT(*) FROM sqlite_master WHERE type='view' AND name LIKE 'v_%'")
        report(
            "OK" if persisted_views == 0 else "ERROR",
            f"persisted SQL views={persisted_views}; schema 21 expects physical tables only",
        )
        if persisted_views != 0:
            return 1

    schema22_tables = [
        "mmo_save_slots",
        "mmo_save_slot_snapshots",
        "mmo_save_slot_characters",
        "mmo_save_slot_unit_stat",
        "mmo_save_slot_unit_stat_sheet",
        "mmo_save_slot_character_inventory",
        "mmo_save_slot_character_wallet",
        "mmo_save_slot_character_quests",
        "mmo_save_slot_character_known_dialogs",
        "mmo_save_slot_world_clock",
        "mmo_save_slot_creature_spawns",
        "mmo_save_slot_creature_inventory",
        "mmo_save_slot_creature_inventory_snapshots",
        "mmo_save_slot_creature_relations",
        "mmo_save_slot_world_items",
        "mmo_save_slot_world_interactives",
        "mmo_save_slot_world_container_inventory",
        "mmo_save_slot_script_globals",
        "mmo_save_slot_script_global_values",
        "mmo_save_slot_guild_attitudes",
    ]
    missing_schema22 = [name for name in schema22_tables if not table_exists(conn, name)]
    if schema_version_int >= 22 and missing_schema22:
        report("ERROR", "schema_version >= 22 but missing save-slot tables: " + ", ".join(missing_schema22))
        return 1
    if schema_version_int < 22 and missing_schema22:
        report("INFO", "save-slot snapshot tables not present yet; run the new build once to migrate to schema_version 22")

    schema23_columns = {
        "runtime_npc_ai_state": {"state_other_key", "state_victim_key"},
        "runtime_npc_ai_history": {"state_other_key", "state_victim_key"},
    }
    missing_schema23 = []
    for table, expected_columns in schema23_columns.items():
        existing_columns = table_columns(conn, table)
        for column in sorted(expected_columns - existing_columns):
            missing_schema23.append(f"{table}.{column}")
    if schema_version_int >= 23 and missing_schema23:
        report("ERROR", "schema_version >= 23 but missing AI relation columns: " + ", ".join(missing_schema23))
        return 1
    if schema_version_int < 23 and missing_schema23:
        report("INFO", "AI relation context columns not present yet; run the new build once to migrate to schema_version 23")

    schema24_tables = [
        "runtime_story_progress_current",
        "runtime_story_progress_history",
        "runtime_chapter_intro_events",
        "mmo_character_story_progress_current",
        "mmo_save_slot_character_story_progress",
    ]
    missing_schema24 = [name for name in schema24_tables if not table_exists(conn, name)]
    if schema_version_int >= 24 and missing_schema24:
        report("ERROR", "schema_version >= 24 but missing story/chapter tables: " + ", ".join(missing_schema24))
        return 1
    if schema_version_int < 24 and missing_schema24:
        report("INFO", "story/chapter tables not present yet; run the new build once to migrate to schema_version 24")

    schema25_columns = {
        "mmo_unit_stat_sheet_current": {
            "experience_next",
            "learning_points",
            "permanent_attitude",
            "temporary_attitude",
        },
        "mmo_save_slot_unit_stat_sheet": {
            "experience_next",
            "learning_points",
            "permanent_attitude",
            "temporary_attitude",
        },
    }
    missing_schema25 = []
    for table, expected_columns in schema25_columns.items():
        existing_columns = table_columns(conn, table)
        for column in sorted(expected_columns - existing_columns):
            missing_schema25.append(f"{table}.{column}")
    if schema_version_int >= 25 and missing_schema25:
        report("ERROR", "schema_version >= 25 but missing stat sheet columns: " + ", ".join(missing_schema25))
        return 1
    if schema_version_int < 25 and missing_schema25:
        report("INFO", "stat sheet progression columns not present yet; run the new build once to migrate to schema_version 25")

    sessions = scalar(conn, "SELECT COUNT(*) FROM runtime_sessions")
    realms = scalar(conn, "SELECT COUNT(*) FROM runtime_realms")
    accounts = scalar(conn, "SELECT COUNT(*) FROM runtime_accounts")
    character_bindings = scalar(conn, "SELECT COUNT(*) FROM runtime_character_bindings")
    characters = scalar(conn, "SELECT COUNT(*) FROM runtime_characters")
    history = scalar(conn, "SELECT COUNT(*) FROM runtime_character_history")
    inventory = scalar(conn, "SELECT COUNT(*) FROM runtime_character_inventory")
    inv_history = scalar(conn, "SELECT COUNT(*) FROM runtime_character_inventory_history")
    events = scalar(conn, "SELECT COUNT(*) FROM runtime_events")
    world_npcs = scalar(conn, "SELECT COUNT(*) FROM runtime_world_npcs")
    world_npc_history = scalar(conn, "SELECT COUNT(*) FROM runtime_world_npc_history")
    npc_stats = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_stats")
    npc_stat_capture_state = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_stat_capture_state") if table_exists(conn, "runtime_npc_stat_capture_state") else 0
    character_wallet = scalar(conn, "SELECT COUNT(*) FROM runtime_character_wallet") if table_exists(conn, "runtime_character_wallet") else 0
    npc_stat_history = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_stat_history")
    stat_definitions = scalar(conn, "SELECT COUNT(*) FROM mmo_stat_definitions") if table_exists(conn, "mmo_stat_definitions") else 0
    npc_ai_state = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_ai_state")
    npc_ai_history = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_ai_history")
    npc_relation_checkpoints = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_relation_checkpoints") if table_exists(conn, "runtime_npc_relation_checkpoints") else 0
    waypoints = scalar(conn, "SELECT COUNT(*) FROM runtime_waypoints") if table_exists(conn, "runtime_waypoints") else 0
    waypoint_edges = scalar(conn, "SELECT COUNT(*) FROM runtime_waypoint_edges") if table_exists(conn, "runtime_waypoint_edges") else 0
    npc_routines = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_routines") if table_exists(conn, "runtime_npc_routines") else 0
    npc_navigation = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_navigation_state") if table_exists(conn, "runtime_npc_navigation_state") else 0
    npc_navigation_history = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_navigation_history") if table_exists(conn, "runtime_npc_navigation_history") else 0
    quests = scalar(conn, "SELECT COUNT(*) FROM runtime_quests")
    known_dialogs = scalar(conn, "SELECT COUNT(*) FROM runtime_known_dialogs")
    dialog_catalog = scalar(conn, "SELECT COUNT(*) FROM runtime_dialog_catalog")
    dialog_choice_snapshots = scalar(conn, "SELECT COUNT(*) FROM runtime_dialog_choice_snapshots")
    dialog_choice_rows = scalar(conn, "SELECT COUNT(*) FROM runtime_dialog_choice_rows")
    dialog_selections = scalar(conn, "SELECT COUNT(*) FROM runtime_dialog_selections")
    world_items = scalar(conn, "SELECT COUNT(*) FROM runtime_world_items")
    world_mobsi = scalar(conn, "SELECT COUNT(*) FROM runtime_world_mobsi")
    world_mobsi_inventory = scalar(conn, "SELECT COUNT(*) FROM runtime_world_mobsi_inventory")
    script_globals = scalar(conn, "SELECT COUNT(*) FROM runtime_script_globals")
    script_global_history = scalar(conn, "SELECT COUNT(*) FROM runtime_script_global_history")
    story_progress = scalar(conn, "SELECT COUNT(*) FROM runtime_story_progress_current") if table_exists(conn, "runtime_story_progress_current") else 0
    story_history = scalar(conn, "SELECT COUNT(*) FROM runtime_story_progress_history") if table_exists(conn, "runtime_story_progress_history") else 0
    chapter_intro_events = scalar(conn, "SELECT COUNT(*) FROM runtime_chapter_intro_events") if table_exists(conn, "runtime_chapter_intro_events") else 0

    report("OK" if realms > 0 else "WARN", f"realms={realms}")
    report("OK" if accounts > 0 else "WARN", f"accounts={accounts}")
    if character_bindings < characters:
        report("WARN", f"character bindings lower than characters: {character_bindings}/{characters}")
    else:
        report("OK", f"character bindings={character_bindings}")

    if sessions <= 0:
        report("ERROR", "no runtime sessions recorded")
    else:
        report("OK", f"sessions={sessions}")

    if characters != 1:
        report("WARN", f"expected one local HERO character, got {characters}")
    else:
        report("OK", "one character current-state row")

    if history <= 1:
        report("WARN", f"low character history rows: {history}")
    else:
        report("OK", f"character history rows={history}")

    if inventory <= 0:
        report("WARN", "current inventory is empty")
    else:
        report("OK", f"current inventory rows={inventory}")

    if inv_history <= inventory:
        report("WARN", f"inventory history looks shallow: {inv_history}")
    else:
        report("OK", f"inventory history rows={inv_history}")

    if events <= 0:
        report("WARN", "no runtime events recorded")
    else:
        report("OK", f"runtime events={events}")

    if world_npcs <= 0:
        report("ERROR", "no runtime world NPC rows; MMO world state is missing")
    else:
        report("OK", f"runtime world NPC rows={world_npcs}")

    if world_npc_history <= 0:
        report("WARN", "no runtime world NPC history rows yet")
    else:
        report("OK", f"runtime world NPC history rows={world_npc_history}")
    report("OK" if npc_stats > 0 else "WARN", f"runtime NPC stat rows={npc_stats}")
    if table_exists(conn, "runtime_npc_stat_capture_state"):
        report("OK" if npc_stat_capture_state == world_npcs else "WARN", f"runtime NPC stat capture states={npc_stat_capture_state}/{world_npcs}")
    if table_exists(conn, "runtime_character_wallet"):
        report("OK" if character_wallet == 1 else "WARN", f"character wallet rows={character_wallet}")
    report("OK" if npc_stat_history >= 0 else "WARN", f"runtime NPC stat history rows={npc_stat_history}")
    if table_exists(conn, "mmo_stat_definitions"):
        expected_stat_definitions = 191 if schema_version_int >= 16 else 82
        report("OK" if stat_definitions == expected_stat_definitions else "WARN", f"MMO stat definitions={stat_definitions}")
    report("OK" if npc_ai_state > 0 else "WARN", f"runtime NPC AI state rows={npc_ai_state}")
    report("OK" if npc_ai_history >= 0 else "WARN", f"runtime NPC AI history rows={npc_ai_history}")
    if table_exists(conn, "runtime_npc_relation_checkpoints"):
        report("INFO", f"runtime follow/escort checkpoints={npc_relation_checkpoints}")
    if schema_version_int >= 11:
        report("OK" if waypoints > 0 else "WARN", f"runtime waypoint rows={waypoints}")
        report("OK" if waypoint_edges > 0 else "WARN", f"runtime waypoint edge rows={waypoint_edges}")
        report("OK" if npc_routines > 0 else "WARN", f"runtime NPC routine rows={npc_routines}")
        report("OK" if npc_navigation > 0 else "WARN", f"runtime NPC navigation rows={npc_navigation}")
        report("OK" if npc_navigation_history >= 0 else "WARN", f"runtime NPC navigation history rows={npc_navigation_history}")

    report("OK" if quests > 0 else "INFO", f"runtime quest rows={quests}")
    report("OK" if known_dialogs > 0 else "INFO", f"runtime known dialog rows={known_dialogs}")
    report("OK" if dialog_catalog > 0 else "WARN", f"runtime dialog catalog rows={dialog_catalog}")
    report("OK" if dialog_choice_snapshots > 0 else "INFO", f"runtime dialog choice snapshots={dialog_choice_snapshots}")
    report("OK" if dialog_choice_rows >= dialog_choice_snapshots else "WARN", f"runtime dialog choice rows={dialog_choice_rows}")
    report("OK" if dialog_selections > 0 else "INFO", f"runtime dialog selections={dialog_selections}")
    report("OK" if world_items > 0 else "WARN", f"runtime world item rows={world_items}")
    report("OK" if world_mobsi > 0 else "WARN", f"runtime world mobsi rows={world_mobsi}")
    report("OK" if world_mobsi_inventory >= 0 else "WARN", f"runtime world mobsi inventory rows={world_mobsi_inventory}")
    report("OK" if script_globals > 0 else "WARN", f"runtime script global rows={script_globals}")
    report("OK" if script_global_history >= 0 else "WARN", f"runtime script global history rows={script_global_history}")
    if table_exists(conn, "runtime_story_progress_current"):
        report("OK" if story_progress == 1 else "WARN", f"runtime story progress rows={story_progress}")
        report("OK" if story_history >= 0 else "WARN", f"runtime story progress history rows={story_history}")
        report("OK" if chapter_intro_events >= 0 else "WARN", f"runtime chapter intro events={chapter_intro_events}")

    null_inventory_names = scalar(
        conn,
        "SELECT COUNT(*) FROM runtime_character_inventory WHERE display_name IS NULL",
    )
    null_inventory_history_names = scalar(
        conn,
        "SELECT COUNT(*) FROM runtime_character_inventory_history WHERE display_name IS NULL",
    )
    empty_inventory_names = scalar(
        conn,
        "SELECT COUNT(*) FROM runtime_character_inventory WHERE display_name = ''",
    )
    if null_inventory_names or null_inventory_history_names:
        report(
            "ERROR",
            f"NULL display_name in inventory current/history: {null_inventory_names}/{null_inventory_history_names}",
        )
    else:
        report("OK", "inventory display_name has no NULL values")
    if empty_inventory_names:
        report("WARN", f"empty inventory display_name rows={empty_inventory_names}")

    null_world_names = rows(
        conn,
        """
        SELECT 'runtime_world_items' AS table_name, COUNT(*) AS c FROM runtime_world_items WHERE display_name IS NULL
        UNION ALL
        SELECT 'runtime_world_mobsi', COUNT(*) FROM runtime_world_mobsi WHERE display_name IS NULL
        UNION ALL
        SELECT 'runtime_world_mobsi_inventory', COUNT(*) FROM runtime_world_mobsi_inventory WHERE display_name IS NULL
        UNION ALL
        SELECT 'runtime_script_globals', COUNT(*) FROM runtime_script_globals WHERE symbol_name IS NULL
        """,
    )
    bad_world_names = [row for row in null_world_names if row["c"]]
    if bad_world_names:
        report("ERROR", "NULL display_name in world tables")
        for row in bad_world_names:
            print(f"  {row['table_name']}: {row['c']}")
    else:
        report("OK", "world tables display_name has no NULL values")

    text_issues = audit_text_columns(conn, args.limit)
    utf8_text_issues = audit_utf8_text_storage(conn, args.limit)
    fatal_issues = utf8_text_issues

    duplicate_events = rows(
        conn,
        """
        SELECT event_type, entity_key, subject_key, tick_count, COUNT(*) AS c
          FROM runtime_events
         GROUP BY event_type, entity_key, subject_key, tick_count
        HAVING c > 1
         ORDER BY c DESC, tick_count DESC
         LIMIT ?
        """,
        (args.limit,),
    )
    if duplicate_events:
        report("WARN", f"duplicate same tick/type/subject events={len(duplicate_events)}")
        for row in duplicate_events:
            print(
                f"  {row['event_type']} entity={row['entity_key']} "
                f"subject={row['subject_key']} tick={row['tick_count']} count={row['c']}"
            )
    else:
        report("OK", "no duplicate same tick/type/entity/subject events")

    equip_issues = rows(
        conn,
        """
        SELECT display_name, symbol_index, amount, iterator_count, equipped, equip_count, slot
          FROM runtime_character_inventory
         WHERE equipped = 0 AND equip_count != 0
         ORDER BY display_name, symbol_index
         LIMIT ?
        """,
        (args.limit,),
    )
    if equip_issues:
        report("WARN", "non-equipped inventory rows with equip_count != 0")
        for row in equip_issues:
            print(
                f"  {row['display_name']} symbol={row['symbol_index']} "
                f"amount={row['amount']} iterator={row['iterator_count']} "
                f"equip_count={row['equip_count']} slot={row['slot']}"
            )
    else:
        report("OK", "equipment split rows look consistent")

    event_counts = rows(
        conn,
        "SELECT event_type, COUNT(*) AS c FROM runtime_events GROUP BY event_type ORDER BY c DESC",
    )
    print()
    print("Event counts")
    for row in event_counts:
        print(f"  {row['event_type']}: {row['c']}")

    legacy_dialog_selected = scalar(conn, "SELECT COUNT(*) FROM runtime_events WHERE event_type='dialog_selected'")
    if legacy_dialog_selected:
        report("ERROR", f"legacy dialog_selected events={legacy_dialog_selected}; use phase-specific dialog event types")
        fatal_issues += 1
    else:
        report("OK", "dialog event types are phase-specific")

    missing_expected = [
        name
        for name in ("item_added", "character_moved")
        if scalar(conn, "SELECT COUNT(*) FROM runtime_events WHERE event_type=?", (name,)) == 0
    ]
    inventory_quantity_changes = scalar(
        conn,
        """
        SELECT COUNT(*)
          FROM (
            SELECT character_key, symbol_index
              FROM (
                SELECT character_key, symbol_index, tick_count, SUM(iterator_count) AS total_count
                  FROM runtime_character_inventory_history
                 GROUP BY character_key, symbol_index, tick_count
              )
             GROUP BY character_key, symbol_index
            HAVING COUNT(DISTINCT total_count) > 1
          )
        """,
    )
    if inventory_quantity_changes and scalar(conn, "SELECT COUNT(*) FROM runtime_events WHERE event_type='item_quantity_changed'") == 0:
        missing_expected.append("item_quantity_changed")
    if missing_expected:
        report("WARN", "missing expected event classes: " + ", ".join(missing_expected))
    else:
        report("OK", "basic event classes are present")
    if inventory_quantity_changes == 0:
        report("INFO", "no inventory stack quantity changes in history; test buy/pickup same template/consume stack next")

    removed = scalar(conn, "SELECT COUNT(*) FROM runtime_events WHERE event_type='item_removed'")
    if removed == 0:
        report("INFO", "no item_removed events in this run; test drop/consume/sell next")

    current_contract_tables = [
        "mmo_unit_stat_current",
        "mmo_unit_stat_sheet_current",
        "mmo_creature_templates_current",
        "mmo_creature_spawns_current",
        "mmo_world_clock_current",
        "mmo_creature_inventory_current",
        "mmo_creature_inventory_snapshots_current",
        "mmo_creature_relations_current",
        "mmo_characters_current",
        "mmo_character_inventory_current",
        "mmo_character_wallet_current",
        "mmo_character_quests_current",
        "mmo_character_known_dialogs_current",
        "mmo_character_story_progress_current",
        "mmo_world_items_current",
        "mmo_world_interactives_current",
        "mmo_world_container_inventory_current",
        "mmo_script_globals_current",
        "mmo_script_global_values_current",
        "mmo_guild_attitudes_current",
    ]
    empty_contract_tables = [
        name for name in current_contract_tables
        if table_exists(conn, name) and scalar(conn, f"SELECT COUNT(*) FROM {name}") == 0
    ]
    report("OK", f"MMO current contract tables={len(current_contract_tables)}")
    if empty_contract_tables:
        report("INFO", "empty MMO current tables: " + ", ".join(empty_contract_tables[: args.limit]))

    if table_exists(conn, "mmo_stat_definitions"):
        undefined_stats = scalar(
            conn,
            """
            SELECT COUNT(*)
              FROM runtime_npc_stats s
              LEFT JOIN mmo_stat_definitions d
                ON d.stat_group = s.stat_group AND d.stat_id = s.stat_id
             WHERE d.stat_group IS NULL
            """,
        )
        report("OK" if undefined_stats == 0 else "ERROR", f"undefined runtime stat rows={undefined_stats}")

    if table_exists(conn, "mmo_unit_stat_sheet_current"):
        unit_sheet_rows = scalar(conn, "SELECT COUNT(*) FROM mmo_unit_stat_sheet_current")
        report(
            "OK" if unit_sheet_rows == world_npcs else "ERROR",
            f"MMO unit stat sheet rows={unit_sheet_rows}/{world_npcs}",
        )

    if table_exists(conn, "mmo_unit_stat_sheet_current"):
        character_stat_sheets = scalar(conn, "SELECT COUNT(*) FROM mmo_unit_stat_sheet_current WHERE unit_type='character'")
        report(
            "OK" if character_stat_sheets == characters else "WARN",
            f"MMO character stat sheets={character_stat_sheets}/{characters}",
        )

    if table_exists(conn, "mmo_creature_templates_current"):
        creature_templates = scalar(conn, "SELECT COUNT(*) FROM mmo_creature_templates_current")
        creature_spawns = scalar(conn, "SELECT COUNT(*) FROM mmo_creature_spawns_current") if table_exists(conn, "mmo_creature_spawns_current") else 0
        report("OK" if creature_templates > 0 else "WARN", f"MMO creature templates={creature_templates}")
        report("OK" if creature_spawns > 0 else "WARN", f"MMO creature spawns={creature_spawns}")

    if table_exists(conn, "mmo_character_wallet_current"):
        current_wallet = scalar(conn, "SELECT COUNT(*) FROM mmo_character_wallet_current")
        report(
            "OK" if current_wallet == character_wallet else "ERROR",
            f"mmo_character_wallet_current rows={current_wallet}/{character_wallet} from runtime_character_wallet",
        )

    if table_exists(conn, "mmo_character_story_progress_current"):
        current_story = scalar(conn, "SELECT COUNT(*) FROM mmo_character_story_progress_current")
        runtime_story = scalar(conn, "SELECT COUNT(*) FROM runtime_story_progress_current") if table_exists(conn, "runtime_story_progress_current") else 0
        report(
            "OK" if current_story == runtime_story else "ERROR",
            f"mmo_character_story_progress_current rows={current_story}/{runtime_story} from runtime_story_progress_current",
        )

    if table_exists(conn, "mmo_world_items_current"):
        current_items = scalar(conn, "SELECT COUNT(*) FROM mmo_world_items_current WHERE exists_in_world!=0")
        runtime_items = scalar(conn, "SELECT COUNT(*) FROM runtime_world_items")
        report(
            "OK" if current_items == runtime_items else "ERROR",
            f"mmo_world_items_current active rows={current_items}/{runtime_items} from runtime_world_items",
        )
    if table_exists(conn, "mmo_script_global_values_current"):
        current_values = scalar(conn, "SELECT COUNT(*) FROM mmo_script_global_values_current")
        runtime_values = scalar(conn, "SELECT COUNT(*) FROM runtime_script_global_values")
        report(
            "OK" if current_values == runtime_values else "ERROR",
            f"mmo_script_global_values_current rows={current_values}/{runtime_values} from runtime_script_global_values",
        )
    if table_exists(conn, "mmo_guild_attitudes_current"):
        current_attitudes = scalar(conn, "SELECT COUNT(*) FROM mmo_guild_attitudes_current")
        runtime_attitudes = scalar(conn, "SELECT COUNT(*) FROM runtime_guild_attitudes")
        report(
            "OK" if current_attitudes == runtime_attitudes else "ERROR",
            f"mmo_guild_attitudes_current rows={current_attitudes}/{runtime_attitudes} from runtime_guild_attitudes",
        )
    if table_exists(conn, "mmo_world_clock_current"):
        current_clock = scalar(conn, "SELECT COUNT(*) FROM mmo_world_clock_current")
        runtime_clock = scalar(conn, "SELECT COUNT(*) FROM runtime_world_clock")
        report(
            "OK" if current_clock == runtime_clock else "ERROR",
            f"mmo_world_clock_current rows={current_clock}/{runtime_clock} from runtime_world_clock",
        )
    if table_exists(conn, "mmo_creature_inventory_current"):
        current_inventory = scalar(conn, "SELECT COUNT(*) FROM mmo_creature_inventory_current")
        runtime_inventory = scalar(conn, "SELECT COUNT(*) FROM runtime_world_npc_inventory")
        report(
            "OK" if current_inventory == runtime_inventory else "ERROR",
            f"mmo_creature_inventory_current rows={current_inventory}/{runtime_inventory} from runtime_world_npc_inventory",
        )
    if table_exists(conn, "mmo_creature_inventory_snapshots_current"):
        snapshot_rows = scalar(conn, "SELECT COUNT(*) FROM mmo_creature_inventory_snapshots_current")
        creature_rows = scalar(conn, "SELECT COUNT(*) FROM runtime_world_npcs WHERE player=0")
        report(
            "OK" if snapshot_rows == creature_rows else "ERROR",
            f"mmo_creature_inventory_snapshots_current rows={snapshot_rows}/{creature_rows} creature checkpoints",
        )
    if table_exists(conn, "mmo_creature_relations_current"):
        current_relations = scalar(conn, "SELECT COUNT(*) FROM mmo_creature_relations_current")
        runtime_relations = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_relation_checkpoints")
        report(
            "OK" if current_relations == runtime_relations else "ERROR",
            f"mmo_creature_relations_current rows={current_relations}/{runtime_relations} from runtime_npc_relation_checkpoints",
        )
    if table_exists(conn, "mmo_world_templates"):
        baselines = scalar(conn, "SELECT COUNT(*) FROM mmo_world_templates WHERE baseline_captured_at IS NOT NULL")
        instances = scalar(conn, "SELECT COUNT(*) FROM mmo_world_instances")
        report("OK" if baselines > 0 else "WARN", f"sealed world baselines={baselines}")
        report("OK" if instances >= baselines else "ERROR", f"world instances/baselines={instances}/{baselines}")
    if table_exists(conn, "mmo_world_items_current"):
        removed_items = scalar(conn, "SELECT COUNT(*) FROM mmo_world_items_current WHERE exists_in_world=0")
        report("INFO", f"world item tombstones={removed_items}")

    if table_exists(conn, "mmo_save_slots"):
        save_slots = scalar(conn, "SELECT COUNT(*) FROM mmo_save_slots")
        save_snapshots = scalar(conn, "SELECT COUNT(*) FROM mmo_save_slot_snapshots")
        current_slot_snapshots = scalar(conn, "SELECT COUNT(*) FROM mmo_save_slots WHERE current_snapshot_id IS NOT NULL")
        report("OK" if save_slots > 0 else "INFO", f"mmo save slots={save_slots}")
        report("OK" if save_snapshots >= save_slots else "WARN", f"mmo save slot snapshots={save_snapshots}")
        report(
            "OK" if current_slot_snapshots == save_slots else "WARN",
            f"save slots with current snapshots={current_slot_snapshots}/{save_slots}",
        )

    print()
    print("Useful SQL")
    print("  SELECT type, name FROM sqlite_master WHERE type='view' ORDER BY name;")
    print("  SELECT character_key, display_name, world_name, level, experience FROM mmo_characters_current;")
    print("  SELECT slot_key, display_name, source_slot_path, world_name, tick_count, current_snapshot_id, last_saved_at FROM mmo_save_slots ORDER BY updated_at DESC;")
    print("  SELECT snapshot_id, slot_key, display_name, world_name, tick_count, created_at FROM mmo_save_slot_snapshots ORDER BY snapshot_id DESC LIMIT 20;")
    print("  SELECT character_key, world_name, chapter_number, chapter_key, source_symbol_name, updated_at FROM mmo_character_story_progress_current;")
    print("  SELECT tick_count, chapter_before, chapter_after, chapter_key, source_symbol_name FROM runtime_story_progress_history ORDER BY id DESC LIMIT 20;")
    print("  SELECT tick_count, title, subtitle, image, sound, duration FROM runtime_chapter_intro_events ORDER BY id DESC LIMIT 20;")
    print("  SELECT character_key, currency_key, currency_display_name, amount FROM runtime_character_wallet;")
    print("  SELECT character_key, currency_key, currency_display_name, amount FROM mmo_character_wallet_current;")
    print("  SELECT * FROM mmo_unit_stat_sheet_current WHERE unit_type='character';")
    print("  SELECT unit_type, stat_domain, stat_family, COUNT(*) FROM mmo_unit_stat_current GROUP BY unit_type, stat_domain, stat_family;")
    print("  SELECT creature_template_id, display_name, spawn_count, min_level, max_level, base_health_max FROM mmo_creature_templates_current ORDER BY spawn_count DESC LIMIT 50;")
    print("  SELECT creature_spawn_key, creature_template_id, display_name, health_current, health_max, current_waypoint_name FROM mmo_creature_spawns_current ORDER BY display_name LIMIT 50;")
    print("  SELECT event_type, COUNT(*) FROM runtime_events GROUP BY event_type ORDER BY COUNT(*) DESC;")
    print("  SELECT * FROM runtime_events ORDER BY id DESC LIMIT 50;")
    print("  SELECT display_name, symbol_index, iterator_count, equipped, slot FROM runtime_character_inventory ORDER BY equipped DESC, display_name;")
    print("  SELECT tick_count, COUNT(*), SUM(iterator_count) FROM runtime_character_inventory_history GROUP BY tick_count ORDER BY tick_count DESC LIMIT 30;")
    print("  SELECT display_name, symbol_index, hp, hp_max, dead, pos_x, pos_y, pos_z FROM runtime_world_npcs ORDER BY dead DESC, display_name LIMIT 50;")
    print("  SELECT display_name, stat_group, stat_key, value FROM runtime_npc_stats WHERE player!=0 ORDER BY stat_group, stat_key;")
    print("  SELECT display_name, ai_state_name, target_display_name, target_key, state_other_key, state_victim_key, relation_kind FROM runtime_npc_ai_state ORDER BY updated_at DESC LIMIT 50;")
    print("  SELECT world_name, kind, COUNT(*) AS points FROM runtime_waypoints GROUP BY world_name, kind;")
    print("  SELECT display_name, current_waypoint_name, routine_waypoint_name, move_hint, move_target_waypoint_name, path_next_waypoint_name FROM runtime_npc_navigation_state ORDER BY display_name LIMIT 50;")
    print("  SELECT display_name, routine_index, start_minute, end_minute, callback_symbol_name, waypoint_name, active FROM runtime_npc_routines ORDER BY display_name, start_minute LIMIT 50;")
    print("  SELECT tick_count, display_name, changed_fields FROM runtime_npc_navigation_history ORDER BY id DESC LIMIT 50;")
    print("  SELECT display_name, stat_group, stat_key, value_before, value_after FROM runtime_npc_stat_history ORDER BY id DESC LIMIT 50;")
    print("  SELECT name, status, entry_count FROM runtime_quests ORDER BY status, name;")
    print("  SELECT npc_symbol_name, info_symbol_name FROM runtime_known_dialogs ORDER BY first_seen_tick DESC LIMIT 50;")
    print("  SELECT tick_count, npc_display_name, phase, title, info_symbol_name FROM runtime_dialog_selections ORDER BY id DESC LIMIT 50;")
    print("  SELECT display_name, symbol_index, amount, pos_x, pos_y, pos_z FROM runtime_world_items ORDER BY display_name LIMIT 50;")
    print("  SELECT display_name, state, container, door, locked, cracked FROM runtime_world_mobsi ORDER BY container DESC, door DESC, display_name LIMIT 50;")
    print("  SELECT owner_display_name, display_name, iterator_count FROM runtime_world_mobsi_inventory ORDER BY owner_display_name, display_name LIMIT 50;")
    print("  SELECT category, COUNT(*) FROM runtime_script_globals GROUP BY category ORDER BY COUNT(*) DESC;")
    print("  SELECT category, symbol_name, value_before, value_after FROM runtime_script_global_history ORDER BY id DESC LIMIT 50;")

    return 1 if fatal_issues else 0


if __name__ == "__main__":
    raise SystemExit(main())
