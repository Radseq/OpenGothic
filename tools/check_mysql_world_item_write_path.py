#!/usr/bin/env python3
"""Validate and optionally smoke-test the MySQL loose world item write path.

This script uses the mysql command-line client only. It does not require a
Python MySQL driver. It assumes migrations 001..005 are applied and that the
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
         WHERE migration_key='production/mysql/005_world_item_write_path';
        """,
    )
    ok = value == "gothic-mmo-world-item-write-path-v1-mysql"
    return Check("migration 005 marker", ok, value or "missing")


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
            "world item tables",
            f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';",
            ("world_item_audit",),
        ),
        check_named(
            target,
            "world item views",
            f"SELECT table_name FROM information_schema.views WHERE table_schema='{db}';",
            ("v_pickable_world_items", "v_world_item_audit"),
        ),
        check_named(
            target,
            "world item routines",
            f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';",
            ("mmo_pickup_world_item", "mmo_remove_world_item"),
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
          'world-item-smoke',
          'local',
          JSON_OBJECT('tool', 'check_mysql_world_item_write_path'),
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
          'world_item_smoke_done',
          JSON_OBJECT('tool', 'check_mysql_world_item_write_path'),
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id, 1);
        """,
    )


def seed_world_item(target: MySqlTarget, character_key: str, suffix: str, kind: str) -> tuple[str, str]:
    entity_key = f"smoke:{kind}:world-item:{suffix}"
    item_key = f"import:smoke:world-item:{entity_key}"
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
        INSERT INTO world_entity_state(
          world_instance_id, entity_key, entity_kind, lifecycle_state,
          pos_x, pos_y, pos_z, state_json, row_version
        ) VALUES (
          @world_instance_id, {sql_literal(entity_key)}, 'item', 'active',
          0, 0, 0,
          JSON_OBJECT('tool', 'check_mysql_world_item_write_path', 'smoke_kind', {sql_literal(kind)}),
          1
        );
        INSERT INTO item_instances(
          realm_id, item_template_id, item_instance_key, owner_type, owner_id,
          quantity, lifecycle_state, raw_payload
        ) VALUES (
          @realm_id, @item_template_id, {sql_literal(item_key)}, 'world_entity', NULL,
          1, 'active',
          JSON_OBJECT(
            'tool', 'check_mysql_world_item_write_path',
            'item_spawn_key', {sql_literal(entity_key)},
            'entity_key', {sql_literal(entity_key)},
            'smoke_kind', {sql_literal(kind)}
          )
        );
        SELECT {sql_literal(entity_key)}, {sql_literal(item_key)};
        """,
    )
    parts = raw.split("\t") if raw else [entity_key, item_key]
    return parts[0], parts[1] if len(parts) > 1 else item_key


def call_pickup(target: MySqlTarget, session_id: str, entity_key: str, idempotency_key: str, tick: int) -> tuple[str, str, int]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @item_instance_id = NULL;
        SET @amount_picked = NULL;
        CALL mmo_pickup_world_item(
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          {sql_literal(entity_key)},
          NULL,
          NULL,
          {tick},
          JSON_OBJECT('tool', 'check_mysql_world_item_write_path', 'smoke', TRUE),
          {sql_literal(idempotency_key)},
          @event_id,
          @item_instance_id,
          @amount_picked
        );
        SELECT BIN_TO_UUID(@event_id, 1), BIN_TO_UUID(@item_instance_id, 1), @amount_picked;
        """,
    )
    parts = raw.split("\t") if raw else ["", "", "0"]
    return parts[0], parts[1] if len(parts) > 1 else "", int(parts[2] if len(parts) > 2 and parts[2] else 0)


def call_remove(target: MySqlTarget, session_id: str, entity_key: str, idempotency_key: str, tick: int) -> tuple[str, str]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @item_instance_id = NULL;
        CALL mmo_remove_world_item(
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          {sql_literal(entity_key)},
          'world_item_smoke_remove',
          {tick},
          JSON_OBJECT('tool', 'check_mysql_world_item_write_path', 'smoke', TRUE),
          {sql_literal(idempotency_key)},
          @event_id,
          @item_instance_id
        );
        SELECT BIN_TO_UUID(@event_id, 1), BIN_TO_UUID(@item_instance_id, 1);
        """,
    )
    parts = raw.split("\t") if raw else ["", ""]
    return parts[0], parts[1] if len(parts) > 1 else ""


