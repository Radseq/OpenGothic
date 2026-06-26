#!/usr/bin/env python3
"""
Build a production-shaped Gothic MMO SQLite database from a staging import.

The staging database keeps raw dump/replay tables. This tool creates the next
layer: a realm/world/character database that looks closer to what a real MMO
server would persist. SQLite is still only the local target; the schema is
shaped so it can later become PostgreSQL migrations.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path
from typing import Any


def json_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def table_exists(db: sqlite3.Connection, table: str) -> bool:
    row = db.execute(
        "SELECT 1 FROM sqlite_master WHERE type IN ('table', 'view') AND name = ?",
        (table,),
    ).fetchone()
    return row is not None


def scalar(db: sqlite3.Connection, sql: str, args: tuple[Any, ...] = ()) -> Any:
    row = db.execute(sql, args).fetchone()
    return None if row is None else row[0]


def classify_item_template(row: sqlite3.Row | tuple[Any, ...]) -> dict[str, Any]:
    _id, _symbol, name, display_name, visual, _value, flags, _material = tuple(row)
    text = f"{name or ''} {display_name or ''} {visual or ''}".casefold()
    visual_text = str(visual or "").casefold()

    item_class = "misc"
    stack_policy = "stack"
    max_stack = 99
    equipment_slot_group = None
    confidence = "heuristic"

    if "itmi_gold.3ds" in visual_text or "gold" in visual_text and "nugget" not in visual_text:
        item_class = "currency"
        stack_policy = "stack"
        max_stack = 999999
        confidence = "visual"
    elif visual_text.startswith("itmw_"):
        item_class = "equipment_weapon_melee"
        stack_policy = "instance"
        max_stack = 1
        equipment_slot_group = "weapon"
        confidence = "visual"
    elif visual_text.startswith("itrw_bow") or visual_text.startswith("itrw_crossbow"):
        item_class = "equipment_weapon_ranged"
        stack_policy = "instance"
        max_stack = 1
        equipment_slot_group = "ranged_weapon"
        confidence = "visual"
    elif visual_text.startswith("itrw_arrow") or visual_text.startswith("itrw_bolt"):
        item_class = "ammo"
        stack_policy = "stack"
        max_stack = 999
        confidence = "visual"
    elif visual_text.startswith("itar_"):
        item_class = "equipment_armor"
        stack_policy = "instance"
        max_stack = 1
        equipment_slot_group = "armor"
        confidence = "visual"
    elif visual_text.startswith("itam_") or visual_text.startswith("itri_"):
        item_class = "equipment_accessory"
        stack_policy = "instance"
        max_stack = 1
        equipment_slot_group = "accessory"
        confidence = "visual"
    elif visual_text.startswith("itpo"):
        item_class = "consumable_potion"
        stack_policy = "stack"
        max_stack = 99
        confidence = "visual"
    elif visual_text.startswith("itfo"):
        item_class = "consumable_food"
        stack_policy = "stack"
        max_stack = 99
        confidence = "visual"
    elif visual_text.startswith("itpl"):
        item_class = "consumable_plant"
        stack_policy = "stack"
        max_stack = 99
        confidence = "visual"
    elif visual_text.startswith("itke_"):
        item_class = "key"
        stack_policy = "unique"
        max_stack = 1
        confidence = "visual"
    elif visual_text.startswith("itsc") or visual_text.startswith("itwr_scroll"):
        item_class = "spell_scroll"
        stack_policy = "stack"
        max_stack = 99
        confidence = "visual"
    elif visual_text.startswith("itwr_book") or visual_text.startswith("itwr_"):
        item_class = "readable"
        stack_policy = "unique"
        max_stack = 1
        confidence = "visual"
    elif "stoneplate" in visual_text or "stoneplate" in text:
        item_class = "quest_or_progression_item"
        stack_policy = "unique"
        max_stack = 1
        confidence = "visual"
    elif visual_text.startswith("itmi_"):
        if any(token in text for token in ("ore", "nugget", "gem", "aquamarine", "ruda", "bry")):
            item_class = "crafting_or_currency_material"
            stack_policy = "stack"
            max_stack = 999
        elif flags is not None and int(flags) & 4096:
            item_class = "quest_or_script_item"
            stack_policy = "unique"
            max_stack = 1
        else:
            item_class = "trade_good"
            stack_policy = "stack"
            max_stack = 99
        confidence = "heuristic"

    return {
        "item_class": item_class,
        "stack_policy": stack_policy,
        "max_stack": max_stack,
        "equipment_slot_group": equipment_slot_group,
        "confidence": confidence,
    }


def create_schema(db: sqlite3.Connection) -> None:
    db.executescript(
        """
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS schema_meta (
          key TEXT PRIMARY KEY,
          value TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS import_audits (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
          source_db TEXT NOT NULL,
          source_import_run_id INTEGER NOT NULL,
          source_world_instance_id INTEGER NOT NULL,
          source_snapshot_tick INTEGER,
          source_snapshot_hash TEXT,
          notes TEXT
        );

        CREATE TABLE IF NOT EXISTS account_accounts (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          username TEXT NOT NULL UNIQUE,
          display_name TEXT NOT NULL,
          status TEXT NOT NULL DEFAULT 'active',
          created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS account_entitlements (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_id INTEGER NOT NULL REFERENCES account_accounts(id) ON DELETE CASCADE,
          target_code TEXT NOT NULL,
          granted_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
          UNIQUE(account_id, target_code)
        );

        CREATE TABLE IF NOT EXISTS realm_realms (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          code TEXT NOT NULL UNIQUE,
          name TEXT NOT NULL,
          region TEXT NOT NULL DEFAULT 'local',
          status TEXT NOT NULL DEFAULT 'development',
          created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS content_game_targets (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          code TEXT NOT NULL UNIQUE,
          game INTEGER,
          patch INTEGER,
          schema_version INTEGER,
          content_hash TEXT,
          raw_manifest_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS content_world_templates (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          game_target_id INTEGER NOT NULL REFERENCES content_game_targets(id) ON DELETE CASCADE,
          world_name TEXT NOT NULL,
          baseline_tick INTEGER,
          baseline_time_day_millis INTEGER,
          baseline_hash TEXT,
          raw_manifest_json TEXT NOT NULL,
          UNIQUE(game_target_id, world_name, baseline_hash)
        );

        CREATE TABLE IF NOT EXISTS realm_world_instances (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          realm_id INTEGER NOT NULL REFERENCES realm_realms(id) ON DELETE CASCADE,
          world_template_id INTEGER NOT NULL REFERENCES content_world_templates(id) ON DELETE CASCADE,
          shard_key TEXT NOT NULL UNIQUE,
          state_source TEXT NOT NULL,
          snapshot_tick INTEGER,
          snapshot_time_day_millis INTEGER,
          snapshot_hash TEXT,
          created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS content_entity_templates (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          world_template_id INTEGER NOT NULL REFERENCES content_world_templates(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          entity_type TEXT NOT NULL,
          display_name TEXT,
          symbol_index INTEGER,
          persistent_id INTEGER,
          source_file TEXT NOT NULL,
          raw_json TEXT NOT NULL,
          UNIQUE(world_template_id, source_file, stable_key)
        );

        CREATE TABLE IF NOT EXISTS content_item_templates (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          game_target_id INTEGER NOT NULL REFERENCES content_game_targets(id) ON DELETE CASCADE,
          symbol_index INTEGER,
          name TEXT,
          display_name TEXT,
          visual TEXT,
          value INTEGER,
          flags INTEGER,
          material INTEGER,
          sample_raw_json TEXT NOT NULL,
          UNIQUE(game_target_id, symbol_index, name, visual)
        );

        CREATE TABLE IF NOT EXISTS content_item_classification (
          item_template_id INTEGER PRIMARY KEY REFERENCES content_item_templates(id) ON DELETE CASCADE,
          item_class TEXT NOT NULL,
          stack_policy TEXT NOT NULL,
          max_stack INTEGER,
          equipment_slot_group TEXT,
          confidence TEXT NOT NULL,
          rule_version INTEGER NOT NULL DEFAULT 1
        );

        CREATE TABLE IF NOT EXISTS characters (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_id INTEGER NOT NULL REFERENCES account_accounts(id) ON DELETE CASCADE,
          realm_id INTEGER NOT NULL REFERENCES realm_realms(id) ON DELETE CASCADE,
          current_world_instance_id INTEGER REFERENCES realm_world_instances(id) ON DELETE SET NULL,
          source_stable_key TEXT NOT NULL,
          persistent_id INTEGER,
          name TEXT NOT NULL,
          character_kind TEXT NOT NULL DEFAULT 'player',
          created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
          updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
          UNIQUE(realm_id, name)
        );

        CREATE TABLE IF NOT EXISTS character_stats (
          character_id INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
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
          raw_json TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS character_inventory (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
          item_stable_key TEXT NOT NULL,
          item_symbol_index INTEGER,
          item_name TEXT,
          item_display_name TEXT,
          amount INTEGER,
          iterator_count INTEGER,
          equipped INTEGER NOT NULL DEFAULT 0,
          slot INTEGER,
          source_file TEXT NOT NULL,
          raw_json TEXT NOT NULL,
          UNIQUE(character_id, item_stable_key)
        );

        CREATE TABLE IF NOT EXISTS item_instances (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          item_template_id INTEGER REFERENCES content_item_templates(id) ON DELETE SET NULL,
          source_table TEXT NOT NULL,
          source_row_id INTEGER NOT NULL,
          source_stable_key TEXT NOT NULL,
          item_symbol_index INTEGER,
          item_name TEXT,
          item_display_name TEXT,
          owner_type TEXT NOT NULL,
          character_id INTEGER REFERENCES characters(id) ON DELETE CASCADE,
          world_instance_id INTEGER REFERENCES realm_world_instances(id) ON DELETE CASCADE,
          container_scope TEXT,
          container_stable_key TEXT,
          container_display_name TEXT,
          quantity INTEGER NOT NULL DEFAULT 1,
          iterator_count INTEGER,
          equipped INTEGER NOT NULL DEFAULT 0,
          equipment_slot INTEGER,
          source_file TEXT NOT NULL,
          raw_json TEXT NOT NULL,
          UNIQUE(source_table, source_row_id)
        );

        CREATE TABLE IF NOT EXISTS character_equipment (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
          slot INTEGER NOT NULL,
          item_instance_id INTEGER NOT NULL REFERENCES item_instances(id) ON DELETE CASCADE,
          item_template_id INTEGER REFERENCES content_item_templates(id) ON DELETE SET NULL,
          source_stable_key TEXT NOT NULL,
          item_display_name TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(character_id, slot, item_instance_id)
        );

        CREATE TABLE IF NOT EXISTS character_quests (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          name TEXT,
          section INTEGER,
          status INTEGER,
          entry_count INTEGER,
          entries_json TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(character_id, stable_key)
        );

        CREATE TABLE IF NOT EXISTS character_known_dialogs (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          npc_symbol_name TEXT,
          info_symbol_name TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(character_id, stable_key)
        );

        CREATE TABLE IF NOT EXISTS character_script_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          symbol_index INTEGER,
          symbol_name TEXT,
          value_type TEXT,
          category TEXT,
          values_json TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(character_id, stable_key)
        );

        CREATE TABLE IF NOT EXISTS world_entity_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          world_instance_id INTEGER NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
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
          UNIQUE(world_instance_id, entity_type, stable_key)
        );

        CREATE TABLE IF NOT EXISTS world_inventory (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          world_instance_id INTEGER NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
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
          equipped INTEGER NOT NULL DEFAULT 0,
          slot INTEGER,
          source_file TEXT NOT NULL,
          raw_json TEXT NOT NULL,
          UNIQUE(world_instance_id, owner_scope, item_stable_key)
        );

        CREATE TABLE IF NOT EXISTS world_script_state (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          world_instance_id INTEGER NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
          stable_key TEXT NOT NULL,
          symbol_index INTEGER,
          symbol_name TEXT,
          value_type TEXT,
          category TEXT,
          values_json TEXT,
          raw_json TEXT NOT NULL,
          UNIQUE(world_instance_id, stable_key)
        );

        CREATE TABLE IF NOT EXISTS world_event_journal (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          world_instance_id INTEGER NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
          source_event_index INTEGER NOT NULL,
          event_type TEXT NOT NULL,
          event_class TEXT NOT NULL,
          entity_type TEXT,
          stable_key TEXT,
          actor_character_id INTEGER REFERENCES characters(id) ON DELETE SET NULL,
          name TEXT,
          payload_json TEXT NOT NULL,
          created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
          UNIQUE(world_instance_id, source_event_index)
        );

        CREATE TABLE IF NOT EXISTS world_replay_validation (
          world_instance_id INTEGER NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
          metric TEXT NOT NULL,
          snapshot_count INTEGER,
          replay_count INTEGER,
          status TEXT NOT NULL,
          PRIMARY KEY(world_instance_id, metric)
        );

        CREATE TABLE IF NOT EXISTS world_runtime_noise_candidates (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          world_instance_id INTEGER NOT NULL REFERENCES realm_world_instances(id) ON DELETE CASCADE,
          reason TEXT NOT NULL,
          owner_scope TEXT,
          owner_display_name TEXT,
          item_display_name TEXT,
          item_stable_key TEXT,
          raw_json TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_character_inventory_character
          ON character_inventory(character_id);
        CREATE INDEX IF NOT EXISTS idx_item_instances_owner
          ON item_instances(owner_type, character_id, world_instance_id);
        CREATE INDEX IF NOT EXISTS idx_item_instances_template
          ON item_instances(item_template_id, item_symbol_index, item_name);
        CREATE INDEX IF NOT EXISTS idx_item_classification_class
          ON content_item_classification(item_class, stack_policy);
        CREATE INDEX IF NOT EXISTS idx_character_equipment_character
          ON character_equipment(character_id, slot);
        CREATE INDEX IF NOT EXISTS idx_world_entity_state_type
          ON world_entity_state(world_instance_id, entity_type);
        CREATE INDEX IF NOT EXISTS idx_world_inventory_owner
          ON world_inventory(world_instance_id, owner_scope, owner_stable_key);
        CREATE INDEX IF NOT EXISTS idx_world_event_journal_type
          ON world_event_journal(world_instance_id, event_class, event_type);

        CREATE TEMP VIEW IF NOT EXISTS v_character_sheet AS
          SELECT c.id AS character_id, c.name, c.character_kind,
                 s.level, s.experience, s.learning_points,
                 s.hp, s.hp_max, s.mana, s.mana_max,
                 COUNT(DISTINCT i.id) AS inventory_rows,
                 COUNT(DISTINCT q.id) AS quest_rows,
                 COUNT(DISTINCT d.id) AS known_dialog_rows
          FROM characters c
          LEFT JOIN character_stats s ON s.character_id = c.id
          LEFT JOIN character_inventory i ON i.character_id = c.id
          LEFT JOIN character_quests q ON q.character_id = c.id
          LEFT JOIN character_known_dialogs d ON d.character_id = c.id
          GROUP BY c.id;

        CREATE TEMP VIEW IF NOT EXISTS v_character_inventory AS
          SELECT c.name AS character_name, i.item_display_name, i.item_name,
                 i.amount, i.iterator_count, i.equipped, i.slot, i.item_stable_key
          FROM character_inventory i
          JOIN characters c ON c.id = i.character_id;

        CREATE TEMP VIEW IF NOT EXISTS v_item_instances AS
          SELECT ii.id AS item_instance_id, ii.owner_type, c.name AS character_name,
                 ii.container_scope, ii.container_display_name,
                 cls.item_class, cls.stack_policy,
                 ii.item_display_name, ii.item_name, ii.quantity, ii.iterator_count,
                 ii.equipped, ii.equipment_slot, ii.source_stable_key
          FROM item_instances ii
          LEFT JOIN content_item_classification cls ON cls.item_template_id = ii.item_template_id
          LEFT JOIN characters c ON c.id = ii.character_id;

        CREATE TEMP VIEW IF NOT EXISTS v_character_equipment AS
          SELECT c.name AS character_name, e.slot, e.item_display_name,
                 e.source_stable_key, e.item_instance_id
          FROM character_equipment e
          JOIN characters c ON c.id = e.character_id;

        CREATE TEMP VIEW IF NOT EXISTS v_character_item_totals AS
          SELECT c.id AS character_id, c.name AS character_name,
                 i.item_symbol_index, i.item_name, i.item_display_name,
                 COUNT(*) AS row_count,
                 SUM(COALESCE(i.amount, 0)) AS amount_total,
                 SUM(COALESCE(i.iterator_count, 0)) AS iterator_total,
                 SUM(CASE WHEN i.equipped <> 0 THEN 1 ELSE 0 END) AS equipped_rows,
                 GROUP_CONCAT(i.slot) AS slots,
                 GROUP_CONCAT(i.item_stable_key) AS item_stable_keys
          FROM character_inventory i
          JOIN characters c ON c.id = i.character_id
          GROUP BY c.id, i.item_symbol_index, i.item_name, i.item_display_name;

        CREATE TEMP VIEW IF NOT EXISTS v_character_item_stacks AS
          SELECT c.id AS character_id, c.name AS character_name,
                 ii.item_template_id, cls.item_class, cls.stack_policy, cls.max_stack,
                 ii.item_symbol_index, ii.item_name, ii.item_display_name,
                 COUNT(*) AS instance_rows,
                 SUM(ii.quantity) AS quantity_total,
                 SUM(COALESCE(ii.iterator_count, 0)) AS iterator_total,
                 SUM(CASE WHEN ii.equipped <> 0 THEN 1 ELSE 0 END) AS equipped_instances,
                 GROUP_CONCAT(ii.equipment_slot) AS equipment_slots,
                 GROUP_CONCAT(ii.source_stable_key) AS source_stable_keys
          FROM item_instances ii
          JOIN characters c ON c.id = ii.character_id
          LEFT JOIN content_item_classification cls ON cls.item_template_id = ii.item_template_id
          WHERE ii.owner_type = 'character'
          GROUP BY c.id, ii.item_template_id, cls.item_class, cls.stack_policy, cls.max_stack,
                   ii.item_symbol_index, ii.item_name, ii.item_display_name;

        CREATE TEMP VIEW IF NOT EXISTS v_world_item_stacks AS
          SELECT ii.world_instance_id, ii.container_scope, ii.container_stable_key,
                 ii.container_display_name, ii.item_template_id,
                 cls.item_class, cls.stack_policy, cls.max_stack, ii.item_symbol_index,
                 ii.item_name, ii.item_display_name,
                 COUNT(*) AS instance_rows,
                 SUM(ii.quantity) AS quantity_total,
                 SUM(COALESCE(ii.iterator_count, 0)) AS iterator_total,
                 GROUP_CONCAT(ii.source_stable_key) AS source_stable_keys
          FROM item_instances ii
          LEFT JOIN content_item_classification cls ON cls.item_template_id = ii.item_template_id
          WHERE ii.owner_type = 'world'
          GROUP BY ii.world_instance_id, ii.container_scope, ii.container_stable_key,
                   ii.container_display_name, ii.item_template_id,
                   cls.item_class, cls.stack_policy, cls.max_stack, ii.item_symbol_index,
                   ii.item_name, ii.item_display_name;

        CREATE TEMP VIEW IF NOT EXISTS v_item_class_counts AS
          SELECT item_class, stack_policy, COUNT(*) AS template_count
          FROM content_item_classification
          GROUP BY item_class, stack_policy;

        CREATE TEMP VIEW IF NOT EXISTS v_character_stack_policy_issues AS
          SELECT character_name, item_display_name, item_class, stack_policy,
                 instance_rows, quantity_total, iterator_total, equipped_instances,
                 CASE
                   WHEN stack_policy = 'instance' AND quantity_total > instance_rows THEN 'instance_quantity_gt_one'
                   WHEN stack_policy = 'unique' AND quantity_total > 1 THEN 'unique_quantity_gt_one'
                   WHEN max_stack IS NOT NULL AND quantity_total > max_stack THEN 'quantity_gt_max_stack'
                   WHEN stack_policy = 'stack' AND equipped_instances > 0 THEN 'stack_item_equipped'
                   ELSE 'ok'
                 END AS issue
          FROM v_character_item_stacks
          WHERE (stack_policy = 'instance' AND quantity_total > instance_rows)
             OR (stack_policy = 'unique' AND quantity_total > 1)
             OR (max_stack IS NOT NULL AND quantity_total > max_stack)
             OR (stack_policy = 'stack' AND equipped_instances > 0);

        CREATE TEMP VIEW IF NOT EXISTS v_character_inventory_anomalies AS
          SELECT *,
                 CASE
                   WHEN row_count > 1 AND equipped_rows > 0 THEN 'equipped_and_bag_split'
                   WHEN row_count > 1 THEN 'duplicate_item_rows'
                   WHEN amount_total > iterator_total AND iterator_total > 0 THEN 'amount_exceeds_iterator_count'
                   ELSE 'ok'
                 END AS anomaly
          FROM v_character_item_totals
          WHERE row_count > 1
             OR (amount_total > iterator_total AND iterator_total > 0);

        CREATE TEMP VIEW IF NOT EXISTS v_world_dead_npcs AS
          SELECT w.shard_key, e.stable_key, e.display_name, e.persistent_id, e.hp
          FROM world_entity_state e
          JOIN realm_world_instances w ON w.id = e.world_instance_id
          WHERE e.entity_type = 'npc' AND e.dead = 1;

        CREATE TEMP VIEW IF NOT EXISTS v_world_event_counts AS
          SELECT world_instance_id, event_class, event_type, COUNT(*) AS event_count
          FROM world_event_journal
          GROUP BY world_instance_id, event_class, event_type;

        CREATE TEMP VIEW IF NOT EXISTS v_world_replay_validation AS
          SELECT world_instance_id, metric, snapshot_count, replay_count, status
          FROM world_replay_validation;

        CREATE TEMP VIEW IF NOT EXISTS v_runtime_noise_inventory AS
          SELECT world_instance_id, owner_scope, owner_display_name,
                 item_display_name, item_stable_key, reason
          FROM world_runtime_noise_candidates;
        """
    )

    db.execute("INSERT OR REPLACE INTO schema_meta(key, value) VALUES ('schema_name', 'gothic_mmo')")
    db.execute("INSERT OR REPLACE INTO schema_meta(key, value) VALUES ('schema_version', '3')")


def require_source(source: sqlite3.Connection) -> None:
    required = [
        "mmo_game_targets",
        "mmo_world_templates",
        "mmo_world_instances",
        "mmo_entity_templates",
        "mmo_item_definitions",
        "mmo_replay_entities",
        "mmo_replay_inventory",
        "mmo_replay_quest_state",
        "mmo_replay_known_dialog_state",
        "mmo_replay_script_global_state",
        "mmo_event_ledger",
        "mmo_replay_validation",
    ]
    missing = [name for name in required if not table_exists(source, name)]
    if missing:
        raise SystemExit(f"Source staging DB is missing MMO replay tables: {', '.join(missing)}")


def latest_pair(source: sqlite3.Connection, import_run_id: int | None, world_instance_id: int | None) -> tuple[int, int]:
    if import_run_id is not None and world_instance_id is not None:
        return import_run_id, world_instance_id

    row = source.execute(
        """
        SELECT import_run_id, id
        FROM mmo_world_instances
        ORDER BY import_run_id DESC, id DESC
        LIMIT 1
        """
    ).fetchone()
    if row is None:
        raise SystemExit("No mmo_world_instances found in source staging DB.")
    return int(row[0]), int(row[1])


def insert_one(db: sqlite3.Connection, sql: str, args: tuple[Any, ...]) -> int:
    cur = db.execute(sql, args)
    return int(cur.lastrowid)


def normalize_inventory(dest: sqlite3.Connection, character_id: int, world_instance_id: int) -> None:
    dest.execute(
        """
        INSERT INTO item_instances
          (item_template_id, source_table, source_row_id, source_stable_key,
           item_symbol_index, item_name, item_display_name, owner_type, character_id,
           quantity, iterator_count, equipped, equipment_slot, source_file, raw_json)
        SELECT
          (
            SELECT t.id
            FROM content_item_templates t
            WHERE (ci.item_symbol_index IS NOT NULL AND t.symbol_index = ci.item_symbol_index)
               OR (ci.item_symbol_index IS NULL AND (
                    (ci.item_name IS NOT NULL AND t.name = ci.item_name)
                 OR (ci.item_display_name IS NOT NULL AND t.display_name = ci.item_display_name)
               ))
            ORDER BY t.id
            LIMIT 1
          ),
          'character_inventory',
          ci.id,
          ci.item_stable_key,
          ci.item_symbol_index,
          ci.item_name,
          ci.item_display_name,
          'character',
          ci.character_id,
          COALESCE(ci.amount, 1),
          ci.iterator_count,
          ci.equipped,
          CASE WHEN ci.equipped <> 0 THEN ci.slot ELSE NULL END,
          ci.source_file,
          ci.raw_json
        FROM character_inventory ci
        WHERE ci.character_id = ?
        """,
        (character_id,),
    )

    dest.execute(
        """
        INSERT INTO item_instances
          (item_template_id, source_table, source_row_id, source_stable_key,
           item_symbol_index, item_name, item_display_name, owner_type,
           world_instance_id, container_scope, container_stable_key, container_display_name,
           quantity, iterator_count, equipped, equipment_slot, source_file, raw_json)
        SELECT
          (
            SELECT t.id
            FROM content_item_templates t
            WHERE (wi.item_symbol_index IS NOT NULL AND t.symbol_index = wi.item_symbol_index)
               OR (wi.item_symbol_index IS NULL AND (
                    (wi.item_name IS NOT NULL AND t.name = wi.item_name)
                 OR (wi.item_display_name IS NOT NULL AND t.display_name = wi.item_display_name)
               ))
            ORDER BY t.id
            LIMIT 1
          ),
          'world_inventory',
          wi.id,
          wi.item_stable_key,
          wi.item_symbol_index,
          wi.item_name,
          wi.item_display_name,
          'world',
          wi.world_instance_id,
          wi.owner_scope,
          wi.owner_stable_key,
          wi.owner_display_name,
          COALESCE(wi.amount, 1),
          wi.iterator_count,
          wi.equipped,
          CASE WHEN wi.equipped <> 0 THEN wi.slot ELSE NULL END,
          wi.source_file,
          wi.raw_json
        FROM world_inventory wi
        WHERE wi.world_instance_id = ?
        """,
        (world_instance_id,),
    )

    dest.execute(
        """
        INSERT INTO character_equipment
          (character_id, slot, item_instance_id, item_template_id,
           source_stable_key, item_display_name, raw_json)
        SELECT character_id, equipment_slot, id, item_template_id,
               source_stable_key, item_display_name, raw_json
        FROM item_instances
        WHERE owner_type = 'character'
          AND character_id = ?
          AND equipped <> 0
          AND equipment_slot IS NOT NULL
        """,
        (character_id,),
    )


def populate_item_classification(dest: sqlite3.Connection) -> None:
    rows = dest.execute(
        """
        SELECT id, symbol_index, name, display_name, visual, value, flags, material
        FROM content_item_templates
        ORDER BY id
        """
    ).fetchall()
    for row in rows:
        item_template_id = int(row[0])
        classification = classify_item_template(row)
        dest.execute(
            """
            INSERT OR REPLACE INTO content_item_classification
              (item_template_id, item_class, stack_policy, max_stack,
               equipment_slot_group, confidence, rule_version)
            VALUES (?, ?, ?, ?, ?, ?, 1)
            """,
            (
                item_template_id,
                classification["item_class"],
                classification["stack_policy"],
                classification["max_stack"],
                classification["equipment_slot_group"],
                classification["confidence"],
            ),
        )


def copy_database(source: sqlite3.Connection, dest: sqlite3.Connection, source_db: Path, run_id: int, source_world_id: int) -> None:
    game_target = source.execute(
        """
        SELECT code, game, patch, schema_version, content_hash, raw_manifest_json
        FROM mmo_game_targets
        WHERE import_run_id = ?
        ORDER BY id
        LIMIT 1
        """,
        (run_id,),
    ).fetchone()
    if game_target is None:
        raise SystemExit(f"No mmo_game_targets for import_run_id={run_id}")

    target_id = insert_one(
        dest,
        """
        INSERT INTO content_game_targets
          (code, game, patch, schema_version, content_hash, raw_manifest_json)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        tuple(game_target),
    )

    world_template = source.execute(
        """
        SELECT world_name, baseline_tick, baseline_time_day_millis, baseline_hash, raw_manifest_json
        FROM mmo_world_templates
        WHERE import_run_id = ?
        ORDER BY id
        LIMIT 1
        """,
        (run_id,),
    ).fetchone()
    if world_template is None:
        raise SystemExit(f"No mmo_world_templates for import_run_id={run_id}")

    world_template_id = insert_one(
        dest,
        """
        INSERT INTO content_world_templates
          (game_target_id, world_name, baseline_tick, baseline_time_day_millis, baseline_hash, raw_manifest_json)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (target_id, *tuple(world_template)),
    )

    realm_id = insert_one(
        dest,
        "INSERT INTO realm_realms(code, name) VALUES (?, ?)",
        ("local-dev", "Local Development Realm"),
    )

    world_instance = source.execute(
        """
        SELECT shard_name, state_source, snapshot_tick, snapshot_time_day_millis, snapshot_hash
        FROM mmo_world_instances
        WHERE import_run_id = ? AND id = ?
        """,
        (run_id, source_world_id),
    ).fetchone()
    if world_instance is None:
        raise SystemExit(f"No source world instance import_run_id={run_id} id={source_world_id}")

    shard_key, state_source, snapshot_tick, snapshot_time_day_millis, snapshot_hash = tuple(world_instance)
    world_instance_id = insert_one(
        dest,
        """
        INSERT INTO realm_world_instances
          (realm_id, world_template_id, shard_key, state_source, snapshot_tick, snapshot_time_day_millis, snapshot_hash)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (realm_id, world_template_id, shard_key, state_source, snapshot_tick, snapshot_time_day_millis, snapshot_hash),
    )

    audit_id = insert_one(
        dest,
        """
        INSERT INTO import_audits
          (source_db, source_import_run_id, source_world_instance_id, source_snapshot_tick, source_snapshot_hash, notes)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (
            str(source_db),
            run_id,
            source_world_id,
            snapshot_tick,
            snapshot_hash,
            "Built from OpenGothic staging DB mmo_replay_* tables.",
        ),
    )

    account_id = insert_one(
        dest,
        "INSERT INTO account_accounts(username, display_name) VALUES (?, ?)",
        ("local_admin", "Local Admin"),
    )
    dest.execute(
        "INSERT INTO account_entitlements(account_id, target_code) VALUES (?, ?)",
        (account_id, game_target[0]),
    )

    for row in source.execute(
        """
        SELECT stable_key, entity_type, display_name, symbol_index, persistent_id, source_file, raw_json
        FROM mmo_entity_templates
        WHERE import_run_id = ?
        ORDER BY id
        """,
        (run_id,),
    ):
        dest.execute(
            """
            INSERT OR IGNORE INTO content_entity_templates
              (world_template_id, stable_key, entity_type, display_name, symbol_index, persistent_id, source_file, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (world_template_id, *tuple(row)),
        )

    for row in source.execute(
        """
        SELECT symbol_index, name, display_name, visual, value, flags, material, sample_raw_json
        FROM mmo_item_definitions
        WHERE import_run_id = ?
        ORDER BY id
        """,
        (run_id,),
    ):
        dest.execute(
            """
            INSERT OR IGNORE INTO content_item_templates
              (game_target_id, symbol_index, name, display_name, visual, value, flags, material, sample_raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (target_id, *tuple(row)),
        )

    populate_item_classification(dest)

    source_character = source.execute(
        """
        SELECT stable_key, persistent_id, character_kind, name, hp, hp_max, mana, mana_max,
               level, experience, learning_points, attributes_json, talents_json, position_json, raw_json
        FROM mmo_characters
        WHERE import_run_id = ? AND world_instance_id = ?
        ORDER BY id
        LIMIT 1
        """,
        (run_id, source_world_id),
    ).fetchone()
    if source_character is None:
        raise SystemExit("No player character found in source staging DB.")

    (
        char_stable_key,
        persistent_id,
        character_kind,
        char_name,
        hp,
        hp_max,
        mana,
        mana_max,
        level,
        experience,
        learning_points,
        attributes_json,
        talents_json,
        position_json,
        raw_json,
    ) = tuple(source_character)

    character_id = insert_one(
        dest,
        """
        INSERT INTO characters
          (account_id, realm_id, current_world_instance_id, source_stable_key,
           persistent_id, name, character_kind)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (account_id, realm_id, world_instance_id, char_stable_key, persistent_id, char_name, character_kind),
    )
    dest.execute(
        """
        INSERT INTO character_stats
          (character_id, hp, hp_max, mana, mana_max, level, experience, learning_points,
           attributes_json, talents_json, position_json, raw_json)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            character_id,
            hp,
            hp_max,
            mana,
            mana_max,
            level,
            experience,
            learning_points,
            attributes_json,
            talents_json,
            position_json,
            raw_json,
        ),
    )

    for row in source.execute(
        """
        SELECT stable_key, entity_type, display_name, symbol_index, persistent_id, hp, mana, dead,
               mob_state, locked, amount, position_json, stats_json, raw_json
        FROM mmo_replay_entities
        WHERE import_run_id = ? AND world_instance_id = ?
        ORDER BY id
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT INTO world_entity_state
              (world_instance_id, stable_key, entity_type, display_name, symbol_index, persistent_id,
               hp, mana, dead, mob_state, locked, amount, position_json, stats_json, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (world_instance_id, *tuple(row)),
        )

    for row in source.execute(
        """
        SELECT item_stable_key, item_symbol_index, item_name, item_display_name, amount,
               iterator_count, equipped, slot, source_file, raw_json
        FROM mmo_replay_inventory
        WHERE import_run_id = ? AND world_instance_id = ? AND owner_scope = 'character'
        ORDER BY id
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT OR REPLACE INTO character_inventory
              (character_id, item_stable_key, item_symbol_index, item_name, item_display_name,
               amount, iterator_count, equipped, slot, source_file, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (character_id, *tuple(row)),
        )

    for row in source.execute(
        """
        SELECT owner_scope, owner_stable_key, owner_persistent_id, owner_display_name,
               item_stable_key, item_symbol_index, item_name, item_display_name,
               amount, iterator_count, equipped, slot, source_file, raw_json
        FROM mmo_replay_inventory
        WHERE import_run_id = ? AND world_instance_id = ? AND owner_scope <> 'character'
        ORDER BY id
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT INTO world_inventory
              (world_instance_id, owner_scope, owner_stable_key, owner_persistent_id, owner_display_name,
               item_stable_key, item_symbol_index, item_name, item_display_name,
               amount, iterator_count, equipped, slot, source_file, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (world_instance_id, *tuple(row)),
        )

    normalize_inventory(dest, character_id, world_instance_id)

    for row in source.execute(
        """
        SELECT stable_key, name, section, status, entry_count, entries_json, raw_json
        FROM mmo_replay_quest_state
        WHERE import_run_id = ? AND world_instance_id = ?
        ORDER BY id
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT INTO character_quests
              (character_id, stable_key, name, section, status, entry_count, entries_json, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (character_id, *tuple(row)),
        )

    for row in source.execute(
        """
        SELECT stable_key, npc_symbol_name, info_symbol_name, raw_json
        FROM mmo_replay_known_dialog_state
        WHERE import_run_id = ? AND world_instance_id = ?
        ORDER BY id
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT INTO character_known_dialogs
              (character_id, stable_key, npc_symbol_name, info_symbol_name, raw_json)
            VALUES (?, ?, ?, ?, ?)
            """,
            (character_id, *tuple(row)),
        )

    for row in source.execute(
        """
        SELECT stable_key, symbol_index, symbol_name, value_type, category, values_json, raw_json
        FROM mmo_replay_script_global_state
        WHERE import_run_id = ? AND world_instance_id = ? AND scope = 'character'
        ORDER BY id
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT INTO character_script_state
              (character_id, stable_key, symbol_index, symbol_name, value_type, category, values_json, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (character_id, *tuple(row)),
        )

    for row in source.execute(
        """
        SELECT stable_key, symbol_index, symbol_name, value_type, category, values_json, raw_json
        FROM mmo_replay_script_global_state
        WHERE import_run_id = ? AND world_instance_id = ? AND scope = 'world'
        ORDER BY id
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT INTO world_script_state
              (world_instance_id, stable_key, symbol_index, symbol_name, value_type, category, values_json, raw_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (world_instance_id, *tuple(row)),
        )

    for row in source.execute(
        """
        SELECT event_index, event_type, event_class, entity_type, stable_key, name, payload_json
        FROM mmo_event_ledger
        WHERE import_run_id = ? AND world_instance_id = ?
        ORDER BY event_index
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT INTO world_event_journal
              (world_instance_id, source_event_index, event_type, event_class,
               entity_type, stable_key, actor_character_id, name, payload_json)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (world_instance_id, row[0], row[1], row[2], row[3], row[4], character_id, row[5], row[6]),
        )

    for row in source.execute(
        """
        SELECT metric, snapshot_count, replay_count, status
        FROM mmo_replay_validation
        WHERE import_run_id = ? AND world_instance_id = ?
        ORDER BY metric
        """,
        (run_id, source_world_id),
    ):
        dest.execute(
            """
            INSERT INTO world_replay_validation
              (world_instance_id, metric, snapshot_count, replay_count, status)
            VALUES (?, ?, ?, ?, ?)
            """,
            (world_instance_id, *tuple(row)),
        )

    if table_exists(source, "v_mmo_replay_inventory_missing"):
        for row in source.execute(
            """
            SELECT owner_scope, owner_display_name, item_display_name, item_stable_key,
                   json_object(
                     'owner_scope', owner_scope,
                     'owner_display_name', owner_display_name,
                     'item_display_name', item_display_name,
                     'item_name', item_name,
                     'amount', amount,
                     'item_stable_key', item_stable_key,
                     'source_file', source_file
                   )
            FROM v_mmo_replay_inventory_missing
            WHERE import_run_id = ? AND world_instance_id = ?
            ORDER BY owner_display_name, item_display_name
            """,
            (run_id, source_world_id),
        ):
            dest.execute(
                """
                INSERT INTO world_runtime_noise_candidates
                  (world_instance_id, reason, owner_scope, owner_display_name,
                   item_display_name, item_stable_key, raw_json)
                VALUES (?, 'snapshot_inventory_missing_from_replay', ?, ?, ?, ?, ?)
                """,
                (world_instance_id, *tuple(row)),
            )

    dest.execute(
        "INSERT OR REPLACE INTO schema_meta(key, value) VALUES ('last_import_audit_id', ?)",
        (str(audit_id),),
    )


def print_summary(db: sqlite3.Connection) -> None:
    tables = [
        "account_accounts",
        "realm_realms",
        "content_game_targets",
        "content_world_templates",
        "realm_world_instances",
        "content_entity_templates",
        "content_item_templates",
        "content_item_classification",
        "characters",
        "character_inventory",
        "item_instances",
        "character_equipment",
        "character_quests",
        "character_known_dialogs",
        "character_script_state",
        "world_entity_state",
        "world_inventory",
        "world_script_state",
        "world_event_journal",
        "world_runtime_noise_candidates",
    ]
    print("mmo_database:")
    for table in tables:
        print(f"  {table}: {scalar(db, f'SELECT COUNT(*) FROM {table}')}")

    print("character_sheet:")
    for row in db.execute("SELECT * FROM v_character_sheet ORDER BY character_id"):
        print(f"  {row}")

    print("character_inventory_anomalies:")
    rows = db.execute(
        """
        SELECT character_name, item_display_name, row_count, amount_total,
               iterator_total, equipped_rows, anomaly
        FROM v_character_inventory_anomalies
        ORDER BY character_name, item_display_name
        """
    ).fetchall()
    if not rows:
        print("  (none)")
    for row in rows:
        print(f"  {row}")

    print("character_equipment:")
    rows = db.execute(
        """
        SELECT character_name, slot, item_display_name, source_stable_key
        FROM v_character_equipment
        ORDER BY character_name, slot
        """
    ).fetchall()
    if not rows:
        print("  (none)")
    for row in rows:
        print(f"  {row}")

    print("item_class_counts:")
    for row in db.execute(
        """
        SELECT item_class, stack_policy, template_count
        FROM v_item_class_counts
        ORDER BY template_count DESC, item_class
        """
    ):
        print(f"  {row}")

    print("character_item_stacks:")
    for row in db.execute(
        """
        SELECT character_name, item_display_name, item_class, stack_policy,
               instance_rows, quantity_total, iterator_total, equipped_instances
        FROM v_character_item_stacks
        ORDER BY character_name, item_display_name
        """
    ):
        print(f"  {row}")

    print("character_stack_policy_issues:")
    rows = db.execute(
        """
        SELECT character_name, item_display_name, item_class, stack_policy,
               instance_rows, quantity_total, iterator_total, equipped_instances, issue
        FROM v_character_stack_policy_issues
        ORDER BY character_name, item_display_name
        """
    ).fetchall()
    if not rows:
        print("  (none)")
    for row in rows:
        print(f"  {row}")

    print("event_counts:")
    for event_class, event_type, count in db.execute(
        """
        SELECT event_class, event_type, event_count
        FROM v_world_event_counts
        ORDER BY event_count DESC, event_type
        """
    ):
        print(f"  {event_class}.{event_type}: {count}")

    print("replay_validation:")
    for metric, snapshot_count, replay_count, status in db.execute(
        """
        SELECT metric, snapshot_count, replay_count, status
        FROM v_world_replay_validation
        ORDER BY metric
        """
    ):
        print(f"  {metric}: snapshot={snapshot_count} replay={replay_count} status={status}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a production-shaped Gothic MMO SQLite DB from staging.")
    parser.add_argument("--source", required=True, type=Path, help="Staging SQLite database with mmo_replay_* tables.")
    parser.add_argument("--out", required=True, type=Path, help="Output SQLite MMO database.")
    parser.add_argument("--import-run-id", type=int, help="Source import run id. Defaults to latest.")
    parser.add_argument("--world-instance-id", type=int, help="Source world instance id. Defaults to latest.")
    parser.add_argument("--reset", action="store_true", help="Delete output DB before building.")
    args = parser.parse_args()

    if not args.source.is_file():
        raise SystemExit(f"Source DB not found: {args.source}")
    if args.reset and args.out.exists():
        args.out.unlink()
    args.out.parent.mkdir(parents=True, exist_ok=True)

    source = sqlite3.connect(args.source)
    dest = sqlite3.connect(args.out)
    try:
        require_source(source)
        run_id, world_id = latest_pair(source, args.import_run_id, args.world_instance_id)
        create_schema(dest)
        with dest:
            copy_database(source, dest, args.source, run_id, world_id)
        print(f"source: {args.source}")
        print(f"output: {args.out}")
        print(f"source_import_run_id: {run_id}")
        print(f"source_world_instance_id: {world_id}")
        print_summary(dest)
    finally:
        source.close()
        dest.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
