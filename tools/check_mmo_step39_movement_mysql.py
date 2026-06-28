#!/usr/bin/env python3
"""Validate Step39 character_checkpoint outbox -> MySQL checkpoint evidence."""
from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse


class Target:
    def __init__(self, url: str):
        p = urlparse(url)
        if p.scheme not in {"mysql", "mysql+pymysql"}:
            raise ValueError("expected mysql:// URL")
        self.host = p.hostname or "localhost"
        self.port = int(p.port or 3306)
        self.user = unquote(p.username or "")
        self.password = unquote(p.password or "")
        self.database = p.path.lstrip("/")
        if not self.database:
            raise ValueError("database is missing")


def mysql_cmd(t: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--batch", "--raw", "--skip-column-names", "-h", t.host, "-P", str(t.port), "-u", t.user]
    if t.password:
        cmd.append(f"-p{t.password}")
    cmd.append(t.database)
    return cmd


def run_mysql(t: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(t), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def lit(value: Any) -> str:
    if value is None:
        return "NULL"
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''") + "'"


def count(t: Target, sql: str) -> int:
    out = run_mysql(t, sql)
    if not out:
        return 0
    return int(out.splitlines()[-1].split("\t")[0] or 0)


def split_row(text: str) -> list[str] | None:
    if not text:
        return None
    return text.splitlines()[-1].split("\t")


def f(value: str | None) -> float | None:
    try:
        return float(value) if value not in (None, "NULL") else None
    except ValueError:
        return None


def distance(a: list[str] | None, b: list[str] | None) -> float | None:
    if not a or not b or len(a) < 3 or len(b) < 3:
        return None
    vals_a = [f(a[0]), f(a[1]), f(a[2])]
    vals_b = [f(b[0]), f(b[1]), f(b[2])]
    if any(v is None for v in vals_a + vals_b):
        return None
    return math.sqrt(sum((float(x) - float(y)) ** 2 for x, y in zip(vals_a, vals_b)))


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step39 movement/checkpoint MySQL evidence")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--require-no-failed", action="store_true")
    ap.add_argument("--min-applied", type=int, default=1)
    ap.add_argument("--min-distinct-positions", type=int, default=1)
    ap.add_argument("--require-position-change", action="store_true")
    ap.add_argument("--projection-epsilon", type=float, default=0.01)
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step39_movement_mysql_e2e.json"))
    args = ap.parse_args()

    t = Target(args.url)
    prefix = args.session_key + ":%"
    errors: list[str] = []
    outbox_rows = count(t, f"SELECT COUNT(*) FROM mmo_server_action_outbox WHERE idempotency_key LIKE {lit(prefix)} AND action_kind='character_checkpoint';")
    applied_rows = count(t, f"SELECT COUNT(*) FROM mmo_server_action_outbox WHERE idempotency_key LIKE {lit(prefix)} AND action_kind='character_checkpoint' AND status='applied';")
    failed_rows = count(t, f"SELECT COUNT(*) FROM mmo_server_action_outbox WHERE idempotency_key LIKE {lit(prefix)} AND action_kind='character_checkpoint' AND status IN ('failed','dead_letter');")
    dupes = count(t, f"""
        SELECT COUNT(*) FROM (
          SELECT idempotency_key FROM mmo_server_action_outbox
           WHERE idempotency_key LIKE {lit(prefix)} AND action_kind='character_checkpoint'
           GROUP BY idempotency_key HAVING COUNT(*) > 1
        ) d;
    """)
    journal_events = count(t, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key LIKE {lit(prefix)} AND event_type='character_position_checkpoint';")
    audit_rows = count(t, f"SELECT COUNT(*) FROM character_checkpoint_audit WHERE idempotency_key LIKE {lit(prefix)};")
    distinct_positions = count(t, f"""
        SELECT COUNT(*) FROM (
          SELECT ROUND(pos_x, 2), ROUND(pos_y, 2), ROUND(pos_z, 2)
            FROM character_checkpoint_audit
           WHERE idempotency_key LIKE {lit(prefix)}
           GROUP BY ROUND(pos_x, 2), ROUND(pos_y, 2), ROUND(pos_z, 2)
        ) p;
    """)

    latest_projection = split_row(run_mysql(t, f"""
        SELECT cp.pos_x, cp.pos_y, cp.pos_z, cp.rotation_yaw, cp.server_tick, cp.row_version,
               BIN_TO_UUID(cp.character_id,1), BIN_TO_UUID(cp.world_instance_id,1)
          FROM character_positions cp
          JOIN characters c ON c.character_id = cp.character_id
         WHERE c.character_key='PC_HERO'
         ORDER BY cp.updated_at DESC
         LIMIT 1;
    """))
    latest_audit = split_row(run_mysql(t, f"""
        SELECT pos_x, pos_y, pos_z, rotation_yaw, server_tick, position_row_version_after,
               BIN_TO_UUID(event_id,1), idempotency_key
          FROM character_checkpoint_audit
         WHERE idempotency_key LIKE {lit(prefix)}
         ORDER BY created_at DESC
         LIMIT 1;
    """))
    first_audit = split_row(run_mysql(t, f"""
        SELECT pos_x, pos_y, pos_z, rotation_yaw, server_tick, position_row_version_after,
               BIN_TO_UUID(event_id,1), idempotency_key
          FROM character_checkpoint_audit
         WHERE idempotency_key LIKE {lit(prefix)}
         ORDER BY created_at ASC
         LIMIT 1;
    """))

    projection_distance = distance(latest_projection, latest_audit)

    if outbox_rows <= 0:
        errors.append("no character_checkpoint outbox rows")
    if applied_rows < args.min_applied:
        errors.append(f"too few applied checkpoint rows: {applied_rows} < {args.min_applied}")
    if args.require_no_failed and failed_rows:
        errors.append(f"failed/dead_letter checkpoint rows: {failed_rows}")
    if dupes:
        errors.append(f"duplicate idempotency keys: {dupes}")
    if journal_events < applied_rows:
        errors.append(f"journal events lower than applied rows: {journal_events}/{applied_rows}")
    if audit_rows < applied_rows:
        errors.append(f"checkpoint audit rows lower than applied rows: {audit_rows}/{applied_rows}")
    if not latest_projection:
        errors.append("missing character_positions projection row for PC_HERO")
    if not latest_audit:
        errors.append("missing latest character_checkpoint_audit row for session")
    if args.require_position_change and distinct_positions < max(2, args.min_distinct_positions):
        errors.append(f"not enough distinct checkpoint positions: {distinct_positions}")
    elif distinct_positions < args.min_distinct_positions:
        errors.append(f"distinct checkpoint positions lower than required: {distinct_positions} < {args.min_distinct_positions}")
    if projection_distance is not None and projection_distance > args.projection_epsilon:
        errors.append(f"character_positions latest projection does not match latest audit: distance={projection_distance:.6f} > {args.projection_epsilon:.6f}")

    result = {
        "tool": "check_mmo_step39_movement_mysql.py",
        "status": "passed" if not errors else "failed",
        "session_key": args.session_key,
        "outbox_rows": outbox_rows,
        "applied_rows": applied_rows,
        "failed_rows": failed_rows,
        "duplicate_idempotency_groups": dupes,
        "journal_events": journal_events,
        "audit_rows": audit_rows,
        "distinct_positions": distinct_positions,
        "first_audit": first_audit,
        "latest_audit": latest_audit,
        "latest_character_position": latest_projection,
        "latest_projection_distance_to_audit": projection_distance,
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    if errors:
        for error in errors:
            print("ERROR:", error)
    else:
        print("OK: Step39 checkpoint MySQL evidence:", {"outbox": outbox_rows, "applied": applied_rows, "journal": journal_events, "audit": audit_rows, "distinct_positions": distinct_positions})
    print(f"artifact={args.output}")
    print(f"status={result['status']}")
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
