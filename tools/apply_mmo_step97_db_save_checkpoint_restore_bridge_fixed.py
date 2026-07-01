#!/usr/bin/env python3
"""Install Step97 DB-save-checkpoint restore/export bridge."""
from __future__ import annotations

import argparse
import json
import re
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
SQL_PATH = ROOT / "server" / "sql" / "step97_db_save_checkpoint_restore_bridge.sql"

REQUIRED_TABLES = ("mmo_save_checkpoint_world_clock_snapshot",)
REQUIRED_ROUTINES = (
    "mmo_materialize_save_checkpoint_world_clock_snapshot_v1",
    "mmo_create_db_save_checkpoint_v1",
    "mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1",
)
REQUIRED_VIEWS = (
    "v_mmo_save_checkpoint_snapshot_domain_counts",
    "v_mmo_latest_save_checkpoint_restore_readiness",
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


def make_create_function_binlog_safe(sql: str) -> tuple[str, int]:
    """Add READS SQL DATA to CREATE FUNCTION headers when MySQL binary logging requires it.

    MySQL with log_bin enabled rejects stored functions created by normal users unless
    the function is declared DETERMINISTIC, NO SQL, or READS SQL DATA. Step97 uses a
    read-only JSON-building function, so READS SQL DATA is the correct routine
    characteristic and avoids requiring SUPER/log_bin_trust_function_creators.
    """
    lines = sql.splitlines(keepends=True)
    out: list[str] = []
    header: list[str] = []
    in_function = False
    patched = 0

    create_function_re = re.compile(r"\bCREATE\b(?:\s+DEFINER\s*=\s*[^\s]+)?\s+FUNCTION\b", re.IGNORECASE)
    begin_re = re.compile(r"^\s*BEGIN\b", re.IGNORECASE)
    safe_data_access_re = re.compile(r"\b(?:READS\s+SQL\s+DATA|NO\s+SQL)\b", re.IGNORECASE)
    deterministic_re = re.compile(r"(?<!NOT\s)\bDETERMINISTIC\b", re.IGNORECASE)
    contains_sql_re = re.compile(r"\bCONTAINS\s+SQL\b", re.IGNORECASE)
    modifies_sql_re = re.compile(r"\bMODIFIES\s+SQL\s+DATA\b", re.IGNORECASE)

    for line in lines:
        if not in_function and create_function_re.search(line):
            in_function = True
            header = [line]
            continue

        if in_function:
            if begin_re.search(line):
                header_text = "".join(header)
                if modifies_sql_re.search(header_text):
                    # Do not silently lie about write-capable functions. Let MySQL validate/fail.
                    out.extend(header)
                    out.append(line)
                else:
                    if contains_sql_re.search(header_text):
                        header_text = contains_sql_re.sub("READS SQL DATA", header_text, count=1)
                        header = header_text.splitlines(keepends=True)
                        patched += 1
                    elif not safe_data_access_re.search(header_text) and not deterministic_re.search(header_text):
                        header.append("READS SQL DATA\n")
                        patched += 1
                    out.extend(header)
                    out.append(line)
                in_function = False
                header = []
            else:
                header.append(line)
            continue

        out.append(line)

    # If the SQL was malformed and a function header never reached BEGIN, preserve it unchanged.
    if in_function:
        out.extend(header)

    return "".join(out), patched


def sql_literal(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def count_object(target: Target, kind: str, name: str) -> int:
    if kind == "table":
        sql = f"""SELECT COUNT(*) FROM information_schema.tables
                  WHERE table_schema=DATABASE() AND table_name={sql_literal(name)} AND table_type='BASE TABLE';"""
    elif kind in {"routine", "function"}:
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
    ap = argparse.ArgumentParser(description="Install Step97 DB-save-checkpoint restore/export bridge.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--sql", default=str(SQL_PATH))
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    sql_path = Path(args.sql)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path

    result: dict[str, object] = {
        "step": "97_db_save_checkpoint_restore_bridge",
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
            sql_text, patched_functions = make_create_function_binlog_safe(sql_path.read_text(encoding="utf-8"))
            result["patched_create_functions_for_binlog"] = patched_functions
            run_mysql(target, sql_text)
            result.update(inspect(target))
            missing = []
            for section in ("tables", "routines", "views"):
                missing.extend(f"{section}:{name}" for name, ok in result[section].items() if not ok)  # type: ignore[index]
            if missing:
                result["status"] = "failed"
                result["missing"] = missing
            else:
                result["status"] = "applied"
    except Exception as exc:  # noqa: BLE001 - command-line diagnostic surface
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
