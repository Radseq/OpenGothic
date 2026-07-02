#!/usr/bin/env python3
"""Resolve and dispatch MMO semantic actions from mmo_server_action_outbox.

This is a dev server worker, not game-thread code. It claims pending actions,
resolves OpenGothic engine keys to MySQL projection rows, calls the existing
mmo_* stored procedures, and marks outbox rows applied/failed.

Supported real slices:
- client_bootstrap_request -> Step56 server read-model check + bootstrap_ack response manifest
- pickup_world_item -> mmo_pickup_world_item(...)
- remove_world_item -> mmo_remove_world_item(...)
- equip_character_item -> mmo_equip_character_item(...)
- unequip_character_item -> mmo_unequip_character_item(...)
- set_script_int -> mmo_set_character_script_int(...)
- adjust_progression -> mmo_adjust_character_progression(...)
- apply_experience_reward -> mmo_apply_character_experience_reward(...)
- update_quest -> mmo_update_character_quest(...)
- set_known_dialog -> mmo_set_character_known_dialog(...)
- consume_mana -> mmo_consume_character_mana(...)
- consume_item -> mmo_consume_character_item(...)
- apply_character_damage -> mmo_apply_character_damage(...)
- apply_world_entity_damage -> mmo_apply_world_entity_damage(...)
- mark_npc_dead -> mmo_mark_npc_dead(...)
- trade_sell_to_npc -> mmo_trade_sell_to_npc(...)
- trade_buy_from_npc -> mmo_trade_buy_from_npc(...)
- character_checkpoint -> mmo_checkpoint_character_state(...) and optional movement_checkpoint_ack response JSONL
- movement_proposal -> optional Step58 server authority gate ACK/NACK; accepted proposals can persist a bounded checkpoint behind --enable-movement-authority-gate
- use_interactive -> mmo_record_interactive_use(...)
- drop_character_item -> mmo_drop_character_item(...)
- loot_npc_inventory -> mmo_loot_npc_inventory(...)
- update_interactive_state -> mmo_update_interactive_state(...)
- without --enable-movement-authority-gate, raw movement_proposal remains applied/no-op evidence instead of a DB mutation
- trigger/mover/weapon/time/resource/training/teleport/respawn/NPC-reaction domains dispatch to Step51 canonical procedures when installed

The resolver is intentionally conservative. If a client envelope cannot be
resolved uniquely, the action is failed as non-retryable unless --retry-unresolved
is given. For pickup_world_item, the worker allocates the first free
character_inventory.bag_index from the server projection instead of defaulting
to a client slot. Do not fake UUIDs or mark parity green from unresolved rows.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

WORKER_MODE = "resolved_dev_mysql_cli"


BOOTSTRAP_READ_MODEL_TABLES = [
    "mmo_server_read_model_meta",
    "mmo_server_character_read_model",
    "mmo_server_character_inventory_read_model",
    "mmo_server_character_quest_read_model",
    "mmo_server_known_dialog_read_model",
    "mmo_server_world_entity_read_model",
    "mmo_server_world_inventory_read_model",
    "mmo_server_interactive_read_model",
    "mmo_server_script_int_read_model",
    "mmo_server_world_clock_read_model",
    "mmo_server_waypoint_read_model",
    "mmo_server_waypoint_edge_read_model",
]

BOOTSTRAP_CORE_READ_MODEL_TABLES = [
    "mmo_server_character_read_model",
    "mmo_server_world_entity_read_model",
    "mmo_server_script_int_read_model",
]


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


@dataclass
class Action:
    action_uuid: str
    kind: str
    session_uuid: str
    character_uuid: str
    world_uuid: str
    target_key: str | None
    idempotency_key: str
    payload: dict[str, Any]


class ResolveError(RuntimeError):
    pass


@dataclass(frozen=True)
class MovementAuthorityConfig:
    enabled: bool = False
    max_step_distance: float = 2500.0
    max_horizontal_speed: float = 2500.0
    max_vertical_speed: float = 3500.0
    max_vertical_delta: float = 1600.0
    max_fall_speed: float = 12000.0
    max_fall_delta: float = 6000.0
    min_delta_ms: int = 1
    max_delta_ms: int = 5000
    max_coord_abs: float = 10000000.0
    default_vertical_axis: str = "y"


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    db = (p.path or "/").lstrip("/")
    if not db:
        raise ValueError("database is missing in mysql URL")
    return Target(
        host=p.hostname or "localhost",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=db,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-h", target.host,
        "-P", str(target.port),
        "-u", target.user,
    ]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def first_row(target: Target, sql: str) -> list[str]:
    out = run_mysql(target, sql)
    if not out:
        return []
    return out.splitlines()[-1].split("\t")


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def json_sql(value: Any) -> str:
    return f"CAST({sql_literal(json.dumps(value, ensure_ascii=False, separators=(',', ':')))} AS JSON)"


def bin_uuid(uuid: str | None) -> str:
    if not uuid or uuid == "NULL":
        return "NULL"
    return f"UUID_TO_BIN({sql_literal(uuid)},1)"


def qident(name: str) -> str:
    return "`" + name.replace("`", "``") + "`"


def tsv_rows(target: Target, sql: str) -> list[list[str]]:
    out = run_mysql(target, sql)
    rows: list[list[str]] = []
    for line in out.splitlines():
        if line.strip():
            rows.append(line.split("\t"))
    return rows


def scalar_mysql_int(target: Target, sql: str) -> int:
    out = run_mysql(target, sql).strip()
    if not out:
        return 0
    return int(out.splitlines()[-1].strip() or "0")


def table_exists(target: Target, table: str) -> bool:
    return scalar_mysql_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE()
           AND table_name={sql_literal(table)}
           AND table_type='BASE TABLE';
        """,
    ) == 1


def count_table_rows(target: Target, table: str) -> int:
    if not table_exists(target, table):
        return -1
    return scalar_mysql_int(target, f"SELECT COUNT(*) FROM {qident(table)};")


def rows_as_dicts(target: Target, table: str, columns: list[str], limit: int, where_sql: str = "") -> list[dict[str, Any]]:
    if not table_exists(target, table):
        return []
    select = ", ".join(qident(c) for c in columns)
    sql = f"SELECT {select} FROM {qident(table)}"
    if where_sql:
        sql += " " + where_sql
    sql += f" LIMIT {max(0, int(limit))};"
    out = run_mysql(target, sql)
    rows: list[dict[str, Any]] = []
    for line in out.splitlines():
        parts = line.split("\t") if line else []
        rows.append({columns[i]: (parts[i] if i < len(parts) and parts[i] != "NULL" else None) for i in range(len(columns))})
    return rows


def inspect_bootstrap_read_model(target: Target, character_key: str, sample_limit: int) -> dict[str, Any]:
    sample_limit = max(1, int(sample_limit))
    missing_tables = [table for table in BOOTSTRAP_READ_MODEL_TABLES if not table_exists(target, table)]
    counts = {table: count_table_rows(target, table) for table in BOOTSTRAP_READ_MODEL_TABLES}

    json_columns = [
        {"table": row[0], "column": row[1], "data_type": row[2], "column_type": row[3]}
        for row in tsv_rows(
            target,
            """
            SELECT table_name, column_name, data_type, column_type
              FROM information_schema.columns
             WHERE table_schema=DATABASE()
               AND table_name LIKE 'mmo_server\\_%\\_read_model'
               AND data_type='json'
             ORDER BY table_name, ordinal_position;
            """,
        )
    ]
    suspect_payload_columns = [
        {"table": row[0], "column": row[1], "data_type": row[2], "column_type": row[3]}
        for row in tsv_rows(
            target,
            """
            SELECT table_name, column_name, data_type, column_type
              FROM information_schema.columns
             WHERE table_schema=DATABASE()
               AND table_name LIKE 'mmo_server\\_%\\_read_model'
               AND (
                    LOWER(column_name) LIKE '%json%'
                 OR LOWER(column_name) LIKE '%payload%'
                 OR LOWER(column_name) LIKE '%metadata%'
                 OR LOWER(column_name) LIKE '%raw%'
               )
             ORDER BY table_name, ordinal_position;
            """,
        )
    ]
    views = [
        row[0]
        for row in tsv_rows(
            target,
            """
            SELECT table_name
              FROM information_schema.tables
             WHERE table_schema=DATABASE()
               AND table_type='VIEW'
               AND table_name LIKE 'mmo_server\\_%'
             ORDER BY table_name;
            """,
        )
    ]

    character_rows = rows_as_dicts(
        target,
        "mmo_server_character_read_model",
        ["realm_key", "character_key", "display_name", "world_name", "pos_x", "pos_y", "pos_z", "level_value", "experience_value", "learning_points"],
        sample_limit,
        f"WHERE character_key={sql_literal(character_key)} ORDER BY materialized_at DESC",
    )
    if not character_rows:
        character_rows = rows_as_dicts(
            target,
            "mmo_server_character_read_model",
            ["realm_key", "character_key", "display_name", "world_name", "pos_x", "pos_y", "pos_z", "level_value", "experience_value", "learning_points"],
            sample_limit,
            "ORDER BY materialized_at DESC",
        )

    effective_character_key = character_key
    if character_rows and character_rows[0].get("character_key"):
        effective_character_key = str(character_rows[0]["character_key"])

    inventory_rows = rows_as_dicts(
        target,
        "mmo_server_character_inventory_read_model",
        ["character_key", "item_instance_key", "item_template_key", "display_name", "amount", "equipped", "slot_key"],
        sample_limit,
        f"WHERE character_key={sql_literal(effective_character_key)} ORDER BY equipped DESC, display_name, item_instance_key",
    )
    quest_rows = rows_as_dicts(
        target,
        "mmo_server_character_quest_read_model",
        ["character_key", "quest_key", "quest_name", "status_key", "entry_count"],
        sample_limit,
        f"WHERE character_key={sql_literal(effective_character_key)} ORDER BY quest_key",
    )
    world_entity_rows = rows_as_dicts(
        target,
        "mmo_server_world_entity_read_model",
        ["world_name", "entity_key", "entity_kind", "display_name", "active", "dead", "pos_x", "pos_y", "pos_z", "current_waypoint_name"],
        sample_limit,
        "ORDER BY world_name, entity_kind, display_name, entity_key",
    )

    worlds: list[str] = []
    if table_exists(target, "mmo_server_world_entity_read_model"):
        worlds = [row[0] for row in tsv_rows(target, "SELECT DISTINCT world_name FROM mmo_server_world_entity_read_model ORDER BY world_name LIMIT 200;") if row and row[0]]

    entity_kind_counts: list[dict[str, Any]] = []
    if table_exists(target, "mmo_server_world_entity_read_model"):
        for row in tsv_rows(
            target,
            """
            SELECT world_name, entity_kind, active, dead, COUNT(*)
              FROM mmo_server_world_entity_read_model
             GROUP BY world_name, entity_kind, active, dead
             ORDER BY world_name, entity_kind, active DESC, dead;
            """,
        ):
            if len(row) >= 5:
                entity_kind_counts.append({"world_name": row[0], "entity_kind": row[1], "active": row[2], "dead": row[3], "count": int(row[4])})

    populated_core = {table: counts.get(table, -1) > 0 for table in BOOTSTRAP_CORE_READ_MODEL_TABLES}
    all_tables = not missing_tables
    no_json = not json_columns and not suspect_payload_columns
    no_views = not views
    core_ready = all(populated_core.values())
    character_present = bool(character_rows) and any(row.get("character_key") == effective_character_key for row in character_rows)
    ready = bool(all_tables and no_json and no_views and core_ready and character_present)

    reasons: list[str] = []
    if missing_tables:
        reasons.append("read-model tables missing: " + ", ".join(missing_tables))
    if json_columns or suspect_payload_columns:
        reasons.append("read-model contains JSON/raw/payload-shaped columns")
    if views:
        reasons.append("mmo_server_* views exist; server bootstrap must use physical read-model tables")
    if not core_ready:
        empty = [k for k, ok in populated_core.items() if not ok]
        reasons.append("core read-model tables are empty: " + ", ".join(empty))
    if not character_present:
        reasons.append(f"character {effective_character_key!r} is not present in mmo_server_character_read_model")

    return {
        "manifest_kind": "server_bootstrap_manifest_v1",
        "database": target.database,
        "requested_character_key": character_key,
        "effective_character_key": effective_character_key,
        "counts": counts,
        "worlds": worlds,
        "entity_kind_counts": entity_kind_counts,
        "samples": {
            "characters": character_rows,
            "inventory": inventory_rows,
            "quests": quest_rows,
            "world_entities": world_entity_rows,
        },
        "verdict": {
            "read_model_exists": all_tables,
            "read_model_has_no_json": no_json,
            "read_model_uses_no_views": no_views,
            "core_read_model_populated": core_ready,
            "character_present": character_present,
            "ready_for_bootstrap_ack": ready,
            "still_final_production_db": False,
            "reason_if_not_ready": reasons,
        },
    }


