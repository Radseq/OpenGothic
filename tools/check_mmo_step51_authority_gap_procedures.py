#!/usr/bin/env python3
"""Check Step51 authority-gap procedure/projection installation."""
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
EVENT_TYPES = [
    "world_trigger_event",
    "world_mover_state_changed",
    "npc_weapon_readied",
    "npc_weapon_holstered",
    "world_time_changed",
    "character_resource_delta",
    "character_learning_points_spent",
    "character_teleported_or_world_changed",
    "world_item_respawned",
    "container_item_respawned",
    "npc_reaction_started",
    "npc_dialog_initiated",
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
    return Target(p.hostname or "127.0.0.1", p.port or 3306, unquote(p.username or ""), unquote(p.password or ""), db)


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
    return proc.stdout.strip()


def lit(value: Any) -> str:
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def object_exists(target: Target, kind: str, name: str) -> bool:
    if kind == "table":
        out = run_mysql(target, f"SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name={lit(name)};")
    else:
        out = run_mysql(target, f"SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=DATABASE() AND routine_name={lit(name)};")
    return (out or "0").splitlines()[-1].strip() == "1"


def count_rows(target: Target, table: str) -> int | None:
    if not object_exists(target, "table", table):
        return None
    out = run_mysql(target, f"SELECT COUNT(*) FROM `{table}`;")
    return int((out or "0").splitlines()[-1])


def event_counts(target: Target) -> dict[str, int]:
    if not object_exists(target, "table", "world_event_journal"):
        return {}
    in_list = ",".join(lit(x) for x in EVENT_TYPES)
    out = run_mysql(target, f"SELECT event_type, COUNT(*) FROM world_event_journal WHERE event_type IN ({in_list}) GROUP BY event_type ORDER BY event_type;")
    result = {name: 0 for name in EVENT_TYPES}
    for line in out.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) >= 2:
            result[parts[0]] = int(parts[1])
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step51 authority-gap procedure/projection readiness.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--output")
    ap.add_argument("--require-zero-gaps", action="store_true", help="Fail if any Step51 routine/table is missing; default also fails on missing required objects.")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    tables = {name: object_exists(target, "table", name) for name in REQUIRED_TABLES}
    routines = {name: object_exists(target, "routine", name) for name in REQUIRED_ROUTINES}
    row_counts = {name: count_rows(target, name) for name in REQUIRED_TABLES}
    missing_tables = [k for k, ok in tables.items() if not ok]
    missing_routines = [k for k, ok in routines.items() if not ok]
    status = "passed" if not missing_tables and not missing_routines else "failed"
    report = {
        "step": 51,
        "status": status,
        "database": target.database,
        "tables": tables,
        "routines": routines,
        "row_counts": row_counts,
        "event_counts": event_counts(target),
        "missing_tables": missing_tables,
        "missing_routines": missing_routines,
    }
    print("Step51 authority-gap procedure check")
    print("tables:")
    for name, ok in tables.items():
        print(f"  {name}: {'ok' if ok else 'missing'} rows={row_counts.get(name)}")
    print("routines:")
    for name, ok in routines.items():
        print(f"  {name}: {'ok' if ok else 'missing'}")
    print(f"status={status}")
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
