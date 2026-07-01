#!/usr/bin/env python3
"""Inspect why receiver-enqueued MMO actions resolve or fail.

Read-only diagnostic for Step 35. It does not claim outbox rows, call stored
procedures, or mutate projections. Use it when resolved worker reports errors
such as:
- world item resolved but is not active
- world item entity is not active
- character item instance not found
- equipment slot is empty
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


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    db = (p.path or "/").lstrip("/")
    if not db:
        raise ValueError("database missing in URL")
    return Target(p.hostname or "localhost", p.port or 3306, unquote(p.username or ""), unquote(p.password or ""), db)


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


def sql_literal(value: Any) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def scalar_int(value: Any, default: int | None = None) -> int | None:
    try:
        if value is None or value == "":
            return default
        return int(value)
    except (TypeError, ValueError):
        return default


def rows(out: str) -> list[list[str]]:
    # mysql --batch --raw --skip-column-names may omit visually empty trailing
    # fields after stdout.strip().  Keep empty middle columns and let callers pad
    # rows where they need a fixed projection width.
    return [ln.split("\t") for ln in out.splitlines() if ln.strip()]


def padded(row: list[str], width: int) -> list[str]:
    if len(row) >= width:
        return row
    return row + [""] * (width - len(row))


def parse_world_item_key(value: str | None) -> dict[str, Any]:
    text = str(value or "")
    m = re.match(r"^world-item:(?P<world>.*):pid:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    m = re.match(r"^world_item:(?P<world>.*):(?P<pid>\d+):(?P<sym>\d+)(?::.*)?$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    return {"raw": text}


def payload_first(payload: dict[str, Any], *keys: str) -> Any:
    client = payload.get("client_payload") if isinstance(payload.get("client_payload"), dict) else {}
    for key in keys:
        value = payload.get(key)
        if value not in (None, ""):
            return value
    for key in keys:
        value = client.get(key)
        if value not in (None, ""):
            return value
    return None




def next_free_from_rows(out: str) -> int:
    used: set[int] = set()
    for row in rows(out):
        try:
            idx = int(row[0])
        except (TypeError, ValueError, IndexError):
            continue
        if idx >= 0:
            used.add(idx)
    candidate = 0
    while candidate in used:
        candidate += 1
    return candidate


def inspect_character_bag_slots(target: Target, character_uuid: str, limit: int) -> None:
    out = run_mysql(target, f"""
        SELECT ci.bag_index, BIN_TO_UUID(ci.item_instance_id,1), ci.amount, it.symbol_index, ii.lifecycle_state, LEFT(ii.item_instance_key,120)
          FROM character_inventory ci
          JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
          JOIN content_item_templates it ON it.item_template_id = ii.item_template_id
         WHERE ci.character_id = UUID_TO_BIN({sql_literal(character_uuid)},1)
         ORDER BY ci.bag_index ASC
         LIMIT {int(limit)};
    """)
    print_table("  character_inventory occupied bag slots", out)
    print(f"  next_free_bag_index={next_free_from_rows(out)}")

def print_table(title: str, out: str) -> None:
    print(f"\n{title}:")
    print(out or "(none)")


def inspect_world_item(target: Target, action: dict[str, Any], limit: int) -> None:
    payload = action["payload"]
    world_uuid = action["world_uuid"]
    character_uuid = action["character_uuid"]
    raw_key = payload_first(payload, "world_item_entity_key", "engine_world_item_key", "target_key") or action.get("target_key")
    parsed = parse_world_item_key(str(raw_key or ""))
    pid = scalar_int(payload_first(payload, "source_world_item_persistent_id", "item_persistent_id"), parsed.get("persistent_id"))
    sym = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"), parsed.get("symbol"))
    exact = str(raw_key or "")
    like = f"world_item:{parsed.get('world')}:{pid}:{sym}:%" if parsed.get("world") and pid is not None and sym is not None else "__NO_LIKE__"

    print(f"  resolver_input key={exact!r} pid={pid} sym={sym} world={parsed.get('world')!r}")
    pred = f"""
        wes.world_instance_id = UUID_TO_BIN({sql_literal(world_uuid)},1)
        AND wes.entity_kind = 'item'
        AND (
             wes.entity_key = {sql_literal(exact)}
             OR wes.entity_key LIKE {sql_literal(like)}
             OR ({'TRUE' if pid is not None else 'FALSE'} AND CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS SIGNED) = {int(pid or -1)})
        )
        AND ({'TRUE' if sym is not None else 'FALSE'} = FALSE OR CAST(JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS SIGNED) = {int(sym or -1)})
    """
    out = run_mysql(target, f"""
        SELECT wes.entity_key,
               wes.lifecycle_state,
               JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.persistent_id')) AS persistent_id,
               JSON_UNQUOTE(JSON_EXTRACT(wes.state_json,'$.item_template_symbol')) AS template_symbol,
               wes.row_version,
               wes.updated_at
          FROM world_entity_state wes
         WHERE {pred}
         ORDER BY CASE WHEN wes.lifecycle_state='active' THEN 0 ELSE 1 END,
                  CASE WHEN wes.entity_key={sql_literal(exact)} THEN 0 ELSE 1 END,
                  wes.updated_at DESC
         LIMIT {int(limit)};
    """)
    print_table("  world_entity_state candidates", out)

    out = run_mysql(target, f"""
        SELECT ii.item_instance_key,
               ii.owner_type,
               ii.lifecycle_state,
               ii.quantity,
               JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.entity_key')) AS raw_entity_key,
               JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_spawn_key')) AS item_spawn_key,
               ii.updated_at
          FROM item_instances ii
         WHERE ii.realm_id = (SELECT realm_id FROM realm_world_instances WHERE world_instance_id=UUID_TO_BIN({sql_literal(world_uuid)},1) LIMIT 1)
           AND (
                ii.item_instance_key LIKE CONCAT('%:world-item:', {sql_literal(exact)})
                OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.entity_key')) = {sql_literal(exact)}
                OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.item_spawn_key')) = {sql_literal(exact)}
                OR ({'TRUE' if pid is not None else 'FALSE'} AND ii.item_instance_key LIKE {sql_literal('%:' + str(pid or -1) + ':' + str(sym or -1) + ':%')})
           )
         ORDER BY CASE WHEN ii.owner_type='world_entity' AND ii.lifecycle_state='active' THEN 0 ELSE 1 END,
                  ii.updated_at DESC
         LIMIT {int(limit)};
    """)
    print_table("  item_instances candidates", out)
    inspect_character_bag_slots(target, character_uuid, limit)


def inspect_character_item(target: Target, action: dict[str, Any], limit: int) -> None:
    payload = action["payload"]
    session_uuid = action["session_uuid"]
    character_uuid = action["character_uuid"]
    item_symbol = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"))
    item_pid = scalar_int(payload_first(payload, "item_persistent_id", "source_world_item_persistent_id"))
    slot = payload_first(payload, "equipment_slot", "slot")
    print(f"  resolver_input symbol={item_symbol} pid={item_pid} slot={slot}")
    if item_symbol is not None:
        pid_pred = "TRUE"
        if item_pid is not None:
            pid_pred = f"""(
                JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id')) = {sql_literal(item_pid)}
                OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.source_world_item_persistent_id')) = {sql_literal(item_pid)}
                OR ii.item_instance_key LIKE {sql_literal('%:' + str(item_pid) + ':' + str(item_symbol) + ':%')}
            )"""
        out = run_mysql(target, f"""
            SELECT BIN_TO_UUID(ii.item_instance_id,1), ii.item_instance_key, ii.owner_type, ii.lifecycle_state, ci.bag_index, ci.amount, ii.updated_at
              FROM item_instances ii
              JOIN character_inventory ci ON ci.item_instance_id = ii.item_instance_id
              JOIN content_item_templates it ON it.item_template_id = ii.item_template_id
             WHERE ii.realm_id = (SELECT realm_id FROM server_sessions WHERE session_id = UUID_TO_BIN({sql_literal(session_uuid)},1) LIMIT 1)
               AND ii.owner_type = 'character'
               AND ii.owner_id = UUID_TO_BIN({sql_literal(character_uuid)},1)
               AND ci.character_id = UUID_TO_BIN({sql_literal(character_uuid)},1)
               AND it.symbol_index = {int(item_symbol)}
               AND {pid_pred}
             ORDER BY CASE WHEN ii.lifecycle_state='active' THEN 0 ELSE 1 END, ii.updated_at DESC
             LIMIT {int(limit)};
        """)
        print_table("  character_inventory candidates", out)

    out = run_mysql(target, f"""
        SELECT equipment_slot, BIN_TO_UUID(item_instance_id,1), equipped_at, updated_at
          FROM character_equipment
         WHERE character_id = UUID_TO_BIN({sql_literal(character_uuid)},1)
         ORDER BY equipment_slot
         LIMIT {int(limit)};
    """)
    print_table("  character_equipment current slots", out)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Inspect resolver candidates for receiver-enqueued MMO actions.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--limit", type=int, default=10)
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    prefix = args.session_key + ":%"
    out = run_mysql(target, f"""
        SELECT BIN_TO_UUID(action_id,1), action_kind, status, BIN_TO_UUID(session_id,1), BIN_TO_UUID(character_id,1),
               BIN_TO_UUID(world_instance_id,1), target_key, idempotency_key, request_payload,
               COALESCE(last_error_code,''), LEFT(COALESCE(last_error_message,''),240)
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         ORDER BY requested_at ASC, action_id ASC
         LIMIT {int(args.limit)};
    """)
    actions = rows(out)
    if not actions:
        print("no matching outbox rows")
        return 1

    for raw_row in actions:
        row = padded(raw_row, 11)
        payload = json.loads(row[8] or "{}")
        if not isinstance(payload, dict):
            payload = {}
        action = {
            "action_uuid": row[0],
            "kind": row[1],
            "status": row[2],
            "session_uuid": row[3],
            "character_uuid": row[4],
            "world_uuid": row[5],
            "target_key": row[6],
            "idempotency_key": row[7],
            "payload": payload,
        }
        print("=" * 100)
        print(f"{row[1]} status={row[2]} action={row[0]} idem={row[7]}")
        if row[9] or row[10]:
            print(f"  last_error={row[9]} {row[10]}")
        if row[1] in {"pickup_world_item", "remove_world_item"}:
            inspect_world_item(target, action, args.limit)
        elif row[1] in {"equip_character_item", "unequip_character_item"}:
            inspect_character_item(target, action, args.limit)
        else:
            print("  no resolver inspector for this action kind yet")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
