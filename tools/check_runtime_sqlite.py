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
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (name,),
    ).fetchone()
    return row is not None


def quote_identifier(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def table_columns(conn: sqlite3.Connection, name: str) -> set[str]:
    if not table_exists(conn, name):
        return set()
    return {row[1] for row in conn.execute(f"PRAGMA table_info({quote_identifier(name)})")}


def scalar(conn: sqlite3.Connection, sql: str, default=0):
    row = conn.execute(sql).fetchone()
    if row is None:
        return default
    return row[0]


def print_rows(conn: sqlite3.Connection, sql: str, empty: str) -> None:
    cursor = conn.execute(sql)
    rows = cursor.fetchall()
    if not rows:
        print(empty)
        return

    names = [description[0] for description in cursor.description]
    widths = [len(name) for name in names]
    for row in rows:
        for i, value in enumerate(row):
            widths[i] = max(widths[i], len(format_value(value)))

    print("  " + "  ".join(name.ljust(widths[i]) for i, name in enumerate(names)))
    print("  " + "  ".join("-" * widths[i] for i in range(len(names))))
    for row in rows:
        print("  " + "  ".join(format_value(value).ljust(widths[i]) for i, value in enumerate(row)))


def format_value(value) -> str:
    if isinstance(value, bytes):
        return decode_text(value)
    return str(value)


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect OpenGothic runtime MMO SQLite DB.")
    parser.add_argument("--db", required=True, help="Path to runtime SQLite DB.")
    parser.add_argument("--limit", type=int, default=10, help="Rows to show from history.")
    args = parser.parse_args()

    db = Path(args.db)
    if not db.exists():
        print(f"ERROR: DB does not exist: {db}")
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
        print("ERROR: missing runtime tables: " + ", ".join(missing))
        return 1

    schema_version = scalar(conn, "SELECT value FROM runtime_schema_meta WHERE key='schema_version'", "?")
    print(f"DB: {db}")
    print(f"schema_version: {schema_version}")
    try:
        schema_version_int = int(schema_version)
    except (TypeError, ValueError):
        schema_version_int = 0
    schema13_tables = [
        "mmo_stat_definitions",
    ]
    missing_schema13_tables = [name for name in schema13_tables if not table_exists(conn, name)]
    if schema_version_int >= 13 and missing_schema13_tables:
        print("ERROR: schema_version >= 13 but missing production stat objects: " + ", ".join(missing_schema13_tables))
        return 1
    if schema_version_int < 13 and missing_schema13_tables:
        print("production_stat_objects: missing; run the new build once to migrate to schema_version 13")
    schema14_tables = [
        "mmo_unit_stat_current",
        "mmo_unit_stat_sheet_current",
        "mmo_creature_templates_current",
        "mmo_creature_spawns_current",
    ]
    missing_schema14_tables = [name for name in schema14_tables if not table_exists(conn, name)]
    if schema_version_int >= 14 and missing_schema14_tables:
        print("ERROR: schema_version >= 14 but missing materialized MMO current tables: " + ", ".join(missing_schema14_tables))
        return 1
    if schema_version_int < 14 and missing_schema14_tables:
        print("mmo_materialized_current: missing; run the new build once to migrate to schema_version 14")
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
        print("ERROR: schema_version >= 15 but missing canonical MMO current tables: " + ", ".join(missing_schema15_tables))
        return 1
    if schema_version_int < 15 and missing_schema15_tables:
        print("mmo_canonical_current: missing; run the new build once to migrate to schema_version 15")
    schema16_tables = [
        "runtime_world_clock",
        "runtime_world_npc_inventory",
        "mmo_world_clock_current",
        "mmo_creature_inventory_current",
        "mmo_creature_inventory_snapshots_current",
    ]
    missing_schema16_tables = [name for name in schema16_tables if not table_exists(conn, name)]
    if schema_version_int >= 16 and missing_schema16_tables:
        print("ERROR: schema_version >= 16 but missing world-clock/NPC-inventory tables: " + ", ".join(missing_schema16_tables))
        return 1
    if schema_version_int < 16 and missing_schema16_tables:
        print("mmo_world_clock_npc_inventory: missing; run the new build once to migrate to schema_version 16")
    schema17_tables = [
        "runtime_npc_relation_checkpoints",
        "mmo_creature_relations_current",
    ]
    missing_schema17_tables = [name for name in schema17_tables if not table_exists(conn, name)]
    if schema_version_int >= 17 and missing_schema17_tables:
        print("ERROR: schema_version >= 17 but missing follow/escort checkpoint tables: " + ", ".join(missing_schema17_tables))
        return 1
    if schema_version_int < 17 and missing_schema17_tables:
        print("mmo_follow_escort: missing; run the new build once to migrate to schema_version 17")
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
        print("ERROR: schema_version >= 18 but missing world baseline objects: " + ", ".join(missing_schema18))
        return 1
    if schema_version_int < 18 and missing_schema18:
        print("mmo_world_baseline: missing; run the new build once to migrate to schema_version 18")
    schema19_tables = [
        "runtime_npc_stat_capture_state",
    ]
    missing_schema19 = [name for name in schema19_tables if not table_exists(conn, name)]
    if schema_version_int >= 19 and missing_schema19:
        print("ERROR: schema_version >= 19 but missing delta-capture tables: " + ", ".join(missing_schema19))
        return 1
    if schema_version_int < 19 and missing_schema19:
        print("mmo_delta_capture: missing; run the new build once to migrate to schema_version 19")
    schema20_tables = [
        "runtime_character_wallet",
        "mmo_character_wallet_current",
    ]
    missing_schema20 = [name for name in schema20_tables if not table_exists(conn, name)]
    if schema_version_int >= 20 and missing_schema20:
        print("ERROR: schema_version >= 20 but missing character wallet tables: " + ", ".join(missing_schema20))
        return 1
    if schema_version_int < 20 and missing_schema20:
        print("mmo_character_wallet: missing; run the new build once to migrate to schema_version 20")
    if schema_version_int >= 21:
        persisted_views = scalar(conn, "SELECT COUNT(*) FROM sqlite_master WHERE type='view' AND name LIKE 'v_%'")
        if persisted_views != 0:
            print(f"ERROR: schema_version >= 21 but DB still has persisted v_* views: {persisted_views}")
            return 1
        print("persisted_views: 0")
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
        print("ERROR: schema_version >= 22 but missing save-slot tables: " + ", ".join(missing_schema22))
        return 1
    if schema_version_int < 22 and missing_schema22:
        print("mmo_save_slots: missing; run the new build once to migrate to schema_version 22")
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
        print("ERROR: schema_version >= 23 but missing AI relation columns: " + ", ".join(missing_schema23))
        return 1
    if schema_version_int < 23 and missing_schema23:
        print("ai_relation_context: missing; run the new build once to migrate to schema_version 23")
    schema24_tables = [
        "runtime_story_progress_current",
        "runtime_story_progress_history",
        "runtime_chapter_intro_events",
        "mmo_character_story_progress_current",
        "mmo_save_slot_character_story_progress",
    ]
    missing_schema24 = [name for name in schema24_tables if not table_exists(conn, name)]
    if schema_version_int >= 24 and missing_schema24:
        print("ERROR: schema_version >= 24 but missing story/chapter tables: " + ", ".join(missing_schema24))
        return 1
    if schema_version_int < 24 and missing_schema24:
        print("story_chapters: missing; run the new build once to migrate to schema_version 24")
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
        print("ERROR: schema_version >= 25 but missing stat sheet columns: " + ", ".join(missing_schema25))
        return 1
    if schema_version_int < 25 and missing_schema25:
        print("stat_sheet_progression: missing; run the new build once to migrate to schema_version 25")
    print(f"realms: {scalar(conn, 'SELECT COUNT(*) FROM runtime_realms')}")
    print(f"accounts: {scalar(conn, 'SELECT COUNT(*) FROM runtime_accounts')}")
    print(f"character_bindings: {scalar(conn, 'SELECT COUNT(*) FROM runtime_character_bindings')}")
    print(f"sessions: {scalar(conn, 'SELECT COUNT(*) FROM runtime_sessions')}")
    print(f"characters: {scalar(conn, 'SELECT COUNT(*) FROM runtime_characters')}")
    print(f"history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_character_history')}")
    print(f"inventory_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_character_inventory')}")
    print(f"inventory_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_character_inventory_history')}")
    print(f"event_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_events')}")
    print(f"world_npc_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_npcs')}")
    if table_exists(conn, "runtime_world_clock"):
        print(f"world_clock_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_clock')}")
    else:
        print("world_clock_rows: missing; run the game once with schema_version 16")
    if table_exists(conn, "runtime_world_npc_inventory"):
        print(f"world_npc_inventory_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_npc_inventory')}")
    else:
        print("world_npc_inventory_rows: missing; run the game once with schema_version 16")
    if table_exists(conn, "runtime_npc_relation_checkpoints"):
        print(f"npc_relation_checkpoint_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_relation_checkpoints')}")
    else:
        print("npc_relation_checkpoint_rows: missing; run the game once with schema_version 17")
    print(f"world_npc_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_npc_history')}")
    print(f"npc_stat_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_stats')}")
    if table_exists(conn, "runtime_npc_stat_capture_state"):
        print(f"npc_stat_capture_state_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_stat_capture_state')}")
    if table_exists(conn, "runtime_character_wallet"):
        print(f"character_wallet_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_character_wallet')}")
    if table_exists(conn, "mmo_character_wallet_current"):
        print(f"mmo_character_wallet_current_rows: {scalar(conn, 'SELECT COUNT(*) FROM mmo_character_wallet_current')}")
    if table_exists(conn, "mmo_save_slots"):
        print(f"mmo_save_slots: {scalar(conn, 'SELECT COUNT(*) FROM mmo_save_slots')}")
        print(f"mmo_save_slot_snapshots: {scalar(conn, 'SELECT COUNT(*) FROM mmo_save_slot_snapshots')}")
    print(f"npc_stat_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_stat_history')}")
    if table_exists(conn, "mmo_stat_definitions"):
        print(f"mmo_stat_definition_rows: {scalar(conn, 'SELECT COUNT(*) FROM mmo_stat_definitions')}")
    for table_name in (
        "mmo_unit_stat_current",
        "mmo_unit_stat_sheet_current",
        "mmo_creature_templates_current",
        "mmo_creature_spawns_current",
        "mmo_characters_current",
        "mmo_character_inventory_current",
        "mmo_character_quests_current",
        "mmo_character_known_dialogs_current",
        "mmo_character_story_progress_current",
        "mmo_world_items_current",
        "mmo_world_interactives_current",
        "mmo_world_container_inventory_current",
        "mmo_script_globals_current",
        "mmo_script_global_values_current",
        "mmo_guild_attitudes_current",
        "mmo_world_clock_current",
        "mmo_creature_inventory_current",
        "mmo_creature_inventory_snapshots_current",
        "mmo_creature_relations_current",
        "mmo_world_templates",
        "mmo_world_instances",
        "mmo_world_baseline_creatures",
        "mmo_world_baseline_items",
        "mmo_world_baseline_interactives",
        "mmo_world_baseline_script_globals",
    ):
        if table_exists(conn, table_name):
            print(f"{table_name}_rows: {scalar(conn, f'SELECT COUNT(*) FROM {table_name}')}")
    print(f"npc_ai_state_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_ai_state')}")
    print(f"npc_ai_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_ai_history')}")
    optional_counts = [
        ("waypoint_rows", "runtime_waypoints"),
        ("waypoint_edge_rows", "runtime_waypoint_edges"),
        ("npc_routine_rows", "runtime_npc_routines"),
        ("npc_navigation_rows", "runtime_npc_navigation_state"),
        ("npc_navigation_history_rows", "runtime_npc_navigation_history"),
        ("story_progress_rows", "runtime_story_progress_current"),
        ("story_progress_history_rows", "runtime_story_progress_history"),
        ("chapter_intro_event_rows", "runtime_chapter_intro_events"),
    ]
    for label, table in optional_counts:
        if table_exists(conn, table):
            print(f"{label}: {scalar(conn, f'SELECT COUNT(*) FROM {table}')}")
        else:
            print(f"{label}: missing; run the game once with schema_version 11")
    print(f"quest_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_quests')}")
    print(f"quest_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_quest_history')}")
    print(f"known_dialog_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_known_dialogs')}")
    print(f"dialog_catalog_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_dialog_catalog')}")
    print(f"dialog_choice_snapshots: {scalar(conn, 'SELECT COUNT(*) FROM runtime_dialog_choice_snapshots')}")
    print(f"dialog_choice_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_dialog_choice_rows')}")
    print(f"dialog_selections: {scalar(conn, 'SELECT COUNT(*) FROM runtime_dialog_selections')}")
    print(f"world_item_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_items')}")
    print(f"world_item_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_item_history')}")
    print(f"world_mobsi_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_mobsi')}")
    print(f"world_mobsi_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_mobsi_history')}")
    print(f"world_mobsi_inventory_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_mobsi_inventory')}")
    print(f"script_global_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_script_globals')}")
    if table_exists(conn, "runtime_script_global_values"):
        print(f"script_global_value_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_script_global_values')}")
    else:
        print("script_global_value_rows: missing; run the game once with schema_version 15")
    print(f"script_global_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_script_global_history')}")
    if table_exists(conn, "runtime_guild_attitudes"):
        print(f"guild_attitude_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_guild_attitudes')}")
    else:
        print("guild_attitude_rows: missing; run the game once with schema_version 15")

    print()
    print("Persistence summary")
    print_rows(
        conn,
        """
        SELECT 'characters' AS area, COUNT(*) AS row_count FROM runtime_characters
        UNION ALL SELECT 'character_wallet', COUNT(*) FROM runtime_character_wallet
        UNION ALL SELECT 'character_inventory', COUNT(*) FROM runtime_character_inventory
        UNION ALL SELECT 'world_npcs', COUNT(*) FROM runtime_world_npcs
        UNION ALL SELECT 'world_clock', COUNT(*) FROM runtime_world_clock
        UNION ALL SELECT 'world_npc_inventory', COUNT(*) FROM runtime_world_npc_inventory
        UNION ALL SELECT 'world_items', COUNT(*) FROM runtime_world_items
        UNION ALL SELECT 'world_mobsi', COUNT(*) FROM runtime_world_mobsi
        UNION ALL SELECT 'mobsi_inventory', COUNT(*) FROM runtime_world_mobsi_inventory
        UNION ALL SELECT 'quests', COUNT(*) FROM runtime_quests
        UNION ALL SELECT 'known_dialogs', COUNT(*) FROM runtime_known_dialogs
        UNION ALL SELECT 'story_progress', COUNT(*) FROM runtime_story_progress_current
        UNION ALL SELECT 'script_globals', COUNT(*) FROM runtime_script_globals
        UNION ALL SELECT 'events', COUNT(*) FROM runtime_events
         ORDER BY area
        """,
        "  (none)",
    )

    print()
    print("MMO current table contract")
    print_rows(
        conn,
        """
        SELECT 'unit' AS state_domain, 'mmo_unit_stat_current' AS table_name,
               'unit_stat_rows_current' AS persistence_class, COUNT(*) AS row_count
          FROM mmo_unit_stat_current
        UNION ALL SELECT 'unit', 'mmo_unit_stat_sheet_current', 'unit_stat_sheet_current', COUNT(*) FROM mmo_unit_stat_sheet_current
        UNION ALL SELECT 'content', 'mmo_creature_templates_current', 'content_creature_template_current', COUNT(*) FROM mmo_creature_templates_current
        UNION ALL SELECT 'world', 'mmo_creature_spawns_current', 'world_creature_spawn_current', COUNT(*) FROM mmo_creature_spawns_current
        UNION ALL SELECT 'world', 'mmo_world_clock_current', 'world_clock_current', COUNT(*) FROM mmo_world_clock_current
        UNION ALL SELECT 'world', 'mmo_creature_inventory_current', 'world_creature_inventory_current', COUNT(*) FROM mmo_creature_inventory_current
        UNION ALL SELECT 'world', 'mmo_creature_inventory_snapshots_current', 'world_creature_inventory_snapshot_current', COUNT(*) FROM mmo_creature_inventory_snapshots_current
        UNION ALL SELECT 'world', 'mmo_creature_relations_current', 'world_creature_relation_current', COUNT(*) FROM mmo_creature_relations_current
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
         ORDER BY state_domain, table_name
        """,
        "  (none)",
    )

    if table_exists(conn, "mmo_save_slots"):
        print()
        print("MMO save slots")
        print_rows(
            conn,
            f"""
            SELECT slot_key, display_name, source_slot_path, world_name,
                   tick_count, current_snapshot_id, last_saved_at
              FROM mmo_save_slots
             ORDER BY updated_at DESC
             LIMIT {max(1, args.limit)}
            """,
            "  (none)",
        )

        print()
        print("MMO save slot snapshots")
        print_rows(
            conn,
            f"""
            SELECT snapshot_id, slot_key, display_name, world_name,
                   tick_count, created_at
              FROM mmo_save_slot_snapshots
             ORDER BY snapshot_id DESC
             LIMIT {max(1, args.limit)}
            """,
            "  (none)",
        )

    print()
    print("Story progress")
    print_rows(
        conn,
        f"""
        SELECT character_key, world_name, chapter_number, chapter_key,
               source_symbol_name, updated_at
          FROM mmo_character_story_progress_current
         ORDER BY character_key
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Story progress history")
    print_rows(
        conn,
        f"""
        SELECT tick_count, chapter_before, chapter_after, chapter_key,
               source_symbol_name, created_at
          FROM runtime_story_progress_history
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Chapter intro events")
    print_rows(
        conn,
        f"""
        SELECT tick_count, title, subtitle, image, sound, duration
          FROM runtime_chapter_intro_events
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("MMO restore readiness")
    print_rows(
        conn,
        """
        SELECT 'character_position_stats' AS restore_area, 'implemented' AS engine_restore_status,
               COUNT(*) AS current_rows
          FROM mmo_characters_current
        UNION ALL SELECT 'character_inventory', 'implemented', COUNT(*) FROM mmo_character_inventory_current
        UNION ALL SELECT 'character_wallet', 'implemented', COUNT(*) FROM mmo_character_wallet_current
        UNION ALL SELECT 'character_quests', 'implemented', COUNT(*) FROM mmo_character_quests_current
        UNION ALL SELECT 'character_known_dialogs', 'implemented', COUNT(*) FROM mmo_character_known_dialogs_current
        UNION ALL SELECT 'character_story_progress', 'implemented', COUNT(*) FROM mmo_character_story_progress_current
        UNION ALL SELECT 'world_entities', 'implemented_checkpoint', COUNT(*) FROM mmo_unit_stat_sheet_current
        UNION ALL SELECT 'world_clock', 'implemented', COUNT(*) FROM mmo_world_clock_current
        UNION ALL SELECT 'world_npc_inventory', 'implemented', COUNT(*) FROM mmo_creature_inventory_snapshots_current
        UNION ALL SELECT 'world_items', 'implemented', COUNT(*) FROM mmo_world_items_current
        UNION ALL SELECT 'world_interactives', 'implemented', COUNT(*) FROM mmo_world_interactives_current
        UNION ALL SELECT 'world_container_inventory', 'implemented', COUNT(*) FROM mmo_world_container_inventory_current
        UNION ALL SELECT 'world_script_state', 'implemented', COUNT(*) FROM mmo_script_global_values_current
        UNION ALL SELECT 'guild_attitudes', 'implemented', COUNT(*) FROM mmo_guild_attitudes_current
        UNION ALL SELECT 'world_follow_escort', 'implemented_checkpoint', COUNT(*) FROM mmo_creature_relations_current
        UNION ALL SELECT 'npc_navigation', 'runtime_only', COUNT(*) FROM runtime_npc_navigation_state
         ORDER BY restore_area
        """,
        "  (none)",
    )

    print()
    print("MMO unit stat domains")
    print_rows(
        conn,
        """
        SELECT unit_type, stat_domain, stat_family, COUNT(*) AS rows
          FROM mmo_unit_stat_current
         GROUP BY unit_type, stat_domain, stat_family
         ORDER BY unit_type, stat_domain, stat_family
        """,
        "  (none)",
    )

    print()
    print("MMO character stat sheet")
    print_rows(
        conn,
        """
        SELECT character_key, display_name, level, experience, experience_next,
               learning_points, permanent_attitude, temporary_attitude,
               health_current, health_max, mana_current, mana_max,
               strength, dexterity,
               one_handed_skill, one_handed_hit_chance,
               bow_skill, bow_hit_chance,
               take_animal_trophy_skill
          FROM mmo_unit_stat_sheet_current
         WHERE unit_type='character'
         ORDER BY character_key
        """,
        "  (none)",
    )

    print()
    print("MMO creature templates")
    print_rows(
        conn,
        f"""
        SELECT creature_template_id, display_name, spawn_count,
               min_level, max_level, base_health_max,
               base_strength, base_dexterity, resist_edge, resist_fire
          FROM mmo_creature_templates_current
         ORDER BY spawn_count DESC, display_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Latest character state")
    print_rows(
        conn,
        """
        SELECT character_key, display_name, world_name, tick_count,
               ROUND(pos_x, 2) AS pos_x, ROUND(pos_y, 2) AS pos_y, ROUND(pos_z, 2) AS pos_z,
               hp, hp_max, mana, mana_max, level, experience, updated_at
          FROM runtime_characters
         ORDER BY updated_at DESC
         LIMIT 5
        """,
        "  (none)",
    )

    print()
    print("Recent history")
    print_rows(
        conn,
        f"""
        SELECT character_key, world_name, tick_count,
               ROUND(pos_x, 2) AS pos_x, ROUND(pos_y, 2) AS pos_y, ROUND(pos_z, 2) AS pos_z,
               hp, mana, created_at
          FROM runtime_character_history
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Current inventory")
    print_rows(
        conn,
        """
        SELECT display_name, symbol_index, amount, iterator_count,
               equipped, equip_count, slot, value, spell_id
          FROM runtime_character_inventory
         ORDER BY equipped DESC, display_name, symbol_index, slot
         LIMIT 30
        """,
        "  (none)",
    )

    print()
    print("Recent inventory history ticks")
    print_rows(
        conn,
        f"""
        SELECT tick_count, COUNT(*) AS rows,
               SUM(iterator_count) AS iterator_total,
               SUM(CASE WHEN equipped != 0 THEN iterator_count ELSE 0 END) AS equipped_total
          FROM runtime_character_inventory_history
         GROUP BY tick_count
         ORDER BY tick_count DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Character sheet")
    print_rows(
        conn,
        """
        SELECT c.character_key, c.display_name,
               COALESCE(b.account_key, 'local-account') AS account_key,
               COALESCE(b.realm_key, 'local-g2notr') AS realm_key,
               c.world_name, c.hp, c.hp_max, c.mana, c.mana_max, c.level, c.experience
          FROM runtime_characters c
          LEFT JOIN runtime_character_bindings b ON b.character_key = c.character_key
         ORDER BY c.character_key
        """,
        "  (none)",
    )

    print()
    print("Equipment")
    print_rows(
        conn,
        """
        SELECT character_key, display_name, symbol_index, iterator_count, slot, value
          FROM runtime_character_inventory
         WHERE equipped != 0
         ORDER BY character_key, slot
        """,
        "  (none)",
    )

    print()
    print("Event counts")
    print_rows(
        conn,
        """
        SELECT event_type, COUNT(*) AS count,
               ROUND(SUM(delta), 2) AS delta_sum,
               MIN(tick_count) AS first_tick,
               MAX(tick_count) AS last_tick
          FROM runtime_events
         GROUP BY event_type
         ORDER BY count DESC, event_type
        """,
        "  (none)",
    )

    print()
    print("Recent events")
    print_rows(
        conn,
        f"""
        SELECT id, event_type, subject_key, tick_count,
               ROUND(value_before, 2) AS before,
               ROUND(value_after, 2) AS after,
               ROUND(delta, 2) AS delta,
               data_text,
               created_at
          FROM runtime_events
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("World NPC summary")
    print_rows(
        conn,
        """
        SELECT world_name,
               COUNT(*) AS npcs,
               SUM(CASE WHEN dead != 0 THEN 1 ELSE 0 END) AS dead,
               SUM(CASE WHEN player != 0 THEN 1 ELSE 0 END) AS players,
               SUM(CASE WHEN hp > 0 THEN 1 ELSE 0 END) AS hp_positive
          FROM runtime_world_npcs
         GROUP BY world_name
        """,
        "  (none)",
    )

    print()
    print("Recent NPC history")
    print_rows(
        conn,
        f"""
        SELECT tick_count, display_name, symbol_index, hp, mana, dead, changed_fields
          FROM runtime_world_npc_history
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Quest lifecycle")
    print_rows(
        conn,
        """
        SELECT CASE status
                 WHEN 1 THEN 'in_progress'
                 WHEN 2 THEN 'completed_success'
                 WHEN 3 THEN 'completed_failed'
                 WHEN 4 THEN 'obsolete'
                 ELSE 'unknown'
               END AS lifecycle_state,
               COUNT(*) AS quest_count
          FROM runtime_quests
         GROUP BY lifecycle_state
         ORDER BY lifecycle_state
        """,
        "  (none)",
    )

    print()
    print("Player stats")
    print_rows(
        conn,
        f"""
        SELECT stat_group, stat_key, value, updated_at
          FROM runtime_npc_stats
         WHERE player != 0
         ORDER BY stat_group, stat_key
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("NPC character sheets")
    print_rows(
        conn,
        f"""
        SELECT n.display_name, n.player, n.guild, n.true_guild, n.level, n.experience,
               n.hp, n.hp_max, n.mana, n.mana_max,
               MAX(CASE WHEN s.stat_group='attribute' AND s.stat_key='strength' THEN s.value END) AS strength,
               MAX(CASE WHEN s.stat_group='attribute' AND s.stat_key='dexterity' THEN s.value END) AS dexterity,
               ai.ai_state_name, ai.target_display_name, ai.relation_kind
          FROM runtime_world_npcs n
          LEFT JOIN runtime_npc_stats s ON s.entity_key = n.entity_key
          LEFT JOIN runtime_npc_ai_state ai ON ai.entity_key = n.entity_key
         GROUP BY n.entity_key
         ORDER BY n.player DESC, n.display_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    ai_columns = table_columns(conn, "runtime_npc_ai_state")
    state_other_expr = "state_other_key" if "state_other_key" in ai_columns else "'' AS state_other_key"
    state_victim_expr = "state_victim_key" if "state_victim_key" in ai_columns else "'' AS state_victim_key"

    print()
    print("NPC follow/target relations")
    print_rows(
        conn,
        f"""
        SELECT display_name,
               ai_state_name,
               CASE
                 WHEN target_display_name = '' AND instr(lower(ai_state_name), 'player') > 0
                      AND (instr(lower(ai_state_name), 'follow') > 0
                        OR instr(lower(ai_state_name), 'escort') > 0
                        OR instr(lower(ai_state_name), 'guide') > 0)
                   THEN 'PC_HERO'
                 ELSE target_display_name
               END AS target_display_name,
               target_key,
               {state_other_expr},
               {state_victim_expr},
               CASE
                 WHEN instr(lower(ai_state_name), 'follow') > 0 THEN 'following_target'
                 WHEN instr(lower(ai_state_name), 'escort') > 0
                   OR instr(lower(ai_state_name), 'guide') > 0 THEN 'escort_or_guide'
                 ELSE relation_kind
               END AS relation_kind,
               tick_count
          FROM runtime_npc_ai_state
         WHERE relation_kind != 'none'
            OR instr(lower(ai_state_name), 'follow') > 0
            OR instr(lower(ai_state_name), 'escort') > 0
            OR instr(lower(ai_state_name), 'guide') > 0
         ORDER BY updated_at DESC, display_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    if table_exists(conn, "runtime_waypoints"):
        print()
        print("Waypoint graph")
        print_rows(
            conn,
            """
            SELECT world_name, kind, COUNT(*) AS points,
                   SUM(CASE WHEN connected != 0 THEN 1 ELSE 0 END) AS connected,
                   SUM(CASE WHEN underwater != 0 THEN 1 ELSE 0 END) AS underwater,
                   SUM((SELECT COUNT(*) FROM runtime_waypoint_edges e WHERE e.from_waypoint_key=w.waypoint_key)) AS outgoing_edges
              FROM runtime_waypoints w
             GROUP BY world_name, kind
             ORDER BY world_name, kind
            """,
            "  (none)",
        )

        print()
        print("Waypoint users")
        print_rows(
            conn,
            f"""
            SELECT 'current' AS usage_kind,
                   n.current_waypoint_name AS waypoint_name,
                   n.display_name,
                   n.move_hint,
                   ai.ai_state_name,
                   ai.relation_kind
              FROM runtime_npc_navigation_state n
              LEFT JOIN runtime_npc_ai_state ai ON ai.entity_key = n.entity_key
             WHERE n.current_waypoint_key != ''
            UNION ALL
            SELECT 'routine', n.routine_waypoint_name, n.display_name, n.move_hint,
                   ai.ai_state_name, ai.relation_kind
              FROM runtime_npc_navigation_state n
              LEFT JOIN runtime_npc_ai_state ai ON ai.entity_key = n.entity_key
             WHERE n.routine_waypoint_key != ''
            UNION ALL
            SELECT 'move_target', n.move_target_waypoint_name, n.display_name, n.move_hint,
                   ai.ai_state_name, ai.relation_kind
              FROM runtime_npc_navigation_state n
              LEFT JOIN runtime_npc_ai_state ai ON ai.entity_key = n.entity_key
             WHERE n.move_target_waypoint_key != ''
             ORDER BY 1, 2, 3
             LIMIT {max(1, args.limit)}
            """,
            "  (none)",
        )

    if table_exists(conn, "runtime_npc_navigation_state"):
        print()
        print("NPC navigation")
        print_rows(
            conn,
            f"""
            SELECT n.display_name, current_waypoint_name, routine_waypoint_name,
                   move_hint, move_target_waypoint_name, path_next_waypoint_name,
                   path_remaining_count, ai.ai_state_name, ai.relation_kind
              FROM runtime_npc_navigation_state n
              LEFT JOIN runtime_npc_ai_state ai ON ai.entity_key = n.entity_key
             ORDER BY n.display_name
             LIMIT {max(1, args.limit)}
            """,
            "  (none)",
        )

        print()
        print("Recent NPC navigation changes")
        print_rows(
            conn,
            f"""
            SELECT tick_count, display_name, current_waypoint_name, routine_waypoint_name,
                   move_hint, move_target_waypoint_name, path_next_waypoint_name,
                   path_remaining_count, changed_fields
              FROM runtime_npc_navigation_history
             ORDER BY id DESC
             LIMIT {max(1, args.limit)}
            """,
            "  (none)",
        )

    if table_exists(conn, "runtime_npc_routines"):
        print()
        print("NPC routine schedule")
        print_rows(
            conn,
            f"""
            SELECT display_name, routine_index,
                   printf('%02d:%02d', start_minute / 60, start_minute % 60) AS start_time,
                   printf('%02d:%02d', end_minute / 60, end_minute % 60) AS end_time,
                   callback_symbol_name, waypoint_name, active
              FROM runtime_npc_routines
             ORDER BY display_name, start_minute, routine_index
             LIMIT {max(1, args.limit)}
            """,
            "  (none)",
        )

    print()
    print("Recent NPC stat changes")
    print_rows(
        conn,
        f"""
        SELECT tick_count, display_name, stat_group, stat_key, value_before, value_after
          FROM runtime_npc_stat_history
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Quests")
    print_rows(
        conn,
        """
        SELECT name,
               CASE section
                 WHEN 0 THEN 'mission'
                 WHEN 1 THEN 'note'
                 ELSE 'unknown'
               END AS section_label,
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
               updated_at
          FROM runtime_quests
         ORDER BY section, status, name
         LIMIT 30
        """,
        "  (none)",
    )

    print()
    print("Dialog availability")
    print_rows(
        conn,
        """
        SELECT CASE
                 WHEN k.info_symbol_index IS NOT NULL AND c.permanent = 0 THEN 'consumed_hidden'
                 WHEN k.info_symbol_index IS NOT NULL AND c.permanent != 0 THEN 'repeatable_known'
                 WHEN k.info_symbol_index IS NULL AND c.permanent != 0 THEN 'repeatable_not_seen'
                 ELSE 'one_shot_not_seen'
               END AS availability_state,
               COUNT(*) AS dialog_count
          FROM runtime_dialog_catalog c
          LEFT JOIN runtime_known_dialogs k ON k.info_symbol_index = c.info_symbol_index
         GROUP BY availability_state
         ORDER BY availability_state
        """,
        "  (none)",
    )

    print()
    print("Known dialogs")
    print_rows(
        conn,
        f"""
        SELECT c.npc_symbol_name,
               c.info_symbol_name,
               c.description,
               c.permanent,
               CASE
                 WHEN c.permanent = 0 THEN 'consumed_hidden'
                 ELSE 'repeatable_known'
               END AS availability_state,
               k.first_seen_tick
          FROM runtime_known_dialogs k
          LEFT JOIN runtime_dialog_catalog c ON c.info_symbol_index = k.info_symbol_index
         ORDER BY k.first_seen_tick DESC, c.info_symbol_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Consumed one-shot dialogs")
    print_rows(
        conn,
        f"""
        SELECT c.npc_symbol_name, c.info_symbol_name, c.description, k.first_seen_tick
          FROM runtime_known_dialogs k
          LEFT JOIN runtime_dialog_catalog c ON c.info_symbol_index = k.info_symbol_index
         WHERE c.permanent = 0
         ORDER BY k.first_seen_tick DESC, c.info_symbol_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Repeatable known dialogs")
    print_rows(
        conn,
        f"""
        SELECT c.npc_symbol_name, c.info_symbol_name, c.description, k.first_seen_tick
          FROM runtime_known_dialogs k
          LEFT JOIN runtime_dialog_catalog c ON c.info_symbol_index = k.info_symbol_index
         WHERE c.permanent != 0
         ORDER BY k.first_seen_tick DESC, c.info_symbol_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Recent dialog choices shown")
    print_rows(
        conn,
        f"""
        SELECT tick_count, npc_display_name, phase, choice_index, title, info_symbol_name, permanent
          FROM (
            SELECT s.id AS snapshot_id,
                   s.tick_count,
                   s.npc_display_name,
                   s.phase,
                   r.choice_index,
                   r.title,
                   r.info_symbol_name,
                   r.permanent
              FROM runtime_dialog_choice_snapshots s
              LEFT JOIN runtime_dialog_choice_rows r ON r.snapshot_id = s.id
          )
         ORDER BY snapshot_id DESC, choice_index
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Recent dialog selections")
    print_rows(
        conn,
        f"""
        SELECT tick_count, npc_display_name, phase, title, info_symbol_name, permanent
          FROM runtime_dialog_selections
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("World item summary")
    print_rows(
        conn,
        """
        SELECT world_name, COUNT(*) AS items, SUM(amount) AS amount_total
          FROM runtime_world_items
         GROUP BY world_name
        """,
        "  (none)",
    )

    print()
    print("Recent world item history")
    print_rows(
        conn,
        f"""
        SELECT tick_count, display_name, symbol_index, amount, changed_fields
          FROM runtime_world_item_history
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Mobsi summary")
    print_rows(
        conn,
        """
        SELECT world_name, COUNT(*) AS mobsi,
               SUM(container) AS containers,
               SUM(door) AS doors,
               SUM(ladder) AS ladders,
               SUM(CASE WHEN locked != 0 THEN 1 ELSE 0 END) AS locked
          FROM runtime_world_mobsi
         GROUP BY world_name
        """,
        "  (none)",
    )

    print()
    print("Mobsi inventory")
    print_rows(
        conn,
        f"""
        SELECT owner_display_name, display_name, symbol_index, iterator_count, value
          FROM runtime_world_mobsi_inventory
         ORDER BY owner_display_name, display_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Script global categories")
    print_rows(
        conn,
        """
        SELECT category, COUNT(*) AS globals
          FROM runtime_script_globals
         GROUP BY category
         ORDER BY globals DESC, category
        """,
        "  (none)",
    )

    print()
    print("Recent script global changes")
    print_rows(
        conn,
        f"""
        SELECT tick_count, category, symbol_name, value_before, value_after
          FROM runtime_script_global_history
         ORDER BY id DESC
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
