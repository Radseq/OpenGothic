#!/usr/bin/env python3
"""Validate a PostgreSQL bootstrap import created from OpenGothic runtime SQLite."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Sequence


@dataclass(frozen=True)
class Check:
    name: str
    ok: bool
    detail: str


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def run_psql(dsn: str, sql: str) -> str:
    exe = shutil.which("psql")
    if not exe:
        fail("psql command not found")
    proc = subprocess.run(
        [exe, dsn, "-v", "ON_ERROR_STOP=1", "-q", "-t", "-A", "-F", "\t", "-c", sql],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        fail(f"psql exited with status {proc.returncode}")
    return proc.stdout.strip()


def count(dsn: str, sql: str) -> int:
    raw = run_psql(dsn, sql)
    try:
        return int(raw.splitlines()[-1] if raw else "0")
    except ValueError:
        return 0


def exists_count_check(dsn: str, name: str, sql: str, minimum: int = 1) -> Check:
    c = count(dsn, sql)
    return Check(name, c >= minimum, f"{c} rows, expected >= {minimum}")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate imported Gothic MMO PostgreSQL bootstrap data.")
    parser.add_argument("--dsn", default=os.environ.get("DATABASE_URL", ""), help="PostgreSQL DSN. Defaults to DATABASE_URL.")
    parser.add_argument("--realm-key", default="local-dev")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--content-revision-key", default="", help="Optional exact content revision key to validate.")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    if not args.dsn:
        fail("provide --dsn or DATABASE_URL")

    checks: list[Check] = []
    checks.append(exists_count_check(args.dsn, "schema marker 001", "SELECT count(*) FROM mmo_schema_versions WHERE migration_key='production/001_gothic_mmo_production_schema';"))
    checks.append(exists_count_check(args.dsn, "schema marker 002", "SELECT count(*) FROM mmo_schema_versions WHERE migration_key='production/002_bootstrap_import_pipeline';"))
    checks.append(exists_count_check(args.dsn, "finished import runs", "SELECT count(*) FROM mmo_import_runs WHERE status='finished';"))
    checks.append(exists_count_check(args.dsn, "content game target", "SELECT count(*) FROM content_game_targets WHERE game_code IN ('g1','g2','g2notr');"))
    checks.append(exists_count_check(args.dsn, "content revisions", "SELECT count(*) FROM content_revisions;"))
    checks.append(exists_count_check(args.dsn, "world templates", "SELECT count(*) FROM content_world_templates;"))
    checks.append(exists_count_check(args.dsn, "realm", f"SELECT count(*) FROM realm_realms WHERE realm_key='{args.realm_key.replace("'", "''")}';"))
    checks.append(exists_count_check(args.dsn, "world instances", f"SELECT count(*) FROM realm_world_instances wi JOIN realm_realms r ON r.realm_id=wi.realm_id WHERE r.realm_key='{args.realm_key.replace("'", "''")}';"))
    checks.append(exists_count_check(args.dsn, "character", f"SELECT count(*) FROM characters WHERE character_key='{args.character_key.replace("'", "''")}';"))
    checks.append(exists_count_check(args.dsn, "character position", f"SELECT count(*) FROM character_positions p JOIN characters c ON c.character_id=p.character_id WHERE c.character_key='{args.character_key.replace("'", "''")}';"))
    checks.append(exists_count_check(args.dsn, "character stats", f"SELECT count(*) FROM character_stats s JOIN characters c ON c.character_id=s.character_id WHERE c.character_key='{args.character_key.replace("'", "''")}';"))
    checks.append(exists_count_check(args.dsn, "import event", "SELECT count(*) FROM world_event_journal WHERE event_type='bootstrap_import_completed' AND source='import';"))

    if args.content_revision_key:
        key = args.content_revision_key.replace("'", "''")
        checks.append(exists_count_check(args.dsn, "requested content revision", f"SELECT count(*) FROM content_revisions WHERE content_revision_key='{key}';"))

    ok = True
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        ok = ok and check.ok

    if not ok:
        return 1

    summary = run_psql(args.dsn, "SELECT import_run_id, source_schema_version, game_code, status, counters FROM v_mmo_import_runs ORDER BY started_at DESC LIMIT 1;")
    if summary:
        print("latest import:")
        print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
