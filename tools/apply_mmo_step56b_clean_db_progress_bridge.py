#!/usr/bin/env python3
"""Install Step56b clean-DB progress/dialog/quest bridge procedures.

A clean DB rebuilt from the Step54/55 flow can have the live receiver bridge and
Step51 procedures but still miss the older progress projection procedures that
the resolved worker calls for set_script_int, update_quest and set_known_dialog.
This installer applies only those minimal additive procedures.
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
DEFAULT_SQL = ROOT / "server" / "sql" / "step56b_clean_db_progress_bridge.sql"

REQUIRED_ROUTINES = [
    "mmo_set_character_script_int",
    "mmo_update_character_quest",
    "mmo_set_character_known_dialog",
]
REQUIRED_TABLES = [
    "server_sessions",
    "characters",
    "character_script_state",
    "character_quests",
    "character_known_dialogs",
    "world_event_journal",
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
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-h",
        target.host,
        "-P",
        str(target.port),
        "-u",
        target.user,
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


def count_object(target: Target, kind: str, name: str) -> int:
    if kind == "table":
        sql = f"""
            SELECT COUNT(*)
              FROM information_schema.tables
             WHERE table_schema=DATABASE()
               AND table_name={sql_literal(name)}
               AND table_type='BASE TABLE';
        """
    elif kind == "routine":
        sql = f"""
            SELECT COUNT(*)
              FROM information_schema.routines
             WHERE routine_schema=DATABASE()
               AND routine_name={sql_literal(name)};
        """
    else:
        raise ValueError(kind)
    out = run_mysql(target, sql)
    return int((out or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, Any]:
    return {
        "database": target.database,
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
    }


def missing(result: dict[str, Any]) -> dict[str, list[str]]:
    return {
        "tables": [name for name, ok in result["tables"].items() if not ok],
        "routines": [name for name, ok in result["routines"].items() if not ok],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Install Step56b clean-DB progress/dialog/quest bridge procedures.")
    ap.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--sql", default=str(DEFAULT_SQL), help="SQL patch path")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--output", help="Optional JSON result artifact")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    sql_path = Path(args.sql)
    if not sql_path.exists():
        print(f"ERROR: SQL file does not exist: {sql_path}", file=sys.stderr)
        return 1

    if args.dry_run:
        result: dict[str, Any] = {
            "status": "dry_run",
            "database": target.database,
            "sql": str(sql_path),
            "required_tables": REQUIRED_TABLES,
            "required_routines": REQUIRED_ROUTINES,
        }
    else:
        print(f"[APPLY] {sql_path} -> {target.database}")
        run_mysql(target, sql_path.read_text(encoding="utf-8"))
        result = {"status": "applied", "sql": str(sql_path), **inspect(target)}
        miss = missing(result)
        if any(miss.values()):
            result["status"] = "failed"
            result["missing"] = miss

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")

    print("status=" + str(result["status"]))
    return 0 if result["status"] in {"applied", "dry_run"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
