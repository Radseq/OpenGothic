#!/usr/bin/env python3
"""Install Step95 DB-backed save-slot catalog/continue bridge."""
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
SQL_PATH = ROOT / "server" / "sql" / "step95_save_slot_catalog_db_continue_bridge.sql"
REQUIRED_TABLES = ("mmo_save_checkpoint_manifests",)
REQUIRED_VIEWS = ("v_mmo_latest_save_checkpoint_manifests",)
REQUIRED_ROUTINES = ("mmo_create_save_checkpoint_manifest",)
REQUIRED_COLUMNS = (
    "save_slot_key",
    "native_save_path",
    "display_name",
    "client_world_name",
    "native_save_present",
)


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
    cmd = [
        exe,
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
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


def count_table(target: Target, name: str) -> int:
    return int((run_mysql(target, f"SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_type='BASE TABLE' AND table_name={sql_literal(name)};") or "0").splitlines()[-1])


def count_view(target: Target, name: str) -> int:
    return int((run_mysql(target, f"SELECT COUNT(*) FROM information_schema.views WHERE table_schema=DATABASE() AND table_name={sql_literal(name)};") or "0").splitlines()[-1])


def count_routine(target: Target, name: str) -> int:
    return int((run_mysql(target, f"SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=DATABASE() AND routine_name={sql_literal(name)};") or "0").splitlines()[-1])


def count_column(target: Target, table: str, column: str) -> int:
    return int((run_mysql(target, f"SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name={sql_literal(table)} AND column_name={sql_literal(column)};") or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, object]:
    return {
        "tables": {name: count_table(target, name) == 1 for name in REQUIRED_TABLES},
        "views": {name: count_view(target, name) == 1 for name in REQUIRED_VIEWS},
        "routines": {name: count_routine(target, name) == 1 for name in REQUIRED_ROUTINES},
        "columns": {name: count_column(target, "mmo_save_checkpoint_manifests", name) == 1 for name in REQUIRED_COLUMNS},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Install Step95 DB-backed save-slot catalog/continue bridge.")
    ap.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--sql", default=str(SQL_PATH))
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    sql_path = Path(args.sql)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path

    result: dict[str, object] = {
        "step": "95_save_slot_catalog_db_continue_bridge",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "sql": str(sql_path),
        "status": "running",
    }
    try:
        if not sql_path.exists():
            result["status"] = "failed_missing_sql"
            result["error"] = str(sql_path)
        else:
            run_mysql(target, sql_path.read_text(encoding="utf-8"))
            result.update(inspect(target))
            missing = []
            for section in ("tables", "views", "routines", "columns"):
                missing.extend(f"{section}:{name}" for name, ok in result[section].items() if not ok)  # type: ignore[index]
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
    return 0 if result["status"] == "applied" else 1


if __name__ == "__main__":
    raise SystemExit(main())