def make_bootstrap_ack(target: Target, action: Action, sample_limit: int) -> dict[str, Any]:
    character_key = str(payload_first(action.payload, "character_key") or "PC_HERO")
    manifest = inspect_bootstrap_read_model(target, character_key, sample_limit)
    ready = bool(manifest["verdict"].get("ready_for_bootstrap_ack"))
    world = payload_first(action.payload, "world")
    if not world and manifest["samples"].get("characters"):
        world = manifest["samples"]["characters"][0].get("world_name")
    return {
        "version": 1,
        "response_kind": "bootstrap_ack",
        "accepted": ready,
        "bootstrap_status": "ready" if ready else "read_model_not_ready",
        "request_action_uuid": action.action_uuid,
        "request_kind": action.kind,
        "request_idempotency_key": action.idempotency_key,
        "session_uuid": action.session_uuid,
        "character_uuid": action.character_uuid,
        "world_uuid": action.world_uuid,
        "character_key": manifest.get("effective_character_key") or character_key,
        "requested_character_key": character_key,
        "world": world,
        "client": {
            "target_key": action.target_key,
            "server_tick": scalar_int(payload_first(action.payload, "server_tick", "client_tick"), 0),
            "server_endpoint": payload_first(action.payload, "server_endpoint"),
            "reason": payload_first(action.payload, "reason") or "client_bootstrap_request",
        },
        "read_model": manifest,
        "important": {
            "server_checked_mysql_read_model": True,
            "old_single_player_unchanged": True,
            "no_gameplay_world_state_was_mutated_by_bootstrap": True,
            "this_is_not_full_server_authority_yet": True,
        },
    }


def make_checkpoint_ack(action: Action, event_uuid: str | None, result: dict[str, Any], status: str) -> dict[str, Any]:
    resolved = result.get("resolved") if isinstance(result.get("resolved"), dict) else {}
    accepted = status == "applied" and bool(event_uuid)
    return {
        "version": 1,
        "response_kind": "movement_checkpoint_ack",
        "accepted": accepted,
        "checkpoint_status": "persisted" if accepted else "not_persisted",
        "request_action_uuid": action.action_uuid,
        "request_kind": action.kind,
        "request_idempotency_key": action.idempotency_key,
        "session_uuid": action.session_uuid,
        "character_uuid": action.character_uuid,
        "world_uuid": action.world_uuid,
        "event_uuid": event_uuid,
        "outbox_status": status,
        "checkpoint": {
            "pos_x": resolved.get("pos_x"),
            "pos_y": resolved.get("pos_y"),
            "pos_z": resolved.get("pos_z"),
            "rotation_yaw": resolved.get("rotation_yaw"),
            "current_waypoint_key": resolved.get("current_waypoint_key"),
            "level": resolved.get("level"),
            "experience": resolved.get("experience"),
            "experience_next": resolved.get("experience_next"),
            "learning_points": resolved.get("learning_points"),
            "reason": resolved.get("reason"),
        },
        "authority": {
            "server_persisted_checkpoint": accepted,
            "raw_movement_proposal_was_not_persisted": True,
            "client_does_not_consume_this_ack_yet": True,
            "old_single_player_unchanged": True,
        },
        "resolved": resolved,
    }



def _safe_float_payload(action: Action, *keys: str) -> tuple[float | None, str | None]:
    value = payload_first(action.payload, *keys)
    label = "/".join(keys)
    if value in (None, ""):
        return None, f"missing_{label}"
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None, f"invalid_{label}"
    if not math.isfinite(parsed):
        return None, f"non_finite_{label}"
    return parsed, None


def _movement_bool(action: Action, *keys: str) -> bool:
    return bool_payload(payload_first(action.payload, *keys), False)


def _yaw_delta_deg(a: float, b: float) -> float:
    delta = abs((a - b) % 360.0)
    return min(delta, 360.0 - delta)


def evaluate_movement_proposal(action: Action, cfg: MovementAuthorityConfig) -> dict[str, Any]:
    coords: dict[str, float] = {}
    errors: list[str] = []
    for key in ("from_pos_x", "from_pos_y", "from_pos_z", "to_pos_x", "to_pos_y", "to_pos_z"):
        value, error = _safe_float_payload(action, key)
        if error:
            errors.append(error)
        else:
            coords[key] = float(value)

    delta_ms = scalar_int(payload_first(action.payload, "delta_ms", "elapsed_ms"), None)
    if delta_ms is None:
        from_tick = scalar_int(payload_first(action.payload, "from_tick"), None)
        to_tick = scalar_int(payload_first(action.payload, "to_tick", "client_tick", "server_tick"), None)
        if from_tick is not None and to_tick is not None and to_tick >= from_tick:
            delta_ms = to_tick - from_tick
    if delta_ms is None:
        errors.append("missing_delta_ms")
        delta_ms = 0

    vertical_axis = str(payload_first(action.payload, "vertical_axis") or cfg.default_vertical_axis or "y").strip().lower()
    if vertical_axis not in {"x", "y", "z"}:
        errors.append(f"invalid_vertical_axis:{vertical_axis}")
        vertical_axis = "y"

    accepted = not errors
    if delta_ms < cfg.min_delta_ms:
        errors.append(f"delta_ms_below_min:{delta_ms}<{cfg.min_delta_ms}")
        accepted = False
    if delta_ms > cfg.max_delta_ms:
        errors.append(f"delta_ms_above_max:{delta_ms}>{cfg.max_delta_ms}")
        accepted = False

    seconds = max(float(delta_ms) / 1000.0, 0.001)
    deltas = {
        "x": coords.get("to_pos_x", 0.0) - coords.get("from_pos_x", 0.0),
        "y": coords.get("to_pos_y", 0.0) - coords.get("from_pos_y", 0.0),
        "z": coords.get("to_pos_z", 0.0) - coords.get("from_pos_z", 0.0),
    }
    horizontal_axes = [axis for axis in ("x", "y", "z") if axis != vertical_axis]
    horizontal_distance = math.sqrt(sum(deltas[axis] * deltas[axis] for axis in horizontal_axes)) if not errors else 0.0
    vertical_delta = abs(deltas[vertical_axis]) if not errors else 0.0
    total_distance = math.sqrt(sum(value * value for value in deltas.values())) if not errors else 0.0
    horizontal_speed = horizontal_distance / seconds
    vertical_speed = vertical_delta / seconds

    from_yaw, _ = _safe_float_payload(action, "from_rotation_yaw", "from_yaw", "rotation_yaw")
    to_yaw, _ = _safe_float_payload(action, "to_rotation_yaw", "to_yaw", "rotation_yaw")
    yaw_delta = _yaw_delta_deg(float(to_yaw or 0.0), float(from_yaw or 0.0))

    falling_or_air = any(
        _movement_bool(action, key)
        for key in (
            "from_is_in_air", "to_is_in_air", "from_is_falling", "to_is_falling",
            "from_is_falling_deep", "to_is_falling_deep", "from_is_jump", "to_is_jump", "from_is_jump_up", "to_is_jump_up",
        )
    )
    swimming_or_water = any(
        _movement_bool(action, key)
        for key in ("from_is_swim", "to_is_swim", "from_is_dive", "to_is_dive", "from_is_in_water", "to_is_in_water")
    )

    if coords:
        too_large = [name for name, value in coords.items() if abs(value) > cfg.max_coord_abs]
        if too_large:
            errors.append("coordinate_out_of_bounds:" + ",".join(sorted(too_large)))
            accepted = False
    if horizontal_distance > cfg.max_step_distance:
        errors.append(f"horizontal_distance_above_max:{horizontal_distance:.3f}>{cfg.max_step_distance:.3f}")
        accepted = False
    if horizontal_speed > cfg.max_horizontal_speed:
        errors.append(f"horizontal_speed_above_max:{horizontal_speed:.3f}>{cfg.max_horizontal_speed:.3f}")
        accepted = False

    if falling_or_air:
        if vertical_delta > cfg.max_fall_delta:
            errors.append(f"fall_vertical_delta_above_max:{vertical_delta:.3f}>{cfg.max_fall_delta:.3f}")
            accepted = False
        if vertical_speed > cfg.max_fall_speed:
            errors.append(f"fall_vertical_speed_above_max:{vertical_speed:.3f}>{cfg.max_fall_speed:.3f}")
            accepted = False
    else:
        if vertical_delta > cfg.max_vertical_delta:
            errors.append(f"vertical_delta_above_max:{vertical_delta:.3f}>{cfg.max_vertical_delta:.3f}")
            accepted = False
        if vertical_speed > cfg.max_vertical_speed:
            errors.append(f"vertical_speed_above_max:{vertical_speed:.3f}>{cfg.max_vertical_speed:.3f}")
            accepted = False

    return {
        "validator": "movement_proposal_authority_gate_v1",
        "accepted": bool(accepted and not errors),
        "rejection_reasons": errors,
        "vertical_axis": vertical_axis,
        "delta_ms": int(delta_ms),
        "seconds": seconds,
        "from": {"pos_x": coords.get("from_pos_x"), "pos_y": coords.get("from_pos_y"), "pos_z": coords.get("from_pos_z"), "rotation_yaw": from_yaw},
        "to": {"pos_x": coords.get("to_pos_x"), "pos_y": coords.get("to_pos_y"), "pos_z": coords.get("to_pos_z"), "rotation_yaw": to_yaw},
        "metrics": {
            "horizontal_distance": horizontal_distance,
            "vertical_delta": vertical_delta,
            "total_distance": total_distance,
            "horizontal_speed": horizontal_speed,
            "vertical_speed": vertical_speed,
            "yaw_delta_deg": yaw_delta,
            "falling_or_air": falling_or_air,
            "swimming_or_water": swimming_or_water,
        },
        "limits": {
            "max_step_distance": cfg.max_step_distance,
            "max_horizontal_speed": cfg.max_horizontal_speed,
            "max_vertical_speed": cfg.max_vertical_speed,
            "max_vertical_delta": cfg.max_vertical_delta,
            "max_fall_speed": cfg.max_fall_speed,
            "max_fall_delta": cfg.max_fall_delta,
            "min_delta_ms": cfg.min_delta_ms,
            "max_delta_ms": cfg.max_delta_ms,
            "max_coord_abs": cfg.max_coord_abs,
        },
    }


def dispatch_accepted_movement_checkpoint(target: Target, action: Action, tick: int, idem: str, validation: dict[str, Any]) -> tuple[str | None, dict[str, Any]]:
    to_pos = validation.get("to") if isinstance(validation.get("to"), dict) else {}
    pos_x = float(to_pos.get("pos_x") or 0.0)
    pos_y = float(to_pos.get("pos_y") or 0.0)
    pos_z = float(to_pos.get("pos_z") or 0.0)
    rotation_yaw = float(to_pos.get("rotation_yaw") if to_pos.get("rotation_yaw") not in (None, "") else payload_first(action.payload, "to_rotation_yaw", "rotation_yaw") or 0.0)
    waypoint = str(checkpoint_payload_value(action, "current_waypoint_key", "") or "")
    level = int_payload(action, "level", default=0)
    experience = int_payload(action, "experience", default=0)
    experience_next = int_payload(action, "experience_next", default=0)
    learning_points = int_payload(action, "learning_points", default=0)
    health_current = int_payload(action, "health_current", default=0)
    health_max = int_payload(action, "health_max", default=max(health_current, 0))
    mana_current = int_payload(action, "mana_current", default=0)
    mana_max = int_payload(action, "mana_max", default=max(mana_current, 0))
    strength = int_payload(action, "strength", default=0)
    dexterity = int_payload(action, "dexterity", default=0)
    guild = checkpoint_payload_value(action, "guild")
    true_guild = checkpoint_payload_value(action, "true_guild")
    permanent_attitude = checkpoint_payload_value(action, "permanent_attitude")
    temporary_attitude = checkpoint_payload_value(action, "temporary_attitude")
    resolved = {
        "resolver": "movement_proposal_authority_gate_v1",
        "character_uuid": action.character_uuid,
        "actor_key": payload_first(action.payload, "actor_key") or action.target_key,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "pos_z": pos_z,
        "rotation_yaw": rotation_yaw,
        "current_waypoint_key": waypoint,
        "level": level,
        "experience": experience,
        "experience_next": experience_next,
        "learning_points": learning_points,
        "reason": payload_first(action.payload, "reason") or "movement_proposal_authority_accept",
        "source_action_kind": "movement_proposal",
        "server_checkpoint_source": "accepted_movement_proposal",
        "validation": validation,
    }
    row = first_row(target, f"""
        SET @event_id=NULL;
        CALL mmo_checkpoint_character_state(
          UUID_TO_BIN({sql_literal(action.session_uuid)},1),
          {int(tick)},
          {pos_x},
          {pos_y},
          {pos_z},
          {rotation_yaw},
          {sql_literal(waypoint)},
          {int(level)},
          {int(experience)},
          {int(experience_next)},
          {int(learning_points)},
          {int(health_current)},
          {int(health_max)},
          {int(mana_current)},
          {int(mana_max)},
          {int(strength)},
          {int(dexterity)},
          {sql_literal(guild) if guild not in (None, '') else 'NULL'},
          {sql_literal(true_guild) if true_guild not in (None, '') else 'NULL'},
          {sql_literal(permanent_attitude) if permanent_attitude not in (None, '') else 'NULL'},
          {sql_literal(temporary_attitude) if temporary_attitude not in (None, '') else 'NULL'},
          {json_sql(metadata(action, resolved))},
          {sql_literal(idem)},
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id,1);
    """)
    return row[0], {"resolved": resolved}


