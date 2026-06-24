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
    print(f"world_npc_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_world_npc_history')}")
    print(f"npc_stat_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_stats')}")
    print(f"npc_stat_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_stat_history')}")
    print(f"npc_ai_state_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_ai_state')}")
    print(f"npc_ai_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_npc_ai_history')}")
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
    print(f"script_global_history_rows: {scalar(conn, 'SELECT COUNT(*) FROM runtime_script_global_history')}")

    print()
    print("Persistence summary")
    print_rows(
        conn,
        """
        SELECT area, row_count
          FROM v_runtime_persistence_summary
         ORDER BY area
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
    print("Character sheet view")
    print_rows(
        conn,
        """
        SELECT character_key, display_name, account_key, realm_key, world_name,
               hp, hp_max, mana, mana_max, level, experience
          FROM v_runtime_character_sheet
         ORDER BY character_key
        """,
        "  (none)",
    )

    print()
    print("Equipment view")
    print_rows(
        conn,
        """
        SELECT character_key, display_name, symbol_index, iterator_count, slot, value
          FROM v_runtime_character_equipment
         ORDER BY character_key, slot
        """,
        "  (none)",
    )

    print()
    print("Event counts")
    print_rows(
        conn,
        """
        SELECT event_type, event_count AS count,
               ROUND(delta_sum, 2) AS delta_sum,
               first_tick, last_tick
          FROM v_runtime_event_counts
         ORDER BY event_count DESC, event_type
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
        SELECT lifecycle_state, quest_count
          FROM v_runtime_quest_lifecycle
         ORDER BY lifecycle_state
        """,
        "  (none)",
    )

    print()
    print("Player stats")
    print_rows(
        conn,
        """
        SELECT stat_group, stat_key, value, updated_at
          FROM v_runtime_player_stats
         ORDER BY stat_group, stat_key
        """,
        "  (none)",
    )

    print()
    print("NPC character sheets")
    print_rows(
        conn,
        f"""
        SELECT display_name, player, guild, true_guild, level, experience,
               hp, hp_max, mana, mana_max, strength, dexterity,
               ai_state_name, target_display_name, relation_kind
          FROM v_runtime_npc_character_sheet
         ORDER BY player DESC, display_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("NPC follow/target relations")
    print_rows(
        conn,
        f"""
        SELECT display_name, ai_state_name, target_display_name, relation_kind, tick_count
          FROM v_runtime_npc_follow_relations
         ORDER BY updated_at DESC, display_name
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
        SELECT name, section_label, status_label, lifecycle_state, entry_count, updated_at
          FROM v_runtime_quest_state
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
        SELECT availability_state, dialog_count
          FROM v_runtime_dialog_availability
         ORDER BY availability_state
        """,
        "  (none)",
    )

    print()
    print("Known dialogs")
    print_rows(
        conn,
        f"""
        SELECT npc_symbol_name, info_symbol_name, description, permanent, availability_state, first_seen_tick
          FROM v_runtime_dialog_state
         WHERE known != 0
         ORDER BY first_seen_tick DESC, info_symbol_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Consumed one-shot dialogs")
    print_rows(
        conn,
        f"""
        SELECT npc_symbol_name, info_symbol_name, description, first_seen_tick
          FROM v_runtime_dialog_state
         WHERE availability_state = 'consumed_hidden'
         ORDER BY first_seen_tick DESC, info_symbol_name
         LIMIT {max(1, args.limit)}
        """,
        "  (none)",
    )

    print()
    print("Repeatable known dialogs")
    print_rows(
        conn,
        f"""
        SELECT npc_symbol_name, info_symbol_name, description, first_seen_tick
          FROM v_runtime_dialog_state
         WHERE availability_state = 'repeatable_known'
         ORDER BY first_seen_tick DESC, info_symbol_name
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
          FROM v_runtime_dialog_choice_timeline
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
          FROM v_runtime_dialog_selection_timeline
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
