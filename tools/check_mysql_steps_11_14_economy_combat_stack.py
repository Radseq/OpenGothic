#!/usr/bin/env python3
"""Validate and smoke-test MySQL production steps 011..014.

Uses only the mysql command-line client. Requires migrations 001..014 and a
bootstrap-imported account/character, plus earlier write-path procedures.
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
    return MySqlTarget(parsed.hostname or "localhost", int(parsed.port or 3306), unquote(parsed.username or "root"), unquote(parsed.password or ""), database)


def mysql_cmd(target: MySqlTarget) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [exe, f"--host={target.host}", f"--port={target.port}", f"--user={target.user}", "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names"]
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
        check_marker(target, "production/mysql/011_trade_economy_write_path", "gothic-mmo-trade-economy-write-path-v1-mysql", "migration 011 marker"),
        check_marker(target, "production/mysql/012_combat_resource_write_path", "gothic-mmo-combat-resource-write-path-v1-mysql", "migration 012 marker"),
        check_marker(target, "production/mysql/013_item_stack_write_path", "gothic-mmo-item-stack-write-path-v1-mysql", "migration 013 marker"),
        check_marker(target, "production/mysql/014_projection_diagnostics", "gothic-mmo-projection-diagnostics-v1-mysql", "migration 014 marker"),
        check_named(target, "steps 011..014 tables", f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';", ("npc_trade_inventory", "trade_economy_audit", "combat_resource_audit", "item_stack_audit")),
        check_named(target, "steps 011..014 views", f"SELECT table_name FROM information_schema.views WHERE table_schema='{db}';", ("v_npc_trade_inventory", "v_trade_economy_audit", "v_combat_resource_audit", "v_character_combat_sheet", "v_item_stack_audit", "v_character_stack_items", "v_projection_validation_latest_errors", "v_item_projection_diagnostics", "v_world_entity_projection_diagnostics")),
        check_named(target, "steps 011..014 routines", f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';", ("mmo_trade_buy_from_npc", "mmo_trade_sell_to_npc", "mmo_apply_character_damage", "mmo_apply_world_entity_damage", "mmo_consume_character_mana", "mmo_consume_character_item", "mmo_split_character_item_stack", "mmo_merge_character_item_stack", "mmo_validate_world_projection_extended")),
    ]


def call_login(target: MySqlTarget, account_name: str, character_key: str, session_key: str) -> str:
    return scalar(target, f"""
        SET @session_id = NULL;
        CALL mmo_login_character({sql_literal(account_name)}, {sql_literal(character_key)}, {sql_literal(session_key)}, 'steps-11-14-smoke', '127.0.0.1', JSON_OBJECT('source','check_mysql_steps_11_14'), @session_id);
        SELECT BIN_TO_UUID(@session_id, 1);
    """)


def call_logout(target: MySqlTarget, session_uuid: str, reason: str = "smoke done") -> str:
    return scalar(target, f"""
        SET @event_id = NULL;
        CALL mmo_logout_character(UUID_TO_BIN({sql_literal(session_uuid)}, 1), {sql_literal(reason)}, JSON_OBJECT('source','check_mysql_steps_11_14'), @event_id);
        SELECT BIN_TO_UUID(@event_id, 1);
    """)


def seed_world_npc(target: MySqlTarget, session_uuid: str, npc_key: str) -> None:
    run_mysql(target, f"""
        SET @session_id = UUID_TO_BIN({sql_literal(session_uuid)}, 1);
        SELECT realm_id, world_instance_id INTO @realm_id, @world_id FROM server_sessions WHERE session_id=@session_id;
        SELECT entity_template_id INTO @entity_template_id FROM content_entity_templates WHERE entity_kind IN ('npc','creature') LIMIT 1;
        INSERT INTO world_entity_state(world_instance_id, entity_key, entity_kind, entity_template_id, lifecycle_state, pos_x, pos_y, pos_z, rotation_yaw, health_current, health_max, state_json)
        VALUES(@world_id, {sql_literal(npc_key)}, 'npc', @entity_template_id, 'active', 1,2,3,0, 10,10, JSON_OBJECT('source','steps-11-14-smoke'))
        ON DUPLICATE KEY UPDATE lifecycle_state='active', health_current=10, health_max=10, state_json=JSON_OBJECT('source','steps-11-14-smoke'), updated_at=CURRENT_TIMESTAMP(6);
    """)


def seed_item(target: MySqlTarget, session_uuid: str, item_key: str, owner_sql: str, amount: int) -> str:
    return scalar(target, f"""
        SET @session_id = UUID_TO_BIN({sql_literal(session_uuid)}, 1);
        SELECT realm_id, character_id, world_instance_id INTO @realm_id, @character_id, @world_id FROM server_sessions WHERE session_id=@session_id;
        SELECT item_template_id INTO @item_template_id FROM content_item_templates LIMIT 1;
        INSERT INTO item_instances(realm_id, item_template_id, item_instance_key, owner_type, owner_id, quantity, lifecycle_state, raw_payload)
        VALUES(@realm_id, @item_template_id, {sql_literal(item_key)}, {owner_sql}, IF({owner_sql}='character', @character_id, NULL), {amount}, 'active', JSON_OBJECT('source','steps-11-14-smoke'))
        ON DUPLICATE KEY UPDATE quantity=VALUES(quantity), lifecycle_state='active', owner_type=VALUES(owner_type), owner_id=VALUES(owner_id), raw_payload=VALUES(raw_payload);
        SELECT BIN_TO_UUID(item_instance_id, 1) FROM item_instances WHERE item_instance_key={sql_literal(item_key)};
    """)


def seed_character_inventory_item(target: MySqlTarget, session_uuid: str, item_key: str, amount: int, bag_index: int | None) -> str:
    item_uuid = seed_item(target, session_uuid, item_key, "'character'", amount)
    bag_sql = "NULL" if bag_index is None else str(bag_index)
    run_mysql(target, f"""
        SET @session_id = UUID_TO_BIN({sql_literal(session_uuid)}, 1);
        SELECT character_id INTO @character_id FROM server_sessions WHERE session_id=@session_id;
        SET @item_id = UUID_TO_BIN({sql_literal(item_uuid)}, 1);
        INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
        VALUES(@character_id, @item_id, {bag_sql}, {amount}, {amount}, {amount})
        ON DUPLICATE KEY UPDATE bag_index=VALUES(bag_index), amount=VALUES(amount), source_amount=VALUES(source_amount), source_iterator_count=VALUES(source_iterator_count);
    """)
    return item_uuid


def seed_npc_trade_item(target: MySqlTarget, session_uuid: str, npc_key: str, item_key: str, amount: int, price: int) -> str:
    item_uuid = seed_item(target, session_uuid, item_key, "'system'", amount)
    run_mysql(target, f"""
        SET @session_id = UUID_TO_BIN({sql_literal(session_uuid)}, 1);
        SELECT world_instance_id INTO @world_id FROM server_sessions WHERE session_id=@session_id;
        SET @item_id = UUID_TO_BIN({sql_literal(item_uuid)}, 1);
        INSERT INTO npc_trade_inventory(world_instance_id, npc_entity_key, item_instance_id, amount, unit_price, currency_key, stock_state, raw_payload)
        VALUES(@world_id, {sql_literal(npc_key)}, @item_id, {amount}, {price}, 'g2notr:gold', 'available', JSON_OBJECT('source','steps-11-14-smoke'))
        ON DUPLICATE KEY UPDATE amount=VALUES(amount), unit_price=VALUES(unit_price), currency_key=VALUES(currency_key), stock_state='available', raw_payload=VALUES(raw_payload);
    """)
    return item_uuid


def run_smoke(target: MySqlTarget, account_name: str, character_key: str) -> list[Check]:
    checks: list[Check] = []
    suffix = uuid.uuid4().hex
    session_key = f"smoke:steps-11-14:{character_key}:{suffix}"
    session_uuid = ""
    start_stats: tuple[int, int, int, int] | None = None
    try:
        session_uuid = call_login(target, account_name, character_key, session_key)
        checks.append(Check("login", True, session_uuid))

        # Store initial stats so destructive smoke operations can restore them even if the test fails midway.
        stat = row(target, f"""
            SELECT cs.health_current, cs.health_max, cs.mana_current, cs.mana_max, cs.experience, cs.learning_points
              FROM server_sessions ss JOIN character_stats cs ON cs.character_id=ss.character_id
             WHERE ss.session_id=UUID_TO_BIN({sql_literal(session_uuid)},1);
        """)
        start_hp, start_hp_max, start_mana, start_mana_max = [int(x) for x in stat[:4]]
        start_stats = (start_hp, start_hp_max, start_mana, start_mana_max)
        run_mysql(target, f"""
            UPDATE character_stats cs JOIN server_sessions ss ON ss.character_id=cs.character_id
               SET cs.health_max=GREATEST(cs.health_max, 20), cs.health_current=GREATEST(cs.health_current, 20), cs.mana_max=GREATEST(cs.mana_max, 20), cs.mana_current=GREATEST(cs.mana_current, 20)
             WHERE ss.session_id=UUID_TO_BIN({sql_literal(session_uuid)},1);
        """)

        npc_key = f"smoke:npc:trade-combat:{suffix}"
        seed_world_npc(target, session_uuid, npc_key)
        checks.append(Check("npc fixture", True, npc_key))

        # Give enough gold via the existing wallet write path, then buy and sell.
        grant_key = f"smoke:steps-11-14:{suffix}:gold-grant"
        grant = row(target, f"""
            SET @event_id=NULL; SET @amount_after=NULL;
            CALL mmo_grant_character_gold(UUID_TO_BIN({sql_literal(session_uuid)},1), 100, 'steps_11_14_trade_seed', 11001, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(grant_key)}, @event_id, @amount_after);
            SELECT BIN_TO_UUID(@event_id,1), @amount_after;
        """)
        checks.append(Check("gold seed", True, "/".join(grant)))

        trade_item_uuid = seed_npc_trade_item(target, session_uuid, npc_key, f"smoke:trade:item:{suffix}", 1, 5)
        buy_key = f"smoke:steps-11-14:{suffix}:buy"
        buy1 = row(target, f"""
            SET @event_id=NULL; SET @wallet_after=NULL; SET @bag_index=NULL;
            CALL mmo_trade_buy_from_npc(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(npc_key)}, UUID_TO_BIN({sql_literal(trade_item_uuid)},1), 5, 'g2notr:gold', NULL, 11002, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(buy_key)}, @event_id, @wallet_after, @bag_index);
            SELECT BIN_TO_UUID(@event_id,1), @wallet_after, @bag_index;
        """)
        buy2 = row(target, f"""
            SET @event_id=NULL; SET @wallet_after=NULL; SET @bag_index=NULL;
            CALL mmo_trade_buy_from_npc(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(npc_key)}, UUID_TO_BIN({sql_literal(trade_item_uuid)},1), 5, 'g2notr:gold', NULL, 11002, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(buy_key)}, @event_id, @wallet_after, @bag_index);
            SELECT BIN_TO_UUID(@event_id,1), @wallet_after, @bag_index;
        """)
        checks.append(Check("trade buy idempotent", buy1 == buy2, f"first={'/'.join(buy1)} retry={'/'.join(buy2)}"))
        checks.append(Check("trade buy audit count", count(target, f"SELECT COUNT(*) FROM trade_economy_audit WHERE idempotency_key={sql_literal(buy_key)};") == 1, "1"))

        sell_key = f"smoke:steps-11-14:{suffix}:sell"
        sell1 = row(target, f"""
            SET @event_id=NULL; SET @wallet_after=NULL;
            CALL mmo_trade_sell_to_npc(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(npc_key)}, UUID_TO_BIN({sql_literal(trade_item_uuid)},1), 3, 'g2notr:gold', 11003, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(sell_key)}, @event_id, @wallet_after);
            SELECT BIN_TO_UUID(@event_id,1), @wallet_after;
        """)
        sell2 = row(target, f"""
            SET @event_id=NULL; SET @wallet_after=NULL;
            CALL mmo_trade_sell_to_npc(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(npc_key)}, UUID_TO_BIN({sql_literal(trade_item_uuid)},1), 3, 'g2notr:gold', 11003, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(sell_key)}, @event_id, @wallet_after);
            SELECT BIN_TO_UUID(@event_id,1), @wallet_after;
        """)
        checks.append(Check("trade sell idempotent", sell1 == sell2, f"first={'/'.join(sell1)} retry={'/'.join(sell2)}"))

        mana_key = f"smoke:steps-11-14:{suffix}:mana"
        mana = row(target, f"""
            SET @event_id=NULL; SET @mana_after=NULL;
            CALL mmo_consume_character_mana(UUID_TO_BIN({sql_literal(session_uuid)},1), 3, 12001, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(mana_key)}, @event_id, @mana_after);
            SELECT BIN_TO_UUID(@event_id,1), @mana_after;
        """)
        checks.append(Check("mana consume", True, "/".join(mana)))

        dmg_key = f"smoke:steps-11-14:{suffix}:char-damage"
        damage = row(target, f"""
            SET @event_id=NULL; SET @health_after=NULL;
            CALL mmo_apply_character_damage(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(character_key)}, 1, 12002, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(dmg_key)}, @event_id, @health_after);
            SELECT BIN_TO_UUID(@event_id,1), @health_after;
        """)
        checks.append(Check("character damage", True, "/".join(damage)))

        entity_dmg_key = f"smoke:steps-11-14:{suffix}:entity-damage"
        entity_damage = row(target, f"""
            SET @event_id=NULL; SET @health_after=NULL; SET @row_after=NULL;
            CALL mmo_apply_world_entity_damage(UUID_TO_BIN({sql_literal(session_uuid)},1), {sql_literal(npc_key)}, 4, FALSE, 12003, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(entity_dmg_key)}, @event_id, @health_after, @row_after);
            SELECT BIN_TO_UUID(@event_id,1), @health_after, @row_after;
        """)
        checks.append(Check("world entity damage", True, "/".join(entity_damage)))

        consume_item_uuid = seed_character_inventory_item(target, session_uuid, f"smoke:consume:item:{suffix}", 2, None)
        consume_key = f"smoke:steps-11-14:{suffix}:consume-item"
        consume = row(target, f"""
            SET @event_id=NULL; SET @amount_after=NULL;
            CALL mmo_consume_character_item(UUID_TO_BIN({sql_literal(session_uuid)},1), UUID_TO_BIN({sql_literal(consume_item_uuid)},1), 1, 'smoke_consume', 12004, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(consume_key)}, @event_id, @amount_after);
            SELECT BIN_TO_UUID(@event_id,1), @amount_after;
        """)
        checks.append(Check("item consume", True, "/".join(consume)))

        stack_item_uuid = seed_character_inventory_item(target, session_uuid, f"smoke:stack:item:{suffix}", 5, None)
        split_key = f"smoke:steps-11-14:{suffix}:split"
        new_stack_key = f"smoke:stack:item:{suffix}:split"
        split1 = row(target, f"""
            SET @event_id=NULL; SET @new_item_id=NULL; SET @source_after=NULL;
            CALL mmo_split_character_item_stack(UUID_TO_BIN({sql_literal(session_uuid)},1), UUID_TO_BIN({sql_literal(stack_item_uuid)},1), 2, {sql_literal(new_stack_key)}, NULL, 13001, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(split_key)}, @event_id, @new_item_id, @source_after);
            SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@new_item_id,1), @source_after;
        """)
        split2 = row(target, f"""
            SET @event_id=NULL; SET @new_item_id=NULL; SET @source_after=NULL;
            CALL mmo_split_character_item_stack(UUID_TO_BIN({sql_literal(session_uuid)},1), UUID_TO_BIN({sql_literal(stack_item_uuid)},1), 2, {sql_literal(new_stack_key)}, NULL, 13001, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(split_key)}, @event_id, @new_item_id, @source_after);
            SELECT BIN_TO_UUID(@event_id,1), BIN_TO_UUID(@new_item_id,1), @source_after;
        """)
        checks.append(Check("stack split idempotent", split1 == split2, f"first={'/'.join(split1)} retry={'/'.join(split2)}"))
        new_stack_uuid = split1[1]
        merge_key = f"smoke:steps-11-14:{suffix}:merge"
        merge1 = row(target, f"""
            SET @event_id=NULL; SET @target_after=NULL;
            CALL mmo_merge_character_item_stack(UUID_TO_BIN({sql_literal(session_uuid)},1), UUID_TO_BIN({sql_literal(new_stack_uuid)},1), UUID_TO_BIN({sql_literal(stack_item_uuid)},1), 13002, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(merge_key)}, @event_id, @target_after);
            SELECT BIN_TO_UUID(@event_id,1), @target_after;
        """)
        merge2 = row(target, f"""
            SET @event_id=NULL; SET @target_after=NULL;
            CALL mmo_merge_character_item_stack(UUID_TO_BIN({sql_literal(session_uuid)},1), UUID_TO_BIN({sql_literal(new_stack_uuid)},1), UUID_TO_BIN({sql_literal(stack_item_uuid)},1), 13002, JSON_OBJECT('source','steps-11-14-smoke'), {sql_literal(merge_key)}, @event_id, @target_after);
            SELECT BIN_TO_UUID(@event_id,1), @target_after;
        """)
        checks.append(Check("stack merge idempotent", merge1 == merge2, f"first={'/'.join(merge1)} retry={'/'.join(merge2)}"))

        validation_key = f"smoke:steps-11-14:{suffix}:extended-validation"
        val = row(target, f"""
            SET @run_id=NULL; SET @errors=NULL; SET @warnings=NULL;
            SELECT world_instance_id INTO @world_id FROM server_sessions WHERE session_id=UUID_TO_BIN({sql_literal(session_uuid)},1);
            CALL mmo_validate_world_projection_extended(@world_id, {sql_literal(validation_key)}, JSON_OBJECT('source','steps-11-14-smoke'), @run_id, @errors, @warnings);
            SELECT BIN_TO_UUID(@run_id,1), @errors, @warnings;
        """)
        checks.append(Check("extended projection validation", True, "/".join(val)))
        latest = run_mysql(target, "SELECT check_name, severity, problem_count FROM v_projection_validation_latest_errors LIMIT 8;")
        if latest:
            checks.append(Check("latest projection diagnostics", True, latest.replace("\n", " | ")))

        # Restore PC_HERO HP/mana back to their values from before the destructive smoke test.
        run_mysql(target, f"""
            UPDATE character_stats cs JOIN server_sessions ss ON ss.character_id=cs.character_id
               SET cs.health_current={start_hp}, cs.health_max={start_hp_max}, cs.mana_current={start_mana}, cs.mana_max={start_mana_max}
             WHERE ss.session_id=UUID_TO_BIN({sql_literal(session_uuid)},1);
        """)

        logout_event = call_logout(target, session_uuid)
        checks.append(Check("logout", True, logout_event))
        active = count(target, f"SELECT COUNT(*) FROM server_sessions WHERE session_key={sql_literal(session_key)} AND lifecycle_state='active';")
        checks.append(Check("session closed", active == 0, f"active_sessions={active}"))
    except Exception as exc:
        if session_uuid:
            if start_stats is not None:
                try:
                    hp, hp_max, mana, mana_max = start_stats
                    run_mysql(target, f"""
                        UPDATE character_stats cs JOIN server_sessions ss ON ss.character_id=cs.character_id
                           SET cs.health_current={hp}, cs.health_max={hp_max}, cs.mana_current={mana}, cs.mana_max={mana_max}
                         WHERE ss.session_id=UUID_TO_BIN({sql_literal(session_uuid)},1);
                    """)
                except Exception:
                    pass
            try:
                call_logout(target, session_uuid, f"smoke failed: {exc}")
            except Exception:
                pass
        raise
    return checks


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=os.environ.get("MYSQL_URL"), help="mysql://user:password@host:port/database")
    parser.add_argument("--account-name", default="local-import")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--run-smoke", action="store_true")
    args = parser.parse_args(argv)
    if not args.url:
        print("error: --url or MYSQL_URL is required", file=sys.stderr)
        return 2
    target = parse_url(args.url)
    checks = validate_objects(target)
    if args.run_smoke:
        checks.extend(run_smoke(target, args.account_name, args.character_key))
    ok = True
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        ok = ok and check.ok
    if not ok:
        print("\nHint: apply migrations 011..014 after 010 and keep previous steps/import intact.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