def make_movement_authority_ack(action: Action, event_uuid: str | None, result: dict[str, Any], status: str) -> dict[str, Any]:
    resolved = result.get("resolved") if isinstance(result.get("resolved"), dict) else {}
    validation = resolved.get("validation") if isinstance(resolved.get("validation"), dict) else result.get("validation", {})
    accepted = bool(result.get("accepted") is True or validation.get("accepted") is True)
    return {
        "version": 1,
        "response_kind": "movement_authority_ack",
        "accepted": accepted,
        "movement_status": "accepted_checkpoint_persisted" if accepted and event_uuid else "rejected_no_mutation",
        "request_action_uuid": action.action_uuid,
        "request_kind": action.kind,
        "request_idempotency_key": action.idempotency_key,
        "session_uuid": action.session_uuid,
        "character_uuid": action.character_uuid,
        "world_uuid": action.world_uuid,
        "event_uuid": event_uuid,
        "outbox_status": status,
        "validation": validation,
        "authority": {
            "server_validated_movement_proposal": True,
            "accepted_proposal_persisted_as_checkpoint": bool(accepted and event_uuid),
            "rejected_proposal_mutated_no_gameplay_state": not accepted,
            "client_does_not_consume_this_ack_yet": True,
            "old_single_player_unchanged": True,
        },
        "resolved": resolved,
    }


def make_pickup_ack(action: Action, event_uuid: str | None, result: dict[str, Any], status: str) -> dict[str, Any]:
    resolved = result.get("resolved") if isinstance(result.get("resolved"), dict) else {}
    return {
        "version": 1,
        "response_kind": "pickup_ack",
        "accepted": bool(event_uuid),
        "pickup_status": "accepted_inventory_persisted" if event_uuid else "rejected_no_mutation",
        "request_action_uuid": action.action_uuid,
        "request_kind": action.kind,
        "request_idempotency_key": action.idempotency_key,
        "session_uuid": action.session_uuid,
        "character_uuid": action.character_uuid,
        "world_uuid": action.world_uuid,
        "event_uuid": event_uuid,
        "outbox_status": status,
        "item_instance_uuid": result.get("item_instance_uuid"),
        "amount_picked": result.get("amount_picked"),
        "bag_index": result.get("bag_index"),
        "world_item_entity_key": resolved.get("world_item_entity_key"),
        "authority": {
            "server_resolved_world_item": True,
            "world_item_removed_from_world_projection": bool(event_uuid),
            "item_added_to_character_inventory": bool(event_uuid),
            "client_does_not_consume_this_ack_yet": True,
            "old_single_player_unchanged": True,
        },
        "resolved": resolved,
    }


def make_equipment_ack(action: Action, event_uuid: str | None, result: dict[str, Any], status: str) -> dict[str, Any]:
    resolved = result.get("resolved") if isinstance(result.get("resolved"), dict) else {}
    is_equip = action.kind == "equip_character_item"
    return {
        "version": 1,
        "response_kind": "equipment_ack",
        "accepted": bool(event_uuid),
        "equipment_status": ("accepted_equipped" if is_equip else "accepted_unequipped") if event_uuid else "rejected_no_mutation",
        "request_action_uuid": action.action_uuid,
        "request_kind": action.kind,
        "request_idempotency_key": action.idempotency_key,
        "session_uuid": action.session_uuid,
        "character_uuid": action.character_uuid,
        "world_uuid": action.world_uuid,
        "event_uuid": event_uuid,
        "outbox_status": status,
        "item_instance_uuid": result.get("item_instance_uuid"),
        "equipment_slot": result.get("equipment_slot"),
        "authority": {
            "server_resolved_character_item": is_equip,
            "equipment_projection_mutated": bool(event_uuid),
            "inventory_item_remains_server_owned": bool(event_uuid),
            "client_does_not_consume_this_ack_yet": True,
            "old_single_player_unchanged": True,
        },
        "resolved": resolved,
    }


