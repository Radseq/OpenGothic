#!/usr/bin/env python3
"""
Smoke validator for the Gothic MMO PostgreSQL production schema.

The script intentionally uses the `psql` command line client instead of importing a
Python PostgreSQL driver, so it can run in a minimal developer environment.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Iterable


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

REQUIRED_FUNCTIONS: tuple[str, ...] = (
    "mmo_touch_updated_at",
    "mmo_append_world_event",
)

REQUIRED_INDEXES: tuple[str, ...] = (
    "ux_world_event_journal_idempotency",
    "ix_world_event_journal_world_seq",
    "ix_world_event_journal_actor_seq",
    "ix_world_event_journal_type_seq",
    "ix_world_event_journal_payload_gin",
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
class CheckResult:
    name: str
    ok: bool
    detail: str


def run_psql(dsn: str, sql: str) -> str:
    psql = shutil.which("psql")
    if psql is None:
        raise RuntimeError("psql executable was not found in PATH")

    cmd = [psql, dsn, "-X", "-v", "ON_ERROR_STOP=1", "-Atqc", sql]
    completed = subprocess.run(
        cmd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or f"psql failed with exit code {completed.returncode}")
    return completed.stdout.strip()


def csv_set(raw: str) -> set[str]:
    if not raw:
        return set()
    return {line.strip() for line in raw.splitlines() if line.strip()}


def check_named_objects(dsn: str, title: str, sql: str, required: Iterable[str]) -> CheckResult:
    found = csv_set(run_psql(dsn, sql))
    required_set = set(required)
    missing = sorted(required_set - found)
    if missing:
        return CheckResult(title, False, "missing: " + ", ".join(missing))
    return CheckResult(title, True, f"ok ({len(required_set)} required)")


def check_schema_marker(dsn: str) -> CheckResult:
    value = run_psql(
        dsn,
        """
        SELECT schema_contract
          FROM mmo_schema_versions
         WHERE migration_key='production/001_gothic_mmo_production_schema';
        """,
    )
    if value != "gothic-mmo-production-db-v1":
        return CheckResult("schema marker", False, f"unexpected marker: {value!r}")
    return CheckResult("schema marker", True, value)


def check_event_journal_append(dsn: str) -> CheckResult:
    """Validate function signature without inserting game data."""
    signature = run_psql(
        dsn,
        """
        SELECT pg_get_function_identity_arguments(p.oid)
          FROM pg_proc p
          JOIN pg_namespace n ON n.oid=p.pronamespace
         WHERE n.nspname='public'
           AND p.proname='mmo_append_world_event';
        """,
    )
    required_fragments = (
        "p_realm_id uuid",
        "p_world_instance_id uuid",
        "p_event_type text",
        "p_event_class text",
        "p_idempotency_key text",
    )
    missing = [frag for frag in required_fragments if frag not in signature]
    if missing:
        return CheckResult("event append function", False, "signature missing: " + ", ".join(missing))
    return CheckResult("event append function", True, "signature ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Gothic MMO PostgreSQL production schema.")
    parser.add_argument("--dsn", default=os.environ.get("DATABASE_URL"), help="PostgreSQL DSN. Defaults to DATABASE_URL.")
    args = parser.parse_args()

    if not args.dsn:
        print("error: provide --dsn or DATABASE_URL", file=sys.stderr)
        return 2

    checks: list[CheckResult] = []

    try:
        checks.append(check_schema_marker(args.dsn))
        checks.append(check_named_objects(
            args.dsn,
            "tables",
            """
            SELECT table_name
              FROM information_schema.tables
             WHERE table_schema='public'
               AND table_type='BASE TABLE';
            """,
            REQUIRED_TABLES,
        ))
        checks.append(check_named_objects(
            args.dsn,
            "views",
            """
            SELECT table_name
              FROM information_schema.views
             WHERE table_schema='public';
            """,
            REQUIRED_VIEWS,
        ))
        checks.append(check_named_objects(
            args.dsn,
            "functions",
            """
            SELECT p.proname
              FROM pg_proc p
              JOIN pg_namespace n ON n.oid=p.pronamespace
             WHERE n.nspname='public';
            """,
            REQUIRED_FUNCTIONS,
        ))
        checks.append(check_named_objects(
            args.dsn,
            "indexes",
            """
            SELECT indexname
              FROM pg_indexes
             WHERE schemaname='public';
            """,
            REQUIRED_INDEXES,
        ))
        checks.append(check_named_objects(
            args.dsn,
            "constraints",
            """
            SELECT conname
              FROM pg_constraint c
              JOIN pg_namespace n ON n.oid=c.connamespace
             WHERE n.nspname='public';
            """,
            REQUIRED_CONSTRAINTS,
        ))
        checks.append(check_event_journal_append(args.dsn))
    except Exception as exc:  # noqa: BLE001 - command-line diagnostic tool
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
