#!/usr/bin/env python3
"""Validate and optionally smoke-test the MySQL character inventory/equipment write path.

This script uses the mysql command-line client only. It does not require a
Python MySQL driver. It assumes migrations 001..006 are applied and that the
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
        "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
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


def line_set(raw: str) -> set[str]:
    return {line.strip() for line in raw.splitlines() if line.strip()}


def scalar(target: MySqlTarget, sql: str) -> str:
    raw = run_mysql(target, sql)
    return raw.splitlines()[-1].strip() if raw else ""


def count(target: MySqlTarget, sql: str) -> int:
    raw = scalar(target, sql)
    try:
        return int(raw)
    except ValueError:
        return 0


def check_marker(target: MySqlTarget) -> Check:
    value = scalar(
        target,
        """
        SELECT schema_contract
          FROM mmo_schema_versions
         WHERE migration_key='production/mysql/006_character_inventory_equipment_write_path';
        """,
    )
    ok = value == "gothic-mmo-character-inventory-equipment-write-path-v1-mysql"
    return Check("migration 006 marker", ok, value or "missing")


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
        check_marker(target),
        check_named(
            target,
            "inventory/equipment tables",
            f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';",
            ("character_inventory_audit",),
        ),
        check_named(
            target,
            "inventory/equipment views",
            f"SELECT table_name FROM information_schema.views WHERE table_schema='{db}';",
            ("v_character_equipment_state", "v_character_inventory_audit"),
        ),
        check_named(
            target,
            "inventory/equipment routines",
            f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';",
            ("mmo_transfer_character_item", "mmo_equip_character_item", "mmo_unequip_character_item"),
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
          'inventory-equipment-smoke',
          'local',
          JSON_OBJECT('tool', 'check_mysql_character_inventory_equipment_write_path'),
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
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          'inventory_equipment_smoke_done',
          JSON_OBJECT('tool', 'check_mysql_character_inventory_equipment_write_path'),
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id, 1);
        """,
    )


def seed_inventory_item(target: MySqlTarget, character_key: str, suffix: str) -> tuple[str, str]:
    item_key = f"import:smoke:character-inventory:{character_key}:{suffix}"
    raw = run_mysql(
        target,
        f"""
        SET @character_id = (
          SELECT character_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1
        );
        SET @world_instance_id = (
          SELECT current_world_instance_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1
        );
        SET @realm_id = (
          SELECT realm_id FROM characters WHERE character_key={sql_literal(character_key)} LIMIT 1
        );
        SET @content_revision_id = (
          SELECT active_content_revision_id FROM realm_realms WHERE realm_id=@realm_id LIMIT 1
        );
        SET @item_template_id = (
          SELECT item_template_id
            FROM content_item_templates
           WHERE content_revision_id = @content_revision_id
           ORDER BY created_at DESC, item_template_key DESC
           LIMIT 1
        );
        SET @item_template_id = COALESCE(
          @item_template_id,
          (SELECT item_template_id FROM content_item_templates ORDER BY created_at DESC, item_template_key DESC LIMIT 1)
        );
        INSERT INTO item_instances(
          realm_id, item_template_id, item_instance_key, owner_type, owner_id,
          quantity, lifecycle_state, raw_payload
        ) VALUES (
          @realm_id, @item_template_id, {sql_literal(item_key)}, 'character', @character_id,
          1, 'active',
          JSON_OBJECT('tool', 'check_mysql_character_inventory_equipment_write_path', 'smoke', TRUE)
        );
        INSERT INTO character_inventory(character_id, item_instance_id, bag_index, amount, source_amount, source_iterator_count)
        VALUES(@character_id, (SELECT item_instance_id FROM item_instances WHERE item_instance_key={sql_literal(item_key)}), NULL, 1, 1, 1);
        SELECT BIN_TO_UUID((SELECT item_instance_id FROM item_instances WHERE item_instance_key={sql_literal(item_key)}), 1), {sql_literal(item_key)};
        """,
    )
    parts = raw.split("\t") if raw else ["", item_key]
    return parts[0], parts[1] if len(parts) > 1 else item_key


def seed_target_character(target: MySqlTarget, source_character_key: str, suffix: str) -> str:
    target_key = f"SMOKE_TARGET_{suffix}"
    account_name = f"smoke-target-{suffix}"
    character_name = f"Smoke Target {suffix[:8]}"
    run_mysql(
        target,
        f"""
        SET @source_character_id = (
          SELECT character_id FROM characters WHERE character_key={sql_literal(source_character_key)} LIMIT 1
        );
        SET @realm_id = (
          SELECT realm_id FROM characters WHERE character_key={sql_literal(source_character_key)} LIMIT 1
        );
        SET @world_instance_id = (
          SELECT current_world_instance_id FROM characters WHERE character_key={sql_literal(source_character_key)} LIMIT 1
        );
        INSERT INTO account_accounts(account_name, status, flags)
        VALUES({sql_literal(account_name)}, 'active', JSON_OBJECT('tool', 'check_mysql_character_inventory_equipment_write_path'));
        SET @account_id = (SELECT account_id FROM account_accounts WHERE account_name={sql_literal(account_name)} LIMIT 1);
        INSERT INTO characters(
          account_id, realm_id, current_world_instance_id, character_key, character_name, lifecycle_state, metadata
        ) VALUES(
          @account_id, @realm_id, @world_instance_id, {sql_literal(target_key)}, {sql_literal(character_name)}, 'active',
          JSON_OBJECT('tool', 'check_mysql_character_inventory_equipment_write_path', 'smoke', TRUE)
        );
        """,
    )
    return target_key


