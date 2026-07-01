#!/usr/bin/env python3
"""Install Step104 DB checkpoint full script-state export bridge."""
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

ROOT = Path(__file__).resolve().parents[1]
SQL_PATH = ROOT / "server" / "sql" / "step104_db_checkpoint_script_state_full_export.sql"
FOUNDATION_SQL_PATH = ROOT / "server" / "sql" / "step108_db_checkpoint_world_clock_foundation.sql"

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


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
    return Target(
        host=parsed.hostname or "127.0.0.1",
        port=parsed.port or 3306,
        user=unquote(parsed.username or ""),
        password=unquote(parsed.password or ""),
        database=database,
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


def make_create_function_binlog_safe(sql: str) -> tuple[str, int]:
    """Ensure read-only stored functions can be created with MySQL binary logging enabled."""
    lines = sql.splitlines(keepends=True)
    out: list[str] = []
    header: list[str] = []
    in_function = False
    patched = 0

    create_function_re = re.compile(r"\bCREATE\b(?:\s+DEFINER\s*=\s*[^\s]+)?\s+FUNCTION\b", re.IGNORECASE)
    begin_re = re.compile(r"^\s*BEGIN\b", re.IGNORECASE)
    deterministic_re = re.compile(r"(?<!NOT\s)\bDETERMINISTIC\b", re.IGNORECASE)
    safe_data_access_re = re.compile(r"\b(?:READS\s+SQL\s+DATA|NO\s+SQL)\b", re.IGNORECASE)
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
                if not modifies_sql_re.search(header_text):
                    if not deterministic_re.search(header_text):
                        header.append("    DETERMINISTIC\n")
                        patched += 1
                    if contains_sql_re.search(header_text):
                        header_text = "".join(header)
                        header = contains_sql_re.sub("READS SQL DATA", header_text, count=1).splitlines(keepends=True)
                        patched += 1
                    elif not safe_data_access_re.search(header_text):
                        header.append("    READS SQL DATA\n")
                        patched += 1
                out.extend(header)
                out.append(line)
                in_function = False
                header = []
            else:
                header.append(line)
            continue

        out.append(line)

    if in_function:
        out.extend(header)

    return "".join(out), patched


def run_mysql_file(target: Target, sql_path: Path) -> int:
    sql_text, patched_functions = make_create_function_binlog_safe(sql_path.read_text(encoding="utf-8"))
    proc = subprocess.run(
        mysql_cmd(target),
        input=sql_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return patched_functions


def main() -> int:
    parser = argparse.ArgumentParser(description="Install Step104 DB checkpoint full script-state export bridge.")
    parser.add_argument("--url", required=True)
    parser.add_argument("--sql", default=str(SQL_PATH))
    parser.add_argument("--foundation-sql", default=str(FOUNDATION_SQL_PATH))
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    result: dict[str, object] = {
        "step": "104_db_checkpoint_script_state_full_export",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "sql": args.sql,
        "foundation_sql": args.foundation_sql,
        "status": "running",
    }

    try:
        target = parse_mysql_url(args.url)
        result["database"] = target.database
        sql_path = Path(args.sql)
        if not sql_path.exists():
            result["status"] = "failed"
            result["error"] = f"SQL file does not exist: {sql_path}"
        else:
            foundation_sql_path = Path(args.foundation_sql)
            if not foundation_sql_path.exists():
                result["status"] = "failed"
                result["error"] = f"Foundation SQL file does not exist: {foundation_sql_path}"
            else:
                result["patched_create_function_characteristics_for_binlog_foundation_before"] = run_mysql_file(
                    target,
                    foundation_sql_path,
                )
                result["patched_create_function_characteristics_for_binlog"] = run_mysql_file(target, sql_path)
                result["patched_create_function_characteristics_for_binlog_foundation_after"] = run_mysql_file(
                    target,
                    foundation_sql_path,
                )
                result["status"] = "ok"
    except Exception as exc:  # noqa: BLE001 - CLI installer should report exact failure.
        result["status"] = "failed"
        result["error"] = str(exc)

    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("status=" + str(result["status"]))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
