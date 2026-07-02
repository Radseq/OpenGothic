#!/usr/bin/env python3
"""Smoke validator for the Gothic MMO MySQL production schema."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Iterable
from urllib.parse import urlparse, unquote

REQUIRED_TABLES: tuple[str, ...] = (
    "mmo_schema_versions",
    "account_accounts",
    "account_entitlements",
    "content_game_targets",
    "content_revisions",
    "content_world_templates",
    "content_entity_templates",
    "content_item_templates",
    "realm_realms",
    "realm_world_instances",
    "characters",
    "character_positions",
    "character_stats",
    "character_wallets",
    "item_instances",
    "character_inventory",
    "character_equipment",
    "character_quests",
    "character_known_dialogs",
    "character_script_state",
    "world_entity_state",
    "world_inventory",
    "world_script_state",
    "world_event_journal",
    "world_projection_offsets",
    "world_state_snapshots",
)

REQUIRED_VIEWS: tuple[str, ...] = (
    "v_character_sheet",
    "v_character_inventory",
    "v_world_event_counts",
    "v_world_dead_entities",
)

REQUIRED_ROUTINES: tuple[str, ...] = (
    "mmo_append_world_event",
)

REQUIRED_INDEXES: tuple[str, ...] = (
    "ux_world_event_journal_idempotency",
    "ix_world_event_journal_world_seq",
    "ix_world_event_journal_actor_seq",
    "ix_world_event_journal_type_seq",
)

REQUIRED_CONSTRAINTS: tuple[str, ...] = (
    "account_accounts_status_ck",
    "content_revisions_key_uk",
    "realm_realms_status_ck",
    "characters_name_per_realm_uk",
    "character_stats_hp_ck",
    "character_wallets_amount_ck",
    "item_instances_owner_type_ck",
    "character_equipment_slot_ck",
    "world_entity_state_uk",
    "world_event_journal_event_class_ck",
    "world_event_journal_source_ck",
)

@dataclass(frozen=True)
class MySqlTarget:
    host: str
    port: int
    user: str
    password: str
    database: str

@dataclass(frozen=True)
class CheckResult:
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
    completed = subprocess.run(
        mysql_cmd(target) + ["--execute", sql],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or f"mysql failed with exit code {completed.returncode}")
    return completed.stdout.strip()


def line_set(raw: str) -> set[str]:
    if not raw:
        return set()
    return {line.strip() for line in raw.splitlines() if line.strip()}


def sql_list(values: Iterable[str]) -> str:
    return ",".join("'" + value.replace("'", "''") + "'" for value in values)


def check_named_objects(target: MySqlTarget, title: str, sql: str, required: Iterable[str]) -> CheckResult:
    found = line_set(run_mysql(target, sql))
    required_set = set(required)
    missing = sorted(required_set - found)
    if missing:
        return CheckResult(title, False, "missing: " + ", ".join(missing))
    return CheckResult(title, True, f"ok ({len(required_set)} required)")


def check_schema_marker(target: MySqlTarget) -> CheckResult:
    value = run_mysql(
        target,
        """
        SELECT schema_contract
          FROM mmo_schema_versions
         WHERE migration_key='production/mysql/001_gothic_mmo_production_schema';
        """,
    )
    if value != "gothic-mmo-production-db-v1-mysql":
        return CheckResult("schema marker", False, f"unexpected marker: {value!r}")
    return CheckResult("schema marker", True, value)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Gothic MMO MySQL production schema.")
    parser.add_argument("--url", default=os.environ.get("MYSQL_URL", ""), help="mysql://user:password@host:port/database. Defaults to MYSQL_URL.")
    args = parser.parse_args()

    if not args.url:
        print("error: provide --url or MYSQL_URL", file=sys.stderr)
        return 2

    try:
        target = parse_url(args.url)
        db = target.database.replace("'", "''")
        checks: list[CheckResult] = [check_schema_marker(target)]
        checks.append(check_named_objects(
            target,
            "tables",
            f"SELECT table_name FROM information_schema.tables WHERE table_schema='{db}' AND table_type='BASE TABLE';",
            REQUIRED_TABLES,
        ))
        checks.append(check_named_objects(
            target,
            "views",
            f"SELECT table_name FROM information_schema.views WHERE table_schema='{db}';",
            REQUIRED_VIEWS,
        ))
        checks.append(check_named_objects(
            target,
            "routines",
            f"SELECT routine_name FROM information_schema.routines WHERE routine_schema='{db}';",
            REQUIRED_ROUTINES,
        ))
        checks.append(check_named_objects(
            target,
            "indexes",
            f"SELECT DISTINCT index_name FROM information_schema.statistics WHERE table_schema='{db}';",
            REQUIRED_INDEXES,
        ))
        checks.append(check_named_objects(
            target,
            "constraints",
            f"SELECT constraint_name FROM information_schema.table_constraints WHERE table_schema='{db}';",
            REQUIRED_CONSTRAINTS,
        ))
    except Exception as exc:  # noqa: BLE001 - command-line diagnostic
        print(f"schema check failed: {exc}", file=sys.stderr)
        return 1

    ok = True
    for check in checks:
        status = "OK" if check.ok else "FAIL"
        print(f"[{status}] {check.name}: {check.detail}")
        ok = ok and check.ok
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