def call_equip(target: MySqlTarget, session_id: str, item_instance_id: str, idempotency_key: str, tick: int) -> str:
    return scalar(
        target,
        f"""
        SET @event_id = NULL;
        CALL mmo_equip_character_item(
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          UUID_TO_BIN({sql_literal(item_instance_id)}, 1),
          'torch',
          {tick},
          JSON_OBJECT('tool', 'check_mysql_character_inventory_equipment_write_path', 'smoke', TRUE),
          {sql_literal(idempotency_key)},
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id, 1);
        """,
    )


def call_unequip(target: MySqlTarget, session_id: str, idempotency_key: str, tick: int) -> tuple[str, str]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @item_instance_id = NULL;
        CALL mmo_unequip_character_item(
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          'torch',
          {tick},
          JSON_OBJECT('tool', 'check_mysql_character_inventory_equipment_write_path', 'smoke', TRUE),
          {sql_literal(idempotency_key)},
          @event_id,
          @item_instance_id
        );
        SELECT BIN_TO_UUID(@event_id, 1), BIN_TO_UUID(@item_instance_id, 1);
        """,
    )
    parts = raw.split("\t") if raw else ["", ""]
    return parts[0], parts[1] if len(parts) > 1 else ""


def call_transfer(target: MySqlTarget, session_id: str, item_instance_id: str, target_character_key: str, idempotency_key: str, tick: int) -> tuple[str, str, int]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @target_character_id = NULL;
        SET @amount_transferred = NULL;
        CALL mmo_transfer_character_item(
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          UUID_TO_BIN({sql_literal(item_instance_id)}, 1),
          {sql_literal(target_character_key)},
          NULL,
          {tick},
          JSON_OBJECT('tool', 'check_mysql_character_inventory_equipment_write_path', 'smoke', TRUE),
          {sql_literal(idempotency_key)},
          @event_id,
          @target_character_id,
          @amount_transferred
        );
        SELECT BIN_TO_UUID(@event_id, 1), BIN_TO_UUID(@target_character_id, 1), CAST(@amount_transferred AS SIGNED);
        """,
    )
    parts = raw.split("\t") if raw else ["", "", "0"]
    return parts[0], parts[1] if len(parts) > 1 else "", int(parts[2] if len(parts) > 2 and parts[2] else 0)


def idempotent_event_count(target: MySqlTarget, key: str) -> int:
    return count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(key)};")


def audit_count(target: MySqlTarget, key: str) -> int:
    return count(target, f"SELECT COUNT(*) FROM character_inventory_audit WHERE idempotency_key={sql_literal(key)};")


