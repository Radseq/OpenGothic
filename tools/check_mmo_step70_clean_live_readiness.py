#!/usr/bin/env python3
"""Check and prepare the clean PC_HERO_TEST live loop after a DB rebuild."""
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
DEFAULT_SQLITE = ROOT / "runtime" / "g2notr_ch1_pre_xardas.sqlite"
DEFAULT_OUTPUT = ROOT / "runtime" / "pc_hero_test_live" / "clean_live_readiness.json"
DEFAULT_SESSION_KEY = "local-dev-PC_HERO_TEST"
DEFAULT_CHARACTER_KEY = "PC_HERO"

REQUIRED_TABLES = [
    "server_sessions",
    "mmo_server_action_outbox",
    "mmo_server_action_worker_runs",
    "mmo_server_action_worker_results",
    "mmo_server_character_read_model",
    "mmo_server_character_inventory_read_model",
    "mmo_server_world_entity_read_model",
    "mmo_server_world_inventory_read_model",
    "characters",
    "character_inventory",
    "character_equipment",
    "item_instances",
    "content_item_templates",
    "world_event_journal",
    "world_item_audit",
]

REQUIRED_ROUTINES = [
    "mmo_login_character",
    "mmo_enqueue_server_action",
    "mmo_claim_next_server_action",
    "mmo_mark_server_action_applied",
    "mmo_mark_server_action_failed",
    "mmo_checkpoint_character_state",
    "mmo_set_character_script_int",
    "mmo_update_character_quest",
    "mmo_set_character_known_dialog",
    "mmo_pickup_world_item",
    "mmo_remove_world_item",
    "mmo_equip_character_item",
    "mmo_unequip_character_item",
    "mmo_record_interactive_use",
    "mmo_drop_character_item",
    "mmo_loot_npc_inventory",
]


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
        raise ValueError("database name is missing in MySQL URL")
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


def existing_tables(target: Target) -> set[str]:
    names = ",".join(sql_literal(name) for name in REQUIRED_TABLES)
    raw = run_mysql(
        target,
        f"""
        SELECT table_name
          FROM information_schema.TABLES
         WHERE table_schema = DATABASE()
           AND table_type = 'BASE TABLE'
           AND table_name IN ({names})
         ORDER BY table_name;
        """,
    )
    return {line.strip() for line in raw.splitlines() if line.strip()}


def existing_routines(target: Target) -> set[str]:
    names = ",".join(sql_literal(name) for name in REQUIRED_ROUTINES)
    raw = run_mysql(
        target,
        f"""
        SELECT routine_name
          FROM information_schema.ROUTINES
         WHERE routine_schema = DATABASE()
           AND routine_name IN ({names})
         ORDER BY routine_name;
        """,
    )
    return {line.strip() for line in raw.splitlines() if line.strip()}


def character_summary(target: Target, character_key: str) -> dict[str, object]:
    raw = run_mysql(
        target,
        f"""
        SELECT BIN_TO_UUID(c.character_id,1),
               c.character_key,
               c.character_name,
               COALESCE(ci.inventory_count,0),
               COALESCE(ce.equipment_count,0)
          FROM characters c
          LEFT JOIN (
            SELECT character_id, COUNT(*) AS inventory_count
              FROM character_inventory
             GROUP BY character_id
          ) ci ON ci.character_id = c.character_id
          LEFT JOIN (
            SELECT character_id, COUNT(*) AS equipment_count
              FROM character_equipment
             GROUP BY character_id
          ) ce ON ce.character_id = c.character_id
         WHERE c.character_key = {sql_literal(character_key)}
         LIMIT 1;
        """,
    )
    data = rows(raw)
    if not data:
        return {"found": False, "character_key": character_key}
    row = data[0] + [""] * 5
    return {
        "found": True,
        "character_uuid": row[0],
        "character_key": row[1],
        "character_name": row[2],
        "inventory_count": int(row[3] or 0),
        "equipment_count": int(row[4] or 0),
    }


def outbox_summary(target: Target, session_key: str) -> dict[str, object]:
    raw = run_mysql(
        target,
        f"""
        SELECT status, COUNT(*) AS c
          FROM mmo_server_action_outbox
         WHERE idempotency_key LIKE {sql_literal(session_key + ':%')}
         GROUP BY status
         ORDER BY status;
        """,
    )
    status_counts: dict[str, int] = {}
    for row in rows(raw):
        row = row + [""] * 2
        status_counts[row[0]] = int(row[1] or 0)
    return {
        "session_key": session_key,
        "status_counts": status_counts,
        "pendingish": sum(status_counts.get(k, 0) for k in ("pending", "claimed", "failed", "dead_letter")),
        "total": sum(status_counts.values()),
    }


def collation_summary(target: Target) -> dict[str, object]:
    raw_count = run_mysql(
        target,
        """
        SELECT COUNT(*)
          FROM information_schema.TABLES
         WHERE table_schema = DATABASE()
           AND table_type = 'BASE TABLE'
           AND table_collation IS NOT NULL
           AND table_collation <> 'utf8mb4_0900_ai_ci';
        """,
    )
    mismatch_count = int((raw_count.splitlines()[-1] if raw_count else "0") or 0)
    raw = run_mysql(
        target,
        """
        SELECT table_name, table_collation
          FROM information_schema.TABLES
         WHERE table_schema = DATABASE()
           AND table_type = 'BASE TABLE'
           AND table_collation IS NOT NULL
           AND table_collation <> 'utf8mb4_0900_ai_ci'
         ORDER BY table_name
         LIMIT 50;
        """,
    )
    mismatches = [{"table": r[0], "collation": r[1]} for r in (row + [""] * 2 for row in rows(raw))]
    return {
        "ok": mismatch_count == 0,
        "required_collation": "utf8mb4_0900_ai_ci",
        "mismatch_count": mismatch_count,
        "mismatches_sample": mismatches,
    }


