#!/usr/bin/env python3
"""
Validate a generated gothic_mmo.sqlite database against production invariants.

This is a local smoke test for the server-shaped DB before the schema is moved
to a live PostgreSQL instance.
"""

from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path
from typing import Any


REQUIRED_TABLES = [
    "schema_meta",
    "import_audits",
    "account_accounts",
    "account_entitlements",
    "realm_realms",
    "realm_world_instances",
    "content_game_targets",
    "content_world_templates",
    "content_entity_templates",
    "content_item_templates",
    "content_item_classification",
    "characters",
    "character_stats",
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
    "world_replay_validation",
    "world_runtime_noise_candidates",
]

REQUIRED_VIEWS = [
    "v_character_sheet",
    "v_character_inventory",
    "v_item_instances",
    "v_character_equipment",
    "v_character_item_totals",
    "v_character_item_stacks",
    "v_world_item_stacks",
    "v_item_class_counts",
    "v_character_stack_policy_issues",
    "v_world_dead_npcs",
    "v_world_event_counts",
    "v_world_replay_validation",
    "v_runtime_noise_inventory",
]


def scalar(db: sqlite3.Connection, sql: str, args: tuple[Any, ...] = ()) -> Any:
    row = db.execute(sql, args).fetchone()
    return None if row is None else row[0]


def relation_exists(db: sqlite3.Connection, name: str, kind: str) -> bool:
    return scalar(
        db,
        "SELECT 1 FROM sqlite_master WHERE type = ? AND name = ?",
        (kind, name),
    ) is not None


def check(condition: bool, name: str, failures: list[str], details: str = "") -> None:
    if condition:
        print(f"ok: {name}{(' - ' + details) if details else ''}")
    else:
        print(f"FAIL: {name}{(' - ' + details) if details else ''}")
        failures.append(name)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate gothic_mmo.sqlite invariants.")
    parser.add_argument("--db", required=True, type=Path, help="Path to gothic_mmo.sqlite.")
    parser.add_argument("--allow-known-policy-issues", action="store_true", help="Do not fail on stack policy issues.")
    args = parser.parse_args()

    if not args.db.is_file():
        raise SystemExit(f"DB not found: {args.db}")

    db = sqlite3.connect(args.db)
    failures: list[str] = []
    try:
        for table in REQUIRED_TABLES:
            check(relation_exists(db, table, "table"), f"table:{table}", failures)
        for view in REQUIRED_VIEWS:
            check(relation_exists(db, view, "view"), f"view:{view}", failures)

        schema_name = scalar(db, "SELECT value FROM schema_meta WHERE key = 'schema_name'")
        schema_version = scalar(db, "SELECT value FROM schema_meta WHERE key = 'schema_version'")
        check(schema_name == "gothic_mmo", "schema_name", failures, str(schema_name))
        check(schema_version == "3", "schema_version", failures, str(schema_version))

        item_templates = int(scalar(db, "SELECT COUNT(*) FROM content_item_templates") or 0)
        item_classes = int(scalar(db, "SELECT COUNT(*) FROM content_item_classification") or 0)
        check(item_templates > 0, "item_templates_nonempty", failures, str(item_templates))
        check(item_templates == item_classes, "item_classification_coverage", failures, f"{item_classes}/{item_templates}")

        characters = int(scalar(db, "SELECT COUNT(*) FROM characters") or 0)
        character_stats = int(scalar(db, "SELECT COUNT(*) FROM character_stats") or 0)
        check(characters > 0, "characters_nonempty", failures, str(characters))
        check(characters == character_stats, "character_stats_coverage", failures, f"{character_stats}/{characters}")

        char_inventory = int(scalar(db, "SELECT COUNT(*) FROM character_inventory") or 0)
        equipment = int(scalar(db, "SELECT COUNT(*) FROM character_equipment") or 0)
        item_instances = int(scalar(db, "SELECT COUNT(*) FROM item_instances") or 0)
        world_inventory = int(scalar(db, "SELECT COUNT(*) FROM world_inventory") or 0)
        check(char_inventory > 0, "character_inventory_nonempty", failures, str(char_inventory))
        check(item_instances >= char_inventory + world_inventory, "item_instances_cover_inventory", failures, f"{item_instances} vs {char_inventory}+{world_inventory}")
        check(equipment > 0, "equipment_detected", failures, str(equipment))

        events = int(scalar(db, "SELECT COUNT(*) FROM world_event_journal") or 0)
        check(events > 0, "world_event_journal_nonempty", failures, str(events))

        replay_bad = int(scalar(db, "SELECT COUNT(*) FROM world_replay_validation WHERE status <> 'ok' AND metric <> 'inventory_rows'") or 0)
        check(replay_bad == 0, "replay_validation_core_ok", failures, str(replay_bad))

        noise = int(scalar(db, "SELECT COUNT(*) FROM world_runtime_noise_candidates") or 0)
        print(f"info: runtime_noise_candidates={noise}")

        policy_issues = db.execute(
            """
            SELECT character_name, item_display_name, item_class, stack_policy, issue
            FROM v_character_stack_policy_issues
            ORDER BY character_name, item_display_name
            """
        ).fetchall()
        if policy_issues:
            for row in policy_issues:
                print(f"policy_issue: {row}")
        check(
            args.allow_known_policy_issues or len(policy_issues) == 0,
            "stack_policy_issues",
            failures,
            str(len(policy_issues)),
        )

        print("summary:")
        for table in (
            "content_entity_templates",
            "content_item_templates",
            "content_item_classification",
            "characters",
            "character_inventory",
            "item_instances",
            "character_equipment",
            "world_entity_state",
            "world_inventory",
            "world_event_journal",
        ):
            print(f"  {table}: {scalar(db, f'SELECT COUNT(*) FROM {table}')}")
    finally:
        db.close()

    if failures:
        print("failed_checks:")
        for name in failures:
            print(f"  {name}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
