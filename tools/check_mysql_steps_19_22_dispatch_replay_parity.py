#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import uuid
from dataclasses import dataclass
from typing import Any
from urllib.parse import urlparse, unquote


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme != "mysql":
        raise ValueError("expected mysql:// URL")
    return Target(p.hostname or "localhost", p.port or 3306, unquote(p.username or ""), unquote(p.password or ""), (p.path or "/").lstrip("/"))


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''") + "'"


def json_sql(value: Any) -> str:
    return f"CAST({sql_literal(json.dumps(value, ensure_ascii=False, separators=(',', ':')))} AS JSON)"


def run_mysql(target: Target, sql: str) -> str:
    cmd = [
        "mysql", "--default-character-set=utf8mb4", "--batch", "--raw", "--skip-column-names",
        "-h", target.host, "-P", str(target.port), "-u", target.user, f"-p{target.password}", target.database,
    ]
    proc = subprocess.run(cmd, input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def row(target: Target, sql: str) -> list[str]:
    out = run_mysql(target, sql)
    if not out:
        return []
    return out.splitlines()[-1].split("\t")


def scalar(target: Target, sql: str) -> str:
    r = row(target, sql)
    return r[0] if r else ""


def check(ok: bool, name: str, detail: str) -> tuple[bool, str, str]:
    return ok, name, detail


def schema_checks(target: Target) -> list[tuple[bool,str,str]]:
    out: list[tuple[bool,str,str]] = []
    expected = {
        "production/mysql/019_server_action_dispatch_contract": "gothic-mmo-server-action-dispatch-contract-v1-mysql",
        "production/mysql/020_server_action_worker_observability": "gothic-mmo-server-action-worker-observability-v1-mysql",
        "production/mysql/021_strict_replay_journal_audit": "gothic-mmo-strict-replay-journal-audit-v1-mysql",
        "production/mysql/022_restore_parity_artifacts": "gothic-mmo-restore-parity-artifacts-v1-mysql",
    }
    for key, contract in expected.items():
        got = scalar(target, f"SELECT schema_contract FROM mmo_schema_versions WHERE migration_key={sql_literal(key)} LIMIT 1;")
        out.append(check(got == contract, f"migration {key.split('/')[-1].split('_')[0]} marker", got or "missing"))

    tables = [
        "mmo_server_action_dispatch_contracts",
        "mmo_server_action_worker_runs",
        "mmo_server_action_worker_results",
        "mmo_restore_parity_artifacts",
    ]
    got_tables = int(scalar(target, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name IN (" + ",".join(sql_literal(t) for t in tables) + ");") or "0")
    out.append(check(got_tables == len(tables), "steps 019..022 tables", f"{got_tables}/{len(tables)}"))

    views = [
        "v_server_action_dispatch_contracts", "v_server_action_dispatch_gaps", "v_claimable_server_actions",
        "v_server_action_worker_latest_runs", "v_server_action_worker_latest_results",
        "v_strict_replay_latest_results", "v_strict_replay_latest_errors",
        "v_restore_parity_artifact_comparison", "v_restore_parity_artifact_failures",
    ]
    got_views = int(scalar(target, "SELECT COUNT(*) FROM information_schema.views WHERE table_schema=DATABASE() AND table_name IN (" + ",".join(sql_literal(v) for v in views) + ");") or "0")
    out.append(check(got_views == len(views), "steps 019..022 views", f"{got_views}/{len(views)}"))

    routines = [
        "mmo_validate_server_action_dispatch_contracts", "mmo_claim_next_server_action", "mmo_requeue_stale_claimed_actions",
        "mmo_start_server_action_worker_run", "mmo_record_server_action_worker_result", "mmo_finish_server_action_worker_run",
        "mmo_audit_strict_replay_journal", "mmo_record_restore_parity_artifact", "mmo_materialize_restore_parity_artifact_results",
    ]
    got_routines = int(scalar(target, "SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=DATABASE() AND routine_name IN (" + ",".join(sql_literal(r) for r in routines) + ");") or "0")
    out.append(check(got_routines == len(routines), "steps 019..022 routines", f"{got_routines}/{len(routines)}"))
    return out


def run_smoke(target: Target, account_name: str, character_key: str) -> list[tuple[bool,str,str]]:
    out: list[tuple[bool,str,str]] = []
    suffix = uuid.uuid4().hex[:12]
    session_uuid = None

    try:
        login = row(target, f"""
            SET @session_id=NULL;
            CALL mmo_login_character(
              {sql_literal(account_name)},
              {sql_literal(character_key)},
              {sql_literal('steps-19-22-' + suffix)},
              'steps-19-22-smoke',
              '127.0.0.1',
              JSON_OBJECT('source','steps-19-22-smoke'),
              @session_id
            );
            SELECT BIN_TO_UUID(@session_id,1),
                   BIN_TO_UUID((SELECT login_event_id FROM server_sessions WHERE session_id=@session_id LIMIT 1),1);
        """)
        session_uuid = login[0]
        out.append(check(bool(session_uuid and session_uuid != "NULL"), "login", session_uuid))

        contract = row(target, "SET @e=NULL; SET @w=NULL; CALL mmo_validate_server_action_dispatch_contracts(@e,@w); SELECT @e,@w;")
        out.append(check(contract[0] == "0", "dispatch contract validation", f"errors={contract[0]} warnings={contract[1]}"))

        enqueue = row(target, f"""
            SET @action_id=NULL; SET @status=NULL;
            CALL mmo_enqueue_server_action(
              UUID_TO_BIN({sql_literal(session_uuid)},1),
              'wallet_delta',
              {sql_literal(character_key)},
              JSON_OBJECT('currency_key','g2notr:gold','delta_amount',2,'reason_key','steps-19-22-smoke','server_tick',19002,'metadata',JSON_OBJECT('source','steps-19-22-smoke')),
              {sql_literal('smoke:19:wallet-delta:' + suffix)},
              10,
              3,
              @action_id,
              @status
            );
            SELECT BIN_TO_UUID(@action_id,1), @status;
        """)
        action_uuid = enqueue[0]
        out.append(check(enqueue[1] in ("pending", "applied"), "server action enqueue", f"{action_uuid}/{enqueue[1]}"))

        worker = row(target, f"""
            SET @worker_run_id=NULL;
            CALL mmo_start_server_action_worker_run('steps-19-22-smoke', {sql_literal('smoke:worker:' + suffix)}, 'smoke', JSON_OBJECT('source','steps-19-22-smoke'), @worker_run_id);
            SELECT BIN_TO_UUID(@worker_run_id,1);
        """)
        worker_uuid = worker[0]
        out.append(check(worker_uuid != "NULL", "worker run started", worker_uuid))

        claimed = row(target, """
            SET @claim_action_id=NULL; SET @kind=NULL; SET @claim_session=NULL; SET @claim_char=NULL; SET @claim_world=NULL; SET @target=NULL; SET @idem=NULL; SET @payload=NULL;
            CALL mmo_claim_next_server_action('steps-19-22-smoke', @claim_action_id, @kind, @claim_session, @claim_char, @claim_world, @target, @idem, @payload);
            SELECT BIN_TO_UUID(@claim_action_id,1), @kind, BIN_TO_UUID(@claim_session,1), @idem, @payload;
        """)
        out.append(check(claimed[0] == action_uuid and claimed[1] == "wallet_delta", "server action claim", f"{claimed[0]}/{claimed[1]}"))

        applied = row(target, f"""
            SET @event_id=NULL; SET @amount_after=NULL; SET @status=NULL;
            CALL mmo_adjust_character_wallet(UUID_TO_BIN({sql_literal(session_uuid)},1), 'g2notr:gold', 2, 'steps-19-22-smoke', 19002, JSON_OBJECT('source','steps-19-22-smoke'), {sql_literal('smoke:19:wallet-delta:' + suffix)}, @event_id, @amount_after);
            CALL mmo_mark_server_action_applied(UUID_TO_BIN({sql_literal(action_uuid)},1), @event_id, JSON_OBJECT('amount_after', @amount_after), @status);
            CALL mmo_record_server_action_worker_result(UUID_TO_BIN({sql_literal(worker_uuid)},1), UUID_TO_BIN({sql_literal(action_uuid)},1), 'wallet_delta', @status, @event_id, NULL, NULL, JSON_OBJECT('amount_after', @amount_after));
            SELECT BIN_TO_UUID(@event_id,1), @amount_after, @status;
        """)
        event_uuid = applied[0]
        out.append(check(applied[2] == "applied", "server action applied", f"{event_uuid}/{applied[1]}/{applied[2]}"))

        finish = row(target, f"""
            SET @status=NULL; SET @applied=NULL;
            CALL mmo_finish_server_action_worker_run(UUID_TO_BIN({sql_literal(worker_uuid)},1), FALSE, @status, @applied);
            SELECT @status,@applied;
        """)
        out.append(check(finish[0] == "finished" and finish[1] == "1", "worker run finished", f"{finish[0]}/{finish[1]}"))

        replay = row(target, f"""
            SET @world_id=(SELECT world_instance_id FROM server_sessions WHERE session_id=UUID_TO_BIN({sql_literal(session_uuid)},1));
            SET @run_id=NULL; SET @errors=NULL; SET @warnings=NULL;
            CALL mmo_audit_strict_replay_journal(@world_id, {sql_literal('smoke:strict-replay:' + suffix)}, JSON_OBJECT('source','steps-19-22-smoke'), @run_id, @errors, @warnings);
            SELECT BIN_TO_UUID(@run_id,1), @errors, @warnings;
        """)
        out.append(check(replay[1] == "0", "strict replay journal audit", f"{replay[0]}/errors={replay[1]}/warnings={replay[2]}"))

        parity = row(target, f"""
            SET @world_id=(SELECT world_instance_id FROM server_sessions WHERE session_id=UUID_TO_BIN({sql_literal(session_uuid)},1));
            SET @parity_run_id=NULL;
            CALL mmo_start_restore_parity_run(@world_id, {sql_literal(character_key)}, {sql_literal('smoke:artifact-parity:' + suffix)}, JSON_OBJECT('source','steps-19-22-smoke'), @parity_run_id);
            CALL mmo_record_restore_parity_artifact(@parity_run_id, 'bookstand_script_xp', 'native_sav', 'smoke-native', 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 1, JSON_OBJECT('source','smoke'));
            CALL mmo_record_restore_parity_artifact(@parity_run_id, 'bookstand_script_xp', 'sqlite_snapshot', 'smoke-sqlite', 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 1, JSON_OBJECT('source','smoke'));
            CALL mmo_record_restore_parity_artifact(@parity_run_id, 'bookstand_script_xp', 'mysql_projection', 'smoke-mysql', 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 1, JSON_OBJECT('source','smoke'));
            SET @mat=NULL; SET @failed=NULL; SET @final_status=NULL; SET @final_failed=NULL;
            CALL mmo_materialize_restore_parity_artifact_results(@parity_run_id, @mat, @failed);
            CALL mmo_finalize_restore_parity_run(@parity_run_id, @final_status, @final_failed);
            SELECT BIN_TO_UUID(@parity_run_id,1), @mat, @failed, @final_status, @final_failed;
        """)
        out.append(check(parity[2] == "0", "restore parity artifact comparison", f"{parity[0]}/materialized={parity[1]}/failed={parity[2]}/final={parity[3]}"))

    finally:
        if session_uuid:
            try:
                logout = row(target, f"""
                    SET @event_id=NULL;
                    CALL mmo_logout_character(UUID_TO_BIN({sql_literal(session_uuid)},1), 'steps-19-22-smoke', JSON_OBJECT('source','steps-19-22-smoke'), @event_id);
                    SELECT BIN_TO_UUID(@event_id,1);
                """)
                out.append(check(logout and logout[0] != "NULL", "logout", logout[0]))
                active = scalar(target, f"SELECT COUNT(*) FROM server_sessions WHERE session_id=UUID_TO_BIN({sql_literal(session_uuid)},1) AND lifecycle_state='active';")
                out.append(check(active == "0", "session closed", f"active_sessions={active}"))
            except Exception as exc:
                out.append(check(False, "cleanup logout", str(exc)))
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", required=True)
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--run-smoke", action="store_true")
    args = ap.parse_args(argv)
    target = parse_mysql_url(args.url)

    checks = schema_checks(target)
    if args.run_smoke:
        checks.extend(run_smoke(target, args.account_name, args.character_key))

    ok = True
    for passed, name, detail in checks:
        ok = ok and passed
        print(f"[{'OK' if passed else 'FAIL'}] {name}: {detail}")
    if not ok:
        print("\nHint: apply migrations 019..022 after 018 and keep previous steps/import intact.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
