#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Any
from urllib.parse import unquote, urlparse


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
        raise ValueError("database missing in URL")
    return Target(p.hostname or "localhost", p.port or 3306, unquote(p.username or ""), unquote(p.password or ""), db)


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable not found")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def rows(out: str) -> list[list[str]]:
    return [ln.split("\t") for ln in out.splitlines() if ln.strip()]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Check resolved MMO action dispatch results in MySQL.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", default="local-dev-PC_HERO", help="idempotency/session key prefix to inspect")
    ap.add_argument("--require-applied-kind", action="append", default=[])
    ap.add_argument("--allow-failed", action="store_true")
    ap.add_argument("--limit", type=int, default=20)
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    prefix = args.session_key + ":%"
    ok = True

    print("Outbox by kind/status:")
    out = run_mysql(target, f"""
        SELECT action_kind, status, COUNT(*)
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         GROUP BY action_kind, status
         ORDER BY action_kind, status;
    """)
    print(out or "(none)")

    print("\nJournal events from receiver/worker idempotency prefix:")
    out = run_mysql(target, f"""
        SELECT event_type, event_class, source, COUNT(*)
          FROM world_event_journal
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         GROUP BY event_type, event_class, source
         ORDER BY event_type, event_class, source;
    """)
    print(out or "(none)")

    print("\nLatest matching outbox rows:")
    out = run_mysql(target, f"""
        SELECT action_kind, status, COALESCE(BIN_TO_UUID(event_id,1),'NULL'), idempotency_key, COALESCE(last_error_code,''), LEFT(COALESCE(last_error_message,''),180)
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         ORDER BY updated_at DESC
         LIMIT {int(args.limit)};
    """)
    print(out or "(none)")

    print("\nLatest worker results:")
    # v_server_action_worker_latest_results exposes event_uuid/action_uuid text aliases,
    # not raw event_id. Older checker versions crashed here with Unknown column event_id.
    out = run_mysql(target, f"""
        SELECT action_kind, status, COALESCE(event_uuid,'NULL'), COALESCE(error_code,''), LEFT(COALESCE(error_message,''),180)
          FROM v_server_action_worker_latest_results
         WHERE action_uuid IN (
               SELECT BIN_TO_UUID(action_id,1)
                 FROM mmo_server_action_outbox
                WHERE idempotency_key LIKE {sql_literal(prefix)}
         )
         LIMIT {int(args.limit)};
    """)
    print(out or "(none)")

    for kind in args.require_applied_kind:
        out = run_mysql(target, f"""
            SELECT COUNT(*)
              FROM mmo_server_action_outbox
             WHERE idempotency_key LIKE {sql_literal(prefix)}
               AND action_kind = {sql_literal(kind)}
               AND status = 'applied';
        """)
        count = int((out or "0").splitlines()[-1])
        if count <= 0:
            print(f"[FAIL] applied kind missing: {kind}")
            ok = False
        else:
            print(f"[OK] applied kind {kind}: {count}")

    if not args.allow_failed:
        out = run_mysql(target, f"""
            SELECT COUNT(*)
              FROM mmo_server_action_outbox
             WHERE idempotency_key LIKE {sql_literal(prefix)}
               AND status IN ('failed','dead_letter');
        """)
        bad = int((out or "0").splitlines()[-1])
        if bad:
            print(f"[FAIL] failed/dead-letter actions: {bad}")
            ok = False
        else:
            print("[OK] no failed/dead-letter matching actions")

    print("[OK]" if ok else "[FAIL]")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
