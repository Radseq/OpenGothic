#!/usr/bin/env python3
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


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SESSION_KEY = "local-dev-PC_HERO_TEST"
DEFAULT_CHARACTER_KEY = "PC_HERO"
DEFAULT_OUTPUT = ROOT / "runtime" / "pc_hero_test_live" / "mysql_restore_snapshot.json"


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


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(
        mysql_cmd(target) + ["--execute", sql],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr, file=sys.stderr, end="")
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''") + "'"


def rows(raw: str) -> list[list[str]]:
    if not raw:
        return []
    return [line.split("\t") for line in raw.splitlines()]


def table_columns(target: Target, table: str) -> set[str]:
    raw = run_mysql(
        target,
        f"""
        SELECT column_name
          FROM information_schema.COLUMNS
         WHERE table_schema = DATABASE()
           AND table_name = {sql_literal(table)}
         ORDER BY ordinal_position;
        """,
    )
    return {line.strip() for line in raw.splitlines() if line.strip()}


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


def expr(cols: set[str], alias: str, column: str, fallback: str = "NULL") -> str:
    return f"{alias}.{column}" if column in cols else fallback


def uuid_expr(cols: set[str], alias: str, column: str) -> str:
    return f"BIN_TO_UUID({alias}.{column}, 1)" if column in cols else "NULL"


def require_tables(target: Target, names: tuple[str, ...]) -> dict[str, bool]:
    return {name: table_exists(target, name) for name in names}


def item_template_table(target: Target) -> str:
    if table_exists(target, "content_item_templates"):
        return "content_item_templates"
    if table_exists(target, "item_templates"):
        return "item_templates"
    raise RuntimeError("missing required item template table: content_item_templates or item_templates")


def load_character(target: Target, character_key: str, c_cols: set[str]) -> dict[str, object] | None:
    display = expr(c_cols, "c", "display_name")
    realm = uuid_expr(c_cols, "c", "realm_id")
    raw = run_mysql(
        target,
        f"""
        SELECT {uuid_expr(c_cols, 'c', 'character_id')},
               c.character_key,
               {display},
               {realm}
          FROM characters c
         WHERE c.character_key = {sql_literal(character_key)}
         ORDER BY c.character_key
         LIMIT 1;
        """,
    )
    data = rows(raw)
    if not data:
        return None
    row = data[0] + [""] * 4
    return {
        "character_uuid": row[0] or None,
        "character_key": row[1] or character_key,
        "display_name": row[2] or None,
        "realm_uuid": row[3] or None,
    }


def load_inventory(target: Target, character_key: str, cols: dict[str, set[str]]) -> list[dict[str, object]]:
    ci = cols["character_inventory"]
    ii = cols["item_instances"]
    template_table = str(cols["_template_table_name"])
    it = cols[template_table]
    raw = run_mysql(
        target,
        f"""
        SELECT {uuid_expr(ci, 'ci', 'item_instance_id')},
               {expr(ii, 'ii', 'item_instance_key')},
               {expr(it, 'it', 'item_template_key')},
               {expr(it, 'it', 'symbol_index')},
               {expr(it, 'it', 'display_name')},
               {expr(ci, 'ci', 'bag_index')},
               {expr(ci, 'ci', 'amount', '1')},
               {expr(ci, 'ci', 'source_amount')},
               {expr(ci, 'ci', 'source_iterator_count')},
               {expr(ii, 'ii', 'owner_type')},
               {expr(ii, 'ii', 'lifecycle_state')},
               {expr(ii, 'ii', 'quantity')},
               {expr(ci, 'ci', 'updated_at')}
          FROM character_inventory ci
          JOIN characters c ON c.character_id = ci.character_id
          LEFT JOIN item_instances ii ON ii.item_instance_id = ci.item_instance_id
          LEFT JOIN {template_table} it ON it.item_template_id = ii.item_template_id
         WHERE c.character_key = {sql_literal(character_key)}
         ORDER BY COALESCE(ci.bag_index, 2147483647), ii.item_instance_key, ci.item_instance_id;
        """,
    )
    out: list[dict[str, object]] = []
    for row in rows(raw):
        row = row + [""] * 13
        out.append(
            {
                "item_instance_uuid": row[0] or None,
                "item_instance_key": row[1] or None,
                "item_template_key": row[2] or None,
                "symbol_index": int(row[3]) if row[3].isdigit() else None,
                "display_name": row[4] or None,
                "bag_index": int(row[5]) if row[5].lstrip("-").isdigit() else None,
                "amount": int(row[6]) if row[6].lstrip("-").isdigit() else None,
                "source_amount": int(row[7]) if row[7].lstrip("-").isdigit() else None,
                "source_iterator_count": int(row[8]) if row[8].lstrip("-").isdigit() else None,
                "owner_type": row[9] or None,
                "lifecycle_state": row[10] or None,
                "instance_quantity": int(row[11]) if row[11].lstrip("-").isdigit() else None,
                "updated_at": row[12] or None,
            }
        )
    return out


