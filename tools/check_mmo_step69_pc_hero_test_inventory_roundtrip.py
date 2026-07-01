#!/usr/bin/env python3
"""Check the PC_HERO_TEST live inventory roundtrip after Step68."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SESSION_KEY = "local-dev-PC_HERO_TEST"
DEFAULT_CHARACTER_KEY = "PC_HERO"
DEFAULT_SNAPSHOT = ROOT / "runtime" / "pc_hero_test_live" / "mysql_restore_snapshot.json"

EXPECTED_RESPONSE_KIND = {
    "pickup_world_item": "pickup_ack",
    "drop_character_item": "drop_item_ack",
    "loot_npc_inventory": "loot_npc_inventory_ack",
    "equip_character_item": "equipment_ack",
    "unequip_character_item": "equipment_ack",
    "use_interactive": "interactive_use_ack",
    "movement_proposal": "movement_authority_ack",
    "client_bootstrap_request": "bootstrap_ack",
}


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
    database = (p.path or "/").lstrip("/")
    if not database:
        raise ValueError("database name is missing")
    return Target(
        host=p.hostname or "127.0.0.1",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=database,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
        "--skip-column-names",
        "-h",
        target.host,
        "-P",
        str(target.port),
        "-u",
        target.user,
    ]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        head = "\n".join(line.rstrip() for line in sql.strip().splitlines()[:16])
        raise RuntimeError(f"mysql exited with status {proc.returncode}: {proc.stderr.strip()}; sql_head={head}")
    return proc.stdout.strip()


def rows(raw: str) -> list[list[str]]:
    return [line.split("\t") for line in raw.splitlines() if line.strip()]


def table_exists(target: Target, table: str) -> bool:
    raw = run_mysql(
        target,
        f"""
        SELECT COUNT(*)
          FROM information_schema.TABLES
         WHERE table_schema = DATABASE()
           AND table_name = {sql_literal(table)};
        """,
    )
    return (raw.splitlines()[-1].strip() if raw else "0") == "1"


def scalar_int(target: Target, sql: str) -> int:
    raw = run_mysql(target, sql)
    if not raw:
        return 0
    return int((raw.splitlines()[-1] or "0").split("\t")[0] or "0")


def require_tables(target: Target) -> dict[str, object]:
    required = [
        "mmo_server_action_outbox",
        "characters",
        "character_inventory",
        "character_equipment",
        "item_instances",
        "world_item_audit",
        "world_event_journal",
    ]
    present = {name: table_exists(target, name) for name in required}
    return {
        "present": present,
        "missing": [name for name, ok in present.items() if not ok],
    }


def action_summary(target: Target, session_key: str) -> dict[str, object]:
    prefix = session_key + ":%"
    raw = run_mysql(
        target,
        f"""
        SELECT action_kind,
               status,
               COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.response_kind')), ''),
               COUNT(*) AS c
         FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         GROUP BY action_kind, status, COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.response_kind')), '')
         ORDER BY 1, 2, 3;
        """,
    )
    status_totals: dict[str, int] = {}
    action_totals: dict[str, int] = {}
    response_kind_totals: dict[str, int] = {}
    grouped = []
    for row in rows(raw):
        row = row + [""] * 4
        count = int(row[3] or 0)
        grouped.append({"action_kind": row[0], "status": row[1], "response_kind": row[2] or None, "count": count})
        status_totals[row[1]] = status_totals.get(row[1], 0) + count
        action_totals[row[0]] = action_totals.get(row[0], 0) + count
        if row[2]:
            response_kind_totals[row[2]] = response_kind_totals.get(row[2], 0) + count

    failed_raw = run_mysql(
        target,
        f"""
        SELECT action_kind, status, COALESCE(last_error_code,''), LEFT(COALESCE(last_error_message,''),240), idempotency_key
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
           AND status IN ('failed','dead_letter')
         ORDER BY failed_at DESC, requested_at DESC
         LIMIT 30;
        """,
    )
    failed = []
    for row in rows(failed_raw):
        row = row + [""] * 5
        failed.append({"action_kind": row[0], "status": row[1], "error_code": row[2], "error_message": row[3], "idempotency_key": row[4]})

    mismatch_raw = run_mysql(
        target,
        f"""
        SELECT action_kind, status,
               COALESCE(JSON_UNQUOTE(JSON_EXTRACT(result_payload,'$.response_kind')), '') AS response_kind,
               idempotency_key
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
           AND status = 'applied'
           AND action_kind IN ('pickup_world_item','drop_character_item','loot_npc_inventory','equip_character_item','unequip_character_item','use_interactive','movement_proposal','client_bootstrap_request')
         ORDER BY requested_at ASC
         LIMIT 500;
        """,
    )
    mismatches = []
    for row in rows(mismatch_raw):
        row = row + [""] * 4
        expected = EXPECTED_RESPONSE_KIND.get(row[0])
        actual = row[2] or None
        if expected and actual != expected:
            mismatches.append({"action_kind": row[0], "expected_response_kind": expected, "actual_response_kind": actual, "idempotency_key": row[3]})

    return {
        "status_totals": status_totals,
        "action_totals": action_totals,
        "response_kind_totals": response_kind_totals,
        "grouped": grouped,
        "failed_rows": failed,
        "response_kind_mismatches": mismatches,
    }


def evidence_summary(target: Target, session_key: str) -> dict[str, object]:
    prefix = session_key + ":%"
    events_raw = run_mysql(
        target,
        f"""
        SELECT event_type, event_class, COUNT(*) AS c
          FROM world_event_journal
         WHERE idempotency_key LIKE {sql_literal(prefix)}
           AND event_class IN ('inventory','equipment','world_entity')
         GROUP BY event_type, event_class
         ORDER BY event_type, event_class;
        """,
    )
    audit_raw = run_mysql(
        target,
        f"""
        SELECT audit_type, COUNT(*) AS c
          FROM world_item_audit
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         GROUP BY audit_type
         ORDER BY audit_type;
        """,
    )
    return {
        "events": [{"event_type": r[0], "event_class": r[1], "count": int(r[2] or 0)} for r in (row + [""] * 3 for row in rows(events_raw))],
        "world_item_audit": [{"audit_type": r[0], "count": int(r[1] or 0)} for r in (row + [""] * 2 for row in rows(audit_raw))],
    }


def inventory_integrity(target: Target, character_key: str) -> dict[str, object]:
    duplicate_bags = scalar_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM (
            SELECT ci.bag_index
              FROM character_inventory ci
              JOIN characters c ON c.character_id = ci.character_id
             WHERE c.character_key = {sql_literal(character_key)}
               AND ci.bag_index IS NOT NULL
             GROUP BY ci.bag_index
            HAVING COUNT(*) > 1
          ) d;
        """,
    )
    equipped_missing_inventory = scalar_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM character_equipment ce
          JOIN characters c ON c.character_id = ce.character_id
          LEFT JOIN character_inventory ci
            ON ci.character_id = ce.character_id
           AND ci.item_instance_id = ce.item_instance_id
         WHERE c.character_key = {sql_literal(character_key)}
           AND ci.item_instance_id IS NULL;
        """,
    )
    inventory_owner_mismatch = scalar_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM character_inventory ci
          JOIN characters c ON c.character_id = ci.character_id
          JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
         WHERE c.character_key = {sql_literal(character_key)}
           AND (ii.owner_type <> 'character' OR ii.owner_id <> c.character_id OR ii.lifecycle_state <> 'active');
        """,
    )
    non_positive_amount = scalar_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM character_inventory ci
          JOIN characters c ON c.character_id = ci.character_id
         WHERE c.character_key = {sql_literal(character_key)}
           AND ci.amount <= 0;
        """,
    )
    inventory_count = scalar_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM character_inventory ci
          JOIN characters c ON c.character_id = ci.character_id
         WHERE c.character_key = {sql_literal(character_key)};
        """,
    )
    equipment_count = scalar_int(
        target,
        f"""
        SELECT COUNT(*)
          FROM character_equipment ce
          JOIN characters c ON c.character_id = ce.character_id
         WHERE c.character_key = {sql_literal(character_key)};
        """,
    )
    problems = []
    if duplicate_bags:
        problems.append("duplicate_bag_index")
    if equipped_missing_inventory:
        problems.append("equipped_item_not_in_character_inventory")
    if inventory_owner_mismatch:
        problems.append("inventory_owner_mismatch")
    if non_positive_amount:
        problems.append("non_positive_inventory_amount")
    return {
        "ok": not problems,
        "problems": problems,
        "inventory_count": inventory_count,
        "equipment_count": equipment_count,
        "duplicate_bag_indexes": duplicate_bags,
        "equipped_missing_inventory": equipped_missing_inventory,
        "inventory_owner_mismatch": inventory_owner_mismatch,
        "non_positive_amount": non_positive_amount,
    }


