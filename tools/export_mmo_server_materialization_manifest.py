#!/usr/bin/env python3
"""Export a compact server materialization manifest from Step53 read-model tables.

This outputs a JSON file for humans/CI. It does not introduce JSON into the DB.
The future server should load the typed tables into memory, not query SQL views or
parse JSON payload columns on the hot path.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

TABLES = [
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


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    database = (parsed.path or "/").lstrip("/")
    if not database:
        raise ValueError("database is missing in mysql URL")
    return Target(parsed.hostname or "127.0.0.1", parsed.port or 3306, unquote(parsed.username or ""), unquote(parsed.password or ""), database)


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
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
    return proc.stdout


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''") + "'"


def qident(name: str) -> str:
    return "`" + name.replace("`", "``") + "`"


def scalar_int(target: Target, sql: str) -> int:
    out = run_mysql(target, sql).strip()
    if not out:
        return 0
    return int(out.splitlines()[-1].strip() or "0")


def table_exists(target: Target, table: str) -> bool:
    return scalar_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM information_schema.tables
         WHERE table_schema=DATABASE() AND table_name={sql_literal(table)} AND table_type='BASE TABLE';
        """,
    ) == 1


def rows_as_dicts(target: Target, table: str, columns: list[str], limit: int) -> list[dict[str, Any]]:
    if not table_exists(target, table):
        return []
    select = ", ".join(qident(c) for c in columns)
    out = run_mysql(target, f"SELECT {select} FROM {qident(table)} LIMIT {int(limit)};")
    rows: list[dict[str, Any]] = []
    for line in out.splitlines():
        parts = line.split("\t") if line else []
        rows.append({columns[i]: (parts[i] if i < len(parts) and parts[i] != "NULL" else None) for i in range(len(columns))})
    return rows


def count(target: Target, table: str) -> int:
    if not table_exists(target, table):
        return -1
    return scalar_int(target, f"SELECT COUNT(*) FROM {qident(table)};")


def export_manifest(target: Target, sample_limit: int) -> dict[str, Any]:
    counts = {table: count(target, table) for table in TABLES}
    worlds: list[str] = []
    if table_exists(target, "mmo_server_world_entity_read_model"):
        out = run_mysql(target, "SELECT DISTINCT world_name FROM mmo_server_world_entity_read_model ORDER BY world_name LIMIT 200;")
        worlds = [line.strip() for line in out.splitlines() if line.strip()]
    entity_kind_counts: list[dict[str, Any]] = []
    if table_exists(target, "mmo_server_world_entity_read_model"):
        out = run_mysql(
            target,
            """
            SELECT world_name, entity_kind, active, dead, COUNT(*)
              FROM mmo_server_world_entity_read_model
             GROUP BY world_name, entity_kind, active, dead
             ORDER BY world_name, entity_kind, active DESC, dead;
            """,
        )
        for line in out.splitlines():
            parts = line.split("\t")
            if len(parts) >= 5:
                entity_kind_counts.append({"world_name": parts[0], "entity_kind": parts[1], "active": parts[2], "dead": parts[3], "count": int(parts[4])})
    return {
        "step": 53,
        "database": target.database,
        "manifest_kind": "server_materialization_read_model_v1",
        "important": {
            "this_json_is_file_artifact_not_db_hot_path": True,
            "server_should_load_typed_tables_not_sql_views": True,
            "current_db_still_not_final_production_mmo_schema": True,
        },
        "counts": counts,
        "worlds": worlds,
        "entity_kind_counts": entity_kind_counts,
        "samples": {
            "characters": rows_as_dicts(target, "mmo_server_character_read_model", ["realm_key", "character_key", "display_name", "world_name", "pos_x", "pos_y", "pos_z", "level_value", "experience_value", "learning_points"], sample_limit),
            "world_entities": rows_as_dicts(target, "mmo_server_world_entity_read_model", ["world_name", "entity_key", "entity_kind", "display_name", "active", "dead", "pos_x", "pos_y", "pos_z", "current_waypoint_name"], sample_limit),
            "inventory": rows_as_dicts(target, "mmo_server_character_inventory_read_model", ["character_key", "item_instance_key", "item_template_key", "display_name", "amount", "equipped", "slot_key"], sample_limit),
            "script_ints": rows_as_dicts(target, "mmo_server_script_int_read_model", ["scope_key", "owner_key", "symbol_name", "int_value", "category_key"], sample_limit),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Export Step53 server materialization manifest from typed read model.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--output", required=True)
    parser.add_argument("--sample-limit", type=int, default=25)
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    manifest = export_manifest(target, max(1, args.sample_limit))
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={out}")
    print("Step53 server materialization manifest")
    print(f"database={target.database}")
    for table, c in manifest["counts"].items():
        print(f"  {table}: {c}")
    print("status=exported")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