def load_equipment(target: Target, character_key: str, cols: dict[str, set[str]]) -> list[dict[str, object]]:
    ce = cols["character_equipment"]
    ii = cols["item_instances"]
    template_table = str(cols["_template_table_name"])
    it = cols[template_table]
    raw = run_mysql(
        target,
        f"""
        SELECT {expr(ce, 'ce', 'equipment_slot')},
               {uuid_expr(ce, 'ce', 'item_instance_id')},
               {expr(ii, 'ii', 'item_instance_key')},
               {expr(it, 'it', 'item_template_key')},
               {expr(it, 'it', 'symbol_index')},
               {expr(it, 'it', 'display_name')},
               {expr(ce, 'ce', 'equipped_at')},
               {expr(ce, 'ce', 'updated_at')},
               {expr(ii, 'ii', 'owner_type')},
               {expr(ii, 'ii', 'lifecycle_state')}
          FROM character_equipment ce
          JOIN characters c ON c.character_id = ce.character_id
          LEFT JOIN item_instances ii ON ii.item_instance_id = ce.item_instance_id
          LEFT JOIN {template_table} it ON it.item_template_id = ii.item_template_id
         WHERE c.character_key = {sql_literal(character_key)}
         ORDER BY ce.equipment_slot;
        """,
    )
    out: list[dict[str, object]] = []
    for row in rows(raw):
        row = row + [""] * 10
        out.append(
            {
                "equipment_slot": row[0] or None,
                "item_instance_uuid": row[1] or None,
                "item_instance_key": row[2] or None,
                "item_template_key": row[3] or None,
                "symbol_index": int(row[4]) if row[4].isdigit() else None,
                "display_name": row[5] or None,
                "equipped_at": row[6] or None,
                "updated_at": row[7] or None,
                "owner_type": row[8] or None,
                "lifecycle_state": row[9] or None,
            }
        )
    return out


def validate_snapshot(inventory: list[dict[str, object]], equipment: list[dict[str, object]]) -> dict[str, object]:
    inventory_ids = {str(row["item_instance_uuid"]) for row in inventory if row.get("item_instance_uuid")}
    bag_indexes = [row.get("bag_index") for row in inventory if row.get("bag_index") is not None]
    duplicate_bags = sorted({int(v) for v in bag_indexes if bag_indexes.count(v) > 1})
    missing_inventory = [
        row
        for row in equipment
        if row.get("item_instance_uuid") and str(row["item_instance_uuid"]) not in inventory_ids
    ]
    duplicate_slots: list[str] = []
    seen_slots: set[str] = set()
    for row in equipment:
        slot = str(row.get("equipment_slot") or "")
        if not slot:
            continue
        if slot in seen_slots and slot not in duplicate_slots:
            duplicate_slots.append(slot)
        seen_slots.add(slot)
    problems = []
    if duplicate_bags:
        problems.append("duplicate_bag_index")
    if duplicate_slots:
        problems.append("duplicate_equipment_slot")
    if missing_inventory:
        problems.append("equipped_item_not_in_character_inventory")
    return {
        "ok": not problems,
        "problems": problems,
        "inventory_count": len(inventory),
        "equipment_count": len(equipment),
        "duplicate_bag_indexes": duplicate_bags,
        "duplicate_equipment_slots": duplicate_slots,
        "equipped_items_missing_from_inventory": missing_inventory,
    }