def prepare_runtime_dirs(session_key: str) -> dict[str, str]:
    receiver_dir = ROOT / "runtime" / "pc_hero_test_live"
    worker_dir = ROOT / "runtime" / "live_resolved_worker" / session_key
    baseline_dir = ROOT / "runtime" / "baselines"
    for path in (receiver_dir, worker_dir, baseline_dir):
        path.mkdir(parents=True, exist_ok=True)
    return {
        "receiver_dir": str(receiver_dir),
        "worker_dir": str(worker_dir),
        "baseline_dir": str(baseline_dir),
    }


def command_plan(mysql_url: str, session_key: str) -> dict[str, list[str]]:
    gothic_path = '"/mnt/windows-games/Games/Steam/steamapps/common/Gothic II/"'
    return {
        "receiver": [
            "python3 tools/run_mmo_pc_hero_test_receiver.py",
            f'  --url "{mysql_url}"',
        ],
        "game": [
            "./build/opengothic/Gothic2Notr",
            f"  -g {gothic_path}",
            "  -g2",
            "  -mmo-client-server 127.0.0.1:29777",
            "  -mmo-action-jsonl runtime/pc_hero_test_live/client_actions.jsonl",
            f"  -mmo-action-session-key {session_key}",
            "  -mmo-action-queue-capacity 8192",
        ],
        "worker": [
            "python3 tools/run_mmo_live_resolved_worker.py",
            f'  --url "{mysql_url}"',
            "  --max-actions 500",
            "  --reset-matching-failed",
        ],
        "roundtrip_check_after_actions": [
            "python3 tools/check_mmo_step69_pc_hero_test_inventory_roundtrip.py",
            f'  --url "{mysql_url}"',
            f"  --session-key {session_key}",
            "  --character-key PC_HERO",
            f"  --snapshot runtime/live_resolved_worker/{session_key}/mysql_restore_snapshot.json",
            f"  --output runtime/live_resolved_worker/{session_key}/inventory_roundtrip_check_manual.json",
        ],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Check clean MySQL + runtime readiness for the PC_HERO_TEST live loop.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--sqlite", default=str(DEFAULT_SQLITE))
    ap.add_argument("--session-key", default=DEFAULT_SESSION_KEY)
    ap.add_argument("--character-key", default=DEFAULT_CHARACTER_KEY)
    ap.add_argument("--prepare-runtime", action="store_true", help="Create runtime directories used by the stable receiver/worker profiles.")
    ap.add_argument("--strict-collation", action="store_true", help="Treat table collation mismatches as a readiness failure instead of a warning.")
    ap.add_argument("--output", default=str(DEFAULT_OUTPUT))
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    sqlite_path = Path(args.sqlite)
    if not sqlite_path.is_absolute():
        sqlite_path = ROOT / sqlite_path

    result: dict[str, object] = {
        "step": "70_clean_live_readiness",
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "session_key": args.session_key,
        "character_key": args.character_key,
        "sqlite": {"path": str(sqlite_path), "exists": sqlite_path.exists()},
        "runtime_dirs": None,
        "commands": command_plan(args.url, args.session_key),
        "strict_collation": args.strict_collation,
    }

    try:
        present_tables = existing_tables(target)
        present_routines = existing_routines(target)
        result["tables"] = {"missing": [name for name in REQUIRED_TABLES if name not in present_tables], "present_count": len(present_tables)}
        result["routines"] = {"missing": [name for name in REQUIRED_ROUTINES if name not in present_routines], "present_count": len(present_routines)}
        result["character"] = character_summary(target, args.character_key)
        result["outbox"] = outbox_summary(target, args.session_key)
        result["collation"] = collation_summary(target)
        if args.prepare_runtime:
            result["runtime_dirs"] = prepare_runtime_dirs(args.session_key)

        errors = []
        warnings = []
        if not result["sqlite"]["exists"]:
            errors.append("missing_sqlite_capture")
        if result["tables"]["missing"]:
            errors.append("missing_tables")
        if result["routines"]["missing"]:
            errors.append("missing_routines")
        if not result["character"].get("found"):
            errors.append("missing_character")
        if not result["collation"]["ok"]:
            if args.strict_collation:
                errors.append("collation_mismatch")
            else:
                warnings.append("collation_mismatch")
        result["errors"] = errors
        result["warnings"] = warnings
        result["ready_for_live_loop"] = not errors
        result["status"] = "passed" if not errors else "failed"
    except Exception as exc:  # noqa: BLE001 - produce a readable artifact
        result["status"] = "failed"
        result["ready_for_live_loop"] = False
        result["errors"] = ["checker_exception"]
        result["exception"] = {"type": type(exc).__name__, "message": str(exc)}

    out = Path(args.output)
    if not out.is_absolute():
        out = ROOT / out
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("status=" + str(result["status"]))
    print("ready_for_live_loop=" + str(result.get("ready_for_live_loop", False)).lower())
    print(f"output={out}")
    if result.get("errors"):
        print("errors=" + ",".join(str(x) for x in result["errors"]))
    if result.get("warnings"):
        print("warnings=" + ",".join(str(x) for x in result["warnings"]))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())

