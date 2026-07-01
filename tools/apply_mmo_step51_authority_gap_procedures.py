#!/usr/bin/env python3
"""Install Step51 MySQL authority-gap projections and stored procedures.

This is an additive DB patch for the dev/production-shaped MySQL schema. It
creates canonical journal-backed procedures for domains that Step49 reported as
missing/capture-only: trigger/mover, NPC weapon state, world time, character
resource delta, learning point spend, teleport/world transition, respawn and
future NPC reactive/dialog initiation events.
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

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SQL = ROOT / "server" / "sql" / "step51_authority_gap_procedures.sql"
REQUIRED_ROUTINES = [
    "mmo_record_trigger_event",
    "mmo_record_mover_state",
    "mmo_record_npc_weapon_state",
    "mmo_record_world_time_changed",
    "mmo_record_character_resource_delta",
    "mmo_spend_learning_points",
    "mmo_change_world_or_teleport_character",
    "mmo_respawn_world_item",
    "mmo_respawn_container_item",
    "mmo_record_npc_reaction_started",
    "mmo_record_npc_dialog_initiated",
]
REQUIRED_TABLES = [
    "mmo_world_trigger_events",
    "mmo_world_mover_state_current",
    "mmo_world_mover_state_history",
    "mmo_npc_weapon_state_current",
    "mmo_npc_weapon_state_history",
    "mmo_world_clock_state_current",
    "mmo_world_clock_state_history",
    "mmo_character_resource_state_current",
    "mmo_character_resource_state_history",
    "mmo_character_training_state_current",
    "mmo_character_training_history",
    "mmo_character_teleport_history",
    "mmo_world_respawn_history",
    "mmo_npc_reaction_history",
]


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    db = (p.path or "/").lstrip("/")
    if not db:
        raise ValueError("database is missing in mysql URL")
    return Target(
        host=p.hostname or "127.0.0.1",
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
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def count_object(target: Target, object_type: str, name: str) -> int:
    table = "tables" if object_type == "table" else "routines"
    col = "table_name" if object_type == "table" else "routine_name"
    out = run_mysql(target, f"""
        SELECT COUNT(*)
          FROM information_schema.{table}
         WHERE {'table_schema' if object_type == 'table' else 'routine_schema'}=DATABASE()
           AND {col}={sql_literal(name)};
    """)
    return int((out or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, Any]:
    return {
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Install Step51 MMO authority-gap MySQL procedures.")
    ap.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--sql", default=str(DEFAULT_SQL), help="SQL patch path")
    ap.add_argument("--dry-run", action="store_true", help="Only print planned SQL path and required objects")
    ap.add_argument("--output", help="Optional JSON result path")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    sql_path = Path(args.sql)
    if not sql_path.exists():
        print(f"ERROR: SQL file does not exist: {sql_path}", file=sys.stderr)
        return 1

    if args.dry_run:
        result = {"status": "dry_run", "database": target.database, "sql": str(sql_path), "required_tables": REQUIRED_TABLES, "required_routines": REQUIRED_ROUTINES}
    else:
        print(f"[APPLY] {sql_path} -> {target.database}")
        run_mysql(target, sql_path.read_text(encoding="utf-8"))
        result = {"status": "applied", "database": target.database, "sql": str(sql_path), **inspect(target)}
        missing_tables = [k for k, ok in result["tables"].items() if not ok]
        missing_routines = [k for k, ok in result["routines"].items() if not ok]
        if missing_tables or missing_routines:
            result["status"] = "failed"
            result["missing_tables"] = missing_tables
            result["missing_routines"] = missing_routines

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    print("status=" + str(result["status"]))
    if result.get("status") == "failed":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
