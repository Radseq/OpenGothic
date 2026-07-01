#!/usr/bin/env python3
"""Validate and optionally smoke-test the MySQL container/interactive write path.

This script uses the mysql command-line client only. It does not require a
Python MySQL driver. It assumes migrations 001..007 are applied and that the
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


def uuid_bin(value: str) -> str:
    return f"UUID_TO_BIN({sql_literal(value)}, 1)"


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
         WHERE migration_key='production/mysql/007_container_interactive_write_path';
        """,
    )
    ok = value == "gothic-mmo-container-interactive-write-path-v1-mysql"
    return Check("migration 007 marker", ok, value or "missing")


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
            "container/interactive tables",
            f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';",
            ("world_interactive_audit",),
        ),
        check_named(
            target,
            "container/interactive views",
            f"SELECT table_name FROM information_schema.views WHERE table_schema='{db}';",
            ("v_world_container_inventory_state", "v_world_interactive_state", "v_world_interactive_audit"),
        ),
        check_named(
            target,
            "container/interactive routines",
            f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';",
            ("mmo_take_container_item", "mmo_put_container_item", "mmo_update_interactive_state"),
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
          'container-interactive-smoke',
          'local',
          JSON_OBJECT('tool', 'check_mysql_container_interactive_write_path'),
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
          'container_interactive_smoke_done',
          JSON_OBJECT('tool', 'check_mysql_container_interactive_write_path'),
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id, 1);
        """,
    )


def seed_container_item(target: MySqlTarget, character_key: str, suffix: str) -> tuple[str, str]:
    entity_key = f"smoke:interactive:container:{suffix}"
    item_instance_id = str(uuid.uuid4())
    item_key = f"import:smoke:container-item:{entity_key}"
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
          SELECT active_content_revision_id
            FROM realm_realms
           WHERE realm_id = @realm_id
           LIMIT 1
        );
        SET @entity_template_id = (
          SELECT entity_template_id
            FROM content_entity_templates
           WHERE content_revision_id = @content_revision_id
             AND entity_kind = 'interactive'
           ORDER BY created_at DESC, engine_template_key DESC
           LIMIT 1
        );
        SET @item_template_id = (
          SELECT item_template_id
            FROM content_item_templates
           WHERE content_revision_id = @content_revision_id
           ORDER BY created_at DESC, item_template_key DESC
           LIMIT 1
        );
        SET @item_template_id = COALESCE(@item_template_id, (
          SELECT item_template_id FROM content_item_templates ORDER BY created_at DESC, item_template_key DESC LIMIT 1
        ));
        INSERT INTO world_entity_state(
          world_instance_id, entity_key, entity_kind, entity_template_id, lifecycle_state,
          pos_x, pos_y, pos_z, state_json, row_version
        ) VALUES (
          @world_instance_id,
          {sql_literal(entity_key)},
          'interactive',
          @entity_template_id,
          'active',
          0, 0, 0,
          JSON_OBJECT('tool', 'check_mysql_container_interactive_write_path', 'state_id', 1, 'state_count', 1, 'state_mask', 0, 'locked', 1, 'cracked', 0),
          1
        )
        ON DUPLICATE KEY UPDATE
          lifecycle_state='active',
          state_json=VALUES(state_json),
          row_version=row_version+1;
        INSERT INTO item_instances(
          item_instance_id, realm_id, item_template_id, item_instance_key, owner_type, owner_id,
          quantity, lifecycle_state, raw_payload
        ) VALUES (
          {uuid_bin(item_instance_id)},
          @realm_id,
          @item_template_id,
          {sql_literal(item_key)},
          'container',
          NULL,
          1,
          'active',
          JSON_OBJECT('tool', 'check_mysql_container_interactive_write_path', 'fixture', TRUE)
        )
        ON DUPLICATE KEY UPDATE
          owner_type='container',
          owner_id=NULL,
          quantity=1,
          lifecycle_state='active',
          raw_payload=VALUES(raw_payload);
        INSERT INTO world_inventory(world_instance_id, owner_entity_key, item_instance_id, amount, source_amount, source_iterator_count)
        VALUES(@world_instance_id, {sql_literal(entity_key)}, {uuid_bin(item_instance_id)}, 1, NULL, NULL)
        ON DUPLICATE KEY UPDATE amount=VALUES(amount);
        SELECT {sql_literal(entity_key)}, {sql_literal(item_instance_id)};
        """,
    )
    parts = raw.split("\t") if raw else [entity_key, item_instance_id]
    return parts[0], parts[1]


