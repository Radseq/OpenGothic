#!/usr/bin/env python3
"""Normalize a local OpenGothic MMO MySQL dev DB to utf8mb4_0900_ai_ci.

This is used after destructive clean imports from SQLite because additive SQL
surfaces from older steps may have been created under a different connection
collation. It does not drop data. It converts base tables only; views inherit
collation from their source expressions and are not altered directly.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

TARGET_CHARSET = "utf8mb4"
TARGET_COLLATION = "utf8mb4_0900_ai_ci"


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


def mysql_cmd(target: Target, *, skip_column_names: bool = True) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        f"--default-character-set={TARGET_CHARSET}",
        f"--init-command=SET NAMES {TARGET_CHARSET} COLLATE {TARGET_COLLATION}",
        "--batch",
        "--raw",
    ]
    if skip_column_names:
        cmd.append("--skip-column-names")
    cmd += ["-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str, *, skip_column_names: bool = True) -> str:
    proc = subprocess.run(mysql_cmd(target, skip_column_names=skip_column_names), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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


def qident(name: str) -> str:
    return "`" + name.replace("`", "``") + "`"


def rows(text: str) -> list[list[str]]:
    return [line.split("\t") for line in text.splitlines() if line.strip()]


def candidate_tables(target: Target) -> list[str]:
    raw = run_mysql(
        target,
        f"""
        SELECT DISTINCT c.TABLE_NAME
          FROM information_schema.COLUMNS c
          JOIN information_schema.TABLES t
            ON t.TABLE_SCHEMA = c.TABLE_SCHEMA
           AND t.TABLE_NAME = c.TABLE_NAME
         WHERE c.TABLE_SCHEMA = DATABASE()
           AND t.TABLE_TYPE = 'BASE TABLE'
           AND c.COLLATION_NAME IS NOT NULL
           AND c.COLLATION_NAME <> {sql_literal(TARGET_COLLATION)}
         ORDER BY c.TABLE_NAME;
        """,
    )
    return [parts[0] for parts in rows(raw) if parts]


def collation_summary(target: Target) -> dict[str, int]:
    raw = run_mysql(
        target,
        """
        SELECT COALESCE(COLLATION_NAME,''), COUNT(*)
          FROM information_schema.COLUMNS
         WHERE TABLE_SCHEMA = DATABASE()
           AND COLLATION_NAME IS NOT NULL
         GROUP BY COLLATION_NAME
         ORDER BY COLLATION_NAME;
        """,
    )
    out: dict[str, int] = {}
    for parts in rows(raw):
        if len(parts) >= 2:
            out[parts[0]] = int(parts[1])
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Normalize OpenGothic MMO MySQL table collations to utf8mb4_0900_ai_ci.")
    ap.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--output", help="Optional JSON result artifact")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    before = collation_summary(target)
    tables = candidate_tables(target)
    converted: list[str] = []
    errors: list[dict[str, str]] = []

    if not args.dry_run:
        run_mysql(target, f"ALTER DATABASE {qident(target.database)} CHARACTER SET {TARGET_CHARSET} COLLATE {TARGET_COLLATION};")
        for table in tables:
            try:
                run_mysql(target, f"ALTER TABLE {qident(table)} CONVERT TO CHARACTER SET {TARGET_CHARSET} COLLATE {TARGET_COLLATION};")
                converted.append(table)
                print(f"converted={table}")
            except Exception as exc:  # keep going so the report shows every incompatible table
                errors.append({"table": table, "error": str(exc)})
                print(f"ERROR: failed to convert {table}: {exc}", file=sys.stderr)

    after = collation_summary(target) if not args.dry_run else before
    remaining = candidate_tables(target) if not args.dry_run else tables
    status = "dry_run" if args.dry_run else ("passed" if not errors and not remaining else "failed")
    result: dict[str, Any] = {
        "tool": "normalize_mmo_mysql_collation.py",
        "status": status,
        "database": target.database,
        "target_charset": TARGET_CHARSET,
        "target_collation": TARGET_COLLATION,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "candidate_tables": tables,
        "converted_tables": converted,
        "remaining_tables": remaining,
        "errors": errors,
        "collations_before": before,
        "collations_after": after,
    }

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")

    print("status=" + status)
    return 0 if status in {"passed", "dry_run"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
