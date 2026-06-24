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


def scalar(conn: sqlite3.Connection, sql: str, params=(), default=0):
    row = conn.execute(sql, params).fetchone()
    return default if row is None else row[0]


def rows(conn: sqlite3.Connection, sql: str, params=()):
    return conn.execute(sql, params).fetchall()


def report(level: str, message: str) -> None:
    print(f"{level}: {message}")


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
    npc_stat_history = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_stat_history")
    npc_ai_state = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_ai_state")
    npc_ai_history = scalar(conn, "SELECT COUNT(*) FROM runtime_npc_ai_history")
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
    report("OK" if npc_stat_history >= 0 else "WARN", f"runtime NPC stat history rows={npc_stat_history}")
    report("OK" if npc_ai_state > 0 else "WARN", f"runtime NPC AI state rows={npc_ai_state}")
    report("OK" if npc_ai_history >= 0 else "WARN", f"runtime NPC AI history rows={npc_ai_history}")

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

    missing_expected = [
        name
        for name in ("item_added", "item_quantity_changed", "character_moved")
        if scalar(conn, "SELECT COUNT(*) FROM runtime_events WHERE event_type=?", (name,)) == 0
    ]
    if missing_expected:
        report("WARN", "missing expected event classes: " + ", ".join(missing_expected))
    else:
        report("OK", "basic event classes are present")

    removed = scalar(conn, "SELECT COUNT(*) FROM runtime_events WHERE event_type='item_removed'")
    if removed == 0:
        report("INFO", "no item_removed events in this run; test drop/consume/sell next")

    print()
    print("Useful SQL")
    print("  SELECT * FROM v_runtime_persistence_summary ORDER BY area;")
    print("  SELECT * FROM v_runtime_character_sheet;")
    print("  SELECT * FROM v_runtime_character_equipment;")
    print("  SELECT * FROM v_runtime_world_population;")
    print("  SELECT * FROM v_runtime_event_counts ORDER BY event_count DESC;")
    print("  SELECT event_type, COUNT(*) FROM runtime_events GROUP BY event_type ORDER BY COUNT(*) DESC;")
    print("  SELECT * FROM runtime_events ORDER BY id DESC LIMIT 50;")
    print("  SELECT display_name, symbol_index, iterator_count, equipped, slot FROM runtime_character_inventory ORDER BY equipped DESC, display_name;")
    print("  SELECT tick_count, COUNT(*), SUM(iterator_count) FROM runtime_character_inventory_history GROUP BY tick_count ORDER BY tick_count DESC LIMIT 30;")
    print("  SELECT display_name, symbol_index, hp, hp_max, dead, pos_x, pos_y, pos_z FROM runtime_world_npcs ORDER BY dead DESC, display_name LIMIT 50;")
    print("  SELECT display_name, stat_group, stat_key, value FROM v_runtime_player_stats ORDER BY stat_group, stat_key;")
    print("  SELECT display_name, ai_state_name, target_display_name, relation_kind FROM v_runtime_npc_follow_relations ORDER BY updated_at DESC LIMIT 50;")
    print("  SELECT display_name, stat_group, stat_key, value_before, value_after FROM runtime_npc_stat_history ORDER BY id DESC LIMIT 50;")
    print("  SELECT name, status, entry_count FROM runtime_quests ORDER BY status, name;")
    print("  SELECT lifecycle_state, quest_count FROM v_runtime_quest_lifecycle ORDER BY lifecycle_state;")
    print("  SELECT npc_symbol_name, info_symbol_name FROM runtime_known_dialogs ORDER BY first_seen_tick DESC LIMIT 50;")
    print("  SELECT availability_state, dialog_count FROM v_runtime_dialog_availability ORDER BY availability_state;")
    print("  SELECT npc_symbol_name, description, permanent, known, availability_state FROM v_runtime_dialog_state WHERE availability_state IN ('consumed_hidden','repeatable_known') ORDER BY npc_symbol_name, sort_order LIMIT 50;")
    print("  SELECT tick_count, npc_display_name, phase, choice_index, title FROM v_runtime_dialog_choice_timeline ORDER BY snapshot_id DESC, choice_index LIMIT 50;")
    print("  SELECT tick_count, npc_display_name, phase, title, info_symbol_name FROM v_runtime_dialog_selection_timeline ORDER BY id DESC LIMIT 50;")
    print("  SELECT display_name, symbol_index, amount, pos_x, pos_y, pos_z FROM runtime_world_items ORDER BY display_name LIMIT 50;")
    print("  SELECT display_name, state, container, door, locked, cracked FROM runtime_world_mobsi ORDER BY container DESC, door DESC, display_name LIMIT 50;")
    print("  SELECT owner_display_name, display_name, iterator_count FROM runtime_world_mobsi_inventory ORDER BY owner_display_name, display_name LIMIT 50;")
    print("  SELECT category, COUNT(*) FROM runtime_script_globals GROUP BY category ORDER BY COUNT(*) DESC;")
    print("  SELECT category, symbol_name, value_before, value_after FROM runtime_script_global_history ORDER BY id DESC LIMIT 50;")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