def snapshot_check(snapshot_path: Path, db_integrity: dict[str, object]) -> dict[str, object]:
    if not snapshot_path.exists():
        return {"path": str(snapshot_path), "exists": False, "ok": True, "note": "snapshot not required for this check"}
    data = json.loads(snapshot_path.read_text(encoding="utf-8"))
    snap_integrity = data.get("integrity") if isinstance(data.get("integrity"), dict) else {}
    inventory_count = int(snap_integrity.get("inventory_count") or len(data.get("inventory", [])))
    equipment_count = int(snap_integrity.get("equipment_count") or len(data.get("equipment", [])))
    problems = []
    if not snap_integrity.get("ok", False):
        problems.append("snapshot_integrity_not_ok")
    if inventory_count != int(db_integrity.get("inventory_count") or 0):
        problems.append("snapshot_inventory_count_differs_from_db")
    if equipment_count != int(db_integrity.get("equipment_count") or 0):
        problems.append("snapshot_equipment_count_differs_from_db")
    return {
        "path": str(snapshot_path),
        "exists": True,
        "ok": not problems,
        "problems": problems,
        "snapshot_inventory_count": inventory_count,
        "snapshot_equipment_count": equipment_count,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Check PC_HERO_TEST live inventory roundtrip after Step68.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", default=DEFAULT_SESSION_KEY)
    ap.add_argument("--character-key", default=DEFAULT_CHARACTER_KEY)
    ap.add_argument("--snapshot", default=str(DEFAULT_SNAPSHOT))
    ap.add_argument("--require-pickup", action="store_true")
    ap.add_argument("--require-drop", action="store_true")
    ap.add_argument("--require-loot", action="store_true")
    ap.add_argument("--require-equipment", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "69_pc_hero_test_inventory_roundtrip",
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "session_key": args.session_key,
        "character_key": args.character_key,
    }
    try:
        result["tables"] = require_tables(target)
        if result["tables"]["missing"]:
            result["status"] = "failed"
            result["errors"] = ["missing_tables"]
        else:
            result["actions"] = action_summary(target, args.session_key)
            result["evidence"] = evidence_summary(target, args.session_key)
            result["inventory_integrity"] = inventory_integrity(target, args.character_key)
            snapshot_path = Path(args.snapshot)
            if not snapshot_path.is_absolute():
                snapshot_path = ROOT / snapshot_path
            result["snapshot"] = snapshot_check(snapshot_path, result["inventory_integrity"])

            errors = []
            action_totals = result["actions"]["action_totals"]
            response_totals = result["actions"]["response_kind_totals"]
            if result["actions"]["failed_rows"]:
                errors.append("failed_outbox_rows")
            if result["actions"]["response_kind_mismatches"]:
                errors.append("response_kind_mismatch")
            if not result["inventory_integrity"]["ok"]:
                errors.append("inventory_integrity")
            if not result["snapshot"]["ok"]:
                errors.append("snapshot")
            if args.require_pickup and action_totals.get("pickup_world_item", 0) == 0:
                errors.append("missing_pickup_world_item")
            if args.require_drop and action_totals.get("drop_character_item", 0) == 0:
                errors.append("missing_drop_character_item")
            if args.require_loot and action_totals.get("loot_npc_inventory", 0) == 0:
                errors.append("missing_loot_npc_inventory")
            if args.require_equipment and not (action_totals.get("equip_character_item", 0) or action_totals.get("unequip_character_item", 0)):
                errors.append("missing_equipment_action")
            if args.require_drop and response_totals.get("drop_item_ack", 0) == 0:
                errors.append("missing_drop_item_ack")
            if args.require_loot and response_totals.get("loot_npc_inventory_ack", 0) == 0:
                errors.append("missing_loot_npc_inventory_ack")
            result["errors"] = errors
            result["status"] = "failed" if errors else "passed"
    except Exception as exc:  # noqa: BLE001 - checker should emit a report, not a traceback
        result["status"] = "failed"
        result["errors"] = ["checker_exception"]
        result["exception"] = {"type": type(exc).__name__, "message": str(exc)}

    if args.output:
        out = Path(args.output)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("status=" + str(result["status"]))
    if result.get("errors"):
        print("errors=" + ",".join(str(x) for x in result["errors"]))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