def run_smoke(target: MySqlTarget, account_name: str, character_key: str) -> list[Check]:
    checks: list[Check] = []
    character_count = count(target, f"SELECT COUNT(*) FROM characters WHERE character_key={sql_literal(character_key)};")
    if character_count < 1:
        return [Check("smoke prerequisite character", False, f"missing character_key={character_key}")]

    suffix = uuid.uuid4().hex
    session_id = ""
    try:
        session_id = call_login(target, account_name, character_key, f"inventory-equipment-smoke-{suffix}")
        checks.append(Check("login", bool(session_id), session_id or "missing"))

        item_id, item_key = seed_inventory_item(target, character_key, suffix)
        checks.append(Check("inventory fixture", bool(item_id), f"{item_key}/{item_id}"))

        equip_key = f"smoke:inventory-equipment:equip:{suffix}"
        equip_event = call_equip(target, session_id, item_id, equip_key, 6100)
        equip_retry = call_equip(target, session_id, item_id, equip_key, 6101)
        checks.append(Check("equip append", bool(equip_event), equip_event or "missing"))
        checks.append(Check("equip idempotent retry", equip_event == equip_retry, f"first={equip_event} retry={equip_retry}"))
        checks.append(Check("equip duplicate event count", idempotent_event_count(target, equip_key) == 1, str(idempotent_event_count(target, equip_key))))
        checks.append(Check("equip audit count", audit_count(target, equip_key) == 1, str(audit_count(target, equip_key))))
        equipped = count(target, f"SELECT COUNT(*) FROM character_equipment WHERE equipment_slot='torch' AND item_instance_id=UUID_TO_BIN({sql_literal(item_id)}, 1);")
        checks.append(Check("equipped row", equipped == 1, str(equipped)))

        unequip_key = f"smoke:inventory-equipment:unequip:{suffix}"
        unequip_event, unequip_item = call_unequip(target, session_id, unequip_key, 6110)
        unequip_retry_event, unequip_retry_item = call_unequip(target, session_id, unequip_key, 6111)
        checks.append(Check("unequip append", bool(unequip_event), f"{unequip_event}/{unequip_item}"))
        checks.append(Check("unequip idempotent retry", unequip_event == unequip_retry_event and unequip_item == unequip_retry_item, f"first={unequip_event}/{unequip_item} retry={unequip_retry_event}/{unequip_retry_item}"))
        checks.append(Check("unequip duplicate event count", idempotent_event_count(target, unequip_key) == 1, str(idempotent_event_count(target, unequip_key))))
        checks.append(Check("unequip audit count", audit_count(target, unequip_key) == 1, str(audit_count(target, unequip_key))))
        still_equipped = count(target, f"SELECT COUNT(*) FROM character_equipment WHERE equipment_slot='torch' AND item_instance_id=UUID_TO_BIN({sql_literal(item_id)}, 1);")
        checks.append(Check("unequipped row removed", still_equipped == 0, str(still_equipped)))

        target_key = seed_target_character(target, character_key, suffix)
        checks.append(Check("target character fixture", bool(target_key), target_key))
        transfer_key = f"smoke:inventory-equipment:transfer:{suffix}"
        transfer_event, target_id, amount = call_transfer(target, session_id, item_id, target_key, transfer_key, 6120)
        retry_event, retry_target_id, retry_amount = call_transfer(target, session_id, item_id, target_key, transfer_key, 6121)
        checks.append(Check("transfer append", bool(transfer_event), f"{transfer_event}/{target_id}/{amount}"))
        checks.append(Check("transfer idempotent retry", transfer_event == retry_event and target_id == retry_target_id and amount == retry_amount, f"first={transfer_event}/{target_id}/{amount} retry={retry_event}/{retry_target_id}/{retry_amount}"))
        checks.append(Check("transfer duplicate event count", idempotent_event_count(target, transfer_key) == 1, str(idempotent_event_count(target, transfer_key))))
        checks.append(Check("transfer audit count", audit_count(target, transfer_key) == 1, str(audit_count(target, transfer_key))))
        owner = scalar(target, f"SELECT CONCAT(owner_type, '/', lifecycle_state, '/', quantity) FROM item_instances WHERE item_instance_id=UUID_TO_BIN({sql_literal(item_id)}, 1);")
        checks.append(Check("transfer item still active", owner == "character/active/1", owner))
        source_rows = count(target, f"SELECT COUNT(*) FROM character_inventory ci JOIN characters c ON c.character_id=ci.character_id WHERE c.character_key={sql_literal(character_key)} AND ci.item_instance_id=UUID_TO_BIN({sql_literal(item_id)}, 1);")
        target_rows = count(target, f"SELECT COUNT(*) FROM character_inventory ci JOIN characters c ON c.character_id=ci.character_id WHERE c.character_key={sql_literal(target_key)} AND ci.item_instance_id=UUID_TO_BIN({sql_literal(item_id)}, 1);")
        checks.append(Check("transfer source inventory removed", source_rows == 0, str(source_rows)))
        checks.append(Check("transfer target inventory row", target_rows == 1, str(target_rows)))

        logout_event = call_logout(target, session_id)
        checks.append(Check("logout", bool(logout_event), logout_event or "missing"))
        active_sessions = count(target, f"SELECT COUNT(*) FROM server_sessions WHERE session_id=UUID_TO_BIN({sql_literal(session_id)}, 1) AND lifecycle_state='active';")
        checks.append(Check("session closed", active_sessions == 0, f"active_sessions={active_sessions}"))
    except Exception as exc:  # noqa: BLE001 - command-line smoke diagnostic
        if session_id:
            try:
                cleanup_event = call_logout(target, session_id)
                checks.append(Check("cleanup logout after failure", bool(cleanup_event), cleanup_event or "missing"))
            except Exception as cleanup_exc:  # noqa: BLE001
                checks.append(Check("cleanup logout after failure", False, str(cleanup_exc)))
        checks.append(Check("smoke execution", False, str(exc)))
    return checks


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Gothic MMO MySQL character inventory/equipment write path.")
    parser.add_argument("--url", default=os.environ.get("MYSQL_URL", ""), help="mysql://user:password@host:port/database. Defaults to MYSQL_URL.")
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
        if args.run_smoke:
            checks.extend(run_smoke(target, args.account_name, args.character_key))
    except Exception as exc:  # noqa: BLE001
        print(f"inventory/equipment write path check failed: {exc}", file=sys.stderr)
        return 1

    ok = True
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        ok = ok and check.ok

    if not ok:
        print("\nHint: apply 006_character_inventory_equipment_write_path.sql after 005_world_item_write_path.sql and import runtime/g2notr.sqlite first.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