def call_take(target: MySqlTarget, session_id: str, entity_key: str, item_instance_id: str, idem: str, tick: int, bag_index: int) -> tuple[str, int]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @amount_taken = NULL;
        CALL mmo_take_container_item(
          {uuid_bin(session_id)},
          {sql_literal(entity_key)},
          {uuid_bin(item_instance_id)},
          {bag_index},
          {tick},
          JSON_OBJECT('tool', 'check_mysql_container_interactive_write_path', 'smoke', TRUE),
          {sql_literal(idem)},
          @event_id,
          @amount_taken
        );
        SELECT BIN_TO_UUID(@event_id, 1), CAST(@amount_taken AS SIGNED);
        """,
    )
    parts = raw.split("\t") if raw else ["", "0"]
    return parts[0], int(parts[1] if len(parts) > 1 and parts[1] else 0)


def call_put(target: MySqlTarget, session_id: str, entity_key: str, item_instance_id: str, idem: str, tick: int) -> tuple[str, int]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @amount_put = NULL;
        CALL mmo_put_container_item(
          {uuid_bin(session_id)},
          {sql_literal(entity_key)},
          {uuid_bin(item_instance_id)},
          {tick},
          JSON_OBJECT('tool', 'check_mysql_container_interactive_write_path', 'smoke', TRUE),
          {sql_literal(idem)},
          @event_id,
          @amount_put
        );
        SELECT BIN_TO_UUID(@event_id, 1), CAST(@amount_put AS SIGNED);
        """,
    )
    parts = raw.split("\t") if raw else ["", "0"]
    return parts[0], int(parts[1] if len(parts) > 1 and parts[1] else 0)


