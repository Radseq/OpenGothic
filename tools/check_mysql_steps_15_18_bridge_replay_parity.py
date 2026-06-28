#!/usr/bin/env python3
"""Validate and smoke-test MySQL production steps 015..018.

Uses only the mysql command-line client. Requires migrations 001..018 and a
bootstrap-imported account/character, plus earlier write-path procedures.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
from urllib.parse import unquote, urlparse


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


def parse_url(url: str) -> MySqlTarget:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("use mysql://user:password@host:port/database")
    database = parsed.path.lstrip("/")
    if not database:
        raise ValueError("database name is missing in MySQL URL")
    return MySqlTarget(parsed.hostname or "localhost", int(parsed.port or 3306), unquote(parsed.username or "root"), unquote(parsed.password or ""), database)


def mysql_cmd(target: MySqlTarget) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [exe, f"--host={target.host}", f"--port={target.port}", f"--user={target.user}", "--default-character-set=utf8mb4", "--batch", "--raw", "--skip-column-names"]
    if target.password:
        cmd.append(f"--password={target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: MySqlTarget, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target) + ["--execute", sql], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value)
    return "'" + text.replace("'", "''").replace("\\", "\\\\") + "'"


def line_set(raw: str) -> set[str]:
    return {line.strip() for line in raw.splitlines() if line.strip()}


def scalar(target: MySqlTarget, sql: str) -> str:
    raw = run_mysql(target, sql)
    return raw.splitlines()[-1].strip() if raw else ""


def row(target: MySqlTarget, sql: str) -> list[str]:
    raw = scalar(target, sql)
    return raw.split("\t") if raw else []


def count(target: MySqlTarget, sql: str) -> int:
    raw = scalar(target, sql)
    try:
        return int(raw)
    except ValueError:
        return 0


def check_marker(target: MySqlTarget, migration_key: str, expected_contract: str, label: str) -> Check:
    value = scalar(target, f"SELECT schema_contract FROM mmo_schema_versions WHERE migration_key={sql_literal(migration_key)};")
    return Check(label, value == expected_contract, value or "missing")


def check_named(target: MySqlTarget, name: str, sql: str, required: Iterable[str]) -> Check:
    found = line_set(run_mysql(target, sql))
    missing = sorted(set(required) - found)
    if missing:
        return Check(name, False, "missing: " + ", ".join(missing))
    return Check(name, True, f"ok ({len(set(required))} required)")


def validate_objects(target: MySqlTarget) -> list[Check]:
    db = target.database.replace("'", "''")
    return [
        check_marker(target, "production/mysql/015_server_action_outbox", "gothic-mmo-server-action-outbox-v1-mysql", "migration 015 marker"),
        check_marker(target, "production/mysql/016_restore_parity_gate", "gothic-mmo-restore-parity-gate-v1-mysql", "migration 016 marker"),
        check_marker(target, "production/mysql/017_event_replay_contract", "gothic-mmo-event-replay-contract-v1-mysql", "migration 017 marker"),
        check_marker(target, "production/mysql/018_mmo_readiness_dashboard", "gothic-mmo-readiness-dashboard-v1-mysql", "migration 018 marker"),
        check_named(target, "steps 015..018 tables", f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';", (
            "mmo_server_action_outbox", "mmo_restore_parity_scenarios", "mmo_restore_parity_runs", "mmo_restore_parity_results", "mmo_event_projection_contracts", "mmo_readiness_runs", "mmo_readiness_results"
        )),
        check_named(target, "steps 015..018 views", f"SELECT table_name FROM information_schema.views WHERE table_schema='{db}';", (
            "v_pending_server_actions", "v_server_action_outbox", "v_restore_parity_latest_runs", "v_restore_parity_failures", "v_event_replay_contract_coverage", "v_event_replay_contract_gaps", "v_mmo_readiness_latest", "v_mmo_readiness_blockers", "v_mmo_remaining_work"
        )),
        check_named(target, "steps 015..018 routines", f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';", (
            "mmo_enqueue_server_action", "mmo_mark_server_action_applied", "mmo_mark_server_action_failed", "mmo_start_restore_parity_run", "mmo_record_restore_parity_result", "mmo_finalize_restore_parity_run", "mmo_validate_event_replay_contract", "mmo_evaluate_mmo_readiness"
        )),
    ]


def call_login(target: MySqlTarget, account_name: str, character_key: str, session_key: str) -> str:
    return scalar(target, f"""
        SET @session_id = NULL;
        CALL mmo_login_character({sql_literal(account_name)}, {sql_literal(character_key)}, {sql_literal(session_key)}, 'steps-15-18-smoke', '127.0.0.1', JSON_OBJECT('source','check_mysql_steps_15_18'), @session_id);
        SELECT BIN_TO_UUID(@session_id, 1);
    """)


def call_logout(target: MySqlTarget, session_uuid: str, reason: str = "smoke done") -> str:
    return scalar(target, f"""
        SET @event_id = NULL;
        CALL mmo_logout_character(UUID_TO_BIN({sql_literal(session_uuid)}, 1), {sql_literal(reason)}, JSON_OBJECT('source','check_mysql_steps_15_18'), @event_id);
        SELECT BIN_TO_UUID(@event_id, 1);
    """)


def active_sessions(target: MySqlTarget, session_key: str) -> int:
    return count(target, f"SELECT COUNT(*) FROM server_sessions WHERE session_key={sql_literal(session_key)} AND lifecycle_state='active';")


def current_world_uuid(target: MySqlTarget, character_key: str) -> str:
    return scalar(target, f"""
        SELECT BIN_TO_UUID(COALESCE(c.current_world_instance_id, cp.world_instance_id),1)
          FROM characters c
          LEFT JOIN character_positions cp ON cp.character_id=c.character_id
         WHERE c.character_key={sql_literal(character_key)}
         LIMIT 1;
    """)


def run_smoke(target: MySqlTarget, account_name: str, character_key: str, project_root: Path) -> list[Check]:
    checks: list[Check] = []
    suffix = uuid.uuid4().hex
    session_key = f"steps-15-18-smoke:{suffix}"
    session_uuid = ""
    try:
        session_uuid = call_login(target, account_name, character_key, session_key)
        checks.append(Check("login", bool(session_uuid), session_uuid))

        action_key = f"smoke:server-action:{suffix}"
        first = row(target, f"""
            SET @action_id=NULL; SET @status=NULL;
            CALL mmo_enqueue_server_action(UUID_TO_BIN({sql_literal(session_uuid)},1), 'pickup_world_item', 'smoke:target', JSON_OBJECT('source','steps-15-18-smoke'), {sql_literal(action_key)}, 10, 3, @action_id, @status);
            SELECT BIN_TO_UUID(@action_id,1), @status;
        """)
        retry = row(target, f"""
            SET @action_id=NULL; SET @status=NULL;
            CALL mmo_enqueue_server_action(UUID_TO_BIN({sql_literal(session_uuid)},1), 'pickup_world_item', 'smoke:target', JSON_OBJECT('source','steps-15-18-smoke','retry',true), {sql_literal(action_key)}, 10, 3, @action_id, @status);
            SELECT BIN_TO_UUID(@action_id,1), @status;
        """)
        checks.append(Check("server action enqueue idempotent", first == retry and len(first) == 2 and first[1] == "pending", f"first={'/'.join(first)} retry={'/'.join(retry)}"))

        applied = row(target, f"""
            SET @status=NULL;
            CALL mmo_mark_server_action_applied(UUID_TO_BIN({sql_literal(first[0])},1), NULL, JSON_OBJECT('source','steps-15-18-smoke','applied',true), @status);
            SELECT @status;
        """)
        checks.append(Check("server action applied", applied == ["applied"], "/".join(applied)))

        world_uuid = current_world_uuid(target, character_key)
        replay = row(target, f"""
            SET @run_id=NULL; SET @errors=NULL; SET @warnings=NULL;
            CALL mmo_validate_event_replay_contract(UUID_TO_BIN({sql_literal(world_uuid)},1), {sql_literal('steps-15-18-replay:' + suffix)}, JSON_OBJECT('source','steps-15-18-smoke'), @run_id, @errors, @warnings);
            SELECT BIN_TO_UUID(@run_id,1), @errors, @warnings;
        """)
        replay_ok = len(replay) == 3 and replay[1] == "0"
        replay_detail = "/".join(replay)
        if not replay_ok:
            gaps = run_mysql(target, """
                SELECT CONCAT(event_type, ': observed=', COALESCE(observed_event_class,'NULL'), ', contract=', COALESCE(contract_event_class,'NULL'), ', count=', event_count)
                  FROM v_event_replay_contract_gaps
                 ORDER BY event_type, observed_event_class
                 LIMIT 12;
            """)
            if gaps:
                replay_detail += " gaps=[" + gaps.replace("\n", "; ") + "]"
        checks.append(Check("event replay contract validation", replay_ok, replay_detail))

        parity_run = row(target, f"""
            SET @parity_run_id=NULL;
            CALL mmo_start_restore_parity_run(UUID_TO_BIN({sql_literal(world_uuid)},1), {sql_literal(character_key)}, {sql_literal('steps-15-18-parity:' + suffix)}, JSON_OBJECT('source','steps-15-18-smoke','mode','contract-only'), @parity_run_id);
            SELECT BIN_TO_UUID(@parity_run_id,1);
        """)
        parity_id = parity_run[0] if parity_run else ""
        checks.append(Check("restore parity run started", bool(parity_id), parity_id))

        scenarios = [line.strip() for line in run_mysql(target, "SELECT scenario_key FROM mmo_restore_parity_scenarios WHERE active=TRUE AND required=TRUE ORDER BY sort_order;").splitlines() if line.strip()]
        for scenario in scenarios:
            # Contract-only smoke marks scenarios blocked, not passed: real native .sav/SQLite/MySQL comparison must do the final pass.
            run_mysql(target, f"""
                CALL mmo_record_restore_parity_result(UUID_TO_BIN({sql_literal(parity_id)},1), {sql_literal(scenario)}, 'blocked', NULL, NULL, NULL, JSON_OBJECT('source','steps-15-18-smoke','reason','contract registered; real parity run not executed by smoke'));
            """)
        parity_final = row(target, f"""
            SET @status=NULL; SET @failed=NULL;
            CALL mmo_finalize_restore_parity_run(UUID_TO_BIN({sql_literal(parity_id)},1), @status, @failed);
            SELECT @status, @failed;
        """)
        checks.append(Check("restore parity gate contract populated", len(parity_final) == 2 and parity_final[0] in {"blocked", "failed", "passed"}, "/".join(parity_final)))

        readiness = row(target, f"""
            SET @run_id=NULL; SET @status=NULL; SET @blockers=NULL; SET @warnings=NULL;
            CALL mmo_evaluate_mmo_readiness(UUID_TO_BIN({sql_literal(world_uuid)},1), {sql_literal('steps-15-18-readiness:' + suffix)}, JSON_OBJECT('source','steps-15-18-smoke'), @run_id, @status, @blockers, @warnings);
            SELECT BIN_TO_UUID(@run_id,1), @status, @blockers, @warnings;
        """)
        checks.append(Check("readiness dashboard evaluated", len(readiness) == 4 and readiness[1] in {"red", "yellow", "green"}, "/".join(readiness)))

        header = project_root / "game" / "game" / "mmosemanticevents.h"
        source = project_root / "game" / "game" / "mmosemanticevents.cpp"
        checks.append(Check("C++ semantic event scaffold files", header.exists() and source.exists(), f"{header} / {source}"))

        logout = call_logout(target, session_uuid)
        checks.append(Check("logout", bool(logout), logout))
        checks.append(Check("session closed", active_sessions(target, session_key) == 0, f"active_sessions={active_sessions(target, session_key)}"))
    except Exception:
        if session_uuid:
            try:
                call_logout(target, session_uuid, "smoke cleanup after failure")
            except Exception:
                pass
        raise
    return checks


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate MySQL Gothic MMO production steps 015..018")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--account-name", default="local-import")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--project-root", default=".")
    parser.add_argument("--run-smoke", action="store_true")
    args = parser.parse_args(argv)

    target = parse_url(args.url)
    project_root = Path(args.project_root).resolve()
    checks = validate_objects(target)
    if args.run_smoke:
        checks.extend(run_smoke(target, args.account_name, args.character_key, project_root))

    failed = False
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        failed = failed or not check.ok
    if failed:
        print("\nHint: apply migrations 015..018 after 014 and keep previous steps/import intact.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
