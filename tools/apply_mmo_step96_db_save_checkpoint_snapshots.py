#!/usr/bin/env python3
"""Install Step96 DB-native save checkpoint snapshot tables/procedures."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
SQL_PATH = ROOT / "server" / "sql" / "step96_db_save_checkpoint_snapshots.sql"

REQUIRED_TABLES = (
    "mmo_save_checkpoint_character_snapshot",
    "mmo_save_checkpoint_inventory_snapshot",
    "mmo_save_checkpoint_equipment_snapshot",
    "mmo_save_checkpoint_quest_snapshot",
    "mmo_save_checkpoint_known_dialog_snapshot",
    "mmo_save_checkpoint_script_state_snapshot",
    "mmo_save_checkpoint_world_entity_snapshot",
    "mmo_save_checkpoint_world_inventory_snapshot",
    "mmo_save_checkpoint_mover_snapshot",
)
REQUIRED_ROUTINES = (
    "mmo_materialize_save_checkpoint_snapshot_v1",
    "mmo_create_db_save_checkpoint_v1",
)
REQUIRED_VIEWS = ("v_mmo_save_checkpoint_snapshot_domain_counts",)


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
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
           "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def count_object(target: Target, kind: str, name: str) -> int:
    if kind == "table":
        sql = f"""SELECT COUNT(*) FROM information_schema.tables
                  WHERE table_schema=DATABASE() AND table_name={sql_literal(name)} AND table_type='BASE TABLE';"""
    elif kind == "routine":
        sql = f"""SELECT COUNT(*) FROM information_schema.routines
                  WHERE routine_schema=DATABASE() AND routine_name={sql_literal(name)};"""
    elif kind == "view":
        sql = f"""SELECT COUNT(*) FROM information_schema.views
                  WHERE table_schema=DATABASE() AND table_name={sql_literal(name)};"""
    else:
        raise ValueError(kind)
    out = run_mysql(target, sql)
    return int((out or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, object]:
    return {
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
        "views": {name: count_object(target, "view", name) == 1 for name in REQUIRED_VIEWS},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Install Step96 DB-native save checkpoint snapshots.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--sql", default=str(SQL_PATH))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    sql_path = Path(args.sql)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path

    result: dict[str, object] = {
        "step": "96_db_save_checkpoint_snapshots",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "sql": str(sql_path),
        "status": "running",
    }

    try:
        if not sql_path.exists():
            result["status"] = "failed_missing_sql"
            result["error"] = str(sql_path)
        elif args.dry_run:
            result["status"] = "dry_run"
            result["required_tables"] = REQUIRED_TABLES
            result["required_routines"] = REQUIRED_ROUTINES
            result["required_views"] = REQUIRED_VIEWS
        else:
            run_mysql(target, sql_path.read_text(encoding="utf-8"))
            result.update(inspect(target))
            missing = (
                [name for name, ok in result["tables"].items() if not ok] +  # type: ignore[index]
                [name for name, ok in result["routines"].items() if not ok] +  # type: ignore[index]
                [name for name, ok in result["views"].items() if not ok]  # type: ignore[index]
            )
            if missing:
                result["status"] = "failed"
                result["missing"] = missing
            else:
                result["status"] = "applied"
    except Exception as exc:  # noqa: BLE001
        result["status"] = "failed"
        result["error"] = str(exc)
        print(f"ERROR: {exc}", file=sys.stderr)

    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    print("status=" + str(result["status"]))
    return 0 if result["status"] in {"applied", "dry_run"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
