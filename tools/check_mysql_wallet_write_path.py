#!/usr/bin/env python3
"""Validate and optionally smoke-test the MySQL wallet/gold write path.

This script uses the mysql command-line client only. It does not require a
Python MySQL driver. It assumes migrations 001, 002, 003 and 004 are applied.
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
         WHERE migration_key='production/mysql/004_wallet_write_path';
        """,
    )
    ok = value == "gothic-mmo-wallet-write-path-v1-mysql"
    return Check("migration 004 marker", ok, value or "missing")


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
            "wallet tables",
            f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';",
            ("character_wallet_audit",),
        ),
        check_named(
            target,
            "wallet views",
            f"SELECT table_name FROM information_schema.views WHERE table_schema='{db}';",
            ("v_character_wallets", "v_character_wallet_audit"),
        ),
        check_named(
            target,
            "wallet routines",
            f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';",
            ("mmo_adjust_character_wallet", "mmo_grant_character_gold", "mmo_spend_character_gold"),
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
          'wallet-smoke',
          'local',
          JSON_OBJECT('tool', 'check_mysql_wallet_write_path'),
          @session_id
        );
        SELECT BIN_TO_UUID(@session_id, 1);
        """,
    )


def call_grant_gold(target: MySqlTarget, session_id: str, amount: int, idempotency_key: str, tick: int) -> tuple[str, int]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @amount_after = NULL;
        CALL mmo_grant_character_gold(
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          {amount},
          'wallet_smoke_grant',
          {tick},
          JSON_OBJECT('tool', 'check_mysql_wallet_write_path', 'smoke', TRUE),
          {sql_literal(idempotency_key)},
          @event_id,
          @amount_after
        );
        SELECT BIN_TO_UUID(@event_id, 1), CAST(@amount_after AS SIGNED);
        """,
    )
    parts = raw.split("\t") if raw else ["", "0"]
    return parts[0], int(parts[1] if len(parts) > 1 and parts[1] else 0)


def call_spend_gold(target: MySqlTarget, session_id: str, amount: int, idempotency_key: str, tick: int) -> tuple[str, int]:
    raw = run_mysql(
        target,
        f"""
        SET @event_id = NULL;
        SET @amount_after = NULL;
        CALL mmo_spend_character_gold(
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          {amount},
          'wallet_smoke_spend',
          {tick},
          JSON_OBJECT('tool', 'check_mysql_wallet_write_path', 'smoke', TRUE),
          {sql_literal(idempotency_key)},
          @event_id,
          @amount_after
        );
        SELECT BIN_TO_UUID(@event_id, 1), CAST(@amount_after AS SIGNED);
        """,
    )
    parts = raw.split("\t") if raw else ["", "0"]
    return parts[0], int(parts[1] if len(parts) > 1 and parts[1] else 0)


def call_logout(target: MySqlTarget, session_id: str) -> str:
    return scalar(
        target,
        f"""
        SET @event_id = NULL;
        CALL mmo_logout_character(
          UUID_TO_BIN({sql_literal(session_id)}, 1),
          'wallet_smoke_done',
          JSON_OBJECT('tool', 'check_mysql_wallet_write_path'),
          @event_id
        );
        SELECT BIN_TO_UUID(@event_id, 1);
        """,
    )


def current_gold(target: MySqlTarget, character_key: str) -> int:
    raw = scalar(
        target,
        f"""
        SELECT COALESCE(CAST(cw.amount AS SIGNED), 0)
          FROM characters c
          LEFT JOIN character_wallets cw
            ON cw.character_id = c.character_id
           AND cw.currency_key = 'g2notr:gold'
         WHERE c.character_key = {sql_literal(character_key)}
         LIMIT 1;
        """,
    )
    try:
        return int(raw or "0")
    except ValueError:
        return 0


def run_smoke(target: MySqlTarget, account_name: str, character_key: str) -> list[Check]:
    checks: list[Check] = []
    character_count = count(target, f"SELECT COUNT(*) FROM characters WHERE character_key={sql_literal(character_key)};")
    account_count = count(target, f"SELECT COUNT(*) FROM account_accounts WHERE account_name={sql_literal(account_name)};")
    if account_count == 0 or character_count == 0:
        checks.append(Check(
            "bootstrap data",
            False,
            "missing account/character; run import_runtime_sqlite_to_mysql.py before wallet smoke",
        ))
        return checks

    suffix = uuid.uuid4().hex
    session_key = f"wallet-smoke:{character_key}:{suffix}"
    grant_key = f"wallet:{session_key}:grant-10"
    spend_key = f"wallet:{session_key}:spend-10"
    starting_balance = current_gold(target, character_key)

    try:
        session_id = call_login(target, account_name, character_key, session_key)
        checks.append(Check("login", bool(session_id), session_id or "no session id"))

        grant_event_1, grant_after_1 = call_grant_gold(target, session_id, 10, grant_key, 2001)
        grant_event_2, grant_after_2 = call_grant_gold(target, session_id, 10, grant_key, 2001)
        checks.append(Check("gold grant append", bool(grant_event_1), grant_event_1 or "no event id"))
        checks.append(Check(
            "gold grant idempotent retry",
            grant_event_1 == grant_event_2 and grant_after_1 == grant_after_2 and bool(grant_event_2),
            f"first={grant_event_1}/{grant_after_1} retry={grant_event_2}/{grant_after_2}",
        ))

        grant_event_count = count(target, f"SELECT COUNT(*) FROM world_event_journal WHERE idempotency_key={sql_literal(grant_key)};")
        grant_audit_count = count(target, f"SELECT COUNT(*) FROM character_wallet_audit WHERE idempotency_key={sql_literal(grant_key)};")
        checks.append(Check("grant duplicate event count", grant_event_count == 1, str(grant_event_count)))
        checks.append(Check("grant audit count", grant_audit_count == 1, str(grant_audit_count)))
        checks.append(Check("grant balance", grant_after_1 == starting_balance + 10, f"start={starting_balance} after={grant_after_1}"))

        spend_event, spend_after = call_spend_gold(target, session_id, 10, spend_key, 2002)
        checks.append(Check("gold spend append", bool(spend_event), spend_event or "no event id"))
        checks.append(Check("spend restored balance", spend_after == starting_balance, f"start={starting_balance} after={spend_after}"))

        final_balance = current_gold(target, character_key)
        checks.append(Check("final wallet balance", final_balance == starting_balance, f"start={starting_balance} final={final_balance}"))

        logout_event = call_logout(target, session_id)
        checks.append(Check("logout", bool(logout_event), logout_event or "no event id"))

        active_sessions = count(
            target,
            f"SELECT COUNT(*) FROM v_active_server_sessions WHERE session_key={sql_literal(session_key)};",
        )
        checks.append(Check("session closed", active_sessions == 0, f"active_sessions={active_sessions}"))
    except Exception as exc:  # noqa: BLE001 - smoke diagnostic tool
        checks.append(Check("smoke execution", False, str(exc)))

    return checks


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate MySQL Gothic MMO wallet/gold write path.")
    parser.add_argument("--url", default=os.environ.get("MYSQL_URL", ""), help="mysql://user:password@host:port/database. Defaults to MYSQL_URL.")
    parser.add_argument("--account-name", default="local-import")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--run-smoke", action="store_true", help="Run login/gold grant/idempotent retry/spend/logout smoke test.")
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
        print(f"wallet write path check failed: {exc}", file=sys.stderr)
        return 1

    ok = True
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        ok = ok and check.ok

    if not ok:
        print("\nHint: apply 004_wallet_write_path.sql after 003_server_write_path.sql and import runtime/g2notr.sqlite first.")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