def item_owner(target: MySqlTarget, item_instance_id: str) -> str:
    return scalar(
        target,
        f"""
        SELECT CONCAT(owner_type, '/', lifecycle_state, '/', quantity)
          FROM item_instances
         WHERE item_instance_id = UUID_TO_BIN({sql_literal(item_instance_id)}, 1)
         LIMIT 1;
        """,
    )


def world_lifecycle(target: MySqlTarget, entity_key: str) -> str:
    return scalar(
        target,
        f"""
        SELECT lifecycle_state
          FROM world_entity_state
         WHERE entity_key = {sql_literal(entity_key)}
         LIMIT 1;
        """,
    )


def character_inventory_count(target: MySqlTarget, character_key: str, item_instance_id: str) -> int:
    return count(
        target,
        f"""
        SELECT COUNT(*)
          FROM character_inventory ci
          JOIN characters c ON c.character_id = ci.character_id
         WHERE c.character_key = {sql_literal(character_key)}
           AND ci.item_instance_id = UUID_TO_BIN({sql_literal(item_instance_id)}, 1);
        """,
    )


def run_smoke(target: MySqlTarget, account_name: str, character_key: str) -> list[Check]:
    checks: list[Check] = []
    character_count = count(target, f"SELECT COUNT(*) FROM characters WHERE character_key={sql_literal(character_key)};")
    account_count = count(target, f"SELECT COUNT(*) FROM account_accounts WHERE account_name={sql_literal(account_name)};")
    template_count = count(target, "SELECT COUNT(*) FROM content_item_templates;")
    if account_count == 0 or character_count == 0 or template_count == 0:
        checks.append(Check(
            "bootstrap data",
            False,
            "missing account/character/item templates; run import_runtime_sqlite_to_mysql.py before world-item smoke",
        ))
        return checks

    suffix = uuid.uuid4().hex
    session_key = f"world-item-smoke:{character_key}:{suffix}"
    pickup_key = f"world-item:{session_key}:pickup"
    remove_key = f"world-item:{session_key}:remove"

    session_id = ""
    try:
        session_id = call_login(target, account_name, character_key, session_key)
        checks.append(Check("login", bool(session_id), session_id or "no session id"))

        pickup_entity, _ = seed_world_item(target, character_key, suffix, "pickup")
        checks.append(Check("pickup fixture", bool(pickup_entity), pickup_entity or "missing entity key"))

        pickup_event_1, pickup_item_1, pickup_amount_1 = call_pickup(target, session_id, pickup_entity, pickup_key, 3001)
        pickup_event_2, pickup_item_2, pickup_amount_2 = call_pickup(target, session_id, pickup_entity, pickup_key, 3001)
        checks.append(Check("pickup append", bool(pickup_event_1 and pickup_item_1), f"{pickup_event_1}/{pickup_item_1}/{pickup_amount_1}"))
        checks.append(Check(
            "pickup idempotent retry",
            pickup_event_1 == pickup_event_2 and pickup_item_1 == pickup_item_2 and pickup_amount_1 == pickup_amount_2 and bool(pickup_event_2),
            f"first={pickup_event_1}/{pickup_item_1}/{pickup_amount_1} retry={pickup_event_2}/{pickup_item_2}/{pickup_amount_2}",
        ))
        checks.append(Check(
            "pickup duplicate event count",
            count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(pickup_key)};") == 1,
            str(count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(pickup_key)};")),
        ))
        checks.append(Check(
            "pickup audit count",
            count(target, f"SELECT COUNT(*) FROM world_item_audit WHERE idempotency_key={sql_literal(pickup_key)};") == 1,
            str(count(target, f"SELECT COUNT(*) FROM world_item_audit WHERE idempotency_key={sql_literal(pickup_key)};")),
        ))
        checks.append(Check("pickup world removed", world_lifecycle(target, pickup_entity) == "removed", world_lifecycle(target, pickup_entity)))
        checks.append(Check("pickup item owned by character", item_owner(target, pickup_item_1).startswith("character/active/1"), item_owner(target, pickup_item_1)))
        inv_rows = character_inventory_count(target, character_key, pickup_item_1)
        checks.append(Check("pickup inventory row", inv_rows == 1, str(inv_rows)))

        remove_entity, _ = seed_world_item(target, character_key, suffix, "remove")
        checks.append(Check("remove fixture", bool(remove_entity), remove_entity or "missing entity key"))

        remove_event_1, remove_item_1 = call_remove(target, session_id, remove_entity, remove_key, 3002)
        remove_event_2, remove_item_2 = call_remove(target, session_id, remove_entity, remove_key, 3002)
        checks.append(Check("remove append", bool(remove_event_1 and remove_item_1), f"{remove_event_1}/{remove_item_1}"))
        checks.append(Check(
            "remove idempotent retry",
            remove_event_1 == remove_event_2 and remove_item_1 == remove_item_2 and bool(remove_event_2),
            f"first={remove_event_1}/{remove_item_1} retry={remove_event_2}/{remove_item_2}",
        ))
        checks.append(Check(
            "remove duplicate event count",
            count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(remove_key)};") == 1,
            str(count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(remove_key)};")),
        ))
        checks.append(Check(
            "remove audit count",
            count(target, f"SELECT COUNT(*) FROM world_item_audit WHERE idempotency_key={sql_literal(remove_key)};") == 1,
            str(count(target, f"SELECT COUNT(*) FROM world_item_audit WHERE idempotency_key={sql_literal(remove_key)};")),
        ))
        checks.append(Check("remove world removed", world_lifecycle(target, remove_entity) == "removed", world_lifecycle(target, remove_entity)))
        checks.append(Check("remove item archived", item_owner(target, remove_item_1).startswith("system/archived/1"), item_owner(target, remove_item_1)))

        logout_event = call_logout(target, session_id)
        checks.append(Check("logout", bool(logout_event), logout_event or "no event id"))

        active_sessions = count(
            target,
            f"SELECT COUNT(*) FROM v_active_server_sessions WHERE session_key={sql_literal(session_key)};",
        )
        checks.append(Check("session closed", active_sessions == 0, f"active_sessions={active_sessions}"))
    except Exception as exc:  # noqa: BLE001 - smoke diagnostic tool
        checks.append(Check("smoke execution", False, str(exc)))
        if session_id:
            try:
                cleanup_event = call_logout(target, session_id)
                checks.append(Check("cleanup logout", bool(cleanup_event), cleanup_event or "no event id"))
            except Exception as cleanup_exc:  # noqa: BLE001 - best-effort smoke cleanup
                checks.append(Check("cleanup logout", False, str(cleanup_exc)))

    return checks


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate MySQL Gothic MMO loose world item write path.")
    parser.add_argument("--url", default=os.environ.get("MYSQL_URL", ""), help="mysql://user:password@host:port/database. Defaults to MYSQL_URL.")
    parser.add_argument("--account-name", default="local-import")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--run-smoke", action="store_true", help="Run login/seed/pickup/retry/remove/retry/logout smoke test.")
    args = parser.parse_args()

    if not args.url:
        print("error: provide --url or MYSQL_URL", file=sys.stderr)
        return 2

    try:
        target = parse_url(args.url)
        checks = validate_objects(target)
        if args.run_smoke:
            checks.extend(run_smoke(target, args.account_name, args.character_key))
    except Exception as exc:  # noqa: BLE001 - command-line diagnostic
        print(f"world item write path check failed: {exc}", file=sys.stderr)
        return 1

    ok = True
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        ok = ok and check.ok

    if not ok:
        print("\nHint: apply 005_world_item_write_path.sql after 004_wallet_write_path.sql and import runtime/g2notr.sqlite first.")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
