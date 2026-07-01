#!/usr/bin/env python3
"""Prepare a dev-only MySQL projection fixture for Step38 replay.

This tool deliberately lives outside the OpenGothic process. It is not a
production repair path and should not be used as parity proof. It only makes a
local MySQL projection match a captured Step38 JSONL enough to exercise the
server-boundary worker/procedure chain when the runtime save and MySQL import
are out of sync.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

STEP38_KINDS = {
    "trade_buy_from_npc",
    "trade_sell_to_npc",
    "consume_item",
    "apply_world_entity_damage",
    "mark_npc_dead",
}

@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str

@dataclass(frozen=True)
class SessionContext:
    session_uuid: str
    realm_uuid: str
    character_uuid: str
    world_uuid: str


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

def table_columns(target: Target, table: str) -> set[str]:
    out = run_mysql(target, f"""
        SELECT column_name
          FROM information_schema.columns
         WHERE table_schema = DATABASE()
           AND table_name = {sql_literal(table)}
         ORDER BY ordinal_position;
    """)
    return {line.strip() for line in out.splitlines() if line.strip()}


def preferred_order_column(target: Target, table: str, candidates: tuple[str, ...], fallback: str) -> str:
    existing = table_columns(target, table)
    for candidate in candidates:
        if candidate in existing:
            return candidate
    return fallback



def scalar_int(value: Any, default: int | None = None) -> int | None:
    if value is None or value == "":
        return default
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def payload_first(payload: dict[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in payload and payload[key] not in (None, ""):
            return payload[key]
    return None


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            text = line.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if isinstance(obj, dict) and str(obj.get("action_kind") or "") in STEP38_KINDS:
                rows.append(obj)
    return rows


def parse_npc_key(value: str | None) -> dict[str, Any]:
    text = str(value or "")
    m = re.match(r"^npc:(?P<world>.*):pid:(?P<pid>\d+):sym:(?P<sym>\d+)$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    m = re.match(r"^npc:(?P<world>.*):(?P<pid>\d+):(?P<sym>\d+)(?::(?P<script>\d+))?$", text)
    if m:
        return {"world": m.group("world"), "persistent_id": int(m.group("pid")), "symbol": int(m.group("sym")), "raw": text}
    return {"raw": text}


def npc_key_from_payload(obj: dict[str, Any], payload: dict[str, Any]) -> str | None:
    return (
        payload_first(payload, "target_npc_entity_key", "npc_entity_key", "npc_key", "target_key")
        or obj.get("target_key")
    )


def total_price_from_payload(payload: dict[str, Any]) -> int:
    price_total = scalar_int(payload_first(payload, "price_total"), None)
    if price_total is not None:
        return max(0, price_total)
    unit = scalar_int(payload_first(payload, "unit_price"), 0) or 0
    amount = max(1, scalar_int(payload_first(payload, "amount"), 1) or 1)
    return max(0, unit * amount)


def session_context(target: Target, session_key: str) -> SessionContext:
    # Current MySQL schema has login_at/last_seen_at on server_sessions, not
    # updated_at. Older local scripts assumed updated_at; keep this lookup
    # schema-aware so dev fixtures do not break on a valid Step30 database.
    session_order = preferred_order_column(
        target,
        "server_sessions",
        ("last_seen_at", "login_at", "logout_at", "created_at", "updated_at"),
        "session_key",
    )
    outbox_order = preferred_order_column(
        target,
        "mmo_server_action_outbox",
        ("requested_at", "updated_at", "applied_at", "failed_at"),
        "idempotency_key",
    )
    row = first_row(target, f"""
        SELECT BIN_TO_UUID(ss.session_id,1), BIN_TO_UUID(ss.realm_id,1), BIN_TO_UUID(ss.character_id,1), BIN_TO_UUID(ss.world_instance_id,1)
          FROM server_sessions ss
         WHERE ss.session_key = {sql_literal(session_key)}
         ORDER BY ss.{session_order} DESC
         LIMIT 1;
    """)
    source = "server_sessions"
    if not row:
        row = first_row(target, f"""
            SELECT BIN_TO_UUID(ss.session_id,1), BIN_TO_UUID(ss.realm_id,1), BIN_TO_UUID(o.character_id,1), BIN_TO_UUID(o.world_instance_id,1)
              FROM mmo_server_action_outbox o
              JOIN server_sessions ss ON ss.session_id=o.session_id
             WHERE o.idempotency_key LIKE {sql_literal(session_key + ':%')}
             ORDER BY o.{outbox_order} DESC
             LIMIT 1;
        """)
        source = "mmo_server_action_outbox"
    if len(row) < 4:
        raise RuntimeError(f"server session not found for session_key={session_key!r}; run receiver/e2e first")
    ctx = SessionContext(row[0], row[1], row[2], row[3])
    object.__setattr__(ctx, "_source", source)
    object.__setattr__(ctx, "_session_order", session_order)
    object.__setattr__(ctx, "_outbox_order", outbox_order)
    return ctx

def next_character_bag_index(target: Target, character_uuid: str) -> int:
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
            used.add(int(line.split("\t")[0]))
        except ValueError:
            pass
    candidate = 0
    while candidate in used:
        candidate += 1
    return candidate


def npc_fixture_rows(rows: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    grouped: dict[str, dict[str, Any]] = {}
    for obj in rows:
        kind = str(obj.get("action_kind") or "")
        if kind not in {"apply_world_entity_damage", "mark_npc_dead", "trade_buy_from_npc", "trade_sell_to_npc"}:
            continue
        payload = obj.get("payload") if isinstance(obj.get("payload"), dict) else {}
        raw_key = npc_key_from_payload(obj, payload)
        if not raw_key:
            continue
        parsed = parse_npc_key(str(raw_key))
        health = scalar_int(payload_first(payload, "value_before", "target_health_before"), None)
        damage = scalar_int(payload_first(payload, "damage_amount", "amount", "delta"), 1) or 1
        current = grouped.setdefault(str(raw_key), {
            "key": str(raw_key),
            "world": parsed.get("world"),
            "persistent_id": parsed.get("persistent_id"),
            "symbol": parsed.get("symbol"),
            "health": max(health or damage or 10, damage, 1),
            "pos_x": 0.0,
            "pos_y": 0.0,
            "pos_z": 0.0,
            "actions": 0,
            "action_kinds": [],
        })
        current["actions"] += 1
        current["action_kinds"].append(kind)
        if health is not None:
            current["health"] = max(int(current["health"]), int(health))
        pos = payload.get("target_position") or payload.get("npc_position")
        if isinstance(pos, dict):
            current["pos_x"] = pos.get("x", current["pos_x"])
            current["pos_y"] = pos.get("y", current["pos_y"])
            current["pos_z"] = pos.get("z", current["pos_z"])
    return grouped


def item_fixture_rows(rows: list[dict[str, Any]]) -> dict[tuple[int, int | None, str], dict[str, Any]]:
    grouped: dict[tuple[int, int | None, str], dict[str, Any]] = {}
    for obj in rows:
        kind = str(obj.get("action_kind") or "")
        if kind not in {"consume_item", "trade_sell_to_npc"}:
            continue
        payload = obj.get("payload") if isinstance(obj.get("payload"), dict) else {}
        symbol = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"))
        if symbol is None:
            continue
        pid = scalar_int(payload_first(payload, "item_persistent_id", "seller_item_persistent_id", "source_world_item_persistent_id"))
        amount = max(1, scalar_int(payload_first(payload, "amount"), 1) or 1)
        purpose = "trade_sell" if kind == "trade_sell_to_npc" else "consume_item"
        key = (symbol, pid, purpose)
        current = grouped.setdefault(key, {"symbol": symbol, "persistent_id": pid, "amount": 0, "actions": 0, "purpose": purpose})
        current["amount"] += amount
        current["actions"] += 1
    return grouped


def npc_trade_fixture_rows(rows: list[dict[str, Any]]) -> dict[tuple[str, int, int | None], dict[str, Any]]:
    grouped: dict[tuple[str, int, int | None], dict[str, Any]] = {}
    for obj in rows:
        if str(obj.get("action_kind") or "") != "trade_buy_from_npc":
            continue
        payload = obj.get("payload") if isinstance(obj.get("payload"), dict) else {}
        npc_key = npc_key_from_payload(obj, payload)
        symbol = scalar_int(payload_first(payload, "item_symbol", "inventory_item_symbol"))
        if not npc_key or symbol is None:
            continue
        pid = scalar_int(payload_first(payload, "vendor_item_persistent_id", "item_persistent_id", "source_world_item_persistent_id"))
        amount = max(1, scalar_int(payload_first(payload, "amount"), 1) or 1)
        unit_price = scalar_int(payload_first(payload, "unit_price"), None)
        price_total = total_price_from_payload(payload)
        price_for_proc = max(price_total, 0)
        key = (str(npc_key), int(symbol), pid)
        current = grouped.setdefault(key, {
            "npc_key": str(npc_key),
            "symbol": int(symbol),
            "persistent_id": pid,
            "amount": 0,
            "unit_price": unit_price if unit_price is not None else price_for_proc,
            "price_total": 0,
            "currency_key": str(payload_first(payload, "currency_key") or "g2notr:gold"),
            "actions": 0,
        })
        current["amount"] += amount
        current["price_total"] = max(int(current["price_total"]), price_for_proc)
        current["unit_price"] = max(int(current.get("unit_price") or 0), price_for_proc if unit_price is None else int(unit_price))
        current["actions"] += 1
    return grouped


def wallet_fixture(rows: list[dict[str, Any]]) -> dict[str, Any] | None:
    required = 0
    currency = "g2notr:gold"
    for obj in rows:
        if str(obj.get("action_kind") or "") != "trade_buy_from_npc":
            continue
        payload = obj.get("payload") if isinstance(obj.get("payload"), dict) else {}
        required = max(required, total_price_from_payload(payload), scalar_int(payload_first(payload, "wallet_before"), 0) or 0)
        currency = str(payload_first(payload, "currency_key") or currency)
    if required <= 0:
        return None
    return {"currency_key": currency, "amount": max(required, 100)}


def npc_candidate_count(target: Target, ctx: SessionContext, item: dict[str, Any]) -> int:
    key = item["key"]
    alias = "__NO_ALIAS__"
    if item.get("world") and item.get("persistent_id") is not None and item.get("symbol") is not None:
        alias = f"npc:{item['world']}:{item['persistent_id']}:{item['symbol']}:%"
    row = first_row(target, f"""
        SELECT COUNT(*)
          FROM world_entity_state
         WHERE world_instance_id = UUID_TO_BIN({sql_literal(ctx.world_uuid)},1)
           AND entity_kind IN ('npc','creature')
           AND (entity_key={sql_literal(key)} OR entity_key LIKE {sql_literal(alias)});
    """)
    return int(row[0]) if row else 0


def prepare_npc(target: Target, ctx: SessionContext, item: dict[str, Any], apply: bool) -> dict[str, Any]:
    count = npc_candidate_count(target, ctx, item)
    health = max(1, int(item.get("health") or 10))
    manifest = {"kind": "npc", "key": item["key"], "existing_candidates": count, "health": health, "applied": False}
    if not apply:
        manifest["operation"] = "update_existing" if count else "insert_missing"
        return manifest
    state = {
        "source": "step38_dev_fixture",
        "requested_key": item["key"],
        "persistent_id": item.get("persistent_id"),
        "symbol_index": item.get("symbol"),
        "actions": item.get("actions", 0),
    }
    if count:
        alias = "__NO_ALIAS__"
        if item.get("world") and item.get("persistent_id") is not None and item.get("symbol") is not None:
            alias = f"npc:{item['world']}:{item['persistent_id']}:{item['symbol']}:%"
        run_mysql(target, f"""
            UPDATE world_entity_state
               SET lifecycle_state='active',
                   health_current=GREATEST(COALESCE(health_current,0), {health}),
                   health_max=GREATEST(COALESCE(health_max,0), {health}),
                   state_json=JSON_SET(COALESCE(state_json, JSON_OBJECT()), '$.step38_dev_fixture', TRUE, '$.step38_requested_key', {sql_literal(item['key'])}),
                   row_version=row_version+1,
                   updated_at=CURRENT_TIMESTAMP(6)
             WHERE world_instance_id=UUID_TO_BIN({sql_literal(ctx.world_uuid)},1)
               AND entity_kind IN ('npc','creature')
               AND (entity_key={sql_literal(item['key'])} OR entity_key LIKE {sql_literal(alias)});
        """)
        manifest["operation"] = "update_existing"
    else:
        run_mysql(target, f"""
            SET @entity_template_id = NULL;
            SELECT entity_template_id INTO @entity_template_id
              FROM content_entity_templates
             WHERE entity_kind IN ('creature','npc')
             ORDER BY CASE entity_kind WHEN 'creature' THEN 0 ELSE 1 END
             LIMIT 1;
            INSERT INTO world_entity_state(
              world_instance_id, entity_key, entity_kind, entity_template_id, lifecycle_state,
              pos_x, pos_y, pos_z, rotation_yaw, health_current, health_max, state_json, row_version
            ) VALUES(
              UUID_TO_BIN({sql_literal(ctx.world_uuid)},1), {sql_literal(item['key'])}, 'creature', @entity_template_id, 'active',
              {float(item.get('pos_x') or 0.0)}, {float(item.get('pos_y') or 0.0)}, {float(item.get('pos_z') or 0.0)}, 0.0,
              {health}, {health}, {json_sql(state)}, 0
            )
            ON DUPLICATE KEY UPDATE
              lifecycle_state='active', health_current=VALUES(health_current), health_max=VALUES(health_max),
              state_json=VALUES(state_json), row_version=row_version+1, updated_at=CURRENT_TIMESTAMP(6);
        """)
        manifest["operation"] = "insert_missing"
    manifest["applied"] = True
    return manifest


def preferred_character_item_count(target: Target, ctx: SessionContext, symbol: int, pid: int | None) -> int:
    if pid is None:
        return 0
    row = first_row(target, f"""
        SELECT COUNT(*)
          FROM item_instances ii
          JOIN character_inventory ci ON ci.item_instance_id=ii.item_instance_id
          JOIN content_item_templates it ON it.item_template_id=ii.item_template_id
         WHERE ii.realm_id=UUID_TO_BIN({sql_literal(ctx.realm_uuid)},1)
           AND ii.owner_type='character'
           AND ii.owner_id=UUID_TO_BIN({sql_literal(ctx.character_uuid)},1)
           AND ii.lifecycle_state='active'
           AND ci.character_id=UUID_TO_BIN({sql_literal(ctx.character_uuid)},1)
           AND it.symbol_index={int(symbol)}
           AND (
              JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id'))={sql_literal(pid)}
              OR ii.item_instance_key LIKE {sql_literal('%:' + str(symbol) + ':' + str(pid) + ':%')}
              OR ii.item_instance_key LIKE {sql_literal('%:' + str(pid) + ':' + str(symbol) + ':%')}
           );
    """)
    return int(row[0]) if row else 0


def prepare_item(target: Target, ctx: SessionContext, item: dict[str, Any], session_key: str, apply: bool) -> dict[str, Any]:
    symbol = int(item["symbol"])
    pid = item.get("persistent_id")
    amount = max(1, int(item.get("amount") or 1))
    preferred = preferred_character_item_count(target, ctx, symbol, pid)
    manifest = {"kind": "character_item", "symbol": symbol, "persistent_id": pid, "amount": amount, "preferred_candidates": preferred, "applied": False}
    if preferred == 1:
        manifest["operation"] = "already_resolvable"
        return manifest
    if preferred > 1:
        manifest["operation"] = "ambiguous_existing_preferred"
        return manifest
    item_key = f"fixture:step38:{session_key}:character:{symbol}:{pid if pid is not None else 'nopid'}:0"
    bag = next_character_bag_index(target, ctx.character_uuid)
    manifest["operation"] = "insert_preferred_fixture"
    manifest["item_instance_key"] = item_key
    manifest["bag_index"] = bag
    if not apply:
        return manifest
    row = first_row(target, f"""
        SELECT BIN_TO_UUID(item_template_id,1)
          FROM content_item_templates
         WHERE symbol_index={symbol}
         LIMIT 1;
    """)
    if not row:
        raise RuntimeError(f"content_item_templates row not found for symbol={symbol}")
    run_mysql(target, f"""
        SET @item_template_id = UUID_TO_BIN({sql_literal(row[0])},1);
        INSERT INTO item_instances(realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, lifecycle_state, raw_payload)
        VALUES(
          UUID_TO_BIN({sql_literal(ctx.realm_uuid)},1), @item_template_id, {sql_literal(item_key)}, 'character', UUID_TO_BIN({sql_literal(ctx.character_uuid)},1),
          {amount}, 'active', JSON_OBJECT('source','step38_dev_fixture','persistent_id',{sql_literal(pid)},'item_symbol',{symbol})
        )
        ON DUPLICATE KEY UPDATE
          owner_type='character', owner_id=UUID_TO_BIN({sql_literal(ctx.character_uuid)},1), quantity=GREATEST(quantity, VALUES(quantity)),
          lifecycle_state='active', raw_payload=VALUES(raw_payload), updated_at=CURRENT_TIMESTAMP(6);
        SET @item_id = (SELECT item_instance_id FROM item_instances WHERE item_instance_key={sql_literal(item_key)} LIMIT 1);
        INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
        VALUES(UUID_TO_BIN({sql_literal(ctx.character_uuid)},1), @item_id, {bag}, {amount}, {amount}, {amount})
        ON DUPLICATE KEY UPDATE amount=GREATEST(amount, VALUES(amount)), source_amount=VALUES(source_amount), source_iterator_count=VALUES(source_iterator_count);
    """)
    manifest["applied"] = True
    return manifest


def npc_trade_item_candidate_count(target: Target, ctx: SessionContext, npc_key: str, symbol: int, pid: int | None, currency_key: str) -> int:
    pid_filter = "TRUE"
    if pid is not None:
        pid_filter = f"""(
          JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.persistent_id'))={sql_literal(pid)}
          OR JSON_UNQUOTE(JSON_EXTRACT(ii.raw_payload,'$.vendor_item_persistent_id'))={sql_literal(pid)}
          OR ii.item_instance_key LIKE {sql_literal('%:' + str(symbol) + ':' + str(pid) + ':%')}
          OR ii.item_instance_key LIKE {sql_literal('%:' + str(pid) + ':' + str(symbol) + ':%')}
        )"""
    row = first_row(target, f"""
        SELECT COUNT(*)
          FROM npc_trade_inventory nti
          JOIN item_instances ii ON ii.item_instance_id=nti.item_instance_id
          JOIN content_item_templates it ON it.item_template_id=ii.item_template_id
         WHERE nti.world_instance_id=UUID_TO_BIN({sql_literal(ctx.world_uuid)},1)
           AND nti.npc_entity_key={sql_literal(npc_key)}
           AND nti.stock_state='available'
           AND nti.currency_key={sql_literal(currency_key)}
           AND ii.lifecycle_state='active'
           AND it.symbol_index={int(symbol)}
           AND {pid_filter};
    """)
    return int(row[0]) if row else 0


def prepare_npc_trade_item(target: Target, ctx: SessionContext, item: dict[str, Any], session_key: str, apply: bool) -> dict[str, Any]:
    npc_key = str(item["npc_key"])
    symbol = int(item["symbol"])
    pid = item.get("persistent_id")
    amount = max(1, int(item.get("amount") or 1))
    currency_key = str(item.get("currency_key") or "g2notr:gold")
    price = max(0, int(item.get("price_total") or item.get("unit_price") or 0))
    if price == 0:
        price = max(0, int(item.get("unit_price") or 0))
    candidates = npc_trade_item_candidate_count(target, ctx, npc_key, symbol, pid, currency_key)
    manifest = {
        "kind": "npc_trade_item",
        "npc_key": npc_key,
        "symbol": symbol,
        "persistent_id": pid,
        "amount": amount,
        "price": price,
        "currency_key": currency_key,
        "existing_candidates": candidates,
        "applied": False,
    }
    if candidates == 1:
        manifest["operation"] = "already_resolvable"
        return manifest
    if candidates > 1:
        manifest["operation"] = "ambiguous_existing_trade_stock"
        return manifest
    item_key = f"fixture:step38:{session_key}:npc_trade:{npc_key}:{symbol}:{pid if pid is not None else 'nopid'}:0"
    manifest["operation"] = "insert_npc_trade_fixture"
    manifest["item_instance_key"] = item_key
    if not apply:
        return manifest
    row = first_row(target, f"""
        SELECT BIN_TO_UUID(item_template_id,1)
          FROM content_item_templates
         WHERE symbol_index={symbol}
         LIMIT 1;
    """)
    if not row:
        raise RuntimeError(f"content_item_templates row not found for trade symbol={symbol}")
    run_mysql(target, f"""
        SET @item_template_id = UUID_TO_BIN({sql_literal(row[0])},1);
        INSERT INTO item_instances(realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, lifecycle_state, raw_payload)
        VALUES(
          UUID_TO_BIN({sql_literal(ctx.realm_uuid)},1), @item_template_id, {sql_literal(item_key)}, 'system', NULL,
          {amount}, 'active', JSON_OBJECT('source','step38_dev_fixture','purpose','npc_trade_inventory','persistent_id',{sql_literal(pid)},'vendor_item_persistent_id',{sql_literal(pid)},'item_symbol',{symbol})
        )
        ON DUPLICATE KEY UPDATE
          owner_type='system', owner_id=NULL, quantity=GREATEST(quantity, VALUES(quantity)),
          lifecycle_state='active', raw_payload=VALUES(raw_payload), updated_at=CURRENT_TIMESTAMP(6);
        SET @item_id = (SELECT item_instance_id FROM item_instances WHERE item_instance_key={sql_literal(item_key)} LIMIT 1);
        INSERT INTO npc_trade_inventory(world_instance_id, npc_entity_key, item_instance_id, amount, unit_price, currency_key, stock_state, raw_payload)
        VALUES(UUID_TO_BIN({sql_literal(ctx.world_uuid)},1), {sql_literal(npc_key)}, @item_id, {amount}, {price}, {sql_literal(currency_key)}, 'available', JSON_OBJECT('source','step38_dev_fixture','purpose','trade_buy_from_npc'))
        ON DUPLICATE KEY UPDATE amount=GREATEST(amount, VALUES(amount)), unit_price=VALUES(unit_price), currency_key=VALUES(currency_key), stock_state='available', raw_payload=VALUES(raw_payload);
    """)
    manifest["applied"] = True
    return manifest


def prepare_wallet(target: Target, ctx: SessionContext, item: dict[str, Any] | None, apply: bool) -> dict[str, Any] | None:
    if item is None:
        return None
    currency_key = str(item.get("currency_key") or "g2notr:gold")
    amount = max(0, int(item.get("amount") or 0))
    manifest = {"kind": "character_wallet", "currency_key": currency_key, "amount": amount, "applied": False}
    if amount <= 0:
        manifest["operation"] = "not_needed"
        return manifest
    row = first_row(target, f"""
        SELECT CAST(amount AS SIGNED)
          FROM character_wallets
         WHERE character_id=UUID_TO_BIN({sql_literal(ctx.character_uuid)},1)
           AND currency_key={sql_literal(currency_key)};
    """)
    before = int(row[0]) if row else 0
    manifest["amount_before"] = before
    manifest["operation"] = "already_sufficient" if before >= amount else "upsert_wallet_floor"
    if not apply or before >= amount:
        return manifest
    run_mysql(target, f"""
        INSERT INTO character_wallets(character_id, currency_key, amount)
        VALUES(UUID_TO_BIN({sql_literal(ctx.character_uuid)},1), {sql_literal(currency_key)}, {amount})
        ON DUPLICATE KEY UPDATE amount=GREATEST(amount, VALUES(amount));
    """)
    manifest["applied"] = True
    return manifest


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Prepare dev-only Step38 MySQL fixture from captured JSONL")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", required=True)
    ap.add_argument("--client-jsonl", type=Path, required=True)
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--output", type=Path, default=Path("runtime/mmo_step38_dev_fixture.json"))
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    rows = load_jsonl(args.client_jsonl)
    ctx = session_context(target, args.session_key)
    result: dict[str, Any] = {
        "tool": "prepare_mmo_step38_dev_fixture.py",
        "status": "running",
        "mode": "apply" if args.apply else "dry_run",
        "session_key": args.session_key,
        "client_jsonl": str(args.client_jsonl),
        "rows": len(rows),
        "session": {
            "session_uuid": ctx.session_uuid,
            "realm_uuid": ctx.realm_uuid,
            "character_uuid": ctx.character_uuid,
            "world_uuid": ctx.world_uuid,
            "source": getattr(ctx, "_source", "unknown"),
            "session_order_column": getattr(ctx, "_session_order", "unknown"),
            "outbox_order_column": getattr(ctx, "_outbox_order", "unknown"),
        },
        "npc": [],
        "character_items": [],
        "npc_trade_items": [],
        "wallet": None,
    }
    try:
        for item in npc_fixture_rows(rows).values():
            result["npc"].append(prepare_npc(target, ctx, item, args.apply))
        wallet = prepare_wallet(target, ctx, wallet_fixture(rows), args.apply)
        if wallet is not None:
            result["wallet"] = wallet
        for item in item_fixture_rows(rows).values():
            result["character_items"].append(prepare_item(target, ctx, item, args.session_key, args.apply))
        for item in npc_trade_fixture_rows(rows).values():
            result["npc_trade_items"].append(prepare_npc_trade_item(target, ctx, item, args.session_key, args.apply))
        result["status"] = "applied" if args.apply else "planned"
    except Exception as exc:
        result["status"] = "failed"
        result["error"] = str(exc)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
        print(f"artifact={args.output}")
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    print(f"artifact={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))



