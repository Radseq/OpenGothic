#!/usr/bin/env python3
"""Validate a MySQL bootstrap import created from OpenGothic runtime SQLite."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Sequence
from urllib.parse import urlparse, unquote

@dataclass(frozen=True)
class MySqlTarget:
    host: str
    port: int
    user: str
    password: str
    database: str

@dataclass(frozen=True)
class Check:
    name: str
    ok: bool
    detail: str


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_url(url: str) -> MySqlTarget:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        fail("use mysql://user:password@host:port/database")
    database = parsed.path.lstrip("/")
    if not database:
        fail("database name is missing in MySQL URL")
    return MySqlTarget(
        host=parsed.hostname or "localhost",
        port=int(parsed.port or 3306),
        user=unquote(parsed.username or "root"),
        password=unquote(parsed.password or ""),
        database=database,
    )


def mysql_cmd(target: MySqlTarget) -> list[str]:
    exe = shutil.which("mysql")
    if not exe:
        fail("mysql command not found")
    cmd = [
        exe,
        f"--host={target.host}",
        f"--port={target.port}",
        f"--user={target.user}",
        "--default-character-set=utf8mb4",
        "--batch",
        "--raw",
        "--skip-column-names",
    ]
    if target.password:
        cmd.append(f"--password={target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: MySqlTarget, sql: str) -> str:
    proc = subprocess.run(
        mysql_cmd(target) + ["--execute", sql],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        fail(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def count(target: MySqlTarget, sql: str) -> int:
    raw = run_mysql(target, sql)
    try:
        return int(raw.splitlines()[-1] if raw else "0")
    except ValueError:
        return 0


def exists_count_check(target: MySqlTarget, name: str, sql: str, minimum: int = 1) -> Check:
    c = count(target, sql)
    return Check(name, c >= minimum, f"{c} rows, expected >= {minimum}")


def quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate imported Gothic MMO MySQL bootstrap data.")
    parser.add_argument("--url", default=os.environ.get("MYSQL_URL", ""), help="MySQL URL. Defaults to MYSQL_URL.")
    parser.add_argument("--realm-key", default="local-dev")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--content-revision-key", default="", help="Optional exact content revision key to validate.")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    if not args.url:
        fail("provide --url or MYSQL_URL")
    target = parse_url(args.url)

    checks: list[Check] = []
    checks.append(exists_count_check(target, "schema marker 001", "SELECT COUNT(*) FROM mmo_schema_versions WHERE migration_key='production/mysql/001_gothic_mmo_production_schema';"))
    checks.append(exists_count_check(target, "schema marker 002", "SELECT COUNT(*) FROM mmo_schema_versions WHERE migration_key='production/mysql/002_bootstrap_import_pipeline';"))
    checks.append(exists_count_check(target, "finished import runs", "SELECT COUNT(*) FROM mmo_import_runs WHERE status='finished';"))
    checks.append(exists_count_check(target, "content game target", "SELECT COUNT(*) FROM content_game_targets WHERE game_code IN ('g1','g2','g2notr');"))
    checks.append(exists_count_check(target, "content revisions", "SELECT COUNT(*) FROM content_revisions;"))
    checks.append(exists_count_check(target, "world templates", "SELECT COUNT(*) FROM content_world_templates;"))
    checks.append(exists_count_check(target, "realm", f"SELECT COUNT(*) FROM realm_realms WHERE realm_key={quote(args.realm_key)};"))
    checks.append(exists_count_check(target, "world instances", f"SELECT COUNT(*) FROM realm_world_instances wi JOIN realm_realms r ON r.realm_id=wi.realm_id WHERE r.realm_key={quote(args.realm_key)};"))
    checks.append(exists_count_check(target, "character", f"SELECT COUNT(*) FROM characters WHERE character_key={quote(args.character_key)};"))
    checks.append(exists_count_check(target, "character position", f"SELECT COUNT(*) FROM character_positions p JOIN characters c ON c.character_id=p.character_id WHERE c.character_key={quote(args.character_key)};"))
    checks.append(exists_count_check(target, "character stats", f"SELECT COUNT(*) FROM character_stats s JOIN characters c ON c.character_id=s.character_id WHERE c.character_key={quote(args.character_key)};"))
    checks.append(exists_count_check(target, "import event", "SELECT COUNT(*) FROM world_event_journal WHERE event_type='bootstrap_import_completed' AND source='import';"))

    if args.content_revision_key:
        checks.append(exists_count_check(target, "requested content revision", f"SELECT COUNT(*) FROM content_revisions WHERE content_revision_key={quote(args.content_revision_key)};"))

    ok = True
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        ok = ok and check.ok

    if not ok:
        return 1

    summary = run_mysql(target, "SELECT import_run_id, source_schema_version, game_code, status, counters FROM v_mmo_import_runs ORDER BY started_at DESC LIMIT 1;")
    if summary:
        print("latest import:")
        print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
