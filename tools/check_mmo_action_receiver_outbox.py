#!/usr/bin/env python3
"""Inspect MySQL outbox rows produced by run_mmo_action_receiver.py.

Uses the mysql command-line client only; no Python MySQL dependency.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from typing import Iterable
from urllib.parse import unquote, urlparse


@dataclass(frozen=True)
class MySqlTarget:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> MySqlTarget:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("use mysql://user:password@host:port/database")
    database = parsed.path.lstrip("/")
    if not database:
        raise ValueError("database name is missing in MySQL URL")
    return MySqlTarget(
        host=parsed.hostname or "localhost",
        port=int(parsed.port or 3306),
        user=unquote(parsed.username or "root"),
        password=unquote(parsed.password or ""),
        database=database,
    )


def mysql_cmd(target: MySqlTarget) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
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
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def rows(raw: str) -> list[list[str]]:
    return [line.split("\t") for line in raw.splitlines() if line.strip()]


def split_csv(value: str) -> list[str]:
    return [x.strip() for x in value.split(",") if x.strip()]


def sql_where(args: argparse.Namespace) -> str:
    clauses: list[str] = []
    if args.session_key:
        clauses.append(f"s.session_key = {sql_literal(args.session_key)}")
    if args.idempotency_prefix:
        clauses.append(f"o.idempotency_key LIKE CONCAT({sql_literal(args.idempotency_prefix)}, '%')")
    if args.status:
        statuses = ",".join(sql_literal(x) for x in split_csv(args.status))
        clauses.append(f"o.status IN ({statuses})")
    if not clauses:
        clauses.append("o.requested_at >= TIMESTAMPADD(HOUR, -6, CURRENT_TIMESTAMP(6))")
    return " AND ".join(clauses)


def print_counts(target: MySqlTarget, where: str) -> tuple[int, Counter[str], Counter[str]]:
    raw = run_mysql(
        target,
        f"""
        SELECT o.action_kind, o.status, COUNT(*)
          FROM mmo_server_action_outbox o
          LEFT JOIN server_sessions s ON s.session_id=o.session_id
         WHERE {where}
         GROUP BY o.action_kind, o.status
         ORDER BY o.action_kind, o.status;
        """,
    )
    total = 0
    kinds: Counter[str] = Counter()
    statuses: Counter[str] = Counter()
    for kind, status, count_text in rows(raw):
        count = int(count_text)
        total += count
        kinds[kind] += count
        statuses[status] += count
        print(f"kind.{kind}.{status}={count}")
    print(f"rows={total}")
    for status, count in sorted(statuses.items()):
        print(f"status.{status}={count}")
    return total, kinds, statuses


def print_latest(target: MySqlTarget, where: str, limit: int) -> None:
    if limit <= 0:
        return
    raw = run_mysql(
        target,
        f"""
        SELECT BIN_TO_UUID(o.action_id,1), o.action_kind, o.status, o.idempotency_key,
               o.target_key, JSON_EXTRACT(o.request_payload, '$.dispatch_ready'),
               JSON_EXTRACT(o.request_payload, '$.dispatch_missing_fields'), o.requested_at
          FROM mmo_server_action_outbox o
          LEFT JOIN server_sessions s ON s.session_id=o.session_id
         WHERE {where}
         ORDER BY o.requested_at DESC
         LIMIT {int(limit)};
        """,
    )
    if not raw:
        return
    print("latest:")
    for action_uuid, kind, status, idem, target_key, ready, missing, requested_at in rows(raw):
        print(f"  {requested_at} {status:9} {kind:28} {action_uuid} ready={ready} missing={missing} target={target_key} idem={idem}")


def print_gaps(target: MySqlTarget, where: str) -> int:
    raw = run_mysql(
        target,
        f"""
        SELECT o.action_kind, COUNT(*)
          FROM mmo_server_action_outbox o
          LEFT JOIN server_sessions s ON s.session_id=o.session_id
          LEFT JOIN mmo_server_action_dispatch_contracts c ON c.action_kind=o.action_kind AND c.enabled=TRUE
         WHERE {where}
           AND o.status IN ('pending','claimed')
           AND c.action_kind IS NULL
         GROUP BY o.action_kind
         ORDER BY o.action_kind;
        """,
    )
    gap_count = 0
    for kind, count_text in rows(raw):
        count = int(count_text)
        gap_count += count
        print(f"gap.missing_or_disabled_contract.{kind}={count}")
    print(f"contract_gaps={gap_count}")
    return gap_count


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Check MMO receiver-enqueued MySQL outbox rows")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", default="", help="filter by server_sessions.session_key")
    ap.add_argument("--idempotency-prefix", default="", help="filter by idempotency key prefix")
    ap.add_argument("--status", default="", help="comma-separated status filter")
    ap.add_argument("--require-kind", action="append", default=[])
    ap.add_argument("--expect-count", type=int, default=-1)
    ap.add_argument("--latest", type=int, default=20)
    ap.add_argument("--allow-contract-gaps", action="store_true")
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    where = sql_where(args)
    total, kinds, _statuses = print_counts(target, where)
    gap_count = print_gaps(target, where)
    print_latest(target, where, args.latest)

    ok = True
    if args.expect_count >= 0 and total != args.expect_count:
        print(f"[FAIL] expected rows={args.expect_count}, got={total}")
        ok = False
    for required in args.require_kind:
        if kinds[required] <= 0:
            print(f"[FAIL] missing required kind: {required}")
            ok = False
    if gap_count and not args.allow_contract_gaps:
        print("[FAIL] pending/claimed rows have dispatch contract gaps")
        ok = False

    print("[OK]" if ok else "[FAIL]")
    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