def load_session_action_summary(target: Target, session_key: str) -> dict[str, object]:
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
    ) if table_exists(target, "mmo_server_action_outbox") else ""
    rows_out = []
    totals: dict[str, int] = {}
    response_kinds: dict[str, int] = {}
    for row in rows(raw):
        row = row + [""] * 4
        count = int(row[3] or 0)
        rows_out.append({"action_kind": row[0], "status": row[1], "response_kind": row[2] or None, "count": count})
        totals[row[1]] = totals.get(row[1], 0) + count
        if row[2]:
            response_kinds[row[2]] = response_kinds.get(row[2], 0) + count

    failure_raw = run_mysql(
        target,
        f"""
        SELECT action_kind, status, COALESCE(last_error_code,''), LEFT(COALESCE(last_error_message,''),240), idempotency_key
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(prefix)}
           AND status IN ('failed','dead_letter')
         ORDER BY failed_at DESC, requested_at DESC
         LIMIT 20;
        """,
    ) if table_exists(target, "mmo_server_action_outbox") else ""
    failures = []
    for row in rows(failure_raw):
        row = row + [""] * 5
        failures.append({"action_kind": row[0], "status": row[1], "error_code": row[2], "error_message": row[3], "idempotency_key": row[4]})

    return {
        "session_key": session_key,
        "status_totals": totals,
        "response_kind_totals": response_kinds,
        "rows": rows_out,
        "failures": failures,
    }


def load_inventory_evidence_summary(target: Target, session_key: str) -> dict[str, object]:
    prefix = session_key + ":%"
    event_raw = run_mysql(
        target,
        f"""
        SELECT event_type, event_class, COUNT(*) AS c
          FROM world_event_journal
         WHERE idempotency_key LIKE {sql_literal(prefix)}
           AND event_class IN ('inventory','equipment','world_entity')
         GROUP BY event_type, event_class
         ORDER BY event_type, event_class;
        """,
    ) if table_exists(target, "world_event_journal") else ""
    audit_raw = run_mysql(
        target,
        f"""
        SELECT audit_type, COUNT(*) AS c
          FROM world_item_audit
         WHERE idempotency_key LIKE {sql_literal(prefix)}
         GROUP BY audit_type
         ORDER BY audit_type;
        """,
    ) if table_exists(target, "world_item_audit") else ""
    return {
        "events": [{"event_type": r[0], "event_class": r[1], "count": int(r[2] or 0)} for r in (row + [""] * 3 for row in rows(event_raw))],
        "world_item_audit": [{"audit_type": r[0], "count": int(r[1] or 0)} for r in (row + [""] * 2 for row in rows(audit_raw))],
    }


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Export a guarded MySQL inventory/equipment restore snapshot for PC_HERO_TEST.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--session-key", default=DEFAULT_SESSION_KEY)
    ap.add_argument("--character-key", default=DEFAULT_CHARACTER_KEY)
    ap.add_argument("--output", default=str(DEFAULT_OUTPUT))
    ap.add_argument("--require-ok", action="store_true", help="Exit non-zero when restore snapshot integrity checks fail.")
    args = ap.parse_args(argv)

    target = parse_mysql_url(args.url)
    template_table = item_template_table(target)
    required = require_tables(target, ("characters", "character_inventory", "character_equipment", "item_instances", template_table))
    missing = [name for name, ok in required.items() if not ok]
    if missing:
        raise SystemExit(f"missing required tables: {', '.join(missing)}")

    cols = {name: table_columns(target, name) for name in required}
    cols["_template_table_name"] = template_table
    character = load_character(target, args.character_key, cols["characters"])
    if character is None:
        raise SystemExit(f"character {args.character_key!r} was not found")

    inventory = load_inventory(target, args.character_key, cols)
    equipment = load_equipment(target, args.character_key, cols)
    integrity = validate_snapshot(inventory, equipment)

    snapshot = {
        "step": "63_guarded_inventory_equipment_restore_snapshot",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "session_key": args.session_key,
        "character_key": args.character_key,
        "source": {
            "kind": "mysql_current_projection",
            "database": target.database,
            "tables": required,
            "item_template_table": template_table,
            "client_restore_enabled": False,
            "client_restore_note": "This is a server truth snapshot artifact. Client consumption must remain behind an explicit server-bound restore flag in a later C++ step.",
        },
        "character": character,
        "inventory": inventory,
        "equipment": equipment,
        "integrity": integrity,
        "session_summary": load_session_action_summary(target, args.session_key),
        "inventory_evidence_summary": load_inventory_evidence_summary(target, args.session_key),
    }

    out = Path(args.output)
    if not out.is_absolute():
        out = ROOT / out
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(snapshot, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"status={'passed' if integrity['ok'] else 'failed'}")
    print(f"character_key={args.character_key}")
    print(f"inventory_count={integrity['inventory_count']}")
    print(f"equipment_count={integrity['equipment_count']}")
    print(f"output={out}")
    if args.require_ok and not integrity["ok"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

