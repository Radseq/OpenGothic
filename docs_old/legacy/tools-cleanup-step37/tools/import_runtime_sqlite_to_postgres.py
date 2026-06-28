#!/usr/bin/env python3
"""
Bootstrap/import runtime OpenGothic SQLite into the production PostgreSQL MMO schema.

This is a one-way migration tool from the local SQLite bridge to the clean
server-owned PostgreSQL contract. It deliberately reads `mmo_*_current` and
`mmo_world_baseline_*` projections, not raw `runtime_*` diagnostics.

Requirements:
  - Python 3.10+
  - sqlite3 from the Python stdlib
  - psql command-line client, unless --dry-run-sql is used
  - production migrations 001 and 002 already applied in PostgreSQL
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


CURRENT_TABLES_FOR_HASH: tuple[str, ...] = (
    "mmo_world_templates",
    "mmo_world_instances",
    "mmo_world_clock_current",
    "mmo_characters_current",
    "mmo_unit_stat_sheet_current",
    "mmo_character_wallet_current",
    "mmo_character_inventory_current",
    "mmo_character_quests_current",
    "mmo_character_known_dialogs_current",
    "mmo_character_story_progress_current",
    "mmo_creature_templates_current",
    "mmo_creature_spawns_current",
    "mmo_creature_inventory_current",
    "mmo_world_items_current",
    "mmo_world_interactives_current",
    "mmo_world_container_inventory_current",
    "mmo_script_globals_current",
    "mmo_script_global_values_current",
)

BASELINE_TABLES_FOR_HASH: tuple[str, ...] = (
    "mmo_world_baseline_creature_templates",
    "mmo_world_baseline_creatures",
    "mmo_world_baseline_creature_stats",
    "mmo_world_baseline_creature_inventory",
    "mmo_world_baseline_items",
    "mmo_world_baseline_interactives",
    "mmo_world_baseline_container_inventory",
    "mmo_world_baseline_script_globals",
    "mmo_world_baseline_script_global_values",
)


@dataclass(frozen=True)
class SourceMeta:
    schema_name: str
    schema_version: int | None
    source_fingerprint: str
    worlds_hash: str
    items_hash: str
    npcs_hash: str
    script_hash: str
    counts: dict[str, int]


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def open_sqlite(path: Path) -> sqlite3.Connection:
    if not path.exists():
        fail(f"SQLite file does not exist: {path}")
    con = sqlite3.connect(str(path))
    con.row_factory = sqlite3.Row
    return con


def table_names(con: sqlite3.Connection) -> set[str]:
    rows = con.execute("SELECT name FROM sqlite_master WHERE type='table'").fetchall()
    return {str(r[0]) for r in rows}


def columns(con: sqlite3.Connection, table: str) -> set[str]:
    if table not in table_names(con):
        return set()
    return {str(r[1]) for r in con.execute(f"PRAGMA table_info({quote_ident_sqlite(table)})")}


def quote_ident_sqlite(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def rows(con: sqlite3.Connection, table: str, where: str = "", params: Sequence[Any] = ()) -> list[dict[str, Any]]:
    if table not in table_names(con):
        return []
    sql = f"SELECT * FROM {quote_ident_sqlite(table)}"
    if where:
        sql += " WHERE " + where
    return [dict(r) for r in con.execute(sql, params).fetchall()]


def first_row(con: sqlite3.Connection, table: str, where: str = "", params: Sequence[Any] = ()) -> dict[str, Any] | None:
    r = rows(con, table, where, params)
    return r[0] if r else None


def scalar(con: sqlite3.Connection, sql: str, params: Sequence[Any] = ()) -> Any:
    row = con.execute(sql, params).fetchone()
    return None if row is None else row[0]


def table_count(con: sqlite3.Connection, table: str) -> int:
    if table not in table_names(con):
        return 0
    return int(scalar(con, f"SELECT count(*) FROM {quote_ident_sqlite(table)}") or 0)


def stable_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), default=str)


def hash_rows(con: sqlite3.Connection, tables: Iterable[str]) -> str:
    h = hashlib.sha256()
    names = table_names(con)
    for table in tables:
        h.update(table.encode("utf-8"))
        h.update(b"\0")
        if table not in names:
            h.update(b"missing\0")
            continue
        cols = sorted(columns(con, table))
        order_by = ", ".join(quote_ident_sqlite(c) for c in cols) if cols else "rowid"
        for row in con.execute(f"SELECT * FROM {quote_ident_sqlite(table)} ORDER BY {order_by}"):
            h.update(stable_json(dict(row)).encode("utf-8"))
            h.update(b"\n")
    return h.hexdigest()


def source_meta(con: sqlite3.Connection) -> SourceMeta:
    meta = {str(r[0]): str(r[1]) for r in con.execute("SELECT key, value FROM runtime_schema_meta").fetchall()} if "runtime_schema_meta" in table_names(con) else {}
    counts = {t: table_count(con, t) for t in (*CURRENT_TABLES_FOR_HASH, *BASELINE_TABLES_FOR_HASH) if t in table_names(con)}
    worlds_hash = hash_rows(con, ["mmo_world_templates", "mmo_world_clock_current", "mmo_world_items_current", "mmo_world_interactives_current", "mmo_world_container_inventory_current", *BASELINE_TABLES_FOR_HASH])
    items_hash = hash_rows(con, ["mmo_character_inventory_current", "mmo_character_wallet_current", "mmo_world_items_current", "mmo_world_container_inventory_current", "mmo_creature_inventory_current", "mmo_world_baseline_items", "mmo_world_baseline_container_inventory"])
    npcs_hash = hash_rows(con, ["mmo_creature_templates_current", "mmo_creature_spawns_current", "mmo_unit_stat_sheet_current", "mmo_world_baseline_creature_templates", "mmo_world_baseline_creatures", "mmo_world_baseline_creature_stats"])
    script_hash = hash_rows(con, ["mmo_character_quests_current", "mmo_character_known_dialogs_current", "mmo_character_story_progress_current", "mmo_script_globals_current", "mmo_script_global_values_current", "mmo_world_baseline_script_globals", "mmo_world_baseline_script_global_values"])
    all_hash = hashlib.sha256(stable_json({
        "schema": meta,
        "counts": counts,
        "worlds_hash": worlds_hash,
        "items_hash": items_hash,
        "npcs_hash": npcs_hash,
        "script_hash": script_hash,
    }).encode("utf-8")).hexdigest()
    version_raw = meta.get("schema_version")
    try:
        version = int(version_raw) if version_raw is not None else None
    except ValueError:
        version = None
    return SourceMeta(
        schema_name=meta.get("schema_name", ""),
        schema_version=version,
        source_fingerprint=all_hash,
        worlds_hash=worlds_hash,
        items_hash=items_hash,
        npcs_hash=npcs_hash,
        script_hash=script_hash,
        counts=counts,
    )


def pg_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if value != value:
            return "NULL"
        return repr(value)
    text = str(value)
    return "'" + text.replace("'", "''") + "'"


def pg_jsonb(value: Any) -> str:
    return f"{pg_literal(json.dumps(value, ensure_ascii=False, sort_keys=True, default=str))}::jsonb"


def clean_key(value: Any, fallback: str) -> str:
    text = str(value or "").strip()
    return text if text else fallback


def int_or_none(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def number_or_zero(value: Any) -> int:
    n = int_or_none(value)
    return 0 if n is None else n


def bool_from_int(value: Any) -> bool:
    return bool(number_or_zero(value))


def quest_status(raw: Any) -> str:
    return {
        1: "running",
        2: "success",
        3: "failed",
        4: "obsolete",
    }.get(number_or_zero(raw), "running")


def dialog_availability(known: bool, permanent: bool) -> str:
    if known and permanent:
        return "repeatable_known"
    if known and not permanent:
        return "consumed_hidden"
    return "unknown"


def equipment_slot(slot: Any, equipped: Any) -> str | None:
    if not bool_from_int(equipped):
        return None
    s = number_or_zero(slot)
    if s == 1:
        return "weapon_melee"
    if s == 2:
        return "weapon_ranged"
    # Gothic stores armor/rings/belt/amulet with NSLOT here; keep those in inventory raw_payload
    # until a later semantic equipment mapper classifies by item flags/template.
    return None


def item_classification(symbol: Any, currency_symbols: set[int]) -> tuple[str, str]:
    sym = int_or_none(symbol)
    if sym is not None and sym in currency_symbols:
        return "currency", "currency"
    return "unknown", "unknown"


def world_key(world_name: str) -> str:
    return world_name.strip().lower().replace("\\", "/") or "unknown-world"


class SqlScript:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def add(self, sql: str = "") -> None:
        self.lines.append(sql)

    def comment(self, text: str) -> None:
        self.lines.append(f"-- {text}")

    def text(self) -> str:
        return "\n".join(self.lines) + "\n"


def sub_game_target_id(game_code: str) -> str:
    return f"(SELECT game_target_id FROM content_game_targets WHERE game_code={pg_literal(game_code)})"


def sub_revision_id(revision_key: str) -> str:
    return f"(SELECT content_revision_id FROM content_revisions WHERE content_revision_key={pg_literal(revision_key)})"


def sub_world_template_id(revision_key: str, wkey: str) -> str:
    return (
        "(SELECT world_template_id FROM content_world_templates "
        f"WHERE content_revision_id={sub_revision_id(revision_key)} AND world_key={pg_literal(wkey)})"
    )


def sub_realm_id(realm_key: str) -> str:
    return f"(SELECT realm_id FROM realm_realms WHERE realm_key={pg_literal(realm_key)})"


def sub_world_instance_id(world_instance_key: str) -> str:
    return f"(SELECT world_instance_id FROM realm_world_instances WHERE world_instance_key={pg_literal(world_instance_key)})"


def sub_character_id(character_key: str) -> str:
    return f"(SELECT character_id FROM characters WHERE character_key={pg_literal(character_key)})"


def sub_item_template_id(revision_key: str, item_template_key: str) -> str:
    return (
        "(SELECT item_template_id FROM content_item_templates "
        f"WHERE content_revision_id={sub_revision_id(revision_key)} AND item_template_key={pg_literal(item_template_key)})"
    )


def sub_entity_template_id(revision_key: str, entity_kind: str, engine_key: str) -> str:
    return (
        "(SELECT entity_template_id FROM content_entity_templates "
        f"WHERE content_revision_id={sub_revision_id(revision_key)} "
        f"AND entity_kind={pg_literal(entity_kind)} AND engine_template_key={pg_literal(engine_key)})"
    )


def sub_item_instance_id(item_instance_key: str) -> str:
    return f"(SELECT item_instance_id FROM item_instances WHERE item_instance_key={pg_literal(item_instance_key)})"


def collect_worlds(con: sqlite3.Connection) -> list[dict[str, Any]]:
    worlds: dict[str, dict[str, Any]] = {}

    for row in rows(con, "mmo_world_templates"):
        name = clean_key(row.get("world_name"), "unknown-world")
        wkey = clean_key(row.get("world_template_key"), world_key(name))
        worlds[name] = {
            "world_key": wkey,
            "world_name": name,
            "zen_path": name,
            "baseline_tick": number_or_zero(row.get("baseline_tick")),
            "baseline_world_time_ms": number_or_zero(row.get("baseline_world_time_millis")),
            "baseline_payload": row,
        }

    for table in ("mmo_world_clock_current", "mmo_characters_current", "mmo_creature_spawns_current", "mmo_world_items_current", "mmo_world_interactives_current"):
        for row in rows(con, table):
            name = clean_key(row.get("world_name"), "unknown-world")
            worlds.setdefault(name, {
                "world_key": world_key(name),
                "world_name": name,
                "zen_path": name,
                "baseline_tick": number_or_zero(row.get("tick_count")),
                "baseline_world_time_ms": number_or_zero(row.get("world_time_millis")),
                "baseline_payload": {"source_table": table},
            })

    if not worlds:
        fail("No world rows found in mmo_world_templates/mmo_*_current tables")
    return list(worlds.values())


def collect_item_templates(con: sqlite3.Connection, currency_symbols: set[int]) -> dict[str, dict[str, Any]]:
    out: dict[str, dict[str, Any]] = {}

    def add(symbol: Any, name: Any, raw: Mapping[str, Any]) -> None:
        sym = int_or_none(symbol)
        if sym is None:
            key = "item-hash:" + hashlib.sha256(stable_json(raw).encode("utf-8")).hexdigest()[:16]
        else:
            key = f"item-symbol:{sym}"
        classification, stack_policy = item_classification(sym, currency_symbols)
        current = out.get(key)
        if current is None or (not current.get("display_name") and name):
            out[key] = {
                "item_template_key": key,
                "symbol_index": sym,
                "display_name": str(name or ""),
                "classification": classification,
                "stack_policy": stack_policy,
                "value": int_or_none(raw.get("value")),
                "raw_payload": dict(raw),
            }
        elif classification == "currency":
            current["classification"] = "currency"
            current["stack_policy"] = "currency"

    for table, symbol_col, name_col in (
        ("mmo_character_inventory_current", "item_template_symbol", "item_display_name"),
        ("mmo_creature_inventory_current", "item_template_symbol", "item_display_name"),
        ("mmo_world_items_current", "item_template_symbol", "item_display_name"),
        ("mmo_world_container_inventory_current", "item_template_symbol", "item_display_name"),
        ("mmo_world_baseline_items", "item_template_symbol", "item_display_name"),
        ("mmo_world_baseline_container_inventory", "item_template_symbol", "item_display_name"),
        ("mmo_world_baseline_creature_inventory", "item_template_symbol", "item_display_name"),
    ):
        for row in rows(con, table):
            add(row.get(symbol_col), row.get(name_col), row)

    for row in rows(con, "mmo_character_wallet_current"):
        add(row.get("item_template_symbol"), row.get("currency_display_name") or row.get("currency_key"), row)

    return out


def collect_entity_templates(con: sqlite3.Connection) -> list[dict[str, Any]]:
    out: dict[tuple[str, str], dict[str, Any]] = {}

    def add(kind: str, key: str, row: Mapping[str, Any], symbol: Any = None, script_id: Any = None, name: Any = None) -> None:
        if not key:
            return
        out[(kind, key)] = {
            "entity_kind": kind,
            "engine_template_key": key,
            "symbol_index": int_or_none(symbol),
            "script_id": int_or_none(script_id),
            "display_name": str(name or ""),
            "raw_payload": dict(row),
        }

    for row in rows(con, "mmo_creature_templates_current"):
        add("creature", clean_key(row.get("creature_template_key"), f"creature-template:{row.get('creature_template_id')}"), row, row.get("creature_template_id"), row.get("script_id"), row.get("display_name"))
    for row in rows(con, "mmo_world_baseline_creature_templates"):
        add("creature", clean_key(row.get("creature_template_key"), f"creature-template:{row.get('creature_template_id')}"), row, row.get("creature_template_id"), row.get("script_id"), row.get("display_name"))
    for row in rows(con, "mmo_world_items_current"):
        symbol = row.get("item_template_symbol")
        add("item", f"item-symbol:{symbol}", row, symbol, row.get("script_id"), row.get("item_display_name"))
    for row in rows(con, "mmo_world_baseline_items"):
        symbol = row.get("item_template_symbol")
        add("item", f"item-symbol:{symbol}", row, symbol, row.get("script_id"), row.get("item_display_name"))
    for row in rows(con, "mmo_world_interactives_current"):
        key = clean_key(row.get("scheme"), "") or clean_key(row.get("tag"), "") or f"interactive-vob:{row.get('vob_id')}"
        add("interactive", f"interactive:{key}", row, row.get("vob_id"), None, row.get("display_name"))
    for row in rows(con, "mmo_world_baseline_interactives"):
        key = clean_key(row.get("scheme"), "") or clean_key(row.get("tag"), "") or f"interactive-vob:{row.get('vob_id')}"
        add("interactive", f"interactive:{key}", row, row.get("vob_id"), None, row.get("display_name"))

    return list(out.values())


def build_import_sql(con: sqlite3.Connection, args: argparse.Namespace) -> str:
    names = table_names(con)
    required = {"mmo_characters_current", "mmo_unit_stat_sheet_current"}
    missing = sorted(required - names)
    if missing:
        fail(f"SQLite does not contain required production projection tables: {', '.join(missing)}")

    meta = source_meta(con)
    import_run_id = str(uuid.uuid4())
    content_revision_key = args.content_revision_key or f"runtime-sqlite:{args.game_code}:{meta.source_fingerprint[:16]}"
    migration_hash = hashlib.sha256((Path(__file__).name + "|v1").encode("utf-8")).hexdigest()
    worlds = collect_worlds(con)

    hero = first_row(con, "mmo_characters_current", "character_key = ?", [args.character_key])
    if hero is None:
        hero = first_row(con, "mmo_characters_current")
    if hero is None:
        fail("No character rows found in mmo_characters_current")
    character_key = clean_key(hero.get("character_key"), args.character_key)
    character_name = clean_key(hero.get("display_name"), character_key)
    hero_world_name = clean_key(hero.get("world_name"), worlds[0]["world_name"])
    hero_world_key = next((w["world_key"] for w in worlds if w["world_name"] == hero_world_name), world_key(hero_world_name))
    world_instance_key_for_hero = f"{args.realm_key}:{hero_world_key}:1"

    unit = first_row(con, "mmo_unit_stat_sheet_current", "character_key = ?", [character_key])
    if unit is None:
        unit = first_row(con, "mmo_unit_stat_sheet_current", "unit_key = ?", [character_key])
    unit = unit or {}

    currency_symbols = {number_or_zero(r.get("item_template_symbol")) for r in rows(con, "mmo_character_wallet_current")}
    item_templates = collect_item_templates(con, currency_symbols)
    entity_templates = collect_entity_templates(con)

    script = SqlScript()
    script.comment("Generated by tools/import_runtime_sqlite_to_postgres.py")
    script.comment(f"Import run: {import_run_id}")
    script.add("BEGIN;")
    script.add("SET CONSTRAINTS ALL IMMEDIATE;")

    script.add("INSERT INTO content_game_targets(game_code, display_name, engine, save_format_version)")
    script.add(f"VALUES ({pg_literal(args.game_code)}, {pg_literal(args.game_display_name)}, 'opengothic', NULL)")
    script.add("ON CONFLICT(game_code) DO UPDATE SET display_name=EXCLUDED.display_name;")

    if args.activate_content:
        script.add(f"UPDATE content_revisions SET is_active=FALSE WHERE game_target_id={sub_game_target_id(args.game_code)};")

    script.add("INSERT INTO content_revisions(game_target_id, content_revision_key, script_symbols_hash, worlds_hash, items_hash, npcs_hash, migration_hash, source_description, is_active)")
    script.add(
        "SELECT game_target_id, "
        f"{pg_literal(content_revision_key)}, {pg_literal(meta.script_hash)}, {pg_literal(meta.worlds_hash)}, "
        f"{pg_literal(meta.items_hash)}, {pg_literal(meta.npcs_hash)}, {pg_literal(migration_hash)}, "
        f"{pg_literal(args.source_description)}, {pg_literal(bool(args.activate_content))} "
        f"FROM content_game_targets WHERE game_code={pg_literal(args.game_code)}"
    )
    script.add("ON CONFLICT(content_revision_key) DO UPDATE SET script_symbols_hash=EXCLUDED.script_symbols_hash, worlds_hash=EXCLUDED.worlds_hash, items_hash=EXCLUDED.items_hash, npcs_hash=EXCLUDED.npcs_hash, migration_hash=EXCLUDED.migration_hash, source_description=EXCLUDED.source_description, is_active=EXCLUDED.is_active;")

    script.add("INSERT INTO mmo_import_runs(import_run_id, source_system, source_path, source_fingerprint, source_schema_name, source_schema_version, import_mode, game_code, content_revision_id, status, counters, diagnostics)")
    script.add(
        f"VALUES ({pg_literal(import_run_id)}::uuid, 'runtime_sqlite', {pg_literal(str(args.sqlite))}, {pg_literal(meta.source_fingerprint)}, "
        f"{pg_literal(meta.schema_name)}, {pg_literal(meta.schema_version)}, 'bootstrap', {pg_literal(args.game_code)}, {sub_revision_id(content_revision_key)}, 'started', "
        f"{pg_jsonb(meta.counts)}, {pg_jsonb({'source': 'runtime sqlite production projection', 'tool': 'import_runtime_sqlite_to_postgres.py'})})"
    )
    script.add("ON CONFLICT(import_run_id) DO NOTHING;")

    for w in worlds:
        wkey = clean_key(w["world_key"], world_key(w["world_name"]))
        baseline_hash = hashlib.sha256(stable_json(w).encode("utf-8")).hexdigest()
        script.add("INSERT INTO content_world_templates(content_revision_id, world_key, world_name, zen_path, baseline_hash, baseline_tick, baseline_world_time_ms, baseline_payload)")
        script.add(
            f"VALUES ({sub_revision_id(content_revision_key)}, {pg_literal(wkey)}, {pg_literal(w['world_name'])}, {pg_literal(w['zen_path'])}, "
            f"{pg_literal(baseline_hash)}, {pg_literal(number_or_zero(w.get('baseline_tick')))}, {pg_literal(number_or_zero(w.get('baseline_world_time_ms')))}, {pg_jsonb(w.get('baseline_payload', {}))})"
        )
        script.add("ON CONFLICT(content_revision_id, world_key) DO UPDATE SET world_name=EXCLUDED.world_name, zen_path=EXCLUDED.zen_path, baseline_hash=EXCLUDED.baseline_hash, baseline_tick=EXCLUDED.baseline_tick, baseline_world_time_ms=EXCLUDED.baseline_world_time_ms, baseline_payload=EXCLUDED.baseline_payload;")
        script.add("INSERT INTO mmo_import_object_map(import_run_id, source_table, source_key, target_table, target_key, raw_hash)")
        script.add(f"VALUES ({pg_literal(import_run_id)}::uuid, 'mmo_world_templates', {pg_literal(wkey)}, 'content_world_templates', {pg_literal(wkey)}, {pg_literal(baseline_hash)}) ON CONFLICT DO NOTHING;")

    for et in entity_templates:
        raw_hash = hashlib.sha256(stable_json(et["raw_payload"]).encode("utf-8")).hexdigest()
        script.add("INSERT INTO content_entity_templates(content_revision_id, entity_kind, engine_template_key, symbol_index, script_id, display_name, raw_payload)")
        script.add(
            f"VALUES ({sub_revision_id(content_revision_key)}, {pg_literal(et['entity_kind'])}, {pg_literal(et['engine_template_key'])}, "
            f"{pg_literal(et.get('symbol_index'))}, {pg_literal(et.get('script_id'))}, {pg_literal(et.get('display_name', ''))}, {pg_jsonb(et['raw_payload'])})"
        )
        script.add("ON CONFLICT(content_revision_id, entity_kind, engine_template_key) DO UPDATE SET symbol_index=EXCLUDED.symbol_index, script_id=EXCLUDED.script_id, display_name=EXCLUDED.display_name, raw_payload=EXCLUDED.raw_payload;")
        script.add("INSERT INTO mmo_import_object_map(import_run_id, source_table, source_key, target_table, target_key, raw_hash)")
        script.add(f"VALUES ({pg_literal(import_run_id)}::uuid, 'entity-template-scan', {pg_literal(et['engine_template_key'])}, 'content_entity_templates', {pg_literal(et['engine_template_key'])}, {pg_literal(raw_hash)}) ON CONFLICT DO NOTHING;")

    for it in item_templates.values():
        raw_hash = hashlib.sha256(stable_json(it["raw_payload"]).encode("utf-8")).hexdigest()
        script.add("INSERT INTO content_item_templates(content_revision_id, item_template_key, symbol_index, display_name, classification, stack_policy, value, raw_payload)")
        script.add(
            f"VALUES ({sub_revision_id(content_revision_key)}, {pg_literal(it['item_template_key'])}, {pg_literal(it.get('symbol_index'))}, "
            f"{pg_literal(it.get('display_name', ''))}, {pg_literal(it.get('classification', 'unknown'))}, {pg_literal(it.get('stack_policy', 'unknown'))}, "
            f"{pg_literal(it.get('value'))}, {pg_jsonb(it['raw_payload'])})"
        )
        script.add("ON CONFLICT(content_revision_id, item_template_key) DO UPDATE SET symbol_index=EXCLUDED.symbol_index, display_name=EXCLUDED.display_name, classification=EXCLUDED.classification, stack_policy=EXCLUDED.stack_policy, value=EXCLUDED.value, raw_payload=EXCLUDED.raw_payload;")
        script.add("INSERT INTO mmo_import_object_map(import_run_id, source_table, source_key, target_table, target_key, raw_hash)")
        script.add(f"VALUES ({pg_literal(import_run_id)}::uuid, 'item-template-scan', {pg_literal(it['item_template_key'])}, 'content_item_templates', {pg_literal(it['item_template_key'])}, {pg_literal(raw_hash)}) ON CONFLICT DO NOTHING;")

    script.add("INSERT INTO realm_realms(game_target_id, active_content_revision_id, realm_key, display_name, status, max_players)")
    script.add(
        f"VALUES ({sub_game_target_id(args.game_code)}, {sub_revision_id(content_revision_key)}, {pg_literal(args.realm_key)}, {pg_literal(args.realm_display_name)}, {pg_literal(args.realm_status)}, {pg_literal(args.max_players)})"
    )
    script.add("ON CONFLICT(realm_key) DO UPDATE SET active_content_revision_id=EXCLUDED.active_content_revision_id, display_name=EXCLUDED.display_name, status=EXCLUDED.status, max_players=EXCLUDED.max_players;")

    for w in worlds:
        wkey = clean_key(w["world_key"], world_key(w["world_name"]))
        instance_key = f"{args.realm_key}:{wkey}:1"
        clock = first_row(con, "mmo_world_clock_current", "world_name = ?", [w["world_name"]]) or {}
        script.add("INSERT INTO realm_world_instances(realm_id, world_template_id, world_instance_key, lifecycle_state, generation, current_tick, current_world_time_ms)")
        script.add(
            f"VALUES ({sub_realm_id(args.realm_key)}, {sub_world_template_id(content_revision_key, wkey)}, {pg_literal(instance_key)}, 'active', 1, "
            f"{pg_literal(number_or_zero(clock.get('tick_count') or w.get('baseline_tick')))}, {pg_literal(number_or_zero(clock.get('world_time_millis') or w.get('baseline_world_time_ms')))})"
        )
        script.add("ON CONFLICT(world_instance_key) DO UPDATE SET lifecycle_state=EXCLUDED.lifecycle_state, current_tick=EXCLUDED.current_tick, current_world_time_ms=EXCLUDED.current_world_time_ms;")

    script.add("INSERT INTO account_accounts(account_name, auth_provider, flags)")
    script.add(f"VALUES ({pg_literal(args.account_name)}, 'local', {pg_jsonb({'imported_from': 'runtime_sqlite'})})")
    script.add("ON CONFLICT(account_name) DO UPDATE SET flags=account_accounts.flags || EXCLUDED.flags;")
    script.add("INSERT INTO account_entitlements(account_id, game_code, entitlement_key, source, status, metadata)")
    script.add(f"SELECT account_id, {pg_literal(args.game_code)}, 'gothic-runtime-import', 'import', 'active', {pg_jsonb({'import_run_id': import_run_id})} FROM account_accounts WHERE account_name={pg_literal(args.account_name)}")
    script.add("ON CONFLICT(account_id, game_code, entitlement_key) DO UPDATE SET status='active', metadata=account_entitlements.metadata || EXCLUDED.metadata;")

    script.add("INSERT INTO characters(account_id, realm_id, current_world_instance_id, character_key, character_name, lifecycle_state, metadata)")
    script.add(
        f"SELECT a.account_id, {sub_realm_id(args.realm_key)}, {sub_world_instance_id(world_instance_key_for_hero)}, {pg_literal(character_key)}, {pg_literal(character_name)}, 'active', {pg_jsonb({'source_character_row': hero})} "
        f"FROM account_accounts a WHERE a.account_name={pg_literal(args.account_name)}"
    )
    script.add("ON CONFLICT(character_key) DO UPDATE SET current_world_instance_id=EXCLUDED.current_world_instance_id, character_name=EXCLUDED.character_name, lifecycle_state=EXCLUDED.lifecycle_state, metadata=characters.metadata || EXCLUDED.metadata;")

    pos_x = unit.get("pos_x", hero.get("pos_x"))
    pos_y = unit.get("pos_y", hero.get("pos_y"))
    pos_z = unit.get("pos_z", hero.get("pos_z"))
    rotation = unit.get("rotation", hero.get("rotation"))
    tick_count = number_or_zero(unit.get("tick_count", hero.get("tick_count")))
    script.add("INSERT INTO character_positions(character_id, world_instance_id, pos_x, pos_y, pos_z, rotation_yaw, current_waypoint_key, server_tick, row_version)")
    script.add(
        f"VALUES ({sub_character_id(character_key)}, {sub_world_instance_id(world_instance_key_for_hero)}, {pg_literal(pos_x or 0)}, {pg_literal(pos_y or 0)}, {pg_literal(pos_z or 0)}, {pg_literal(rotation or 0)}, {pg_literal(unit.get('waypoint'))}, {pg_literal(tick_count)}, 1)"
    )
    script.add("ON CONFLICT(character_id) DO UPDATE SET world_instance_id=EXCLUDED.world_instance_id, pos_x=EXCLUDED.pos_x, pos_y=EXCLUDED.pos_y, pos_z=EXCLUDED.pos_z, rotation_yaw=EXCLUDED.rotation_yaw, current_waypoint_key=EXCLUDED.current_waypoint_key, server_tick=EXCLUDED.server_tick, row_version=character_positions.row_version+1;")

    hp = max(0, number_or_zero(unit.get("health_current", hero.get("health_current"))))
    hpmax = max(hp, number_or_zero(unit.get("health_max", hero.get("health_max"))))
    mana = max(0, number_or_zero(unit.get("mana_current", hero.get("mana_current"))))
    manamax = max(mana, number_or_zero(unit.get("mana_max", hero.get("mana_max"))))
    script.add("INSERT INTO character_stats(character_id, level, experience, experience_next, learning_points, health_current, health_max, mana_current, mana_max, strength, dexterity, guild, true_guild, permanent_attitude, temporary_attitude, raw_stats, row_version)")
    script.add(
        f"VALUES ({sub_character_id(character_key)}, {pg_literal(number_or_zero(unit.get('level', hero.get('level'))))}, {pg_literal(number_or_zero(unit.get('experience', hero.get('experience'))))}, "
        f"{pg_literal(int_or_none(unit.get('experience_next')))}, {pg_literal(number_or_zero(unit.get('learning_points')))}, {pg_literal(hp)}, {pg_literal(hpmax)}, {pg_literal(mana)}, {pg_literal(manamax)}, "
        f"{pg_literal(number_or_zero(unit.get('strength')))}, {pg_literal(number_or_zero(unit.get('dexterity')))}, {pg_literal(int_or_none(unit.get('guild')))}, {pg_literal(int_or_none(unit.get('true_guild')))}, "
        f"{pg_literal(int_or_none(unit.get('permanent_attitude')))}, {pg_literal(int_or_none(unit.get('temporary_attitude')))}, {pg_jsonb(unit)}, 1)"
    )
    script.add("ON CONFLICT(character_id) DO UPDATE SET level=EXCLUDED.level, experience=EXCLUDED.experience, experience_next=EXCLUDED.experience_next, learning_points=EXCLUDED.learning_points, health_current=EXCLUDED.health_current, health_max=EXCLUDED.health_max, mana_current=EXCLUDED.mana_current, mana_max=EXCLUDED.mana_max, strength=EXCLUDED.strength, dexterity=EXCLUDED.dexterity, guild=EXCLUDED.guild, true_guild=EXCLUDED.true_guild, permanent_attitude=EXCLUDED.permanent_attitude, temporary_attitude=EXCLUDED.temporary_attitude, raw_stats=EXCLUDED.raw_stats, row_version=character_stats.row_version+1;")

    for wallet in rows(con, "mmo_character_wallet_current", "character_key = ?", [character_key]):
        script.add("INSERT INTO character_wallets(character_id, currency_key, amount)")
        script.add(f"VALUES ({sub_character_id(character_key)}, {pg_literal(wallet.get('currency_key'))}, {pg_literal(number_or_zero(wallet.get('amount')))})")
        script.add("ON CONFLICT(character_id, currency_key) DO UPDATE SET amount=EXCLUDED.amount;")

    # Character inventory and equipment. Currency remains wallet-owned, but item rows are kept as source-faithful imported instances.
    for idx, inv in enumerate(rows(con, "mmo_character_inventory_current", "character_key = ?", [character_key])):
        symbol = int_or_none(inv.get("item_template_symbol"))
        item_template_key = f"item-symbol:{symbol}" if symbol is not None else "item-hash:" + hashlib.sha256(stable_json(inv).encode("utf-8")).hexdigest()[:16]
        imported_key = f"import:{content_revision_key}:character:{character_key}:{clean_key(inv.get('item_instance_key'), str(idx))}"
        raw = dict(inv)
        raw["source_owner"] = character_key
        raw["source_order"] = idx
        amount = max(1, number_or_zero(inv.get("amount")))
        script.add("INSERT INTO item_instances(realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, raw_payload)")
        script.add(
            f"VALUES ({sub_realm_id(args.realm_key)}, {sub_item_template_id(content_revision_key, item_template_key)}, {pg_literal(imported_key)}, 'character', {sub_character_id(character_key)}, {pg_literal(amount)}, {pg_jsonb(raw)})"
        )
        script.add("ON CONFLICT(item_instance_key) DO UPDATE SET item_template_id=EXCLUDED.item_template_id, owner_type=EXCLUDED.owner_type, owner_id=EXCLUDED.owner_id, quantity=EXCLUDED.quantity, raw_payload=EXCLUDED.raw_payload;")
        script.add("INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)")
        script.add(
            f"VALUES ({sub_character_id(character_key)}, {sub_item_instance_id(imported_key)}, NULL, {pg_literal(amount)}, {pg_literal(int_or_none(inv.get('amount')))}, {pg_literal(int_or_none(inv.get('iterator_count')))})"
        )
        script.add("ON CONFLICT(character_id, item_instance_id) DO UPDATE SET bag_index=EXCLUDED.bag_index, amount=EXCLUDED.amount, source_amount=EXCLUDED.source_amount, source_iterator_count=EXCLUDED.source_iterator_count;")
        slot_name = equipment_slot(inv.get("slot"), inv.get("equipped"))
        if slot_name:
            script.add("INSERT INTO character_equipment(character_id, equipment_slot, item_instance_id)")
            script.add(f"VALUES ({sub_character_id(character_key)}, {pg_literal(slot_name)}, {sub_item_instance_id(imported_key)})")
            script.add("ON CONFLICT(character_id, equipment_slot) DO UPDATE SET item_instance_id=EXCLUDED.item_instance_id;")

    for quest in rows(con, "mmo_character_quests_current", "character_key = ?", [character_key]):
        entries_text = quest.get("entries_text") or ""
        entries = [line for line in str(entries_text).split("\n") if line]
        script.add("INSERT INTO character_quests(character_id, quest_key, section, status, entry_order, text_entries)")
        script.add(
            f"VALUES ({sub_character_id(character_key)}, {pg_literal(quest.get('quest_key'))}, {pg_literal(str(quest.get('section') or ''))}, {pg_literal(quest_status(quest.get('status')))}, {pg_literal(number_or_zero(quest.get('entry_count')))}, {pg_jsonb(entries)})"
        )
        script.add("ON CONFLICT(character_id, quest_key) DO UPDATE SET section=EXCLUDED.section, status=EXCLUDED.status, entry_order=EXCLUDED.entry_order, text_entries=EXCLUDED.text_entries;")

    for dialog in rows(con, "mmo_character_known_dialogs_current", "character_key = ?", [character_key]):
        known = True
        permanent = bool_from_int(dialog.get("permanent"))
        npc_key = clean_key(dialog.get("npc_symbol_name"), f"npc-symbol:{dialog.get('npc_symbol_index')}")
        info_key = clean_key(dialog.get("info_symbol_name"), f"info-symbol:{dialog.get('info_symbol_index')}")
        script.add("INSERT INTO character_known_dialogs(character_id, npc_key, info_key, known, permanent, availability_state)")
        script.add(f"VALUES ({sub_character_id(character_key)}, {pg_literal(npc_key)}, {pg_literal(info_key)}, TRUE, {pg_literal(permanent)}, {pg_literal(dialog_availability(known, permanent))})")
        script.add("ON CONFLICT(character_id, npc_key, info_key) DO UPDATE SET known=EXCLUDED.known, permanent=EXCLUDED.permanent, availability_state=EXCLUDED.availability_state;")

    for story in rows(con, "mmo_character_story_progress_current", "character_key = ?", [character_key]):
        script_key = clean_key(story.get("chapter_key"), "story:chapter")
        script.add("INSERT INTO character_script_state(character_id, script_key, symbol_index, value_type, value_index, value_int, value_text)")
        script.add(
            f"VALUES ({sub_character_id(character_key)}, {pg_literal(script_key)}, {pg_literal(int_or_none(story.get('source_symbol_index')))}, 'int', 0, {pg_literal(number_or_zero(story.get('chapter_number')))}, {pg_literal(story.get('source_symbol_name'))})"
        )
        script.add("ON CONFLICT(character_id, script_key, value_index) DO UPDATE SET symbol_index=EXCLUDED.symbol_index, value_type=EXCLUDED.value_type, value_int=EXCLUDED.value_int, value_text=EXCLUDED.value_text;")

    # Persistent world entities: creatures/NPCs, loose world items and interactives.
    for creature in rows(con, "mmo_creature_spawns_current"):
        wname = clean_key(creature.get("world_name"), hero_world_name)
        wkey = next((w["world_key"] for w in worlds if w["world_name"] == wname), world_key(wname))
        instance_key = f"{args.realm_key}:{wkey}:1"
        template_key = f"creature-template:{creature.get('creature_template_id')}"
        entity_key = clean_key(creature.get("creature_spawn_key"), template_key)
        lifecycle = "dead" if bool_from_int(creature.get("dead")) else "active"
        script.add("INSERT INTO world_entity_state(world_instance_id, entity_key, entity_kind, entity_template_id, lifecycle_state, pos_x, pos_y, pos_z, rotation_yaw, health_current, health_max, state_json, row_version)")
        script.add(
            f"VALUES ({sub_world_instance_id(instance_key)}, {pg_literal(entity_key)}, 'creature', {sub_entity_template_id(content_revision_key, 'creature', template_key)}, {pg_literal(lifecycle)}, "
            f"{pg_literal(creature.get('pos_x'))}, {pg_literal(creature.get('pos_y'))}, {pg_literal(creature.get('pos_z'))}, {pg_literal(creature.get('rotation'))}, "
            f"{pg_literal(int_or_none(creature.get('health_current')))}, {pg_literal(int_or_none(creature.get('health_max')))}, {pg_jsonb(creature)}, 1)"
        )
        script.add("ON CONFLICT(world_instance_id, entity_key) DO UPDATE SET lifecycle_state=EXCLUDED.lifecycle_state, pos_x=EXCLUDED.pos_x, pos_y=EXCLUDED.pos_y, pos_z=EXCLUDED.pos_z, rotation_yaw=EXCLUDED.rotation_yaw, health_current=EXCLUDED.health_current, health_max=EXCLUDED.health_max, state_json=EXCLUDED.state_json, row_version=world_entity_state.row_version+1;")

    for item in rows(con, "mmo_world_items_current"):
        wname = clean_key(item.get("world_name"), hero_world_name)
        wkey = next((w["world_key"] for w in worlds if w["world_name"] == wname), world_key(wname))
        instance_key = f"{args.realm_key}:{wkey}:1"
        entity_key = clean_key(item.get("item_spawn_key"), f"world-item:{item.get('persistent_id')}")
        lifecycle = "active" if bool_from_int(item.get("exists_in_world")) else "removed"
        symbol = int_or_none(item.get("item_template_symbol"))
        item_template_key = f"item-symbol:{symbol}" if symbol is not None else "item-hash:" + hashlib.sha256(stable_json(item).encode("utf-8")).hexdigest()[:16]
        imported_item_key = f"import:{content_revision_key}:world-item:{entity_key}"
        amount = max(1, number_or_zero(item.get("amount")))
        script.add("INSERT INTO world_entity_state(world_instance_id, entity_key, entity_kind, entity_template_id, lifecycle_state, pos_x, pos_y, pos_z, state_json, row_version)")
        script.add(
            f"VALUES ({sub_world_instance_id(instance_key)}, {pg_literal(entity_key)}, 'item', {sub_entity_template_id(content_revision_key, 'item', f'item-symbol:{symbol}')}, {pg_literal(lifecycle)}, "
            f"{pg_literal(item.get('pos_x'))}, {pg_literal(item.get('pos_y'))}, {pg_literal(item.get('pos_z'))}, {pg_jsonb(item)}, 1)"
        )
        script.add("ON CONFLICT(world_instance_id, entity_key) DO UPDATE SET lifecycle_state=EXCLUDED.lifecycle_state, pos_x=EXCLUDED.pos_x, pos_y=EXCLUDED.pos_y, pos_z=EXCLUDED.pos_z, state_json=EXCLUDED.state_json, row_version=world_entity_state.row_version+1;")
        script.add("INSERT INTO item_instances(realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, lifecycle_state, raw_payload)")
        script.add(
            f"VALUES ({sub_realm_id(args.realm_key)}, {sub_item_template_id(content_revision_key, item_template_key)}, {pg_literal(imported_item_key)}, 'world_entity', NULL, {pg_literal(amount)}, "
            f"{pg_literal('active' if lifecycle == 'active' else 'archived')}, {pg_jsonb(item)})"
        )
        script.add("ON CONFLICT(item_instance_key) DO UPDATE SET item_template_id=EXCLUDED.item_template_id, owner_type=EXCLUDED.owner_type, quantity=EXCLUDED.quantity, lifecycle_state=EXCLUDED.lifecycle_state, raw_payload=EXCLUDED.raw_payload;")

    for inter in rows(con, "mmo_world_interactives_current"):
        wname = clean_key(inter.get("world_name"), hero_world_name)
        wkey = next((w["world_key"] for w in worlds if w["world_name"] == wname), world_key(wname))
        instance_key = f"{args.realm_key}:{wkey}:1"
        entity_key = clean_key(inter.get("interactive_key"), f"interactive:{inter.get('vob_id')}")
        template_source = clean_key(inter.get("scheme"), "") or clean_key(inter.get("tag"), "") or f"interactive-vob:{inter.get('vob_id')}"
        template_key = f"interactive:{template_source}"
        lifecycle = "active"
        if bool_from_int(inter.get("container")) and bool_from_int(inter.get("cracked")):
            lifecycle = "active"
        script.add("INSERT INTO world_entity_state(world_instance_id, entity_key, entity_kind, entity_template_id, lifecycle_state, pos_x, pos_y, pos_z, state_json, row_version)")
        script.add(
            f"VALUES ({sub_world_instance_id(instance_key)}, {pg_literal(entity_key)}, 'interactive', {sub_entity_template_id(content_revision_key, 'interactive', template_key)}, {pg_literal(lifecycle)}, "
            f"{pg_literal(inter.get('pos_x'))}, {pg_literal(inter.get('pos_y'))}, {pg_literal(inter.get('pos_z'))}, {pg_jsonb(inter)}, 1)"
        )
        script.add("ON CONFLICT(world_instance_id, entity_key) DO UPDATE SET lifecycle_state=EXCLUDED.lifecycle_state, pos_x=EXCLUDED.pos_x, pos_y=EXCLUDED.pos_y, pos_z=EXCLUDED.pos_z, state_json=EXCLUDED.state_json, row_version=world_entity_state.row_version+1;")

    for idx, inv in enumerate(rows(con, "mmo_world_container_inventory_current")):
        wname = clean_key(inv.get("world_name"), hero_world_name)
        wkey = next((w["world_key"] for w in worlds if w["world_name"] == wname), world_key(wname))
        instance_key = f"{args.realm_key}:{wkey}:1"
        owner_key = clean_key(inv.get("owner_key"), "unknown-container")
        symbol = int_or_none(inv.get("item_template_symbol"))
        item_template_key = f"item-symbol:{symbol}" if symbol is not None else "item-hash:" + hashlib.sha256(stable_json(inv).encode("utf-8")).hexdigest()[:16]
        imported_key = f"import:{content_revision_key}:container:{owner_key}:{clean_key(inv.get('item_instance_key'), str(idx))}"
        amount = max(1, number_or_zero(inv.get("amount")))
        script.add("INSERT INTO item_instances(realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, raw_payload)")
        script.add(
            f"VALUES ({sub_realm_id(args.realm_key)}, {sub_item_template_id(content_revision_key, item_template_key)}, {pg_literal(imported_key)}, 'container', NULL, {pg_literal(amount)}, {pg_jsonb(inv)})"
        )
        script.add("ON CONFLICT(item_instance_key) DO UPDATE SET item_template_id=EXCLUDED.item_template_id, owner_type=EXCLUDED.owner_type, quantity=EXCLUDED.quantity, raw_payload=EXCLUDED.raw_payload;")
        script.add("INSERT INTO world_inventory(world_instance_id, owner_entity_key, item_instance_id, amount, source_amount, source_iterator_count)")
        script.add(
            f"VALUES ({sub_world_instance_id(instance_key)}, {pg_literal(owner_key)}, {sub_item_instance_id(imported_key)}, {pg_literal(amount)}, {pg_literal(int_or_none(inv.get('amount')))}, {pg_literal(int_or_none(inv.get('iterator_count')))})"
        )
        script.add("ON CONFLICT(world_instance_id, owner_entity_key, item_instance_id) DO UPDATE SET amount=EXCLUDED.amount, source_amount=EXCLUDED.source_amount, source_iterator_count=EXCLUDED.source_iterator_count;")

    for glob in rows(con, "mmo_script_globals_current"):
        for w in worlds:
            instance_key = f"{args.realm_key}:{clean_key(w['world_key'], world_key(w['world_name']))}:1"
            script.add("INSERT INTO world_script_state(world_instance_id, script_key, scope_key, symbol_index, value_type, value_index, value_text)")
            script.add(
                f"VALUES ({sub_world_instance_id(instance_key)}, {pg_literal(glob.get('global_key'))}, {pg_literal(glob.get('category') or 'world')}, {pg_literal(int_or_none(glob.get('symbol_index')))}, {pg_literal(glob.get('value_type') or 'unknown')}, 0, {pg_literal(glob.get('value_text'))})"
            )
            script.add("ON CONFLICT(world_instance_id, scope_key, script_key, value_index) DO UPDATE SET symbol_index=EXCLUDED.symbol_index, value_type=EXCLUDED.value_type, value_text=EXCLUDED.value_text;")

    # Record one import event per world instance. This is import provenance, not inferred gameplay.
    for w in worlds:
        wkey = clean_key(w["world_key"], world_key(w["world_name"]))
        instance_key = f"{args.realm_key}:{wkey}:1"
        payload = {
            "import_run_id": import_run_id,
            "source_fingerprint": meta.source_fingerprint,
            "source_schema_version": meta.schema_version,
            "content_revision_key": content_revision_key,
        }
        script.add("SELECT mmo_append_world_event(")
        script.add(f"  {sub_realm_id(args.realm_key)}, {sub_world_instance_id(instance_key)}, {sub_character_id(character_key)},")
        script.add(f"  'bootstrap_import_completed', 'system', 0, NULL, {pg_literal(content_revision_key)}, {pg_jsonb(payload)},")
        script.add(f"  {pg_literal('bootstrap-import:' + import_run_id + ':' + instance_key)}, 'import', NULL, NULL")
        script.add(");")

    final_counters = dict(meta.counts)
    final_counters.update({
        "imported_world_templates": len(worlds),
        "imported_item_templates": len(item_templates),
        "imported_entity_templates": len(entity_templates),
        "imported_character_key": character_key,
    })
    script.add(f"SELECT mmo_mark_import_finished({pg_literal(import_run_id)}::uuid, 'finished', {pg_jsonb(final_counters)}, {pg_jsonb({'content_revision_key': content_revision_key, 'realm_key': args.realm_key})});")
    script.add("COMMIT;")
    return script.text()


def run_psql(dsn: str, sql: str) -> None:
    exe = shutil.which("psql")
    if not exe:
        fail("psql command not found; install PostgreSQL client tools or use --dry-run-sql")
    proc = subprocess.run(
        [exe, dsn, "-v", "ON_ERROR_STOP=1", "-q"],
        input=sql,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        fail(f"psql exited with status {proc.returncode}")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Import OpenGothic runtime SQLite mmo_* projection into the PostgreSQL production MMO schema.")
    parser.add_argument("--sqlite", type=Path, required=True, help="Path to runtime/g2notr.sqlite")
    parser.add_argument("--dsn", default=os.environ.get("DATABASE_URL", ""), help="PostgreSQL DSN. Defaults to DATABASE_URL.")
    parser.add_argument("--dry-run-sql", type=Path, help="Write generated SQL to this file instead of executing psql.")
    parser.add_argument("--game-code", default="g2notr", choices=("g1", "g2", "g2notr"))
    parser.add_argument("--game-display-name", default="Gothic II: Night of the Raven")
    parser.add_argument("--content-revision-key", default="", help="Override content revision key. Default derives from SQLite fingerprint.")
    parser.add_argument("--activate-content", action="store_true", help="Mark imported content revision active for its game target, deactivating previous active revision.")
    parser.add_argument("--realm-key", default="local-dev")
    parser.add_argument("--realm-display-name", default="Local Dev Realm")
    parser.add_argument("--realm-status", default="maintenance", choices=("offline", "maintenance", "online", "locked", "retired"))
    parser.add_argument("--max-players", type=int, default=1000)
    parser.add_argument("--account-name", default="local-import")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--source-description", default="Imported from OpenGothic runtime SQLite production projection")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    if not args.dry_run_sql and not args.dsn:
        fail("provide --dsn, DATABASE_URL, or --dry-run-sql")
    con = open_sqlite(args.sqlite)
    try:
        sql = build_import_sql(con, args)
    finally:
        con.close()

    if args.dry_run_sql:
        args.dry_run_sql.parent.mkdir(parents=True, exist_ok=True)
        args.dry_run_sql.write_text(sql, encoding="utf-8")
        print(f"wrote SQL: {args.dry_run_sql}")
        return 0

    run_psql(args.dsn, sql)
    print("runtime SQLite import finished")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
