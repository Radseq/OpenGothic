#!/usr/bin/env python3
"""Prepare a DEV-only MySQL projection fixture for Step 35 resolved dispatch.

Use case: OpenGothic emitted valid pickup/equip/unequip actions, but the current
MySQL projection already marks those loose world items as removed/archived. That
is a client-vs-DB start-state mismatch, not a transport bug. This tool can
explicitly reactivate only the world-item rows referenced by one receiver session
so the resolved dispatcher can be tested end-to-end.

This is NOT a production repair tool. It mutates current-state projections
outside gameplay procedures and writes dev_fixture markers into JSON payloads.
Use it only on local/dev databases.
"""
from __future__ import annotations

import argparse
import json
import re
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


@dataclass(frozen=True)
class PickupCandidate:
    action_kind: str
    idempotency_key: str
    session_uuid: str
    character_uuid: str
    world_uuid: str
    target_key: str
    payload: dict[str, Any]


class FixtureError(RuntimeError):
    pass


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    db = (p.path or "/").lstrip("/")
    if not db:
        raise ValueError("database is missing in mysql URL")
    return Target(p.hostname or "localhost", p.port or 3306, unquote(p.username or ""), unquote(p.password or ""), db)


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
            sys.stderr.write(proc.stderr)
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def rows(out: str) -> list[list[str]]:
    return [line.split("\t") for line in out.splitlines() if line.strip()]


def scalar_int(value: Any, default: int | None = None) -> int | None:
    try:
        if value is None or value == "":
            return default
        return int(value)
    except (TypeError, ValueError):
        return default


