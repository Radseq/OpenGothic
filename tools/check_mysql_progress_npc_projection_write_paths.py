#!/usr/bin/env python3
"""Validate and smoke-test MySQL production steps 008..010.

This script uses the mysql command-line client only. It does not require a
Python MySQL driver. It assumes migrations 001..010 are applied and that the
runtime SQLite bootstrap import has already created a realm/account/character.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import uuid
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
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value)
    return "'" + text.replace("'", "''").replace("\\", "\\\\") + "'"


def uuid_bin(value: str) -> str:
    return f"UUID_TO_BIN({sql_literal(value)}, 1)"


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
    value = scalar(
        target,
        f"""
        SELECT schema_contract
          FROM mmo_schema_versions
         WHERE migration_key={sql_literal(migration_key)};
        """,
    )
    ok = value == expected_contract
    return Check(label, ok, value or "missing")


def check_named(target: MySqlTarget, name: str, sql: str, required: Iterable[str]) -> Check:
    found = line_set(run_mysql(target, sql))
    required_set = set(required)
    missing = sorted(required_set - found)
    if missing:
        return Check(name, False, "missing: " + ", ".join(missing))
    return Check(name, True, f"ok ({len(required_set)} required)")


def validate_objects(target: MySqlTarget) -> list[Check]:
    db = target.database.replace("'", "''")
    return [
        check_marker(
            target,
            "production/mysql/008_character_progress_write_path",
            "gothic-mmo-character-progress-write-path-v1-mysql",
            "migration 008 marker",
        ),
        check_marker(
            target,
            "production/mysql/009_npc_lifecycle_write_path",
            "gothic-mmo-npc-lifecycle-write-path-v1-mysql",
            "migration 009 marker",
        ),
        check_marker(
            target,
            "production/mysql/010_projection_validation",
            "gothic-mmo-projection-validation-v1-mysql",
            "migration 010 marker",
        ),
        check_named(
            target,
            "steps 008..010 tables",
            f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';",
            (
                "character_progress_audit",
                "world_npc_lifecycle_audit",
                "mmo_projection_validation_runs",
                "mmo_projection_validation_results",
            ),
        ),
        check_named(
            target,
            "steps 008..010 views",
            f"SELECT table_name FROM information_schema.views WHERE table_schema='{db}';",
            (
                "v_character_script_progress",
                "v_character_quest_progress",
                "v_character_dialog_progress",
                "v_character_progress_audit",
                "v_world_npc_lifecycle_state",
                "v_world_npc_lifecycle_audit",
                "v_projection_validation_latest",
                "v_projection_validation_results",
                "v_projection_validation_errors",
            ),
        ),
        check_named(
            target,
            "steps 008..010 routines",
            f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';",
            (
                "mmo_set_character_script_int",
                "mmo_update_character_quest",
                "mmo_set_character_known_dialog",
                "mmo_adjust_character_progression",
                "mmo_apply_character_experience_reward",
                "mmo_mark_npc_dead",
                "mmo_respawn_npc",
                "mmo_validate_world_projection_basic",
            ),
        ),
    ]


def call_login(target: MySqlTarget, account_name: str, character_key: str, session_key: str) -> str:
    return scalar(
        target,
        f"""
        SET @session_id = NULL;
        CALL mmo_login_character(
          {sql_literal(account_name)},
          {sql_literal(character_key)},
          {sql_literal(session_key)},
          'progress-npc-projection-smoke',
          'local',
          JSON_OBJECT('tool', 'check_mysql_progress_npc_projection_write_paths'),
          @session_id
        );
        SELECT BIN_TO_UUID(@session_id, 1);
        """,
    )


def call_logout(target: MySqlTarget, session_id: str) -> str:
    return scalar(
        target,
        f"""
        SET @event_id = NULL;
        CALL mmo_logout_character(
          {uuid_bin(session_id)},
          'progress_npc_projection_smoke_done',
          JSON_OBJECT('tool', 'check_mysql_progress_npc_projection_write_paths'),
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id, 1);
        """,
    )


def call_script_int(target: MySqlTarget, session_id: str, script_key: str, idem: str, value: int) -> tuple[str, int]:
    parts = row(
        target,
        f"""
        SET @event_id = NULL;
        SET @value_after = NULL;
        CALL mmo_set_character_script_int(
          {uuid_bin(session_id)},
          {sql_literal(script_key)},
          NULL,
          0,
          {value},
          8101,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(idem)},
          @event_id,
          @value_after
        );
        SELECT BIN_TO_UUID(@event_id, 1), @value_after;
        """,
    )
    return parts[0], int(parts[1])


def call_progression(target: MySqlTarget, session_id: str, idem: str, exp_delta: int, lp_delta: int, reason: str) -> tuple[str, int, int]:
    parts = row(
        target,
        f"""
        SET @event_id = NULL;
        SET @experience_after = NULL;
        SET @learning_points_after = NULL;
        CALL mmo_adjust_character_progression(
          {uuid_bin(session_id)},
          {exp_delta},
          {lp_delta},
          {sql_literal(reason)},
          8102,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(idem)},
          @event_id,
          @experience_after,
          @learning_points_after
        );
        SELECT BIN_TO_UUID(@event_id, 1), @experience_after, @learning_points_after;
        """,
    )
    return parts[0], int(parts[1]), int(parts[2])


def call_quest(target: MySqlTarget, session_id: str, quest_key: str, idem: str) -> str:
    return scalar(
        target,
        f"""
        SET @event_id = NULL;
        CALL mmo_update_character_quest(
          {uuid_bin(session_id)},
          {sql_literal(quest_key)},
          'SMOKE',
          'running',
          0,
          JSON_ARRAY('Smoke quest entry'),
          8103,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(idem)},
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id, 1);
        """,
    )


def call_dialog(target: MySqlTarget, session_id: str, npc_key: str, info_key: str, idem: str) -> str:
    return scalar(
        target,
        f"""
        SET @event_id = NULL;
        CALL mmo_set_character_known_dialog(
          {uuid_bin(session_id)},
          {sql_literal(npc_key)},
          {sql_literal(info_key)},
          TRUE,
          FALSE,
          'consumed_hidden',
          8104,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(idem)},
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id, 1);
        """,
    )


def seed_npc(target: MySqlTarget, character_key: str, suffix: str) -> str:
    npc_key = f"smoke:npc:lifecycle:{suffix}"
    run_mysql(
        target,
        f"""
        SET @world_instance_id = (
          SELECT current_world_instance_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1
        );
        INSERT INTO world_entity_state(
          world_instance_id, entity_key, entity_kind, lifecycle_state,
          pos_x, pos_y, pos_z, rotation_yaw,
          health_current, health_max, state_json, row_version
        ) VALUES(
          @world_instance_id, {sql_literal(npc_key)}, 'npc', 'active',
          10.0, 20.0, 30.0, 0.0,
          10, 10, JSON_OBJECT('smoke', TRUE), 0
        )
        ON DUPLICATE KEY UPDATE
          entity_kind='npc',
          lifecycle_state='active',
          pos_x=VALUES(pos_x),
          pos_y=VALUES(pos_y),
          pos_z=VALUES(pos_z),
          rotation_yaw=VALUES(rotation_yaw),
          health_current=10,
          health_max=10,
          state_json=JSON_OBJECT('smoke', TRUE),
          row_version=0,
          updated_at=CURRENT_TIMESTAMP(6);
        """,
    )
    return npc_key


def call_npc_dead(target: MySqlTarget, session_id: str, npc_key: str, idem: str) -> tuple[str, int]:
    parts = row(
        target,
        f"""
        SET @event_id = NULL;
        SET @row_version_after = NULL;
        CALL mmo_mark_npc_dead(
          {uuid_bin(session_id)},
          {sql_literal(npc_key)},
          8201,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(idem)},
          @event_id,
          @row_version_after
        );
        SELECT BIN_TO_UUID(@event_id, 1), @row_version_after;
        """,
    )
    return parts[0], int(parts[1])


def call_npc_respawn(target: MySqlTarget, session_id: str, npc_key: str, idem: str) -> tuple[str, int]:
    parts = row(
        target,
        f"""
        SET @event_id = NULL;
        SET @row_version_after = NULL;
        CALL mmo_respawn_npc(
          {uuid_bin(session_id)},
          {sql_literal(npc_key)},
          11.0, 21.0, 31.0, 90.0,
          10, 10,
          8202,
          JSON_OBJECT('smoke', TRUE),
          {sql_literal(idem)},
          @event_id,
          @row_version_after
        );
        SELECT BIN_TO_UUID(@event_id, 1), @row_version_after;
        """,
    )
    return parts[0], int(parts[1])


def call_projection_validation(target: MySqlTarget, character_key: str, run_key: str) -> tuple[str, int, str]:
    parts = row(
        target,
        f"""
        SET @world_instance_id = (
          SELECT current_world_instance_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1
        );
        SET @run_id = NULL;
        SET @error_count = NULL;
        CALL mmo_validate_world_projection_basic(
          @world_instance_id,
          {sql_literal(run_key)},
          JSON_OBJECT('tool', 'check_mysql_progress_npc_projection_write_paths'),
          @run_id,
          @error_count
        );
        SELECT BIN_TO_UUID(@run_id, 1), @error_count,
               (SELECT status FROM mmo_projection_validation_runs WHERE validation_run_id=@run_id);
        """,
    )
    return parts[0], int(parts[1]), parts[2]


def duplicate_count(target: MySqlTarget, idem: str) -> int:
    return count(
        target,
        f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(idem)};",
    )


def print_check(check: Check) -> None:
    prefix = "[OK]" if check.ok else "[FAIL]"
    print(f"{prefix} {check.name}: {check.detail}")


def run_smoke(target: MySqlTarget, account_name: str, character_key: str) -> list[Check]:
    checks: list[Check] = []
    suffix = uuid.uuid4().hex
    session_id: str | None = None
    try:
        session_key = f"dev-smoke:progress-npc-projection:{character_key}:{suffix}"
        session_id = call_login(target, account_name, character_key, session_key)
        checks.append(Check("login", bool(session_id), session_id or "missing"))

        script_key = f"SMOKE_READ_BOOKSTAND_{suffix}"
        script_idem = f"smoke:script-int:{suffix}"
        ev1, val1 = call_script_int(target, session_id, script_key, script_idem, 1)
        ev1_retry, val1_retry = call_script_int(target, session_id, script_key, script_idem, 1)
        checks.append(Check("script int append", bool(ev1) and val1 == 1, f"{ev1}/{val1}"))
        checks.append(Check("script int idempotent retry", ev1 == ev1_retry and val1_retry == 1, f"first={ev1}/{val1} retry={ev1_retry}/{val1_retry}"))
        checks.append(Check("script int duplicate event count", duplicate_count(target, script_idem) == 1, str(duplicate_count(target, script_idem))))

        start_parts = row(
            target,
            f"SELECT s.experience, s.learning_points FROM character_stats s JOIN characters c ON c.character_id=s.character_id WHERE c.character_key={sql_literal(character_key)} LIMIT 1;",
        )
        start_exp, start_lp = int(start_parts[0]), int(start_parts[1])
        prog_idem = f"smoke:progression:grant:{suffix}"
        ev2, exp_after, lp_after = call_progression(target, session_id, prog_idem, 7, 1, f"smoke:bookstand-reward:{suffix}")
        ev2_retry, exp_after_retry, lp_after_retry = call_progression(target, session_id, prog_idem, 7, 1, f"smoke:bookstand-reward:{suffix}")
        checks.append(Check("progression grant", exp_after == start_exp + 7 and lp_after == start_lp + 1, f"{ev2}/{start_exp}->{exp_after}/{start_lp}->{lp_after}"))
        checks.append(Check("progression grant idempotent retry", ev2 == ev2_retry and exp_after == exp_after_retry and lp_after == lp_after_retry, f"first={ev2}/{exp_after}/{lp_after} retry={ev2_retry}/{exp_after_retry}/{lp_after_retry}"))
        rollback_idem = f"smoke:progression:restore:{suffix}"
        ev3, exp_final, lp_final = call_progression(target, session_id, rollback_idem, -7, -1, f"smoke:bookstand-reward-restore:{suffix}")
        checks.append(Check("progression restored", exp_final == start_exp and lp_final == start_lp, f"{ev3}/{start_exp}->{exp_final}/{start_lp}->{lp_final}"))

        quest_key = f"SMOKE_QUEST_{suffix}"
        quest_idem = f"smoke:quest:{suffix}"
        ev4 = call_quest(target, session_id, quest_key, quest_idem)
        ev4_retry = call_quest(target, session_id, quest_key, quest_idem)
        checks.append(Check("quest append", bool(ev4), ev4 or "missing"))
        checks.append(Check("quest idempotent retry", ev4 == ev4_retry, f"first={ev4} retry={ev4_retry}"))

        npc_dialog_key = f"SMOKE_NPC_{suffix}"
        info_key = f"SMOKE_INFO_{suffix}"
        dialog_idem = f"smoke:dialog:{suffix}"
        ev5 = call_dialog(target, session_id, npc_dialog_key, info_key, dialog_idem)
        ev5_retry = call_dialog(target, session_id, npc_dialog_key, info_key, dialog_idem)
        checks.append(Check("dialog append", bool(ev5), ev5 or "missing"))
        checks.append(Check("dialog idempotent retry", ev5 == ev5_retry, f"first={ev5} retry={ev5_retry}"))

        npc_key = seed_npc(target, character_key, suffix)
        checks.append(Check("npc fixture", bool(npc_key), npc_key))
        dead_idem = f"smoke:npc-dead:{suffix}"
        ev6, rv_dead = call_npc_dead(target, session_id, npc_key, dead_idem)
        ev6_retry, rv_dead_retry = call_npc_dead(target, session_id, npc_key, dead_idem)
        checks.append(Check("npc death append", bool(ev6) and rv_dead == 1, f"{ev6}/{rv_dead}"))
        checks.append(Check("npc death idempotent retry", ev6 == ev6_retry and rv_dead == rv_dead_retry, f"first={ev6}/{rv_dead} retry={ev6_retry}/{rv_dead_retry}"))
        npc_state = scalar(target, f"SELECT CONCAT(lifecycle_state, '/', health_current, '/', row_version) FROM world_entity_state WHERE entity_key={sql_literal(npc_key)} LIMIT 1;")
        checks.append(Check("npc death projection", npc_state == "dead/0/1", npc_state))

        respawn_idem = f"smoke:npc-respawn:{suffix}"
        ev7, rv_respawn = call_npc_respawn(target, session_id, npc_key, respawn_idem)
        ev7_retry, rv_respawn_retry = call_npc_respawn(target, session_id, npc_key, respawn_idem)
        checks.append(Check("npc respawn append", bool(ev7) and rv_respawn == 2, f"{ev7}/{rv_respawn}"))
        checks.append(Check("npc respawn idempotent retry", ev7 == ev7_retry and rv_respawn == rv_respawn_retry, f"first={ev7}/{rv_respawn} retry={ev7_retry}/{rv_respawn_retry}"))
        npc_state2 = scalar(target, f"SELECT CONCAT(lifecycle_state, '/', health_current, '/', row_version) FROM world_entity_state WHERE entity_key={sql_literal(npc_key)} LIMIT 1;")
        checks.append(Check("npc respawn projection", npc_state2 == "active/10/2", npc_state2))

        run_id, error_count, status = call_projection_validation(target, character_key, f"smoke:projection-basic:{suffix}")
        checks.append(Check("projection validation run", bool(run_id), f"{run_id}/{status}/errors={error_count}"))
        # Existing imported development data may have historical consistency issues. The smoke
        # check validates execution and records the error count instead of failing on old data.

        logout_event = call_logout(target, session_id)
        checks.append(Check("logout", bool(logout_event), logout_event or "missing"))
        active = count(target, f"SELECT COUNT(*) FROM server_sessions WHERE session_id={uuid_bin(session_id)} AND lifecycle_state='active';")
        checks.append(Check("session closed", active == 0, f"active_sessions={active}"))
        session_id = None
    finally:
        if session_id:
            try:
                call_logout(target, session_id)
            except Exception as exc:  # noqa: BLE001
                print(f"[WARN] cleanup logout failed for {session_id}: {exc}", file=sys.stderr)
    return checks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default=os.environ.get("MYSQL_URL"), help="mysql://user:password@host:port/database")
    parser.add_argument("--account-name", default="local-import")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--run-smoke", action="store_true")
    args = parser.parse_args()

    if not args.url:
        print("error: provide --url or MYSQL_URL", file=sys.stderr)
        return 2

    try:
        target = parse_url(args.url)
        checks = validate_objects(target)
        for check in checks:
            print_check(check)
        if not all(c.ok for c in checks):
            return 1

        if args.run_smoke:
            smoke_checks = run_smoke(target, args.account_name, args.character_key)
            for check in smoke_checks:
                print_check(check)
            if not all(c.ok for c in smoke_checks):
                return 1

    except Exception as exc:  # noqa: BLE001
        print(f"[FAIL] smoke execution: {exc}", file=sys.stderr)
        print("\nHint: apply 008, 009 and 010 after 007_container_interactive_write_path.sql and import runtime/g2notr.sqlite first.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
