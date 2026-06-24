#!/usr/bin/env python3
"""
Import OpenGothic world JSONL dumps into a local SQLite staging database.

This is intentionally a staging importer, not the final production schema.
It gives the MMO work a concrete DB target for validating baseline entities,
player/script state, and normalized delta events.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
import sys
from pathlib import Path
from typing import Any, Iterable


BASELINE_FILES = {
    "npcs.jsonl": "npc",
    "npc_stats.jsonl": "npc_stats",
    "items.jsonl": "item",
    "npc_inventory.jsonl": "npc_inventory",
    "mobsi.jsonl": "mobsi",
    "mobsi_inventory.jsonl": "mobsi_inventory",
    "quests.jsonl": "quest",
    "known_dialogs.jsonl": "known_dialog",
    "script_globals.jsonl": "script_global",
}


PLAYER_NAMES = {"Ja", "Hero", "PC_HERO"}


def read_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8-sig") as f:
        return json.load(f)


def read_text_with_fallback(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "cp1250", "latin-1"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def iter_jsonl(path: Path) -> Iterable[dict[str, Any]]:
    if not path.exists():
        return
    for line_no, line in enumerate(read_text_with_fallback(path).splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            yield json.loads(line)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"{path}:{line_no}: invalid JSONL: {exc}") from exc


def json_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def stable_hash(path: Path) -> str:
    h = hashlib.sha256()
    if path.exists():
        with path.open("rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                h.update(chunk)
    return h.hexdigest()


def get_value(row: dict[str, Any], *names: str) -> Any:
    for name in names:
        if name in row:
            return row[name]
    return None


def read_jsonl_list(directory: Path | None, file_name: str) -> list[dict[str, Any]]:
    if directory is None:
        return []
    return list(iter_jsonl(directory / file_name) or ())


def rows_by_key(rows: Iterable[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    ret: dict[str, dict[str, Any]] = {}
    for row in rows:
        key = str(row.get("stable_key") or "")
        if key:
            ret[key] = row
    return ret


def is_player_row(row: dict[str, Any]) -> bool:
    return bool(row.get("player")) or str(row.get("display_name") or row.get("owner_display_name") or "") in PLAYER_NAMES


def classify_script_global(row: dict[str, Any]) -> str:
    category = str(row.get("category") or "")
    symbol = str(row.get("symbol_name") or "").upper()
    if category in {"dialog", "quest", "knowledge", "reward"}:
        return "character"
    if symbol.startswith(("DIA_", "MIS_", "LOG_", "B_GIVEPLAYERXP", "XP_", "TA_READ_")):
        return "character"
    return "world"


def event_class(event_type: str | None) -> str:
    name = event_type or ""
    if name.startswith("player_"):
        return "character"
    if name.startswith("quest_") or name.startswith("known_dialog_"):
        return "character_progression"
    if name.startswith("script_global_"):
        return "script"
    if name.startswith("container_") or name.startswith("mobsi_"):
        return "world_interaction"
    if name.startswith("npc_"):
        return "creature"
    if name.startswith("world_item_"):
        return "world_item"
    return "world"


def merge_dict(base: dict[str, Any], patch: dict[str, Any] | None) -> dict[str, Any]:
    ret = dict(base)
    if patch:
        ret.update(patch)
    return ret


def event_item(event: dict[str, Any]) -> dict[str, Any]:
    item = event.get("item")
    return dict(item) if isinstance(item, dict) else {}


def event_after(event: dict[str, Any]) -> dict[str, Any]:
    after = event.get("after")
    return dict(after) if isinstance(after, dict) else {}


def event_before(event: dict[str, Any]) -> dict[str, Any]:
    before = event.get("before")
    return dict(before) if isinstance(before, dict) else {}


def create_schema(db: sqlite3.Connection) -> None:
    db.executescript(
        """
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS import_runs (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
          baseline_dir TEXT NOT NULL,
          snapshot_dir TEXT,
          target TEXT,
          world TEXT,
          schema_version INTEGER,
          baseline_hash TEXT,
          snapshot_hash TEXT
        );

        CREATE TABLE IF NOT EXISTS worlds (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          target TEXT,
          world TEXT,
          game INTEGER,
          patch INTEGER,
          baseline_kind TEXT,
          snapshot_kind TEXT,
          baseline_tick INTEGER,
          snapshot_tick INTEGER,
          baseline_time_day_millis INTEGER,
          snapshot_time_day_millis INTEGER
        );

        CREATE TABLE IF NOT EXISTS baseline_entities (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          file_name TEXT NOT NULL,
          entity_type TEXT NOT NULL,
          stable_key TEXT NOT NULL,
          world TEXT,
          source_slot_id INTEGER,
          persistent_id INTEGER,
          owner_stable_key TEXT,
          owner_persistent_id INTEGER,
          symbol_index INTEGER,
          script_id INTEGER,
          display_name TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(import_run_id, file_name, stable_key)
        );

        CREATE TABLE IF NOT EXISTS baseline_npc_state (
          entity_id INTEGER PRIMARY KEY REFERENCES baseline_entities(id) ON DELETE CASCADE,
          guild INTEGER,
          true_guild INTEGER,
          hp INTEGER,
          hp_max INTEGER,
          mana INTEGER,
          mana_max INTEGER,
          level INTEGER,
          dead INTEGER,
          player INTEGER,
          waypoint TEXT
        );

        CREATE TABLE IF NOT EXISTS baseline_item_state (
          entity_id INTEGER PRIMARY KEY REFERENCES baseline_entities(id) ON DELETE CASCADE,
          amount INTEGER,
          value INTEGER,
          main_flag INTEGER,
          flags INTEGER,
          material INTEGER,
          visual TEXT
        );

        CREATE TABLE IF NOT EXISTS baseline_inventory_items (
          entity_id INTEGER PRIMARY KEY REFERENCES baseline_entities(id) ON DELETE CASCADE,
          owner_stable_key TEXT,
          owner_persistent_id INTEGER,
          owner_display_name TEXT,
          item_symbol_index INTEGER,
          amount INTEGER,
          iterator_count INTEGER,
          equipped INTEGER,
          slot INTEGER
        );

        CREATE TABLE IF NOT EXISTS baseline_quests (
          entity_id INTEGER PRIMARY KEY REFERENCES baseline_entities(id) ON DELETE CASCADE,
          name TEXT,
          section INTEGER,
          status INTEGER,
          entry_count INTEGER,
          entries_json TEXT
        );

        CREATE TABLE IF NOT EXISTS baseline_script_globals (
          entity_id INTEGER PRIMARY KEY REFERENCES baseline_entities(id) ON DELETE CASCADE,
          symbol_index INTEGER,
          symbol_name TEXT,
          value_type TEXT,
          category TEXT,
          values_json TEXT
        );

        CREATE TABLE IF NOT EXISTS world_delta_events (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          event_index INTEGER NOT NULL,
          event_type TEXT NOT NULL,
          entity_type TEXT,
          stable_key TEXT,
          name TEXT,
          payload_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS import_validation (
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          metric TEXT NOT NULL,
          expected INTEGER,
          actual INTEGER,
          status TEXT NOT NULL,
          PRIMARY KEY(import_run_id, metric)
        );

        CREATE INDEX IF NOT EXISTS idx_baseline_entities_type
          ON baseline_entities(import_run_id, entity_type);
        CREATE INDEX IF NOT EXISTS idx_delta_events_type
          ON world_delta_events(import_run_id, event_type);
        CREATE INDEX IF NOT EXISTS idx_delta_events_stable_key
          ON world_delta_events(import_run_id, stable_key);
        """
    )


def create_mmo_schema(db: sqlite3.Connection) -> None:
    db.executescript(
        """
        CREATE TABLE IF NOT EXISTS mmo_game_targets (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          code TEXT NOT NULL,
          game INTEGER,
          patch INTEGER,
          schema_version INTEGER,
          content_hash TEXT,
          raw_manifest_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_world_templates (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          game_target_id INTEGER NOT NULL REFERENCES mmo_game_targets(id) ON DELETE CASCADE,
          world_name TEXT NOT NULL,
          baseline_tick INTEGER,
          baseline_time_day_millis INTEGER,
          baseline_hash TEXT,
          raw_manifest_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_world_instances (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_template_id INTEGER NOT NULL REFERENCES mmo_world_templates(id) ON DELETE CASCADE,
          shard_name TEXT NOT NULL,
          state_source TEXT NOT NULL,
          snapshot_tick INTEGER,
          snapshot_time_day_millis INTEGER,
          snapshot_hash TEXT,
          raw_manifest_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_entity_templates (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_template_id INTEGER NOT NULL REFERENCES mmo_world_templates(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          entity_type TEXT NOT NULL,
          display_name TEXT,
          symbol_index INTEGER,
          persistent_id INTEGER,
          source_file TEXT NOT NULL,
          raw_json TEXT NOT NULL,
          UNIQUE(import_run_id, source_file, stable_key)
        );

        CREATE TABLE IF NOT EXISTS mmo_item_definitions (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          symbol_index INTEGER,
          name TEXT,
          display_name TEXT,
          visual TEXT,
          value INTEGER,
          flags INTEGER,
          material INTEGER,
          sample_raw_json TEXT NOT NULL,
          UNIQUE(import_run_id, symbol_index, name, visual)
        );

        CREATE TABLE IF NOT EXISTS mmo_world_entities (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          entity_type TEXT NOT NULL,
          display_name TEXT,
          symbol_index INTEGER,
          persistent_id INTEGER,
          hp INTEGER,
          mana INTEGER,
          dead INTEGER,
          mob_state INTEGER,
          locked INTEGER,
          amount INTEGER,
          position_json TEXT,
          stats_json TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(import_run_id, world_instance_id, entity_type, stable_key)
        );

        CREATE TABLE IF NOT EXISTS mmo_characters (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          persistent_id INTEGER,
          character_kind TEXT NOT NULL,
          name TEXT,
          hp INTEGER,
          hp_max INTEGER,
          mana INTEGER,
          mana_max INTEGER,
          level INTEGER,
          experience INTEGER,
          learning_points INTEGER,
          attributes_json TEXT,
          talents_json TEXT,
          position_json TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(import_run_id, world_instance_id, stable_key)
        );

        CREATE TABLE IF NOT EXISTS mmo_inventory (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          owner_scope TEXT NOT NULL,
          owner_stable_key TEXT,
          owner_persistent_id INTEGER,
          owner_display_name TEXT,
          item_stable_key TEXT NOT NULL,
          item_symbol_index INTEGER,
          item_name TEXT,
          item_display_name TEXT,
          amount INTEGER,
          iterator_count INTEGER,
          equipped INTEGER,
          slot INTEGER,
          source_file TEXT NOT NULL,
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_quest_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          character_stable_key TEXT,
          stable_key TEXT NOT NULL,
          name TEXT,
          section INTEGER,
          status INTEGER,
          entry_count INTEGER,
          entries_json TEXT,
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_known_dialog_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          character_stable_key TEXT,
          stable_key TEXT NOT NULL,
          npc_symbol_name TEXT,
          info_symbol_name TEXT,
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_script_global_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          scope TEXT NOT NULL,
          character_stable_key TEXT,
          stable_key TEXT NOT NULL,
          symbol_index INTEGER,
          symbol_name TEXT,
          value_type TEXT,
          category TEXT,
          values_json TEXT,
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_event_ledger (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          event_index INTEGER NOT NULL,
          event_type TEXT NOT NULL,
          event_class TEXT NOT NULL,
          entity_type TEXT,
          stable_key TEXT,
          name TEXT,
          payload_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_replay_entities (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          entity_type TEXT NOT NULL,
          display_name TEXT,
          symbol_index INTEGER,
          persistent_id INTEGER,
          hp INTEGER,
          mana INTEGER,
          dead INTEGER,
          mob_state INTEGER,
          locked INTEGER,
          amount INTEGER,
          position_json TEXT,
          stats_json TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(import_run_id, world_instance_id, entity_type, stable_key)
        );

        CREATE TABLE IF NOT EXISTS mmo_replay_inventory (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          owner_scope TEXT NOT NULL,
          owner_stable_key TEXT,
          owner_persistent_id INTEGER,
          owner_display_name TEXT,
          item_stable_key TEXT NOT NULL,
          item_symbol_index INTEGER,
          item_name TEXT,
          item_display_name TEXT,
          amount INTEGER,
          iterator_count INTEGER,
          equipped INTEGER,
          slot INTEGER,
          source_file TEXT NOT NULL,
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_replay_quest_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          character_stable_key TEXT,
          stable_key TEXT NOT NULL,
          name TEXT,
          section INTEGER,
          status INTEGER,
          entry_count INTEGER,
          entries_json TEXT,
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_replay_known_dialog_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          character_stable_key TEXT,
          stable_key TEXT NOT NULL,
          npc_symbol_name TEXT,
          info_symbol_name TEXT,
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_replay_script_global_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          scope TEXT NOT NULL,
          character_stable_key TEXT,
          stable_key TEXT NOT NULL,
          symbol_index INTEGER,
          symbol_name TEXT,
          value_type TEXT,
          category TEXT,
          values_json TEXT,
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS mmo_replay_validation (
          import_run_id INTEGER NOT NULL REFERENCES import_runs(id) ON DELETE CASCADE,
          world_instance_id INTEGER NOT NULL REFERENCES mmo_world_instances(id) ON DELETE CASCADE,
          metric TEXT NOT NULL,
          snapshot_count INTEGER,
          replay_count INTEGER,
          status TEXT NOT NULL,
          PRIMARY KEY(import_run_id, world_instance_id, metric)
        );

        CREATE INDEX IF NOT EXISTS idx_mmo_world_entities_type
          ON mmo_world_entities(import_run_id, world_instance_id, entity_type);
        CREATE INDEX IF NOT EXISTS idx_mmo_inventory_owner
          ON mmo_inventory(import_run_id, world_instance_id, owner_scope, owner_stable_key);
        CREATE INDEX IF NOT EXISTS idx_mmo_event_ledger_type
          ON mmo_event_ledger(import_run_id, world_instance_id, event_type);
        CREATE INDEX IF NOT EXISTS idx_mmo_script_global_scope
          ON mmo_script_global_state(import_run_id, world_instance_id, scope, category);
        CREATE INDEX IF NOT EXISTS idx_mmo_replay_entities_type
          ON mmo_replay_entities(import_run_id, world_instance_id, entity_type);
        CREATE INDEX IF NOT EXISTS idx_mmo_replay_inventory_owner
          ON mmo_replay_inventory(import_run_id, world_instance_id, owner_scope, owner_stable_key);

        CREATE VIEW IF NOT EXISTS v_mmo_event_counts AS
          SELECT import_run_id, world_instance_id, event_class, event_type, COUNT(*) AS event_count
          FROM mmo_event_ledger
          GROUP BY import_run_id, world_instance_id, event_class, event_type;

        CREATE VIEW IF NOT EXISTS v_mmo_player_inventory AS
          SELECT import_run_id, world_instance_id, owner_display_name, item_display_name,
                 item_name, amount, iterator_count, equipped, slot, item_stable_key
          FROM mmo_inventory
          WHERE owner_scope = 'character';

        CREATE VIEW IF NOT EXISTS v_mmo_dead_npcs AS
          SELECT import_run_id, world_instance_id, stable_key, display_name, persistent_id, hp
          FROM mmo_world_entities
          WHERE entity_type = 'npc' AND dead = 1;

        CREATE VIEW IF NOT EXISTS v_mmo_delta_killed_npcs AS
          SELECT import_run_id, world_instance_id, event_index, stable_key, name, payload_json
          FROM mmo_event_ledger
          WHERE event_type = 'npc_killed';

        CREATE VIEW IF NOT EXISTS v_mmo_replay_player_inventory AS
          SELECT import_run_id, world_instance_id, owner_display_name, item_display_name,
                 item_name, amount, iterator_count, equipped, slot, item_stable_key
          FROM mmo_replay_inventory
          WHERE owner_scope = 'character';

        CREATE VIEW IF NOT EXISTS v_mmo_replay_delta AS
          SELECT import_run_id, world_instance_id, metric, snapshot_count, replay_count, status
          FROM mmo_replay_validation;

        CREATE VIEW IF NOT EXISTS v_mmo_replay_inventory_missing AS
          SELECT s.import_run_id, s.world_instance_id, s.owner_scope, s.owner_display_name,
                 s.item_display_name, s.item_name, s.amount, s.item_stable_key, s.source_file
          FROM mmo_inventory s
          LEFT JOIN mmo_replay_inventory r
            ON r.import_run_id = s.import_run_id
           AND r.world_instance_id = s.world_instance_id
           AND r.item_stable_key = s.item_stable_key
          WHERE r.id IS NULL;

        CREATE VIEW IF NOT EXISTS v_mmo_replay_inventory_extra AS
          SELECT r.import_run_id, r.world_instance_id, r.owner_scope, r.owner_display_name,
                 r.item_display_name, r.item_name, r.amount, r.item_stable_key, r.source_file
          FROM mmo_replay_inventory r
          LEFT JOIN mmo_inventory s
            ON s.import_run_id = r.import_run_id
           AND s.world_instance_id = r.world_instance_id
           AND s.item_stable_key = r.item_stable_key
          WHERE s.id IS NULL;

        CREATE VIEW IF NOT EXISTS v_mmo_character_progress AS
          SELECT c.import_run_id, c.world_instance_id, c.name, c.level, c.experience,
                 c.learning_points, c.hp, c.hp_max, c.mana, c.mana_max,
                 COUNT(DISTINCT q.stable_key) AS quest_count,
                 COUNT(DISTINCT d.stable_key) AS known_dialog_count
          FROM mmo_characters c
          LEFT JOIN mmo_quest_state q
            ON q.import_run_id = c.import_run_id
           AND q.world_instance_id = c.world_instance_id
           AND (q.character_stable_key = c.stable_key OR q.character_stable_key IS NULL)
          LEFT JOIN mmo_known_dialog_state d
            ON d.import_run_id = c.import_run_id
           AND d.world_instance_id = c.world_instance_id
           AND (d.character_stable_key = c.stable_key OR d.character_stable_key IS NULL)
          GROUP BY c.id;
        """
    )


def insert_entity(
    db: sqlite3.Connection,
    run_id: int,
    file_name: str,
    entity_type: str,
    row: dict[str, Any],
) -> int:
    stable_key = str(row.get("stable_key") or "")
    if not stable_key:
        raise ValueError(f"{file_name}: row without stable_key")

    cur = db.execute(
        """
        INSERT INTO baseline_entities (
          import_run_id, file_name, entity_type, stable_key, world,
          source_slot_id, persistent_id, owner_stable_key, owner_persistent_id,
          symbol_index, script_id, display_name, raw_json
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            run_id,
            file_name,
            entity_type,
            stable_key,
            row.get("world"),
            get_value(row, "slot_id", "owner_slot_id"),
            get_value(row, "persistent_id"),
            row.get("owner_stable_key"),
            get_value(row, "owner_persistent_id"),
            get_value(row, "symbol_index", "owner_symbol_index", "npc_symbol_index"),
            get_value(row, "script_id"),
            get_value(row, "display_name", "owner_display_name", "name", "symbol_name"),
            json_text(row),
        ),
    )
    return int(cur.lastrowid)


def insert_typed_state(db: sqlite3.Connection, entity_id: int, entity_type: str, row: dict[str, Any]) -> None:
    if entity_type == "npc":
        db.execute(
            """
            INSERT INTO baseline_npc_state
              (entity_id, guild, true_guild, hp, hp_max, mana, mana_max, level, dead, player, waypoint)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                entity_id,
                row.get("guild"),
                row.get("true_guild"),
                row.get("hp"),
                row.get("hp_max"),
                row.get("mana"),
                row.get("mana_max"),
                row.get("level"),
                int(bool(row.get("dead"))) if row.get("dead") is not None else None,
                int(bool(row.get("player"))) if row.get("player") is not None else None,
                row.get("waypoint"),
            ),
        )
    elif entity_type == "item":
        db.execute(
            """
            INSERT INTO baseline_item_state
              (entity_id, amount, value, main_flag, flags, material, visual)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                entity_id,
                row.get("amount"),
                row.get("value"),
                row.get("main_flag"),
                row.get("flags"),
                row.get("material"),
                row.get("visual"),
            ),
        )
    elif entity_type in {"npc_inventory", "mobsi_inventory"}:
        db.execute(
            """
            INSERT INTO baseline_inventory_items
              (entity_id, owner_stable_key, owner_persistent_id, owner_display_name,
               item_symbol_index, amount, iterator_count, equipped, slot)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                entity_id,
                row.get("owner_stable_key"),
                row.get("owner_persistent_id"),
                get_value(row, "owner_display_name", "owner_focus_name"),
                row.get("symbol_index"),
                row.get("amount"),
                row.get("iterator_count"),
                int(bool(row.get("equipped"))) if row.get("equipped") is not None else None,
                row.get("slot"),
            ),
        )
    elif entity_type == "quest":
        db.execute(
            """
            INSERT INTO baseline_quests
              (entity_id, name, section, status, entry_count, entries_json)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (
                entity_id,
                row.get("name"),
                row.get("section"),
                row.get("status"),
                row.get("entry_count"),
                json_text(row.get("entries", [])),
            ),
        )
    elif entity_type == "script_global":
        db.execute(
            """
            INSERT INTO baseline_script_globals
              (entity_id, symbol_index, symbol_name, value_type, category, values_json)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (
                entity_id,
                row.get("symbol_index"),
                row.get("symbol_name"),
                row.get("value_type"),
                row.get("category"),
                json_text(row.get("values", [])),
            ),
        )


def import_baseline(db: sqlite3.Connection, run_id: int, baseline_dir: Path) -> dict[str, int]:
    counts: dict[str, int] = {}
    for file_name, entity_type in BASELINE_FILES.items():
        path = baseline_dir / file_name
        count = 0
        for row in iter_jsonl(path) or ():
            entity_id = insert_entity(db, run_id, file_name, entity_type, row)
            insert_typed_state(db, entity_id, entity_type, row)
            count += 1
        counts[file_name] = count
    return counts


def import_events(db: sqlite3.Connection, run_id: int, events_path: Path | None) -> int:
    if events_path is None or not events_path.exists():
        return 0

    count = 0
    for count, row in enumerate(iter_jsonl(events_path) or (), 1):
        db.execute(
            """
            INSERT INTO world_delta_events
              (import_run_id, event_index, event_type, entity_type, stable_key, name, payload_json)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                count,
                row.get("event_type"),
                row.get("entity_type"),
                row.get("stable_key"),
                row.get("name"),
                json_text(row),
            ),
        )
    return count


def apply_events_to_baseline(
    baseline_dir: Path,
    events_path: Path | None,
) -> dict[str, dict[str, dict[str, Any]]]:
    npcs = rows_by_key(read_jsonl_list(baseline_dir, "npcs.jsonl"))
    npc_stats = rows_by_key(read_jsonl_list(baseline_dir, "npc_stats.jsonl"))
    items = rows_by_key(read_jsonl_list(baseline_dir, "items.jsonl"))
    mobsi = rows_by_key(read_jsonl_list(baseline_dir, "mobsi.jsonl"))
    npc_inventory = rows_by_key(read_jsonl_list(baseline_dir, "npc_inventory.jsonl"))
    mobsi_inventory = rows_by_key(read_jsonl_list(baseline_dir, "mobsi_inventory.jsonl"))
    quests = rows_by_key(read_jsonl_list(baseline_dir, "quests.jsonl"))
    known_dialogs = rows_by_key(read_jsonl_list(baseline_dir, "known_dialogs.jsonl"))
    script_globals = rows_by_key(read_jsonl_list(baseline_dir, "script_globals.jsonl"))

    if events_path is None or not events_path.exists():
        return {
            "npcs": npcs,
            "npc_stats": npc_stats,
            "items": items,
            "mobsi": mobsi,
            "npc_inventory": npc_inventory,
            "mobsi_inventory": mobsi_inventory,
            "quests": quests,
            "known_dialogs": known_dialogs,
            "script_globals": script_globals,
        }

    for event in iter_jsonl(events_path) or ():
        event_type = str(event.get("event_type") or "")
        stable_key = str(event.get("stable_key") or "")

        if event_type in {"npc_killed", "npc_changed"} and stable_key in npcs:
            npcs[stable_key] = merge_dict(npcs[stable_key], event_after(event))
        elif event_type in {"npc_stats_changed", "player_stats_changed"} and stable_key in npc_stats:
            npc_stats[stable_key] = merge_dict(npc_stats[stable_key], event_after(event))
        elif event_type == "world_item_removed" and stable_key:
            items.pop(stable_key, None)
        elif event_type == "world_item_added" and stable_key:
            row = event_item(event)
            row.setdefault("stable_key", stable_key)
            row.setdefault("type", "item")
            items[stable_key] = row
        elif event_type in {"player_item_added", "npc_inventory_added"} and stable_key:
            row = event_item(event)
            row.setdefault("stable_key", stable_key)
            row.setdefault("type", "npc_inventory")
            npc_inventory[stable_key] = row
        elif event_type in {"player_item_removed", "npc_inventory_removed"} and stable_key:
            npc_inventory.pop(stable_key, None)
        elif event_type in {"player_item_changed", "npc_inventory_changed"} and stable_key:
            row = event_after(event)
            row.setdefault("stable_key", stable_key)
            npc_inventory[stable_key] = merge_dict(npc_inventory.get(stable_key, {}), row)
        elif event_type == "container_item_added" and stable_key:
            row = event_item(event)
            row.setdefault("stable_key", stable_key)
            row.setdefault("type", "mobsi_inventory")
            mobsi_inventory[stable_key] = row
        elif event_type == "container_item_removed" and stable_key:
            mobsi_inventory.pop(stable_key, None)
        elif event_type == "mobsi_changed" and stable_key in mobsi:
            mobsi[stable_key] = merge_dict(mobsi[stable_key], event_after(event))
        elif event_type == "quest_added" and stable_key:
            quests[stable_key] = {
                "stable_key": stable_key,
                "type": "quest",
                "name": event.get("name"),
                "section": event.get("section"),
                "status": event.get("status"),
                "entry_count": event.get("entry_count"),
                "entries": event.get("entries"),
            }
        elif event_type == "quest_removed" and stable_key:
            quests.pop(stable_key, None)
        elif event_type == "quest_changed" and stable_key:
            quests[stable_key] = merge_dict(quests.get(stable_key, {}), event_after(event))
        elif event_type == "known_dialog_added" and stable_key:
            known_dialogs[stable_key] = {
                "stable_key": stable_key,
                "type": "known_dialog",
                "npc_symbol_name": event.get("npc_symbol_name"),
                "info_symbol_name": event.get("info_symbol_name"),
            }
        elif event_type == "known_dialog_removed" and stable_key:
            known_dialogs.pop(stable_key, None)
        elif event_type == "script_global_changed" and stable_key in script_globals:
            row = dict(script_globals[stable_key])
            row["values"] = event.get("after")
            script_globals[stable_key] = row
        elif event_type == "script_global_added" and stable_key:
            script_globals[stable_key] = {
                "stable_key": stable_key,
                "type": "script_global",
                "category": event.get("category"),
                "symbol_name": event.get("symbol_name"),
                "values": event.get("values"),
            }
        elif event_type == "script_global_removed" and stable_key:
            script_globals.pop(stable_key, None)

    return {
        "npcs": npcs,
        "npc_stats": npc_stats,
        "items": items,
        "mobsi": mobsi,
        "npc_inventory": npc_inventory,
        "mobsi_inventory": mobsi_inventory,
        "quests": quests,
        "known_dialogs": known_dialogs,
        "script_globals": script_globals,
    }


def owner_scope_for_inventory(row: dict[str, Any], default_scope: str) -> str:
    owner_name = str(get_value(row, "owner_display_name", "owner_focus_name") or "")
    return "character" if owner_name in PLAYER_NAMES else default_scope


def import_replay_model(
    db: sqlite3.Connection,
    run_id: int,
    world_instance_id: int,
    baseline_dir: Path,
    events_path: Path | None,
    player_stable_key: str | None,
) -> None:
    replay = apply_events_to_baseline(baseline_dir, events_path)

    for stable_key, row in replay["npcs"].items():
        stats = replay["npc_stats"].get(stable_key, {})
        db.execute(
            """
            INSERT INTO mmo_replay_entities
              (import_run_id, world_instance_id, stable_key, entity_type, display_name,
               symbol_index, persistent_id, hp, mana, dead, position_json, stats_json, raw_json)
            VALUES (?, ?, ?, 'npc', ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                stable_key,
                row.get("display_name"),
                row.get("symbol_index"),
                row.get("persistent_id"),
                row.get("hp"),
                row.get("mana"),
                int(bool(row.get("dead"))) if row.get("dead") is not None else None,
                json_text(row.get("pos")),
                json_text(stats),
                json_text(row),
            ),
        )

    for stable_key, row in replay["items"].items():
        db.execute(
            """
            INSERT INTO mmo_replay_entities
              (import_run_id, world_instance_id, stable_key, entity_type, display_name,
               symbol_index, persistent_id, amount, position_json, raw_json)
            VALUES (?, ?, ?, 'item', ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                stable_key,
                row.get("display_name"),
                row.get("symbol_index"),
                row.get("persistent_id"),
                row.get("amount"),
                json_text(row.get("pos")),
                json_text(row),
            ),
        )

    for stable_key, row in replay["mobsi"].items():
        db.execute(
            """
            INSERT INTO mmo_replay_entities
              (import_run_id, world_instance_id, stable_key, entity_type, display_name,
               persistent_id, mob_state, locked, position_json, raw_json)
            VALUES (?, ?, ?, 'mobsi', ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                stable_key,
                get_value(row, "display_name", "focus_name"),
                row.get("persistent_id"),
                row.get("state"),
                int(bool(row.get("locked"))) if row.get("locked") is not None else None,
                json_text(row.get("pos")),
                json_text(row),
            ),
        )

    for rows, source_file, default_scope in (
        (replay["npc_inventory"], "npc_inventory.jsonl", "npc"),
        (replay["mobsi_inventory"], "mobsi_inventory.jsonl", "container"),
    ):
        for stable_key, row in rows.items():
            owner_name = str(get_value(row, "owner_display_name", "owner_focus_name") or "")
            owner_scope = owner_scope_for_inventory(row, default_scope)
            db.execute(
                """
                INSERT INTO mmo_replay_inventory
                  (import_run_id, world_instance_id, owner_scope, owner_stable_key, owner_persistent_id,
                   owner_display_name, item_stable_key, item_symbol_index, item_name, item_display_name,
                   amount, iterator_count, equipped, slot, source_file, raw_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    run_id,
                    world_instance_id,
                    owner_scope,
                    row.get("owner_stable_key") if owner_scope != "character" else player_stable_key or row.get("owner_stable_key"),
                    row.get("owner_persistent_id"),
                    owner_name,
                    stable_key,
                    row.get("symbol_index"),
                    row.get("name"),
                    row.get("display_name"),
                    row.get("amount"),
                    row.get("iterator_count"),
                    int(bool(row.get("equipped"))) if row.get("equipped") is not None else None,
                    row.get("slot"),
                    source_file,
                    json_text(row),
                ),
            )

    for stable_key, row in replay["quests"].items():
        db.execute(
            """
            INSERT INTO mmo_replay_quest_state
              (import_run_id, world_instance_id, character_stable_key, stable_key, name,
               section, status, entry_count, entries_json, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                player_stable_key,
                stable_key,
                row.get("name"),
                row.get("section"),
                row.get("status"),
                row.get("entry_count"),
                json_text(row.get("entries", [])),
                json_text(row),
            ),
        )

    for stable_key, row in replay["known_dialogs"].items():
        db.execute(
            """
            INSERT INTO mmo_replay_known_dialog_state
              (import_run_id, world_instance_id, character_stable_key, stable_key,
               npc_symbol_name, info_symbol_name, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                player_stable_key,
                stable_key,
                row.get("npc_symbol_name"),
                row.get("info_symbol_name"),
                json_text(row),
            ),
        )

    for stable_key, row in replay["script_globals"].items():
        scope = classify_script_global(row)
        db.execute(
            """
            INSERT INTO mmo_replay_script_global_state
              (import_run_id, world_instance_id, scope, character_stable_key, stable_key,
               symbol_index, symbol_name, value_type, category, values_json, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                scope,
                player_stable_key if scope == "character" else None,
                stable_key,
                row.get("symbol_index"),
                row.get("symbol_name"),
                row.get("value_type"),
                row.get("category"),
                json_text(row.get("values", [])),
                json_text(row),
            ),
        )

    replay_checks = (
        ("entities_total", "mmo_world_entities", "mmo_replay_entities", None),
        ("npc_entities", "mmo_world_entities", "mmo_replay_entities", "entity_type = 'npc'"),
        ("item_entities", "mmo_world_entities", "mmo_replay_entities", "entity_type = 'item'"),
        ("mobsi_entities", "mmo_world_entities", "mmo_replay_entities", "entity_type = 'mobsi'"),
        ("inventory_rows", "mmo_inventory", "mmo_replay_inventory", None),
        ("quest_rows", "mmo_quest_state", "mmo_replay_quest_state", None),
        ("known_dialog_rows", "mmo_known_dialog_state", "mmo_replay_known_dialog_state", None),
        ("script_global_rows", "mmo_script_global_state", "mmo_replay_script_global_state", None),
    )
    for metric, snapshot_table, replay_table, where_clause in replay_checks:
        where_sql = f" AND {where_clause}" if where_clause else ""
        snapshot_count = db.execute(
            f"SELECT COUNT(*) FROM {snapshot_table} WHERE import_run_id = ? AND world_instance_id = ?{where_sql}",
            (run_id, world_instance_id),
        ).fetchone()[0]
        replay_count = db.execute(
            f"SELECT COUNT(*) FROM {replay_table} WHERE import_run_id = ? AND world_instance_id = ?{where_sql}",
            (run_id, world_instance_id),
        ).fetchone()[0]
        db.execute(
            """
            INSERT OR REPLACE INTO mmo_replay_validation
              (import_run_id, world_instance_id, metric, snapshot_count, replay_count, status)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                metric,
                snapshot_count,
                replay_count,
                "ok" if int(snapshot_count) == int(replay_count) else "mismatch",
            ),
        )


def import_mmo_model(
    db: sqlite3.Connection,
    run_id: int,
    baseline_dir: Path,
    snapshot_dir: Path | None,
    events_path: Path | None,
    baseline_manifest: dict[str, Any],
    snapshot_manifest: dict[str, Any],
    baseline_hash: str,
    snapshot_hash: str | None,
) -> None:
    create_mmo_schema(db)

    state_dir = snapshot_dir if snapshot_dir is not None else baseline_dir
    state_manifest = snapshot_manifest if snapshot_dir is not None else baseline_manifest
    state_hash = snapshot_hash if snapshot_dir is not None else baseline_hash
    state_source = "snapshot" if snapshot_dir is not None else "baseline"

    cur = db.execute(
        """
        INSERT INTO mmo_game_targets
          (import_run_id, code, game, patch, schema_version, content_hash, raw_manifest_json)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (
            run_id,
            baseline_manifest.get("target") or "unknown",
            baseline_manifest.get("game"),
            baseline_manifest.get("patch"),
            baseline_manifest.get("schema"),
            baseline_hash,
            json_text(baseline_manifest),
        ),
    )
    game_target_id = int(cur.lastrowid)

    cur = db.execute(
        """
        INSERT INTO mmo_world_templates
          (import_run_id, game_target_id, world_name, baseline_tick,
           baseline_time_day_millis, baseline_hash, raw_manifest_json)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (
            run_id,
            game_target_id,
            baseline_manifest.get("world") or "unknown",
            baseline_manifest.get("tick_count"),
            baseline_manifest.get("time_day_millis"),
            baseline_hash,
            json_text(baseline_manifest),
        ),
    )
    world_template_id = int(cur.lastrowid)

    cur = db.execute(
        """
        INSERT INTO mmo_world_instances
          (import_run_id, world_template_id, shard_name, state_source,
           snapshot_tick, snapshot_time_day_millis, snapshot_hash, raw_manifest_json)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            run_id,
            world_template_id,
            f"{baseline_manifest.get('target') or 'unknown'}:{baseline_manifest.get('world') or 'unknown'}:local",
            state_source,
            state_manifest.get("tick_count"),
            state_manifest.get("time_day_millis"),
            state_hash,
            json_text(state_manifest),
        ),
    )
    world_instance_id = int(cur.lastrowid)

    template_sources = {
        "npcs.jsonl": "npc",
        "items.jsonl": "item",
        "mobsi.jsonl": "mobsi",
    }
    for file_name, entity_type in template_sources.items():
        for row in read_jsonl_list(baseline_dir, file_name):
            stable_key = str(row.get("stable_key") or "")
            if not stable_key:
                continue
            db.execute(
                """
                INSERT OR IGNORE INTO mmo_entity_templates
                  (import_run_id, world_template_id, stable_key, entity_type, display_name,
                   symbol_index, persistent_id, source_file, raw_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    run_id,
                    world_template_id,
                    stable_key,
                    entity_type,
                    get_value(row, "display_name", "name", "focus_name"),
                    row.get("symbol_index"),
                    row.get("persistent_id"),
                    file_name,
                    json_text(row),
                ),
            )

    item_definition_rows = read_jsonl_list(baseline_dir, "items.jsonl")
    item_definition_rows += read_jsonl_list(baseline_dir, "npc_inventory.jsonl")
    item_definition_rows += read_jsonl_list(baseline_dir, "mobsi_inventory.jsonl")
    for row in item_definition_rows:
        db.execute(
            """
            INSERT OR IGNORE INTO mmo_item_definitions
              (import_run_id, symbol_index, name, display_name, visual, value, flags, material, sample_raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                row.get("symbol_index"),
                row.get("name"),
                row.get("display_name"),
                row.get("visual"),
                row.get("value"),
                row.get("flags"),
                row.get("material"),
                json_text(row),
            ),
        )

    npc_rows = read_jsonl_list(state_dir, "npcs.jsonl")
    npc_stats = rows_by_key(read_jsonl_list(state_dir, "npc_stats.jsonl"))
    item_rows = read_jsonl_list(state_dir, "items.jsonl")
    mobsi_rows = read_jsonl_list(state_dir, "mobsi.jsonl")
    player_stable_key: str | None = None

    for row in npc_rows:
        stable_key = str(row.get("stable_key") or "")
        if not stable_key:
            continue
        stats = npc_stats.get(stable_key, {})
        if is_player_row(row):
            player_stable_key = stable_key
        db.execute(
            """
            INSERT INTO mmo_world_entities
              (import_run_id, world_instance_id, stable_key, entity_type, display_name,
               symbol_index, persistent_id, hp, mana, dead, position_json, stats_json, raw_json)
            VALUES (?, ?, ?, 'npc', ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                stable_key,
                row.get("display_name"),
                row.get("symbol_index"),
                row.get("persistent_id"),
                row.get("hp"),
                row.get("mana"),
                int(bool(row.get("dead"))) if row.get("dead") is not None else None,
                json_text(row.get("pos")),
                json_text(stats),
                json_text(row),
            ),
        )

    for row in item_rows:
        stable_key = str(row.get("stable_key") or "")
        if not stable_key:
            continue
        db.execute(
            """
            INSERT INTO mmo_world_entities
              (import_run_id, world_instance_id, stable_key, entity_type, display_name,
               symbol_index, persistent_id, amount, position_json, raw_json)
            VALUES (?, ?, ?, 'item', ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                stable_key,
                row.get("display_name"),
                row.get("symbol_index"),
                row.get("persistent_id"),
                row.get("amount"),
                json_text(row.get("pos")),
                json_text(row),
            ),
        )

    for row in mobsi_rows:
        stable_key = str(row.get("stable_key") or "")
        if not stable_key:
            continue
        db.execute(
            """
            INSERT INTO mmo_world_entities
              (import_run_id, world_instance_id, stable_key, entity_type, display_name,
               persistent_id, mob_state, locked, position_json, raw_json)
            VALUES (?, ?, ?, 'mobsi', ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                stable_key,
                get_value(row, "display_name", "focus_name"),
                row.get("persistent_id"),
                row.get("state"),
                int(bool(row.get("locked"))) if row.get("locked") is not None else None,
                json_text(row.get("pos")),
                json_text(row),
            ),
        )

    for row in npc_rows:
        if not is_player_row(row):
            continue
        stable_key = str(row.get("stable_key") or "")
        stats = npc_stats.get(stable_key, {})
        db.execute(
            """
            INSERT INTO mmo_characters
              (import_run_id, world_instance_id, stable_key, persistent_id, character_kind,
               name, hp, hp_max, mana, mana_max, level, experience, learning_points,
               attributes_json, talents_json, position_json, raw_json)
            VALUES (?, ?, ?, ?, 'player', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                stable_key,
                row.get("persistent_id"),
                row.get("display_name"),
                row.get("hp"),
                row.get("hp_max"),
                row.get("mana"),
                row.get("mana_max"),
                stats.get("level", row.get("level")),
                stats.get("experience"),
                stats.get("learning_points"),
                json_text(stats.get("attributes")),
                json_text(stats.get("talent_skill")),
                json_text(row.get("pos")),
                json_text({"npc": row, "stats": stats}),
            ),
        )

    for file_name, owner_scope_default in (("npc_inventory.jsonl", "npc"), ("mobsi_inventory.jsonl", "container")):
        for row in read_jsonl_list(state_dir, file_name):
            owner_name = str(get_value(row, "owner_display_name", "owner_focus_name") or "")
            owner_scope = "character" if owner_name in PLAYER_NAMES else owner_scope_default
            db.execute(
                """
                INSERT INTO mmo_inventory
                  (import_run_id, world_instance_id, owner_scope, owner_stable_key, owner_persistent_id,
                   owner_display_name, item_stable_key, item_symbol_index, item_name, item_display_name,
                   amount, iterator_count, equipped, slot, source_file, raw_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    run_id,
                    world_instance_id,
                    owner_scope,
                    row.get("owner_stable_key") if owner_scope != "character" else player_stable_key or row.get("owner_stable_key"),
                    row.get("owner_persistent_id"),
                    owner_name,
                    row.get("stable_key"),
                    row.get("symbol_index"),
                    row.get("name"),
                    row.get("display_name"),
                    row.get("amount"),
                    row.get("iterator_count"),
                    int(bool(row.get("equipped"))) if row.get("equipped") is not None else None,
                    row.get("slot"),
                    file_name,
                    json_text(row),
                ),
            )

    for row in read_jsonl_list(state_dir, "quests.jsonl"):
        db.execute(
            """
            INSERT INTO mmo_quest_state
              (import_run_id, world_instance_id, character_stable_key, stable_key, name,
               section, status, entry_count, entries_json, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                player_stable_key,
                row.get("stable_key"),
                row.get("name"),
                row.get("section"),
                row.get("status"),
                row.get("entry_count"),
                json_text(row.get("entries", [])),
                json_text(row),
            ),
        )

    for row in read_jsonl_list(state_dir, "known_dialogs.jsonl"):
        db.execute(
            """
            INSERT INTO mmo_known_dialog_state
              (import_run_id, world_instance_id, character_stable_key, stable_key,
               npc_symbol_name, info_symbol_name, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                player_stable_key,
                row.get("stable_key"),
                row.get("npc_symbol_name"),
                row.get("info_symbol_name"),
                json_text(row),
            ),
        )

    for row in read_jsonl_list(state_dir, "script_globals.jsonl"):
        scope = classify_script_global(row)
        db.execute(
            """
            INSERT INTO mmo_script_global_state
              (import_run_id, world_instance_id, scope, character_stable_key, stable_key,
               symbol_index, symbol_name, value_type, category, values_json, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                world_instance_id,
                scope,
                player_stable_key if scope == "character" else None,
                row.get("stable_key"),
                row.get("symbol_index"),
                row.get("symbol_name"),
                row.get("value_type"),
                row.get("category"),
                json_text(row.get("values", [])),
                json_text(row),
            ),
        )

    if events_path is not None and events_path.exists():
        for index, row in enumerate(iter_jsonl(events_path) or (), 1):
            db.execute(
                """
                INSERT INTO mmo_event_ledger
                  (import_run_id, world_instance_id, event_index, event_type, event_class,
                   entity_type, stable_key, name, payload_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    run_id,
                    world_instance_id,
                    index,
                    row.get("event_type"),
                    event_class(row.get("event_type")),
                    row.get("entity_type"),
                    row.get("stable_key"),
                    row.get("name"),
                    json_text(row),
                ),
            )

    import_replay_model(db, run_id, world_instance_id, baseline_dir, events_path, player_stable_key)


def validate_counts(
    db: sqlite3.Connection,
    run_id: int,
    manifest: dict[str, Any],
    counts: dict[str, int],
    event_count: int,
) -> list[tuple[str, int | None, int, str]]:
    checks = [
        ("npc_count", manifest.get("npc_count"), counts.get("npcs.jsonl", 0)),
        ("npc_stats_rows", manifest.get("npc_stats_rows"), counts.get("npc_stats.jsonl", 0)),
        ("item_count", manifest.get("item_count"), counts.get("items.jsonl", 0)),
        ("npc_inventory_rows", manifest.get("npc_inventory_rows"), counts.get("npc_inventory.jsonl", 0)),
        ("mobsi_count", manifest.get("mobsi_count"), counts.get("mobsi.jsonl", 0)),
        ("mobsi_inventory_rows", manifest.get("mobsi_inventory_rows"), counts.get("mobsi_inventory.jsonl", 0)),
        ("quest_count", manifest.get("quest_count"), counts.get("quests.jsonl", 0)),
        ("known_dialog_count", manifest.get("known_dialog_count"), counts.get("known_dialogs.jsonl", 0)),
        ("script_global_count", manifest.get("script_global_count"), counts.get("script_globals.jsonl", 0)),
        ("world_delta_events", event_count, event_count),
    ]

    results: list[tuple[str, int | None, int, str]] = []
    for metric, expected, actual in checks:
        status = "ok" if expected is None or int(expected) == int(actual) else "mismatch"
        db.execute(
            """
            INSERT OR REPLACE INTO import_validation
              (import_run_id, metric, expected, actual, status)
            VALUES (?, ?, ?, ?, ?)
            """,
            (run_id, metric, expected, actual, status),
        )
        results.append((metric, expected, actual, status))
    return results


def print_summary(db: sqlite3.Connection, run_id: int, validation: list[tuple[str, int | None, int, str]]) -> None:
    print(f"import_run_id: {run_id}")
    print("validation:")
    for metric, expected, actual, status in validation:
        print(f"  {metric}: expected={expected} actual={actual} status={status}")

    print("event_counts:")
    rows = db.execute(
        """
        SELECT event_type, COUNT(*)
        FROM world_delta_events
        WHERE import_run_id = ?
        GROUP BY event_type
        ORDER BY COUNT(*) DESC, event_type
        """,
        (run_id,),
    ).fetchall()
    if not rows:
        print("  (none)")
    for event_type, count in rows:
        print(f"  {event_type}: {count}")

    print("mmo_tables:")
    for table in (
        "mmo_entity_templates",
        "mmo_item_definitions",
        "mmo_world_entities",
        "mmo_characters",
        "mmo_inventory",
        "mmo_quest_state",
        "mmo_known_dialog_state",
        "mmo_script_global_state",
        "mmo_event_ledger",
        "mmo_replay_entities",
        "mmo_replay_inventory",
        "mmo_replay_quest_state",
        "mmo_replay_known_dialog_state",
        "mmo_replay_script_global_state",
    ):
        count = db.execute(f"SELECT COUNT(*) FROM {table} WHERE import_run_id = ?", (run_id,)).fetchone()[0]
        print(f"  {table}: {count}")

    print("mmo_replay_validation:")
    rows = db.execute(
        """
        SELECT metric, snapshot_count, replay_count, status
        FROM mmo_replay_validation
        WHERE import_run_id = ?
        ORDER BY metric
        """,
        (run_id,),
    ).fetchall()
    if not rows:
        print("  (none)")
    for metric, snapshot_count, replay_count, status in rows:
        print(f"  {metric}: snapshot={snapshot_count} replay={replay_count} status={status}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Import OpenGothic JSONL dumps into SQLite staging DB.")
    parser.add_argument("--baseline", required=True, type=Path, help="Baseline world dump directory.")
    parser.add_argument("--snapshot", type=Path, help="Optional snapshot dump directory.")
    parser.add_argument("--events", type=Path, help="Optional world_events.jsonl path.")
    parser.add_argument("--db", required=True, type=Path, help="SQLite database path to create/update.")
    parser.add_argument("--reset", action="store_true", help="Delete the DB before importing.")
    args = parser.parse_args()

    baseline_dir = args.baseline
    snapshot_dir = args.snapshot
    events_path = args.events

    if not baseline_dir.is_dir():
        raise SystemExit(f"Baseline directory not found: {baseline_dir}")
    if snapshot_dir is not None and not snapshot_dir.is_dir():
        raise SystemExit(f"Snapshot directory not found: {snapshot_dir}")
    if events_path is None and snapshot_dir is not None:
        candidate = snapshot_dir / "world_events.jsonl"
        if candidate.exists():
            events_path = candidate

    if args.reset and args.db.exists():
        args.db.unlink()
    args.db.parent.mkdir(parents=True, exist_ok=True)

    baseline_manifest = read_json(baseline_dir / "manifest.json")
    snapshot_manifest = read_json(snapshot_dir / "manifest.json") if snapshot_dir else {}
    baseline_hash = stable_hash(baseline_dir / "manifest.json")
    snapshot_hash = stable_hash(snapshot_dir / "manifest.json") if snapshot_dir else None

    db = sqlite3.connect(args.db)
    try:
        create_schema(db)
        with db:
            cur = db.execute(
                """
                INSERT INTO import_runs
                  (baseline_dir, snapshot_dir, target, world, schema_version, baseline_hash, snapshot_hash)
                VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    str(baseline_dir),
                    str(snapshot_dir) if snapshot_dir else None,
                    baseline_manifest.get("target"),
                    baseline_manifest.get("world"),
                    baseline_manifest.get("schema"),
                    baseline_hash,
                    snapshot_hash,
                ),
            )
            run_id = int(cur.lastrowid)
            db.execute(
                """
                INSERT INTO worlds
                  (import_run_id, target, world, game, patch, baseline_kind, snapshot_kind,
                   baseline_tick, snapshot_tick, baseline_time_day_millis, snapshot_time_day_millis)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    run_id,
                    baseline_manifest.get("target"),
                    baseline_manifest.get("world"),
                    baseline_manifest.get("game"),
                    baseline_manifest.get("patch"),
                    baseline_manifest.get("kind"),
                    snapshot_manifest.get("kind"),
                    baseline_manifest.get("tick_count"),
                    snapshot_manifest.get("tick_count"),
                    baseline_manifest.get("time_day_millis"),
                    snapshot_manifest.get("time_day_millis"),
                ),
            )
            counts = import_baseline(db, run_id, baseline_dir)
            event_count = import_events(db, run_id, events_path)
            import_mmo_model(
                db,
                run_id,
                baseline_dir,
                snapshot_dir,
                events_path,
                baseline_manifest,
                snapshot_manifest,
                baseline_hash,
                snapshot_hash,
            )
            validation = validate_counts(db, run_id, baseline_manifest, counts, event_count)
        print_summary(db, run_id, validation)
    finally:
        db.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
