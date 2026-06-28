#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
import time
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
    return Target(
        host=p.hostname or "localhost",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=(p.path or "/").lstrip("/"),
    )


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def json_sql(value: Any) -> str:
    return f"CAST({sql_literal(json.dumps(value, ensure_ascii=False, separators=(',', ':')))} AS JSON)"


def run_mysql(target: Target, sql: str, *, batch: bool = True) -> str:
    cmd = [
        "mysql",
        "--default-character-set=utf8mb4",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-h", target.host,
        "-P", str(target.port),
        "-u", target.user,
        f"-p{target.password}",
        target.database,
    ]
    proc = subprocess.run(cmd, input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def first_row(target: Target, sql: str) -> list[str]:
    out = run_mysql(target, sql)
    if not out:
        return []
    return out.splitlines()[-1].split("\t")


def start_worker_run(target: Target, worker_id: str, run_key: str, max_actions: int) -> str:
    row = first_row(target, f"""
        SET @run_id=NULL;
        CALL mmo_start_server_action_worker_run({sql_literal(worker_id)}, {sql_literal(run_key)}, 'dev_mysql_cli', JSON_OBJECT('max_actions',{int(max_actions)}), @run_id);
        SELECT BIN_TO_UUID(@run_id,1);
    """)
    if not row or row[0] in ("NULL", ""):
        raise RuntimeError("failed to start worker run")
    return row[0]


def finish_worker_run(target: Target, run_uuid: str, failed: bool) -> tuple[str, int]:
    row = first_row(target, f"""
        SET @status=NULL; SET @applied=NULL;
        CALL mmo_finish_server_action_worker_run(UUID_TO_BIN({sql_literal(run_uuid)},1), {sql_literal(failed)}, @status, @applied);
        SELECT @status, @applied;
    """)
    return row[0], int(row[1])


def claim(target: Target, worker_id: str) -> dict[str, Any] | None:
    row = first_row(target, f"""
        SET @action_id=NULL; SET @kind=NULL; SET @session_id=NULL; SET @char_id=NULL; SET @world_id=NULL; SET @target=NULL; SET @idem=NULL; SET @payload=NULL;
        CALL mmo_claim_next_server_action({sql_literal(worker_id)}, @action_id, @kind, @session_id, @char_id, @world_id, @target, @idem, @payload);
        SELECT BIN_TO_UUID(@action_id,1), @kind, BIN_TO_UUID(@session_id,1), BIN_TO_UUID(@char_id,1), BIN_TO_UUID(@world_id,1), @target, @idem, @payload;
    """)
    if not row or row[0] in ("NULL", ""):
        return None
    payload = json.loads(row[7] or "{}")
    return {
        "action_uuid": row[0],
        "kind": row[1],
        "session_uuid": row[2],
        "character_uuid": row[3],
        "world_uuid": row[4],
        "target_key": None if row[5] == "NULL" else row[5],
        "idempotency_key": row[6],
        "payload": payload,
    }


def mark_applied(target: Target, action_uuid: str, event_uuid: str | None, result: dict[str, Any]) -> str:
    row = first_row(target, f"""
        SET @status=NULL;
        CALL mmo_mark_server_action_applied(UUID_TO_BIN({sql_literal(action_uuid)},1), {f"UUID_TO_BIN({sql_literal(event_uuid)},1)" if event_uuid else "NULL"}, {json_sql(result)}, @status);
        SELECT @status;
    """)
    return row[0]


def mark_failed(target: Target, action_uuid: str, code: str, message: str, retryable: bool) -> str:
    row = first_row(target, f"""
        SET @status=NULL;
        CALL mmo_mark_server_action_failed(UUID_TO_BIN({sql_literal(action_uuid)},1), {sql_literal(code)}, {sql_literal(message[:1000])}, {sql_literal(retryable)}, @status);
        SELECT @status;
    """)
    return row[0]


def record_result(target: Target, run_uuid: str, action: dict[str, Any], status: str, event_uuid: str | None, details: dict[str, Any], error_code: str | None = None, error_message: str | None = None) -> None:
    run_mysql(target, f"""
        CALL mmo_record_server_action_worker_result(
          UUID_TO_BIN({sql_literal(run_uuid)},1),
          UUID_TO_BIN({sql_literal(action['action_uuid'])},1),
          {sql_literal(action['kind'])},
          {sql_literal(status)},
          {f"UUID_TO_BIN({sql_literal(event_uuid)},1)" if event_uuid else "NULL"},
          {sql_literal(error_code)},
          {sql_literal(error_message)},
          {json_sql(details)}
        );
    """)


def dispatch(target: Target, action: dict[str, Any]) -> tuple[str | None, dict[str, Any]]:
    kind = action["kind"]
    payload = action["payload"]
    session_uuid = action["session_uuid"]
    idem = action["idempotency_key"]
    server_tick = int(payload.get("server_tick", 0))
    metadata = payload.get("metadata", {"source": "run_mysql_mmo_action_worker"})

    if kind == "wallet_delta":
        row = first_row(target, f"""
            SET @event_id=NULL; SET @amount_after=NULL;
            CALL mmo_adjust_character_wallet(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(payload.get('currency_key','g2notr:gold'))}, {int(payload['delta_amount'])}, {sql_literal(payload.get('reason_key','outbox'))}, {server_tick}, {json_sql(metadata)}, {sql_literal(idem)}, @event_id, @amount_after);
            SELECT BIN_TO_UUID(@event_id,1), @amount_after;
        """)
        return row[0], {"amount_after": row[1]}

    if kind == "grant_gold":
        row = first_row(target, f"""
            SET @event_id=NULL; SET @amount_after=NULL;
            CALL mmo_grant_character_gold(UUID_TO_BIN({sql_literal(session_uuid)},1), {int(payload['amount'])}, {sql_literal(payload.get('reason_key','outbox'))}, {server_tick}, {json_sql(metadata)}, {sql_literal(idem)}, @event_id, @amount_after);
            SELECT BIN_TO_UUID(@event_id,1), @amount_after;
        """)
        return row[0], {"amount_after": row[1]}

    if kind == "spend_gold":
        row = first_row(target, f"""
            SET @event_id=NULL; SET @amount_after=NULL;
            CALL mmo_spend_character_gold(UUID_TO_BIN({sql_literal(session_uuid)},1), {int(payload['amount'])}, {sql_literal(payload.get('reason_key','outbox'))}, {server_tick}, {json_sql(metadata)}, {sql_literal(idem)}, @event_id, @amount_after);
            SELECT BIN_TO_UUID(@event_id,1), @amount_after;
        """)
        return row[0], {"amount_after": row[1]}

    if kind == "consume_mana":
        row = first_row(target, f"""
            SET @event_id=NULL; SET @mana_after=NULL;
            CALL mmo_consume_character_mana(UUID_TO_BIN({sql_literal(session_uuid)},1), {int(payload['mana_amount'])}, {server_tick}, {json_sql(metadata)}, {sql_literal(idem)}, @event_id, @mana_after);
            SELECT BIN_TO_UUID(@event_id,1), @mana_after;
        """)
        return row[0], {"mana_after": row[1]}

    if kind == "apply_character_damage":
        row = first_row(target, f"""
            SET @event_id=NULL; SET @health_after=NULL;
            CALL mmo_apply_character_damage(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(payload['target_character_key'])}, {int(payload['damage_amount'])}, {server_tick}, {json_sql(metadata)}, {sql_literal(idem)}, @event_id, @health_after);
            SELECT BIN_TO_UUID(@event_id,1), @health_after;
        """)
        return row[0], {"health_after": row[1]}

    if kind == "set_script_int":
        row = first_row(target, f"""
            SET @event_id=NULL; SET @value_after=NULL;
            CALL mmo_set_character_script_int(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(payload['script_key'])}, {int(payload.get('symbol_index',0))}, {int(payload.get('value_index',0))}, {int(payload['value_int'])}, {server_tick}, {json_sql(metadata)}, {sql_literal(idem)}, @event_id, @value_after);
            SELECT BIN_TO_UUID(@event_id,1), @value_after;
        """)
        return row[0], {"value_after": row[1]}

    raise NotImplementedError(f"unsupported action_kind for dev worker: {kind}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Dev MySQL outbox worker for Gothic MMO semantic actions.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--worker-id", default="dev-mysql-worker")
    ap.add_argument("--run-key", default=None)
    ap.add_argument("--max-actions", type=int, default=10)
    ap.add_argument("--sleep-empty-ms", type=int, default=0)
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    run_key = args.run_key or f"worker-{args.worker_id}-{int(time.time())}"
    run_uuid = start_worker_run(target, args.worker_id, run_key, args.max_actions)
    failed = False

    for _ in range(max(args.max_actions, 0)):
        action = claim(target, args.worker_id)
        if action is None:
            if args.sleep_empty_ms > 0:
                time.sleep(args.sleep_empty_ms / 1000.0)
            break
        record_result(target, run_uuid, action, "claimed", None, {"claimed": True})
        try:
            event_uuid, result = dispatch(target, action)
            status = mark_applied(target, action["action_uuid"], event_uuid, result)
            record_result(target, run_uuid, action, status, event_uuid, result)
            print(f"[APPLIED] {action['kind']} action={action['action_uuid']} event={event_uuid}")
        except Exception as exc:  # dev tool: persist failure and keep going
            failed = True
            try:
                status = mark_failed(target, action["action_uuid"], type(exc).__name__, str(exc), True)
                record_result(target, run_uuid, action, status, None, {"exception": type(exc).__name__}, type(exc).__name__, str(exc))
            finally:
                print(f"[FAILED] {action['kind']} action={action['action_uuid']} error={exc}", file=sys.stderr)

    status, applied = finish_worker_run(target, run_uuid, failed)
    print(f"[RUN] {run_uuid} status={status} applied={applied}")
    return 0 if status == "finished" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
