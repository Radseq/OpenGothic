#!/usr/bin/env python3
"""Verify that a clean MySQL DB is ready for Step55 receiver + outbox usage."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REQUIRED_TABLES = [
    "server_sessions",
    "mmo_server_action_outbox",
    "mmo_server_action_worker_runs",
    "mmo_server_action_worker_results",
]
REQUIRED_ROUTINES = [
    "mmo_login_character",
    "mmo_enqueue_server_action",
    "mmo_claim_next_server_action",
    "mmo_mark_server_action_applied",
    "mmo_mark_server_action_failed",
    "mmo_start_server_action_worker_run",
    "mmo_finish_server_action_worker_run",
    "mmo_record_server_action_worker_result",
]


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


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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


def exists_count(target: Target, object_type: str, name: str) -> int:
    if object_type == "table":
        sql = f"SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name={sql_literal(name)} AND table_type='BASE TABLE';"
    elif object_type == "routine":
        sql = f"SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=DATABASE() AND routine_name={sql_literal(name)};"
    else:
        raise ValueError(object_type)
    out = run_mysql(target, sql)
    return int((out or "0").splitlines()[-1])


def smoke(target: Target, account_name: str, character_key: str) -> dict[str, Any]:
    suffix = uuid.uuid4().hex[:12]
    session_key = f"step55e-smoke-{suffix}"
    idem = f"{session_key}:client_bootstrap_request:1"
    out = run_mysql(
        target,
        f"""
        SET @session_id=NULL;
        CALL mmo_login_character(
          {sql_literal(account_name)},
          {sql_literal(character_key)},
          {sql_literal(session_key)},
          'step55e-smoke',
          '127.0.0.1',
          JSON_OBJECT('tool','check_mmo_step55_live_receiver_bridge'),
          @session_id
        );
        SET @action_id=NULL;
        SET @status=NULL;
        CALL mmo_enqueue_server_action(
          @session_id,
          'client_bootstrap_request',
          {sql_literal(character_key)},
          JSON_OBJECT('character_key',{sql_literal(character_key)},'world','NEWWORLD','server_tick',1),
          {sql_literal(idem)},
          100,
          5,
          @action_id,
          @status
        );
        SET @mark_status=NULL;
        CALL mmo_mark_server_action_applied(
          @action_id,
          NULL,
          JSON_OBJECT('tool','check_mmo_step55_live_receiver_bridge','smoke',TRUE),
          @mark_status
        );
        SET @logout_event_id=NULL;
        CALL mmo_logout_character(
          @session_id,
          'step55e_smoke_done',
          JSON_OBJECT('tool','check_mmo_step55_live_receiver_bridge','smoke',TRUE),
          @logout_event_id
        );
        SELECT BIN_TO_UUID(@session_id,1), BIN_TO_UUID(@action_id,1), @status, @mark_status, BIN_TO_UUID(@logout_event_id,1);
        """,
    )
    parts = (out.splitlines()[-1] if out else "").split("\t")
    return {
        "session_uuid": parts[0] if len(parts) > 0 else "",
        "action_uuid": parts[1] if len(parts) > 1 else "",
        "enqueue_status": parts[2] if len(parts) > 2 else "",
        "mark_status": parts[3] if len(parts) > 3 else "",
        "logout_event_uuid": parts[4] if len(parts) > 4 else "",
        "ok": (
            len(parts) == 5
            and parts[0] not in {"", "NULL"}
            and parts[1] not in {"", "NULL"}
            and parts[2] in {"pending", "applied"}
            and parts[3] == "applied"
            and parts[4] not in {"", "NULL"}
        ),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step55e MySQL live receiver bridge readiness.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--smoke", action="store_true", help="Run a safe login+enqueue smoke row.")
    ap.add_argument("--output", help="Optional JSON result path")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, Any] = {
        "database": target.database,
        "tables": {name: exists_count(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: exists_count(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
    }
    result["missing_tables"] = [name for name, ok in result["tables"].items() if not ok]
    result["missing_routines"] = [name for name, ok in result["routines"].items() if not ok]

    if args.smoke and not result["missing_tables"] and not result["missing_routines"]:
        result["smoke"] = smoke(target, args.account_name, args.character_key)
    elif args.smoke:
        result["smoke"] = {"ok": False, "skipped_reason": "missing required bridge objects"}

    smoke_ok = True if not args.smoke else bool(result.get("smoke", {}).get("ok"))
    result["status"] = "passed" if not result["missing_tables"] and not result["missing_routines"] and smoke_ok else "failed"

    for table, ok in result["tables"].items():
        print(("OK" if ok else "ERROR") + f": table {table}")
    for routine, ok in result["routines"].items():
        print(("OK" if ok else "ERROR") + f": routine {routine}")
    if args.smoke:
        print(("OK" if smoke_ok else "ERROR") + f": smoke login+enqueue {result.get('smoke')}")

    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    print("status=" + result["status"])
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