def parse_world_item_key(value: str | None) -> dict[str, Any]:
    text = str(value or "")
    m = re.match(r"^world-item:(?P<world>.*):pid:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    m = re.match(r"^world_item:(?P<world>.*):(?P<pid>\d+):(?P<sym>\d+)(?::.*)?$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    return {"raw": text}


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


def load_pickups_from_outbox(target: Target, session_key: str, limit: int) -> list[PickupCandidate]:
    out = run_mysql(target, f"""
        SELECT action_kind, idempotency_key,
               BIN_TO_UUID(session_id,1), BIN_TO_UUID(character_id,1), BIN_TO_UUID(world_instance_id,1),
               COALESCE(target_key,''), request_payload
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(session_key + ':%')}
           AND action_kind = 'pickup_world_item'
         ORDER BY requested_at ASC, action_id ASC
         LIMIT {int(limit)};
    """)
    result: list[PickupCandidate] = []
    for row in rows(out):
        try:
            payload = json.loads(row[6] or "{}")
        except json.JSONDecodeError as exc:
            raise FixtureError(f"invalid request_payload for {row[1]}: {exc}") from exc
        if not isinstance(payload, dict):
            payload = {}
        result.append(PickupCandidate(row[0], row[1], row[2], row[3], row[4], row[5], payload))
    return result


def resolve_projection_rows(target: Target, action: PickupCandidate) -> dict[str, Any]:
    payload = action.payload
    raw_key = payload_first(payload, "world_item_entity_key", "engine_world_item_key", "target_key") or action.target_key
    parsed = parse_world_item_key(str(raw_key or ""))
    pid = scalar_int(payload_first(payload, "source_world_item_persistent_id", "item_persistent_id"), parsed.get("persistent_id"))
    sym = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"), parsed.get("symbol"))
    world = payload_first(payload, "world") or parsed.get("world")
    if pid is None or sym is None or not world:
        raise FixtureError(f"cannot parse world item identity from action {action.idempotency_key}: key={raw_key!r} pid={pid} sym={sym} world={world!r}")
    exact = str(raw_key or "")
    like = f"world_item:{world}:{pid}:{sym}:%"

    wes = rows(run_mysql(target, f"""
        SELECT BIN_TO_UUID(wes.world_entity_state_id,1), wes.entity_key, wes.lifecycle_state
          FROM world_entity_state wes
         WHERE wes.world_instance_id = UUID_TO_BIN({sql_literal(action.world_uuid)},1)
           AND wes.entity_kind = 'item'
           AND (
                wes.entity_key = {sql_literal(exact)}
                OR wes.entity_key LIKE {sql_literal(like)}
                OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED) = {int(pid)}
           )
           AND CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED) = {int(sym)}
         ORDER BY CASE WHEN wes.entity_key={sql_literal(exact)} THEN 0 ELSE 1 END, wes.updated_at DESC
         LIMIT 3;
    """))
    if not wes:
        raise FixtureError(f"world_entity_state row not found for {action.idempotency_key}: world={world} pid={pid} sym={sym}")
    if len(wes) > 1:
        raise FixtureError(f"ambiguous world_entity_state rows for {action.idempotency_key}: {len(wes)} candidates")

    wes_uuid, entity_key, wes_state = wes[0]
    ii = rows(run_mysql(target, f"""
        SELECT BIN_TO_UUID(ii.item_instance_id,1), ii.item_instance_key, ii.owner_type, ii.lifecycle_state, ii.quantity
          FROM item_instances ii
         WHERE ii.realm_id = (SELECT realm_id FROM realm_world_instances WHERE world_instance_id=UUID_TO_BIN({sql_literal(action.world_uuid)},1) LIMIT 1)
           AND (
                ii.item_instance_key LIKE CONCAT('%:world-item:', {sql_literal(entity_key)})
                OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.entity_key')) = {sql_literal(entity_key)}
                OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_spawn_key')) = {sql_literal(entity_key)}
                OR ii.item_instance_key LIKE {sql_literal('%:' + str(pid) + ':' + str(sym) + ':%')}
           )
         ORDER BY CASE WHEN ii.owner_type='world_entity' AND ii.lifecycle_state='active' THEN 0 ELSE 1 END,
                  ii.updated_at DESC
         LIMIT 3;
    """))
    if not ii:
        raise FixtureError(f"item_instances row not found for {action.idempotency_key}: entity_key={entity_key}")
    if len(ii) > 1:
        raise FixtureError(f"ambiguous item_instances rows for {action.idempotency_key}: {len(ii)} candidates")
    item_uuid, item_key, owner_type, item_state, quantity = ii[0]
    amount = scalar_int(payload_first(payload, "amount"), 1) or 1
    return {
        "idempotency_key": action.idempotency_key,
        "world_uuid": action.world_uuid,
        "character_uuid": action.character_uuid,
        "world_entity_state_uuid": wes_uuid,
        "entity_key": entity_key,
        "world_entity_lifecycle": wes_state,
        "item_instance_uuid": item_uuid,
        "item_instance_key": item_key,
        "item_owner_type": owner_type,
        "item_lifecycle": item_state,
        "quantity": max(amount, scalar_int(quantity, amount) or amount),
        "pid": pid,
        "sym": sym,
        "world": world,
    }


def print_plan(items: list[dict[str, Any]]) -> None:
    for it in items:
        print(
            f"pickup {it['idempotency_key']}\n"
            f"  world_entity={it['entity_key']} lifecycle={it['world_entity_lifecycle']} -> active\n"
            f"  item_instance={it['item_instance_key']} owner={it['item_owner_type']} lifecycle={it['item_lifecycle']} -> world_entity/active"
        )


def apply_fixture(target: Target, session_key: str, items: list[dict[str, Any]], reset_outbox: bool) -> None:
    for it in items:
        run_mysql(target, f"""
            START TRANSACTION;
            SET @wes_id = UUID_TO_BIN({sql_literal(it['world_entity_state_uuid'])}, 1);
            SET @item_id = UUID_TO_BIN({sql_literal(it['item_instance_uuid'])}, 1);

            DELETE FROM character_equipment
             WHERE item_instance_id = @item_id;

            DELETE FROM character_inventory
             WHERE item_instance_id = @item_id;

            UPDATE item_instances
               SET owner_type = 'world_entity',
                   owner_id = @wes_id,
                   quantity = {int(it['quantity'])},
                   lifecycle_state = 'active',
                   raw_payload = JSON_MERGE_PATCH(
                     COALESCE(raw_payload, JSON_OBJECT()),
                     JSON_OBJECT(
                       'entity_key', {sql_literal(it['entity_key'])},
                       'item_spawn_key', {sql_literal(it['entity_key'])},
                       'persistent_id', {int(it['pid'])},
                       'source_world_item_persistent_id', {int(it['pid'])},
                       'item_template_symbol', {int(it['sym'])},
                       'dev_fixture_restore_session_key', {sql_literal(session_key)},
                       'dev_fixture_restore_reason', 'step35_dispatch_projection_alignment'
                     )
                   ),
                   updated_at = CURRENT_TIMESTAMP(6)
             WHERE item_instance_id = @item_id;

            UPDATE world_entity_state
               SET lifecycle_state = 'active',
                   row_version = row_version + 1,
                   state_json = JSON_MERGE_PATCH(
                     COALESCE(state_json, JSON_OBJECT()),
                     JSON_OBJECT(
                       'persistent_id', {int(it['pid'])},
                       'item_template_symbol', {int(it['sym'])},
                       'dev_fixture_restore_session_key', {sql_literal(session_key)},
                       'dev_fixture_restore_reason', 'step35_dispatch_projection_alignment'
                     )
                   ),
                   updated_at = CURRENT_TIMESTAMP(6)
             WHERE world_entity_state_id = @wes_id;
            COMMIT;
        """)
        print(f"[FIXTURE] reactivated {it['entity_key']} item={it['item_instance_key']}")

    if reset_outbox:
        out = run_mysql(target, f"""
            UPDATE mmo_server_action_outbox
               SET status='pending',
                   attempt_count=0,
                   locked_at=NULL,
                   failed_at=NULL,
                   last_error_code=NULL,
                   last_error_message=NULL,
                   result_payload=JSON_OBJECT('reset_by','prepare_mmo_dispatch_dev_fixture','session_key',{sql_literal(session_key)})
             WHERE idempotency_key LIKE {sql_literal(session_key + ':%')}
               AND status IN ('failed','dead_letter','claimed');
            SELECT ROW_COUNT();
        """)
        print(f"[RESET] outbox rows={out.splitlines()[-1] if out else '0'}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Prepare DEV-only current-state projection for Step 35 resolved dispatch.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", required=True, help="idempotency/session prefix used by receiver, e.g. local-dev-PC_HERO_STEP35V2")
    ap.add_argument("--limit", type=int, default=20)
    ap.add_argument("--apply", action="store_true", help="actually mutate projections; without this the tool only prints a plan")
    ap.add_argument("--no-reset-outbox", action="store_true", help="do not reset matching failed/claimed outbox rows to pending")
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    pickups = load_pickups_from_outbox(target, args.session_key, args.limit)
    if not pickups:
        print("no pickup_world_item outbox rows found for this session key")
        return 1
    resolved = [resolve_projection_rows(target, p) for p in pickups]
    print_plan(resolved)
    if not args.apply:
        print("\n[DRY-RUN] no DB changes were made. Re-run with --apply to prepare the fixture.")
        return 0
    apply_fixture(target, args.session_key, resolved, reset_outbox=not args.no_reset_outbox)
    print("[OK] dev fixture prepared")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