def append_jsonl(path: Path, obj: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as out:
        out.write(json.dumps(obj, ensure_ascii=False, separators=(",", ":"), sort_keys=True) + "\n")
        out.flush()


def start_worker_run(target: Target, worker_id: str, run_key: str, max_actions: int) -> str:
    row = first_row(target, f"""
        SET @run_id=NULL;
        CALL mmo_start_server_action_worker_run({sql_literal(worker_id)}, {sql_literal(run_key)}, {sql_literal(WORKER_MODE)}, JSON_OBJECT('max_actions',{int(max_actions)}), @run_id);
        SELECT BIN_TO_UUID(@run_id,1);
    """)
    if not row or row[0] in ("", "NULL"):
        raise RuntimeError("failed to start worker run")
    return row[0]


def finish_worker_run(target: Target, run_uuid: str, failed: bool) -> tuple[str, int]:
    row = first_row(target, f"""
        SET @status=NULL; SET @applied=NULL;
        CALL mmo_finish_server_action_worker_run(UUID_TO_BIN({sql_literal(run_uuid)},1), {sql_literal(failed)}, @status, @applied);
        SELECT @status, @applied;
    """)
    return row[0], int(row[1])


def _action_from_row(row: list[str]) -> Action | None:
    if not row or row[0] in ("", "NULL"):
        return None
    payload = json.loads(row[7] or "{}")
    return Action(
        action_uuid=row[0],
        kind=row[1],
        session_uuid=row[2],
        character_uuid=row[3],
        world_uuid=row[4],
        target_key=None if row[5] == "NULL" else row[5],
        idempotency_key=row[6],
        payload=payload if isinstance(payload, dict) else {},
    )


def claim(target: Target, worker_id: str) -> Action | None:
    row = first_row(target, f"""
        SET @action_id=NULL; SET @kind=NULL; SET @session_id=NULL; SET @char_id=NULL; SET @world_id=NULL; SET @target=NULL; SET @idem=NULL; SET @payload=NULL;
        CALL mmo_claim_next_server_action({sql_literal(worker_id)}, @action_id, @kind, @session_id, @char_id, @world_id, @target, @idem, @payload);
        SELECT BIN_TO_UUID(@action_id,1), @kind, BIN_TO_UUID(@session_id,1), BIN_TO_UUID(@char_id,1), BIN_TO_UUID(@world_id,1), @target, @idem, @payload;
    """)
    return _action_from_row(row)


def claim_matching_prefix(target: Target, worker_id: str, idempotency_prefix: str) -> Action | None:
    # Filtered claim for dev replay sessions. The stock stored procedure claims the
    # oldest pending action globally, which is dangerous while many local smoke
    # sessions leave pending rows behind. This keeps one test run isolated without
    # changing schema/procedures.
    like = idempotency_prefix + ":%"
    row = first_row(target, f"""
        START TRANSACTION;
        SET @action_id=NULL; SET @kind=NULL; SET @session_id=NULL; SET @char_id=NULL; SET @world_id=NULL; SET @target=NULL; SET @idem=NULL; SET @payload=NULL;
        SELECT action_id, action_kind, session_id, character_id, world_instance_id, target_key, idempotency_key, request_payload
          INTO @action_id, @kind, @session_id, @char_id, @world_id, @target, @idem, @payload
          FROM mmo_server_action_outbox
         WHERE status='pending'
           AND idempotency_key LIKE {sql_literal(like)}
         ORDER BY priority ASC, requested_at ASC, action_id ASC
         LIMIT 1
         FOR UPDATE SKIP LOCKED;
        UPDATE mmo_server_action_outbox
           SET status='claimed',
               locked_at=CURRENT_TIMESTAMP(6),
               result_payload=JSON_MERGE_PATCH(COALESCE(result_payload,JSON_OBJECT()), JSON_OBJECT('claimed_by',{sql_literal(worker_id)}, 'claim_filter','idempotency_prefix'))
         WHERE action_id=@action_id;
        SELECT BIN_TO_UUID(action_id,1), action_kind, BIN_TO_UUID(session_id,1), BIN_TO_UUID(character_id,1), BIN_TO_UUID(world_instance_id,1), target_key, idempotency_key, request_payload
          FROM mmo_server_action_outbox
         WHERE action_id=@action_id;
        COMMIT;
    """)
    return _action_from_row(row)


def reset_failed_for_prefix(target: Target, idempotency_prefix: str) -> int:
    like = idempotency_prefix + ":%"
    out = run_mysql(target, f"""
        UPDATE mmo_server_action_outbox
           SET status='pending',
               attempt_count=0,
               locked_at=NULL,
               failed_at=NULL,
               last_error_code=NULL,
               last_error_message=NULL,
               result_payload=JSON_OBJECT('reset_by','run_mmo_resolved_action_worker','reset_reason','explicit_prefix_reset')
         WHERE idempotency_key LIKE {sql_literal(like)}
           AND status IN ('failed','dead_letter','claimed');
        SELECT ROW_COUNT();
    """)
    return int((out or "0").splitlines()[-1])


def mark_applied(target: Target, action_uuid: str, event_uuid: str | None, result: dict[str, Any]) -> str:
    row = first_row(target, f"""
        SET @status=NULL;
        CALL mmo_mark_server_action_applied(UUID_TO_BIN({sql_literal(action_uuid)},1), {bin_uuid(event_uuid)}, {json_sql(result)}, @status);
        SELECT @status;
    """)
    return row[0]


def mark_failed(target: Target, action_uuid: str, code: str, message: str, retryable: bool) -> str:
    row = first_row(target, f"""
        SET @status=NULL;
        CALL mmo_mark_server_action_failed(UUID_TO_BIN({sql_literal(action_uuid)},1), {sql_literal(code[:64])}, {sql_literal(message[:1000])}, {sql_literal(retryable)}, @status);
        SELECT @status;
    """)
    return row[0]


def record_result(target: Target, run_uuid: str, action: Action, status: str, event_uuid: str | None, details: dict[str, Any], error_code: str | None = None, error_message: str | None = None) -> None:
    run_mysql(target, f"""
        CALL mmo_record_server_action_worker_result(
          UUID_TO_BIN({sql_literal(run_uuid)},1),
          UUID_TO_BIN({sql_literal(action.action_uuid)},1),
          {sql_literal(action.kind)},
          {sql_literal(status)},
          {bin_uuid(event_uuid)},
          {sql_literal(error_code)},
          {sql_literal(error_message)},
          {json_sql(details)}
        );
    """)


def scalar_int(value: Any, default: int | None = None) -> int | None:
    try:
        if value is None or value == "":
            return default
        return int(value)
    except (TypeError, ValueError):
        return default




def client_payload(payload: dict[str, Any]) -> dict[str, Any]:
    raw = payload.get("client_payload")
    return raw if isinstance(raw, dict) else {}


def payload_first(payload: dict[str, Any], *keys: str) -> Any:
    client = client_payload(payload)
    for key in keys:
        value = payload.get(key)
        if value not in (None, ""):
            return value
    for key in keys:
        value = client.get(key)
        if value not in (None, ""):
            return value
    return None


def normalize_equipment_slot(value: Any) -> str:
    if isinstance(value, str) and value in {"weapon_melee", "weapon_ranged", "shield", "armor", "belt", "amulet", "ring_left", "ring_right", "rune", "torch", "unknown"}:
        return value
    try:
        slot = int(value)
    except (TypeError, ValueError):
        return "unknown"
    if slot == 1:
        return "weapon_melee"
    if slot == 2:
        return "weapon_ranged"
    return "unknown"


def parse_world_item_key(value: str | None) -> dict[str, Any]:
    text = str(value or "")
    # Hook key: world-item:newworld.zen:pid:67:sym:6765
    m = re.match(r"^world-item:(?P<world>.*):pid:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    # Runtime SQLite/MySQL import key: world_item:newworld.zen:67:6765:<slot>
    m = re.match(r"^world_item:(?P<world>.*):(?P<pid>\d+):(?P<sym>\d+)(?::.*)?$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    return {"raw": text}


def resolve_world_item_entity_key(target: Target, action: Action) -> dict[str, Any]:
    payload = action.payload
    raw_key = payload_first(payload, "world_item_entity_key", "engine_world_item_key", "target_key") or action.target_key
    parsed = parse_world_item_key(str(raw_key or ""))
    pid = scalar_int(payload_first(payload, "source_world_item_persistent_id", "item_persistent_id"), parsed.get("persistent_id"))
    sym = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"), parsed.get("symbol"))
    world = payload_first(payload, "world") or parsed.get("world")

    exact = str(raw_key or "")
    candidates = [exact]
    if world and pid is not None and sym is not None:
        candidates.append(f"world_item:{world}:{pid}:{sym}:%")

    row = first_row(target, f"""
        SELECT wes.entity_key, wes.lifecycle_state,
               JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS persistent_id,
               JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS template_symbol
          FROM world_entity_state wes
         WHERE wes.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wes.entity_kind = 'item'
           AND (
                wes.entity_key = {sql_literal(exact)}
                OR wes.entity_key LIKE {sql_literal(candidates[-1] if len(candidates) > 1 else '__NO_LIKE__')}
                OR ({'TRUE' if pid is not None else 'FALSE'} AND CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED) = {int(pid or -1)})
           )
           AND ({'TRUE' if sym is not None else 'FALSE'} = FALSE OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED) = {int(sym or -1)})
         ORDER BY CASE WHEN wes.entity_key = {sql_literal(exact)} THEN 0 ELSE 1 END, wes.updated_at DESC
         LIMIT 2;
    """)
    if not row:
        raise ResolveError(f"world item not found for key={exact!r} pid={pid} sym={sym} world={world!r}")
    # first_row only returns one row; do a count for ambiguity
    count_row = first_row(target, f"""
        SELECT COUNT(*)
          FROM world_entity_state wes
         WHERE wes.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wes.entity_kind = 'item'
           AND (
                wes.entity_key = {sql_literal(exact)}
                OR wes.entity_key LIKE {sql_literal(candidates[-1] if len(candidates) > 1 else '__NO_LIKE__')}
                OR ({'TRUE' if pid is not None else 'FALSE'} AND CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED) = {int(pid or -1)})
           )
           AND ({'TRUE' if sym is not None else 'FALSE'} = FALSE OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED) = {int(sym or -1)});
    """)
    cnt = int(count_row[0]) if count_row else 0
    if cnt != 1:
        raise ResolveError(f"world item key is ambiguous: candidates={cnt} key={exact!r} pid={pid} sym={sym}")
    if row[1] != "active":
        raise ResolveError(f"world item resolved but is not active: entity_key={row[0]} lifecycle={row[1]} pid={pid} sym={sym}")
    return {"world_item_entity_key": row[0], "lifecycle_state": row[1], "persistent_id": row[2], "item_symbol": row[3], "resolver": "world_item_engine_key_v2"}



def parse_interactive_key(value: str | None) -> dict[str, Any]:
    text = str(value or "")
    # Runtime SQLite/MySQL import key and Step47 hook key:
    # mobsi:newworld.zen:<slot_id>:<vob_id>:<focus_name>
    m = re.match(r"^mobsi:(?P<world>.*):(?P<slot>\d+):(?P<vob>\d+):(?P<focus>.*)$", text)
    if m:
        return {
            "world": m.group("world"),
            "slot_id": int(m.group("slot")),
            "vob_id": int(m.group("vob")),
            "focus_name": m.group("focus"),
            "raw": text,
        }
    return {"raw": text}


def resolve_interactive_entity_key(target: Target, action: Action) -> dict[str, Any]:
    payload = action.payload
    raw_key = payload_first(payload, "interactive_key", "interactive_entity_key", "target_key") or action.target_key
    parsed = parse_interactive_key(str(raw_key or ""))
    slot_id = scalar_int(payload_first(payload, "slot_id"), parsed.get("slot_id"))
    vob_id = scalar_int(payload_first(payload, "vob_id"), parsed.get("vob_id"))
    exact = str(raw_key or "")

    predicates = [f"wes.entity_key = {sql_literal(exact)}"]
    if vob_id is not None and slot_id is not None:
        predicates.append(
            "CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.vob_id')) AS SIGNED) = "
            f"{int(vob_id)} AND CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.slot_id')) AS SIGNED) = {int(slot_id)}"
        )
    elif vob_id is not None:
        predicates.append(f"CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.vob_id')) AS SIGNED) = {int(vob_id)}")
    where = " OR ".join(f"({p})" for p in predicates if p) or "FALSE"

    rows_out = run_mysql(target, f"""
        SELECT wes.entity_key, wes.lifecycle_state, wes.row_version,
               JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.slot_id')) AS slot_id,
               JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.vob_id')) AS vob_id
          FROM world_entity_state wes
         WHERE wes.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wes.entity_kind = 'interactive'
           AND ({where})
         ORDER BY CASE WHEN wes.entity_key = {sql_literal(exact)} THEN 0 ELSE 1 END, wes.updated_at DESC
         LIMIT 3;
    """)
    lines = [line.split("\t") for line in rows_out.splitlines() if line.strip()]
    if not lines:
        raise ResolveError(f"interactive not found for key={exact!r} slot={slot_id} vob={vob_id}")
    if len(lines) != 1:
        raise ResolveError(f"interactive key is ambiguous: candidates={len(lines)} key={exact!r} slot={slot_id} vob={vob_id}")
    row = lines[0]
    return {
        "interactive_key": row[0],
        "lifecycle_state": row[1],
        "row_version_before": int(row[2] or 0),
        "slot_id": row[3],
        "vob_id": row[4],
        "resolver": "interactive_engine_key_v1",
    }

def _character_item_rows(target: Target, action: Action, item_symbol: int, extra_predicate: str = "TRUE", limit: int = 5) -> list[list[str]]:
    rows = run_mysql(target, f"""
        SELECT BIN_TO_UUID(ii.item_instance_id,1),
               ii.item_instance_key,
               ci.bag_index,
               ci.amount,
               ii.quantity,
               JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_instance_key')) AS raw_item_key
          FROM item_instances ii
          JOIN character_inventory ci ON ci.item_instance_id = ii.item_instance_id
          JOIN content_item_templates it ON it.item_template_id = ii.item_template_id
         WHERE ii.realm_id = (SELECT realm_id FROM server_sessions WHERE session_id = UUID_TO_BIN({sql_literal(action.session_uuid)},1) LIMIT 1)
           AND ii.owner_type = 'character'
           AND ii.owner_id = UUID_TO_BIN({sql_literal(action.character_uuid)},1)
           AND ii.lifecycle_state = 'active'
           AND ci.character_id = UUID_TO_BIN({sql_literal(action.character_uuid)},1)
           AND it.symbol_index = {int(item_symbol)}
           AND {extra_predicate}
         ORDER BY ci.amount DESC, ii.updated_at DESC, ii.item_instance_key ASC
         LIMIT {int(limit)};
    """)
    return [ln.split("\t") for ln in rows.splitlines() if ln.strip()]


def resolve_character_item_instance(target: Target, action: Action) -> dict[str, Any]:
    payload = action.payload
    if payload.get("item_instance_id"):
        return {"item_instance_uuid": str(payload["item_instance_id"]), "resolver": "payload.item_instance_id"}

    item_symbol = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"))
    item_pid = scalar_int(payload_first(payload, "item_persistent_id", "source_world_item_persistent_id"))
    required_amount = max(1, optional_int_payload(action, "amount", default=1))
    if item_symbol is None:
        raise ResolveError("item_symbol is required to resolve character item instance")

    preferred_rows: list[list[str]] = []
    if item_pid is not None:
        pid_text = str(item_pid)
        sym_text = str(item_symbol)
        # Runtime SQLite inventory keys are usually '<symbol>:<slot>:<equipped>';
        # older world/import keys can be '<pid>:<symbol>:...' or raw payload fields.
        pid_pred = f"""(
             JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id')) = {sql_literal(item_pid)}
             OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_world_item_persistent_id')) = {sql_literal(item_pid)}
             OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_instance_key')) LIKE {sql_literal(sym_text + ':' + pid_text + ':%')}
             OR ii.item_instance_key LIKE {sql_literal('%:' + sym_text + ':' + pid_text + ':%')}
             OR ii.item_instance_key LIKE {sql_literal('%:' + pid_text + ':' + sym_text + ':%')}
        )"""
        preferred_rows = _character_item_rows(target, action, item_symbol, pid_pred, limit=5)

    if len(preferred_rows) == 1:
        row = preferred_rows[0]
        return {
            "item_instance_uuid": row[0],
            "item_instance_key": row[1],
            "bag_index": row[2],
            "amount": row[3],
            "resolver": "character_inventory_symbol_pid_v3",
        }
    if len(preferred_rows) > 1:
        raise ResolveError(f"character item instance ambiguous for symbol={item_symbol} pid={item_pid}: {len(preferred_rows)} preferred candidates")

    # Some OpenGothic inventory items do not have a durable per-stack persistent id
    # in the imported MySQL raw payload. For stack resources such as arrows/bolts,
    # fall back to symbol-only only when the server projection is unique enough.
    fallback_rows = _character_item_rows(
        target,
        action,
        item_symbol,
        f"COALESCE(ci.amount, ii.quantity, 0) >= {int(required_amount)}",
        limit=5,
    )
    if len(fallback_rows) == 1:
        row = fallback_rows[0]
        return {
            "item_instance_uuid": row[0],
            "item_instance_key": row[1],
            "bag_index": row[2],
            "amount": row[3],
            "resolver": "character_inventory_symbol_unique_v3",
            "persistent_id_unmatched": item_pid,
        }
    if not fallback_rows:
        raise ResolveError(f"character item instance not found for symbol={item_symbol} pid={item_pid}")
    sample = ", ".join(r[1] for r in fallback_rows[:3])
    raise ResolveError(f"character item instance ambiguous for symbol={item_symbol} pid={item_pid}: {len(fallback_rows)} symbol candidates: {sample}")





def parse_npc_entity_key(value: str | None) -> dict[str, Any]:
    text = str(value or "")
    # Step38 hook key: npc:newworld.zen:pid:123:sym:456
    m = re.match(r"^npc:(?P<world>.*):pid:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    # Runtime SQLite/MySQL import key: npc:<world>:<pid>:<symbol>:<script_id>
    m = re.match(r"^npc:(?P<world>.*):(?P<pid>\d+):(?P<sym>\d+)(?::(?P<script>\d+))?$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    # Older actor key: npc:123:sym:456
    m = re.match(r"^npc:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    return {"raw": text}


def resolve_world_npc_entity_key(target: Target, action: Action, *payload_keys: str) -> dict[str, Any]:
    payload = action.payload
    raw_key = payload_first(payload, *payload_keys) if payload_keys else None
    raw_key = raw_key or payload_first(payload, "target_npc_entity_key", "npc_entity_key", "npc_key", "target_key") or action.target_key
    parsed = parse_npc_entity_key(str(raw_key or ""))
    pid = scalar_int(payload_first(payload, "target_npc_persistent_id", "npc_persistent_id", "persistent_id"), parsed.get("persistent_id"))
    sym = scalar_int(payload_first(payload, "target_npc_symbol", "npc_symbol", "symbol"), parsed.get("symbol"))
    world = payload_first(payload, "world") or parsed.get("world")
    exact = str(raw_key or "")

    like_patterns: list[str] = []
    if world and pid is not None and sym is not None:
        # Runtime SQLite import key shape is npc:<world>:<pid>:<symbol>:<script_id>.
        # Step38 hook key shape is npc:<world>:pid:<pid>:sym:<symbol>.
        like_patterns.append(f"npc:{world}:{pid}:{sym}:%")
    like_sql = " OR ".join(f"wes.entity_key LIKE {sql_literal(p)}" for p in like_patterns) or "FALSE"

    row_sql = f"""
        SELECT wes.entity_key, wes.lifecycle_state, wes.row_version,
               COALESCE(
                 JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')),
                 REGEXP_SUBSTR(wes.entity_key, ':[0-9]+:', 1, 1)
               ) AS persistent_id,
               COALESCE(
                 JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')),
                 JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')),
                 JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol'))
               ) AS symbol_index
          FROM world_entity_state wes
         WHERE wes.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wes.entity_kind IN ('npc','creature')
           AND (
                wes.entity_key = {sql_literal(exact)}
                OR {like_sql}
                OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.creature_spawn_key')) = {sql_literal(exact)}
                OR ({'TRUE' if pid is not None else 'FALSE'} AND (
                    CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED) = {int(pid or -1)}
                    OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.creature_spawn_key')) LIKE {sql_literal('%:' + str(pid or -1) + ':' + str(sym or -1) + ':%')}
                ))
                OR ({'TRUE' if sym is not None else 'FALSE'} AND (
                    CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED) = {int(sym or -1)}
                    OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED) = {int(sym or -1)}
                    OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED) = {int(sym or -1)}
                ))
           )
         ORDER BY CASE WHEN wes.entity_key = {sql_literal(exact)} THEN 0 ELSE 1 END,
                  CASE WHEN {like_sql} THEN 1 ELSE 2 END,
                  CASE WHEN wes.lifecycle_state='active' THEN 0 ELSE 1 END,
                  wes.updated_at DESC
         LIMIT 5;
    """
    out = run_mysql(target, row_sql)
    rows = [line.split("\t") for line in out.splitlines() if line.strip()]
    if not rows:
        raise ResolveError(f"NPC/world entity not found for key={exact!r} pid={pid} sym={sym}")

    exact_rows = [r for r in rows if r[0] == exact]
    if len(exact_rows) == 1:
        rows = exact_rows
    elif like_patterns:
        alias_rows = [r for r in rows if any(r[0].startswith(p[:-1]) if p.endswith('%') else r[0] == p for p in like_patterns)]
        if len(alias_rows) == 1:
            rows = alias_rows
        elif len(alias_rows) > 1:
            active_alias_rows = [r for r in alias_rows if r[1] == 'active']
            if len(active_alias_rows) == 1:
                rows = active_alias_rows
            else:
                raise ResolveError(f"NPC/world entity key is ambiguous: alias_candidates={len(alias_rows)} key={exact!r} pid={pid} sym={sym}")
        elif len(rows) > 1:
            raise ResolveError(f"NPC/world entity key is ambiguous: candidates={len(rows)} key={exact!r} pid={pid} sym={sym}")
    elif len(rows) > 1:
        raise ResolveError(f"NPC/world entity key is ambiguous: candidates={len(rows)} key={exact!r} pid={pid} sym={sym}")

    row = rows[0]
    return {
        "npc_key": row[0],
        "world_entity_key": row[0],
        "lifecycle_state": row[1],
        "row_version": row[2],
        "persistent_id": row[3],
        "symbol_index": row[4],
        "resolver": "npc_world_entity_key_v2",
        "requested_key": exact,
    }


def resolve_world_inventory_item_instance(target: Target, action: Action, owner_entity_key: str) -> dict[str, Any]:
    item_symbol = scalar_int(payload_first(action.payload, "item_symbol", "inventory_item_symbol"))
    if item_symbol is None:
        raise ResolveError("item_symbol is required to resolve world/NPC inventory item")
    rows = run_mysql(target, f"""
        SELECT BIN_TO_UUID(ii.item_instance_id,1), ii.item_instance_key, wi.amount
          FROM world_inventory wi
          JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
          JOIN content_item_templates it ON it.item_template_id = ii.item_template_id
         WHERE wi.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wi.owner_entity_key = {sql_literal(owner_entity_key)}
           AND ii.lifecycle_state = 'active'
           AND it.symbol_index = {int(item_symbol)}
         ORDER BY ii.updated_at DESC
         LIMIT 3;
    """)
    lines = [ln.split("\t") for ln in rows.splitlines() if ln.strip()]
    if not lines:
        raise ResolveError(f"world/NPC inventory item not found owner={owner_entity_key!r} symbol={item_symbol}")
    if len(lines) > 1:
        raise ResolveError(f"world/NPC inventory item ambiguous owner={owner_entity_key!r} symbol={item_symbol}: {len(lines)} candidates")
    row = lines[0]
    return {"item_instance_uuid": row[0], "item_instance_key": row[1], "amount": row[2], "item_symbol": item_symbol, "resolver": "world_inventory_owner_symbol_v1"}

def next_character_bag_index(target: Target, character_uuid: str) -> int:
    """Return the first free non-negative bag_index for a character.

    The MySQL pickup procedure requires the caller to provide the target
    bag_index and enforces character_inventory_bag_uk. Client envelopes do not
    currently carry a server-authoritative bag index, so the dev worker must
    allocate one from the current projection instead of defaulting to 0.
    """
    out = run_mysql(target, f"""
        SELECT bag_index
          FROM character_inventory
         WHERE character_id = UUID_TO_BIN({sql_literal(character_uuid)},1)
         ORDER BY bag_index ASC;
    """)
    used: set[int] = set()
    for line in out.splitlines():
        line = line.strip()
        if not line or line == "NULL":
            continue
        try:
            idx = int(line.split("\t")[0])
        except (TypeError, ValueError):
            continue
        if idx >= 0:
            used.add(idx)
    candidate = 0
    while candidate in used:
        candidate += 1
    return candidate

def server_tick(action: Action) -> int:
    payload = action.payload
    return max(0, int(payload_first(payload, "server_tick", "client_tick") or 0))


def bool_payload(value: Any, default: bool = False) -> bool:
    if value is None or value == "":
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "y", "on"}:
        return True
    if text in {"0", "false", "no", "n", "off"}:
        return False
    return default




def checkpoint_payload_value(action: Action, key: str, default: Any = None) -> Any:
    value = payload_first(action.payload, key)
    return default if value in (None, "") else value


def float_payload(action: Action, *keys: str, default: float | None = None) -> float:
    value = payload_first(action.payload, *keys)
    if value in (None, ""):
        if default is None:
            raise ResolveError("float payload required: " + "/".join(keys))
        return float(default)
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise ResolveError("invalid float payload: " + "/".join(keys)) from exc


def quest_status_payload(value: Any) -> str:
    """Normalize Gothic/runtime quest status values to the MySQL procedure contract."""
    if value is None or value == "":
        return "running"
    text = str(value).strip().lower()
    aliases = {
        "1": "running",
        "running": "running",
        "run": "running",
        "in_progress": "running",
        "2": "success",
        "success": "success",
        "completed_success": "success",
        "succeeded": "success",
        "3": "failed",
        "failed": "failed",
        "failure": "failed",
        "completed_failed": "failed",
        "4": "obsolete",
        "obsolete": "obsolete",
        "closed": "obsolete",
    }
    return aliases.get(text, text)


DIALOG_AVAILABILITY_STATES = {
    "consumed_hidden",
    "repeatable_known",
    "repeatable_not_seen",
    "one_shot_not_seen",
}


def derive_dialog_availability(known: bool, permanent: bool) -> str:
    if known and permanent:
        return "repeatable_known"
    if known and not permanent:
        return "consumed_hidden"
    if not known and permanent:
        return "repeatable_not_seen"
    return "one_shot_not_seen"


def dialog_permanent_payload(payload: dict[str, Any]) -> bool:
    explicit = payload_first(payload, "permanent", "repeatable")
    if explicit is not None:
        return bool_payload(explicit, False)

    removed = payload_first(payload, "removed")
    if removed is not None:
        # OpenGothic Step37 uses removed=true for one-shot consumed dialog choices.
        # MySQL procedure expects permanent=false in that case.
        return not bool_payload(removed, False)

    return False


def dialog_availability_payload(payload: dict[str, Any], known: bool, permanent: bool) -> str:
    explicit = payload_first(payload, "availability_state", "dialog_availability_state")
    if explicit is None:
        return derive_dialog_availability(known, permanent)
    text = str(explicit).strip().lower()
    if text in DIALOG_AVAILABILITY_STATES:
        return text
    raise ResolveError(f"invalid dialog availability_state payload: {text}")

def json_array_sql(value: Any) -> str:
    if isinstance(value, list):
        return json_sql(value)
    if value in (None, ""):
        return "JSON_ARRAY()"
    return json_sql([str(value)])


def script_key_from_payload(action: Action) -> str:
    value = payload_first(action.payload, "script_key", "global_key", "symbol_name", "target_key") or action.target_key
    if value in (None, ""):
        raise ResolveError("script_key/global_key/symbol_name is required")
    return str(value)


def int_payload(action: Action, *keys: str, default: int | None = None) -> int:
    value = payload_first(action.payload, *keys)
    parsed = scalar_int(value, default)
    if parsed is None:
        raise ResolveError("integer payload required: " + "/".join(keys))
    return parsed


def optional_int_payload(action: Action, *keys: str, default: int = 0) -> int:
    value = payload_first(action.payload, *keys)
    parsed = scalar_int(value, default)
    return default if parsed is None else parsed


def optional_float_sql(action: Action, *keys: str) -> str:
    value = payload_first(action.payload, *keys)
    if value in (None, ""):
        return "NULL"
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise ResolveError("invalid float payload: " + "/".join(keys)) from exc
    if not math.isfinite(parsed):
        raise ResolveError("non-finite float payload: " + "/".join(keys))
    return sql_literal(parsed)



def metadata(action: Action, resolved: dict[str, Any]) -> dict[str, Any]:
    base = action.payload.get("metadata") if isinstance(action.payload.get("metadata"), dict) else {}
    return {
        **base,
        "source": "run_mmo_resolved_action_worker",
        "worker_mode": WORKER_MODE,
        "action_uuid": action.action_uuid,
        "client_idempotency_key": payload_first(action.payload, "client_idempotency_key", "idempotency_key"),
        "client_target_key": payload_first(action.payload, "client_target_key", "target_key") or action.target_key,
        "resolver": resolved,
    }


def dispatch(target: Target, action: Action, bootstrap_sample_limit: int = 10, movement_config: MovementAuthorityConfig | None = None) -> tuple[str | None, dict[str, Any]]:
    kind = action.kind
    tick = server_tick(action)
    idem = action.idempotency_key

    if kind == "pickup_world_item":
        resolved = resolve_world_item_entity_key(target, action)
        amount = int(payload_first(action.payload, "amount") or 1)
        server_bag_index = action.payload.get("server_bag_index")
        if server_bag_index not in (None, ""):
            bag_index = int(server_bag_index)
            bag_resolver = "payload.server_bag_index"
        else:
            # Do not trust/guess client bag_index here. The server projection
            # owns character_inventory.bag_index uniqueness, so allocate the
            # first free slot from DB right before calling the procedure.
            bag_index = next_character_bag_index(target, action.character_uuid)
            bag_resolver = "server_next_free_bag_index_v3"
        row = first_row(target, f"""
            SET @event_id=NULL; SET @item_id=NULL; SET @amount_picked=NULL;
            CALL mmo_pickup_world_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(resolved['world_item_entity_key'])},
              {int(amount)},
              {int(bag_index)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @item_id, @amount_picked
            );
            SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@item_id,1), @amount_picked;
        """)
        resolved = {**resolved, "bag_index": bag_index, "bag_index_resolver": bag_resolver}
        return row[0], {"item_instance_uuid": row[1], "amount_picked": int(row[2]), "bag_index": bag_index, "resolved": resolved}

    if kind == "remove_world_item":
        resolved = resolve_world_item_entity_key(target, action)
        reason = action.payload.get("reason") or "semantic_action"
        row = first_row(target, f"""
            SET @event_id=NULL; SET @item_id=NULL;
            CALL mmo_remove_world_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(resolved['world_item_entity_key'])},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @item_id
            );
            SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@item_id,1);
        """)
        return row[0], {"item_instance_uuid": row[1], "resolved": resolved}

    if kind == "equip_character_item":
        resolved = resolve_character_item_instance(target, action)
        slot = normalize_equipment_slot(payload_first(action.payload, "equipment_slot", "slot"))
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_equip_character_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              UUID_TO_BIN({sql_literal(resolved['item_instance_uuid'])},1),
              {sql_literal(slot)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"item_instance_uuid": resolved["item_instance_uuid"], "equipment_slot": slot, "resolved": resolved}

    if kind == "unequip_character_item":
        slot = normalize_equipment_slot(payload_first(action.payload, "equipment_slot", "slot"))
        row = first_row(target, f"""
            SET @event_id=NULL; SET @item_id=NULL;
            CALL mmo_unequip_character_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(slot)},
              {int(tick)},
              {json_sql(metadata(action, {"resolver":"slot_only_v2"}))},
              {sql_literal(idem)},
              @event_id, @item_id
            );
            SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@item_id,1);
        """)
        return row[0], {"item_instance_uuid": row[1], "equipment_slot": slot, "resolved": {"resolver": "slot_only_v2"}}

    if kind == "set_script_int":
        script_key = script_key_from_payload(action)
        symbol_index = optional_int_payload(action, "symbol_index", default=0)
        value_index = optional_int_payload(action, "value_index", default=0)
        value_after = int_payload(action, "value_after", "value")
        resolved = {"resolver": "script_key_payload_v1", "script_key": script_key, "symbol_index": symbol_index, "value_index": value_index}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @value_after=NULL;
            CALL mmo_set_character_script_int(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(script_key)},
              {sql_literal(symbol_index)},
              {int(value_index)},
              {int(value_after)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @value_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @value_after;
        """)
        return row[0], {"script_key": script_key, "value_after": int(row[1]), "resolved": resolved}

    if kind == "adjust_progression":
        exp_delta = int_payload(action, "experience_delta", "xp_delta", "delta")
        lp_delta = optional_int_payload(action, "learning_points_delta", "lp_delta", default=0)
        reason = str(payload_first(action.payload, "reason") or "script_progression")
        resolved = {"resolver": "progression_delta_payload_v1", "experience_delta": exp_delta, "learning_points_delta": lp_delta, "reason": reason}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @experience_after=NULL; SET @learning_points_after=NULL;
            CALL mmo_adjust_character_progression(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {int(exp_delta)},
              {int(lp_delta)},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @experience_after, @learning_points_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @experience_after, @learning_points_after;
        """)
        return row[0], {"experience_after": int(row[1]), "learning_points_after": int(row[2]), "resolved": resolved}

    if kind == "apply_experience_reward":
        exp_delta = int_payload(action, "experience_delta", "xp_delta", "delta")
        reason = str(payload_first(action.payload, "reason") or "script_experience_reward")
        resolved = {"resolver": "experience_reward_payload_v1", "experience_delta": exp_delta, "reason": reason}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @experience_after=NULL;
            CALL mmo_apply_character_experience_reward(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {int(exp_delta)},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @experience_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @experience_after;
        """)
        return row[0], {"experience_after": int(row[1]), "resolved": resolved}

    if kind == "update_quest":
        quest_key = str(payload_first(action.payload, "quest_key", "topic", "target_key") or action.target_key or "")
        if not quest_key:
            raise ResolveError("quest_key/topic is required")
        quest_name = str(payload_first(action.payload, "quest_name", "name") or quest_key)
        status = quest_status_payload(payload_first(action.payload, "status"))
        entry_count = optional_int_payload(action, "entry_count", default=0)
        entries = payload_first(action.payload, "entries")
        resolved = {"resolver": "quest_payload_v1", "quest_key": quest_key, "status": status}
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_update_character_quest(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(quest_key)},
              {sql_literal(quest_name)},
              {sql_literal(status)},
              {int(entry_count)},
              {json_array_sql(entries)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"quest_key": quest_key, "status": status, "resolved": resolved}

    if kind == "set_known_dialog":
        npc_key = str(payload_first(action.payload, "npc_key", "npc_symbol_name") or "")
        info_key = str(payload_first(action.payload, "info_key", "info_symbol_name", "target_key") or action.target_key or "")
        if not npc_key or not info_key:
            raise ResolveError("npc_key and info_key are required")
        known = bool_payload(payload_first(action.payload, "known"), True)
        permanent = dialog_permanent_payload(action.payload)
        availability_state = dialog_availability_payload(action.payload, known, permanent)
        removed = payload_first(action.payload, "removed")
        removed_value = bool_payload(removed, not permanent) if removed is not None else (known and not permanent)
        reason = str(payload_first(action.payload, "reason") or "script_dialog_known")
        resolved = {
            "resolver": "known_dialog_payload_v2",
            "npc_key": npc_key,
            "info_key": info_key,
            "known": known,
            "permanent": permanent,
            "removed": removed_value,
            "availability_state": availability_state,
            "reason": reason,
        }
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_set_character_known_dialog(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc_key)},
              {sql_literal(info_key)},
              {sql_literal(known)},
              {sql_literal(permanent)},
              {sql_literal(availability_state)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {
            "npc_key": npc_key,
            "info_key": info_key,
            "known": known,
            "permanent": permanent,
            "removed": removed_value,
            "availability_state": availability_state,
            "resolved": resolved,
        }


    if kind == "consume_mana":
        mana_amount = int_payload(action, "mana_amount", "amount", "delta")
        resolved = {"resolver": "consume_mana_payload_v1", "mana_amount": mana_amount}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @mana_after=NULL;
            CALL mmo_consume_character_mana(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {int(mana_amount)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @mana_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @mana_after;
        """)
        return row[0], {"mana_after": int(row[1]), "resolved": resolved}

    if kind == "consume_item":
        resolved_item = resolve_character_item_instance(target, action)
        amount = int_payload(action, "amount", default=1)
        reason = str(payload_first(action.payload, "reason") or "consume_item")
        resolved = {**resolved_item, "resolver": "consume_item_character_inventory_v1", "amount": amount, "reason": reason}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @amount_after=NULL;
            CALL mmo_consume_character_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              UUID_TO_BIN({sql_literal(resolved_item['item_instance_uuid'])},1),
              {int(amount)},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @amount_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @amount_after;
        """)
        return row[0], {"amount_after": int(row[1]), "item_instance_uuid": resolved_item["item_instance_uuid"], "resolved": resolved}

    if kind == "apply_character_damage":
        character_key = str(payload_first(action.payload, "target_character_key", "character_key") or "PC_HERO")
        damage_amount = int_payload(action, "damage_amount", "amount", "delta")
        resolved = {"resolver": "character_damage_payload_v1", "target_character_key": character_key, "damage_amount": damage_amount}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @health_after=NULL;
            CALL mmo_apply_character_damage(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(character_key)},
              {int(damage_amount)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @health_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @health_after;
        """)
        return row[0], {"health_after": int(row[1]), "resolved": resolved}

    if kind == "apply_world_entity_damage":
        npc = resolve_world_npc_entity_key(target, action, "target_world_entity_key", "target_npc_entity_key", "target_key")
        damage_amount = int_payload(action, "damage_amount", "amount", "delta")
        fatal = bool_payload(payload_first(action.payload, "fatal", "dead"), False)
        resolved = {**npc, "resolver": "world_entity_damage_payload_v2", "damage_amount": damage_amount, "fatal": fatal}
        if str(npc.get("lifecycle_state") or "") != "active":
            # Dev replay can contain redundant local damage envelopes after a prior
            # mark_npc_dead envelope has already made the server projection inactive.
            # The authoritative DB procedure correctly rejects damage on inactive
            # targets; for isolated replay evidence this stale consequence is accepted
            # as an applied no-op, with no journal event and full resolver metadata.
            result = {
                "applied_noop": True,
                "noop_reason": "target_entity_not_active",
                "event_emitted": False,
                "resolved": resolved,
            }
            return None, result
        row = first_row(target, f"""
            SET @event_id=NULL; SET @health_after=NULL; SET @row_after=NULL;
            CALL mmo_apply_world_entity_damage(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              {int(damage_amount)},
              {sql_literal(fatal)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @health_after, @row_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @health_after, @row_after;
        """)
        return row[0], {"health_after": int(row[1]), "row_version_after": int(row[2]), "resolved": resolved}

    if kind == "mark_npc_dead":
        npc = resolve_world_npc_entity_key(target, action, "target_world_entity_key", "target_npc_entity_key", "target_key")
        resolved = {**npc, "resolver": "mark_npc_dead_payload_v2"}
        if str(npc.get("lifecycle_state") or "") != "active":
            result = {
                "applied_noop": True,
                "noop_reason": "target_entity_already_inactive",
                "event_emitted": False,
                "resolved": resolved,
            }
            return None, result
        row = first_row(target, f"""
            SET @event_id=NULL; SET @row_after=NULL;
            CALL mmo_mark_npc_dead(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @row_after
            );
            SELECT BIN_TO_UUID(@event_id,1), COALESCE(@row_after, 0);
        """)
        return row[0], {"row_version_after": int(row[1]), "resolved": resolved}

    if kind == "trade_sell_to_npc":
        npc = resolve_world_npc_entity_key(target, action, "npc_entity_key", "npc_key")
        item = resolve_character_item_instance(target, action)
        price_total = optional_int_payload(action, "price_total", default=0)
        if price_total == 0:
            price_total = optional_int_payload(action, "unit_price", default=0) * max(1, optional_int_payload(action, "amount", default=1))
        currency_key = str(payload_first(action.payload, "currency_key") or "g2notr:gold")
        resolved = {**npc, **item, "resolver": "trade_sell_payload_v1", "price_total": price_total, "currency_key": currency_key}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @wallet_after=NULL;
            CALL mmo_trade_sell_to_npc(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              UUID_TO_BIN({sql_literal(item['item_instance_uuid'])},1),
              {int(price_total)},
              {sql_literal(currency_key)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @wallet_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @wallet_after;
        """)
        return row[0], {"wallet_after": int(row[1]), "resolved": resolved}

    if kind == "trade_buy_from_npc":
        npc = resolve_world_npc_entity_key(target, action, "npc_entity_key", "npc_key")
        item = resolve_world_inventory_item_instance(target, action, npc["world_entity_key"])
        price_total = optional_int_payload(action, "price_total", default=0)
        if price_total == 0:
            price_total = optional_int_payload(action, "unit_price", default=0) * max(1, optional_int_payload(action, "amount", default=1))
        currency_key = str(payload_first(action.payload, "currency_key") or "g2notr:gold")
        target_bag = payload_first(action.payload, "target_bag_index", "server_bag_index")
        target_bag_sql = "NULL" if target_bag in (None, "") else str(int(target_bag))
        resolved = {**npc, **item, "resolver": "trade_buy_payload_v1", "price_total": price_total, "currency_key": currency_key, "target_bag_index": target_bag}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @wallet_after=NULL; SET @bag_index=NULL;
            CALL mmo_trade_buy_from_npc(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              UUID_TO_BIN({sql_literal(item['item_instance_uuid'])},1),
              {int(price_total)},
              {sql_literal(currency_key)},
              {target_bag_sql},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @wallet_after, @bag_index
            );
            SELECT BIN_TO_UUID(@event_id,1), @wallet_after, @bag_index;
        """)
        return row[0], {"wallet_after": int(row[1]), "bag_index": int(row[2]), "resolved": resolved}


    if kind == "use_interactive":
        state_after = optional_int_payload(action, "state", "state_after", default=-1)
        resolved = {
            "resolver": "use_interactive_step67_procedure_v1",
            "interactive_key": payload_first(action.payload, "interactive_key", "interactive_entity_key", "target_key") or action.target_key,
            "actor_key": payload_first(action.payload, "actor_key"),
            "state": state_after,
            "locked": payload_first(action.payload, "locked", "locked_after"),
            "cracked": payload_first(action.payload, "cracked", "cracked_after"),
            "reason": payload_first(action.payload, "reason") or "use_interactive",
        }
        row = first_row(target, f"""
            SET @event_id=NULL; SET @row_after=NULL;
            CALL mmo_record_interactive_use(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(resolved['interactive_key'])},
              {int(state_after)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id,
              @row_after
            );
            SELECT BIN_TO_UUID(@event_id,1), COALESCE(@row_after, 0);
        """)
        return row[0], {
            "response_kind": "interactive_use_ack",
            "interactive_status": "recorded",
            "row_version_after": int(row[1] or 0),
            "resolved": resolved,
        }


    if kind == "trigger_event":
        resolved = {
            "resolver": "trigger_event_step51_procedure_v1",
            "trigger_key": payload_first(action.payload, "trigger_key", "target_key") or action.target_key,
            "trigger_vob_id": payload_first(action.payload, "trigger_vob_id"),
            "trigger_name": payload_first(action.payload, "trigger_name"),
            "trigger_target": payload_first(action.payload, "trigger_target"),
            "event_target": payload_first(action.payload, "event_target"),
            "event_emitter": payload_first(action.payload, "event_emitter"),
            "event_type_name": payload_first(action.payload, "event_type_name") or "trigger",
            "capture_cause": payload_first(action.payload, "capture_cause"),
            "player_caused": payload_first(action.payload, "player_caused"),
            "reason": payload_first(action.payload, "reason") or "world_trigger_event",
        }
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_record_trigger_event(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(resolved['trigger_key'])},
              {sql_literal(resolved['event_type_name'])},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"resolved": resolved}

    if kind == "mover_state_changed":
        state_after = int_payload(action, "state_after", "state", default=0)
        state_before = optional_int_payload(action, "state_before", default=-1)
        frame = optional_int_payload(action, "frame", default=-1)
        target_frame = optional_int_payload(action, "target_frame", default=-1)
        resolved = {
            "resolver": "mover_state_step51_procedure_v1",
            "mover_key": payload_first(action.payload, "mover_key", "target_key") or action.target_key,
            "mover_vob_id": payload_first(action.payload, "mover_vob_id"),
            "mover_name": payload_first(action.payload, "mover_name"),
            "state_before": state_before,
            "state_after": state_after,
            "state_before_name": payload_first(action.payload, "state_before_name"),
            "state_after_name": payload_first(action.payload, "state_after_name"),
            "frame": frame,
            "target_frame": target_frame,
            "capture_cause": payload_first(action.payload, "capture_cause"),
            "player_caused": payload_first(action.payload, "player_caused"),
            "reason": payload_first(action.payload, "reason") or "mover_state_changed",
        }
        row = first_row(target, f"""
            SET @event_id=NULL; SET @row_after=NULL;
            CALL mmo_record_mover_state(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(resolved['mover_key'])},
              {int(state_before)},
              {int(state_after)},
              {sql_literal(resolved['state_after_name']) if resolved['state_after_name'] not in (None, '') else 'NULL'},
              {int(frame)},
              {int(target_frame)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id,
              @row_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @row_after;
        """)
        return row[0], {"row_version_after": int(row[1] or 0), "resolved": resolved}

    if kind == "update_interactive_state":
        resolved = resolve_interactive_entity_key(target, action)
        state_after = int_payload(action, "state_after", "state")
        state_count = int_payload(action, "state_count", default=0)
        state_mask = int_payload(action, "state_mask", default=0)
        locked_after = bool_payload(payload_first(action.payload, "locked_after", "locked"), False)
        cracked_after = bool_payload(payload_first(action.payload, "cracked_after", "cracked"), False)
        lifecycle = str(payload_first(action.payload, "lifecycle_state") or "active")
        resolved = {
            **resolved,
            "state_before": payload_first(action.payload, "state_before"),
            "state_after": state_after,
            "state_count": state_count,
            "state_mask": state_mask,
            "locked_after": locked_after,
            "cracked_after": cracked_after,
            "lifecycle_state": lifecycle,
            "capture_cause": payload_first(action.payload, "capture_cause"),
            "player_caused": payload_first(action.payload, "player_caused"),
            "reason": payload_first(action.payload, "reason") or "interactive_state_changed",
        }
        row = first_row(target, f"""
            SET @event_id=NULL; SET @row_version_after=NULL;
            CALL mmo_update_interactive_state(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(resolved['interactive_key'])},
              {int(state_after)},
              {int(state_count)},
              {int(state_mask)},
              {sql_literal(locked_after)},
              {sql_literal(cracked_after)},
              {sql_literal(lifecycle)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id,
              @row_version_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @row_version_after;
        """)
        return row[0], {"row_version_after": int(row[1] or 0), "resolved": resolved}


    if kind == "drop_character_item":
        item = resolve_character_item_instance(target, action)
        amount = max(1, optional_int_payload(action, "amount", default=1))
        world_item_entity_key = (
            payload_first(action.payload, "world_item_entity_key", "engine_world_item_key", "dropped_world_item_key", "target_key")
            or action.target_key
            or f"world_item:drop:{action.action_uuid}"
        )
        resolved = {
            **item,
            "resolver": "drop_character_item_step68_procedure_v1",
            "amount": amount,
            "world_item_entity_key": world_item_entity_key,
            "source_item_persistent_id": payload_first(action.payload, "source_item_persistent_id", "item_persistent_id"),
            "world_item_persistent_id": payload_first(action.payload, "world_item_persistent_id"),
        }
        row = first_row(target, f"""
            SET @event_id=NULL; SET @amount_remaining=NULL; SET @amount_dropped=NULL;
            CALL mmo_drop_character_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              UUID_TO_BIN({sql_literal(item['item_instance_uuid'])},1),
              {int(amount)},
              {sql_literal(world_item_entity_key)},
              {optional_float_sql(action, "pos_x", "world_pos_x", "actor_pos_x", "x")},
              {optional_float_sql(action, "pos_y", "world_pos_y", "actor_pos_y", "y")},
              {optional_float_sql(action, "pos_z", "world_pos_z", "actor_pos_z", "z")},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id,
              @amount_remaining,
              @amount_dropped
            );
            SELECT BIN_TO_UUID(@event_id,1), COALESCE(@amount_remaining, 0), COALESCE(@amount_dropped, 0);
        """)
        return row[0], {
            "response_kind": "drop_item_ack",
            "drop_status": "dropped_to_world",
            "amount_remaining": int(row[1] or 0),
            "amount_dropped": int(row[2] or 0),
            "item_instance_uuid": item["item_instance_uuid"],
            "world_item_entity_key": world_item_entity_key,
            "resolved": resolved,
        }


    if kind == "loot_npc_inventory":
        npc = resolve_world_npc_entity_key(target, action, "source_npc_key", "source_npc_entity_key", "npc_entity_key", "target_key")
        item = resolve_world_inventory_item_instance(target, action, npc["world_entity_key"])
        amount = max(1, optional_int_payload(action, "amount", default=1))
        target_bag = payload_first(action.payload, "target_bag_index", "server_bag_index")
        if target_bag in (None, ""):
            bag_index = next_character_bag_index(target, action.character_uuid)
            bag_index_resolver = "server_first_free_bag_index"
        else:
            bag_index = int(target_bag)
            bag_index_resolver = "payload_target_bag_index"
        resolved = {
            **npc,
            **item,
            "resolver": "loot_npc_inventory_step68_procedure_v1",
            "amount": amount,
            "target_bag_index": bag_index,
            "bag_index_resolver": bag_index_resolver,
            "source_item_persistent_id": payload_first(action.payload, "source_item_persistent_id", "item_persistent_id"),
            "source_dead": payload_first(action.payload, "source_dead"),
            "source_unconscious": payload_first(action.payload, "source_unconscious"),
        }
        row = first_row(target, f"""
            SET @event_id=NULL; SET @source_amount_remaining=NULL; SET @amount_looted=NULL;
            CALL mmo_loot_npc_inventory(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              UUID_TO_BIN({sql_literal(item['item_instance_uuid'])},1),
              {int(amount)},
              {int(bag_index)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id,
              @source_amount_remaining,
              @amount_looted
            );
            SELECT BIN_TO_UUID(@event_id,1), COALESCE(@source_amount_remaining, 0), COALESCE(@amount_looted, 0);
        """)
        return row[0], {
            "response_kind": "loot_npc_inventory_ack",
            "loot_status": "looted_to_character",
            "source_amount_remaining": int(row[1] or 0),
            "amount_looted": int(row[2] or 0),
            "item_instance_uuid": item["item_instance_uuid"],
            "source_npc_key": npc["world_entity_key"],
            "bag_index": bag_index,
            "resolved": resolved,
        }

    if kind in {"ready_weapon", "holster_weapon"}:
        actor_key = str(payload_first(action.payload, "actor_key", "actor_entity_key", "target_key") or action.target_key or "")
        weapon_state = str(payload_first(action.payload, "new_weapon_state", "weapon_state") or kind)
        ready = bool_payload(payload_first(action.payload, "ready"), kind == "ready_weapon")
        resolved = {
            "resolver": "npc_weapon_state_step51_procedure_v1",
            "actor_key": actor_key,
            "actor_entity_key": payload_first(action.payload, "actor_entity_key"),
            "previous_weapon_state": payload_first(action.payload, "previous_weapon_state"),
            "new_weapon_state": weapon_state,
            "ready": ready,
            "reason": payload_first(action.payload, "reason") or kind,
        }
        row = first_row(target, f"""
            SET @event_id=NULL; SET @row_after=NULL;
            CALL mmo_record_npc_weapon_state(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(actor_key)},
              {sql_literal(weapon_state)},
              {sql_literal(ready)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id,
              @row_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @row_after;
        """)
        return row[0], {"row_version_after": int(row[1] or 0), "resolved": resolved}


    if kind == "character_resource_delta":
        resource_key = str(payload_first(action.payload, "resource_key", "stat_key") or "")
        delta = optional_int_payload(action, "delta_amount", "amount", "delta", default=0)
        value_before = optional_int_payload(action, "value_before", default=0)
        value_after = optional_int_payload(action, "value_after", default=value_before + delta)
        character_key = str(payload_first(action.payload, "target_character_key", "character_key") or "PC_HERO")
        resolved = {
            "resolver": "character_resource_delta_step51_procedure_v1",
            "target_character_key": character_key,
            "target_key": payload_first(action.payload, "target_key") or action.target_key,
            "resource_key": resource_key,
            "delta_amount": delta,
            "value_before": value_before,
            "value_after": value_after,
            "reason": payload_first(action.payload, "reason") or "character_resource_delta",
        }
        row = first_row(target, f"""
            SET @event_id=NULL; SET @row_after=NULL;
            CALL mmo_record_character_resource_delta(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(character_key)},
              {sql_literal(resource_key)},
              {int(delta)},
              {int(value_before)},
              {int(value_after)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id,
              @row_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @row_after;
        """)
        return row[0], {"row_version_after": int(row[1] or 0), "resolved": resolved}

    if kind == "world_time_changed":
        before_ms = optional_int_payload(action, "world_time_before_ms", "time_before_ms", default=0)
        after_ms = optional_int_payload(action, "world_time_after_ms", "time_after_ms", default=before_ms)
        day_before = optional_int_payload(action, "world_day_before", default=0)
        day_after = optional_int_payload(action, "world_day_after", default=day_before)
        resolved = {
            "resolver": "world_time_changed_capture_only_v2",
            "target_key": payload_first(action.payload, "target_key") or action.target_key,
            "world": payload_first(action.payload, "world"),
            "world_time_before_ms": before_ms,
            "world_time_after_ms": after_ms,
            "world_day_before": day_before,
            "world_day_after": day_after,
            "time_delta_ms": after_ms - before_ms,
            "reason": payload_first(action.payload, "reason") or "world_time_changed",
        }
        return None, {
            "applied_noop": True,
            "event_emitted": False,
            "noop_reason": "world_time_changed_server_bound_no_mysql_mutation",
            "world_clock_status": "client_time_skip_not_authoritative",
            "resolved": resolved,
        }

    if kind == "spend_learning_points":
        stat_key = str(payload_first(action.payload, "stat_key", "talent_key", "attribute_key", "target_key") or action.target_key or "")
        lp_cost = optional_int_payload(action, "learning_points_cost", "lp_cost", "learning_points_delta", default=0)
        lp_cost = abs(int(lp_cost))
        value_before = optional_int_payload(action, "value_before", default=0)
        value_after = optional_int_payload(action, "value_after", default=value_before)
        gold_cost = optional_int_payload(action, "gold_cost", default=0)
        trainer_key = str(payload_first(action.payload, "trainer_key", "npc_key") or "")
        resolved = {
            "resolver": "spend_learning_points_step51_procedure_v1",
            "stat_key": stat_key,
            "learning_points_cost": lp_cost,
            "value_before": value_before,
            "value_after": value_after,
            "gold_cost": gold_cost,
            "trainer_key": trainer_key,
            "reason": payload_first(action.payload, "reason") or "trainer_learning_points_spent",
        }
        row = first_row(target, f"""
            SET @event_id=NULL; SET @lp_after=NULL;
            CALL mmo_spend_learning_points(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(stat_key)},
              {int(lp_cost)},
              {int(value_before)},
              {int(value_after)},
              {int(gold_cost)},
              {sql_literal(trainer_key) if trainer_key else 'NULL'},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id,
              @lp_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @lp_after;
        """)
        return row[0], {"learning_points_after": int(row[1] or 0), "resolved": resolved}

    if kind in {"character_teleport", "world_transition"}:
        pos_x = float_payload(action, "pos_x")
        pos_y = float_payload(action, "pos_y")
        pos_z = float_payload(action, "pos_z")
        yaw = float_payload(action, "rotation_yaw", "yaw", default=0.0)
        target_world = payload_first(action.payload, "target_world_instance_key", "world_instance_key")
        reason = str(payload_first(action.payload, "reason") or kind)
        resolved = {
            "resolver": "teleport_world_transition_step51_procedure_v1",
            "target_world_instance_key": target_world,
            "pos_x": pos_x,
            "pos_y": pos_y,
            "pos_z": pos_z,
            "rotation_yaw": yaw,
            "reason": reason,
        }
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_change_world_or_teleport_character(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(target_world) if target_world not in (None, '') else 'NULL'},
              {pos_x},
              {pos_y},
              {pos_z},
              {yaw},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"resolved": resolved}

    if kind == "respawn_world_item":
        policy = str(payload_first(action.payload, "respawn_policy_key", "policy_key", "target_key") or action.target_key or "")
        entity_key = str(payload_first(action.payload, "world_item_entity_key", "target_key") or action.target_key or "")
        amount = optional_int_payload(action, "amount", default=1)
        resolved = {"resolver": "respawn_world_item_step51_procedure_v1", "respawn_policy_key": policy, "world_item_entity_key": entity_key, "amount": amount}
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_respawn_world_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(policy)},
              {sql_literal(entity_key)},
              {int(amount)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"resolved": resolved}

    if kind == "respawn_container_item":
        policy = str(payload_first(action.payload, "respawn_policy_key", "policy_key", "target_key") or action.target_key or "")
        owner_key = str(payload_first(action.payload, "owner_entity_key", "container_key") or "")
        item_key = str(payload_first(action.payload, "item_instance_key") or "")
        amount = optional_int_payload(action, "amount", default=1)
        resolved = {"resolver": "respawn_container_item_step51_procedure_v1", "respawn_policy_key": policy, "owner_entity_key": owner_key, "item_instance_key": item_key, "amount": amount}
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_respawn_container_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(policy)},
              {sql_literal(owner_key)},
              {sql_literal(item_key) if item_key else 'NULL'},
              {int(amount)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"resolved": resolved}

    if kind == "npc_reaction_started":
        actor = str(payload_first(action.payload, "actor_npc_key", "actor_key", "target_key") or action.target_key or "")
        target_key = str(payload_first(action.payload, "target_key", "target_character_key") or "")
        reaction_kind = str(payload_first(action.payload, "reaction_kind", "reason") or "attention")
        resolved = {"resolver": "npc_reaction_started_step51_procedure_v1", "actor_npc_key": actor, "target_key": target_key, "reaction_kind": reaction_kind}
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_record_npc_reaction_started(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(actor)},
              {sql_literal(target_key) if target_key else 'NULL'},
              {sql_literal(reaction_kind)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"resolved": resolved}

    if kind == "npc_dialog_initiated":
        actor = str(payload_first(action.payload, "actor_npc_key", "actor_key", "target_key") or action.target_key or "")
        character_key = str(payload_first(action.payload, "target_character_key", "character_key") or "PC_HERO")
        info_key = str(payload_first(action.payload, "dialog_info_key", "info_key") or "")
        resolved = {"resolver": "npc_dialog_initiated_step51_procedure_v1", "actor_npc_key": actor, "target_character_key": character_key, "dialog_info_key": info_key}
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_record_npc_dialog_initiated(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(actor)},
              {sql_literal(character_key)},
              {sql_literal(info_key) if info_key else 'NULL'},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"resolved": resolved}


    if kind == "client_bootstrap_request":
        ack = make_bootstrap_ack(target, action, bootstrap_sample_limit)
        resolved = {
            "resolver": "client_bootstrap_request_server_bootstrap_ack_v1",
            "character_key": ack.get("character_key"),
            "actor_key": payload_first(action.payload, "actor_key") or action.target_key,
            "world": ack.get("world"),
            "server_endpoint": payload_first(action.payload, "server_endpoint"),
            "server_tick": scalar_int(payload_first(action.payload, "server_tick", "client_tick"), 0),
            "reason": payload_first(action.payload, "reason") or "client_bootstrap_request",
            "read_model_ready": ack.get("accepted"),
        }
        return None, {
            **ack,
            "applied_noop": True,
            "event_emitted": False,
            "noop_reason": "client_bootstrap_request_materialization_check_only_no_gameplay_mutation",
            "resolved": resolved,
        }

    if kind == "movement_proposal":
        if movement_config is not None and movement_config.enabled:
            validation = evaluate_movement_proposal(action, movement_config)
            if validation.get("accepted") is True:
                event_uuid, checkpoint_result = dispatch_accepted_movement_checkpoint(target, action, tick, idem, validation)
                resolved = checkpoint_result.get("resolved") if isinstance(checkpoint_result.get("resolved"), dict) else {}
                return event_uuid, {
                    **checkpoint_result,
                    "response_kind": "movement_authority_ack",
                    "accepted": True,
                    "movement_status": "accepted_checkpoint_persisted",
                    "validation": validation,
                    "event_emitted": True,
                    "authority_status": "validated_by_worker_gate",
                    "resolved": resolved,
                }
            resolved = {
                "resolver": "movement_proposal_authority_gate_v1",
                "actor_key": payload_first(action.payload, "actor_key") or action.target_key,
                "reason": payload_first(action.payload, "reason") or "movement_proposal",
                "validation": validation,
            }
            return None, {
                "response_kind": "movement_authority_ack",
                "accepted": False,
                "movement_status": "rejected_no_mutation",
                "applied_noop": True,
                "event_emitted": False,
                "noop_reason": "movement_proposal_rejected_by_authority_gate",
                "authority_status": "rejected_by_worker_gate",
                "validation": validation,
                "resolved": resolved,
            }

        # Movement proposals are client intents, not durable DB mutations.
        # Without the explicit Step58 authority flag, direct receiver tests still
        # keep the old Step56b behavior: visible applied/no-op evidence only.
        resolved = {
            "resolver": "movement_proposal_worker_noop_requires_server_authority_v1",
            "actor_key": payload_first(action.payload, "actor_key") or action.target_key,
            "from_tick": payload_first(action.payload, "from_tick"),
            "to_tick": payload_first(action.payload, "to_tick", "client_tick", "server_tick"),
            "reason": payload_first(action.payload, "reason") or "movement_proposal",
            "required_authority_path": "server_authority_gate_converts_accepted_proposal_to_character_checkpoint",
        }
        return None, {
            "applied_noop": True,
            "event_emitted": False,
            "noop_reason": "movement_proposal_is_intent_not_db_mutation",
            "authority_status": "not_validated_by_resolved_worker",
            "resolved": resolved,
        }

    if kind == "character_checkpoint":
        pos_x = float_payload(action, "pos_x")
        pos_y = float_payload(action, "pos_y")
        pos_z = float_payload(action, "pos_z")
        rotation_yaw = float_payload(action, "rotation_yaw", "yaw", default=0.0)
        waypoint = str(checkpoint_payload_value(action, "current_waypoint_key", "") or "")
        level = int_payload(action, "level", default=0)
        experience = int_payload(action, "experience", default=0)
        experience_next = int_payload(action, "experience_next", default=0)
        learning_points = int_payload(action, "learning_points", default=0)
        health_current = int_payload(action, "health_current", default=0)
        health_max = int_payload(action, "health_max", default=max(health_current, 0))
        mana_current = int_payload(action, "mana_current", default=0)
        mana_max = int_payload(action, "mana_max", default=max(mana_current, 0))
        strength = int_payload(action, "strength", default=0)
        dexterity = int_payload(action, "dexterity", default=0)
        guild = checkpoint_payload_value(action, "guild")
        true_guild = checkpoint_payload_value(action, "true_guild")
        permanent_attitude = checkpoint_payload_value(action, "permanent_attitude")
        temporary_attitude = checkpoint_payload_value(action, "temporary_attitude")
        resolved = {
            "resolver": "character_checkpoint_payload_v1",
            "character_uuid": action.character_uuid,
            "pos_x": pos_x,
            "pos_y": pos_y,
            "pos_z": pos_z,
            "rotation_yaw": rotation_yaw,
            "current_waypoint_key": waypoint,
            "level": level,
            "experience": experience,
            "experience_next": experience_next,
            "learning_points": learning_points,
            "reason": checkpoint_payload_value(action, "reason", "character_checkpoint"),
            "checkpoint_interval_ms": checkpoint_payload_value(action, "checkpoint_interval_ms"),
            "checkpoint_min_distance": checkpoint_payload_value(action, "checkpoint_min_distance"),
            "checkpoint_min_yaw_deg": checkpoint_payload_value(action, "checkpoint_min_yaw_deg"),
            "checkpoint_force_interval_ms": checkpoint_payload_value(action, "checkpoint_force_interval_ms"),
        }
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_checkpoint_character_state(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {int(tick)},
              {pos_x},
              {pos_y},
              {pos_z},
              {rotation_yaw},
              {sql_literal(waypoint)},
              {int(level)},
              {int(experience)},
              {int(experience_next)},
              {int(learning_points)},
              {int(health_current)},
              {int(health_max)},
              {int(mana_current)},
              {int(mana_max)},
              {int(strength)},
              {int(dexterity)},
              {sql_literal(guild) if guild not in (None, '') else 'NULL'},
              {sql_literal(true_guild) if true_guild not in (None, '') else 'NULL'},
              {sql_literal(permanent_attitude) if permanent_attitude not in (None, '') else 'NULL'},
              {sql_literal(temporary_attitude) if temporary_attitude not in (None, '') else 'NULL'},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"resolved": resolved}

    raise NotImplementedError(f"unsupported action_kind for resolved worker: {kind}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Resolve and dispatch receiver-enqueued OpenGothic MMO actions.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--worker-id", default="dev-resolved-worker")
    ap.add_argument("--run-key", default=None)
    ap.add_argument("--max-actions", type=int, default=10)
    ap.add_argument("--session-key", default=None, help="only claim pending actions whose idempotency_key starts with '<session-key>:'")
    ap.add_argument("--reset-matching-failed", action="store_true", help="reset failed/dead_letter/claimed rows for --session-key back to pending before running")
    ap.add_argument("--continue-on-error", action="store_true", help="keep claiming later actions after a failure; default stops so dependent equip/unequip is not applied after failed pickup")
    ap.add_argument("--retry-unresolved", action="store_true", help="mark resolver failures retryable instead of failed")
    ap.add_argument("--dry-run", action="store_true", help="claim nothing; only print matching pending/failed rows summary")
    ap.add_argument("--bootstrap-ack-jsonl", default="", help="optional server response JSONL for Step56 bootstrap_ack envelopes")
    ap.add_argument("--bootstrap-manifest-output", default="", help="optional JSON artifact path for the last Step56 bootstrap materialization manifest")
    ap.add_argument("--bootstrap-sample-limit", type=int, default=10, help="sample rows to include in bootstrap_ack manifest")
    ap.add_argument("--checkpoint-ack-jsonl", default="", help="optional server response JSONL for Step57 movement_checkpoint_ack envelopes")
    ap.add_argument("--checkpoint-ack-output", default="", help="optional JSON artifact path for the last Step57 movement checkpoint ACK")
    ap.add_argument("--enable-movement-authority-gate", action="store_true", help="Step58: validate movement_proposal and persist accepted proposals as checkpoints")
    ap.add_argument("--movement-authority-jsonl", default="", help="optional server response JSONL for Step58 movement_authority_ack envelopes")
    ap.add_argument("--movement-authority-output", default="", help="optional JSON artifact path for the last Step58 movement authority ACK/NACK")
    ap.add_argument("--pickup-ack-jsonl", default="", help="optional server response JSONL for Step59 pickup_ack envelopes")
    ap.add_argument("--pickup-ack-output", default="", help="optional JSON artifact path for the last Step59 pickup ACK")
    ap.add_argument("--equipment-ack-jsonl", default="", help="optional server response JSONL for Step60 equipment_ack envelopes")
    ap.add_argument("--equipment-ack-output", default="", help="optional JSON artifact path for the last Step60 equipment ACK")
    ap.add_argument("--movement-max-step-distance", type=float, default=2500.0)
    ap.add_argument("--movement-max-horizontal-speed", type=float, default=2500.0)
    ap.add_argument("--movement-max-vertical-speed", type=float, default=3500.0)
    ap.add_argument("--movement-max-vertical-delta", type=float, default=1600.0)
    ap.add_argument("--movement-max-fall-speed", type=float, default=12000.0)
    ap.add_argument("--movement-max-fall-delta", type=float, default=6000.0)
    ap.add_argument("--movement-min-delta-ms", type=int, default=1)
    ap.add_argument("--movement-max-delta-ms", type=int, default=5000)
    ap.add_argument("--movement-max-coord-abs", type=float, default=10000000.0)
    ap.add_argument("--movement-default-vertical-axis", default="y", choices=("x", "y", "z"))
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    movement_config = MovementAuthorityConfig(
        enabled=bool(args.enable_movement_authority_gate),
        max_step_distance=float(args.movement_max_step_distance),
        max_horizontal_speed=float(args.movement_max_horizontal_speed),
        max_vertical_speed=float(args.movement_max_vertical_speed),
        max_vertical_delta=float(args.movement_max_vertical_delta),
        max_fall_speed=float(args.movement_max_fall_speed),
        max_fall_delta=float(args.movement_max_fall_delta),
        min_delta_ms=int(args.movement_min_delta_ms),
        max_delta_ms=int(args.movement_max_delta_ms),
        max_coord_abs=float(args.movement_max_coord_abs),
        default_vertical_axis=str(args.movement_default_vertical_axis),
    )

    if args.dry_run:
        if args.session_key:
            print(run_mysql(target, f"""
                SELECT action_kind, status, COUNT(*)
                  FROM mmo_server_action_outbox
                 WHERE idempotency_key LIKE {sql_literal(args.session_key + ':%')}
                 GROUP BY action_kind, status
                 ORDER BY action_kind, status;
            """))
        else:
            print(run_mysql(target, """
                SELECT action_kind, status, COUNT(*)
                  FROM mmo_server_action_outbox
                 WHERE status IN ('pending','claimed','failed','dead_letter')
                 GROUP BY action_kind, status
                 ORDER BY action_kind, status;
            """))
        return 0

    if args.reset_matching_failed:
        if not args.session_key:
            raise SystemExit("--reset-matching-failed requires --session-key")
        reset = reset_failed_for_prefix(target, args.session_key)
        print(f"[RESET] session_key={args.session_key} rows={reset}")

    run_key = args.run_key or f"resolved-{args.worker_id}-{int(time.time())}"
    run_uuid = start_worker_run(target, args.worker_id, run_key, args.max_actions)
    failed = False

    for _ in range(max(0, args.max_actions)):
        action = claim_matching_prefix(target, args.worker_id, args.session_key) if args.session_key else claim(target, args.worker_id)
        if action is None:
            break
        record_result(target, run_uuid, action, "claimed", None, {"claimed": True})
        try:
            event_uuid, result = dispatch(target, action, max(1, args.bootstrap_sample_limit), movement_config)
            status = mark_applied(target, action.action_uuid, event_uuid, result)
            record_result(target, run_uuid, action, status, event_uuid, result)
            if action.kind == "client_bootstrap_request" and result.get("response_kind") == "bootstrap_ack":
                response = {**result, "worker_run_uuid": run_uuid, "outbox_status": status}
                if args.bootstrap_ack_jsonl:
                    append_jsonl(Path(args.bootstrap_ack_jsonl), response)
                if args.bootstrap_manifest_output:
                    manifest_path = Path(args.bootstrap_manifest_output)
                    manifest_path.parent.mkdir(parents=True, exist_ok=True)
                    manifest_path.write_text(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            if action.kind == "character_checkpoint":
                response = {**make_checkpoint_ack(action, event_uuid, result, status), "worker_run_uuid": run_uuid}
                if args.checkpoint_ack_jsonl:
                    append_jsonl(Path(args.checkpoint_ack_jsonl), response)
                if args.checkpoint_ack_output:
                    checkpoint_path = Path(args.checkpoint_ack_output)
                    checkpoint_path.parent.mkdir(parents=True, exist_ok=True)
                    checkpoint_path.write_text(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            if action.kind == "movement_proposal" and result.get("response_kind") == "movement_authority_ack":
                response = {**make_movement_authority_ack(action, event_uuid, result, status), "worker_run_uuid": run_uuid}
                if args.movement_authority_jsonl:
                    append_jsonl(Path(args.movement_authority_jsonl), response)
                if args.movement_authority_output:
                    movement_path = Path(args.movement_authority_output)
                    movement_path.parent.mkdir(parents=True, exist_ok=True)
                    movement_path.write_text(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            if action.kind == "pickup_world_item":
                response = {**make_pickup_ack(action, event_uuid, result, status), "worker_run_uuid": run_uuid}
                if args.pickup_ack_jsonl:
                    append_jsonl(Path(args.pickup_ack_jsonl), response)
                if args.pickup_ack_output:
                    pickup_path = Path(args.pickup_ack_output)
                    pickup_path.parent.mkdir(parents=True, exist_ok=True)
                    pickup_path.write_text(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            if action.kind in {"equip_character_item", "unequip_character_item"}:
                response = {**make_equipment_ack(action, event_uuid, result, status), "worker_run_uuid": run_uuid}
                if args.equipment_ack_jsonl:
                    append_jsonl(Path(args.equipment_ack_jsonl), response)
                if args.equipment_ack_output:
                    equipment_path = Path(args.equipment_ack_output)
                    equipment_path.parent.mkdir(parents=True, exist_ok=True)
                    equipment_path.write_text(json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"[APPLIED] {action.kind} action={action.action_uuid} event={event_uuid} result={json.dumps(result, ensure_ascii=False, sort_keys=True)}")
        except Exception as exc:
            failed = True
            retryable = bool(args.retry_unresolved and isinstance(exc, ResolveError))
            status = mark_failed(target, action.action_uuid, type(exc).__name__, str(exc), retryable)
            record_result(target, run_uuid, action, status, None, {"exception": type(exc).__name__}, type(exc).__name__, str(exc))
            print(f"[FAILED] {action.kind} action={action.action_uuid} status={status} error={exc}", file=sys.stderr)
            if not args.continue_on_error:
                break

    status, applied = finish_worker_run(target, run_uuid, failed)
    print(f"[RUN] {run_uuid} status={status} applied={applied}")
    return 0 if status == "finished" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

