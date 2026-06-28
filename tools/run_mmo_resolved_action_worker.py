#!/usr/bin/env python3
"""Resolve and dispatch MMO semantic actions from mmo_server_action_outbox.

This is a dev server worker, not game-thread code. It claims pending actions,
resolves OpenGothic engine keys to MySQL projection rows, calls the existing
mmo_* stored procedures, and marks outbox rows applied/failed.

Supported real slices:
- pickup_world_item -> mmo_pickup_world_item(...)
- remove_world_item -> mmo_remove_world_item(...)
- equip_character_item -> mmo_equip_character_item(...)
- unequip_character_item -> mmo_unequip_character_item(...)
- set_script_int -> mmo_set_character_script_int(...)
- adjust_progression -> mmo_adjust_character_progression(...)
- apply_experience_reward -> mmo_apply_character_experience_reward(...)
- update_quest -> mmo_update_character_quest(...)
- set_known_dialog -> mmo_set_character_known_dialog(...)
- consume_mana -> mmo_consume_character_mana(...)
- consume_item -> mmo_consume_character_item(...)
- apply_character_damage -> mmo_apply_character_damage(...)
- apply_world_entity_damage -> mmo_apply_world_entity_damage(...)
- mark_npc_dead -> mmo_mark_npc_dead(...)
- trade_sell_to_npc -> mmo_trade_sell_to_npc(...)
- trade_buy_from_npc -> mmo_trade_buy_from_npc(...)
- character_checkpoint -> mmo_checkpoint_character_state(...)

The resolver is intentionally conservative. If a client envelope cannot be
resolved uniquely, the action is failed as non-retryable unless --retry-unresolved
is given. For pickup_world_item, the worker allocates the first free
character_inventory.bag_index from the server projection instead of defaulting
to a client slot. Do not fake UUIDs or mark parity green from unresolved rows.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Any
from urllib.parse import unquote, urlparse

WORKER_MODE = "resolved_dev_mysql_cli"


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


@dataclass
class Action:
    action_uuid: str
    kind: str
    session_uuid: str
    character_uuid: str
    world_uuid: str
    target_key: str | None
    idempotency_key: str
    payload: dict[str, Any]


class ResolveError(RuntimeError):
    pass


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    db = (p.path or "/").lstrip("/")
    if not db:
        raise ValueError("database is missing in mysql URL")
    return Target(
        host=p.hostname or "localhost",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=db,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-h", target.host,
        "-P", str(target.port),
        "-u", target.user,
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


def first_row(target: Target, sql: str) -> list[str]:
    out = run_mysql(target, sql)
    if not out:
        return []
    return out.splitlines()[-1].split("\t")


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def json_sql(value: Any) -> str:
    return f"CAST({sql_literal(json.dumps(value, ensure_ascii=False, separators=(',', ':')))} AS JSON)"


def bin_uuid(uuid: str | None) -> str:
    if not uuid or uuid == "NULL":
        return "NULL"
    return f"UUID_TO_BIN({sql_literal(uuid)},1)"


def start_worker_run(target: Target, worker_id: str, run_key: str, max_actions: int) -> str:
    row = first_row(target, f"""
        SET @run_id=NULL;
        CALL mmo_start_server_action_worker_run({sql_literal(worker_id)}, {sql_literal(run_key)}, {sql_literal(WORKER_MODE)}, JSON_OBJECT('max_actions',{int(max_actions)}), @run_id);
        SELECT BIN_TO_UUID(@run_id,1);
    """)
    if not row or row[0] in ("", "NULL"):
        raise RuntimeError("failed to start worker run")
    return row[0]


def finish_worker_run(target: Target, run_uuid: str, failed: bool) -> tuple[str, int]:
    row = first_row(target, f"""
        SET @status=NULL; SET @applied=NULL;
        CALL mmo_finish_server_action_worker_run(UUID_TO_BIN({sql_literal(run_uuid)},1), {sql_literal(failed)}, @status, @applied);
        SELECT @status, @applied;
    """)
    return row[0], int(row[1])


def _action_from_row(row: list[str]) -> Action | None:
    if not row or row[0] in ("", "NULL"):
        return None
    payload = json.loads(row[7] or "{}")
    return Action(
        action_uuid=row[0],
        kind=row[1],
        session_uuid=row[2],
        character_uuid=row[3],
        world_uuid=row[4],
        target_key=None if row[5] == "NULL" else row[5],
        idempotency_key=row[6],
        payload=payload if isinstance(payload, dict) else {},
    )


def claim(target: Target, worker_id: str) -> Action | None:
    row = first_row(target, f"""
        SET @action_id=NULL; SET @kind=NULL; SET @session_id=NULL; SET @char_id=NULL; SET @world_id=NULL; SET @target=NULL; SET @idem=NULL; SET @payload=NULL;
        CALL mmo_claim_next_server_action({sql_literal(worker_id)}, @action_id, @kind, @session_id, @char_id, @world_id, @target, @idem, @payload);
        SELECT BIN_TO_UUID(@action_id,1), @kind, BIN_TO_UUID(@session_id,1), BIN_TO_UUID(@char_id,1), BIN_TO_UUID(@world_id,1), @target, @idem, @payload;
    """)
    return _action_from_row(row)


def claim_matching_prefix(target: Target, worker_id: str, idempotency_prefix: str) -> Action | None:
    # Filtered claim for dev replay sessions. The stock stored procedure claims the
    # oldest pending action globally, which is dangerous while many local smoke
    # sessions leave pending rows behind. This keeps one test run isolated without
    # changing schema/procedures.
    like = idempotency_prefix + ":%"
    row = first_row(target, f"""
        START TRANSACTION;
        SET @action_id=NULL; SET @kind=NULL; SET @session_id=NULL; SET @char_id=NULL; SET @world_id=NULL; SET @target=NULL; SET @idem=NULL; SET @payload=NULL;
        SELECT action_id, action_kind, session_id, character_id, world_instance_id, target_key, idempotency_key, request_payload
          INTO @action_id, @kind, @session_id, @char_id, @world_id, @target, @idem, @payload
          FROM mmo_server_action_outbox
         WHERE status='pending'
           AND idempotency_key LIKE {sql_literal(like)}
         ORDER BY priority ASC, requested_at ASC, action_id ASC
         LIMIT 1
         FOR UPDATE SKIP LOCKED;
        UPDATE mmo_server_action_outbox
           SET status='claimed',
               locked_at=CURRENT_TIMESTAMP(6),
               result_payload=JSON_MERGE_PATCH(COALESCE(result_payload,JSON_OBJECT()), JSON_OBJECT('claimed_by',{sql_literal(worker_id)}, 'claim_filter','idempotency_prefix'))
         WHERE action_id=@action_id;
        SELECT BIN_TO_UUID(action_id,1), action_kind, BIN_TO_UUID(session_id,1), BIN_TO_UUID(character_id,1), BIN_TO_UUID(world_instance_id,1), target_key, idempotency_key, request_payload
          FROM mmo_server_action_outbox
         WHERE action_id=@action_id;
        COMMIT;
    """)
    return _action_from_row(row)


def reset_failed_for_prefix(target: Target, idempotency_prefix: str) -> int:
    like = idempotency_prefix + ":%"
    out = run_mysql(target, f"""
        UPDATE mmo_server_action_outbox
           SET status='pending',
               attempt_count=0,
               locked_at=NULL,
               failed_at=NULL,
               last_error_code=NULL,
               last_error_message=NULL,
               result_payload=JSON_OBJECT('reset_by','run_mmo_resolved_action_worker','reset_reason','explicit_prefix_reset')
         WHERE idempotency_key LIKE {sql_literal(like)}
           AND status IN ('failed','dead_letter','claimed');
        SELECT ROW_COUNT();
    """)
    return int((out or "0").splitlines()[-1])


def mark_applied(target: Target, action_uuid: str, event_uuid: str | None, result: dict[str, Any]) -> str:
    row = first_row(target, f"""
        SET @status=NULL;
        CALL mmo_mark_server_action_applied(UUID_TO_BIN({sql_literal(action_uuid)},1), {bin_uuid(event_uuid)}, {json_sql(result)}, @status);
        SELECT @status;
    """)
    return row[0]


def mark_failed(target: Target, action_uuid: str, code: str, message: str, retryable: bool) -> str:
    row = first_row(target, f"""
        SET @status=NULL;
        CALL mmo_mark_server_action_failed(UUID_TO_BIN({sql_literal(action_uuid)},1), {sql_literal(code[:64])}, {sql_literal(message[:1000])}, {sql_literal(retryable)}, @status);
        SELECT @status;
    """)
    return row[0]


def record_result(target: Target, run_uuid: str, action: Action, status: str, event_uuid: str | None, details: dict[str, Any], error_code: str | None = None, error_message: str | None = None) -> None:
    run_mysql(target, f"""
        CALL mmo_record_server_action_worker_result(
          UUID_TO_BIN({sql_literal(run_uuid)},1),
          UUID_TO_BIN({sql_literal(action.action_uuid)},1),
          {sql_literal(action.kind)},
          {sql_literal(status)},
          {bin_uuid(event_uuid)},
          {sql_literal(error_code)},
          {sql_literal(error_message)},
          {json_sql(details)}
        );
    """)


def scalar_int(value: Any, default: int | None = None) -> int | None:
    try:
        if value is None or value == "":
            return default
        return int(value)
    except (TypeError, ValueError):
        return default




def client_payload(payload: dict[str, Any]) -> dict[str, Any]:
    raw = payload.get("client_payload")
    return raw if isinstance(raw, dict) else {}


def payload_first(payload: dict[str, Any], *keys: str) -> Any:
    client = client_payload(payload)
    for key in keys:
        value = payload.get(key)
        if value not in (None, ""):
            return value
    for key in keys:
        value = client.get(key)
        if value not in (None, ""):
            return value
    return None


def normalize_equipment_slot(value: Any) -> str:
    if isinstance(value, str) and value in {"weapon_melee", "weapon_ranged", "shield", "armor", "belt", "amulet", "ring_left", "ring_right", "rune", "torch", "unknown"}:
        return value
    try:
        slot = int(value)
    except (TypeError, ValueError):
        return "unknown"
    if slot == 1:
        return "weapon_melee"
    if slot == 2:
        return "weapon_ranged"
    return "unknown"


def parse_world_item_key(value: str | None) -> dict[str, Any]:
    text = str(value or "")
    # Hook key: world-item:newworld.zen:pid:67:sym:6765
    m = re.match(r"^world-item:(?P<world>.*):pid:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    # Runtime SQLite/MySQL import key: world_item:newworld.zen:67:6765:<slot>
    m = re.match(r"^world_item:(?P<world>.*):(?P<pid>\d+):(?P<sym>\d+)(?::.*)?$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    return {"raw": text}


def resolve_world_item_entity_key(target: Target, action: Action) -> dict[str, Any]:
    payload = action.payload
    raw_key = payload_first(payload, "world_item_entity_key", "engine_world_item_key", "target_key") or action.target_key
    parsed = parse_world_item_key(str(raw_key or ""))
    pid = scalar_int(payload_first(payload, "source_world_item_persistent_id", "item_persistent_id"), parsed.get("persistent_id"))
    sym = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"), parsed.get("symbol"))
    world = payload_first(payload, "world") or parsed.get("world")

    exact = str(raw_key or "")
    candidates = [exact]
    if world and pid is not None and sym is not None:
        candidates.append(f"world_item:{world}:{pid}:{sym}:%")

    row = first_row(target, f"""
        SELECT wes.entity_key, wes.lifecycle_state,
               JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS persistent_id,
               JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS template_symbol
          FROM world_entity_state wes
         WHERE wes.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wes.entity_kind = 'item'
           AND (
                wes.entity_key = {sql_literal(exact)}
                OR wes.entity_key LIKE {sql_literal(candidates[-1] if len(candidates) > 1 else '__NO_LIKE__')}
                OR ({'TRUE' if pid is not None else 'FALSE'} AND CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED) = {int(pid or -1)})
           )
           AND ({'TRUE' if sym is not None else 'FALSE'} = FALSE OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED) = {int(sym or -1)})
         ORDER BY CASE WHEN wes.entity_key = {sql_literal(exact)} THEN 0 ELSE 1 END, wes.updated_at DESC
         LIMIT 2;
    """)
    if not row:
        raise ResolveError(f"world item not found for key={exact!r} pid={pid} sym={sym} world={world!r}")
    # first_row only returns one row; do a count for ambiguity
    count_row = first_row(target, f"""
        SELECT COUNT(*)
          FROM world_entity_state wes
         WHERE wes.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wes.entity_kind = 'item'
           AND (
                wes.entity_key = {sql_literal(exact)}
                OR wes.entity_key LIKE {sql_literal(candidates[-1] if len(candidates) > 1 else '__NO_LIKE__')}
                OR ({'TRUE' if pid is not None else 'FALSE'} AND CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED) = {int(pid or -1)})
           )
           AND ({'TRUE' if sym is not None else 'FALSE'} = FALSE OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED) = {int(sym or -1)});
    """)
    cnt = int(count_row[0]) if count_row else 0
    if cnt != 1:
        raise ResolveError(f"world item key is ambiguous: candidates={cnt} key={exact!r} pid={pid} sym={sym}")
    if row[1] != "active":
        raise ResolveError(f"world item resolved but is not active: entity_key={row[0]} lifecycle={row[1]} pid={pid} sym={sym}")
    return {"world_item_entity_key": row[0], "lifecycle_state": row[1], "persistent_id": row[2], "item_symbol": row[3], "resolver": "world_item_engine_key_v2"}


def _character_item_rows(target: Target, action: Action, item_symbol: int, extra_predicate: str = "TRUE", limit: int = 5) -> list[list[str]]:
    rows = run_mysql(target, f"""
        SELECT BIN_TO_UUID(ii.item_instance_id,1),
               ii.item_instance_key,
               ci.bag_index,
               ci.amount,
               ii.quantity,
               JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_instance_key')) AS raw_item_key
          FROM item_instances ii
          JOIN character_inventory ci ON ci.item_instance_id = ii.item_instance_id
          JOIN content_item_templates it ON it.item_template_id = ii.item_template_id
         WHERE ii.realm_id = (SELECT realm_id FROM server_sessions WHERE session_id = UUID_TO_BIN({sql_literal(action.session_uuid)},1) LIMIT 1)
           AND ii.owner_type = 'character'
           AND ii.owner_id = UUID_TO_BIN({sql_literal(action.character_uuid)},1)
           AND ii.lifecycle_state = 'active'
           AND ci.character_id = UUID_TO_BIN({sql_literal(action.character_uuid)},1)
           AND it.symbol_index = {int(item_symbol)}
           AND {extra_predicate}
         ORDER BY ci.amount DESC, ii.updated_at DESC, ii.item_instance_key ASC
         LIMIT {int(limit)};
    """)
    return [ln.split("\t") for ln in rows.splitlines() if ln.strip()]


def resolve_character_item_instance(target: Target, action: Action) -> dict[str, Any]:
    payload = action.payload
    if payload.get("item_instance_id"):
        return {"item_instance_uuid": str(payload["item_instance_id"]), "resolver": "payload.item_instance_id"}

    item_symbol = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"))
    item_pid = scalar_int(payload_first(payload, "item_persistent_id", "source_world_item_persistent_id"))
    required_amount = max(1, optional_int_payload(action, "amount", default=1))
    if item_symbol is None:
        raise ResolveError("item_symbol is required to resolve character item instance")

    preferred_rows: list[list[str]] = []
    if item_pid is not None:
        pid_text = str(item_pid)
        sym_text = str(item_symbol)
        # Runtime SQLite inventory keys are usually '<symbol>:<slot>:<equipped>';
        # older world/import keys can be '<pid>:<symbol>:...' or raw payload fields.
        pid_pred = f"""(
             JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id')) = {sql_literal(item_pid)}
             OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_world_item_persistent_id')) = {sql_literal(item_pid)}
             OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_instance_key')) LIKE {sql_literal(sym_text + ':' + pid_text + ':%')}
             OR ii.item_instance_key LIKE {sql_literal('%:' + sym_text + ':' + pid_text + ':%')}
             OR ii.item_instance_key LIKE {sql_literal('%:' + pid_text + ':' + sym_text + ':%')}
        )"""
        preferred_rows = _character_item_rows(target, action, item_symbol, pid_pred, limit=5)

    if len(preferred_rows) == 1:
        row = preferred_rows[0]
        return {
            "item_instance_uuid": row[0],
            "item_instance_key": row[1],
            "bag_index": row[2],
            "amount": row[3],
            "resolver": "character_inventory_symbol_pid_v3",
        }
    if len(preferred_rows) > 1:
        raise ResolveError(f"character item instance ambiguous for symbol={item_symbol} pid={item_pid}: {len(preferred_rows)} preferred candidates")

    # Some OpenGothic inventory items do not have a durable per-stack persistent id
    # in the imported MySQL raw payload. For stack resources such as arrows/bolts,
    # fall back to symbol-only only when the server projection is unique enough.
    fallback_rows = _character_item_rows(
        target,
        action,
        item_symbol,
        f"COALESCE(ci.amount, ii.quantity, 0) >= {int(required_amount)}",
        limit=5,
    )
    if len(fallback_rows) == 1:
        row = fallback_rows[0]
        return {
            "item_instance_uuid": row[0],
            "item_instance_key": row[1],
            "bag_index": row[2],
            "amount": row[3],
            "resolver": "character_inventory_symbol_unique_v3",
            "persistent_id_unmatched": item_pid,
        }
    if not fallback_rows:
        raise ResolveError(f"character item instance not found for symbol={item_symbol} pid={item_pid}")
    sample = ", ".join(r[1] for r in fallback_rows[:3])
    raise ResolveError(f"character item instance ambiguous for symbol={item_symbol} pid={item_pid}: {len(fallback_rows)} symbol candidates: {sample}")





def parse_npc_entity_key(value: str | None) -> dict[str, Any]:
    text = str(value or "")
    # Step38 hook key: npc:newworld.zen:pid:123:sym:456
    m = re.match(r"^npc:(?P<world>.*):pid:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    # Runtime SQLite/MySQL import key: npc:<world>:<pid>:<symbol>:<script_id>
    m = re.match(r"^npc:(?P<world>.*):(?P<pid>\d+):(?P<sym>\d+)(?::(?P<script>\d+))?$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    # Older actor key: npc:123:sym:456
    m = re.match(r"^npc:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    return {"raw": text}


def resolve_world_npc_entity_key(target: Target, action: Action, *payload_keys: str) -> dict[str, Any]:
    payload = action.payload
    raw_key = payload_first(payload, *payload_keys) if payload_keys else None
    raw_key = raw_key or payload_first(payload, "target_npc_entity_key", "npc_entity_key", "npc_key", "target_key") or action.target_key
    parsed = parse_npc_entity_key(str(raw_key or ""))
    pid = scalar_int(payload_first(payload, "target_npc_persistent_id", "npc_persistent_id", "persistent_id"), parsed.get("persistent_id"))
    sym = scalar_int(payload_first(payload, "target_npc_symbol", "npc_symbol", "symbol"), parsed.get("symbol"))
    world = payload_first(payload, "world") or parsed.get("world")
    exact = str(raw_key or "")

    like_patterns: list[str] = []
    if world and pid is not None and sym is not None:
        # Runtime SQLite import key shape is npc:<world>:<pid>:<symbol>:<script_id>.
        # Step38 hook key shape is npc:<world>:pid:<pid>:sym:<symbol>.
        like_patterns.append(f"npc:{world}:{pid}:{sym}:%")
    like_sql = " OR ".join(f"wes.entity_key LIKE {sql_literal(p)}" for p in like_patterns) or "FALSE"

    row_sql = f"""
        SELECT wes.entity_key, wes.lifecycle_state, wes.row_version,
               COALESCE(
                 JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')),
                 REGEXP_SUBSTR(wes.entity_key, ':[0-9]+:', 1, 1)
               ) AS persistent_id,
               COALESCE(
                 JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')),
                 JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')),
                 JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol'))
               ) AS symbol_index
          FROM world_entity_state wes
         WHERE wes.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wes.entity_kind IN ('npc','creature')
           AND (
                wes.entity_key = {sql_literal(exact)}
                OR {like_sql}
                OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.creature_spawn_key')) = {sql_literal(exact)}
                OR ({'TRUE' if pid is not None else 'FALSE'} AND (
                    CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED) = {int(pid or -1)}
                    OR JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.creature_spawn_key')) LIKE {sql_literal('%:' + str(pid or -1) + ':' + str(sym or -1) + ':%')}
                ))
                OR ({'TRUE' if sym is not None else 'FALSE'} AND (
                    CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.symbol_index')) AS SIGNED) = {int(sym or -1)}
                    OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.script_id')) AS SIGNED) = {int(sym or -1)}
                    OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.instance_symbol')) AS SIGNED) = {int(sym or -1)}
                ))
           )
         ORDER BY CASE WHEN wes.entity_key = {sql_literal(exact)} THEN 0 ELSE 1 END,
                  CASE WHEN {like_sql} THEN 1 ELSE 2 END,
                  CASE WHEN wes.lifecycle_state='active' THEN 0 ELSE 1 END,
                  wes.updated_at DESC
         LIMIT 5;
    """
    out = run_mysql(target, row_sql)
    rows = [line.split("\t") for line in out.splitlines() if line.strip()]
    if not rows:
        raise ResolveError(f"NPC/world entity not found for key={exact!r} pid={pid} sym={sym}")

    exact_rows = [r for r in rows if r[0] == exact]
    if len(exact_rows) == 1:
        rows = exact_rows
    elif like_patterns:
        alias_rows = [r for r in rows if any(r[0].startswith(p[:-1]) if p.endswith('%') else r[0] == p for p in like_patterns)]
        if len(alias_rows) == 1:
            rows = alias_rows
        elif len(alias_rows) > 1:
            active_alias_rows = [r for r in alias_rows if r[1] == 'active']
            if len(active_alias_rows) == 1:
                rows = active_alias_rows
            else:
                raise ResolveError(f"NPC/world entity key is ambiguous: alias_candidates={len(alias_rows)} key={exact!r} pid={pid} sym={sym}")
        elif len(rows) > 1:
            raise ResolveError(f"NPC/world entity key is ambiguous: candidates={len(rows)} key={exact!r} pid={pid} sym={sym}")
    elif len(rows) > 1:
        raise ResolveError(f"NPC/world entity key is ambiguous: candidates={len(rows)} key={exact!r} pid={pid} sym={sym}")

    row = rows[0]
    return {
        "npc_key": row[0],
        "world_entity_key": row[0],
        "lifecycle_state": row[1],
        "row_version": row[2],
        "persistent_id": row[3],
        "symbol_index": row[4],
        "resolver": "npc_world_entity_key_v2",
        "requested_key": exact,
    }


def resolve_world_inventory_item_instance(target: Target, action: Action, owner_entity_key: str) -> dict[str, Any]:
    item_symbol = scalar_int(payload_first(action.payload, "item_symbol", "inventory_item_symbol"))
    if item_symbol is None:
        raise ResolveError("item_symbol is required to resolve world/NPC inventory item")
    rows = run_mysql(target, f"""
        SELECT BIN_TO_UUID(ii.item_instance_id,1), ii.item_instance_key, wi.amount
          FROM world_inventory wi
          JOIN item_instances ii ON ii.item_instance_id = wi.item_instance_id
          JOIN content_item_templates it ON it.item_template_id = ii.item_template_id
         WHERE wi.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wi.owner_entity_key = {sql_literal(owner_entity_key)}
           AND ii.lifecycle_state = 'active'
           AND it.symbol_index = {int(item_symbol)}
         ORDER BY ii.updated_at DESC
         LIMIT 3;
    """)
    lines = [ln.split("\t") for ln in rows.splitlines() if ln.strip()]
    if not lines:
        raise ResolveError(f"world/NPC inventory item not found owner={owner_entity_key!r} symbol={item_symbol}")
    if len(lines) > 1:
        raise ResolveError(f"world/NPC inventory item ambiguous owner={owner_entity_key!r} symbol={item_symbol}: {len(lines)} candidates")
    row = lines[0]
    return {"item_instance_uuid": row[0], "item_instance_key": row[1], "amount": row[2], "item_symbol": item_symbol, "resolver": "world_inventory_owner_symbol_v1"}

def next_character_bag_index(target: Target, character_uuid: str) -> int:
    """Return the first free non-negative bag_index for a character.

    The MySQL pickup procedure requires the caller to provide the target
    bag_index and enforces character_inventory_bag_uk. Client envelopes do not
    currently carry a server-authoritative bag index, so the dev worker must
    allocate one from the current projection instead of defaulting to 0.
    """
    out = run_mysql(target, f"""
        SELECT bag_index
          FROM character_inventory
         WHERE character_id = UUID_TO_BIN({sql_literal(character_uuid)},1)
         ORDER BY bag_index ASC;
    """)
    used: set[int] = set()
    for line in out.splitlines():
        line = line.strip()
        if not line or line == "NULL":
            continue
        try:
            idx = int(line.split("\t")[0])
        except (TypeError, ValueError):
            continue
        if idx >= 0:
            used.add(idx)
    candidate = 0
    while candidate in used:
        candidate += 1
    return candidate

def server_tick(action: Action) -> int:
    payload = action.payload
    return max(0, int(payload_first(payload, "server_tick", "client_tick") or 0))


def bool_payload(value: Any, default: bool = False) -> bool:
    if value is None or value == "":
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "y", "on"}:
        return True
    if text in {"0", "false", "no", "n", "off"}:
        return False
    return default




def checkpoint_payload_value(action: Action, key: str, default: Any = None) -> Any:
    value = payload_first(action.payload, key)
    return default if value in (None, "") else value


def float_payload(action: Action, *keys: str, default: float | None = None) -> float:
    value = payload_first(action.payload, *keys)
    if value in (None, ""):
        if default is None:
            raise ResolveError("float payload required: " + "/".join(keys))
        return float(default)
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise ResolveError("invalid float payload: " + "/".join(keys)) from exc


def quest_status_payload(value: Any) -> str:
    """Normalize Gothic/runtime quest status values to the MySQL procedure contract."""
    if value is None or value == "":
        return "running"
    text = str(value).strip().lower()
    aliases = {
        "1": "running",
        "running": "running",
        "run": "running",
        "in_progress": "running",
        "2": "success",
        "success": "success",
        "completed_success": "success",
        "succeeded": "success",
        "3": "failed",
        "failed": "failed",
        "failure": "failed",
        "completed_failed": "failed",
        "4": "obsolete",
        "obsolete": "obsolete",
        "closed": "obsolete",
    }
    return aliases.get(text, text)


DIALOG_AVAILABILITY_STATES = {
    "consumed_hidden",
    "repeatable_known",
    "repeatable_not_seen",
    "one_shot_not_seen",
}


def derive_dialog_availability(known: bool, permanent: bool) -> str:
    if known and permanent:
        return "repeatable_known"
    if known and not permanent:
        return "consumed_hidden"
    if not known and permanent:
        return "repeatable_not_seen"
    return "one_shot_not_seen"


def dialog_permanent_payload(payload: dict[str, Any]) -> bool:
    explicit = payload_first(payload, "permanent", "repeatable")
    if explicit is not None:
        return bool_payload(explicit, False)

    removed = payload_first(payload, "removed")
    if removed is not None:
        # OpenGothic Step37 uses removed=true for one-shot consumed dialog choices.
        # MySQL procedure expects permanent=false in that case.
        return not bool_payload(removed, False)

    return False


def dialog_availability_payload(payload: dict[str, Any], known: bool, permanent: bool) -> str:
    explicit = payload_first(payload, "availability_state", "dialog_availability_state")
    if explicit is None:
        return derive_dialog_availability(known, permanent)
    text = str(explicit).strip().lower()
    if text in DIALOG_AVAILABILITY_STATES:
        return text
    raise ResolveError(f"invalid dialog availability_state payload: {text}")

def json_array_sql(value: Any) -> str:
    if isinstance(value, list):
        return json_sql(value)
    if value in (None, ""):
        return "JSON_ARRAY()"
    return json_sql([str(value)])


def script_key_from_payload(action: Action) -> str:
    value = payload_first(action.payload, "script_key", "global_key", "symbol_name", "target_key") or action.target_key
    if value in (None, ""):
        raise ResolveError("script_key/global_key/symbol_name is required")
    return str(value)


def int_payload(action: Action, *keys: str, default: int | None = None) -> int:
    value = payload_first(action.payload, *keys)
    parsed = scalar_int(value, default)
    if parsed is None:
        raise ResolveError("integer payload required: " + "/".join(keys))
    return parsed


def optional_int_payload(action: Action, *keys: str, default: int = 0) -> int:
    value = payload_first(action.payload, *keys)
    parsed = scalar_int(value, default)
    return default if parsed is None else parsed



def metadata(action: Action, resolved: dict[str, Any]) -> dict[str, Any]:
    base = action.payload.get("metadata") if isinstance(action.payload.get("metadata"), dict) else {}
    return {
        **base,
        "source": "run_mmo_resolved_action_worker",
        "worker_mode": WORKER_MODE,
        "action_uuid": action.action_uuid,
        "client_idempotency_key": payload_first(action.payload, "client_idempotency_key", "idempotency_key"),
        "client_target_key": payload_first(action.payload, "client_target_key", "target_key") or action.target_key,
        "resolver": resolved,
    }


def dispatch(target: Target, action: Action) -> tuple[str | None, dict[str, Any]]:
    kind = action.kind
    tick = server_tick(action)
    idem = action.idempotency_key

    if kind == "pickup_world_item":
        resolved = resolve_world_item_entity_key(target, action)
        amount = int(payload_first(action.payload, "amount") or 1)
        server_bag_index = action.payload.get("server_bag_index")
        if server_bag_index not in (None, ""):
            bag_index = int(server_bag_index)
            bag_resolver = "payload.server_bag_index"
        else:
            # Do not trust/guess client bag_index here. The server projection
            # owns character_inventory.bag_index uniqueness, so allocate the
            # first free slot from DB right before calling the procedure.
            bag_index = next_character_bag_index(target, action.character_uuid)
            bag_resolver = "server_next_free_bag_index_v3"
        row = first_row(target, f"""
            SET @event_id=NULL; SET @item_id=NULL; SET @amount_picked=NULL;
            CALL mmo_pickup_world_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(resolved['world_item_entity_key'])},
              {int(amount)},
              {int(bag_index)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @item_id, @amount_picked
            );
            SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@item_id,1), @amount_picked;
        """)
        resolved = {**resolved, "bag_index": bag_index, "bag_index_resolver": bag_resolver}
        return row[0], {"item_instance_uuid": row[1], "amount_picked": int(row[2]), "bag_index": bag_index, "resolved": resolved}

    if kind == "remove_world_item":
        resolved = resolve_world_item_entity_key(target, action)
        reason = action.payload.get("reason") or "semantic_action"
        row = first_row(target, f"""
            SET @event_id=NULL; SET @item_id=NULL;
            CALL mmo_remove_world_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(resolved['world_item_entity_key'])},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @item_id
            );
            SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@item_id,1);
        """)
        return row[0], {"item_instance_uuid": row[1], "resolved": resolved}

    if kind == "equip_character_item":
        resolved = resolve_character_item_instance(target, action)
        slot = normalize_equipment_slot(payload_first(action.payload, "equipment_slot", "slot"))
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_equip_character_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              UUID_TO_BIN({sql_literal(resolved['item_instance_uuid'])},1),
              {sql_literal(slot)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"item_instance_uuid": resolved["item_instance_uuid"], "equipment_slot": slot, "resolved": resolved}

    if kind == "unequip_character_item":
        slot = normalize_equipment_slot(payload_first(action.payload, "equipment_slot", "slot"))
        row = first_row(target, f"""
            SET @event_id=NULL; SET @item_id=NULL;
            CALL mmo_unequip_character_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(slot)},
              {int(tick)},
              {json_sql(metadata(action, {"resolver":"slot_only_v2"}))},
              {sql_literal(idem)},
              @event_id, @item_id
            );
            SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@item_id,1);
        """)
        return row[0], {"item_instance_uuid": row[1], "equipment_slot": slot, "resolved": {"resolver": "slot_only_v2"}}

    if kind == "set_script_int":
        script_key = script_key_from_payload(action)
        symbol_index = optional_int_payload(action, "symbol_index", default=0)
        value_index = optional_int_payload(action, "value_index", default=0)
        value_after = int_payload(action, "value_after", "value")
        resolved = {"resolver": "script_key_payload_v1", "script_key": script_key, "symbol_index": symbol_index, "value_index": value_index}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @value_after=NULL;
            CALL mmo_set_character_script_int(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(script_key)},
              {sql_literal(symbol_index)},
              {int(value_index)},
              {int(value_after)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @value_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @value_after;
        """)
        return row[0], {"script_key": script_key, "value_after": int(row[1]), "resolved": resolved}

    if kind == "adjust_progression":
        exp_delta = int_payload(action, "experience_delta", "xp_delta", "delta")
        lp_delta = optional_int_payload(action, "learning_points_delta", "lp_delta", default=0)
        reason = str(payload_first(action.payload, "reason") or "script_progression")
        resolved = {"resolver": "progression_delta_payload_v1", "experience_delta": exp_delta, "learning_points_delta": lp_delta, "reason": reason}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @experience_after=NULL; SET @learning_points_after=NULL;
            CALL mmo_adjust_character_progression(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {int(exp_delta)},
              {int(lp_delta)},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @experience_after, @learning_points_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @experience_after, @learning_points_after;
        """)
        return row[0], {"experience_after": int(row[1]), "learning_points_after": int(row[2]), "resolved": resolved}

    if kind == "apply_experience_reward":
        exp_delta = int_payload(action, "experience_delta", "xp_delta", "delta")
        reason = str(payload_first(action.payload, "reason") or "script_experience_reward")
        resolved = {"resolver": "experience_reward_payload_v1", "experience_delta": exp_delta, "reason": reason}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @experience_after=NULL;
            CALL mmo_apply_character_experience_reward(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {int(exp_delta)},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @experience_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @experience_after;
        """)
        return row[0], {"experience_after": int(row[1]), "resolved": resolved}

    if kind == "update_quest":
        quest_key = str(payload_first(action.payload, "quest_key", "topic", "target_key") or action.target_key or "")
        if not quest_key:
            raise ResolveError("quest_key/topic is required")
        quest_name = str(payload_first(action.payload, "quest_name", "name") or quest_key)
        status = quest_status_payload(payload_first(action.payload, "status"))
        entry_count = optional_int_payload(action, "entry_count", default=0)
        entries = payload_first(action.payload, "entries")
        resolved = {"resolver": "quest_payload_v1", "quest_key": quest_key, "status": status}
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_update_character_quest(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(quest_key)},
              {sql_literal(quest_name)},
              {sql_literal(status)},
              {int(entry_count)},
              {json_array_sql(entries)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"quest_key": quest_key, "status": status, "resolved": resolved}

    if kind == "set_known_dialog":
        npc_key = str(payload_first(action.payload, "npc_key", "npc_symbol_name") or "")
        info_key = str(payload_first(action.payload, "info_key", "info_symbol_name", "target_key") or action.target_key or "")
        if not npc_key or not info_key:
            raise ResolveError("npc_key and info_key are required")
        known = bool_payload(payload_first(action.payload, "known"), True)
        permanent = dialog_permanent_payload(action.payload)
        availability_state = dialog_availability_payload(action.payload, known, permanent)
        removed = payload_first(action.payload, "removed")
        removed_value = bool_payload(removed, not permanent) if removed is not None else (known and not permanent)
        reason = str(payload_first(action.payload, "reason") or "script_dialog_known")
        resolved = {
            "resolver": "known_dialog_payload_v2",
            "npc_key": npc_key,
            "info_key": info_key,
            "known": known,
            "permanent": permanent,
            "removed": removed_value,
            "availability_state": availability_state,
            "reason": reason,
        }
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_set_character_known_dialog(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc_key)},
              {sql_literal(info_key)},
              {sql_literal(known)},
              {sql_literal(permanent)},
              {sql_literal(availability_state)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {
            "npc_key": npc_key,
            "info_key": info_key,
            "known": known,
            "permanent": permanent,
            "removed": removed_value,
            "availability_state": availability_state,
            "resolved": resolved,
        }


    if kind == "consume_mana":
        mana_amount = int_payload(action, "mana_amount", "amount", "delta")
        resolved = {"resolver": "consume_mana_payload_v1", "mana_amount": mana_amount}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @mana_after=NULL;
            CALL mmo_consume_character_mana(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {int(mana_amount)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @mana_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @mana_after;
        """)
        return row[0], {"mana_after": int(row[1]), "resolved": resolved}

    if kind == "consume_item":
        resolved_item = resolve_character_item_instance(target, action)
        amount = int_payload(action, "amount", default=1)
        reason = str(payload_first(action.payload, "reason") or "consume_item")
        resolved = {**resolved_item, "resolver": "consume_item_character_inventory_v1", "amount": amount, "reason": reason}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @amount_after=NULL;
            CALL mmo_consume_character_item(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              UUID_TO_BIN({sql_literal(resolved_item['item_instance_uuid'])},1),
              {int(amount)},
              {sql_literal(reason)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @amount_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @amount_after;
        """)
        return row[0], {"amount_after": int(row[1]), "item_instance_uuid": resolved_item["item_instance_uuid"], "resolved": resolved}

    if kind == "apply_character_damage":
        character_key = str(payload_first(action.payload, "target_character_key", "character_key") or "PC_HERO")
        damage_amount = int_payload(action, "damage_amount", "amount", "delta")
        resolved = {"resolver": "character_damage_payload_v1", "target_character_key": character_key, "damage_amount": damage_amount}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @health_after=NULL;
            CALL mmo_apply_character_damage(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(character_key)},
              {int(damage_amount)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @health_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @health_after;
        """)
        return row[0], {"health_after": int(row[1]), "resolved": resolved}

    if kind == "apply_world_entity_damage":
        npc = resolve_world_npc_entity_key(target, action, "target_world_entity_key", "target_npc_entity_key", "target_key")
        damage_amount = int_payload(action, "damage_amount", "amount", "delta")
        fatal = bool_payload(payload_first(action.payload, "fatal", "dead"), False)
        resolved = {**npc, "resolver": "world_entity_damage_payload_v2", "damage_amount": damage_amount, "fatal": fatal}
        if str(npc.get("lifecycle_state") or "") != "active":
            # Dev replay can contain redundant local damage envelopes after a prior
            # mark_npc_dead envelope has already made the server projection inactive.
            # The authoritative DB procedure correctly rejects damage on inactive
            # targets; for isolated replay evidence this stale consequence is accepted
            # as an applied no-op, with no journal event and full resolver metadata.
            result = {
                "applied_noop": True,
                "noop_reason": "target_entity_not_active",
                "event_emitted": False,
                "resolved": resolved,
            }
            return None, result
        row = first_row(target, f"""
            SET @event_id=NULL; SET @health_after=NULL; SET @row_after=NULL;
            CALL mmo_apply_world_entity_damage(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              {int(damage_amount)},
              {sql_literal(fatal)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @health_after, @row_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @health_after, @row_after;
        """)
        return row[0], {"health_after": int(row[1]), "row_version_after": int(row[2]), "resolved": resolved}

    if kind == "mark_npc_dead":
        npc = resolve_world_npc_entity_key(target, action, "target_world_entity_key", "target_npc_entity_key", "target_key")
        resolved = {**npc, "resolver": "mark_npc_dead_payload_v2"}
        if str(npc.get("lifecycle_state") or "") != "active":
            result = {
                "applied_noop": True,
                "noop_reason": "target_entity_already_inactive",
                "event_emitted": False,
                "resolved": resolved,
            }
            return None, result
        row = first_row(target, f"""
            SET @event_id=NULL; SET @row_after=NULL;
            CALL mmo_mark_npc_dead(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @row_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @row_after;
        """)
        return row[0], {"row_version_after": int(row[1]), "resolved": resolved}

    if kind == "trade_sell_to_npc":
        npc = resolve_world_npc_entity_key(target, action, "npc_entity_key", "npc_key")
        item = resolve_character_item_instance(target, action)
        price_total = optional_int_payload(action, "price_total", default=0)
        if price_total == 0:
            price_total = optional_int_payload(action, "unit_price", default=0) * max(1, optional_int_payload(action, "amount", default=1))
        currency_key = str(payload_first(action.payload, "currency_key") or "g2notr:gold")
        resolved = {**npc, **item, "resolver": "trade_sell_payload_v1", "price_total": price_total, "currency_key": currency_key}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @wallet_after=NULL;
            CALL mmo_trade_sell_to_npc(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              UUID_TO_BIN({sql_literal(item['item_instance_uuid'])},1),
              {int(price_total)},
              {sql_literal(currency_key)},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @wallet_after
            );
            SELECT BIN_TO_UUID(@event_id,1), @wallet_after;
        """)
        return row[0], {"wallet_after": int(row[1]), "resolved": resolved}

    if kind == "trade_buy_from_npc":
        npc = resolve_world_npc_entity_key(target, action, "npc_entity_key", "npc_key")
        item = resolve_world_inventory_item_instance(target, action, npc["world_entity_key"])
        price_total = optional_int_payload(action, "price_total", default=0)
        if price_total == 0:
            price_total = optional_int_payload(action, "unit_price", default=0) * max(1, optional_int_payload(action, "amount", default=1))
        currency_key = str(payload_first(action.payload, "currency_key") or "g2notr:gold")
        target_bag = payload_first(action.payload, "target_bag_index", "server_bag_index")
        target_bag_sql = "NULL" if target_bag in (None, "") else str(int(target_bag))
        resolved = {**npc, **item, "resolver": "trade_buy_payload_v1", "price_total": price_total, "currency_key": currency_key, "target_bag_index": target_bag}
        row = first_row(target, f"""
            SET @event_id=NULL; SET @wallet_after=NULL; SET @bag_index=NULL;
            CALL mmo_trade_buy_from_npc(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {sql_literal(npc['world_entity_key'])},
              UUID_TO_BIN({sql_literal(item['item_instance_uuid'])},1),
              {int(price_total)},
              {sql_literal(currency_key)},
              {target_bag_sql},
              {int(tick)},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id, @wallet_after, @bag_index
            );
            SELECT BIN_TO_UUID(@event_id,1), @wallet_after, @bag_index;
        """)
        return row[0], {"wallet_after": int(row[1]), "bag_index": int(row[2]), "resolved": resolved}


    if kind == "character_checkpoint":
        pos_x = float_payload(action, "pos_x")
        pos_y = float_payload(action, "pos_y")
        pos_z = float_payload(action, "pos_z")
        rotation_yaw = float_payload(action, "rotation_yaw", "yaw", default=0.0)
        waypoint = str(checkpoint_payload_value(action, "current_waypoint_key", "") or "")
        level = int_payload(action, "level", default=0)
        experience = int_payload(action, "experience", default=0)
        experience_next = int_payload(action, "experience_next", default=0)
        learning_points = int_payload(action, "learning_points", default=0)
        health_current = int_payload(action, "health_current", default=0)
        health_max = int_payload(action, "health_max", default=max(health_current, 0))
        mana_current = int_payload(action, "mana_current", default=0)
        mana_max = int_payload(action, "mana_max", default=max(mana_current, 0))
        strength = int_payload(action, "strength", default=0)
        dexterity = int_payload(action, "dexterity", default=0)
        guild = checkpoint_payload_value(action, "guild")
        true_guild = checkpoint_payload_value(action, "true_guild")
        permanent_attitude = checkpoint_payload_value(action, "permanent_attitude")
        temporary_attitude = checkpoint_payload_value(action, "temporary_attitude")
        resolved = {
            "resolver": "character_checkpoint_payload_v1",
            "character_uuid": action.character_uuid,
            "pos_x": pos_x,
            "pos_y": pos_y,
            "pos_z": pos_z,
            "rotation_yaw": rotation_yaw,
            "current_waypoint_key": waypoint,
            "level": level,
            "experience": experience,
            "experience_next": experience_next,
            "learning_points": learning_points,
            "reason": checkpoint_payload_value(action, "reason", "character_checkpoint"),
            "checkpoint_interval_ms": checkpoint_payload_value(action, "checkpoint_interval_ms"),
            "checkpoint_min_distance": checkpoint_payload_value(action, "checkpoint_min_distance"),
            "checkpoint_min_yaw_deg": checkpoint_payload_value(action, "checkpoint_min_yaw_deg"),
            "checkpoint_force_interval_ms": checkpoint_payload_value(action, "checkpoint_force_interval_ms"),
        }
        row = first_row(target, f"""
            SET @event_id=NULL;
            CALL mmo_checkpoint_character_state(
              UUID_TO_BIN({sql_literal(action.session_uuid)},1),
              {int(tick)},
              {pos_x},
              {pos_y},
              {pos_z},
              {rotation_yaw},
              {sql_literal(waypoint)},
              {int(level)},
              {int(experience)},
              {int(experience_next)},
              {int(learning_points)},
              {int(health_current)},
              {int(health_max)},
              {int(mana_current)},
              {int(mana_max)},
              {int(strength)},
              {int(dexterity)},
              {sql_literal(guild) if guild not in (None, '') else 'NULL'},
              {sql_literal(true_guild) if true_guild not in (None, '') else 'NULL'},
              {sql_literal(permanent_attitude) if permanent_attitude not in (None, '') else 'NULL'},
              {sql_literal(temporary_attitude) if temporary_attitude not in (None, '') else 'NULL'},
              {json_sql(metadata(action, resolved))},
              {sql_literal(idem)},
              @event_id
            );
            SELECT BIN_TO_UUID(@event_id,1);
        """)
        return row[0], {"resolved": resolved}

    raise NotImplementedError(f"unsupported action_kind for resolved worker: {kind}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Resolve and dispatch receiver-enqueued OpenGothic MMO actions.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--worker-id", default="dev-resolved-worker")
    ap.add_argument("--run-key", default=None)
    ap.add_argument("--max-actions", type=int, default=10)
    ap.add_argument("--session-key", default=None, help="only claim pending actions whose idempotency_key starts with '<session-key>:'")
    ap.add_argument("--reset-matching-failed", action="store_true", help="reset failed/dead_letter/claimed rows for --session-key back to pending before running")
    ap.add_argument("--continue-on-error", action="store_true", help="keep claiming later actions after a failure; default stops so dependent equip/unequip is not applied after failed pickup")
    ap.add_argument("--retry-unresolved", action="store_true", help="mark resolver failures retryable instead of failed")
    ap.add_argument("--dry-run", action="store_true", help="claim nothing; only print matching pending/failed rows summary")
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)

    if args.dry_run:
        if args.session_key:
            print(run_mysql(target, f"""
                SELECT action_kind, status, COUNT(*)
                  FROM mmo_server_action_outbox
                 WHERE idempotency_key LIKE {sql_literal(args.session_key + ':%')}
                 GROUP BY action_kind, status
                 ORDER BY action_kind, status;
            """))
        else:
            print(run_mysql(target, """
                SELECT action_kind, status, COUNT(*)
                  FROM mmo_server_action_outbox
                 WHERE status IN ('pending','claimed','failed','dead_letter')
                 GROUP BY action_kind, status
                 ORDER BY action_kind, status;
            """))
        return 0

    if args.reset_matching_failed:
        if not args.session_key:
            raise SystemExit("--reset-matching-failed requires --session-key")
        reset = reset_failed_for_prefix(target, args.session_key)
        print(f"[RESET] session_key={args.session_key} rows={reset}")

    run_key = args.run_key or f"resolved-{args.worker_id}-{int(time.time())}"
    run_uuid = start_worker_run(target, args.worker_id, run_key, args.max_actions)
    failed = False

    for _ in range(max(0, args.max_actions)):
        action = claim_matching_prefix(target, args.worker_id, args.session_key) if args.session_key else claim(target, args.worker_id)
        if action is None:
            break
        record_result(target, run_uuid, action, "claimed", None, {"claimed": True})
        try:
            event_uuid, result = dispatch(target, action)
            status = mark_applied(target, action.action_uuid, event_uuid, result)
            record_result(target, run_uuid, action, status, event_uuid, result)
            print(f"[APPLIED] {action.kind} action={action.action_uuid} event={event_uuid} result={json.dumps(result, ensure_ascii=False, sort_keys=True)}")
        except Exception as exc:
            failed = True
            retryable = bool(args.retry_unresolved and isinstance(exc, ResolveError))
            status = mark_failed(target, action.action_uuid, type(exc).__name__, str(exc), retryable)
            record_result(target, run_uuid, action, status, None, {"exception": type(exc).__name__}, type(exc).__name__, str(exc))
            print(f"[FAILED] {action.kind} action={action.action_uuid} status={status} error={exc}", file=sys.stderr)
            if not args.continue_on_error:
                break

    status, applied = finish_worker_run(target, run_uuid, failed)
    print(f"[RUN] {run_uuid} status={status} applied={applied}")
    return 0 if status == "finished" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))