def call_state(target: MySqlTarget, session_id: str, entity_key: str, idem: str, tick: int) -> tuple[str, int]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @row_version_after = NULL;
        CALL mmo_update_interactive_state(
          {uuid_bin(session_id)},
          {sql_literal(entity_key)},
          2,
          3,
          7,
          FALSE,
          TRUE,
          'active',
          {tick},
          JSON_OBJECT('tool', 'check_mysql_container_interactive_write_path', 'smoke', TRUE),
          {sql_literal(idem)},
          @event_id,
          @row_version_after
        );
        SELECT BIN_TO_UUID(@event_id, 1), CAST(@row_version_after AS SIGNED);
        """,
    )
    parts = raw.split("\t") if raw else ["", "0"]
    return parts[0], int(parts[1] if len(parts) > 1 and parts[1] else 0)


def run_smoke(target: MySqlTarget, account_name: str, character_key: str) -> list[Check]:
    checks: list[Check] = []
    character_count = count(target, f"SELECT COUNT(*) FROM characters WHERE character_key={sql_literal(character_key)};")
    if character_count < 1:
        return [Check("character fixture", False, f"character_key={character_key!r} missing; import runtime SQLite first")]

    suffix = uuid.uuid4().hex
    session_key = f"container-interactive-smoke:{suffix}"
    session_id = ""
    try:
        session_id = call_login(target, account_name, character_key, session_key)
        checks.append(Check("login", bool(session_id), session_id or "missing"))
        bag_index = 710000 + (int(suffix[:6], 16) % 100000)

        entity_key, item_id = seed_container_item(target, character_key, suffix)
        checks.append(Check("container fixture", bool(entity_key and item_id), f"{entity_key}/{item_id}"))

        take_idem = f"smoke:container-take:{suffix}"
        take_event, take_amount = call_take(target, session_id, entity_key, item_id, take_idem, 9001, bag_index)
        checks.append(Check("take append", bool(take_event) and take_amount == 1, f"{take_event}/{take_amount}"))
        take_event_retry, take_amount_retry = call_take(target, session_id, entity_key, item_id, take_idem, 9001, bag_index)
        checks.append(Check("take idempotent retry", take_event_retry == take_event and take_amount_retry == take_amount, f"first={take_event}/{take_amount} retry={take_event_retry}/{take_amount_retry}"))
        checks.append(Check("take duplicate event count", count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(take_idem)};") == 1, str(count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(take_idem)};"))))
        checks.append(Check("take audit count", count(target, f"SELECT COUNT(*) FROM world_interactive_audit WHERE idempotency_key={sql_literal(take_idem)} AND audit_type='container_take';") == 1, str(count(target, f"SELECT COUNT(*) FROM world_interactive_audit WHERE idempotency_key={sql_literal(take_idem)} AND audit_type='container_take';"))))
        checks.append(Check("take world inventory removed", count(target, f"SELECT COUNT(*) FROM world_inventory WHERE owner_entity_key={sql_literal(entity_key)} AND item_instance_id={uuid_bin(item_id)};") == 0, str(count(target, f"SELECT COUNT(*) FROM world_inventory WHERE owner_entity_key={sql_literal(entity_key)} AND item_instance_id={uuid_bin(item_id)};"))))
        checks.append(Check("take character inventory row", count(target, f"SELECT COUNT(*) FROM character_inventory ci JOIN characters c ON c.character_id=ci.character_id WHERE c.character_key={sql_literal(character_key)} AND ci.item_instance_id={uuid_bin(item_id)};") == 1, str(count(target, f"SELECT COUNT(*) FROM character_inventory ci JOIN characters c ON c.character_id=ci.character_id WHERE c.character_key={sql_literal(character_key)} AND ci.item_instance_id={uuid_bin(item_id)};"))))
        owner_after_take = scalar(target, f"SELECT CONCAT(owner_type, '/', lifecycle_state, '/', quantity) FROM item_instances WHERE item_instance_id={uuid_bin(item_id)};")
        checks.append(Check("take item owned by character", owner_after_take == "character/active/1", owner_after_take))

        state_idem = f"smoke:interactive-state:{suffix}"
        state_event, row_version = call_state(target, session_id, entity_key, state_idem, 9002)
        checks.append(Check("interactive state append", bool(state_event) and row_version > 0, f"{state_event}/{row_version}"))
        state_event_retry, row_version_retry = call_state(target, session_id, entity_key, state_idem, 9002)
        checks.append(Check("interactive state idempotent retry", state_event_retry == state_event and row_version_retry == row_version, f"first={state_event}/{row_version} retry={state_event_retry}/{row_version_retry}"))
        state_values = scalar(target, f"SELECT CONCAT(JSON_UNQUOTE(JSON_EXTRACT(state_json, '$.state_id')), '/', JSON_UNQUOTE(JSON_EXTRACT(state_json, '$.state_count')), '/', JSON_UNQUOTE(JSON_EXTRACT(state_json, '$.state_mask')), '/', JSON_UNQUOTE(JSON_EXTRACT(state_json, '$.locked')), '/', JSON_UNQUOTE(JSON_EXTRACT(state_json, '$.cracked'))) FROM world_entity_state WHERE entity_key={sql_literal(entity_key)};")
        checks.append(Check("interactive state projection", state_values == "2/3/7/0/1", state_values))
        checks.append(Check("interactive state audit count", count(target, f"SELECT COUNT(*) FROM world_interactive_audit WHERE idempotency_key={sql_literal(state_idem)} AND audit_type='interactive_state';") == 1, str(count(target, f"SELECT COUNT(*) FROM world_interactive_audit WHERE idempotency_key={sql_literal(state_idem)} AND audit_type='interactive_state';"))))

        put_idem = f"smoke:container-put:{suffix}"
        put_event, put_amount = call_put(target, session_id, entity_key, item_id, put_idem, 9003)
        checks.append(Check("put append", bool(put_event) and put_amount == 1, f"{put_event}/{put_amount}"))
        put_event_retry, put_amount_retry = call_put(target, session_id, entity_key, item_id, put_idem, 9003)
        checks.append(Check("put idempotent retry", put_event_retry == put_event and put_amount_retry == put_amount, f"first={put_event}/{put_amount} retry={put_event_retry}/{put_amount_retry}"))
        checks.append(Check("put duplicate event count", count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(put_idem)};") == 1, str(count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(put_idem)};"))))
        checks.append(Check("put audit count", count(target, f"SELECT COUNT(*) FROM world_interactive_audit WHERE idempotency_key={sql_literal(put_idem)} AND audit_type='container_put';") == 1, str(count(target, f"SELECT COUNT(*) FROM world_interactive_audit WHERE idempotency_key={sql_literal(put_idem)} AND audit_type='container_put';"))))
        checks.append(Check("put character inventory removed", count(target, f"SELECT COUNT(*) FROM character_inventory ci JOIN characters c ON c.character_id=ci.character_id WHERE c.character_key={sql_literal(character_key)} AND ci.item_instance_id={uuid_bin(item_id)};") == 0, str(count(target, f"SELECT COUNT(*) FROM character_inventory ci JOIN characters c ON c.character_id=ci.character_id WHERE c.character_key={sql_literal(character_key)} AND ci.item_instance_id={uuid_bin(item_id)};"))))
        checks.append(Check("put world inventory row", count(target, f"SELECT COUNT(*) FROM world_inventory WHERE owner_entity_key={sql_literal(entity_key)} AND item_instance_id={uuid_bin(item_id)} AND amount=1;") == 1, str(count(target, f"SELECT COUNT(*) FROM world_inventory WHERE owner_entity_key={sql_literal(entity_key)} AND item_instance_id={uuid_bin(item_id)} AND amount=1;"))))
        owner_after_put = scalar(target, f"SELECT CONCAT(owner_type, '/', lifecycle_state, '/', quantity) FROM item_instances WHERE item_instance_id={uuid_bin(item_id)};")
        checks.append(Check("put item owned by container", owner_after_put == "container/active/1", owner_after_put))

        logout_event = call_logout(target, session_id)
        checks.append(Check("logout", bool(logout_event), logout_event or "missing"))
        active_count = count(target, f"SELECT COUNT(*) FROM server_sessions WHERE session_id={uuid_bin(session_id)} AND lifecycle_state='active';")
        checks.append(Check("session closed", active_count == 0, f"active_sessions={active_count}"))
    except Exception as exc:  # noqa: BLE001 - CLI diagnostic
        if session_id:
            try:
                cleanup_event = call_logout(target, session_id)
                checks.append(Check("cleanup logout after failure", bool(cleanup_event), cleanup_event or "missing"))
            except Exception as cleanup_exc:  # noqa: BLE001
                checks.append(Check("cleanup logout after failure", False, str(cleanup_exc)))
        checks.append(Check("smoke execution", False, str(exc)))
    return checks


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Gothic MMO MySQL container/interactive write path.")
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
        print(f"container/interactive check failed: {exc}", file=sys.stderr)
        return 1

    ok = True
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        ok = ok and check.ok

    if not ok:
        print()
        print("Hint: apply 007_container_interactive_write_path.sql after 006_character_inventory_equipment_write_path.sql and import runtime/g2notr.sqlite first.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
