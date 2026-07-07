#!/usr/bin/env python3
"""Apply the current MMO DB/server authority state to an existing MySQL DB.

The StepXX SQL files are kept as migration history. This tool is the user-facing
"state as of now" entrypoint: it applies the currently active additive SQL
surfaces in the order expected by the C++ UDP server path.

It intentionally does not drop or recreate the database. It also does not run
the Step53 read-model materialization wrapper, because that is a Python
materialization job rather than a plain SQL surface.
"""
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
from pathlib import Path as _MysqlCliPath
_MYSQL_CLI_TOOLS_DIR = _MysqlCliPath(__file__).resolve().parents[1]
if str(_MYSQL_CLI_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_MYSQL_CLI_TOOLS_DIR))
try:
    from _mysql_cli import resolve_mysql_exe
except Exception:  # pragma: no cover - fallback for standalone patch bundles.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


@dataclass(frozen=True)
class SqlSurface:
    key: str
    path: Path
    note: str


CURRENT_SQL_SURFACES = (
    SqlSurface("authority_gap", ROOT / "server" / "sql" / "step51_authority_gap_procedures.sql", "trigger/mover/world-time/resource/training/respawn/reaction procedures"),
    SqlSurface("live_receiver", ROOT / "server" / "sql" / "step55_live_receiver_bridge.sql", "server_sessions, outbox and live receiver procedures"),
    SqlSurface("progress_bridge", ROOT / "server" / "sql" / "step56b_clean_db_progress_bridge.sql", "dialog/quest/progress procedures"),
    SqlSurface("item_interactive_progress", ROOT / "server" / "sql" / "step59_clean_db_item_interactive_progress_bridge.sql", "pickup/remove/interactive/progression procedures"),
    SqlSurface("equipment", ROOT / "server" / "sql" / "step60_clean_db_equipment_bridge.sql", "equip/unequip/transfer procedures"),
    SqlSurface("interactive_use", ROOT / "server" / "sql" / "step67_interactive_use_bridge.sql", "interactive-use procedure"),
    SqlSurface("drop_loot", ROOT / "server" / "sql" / "step68_drop_loot_inventory_bridge.sql", "drop/loot inventory procedures"),
    SqlSurface("combat_lifecycle", ROOT / "server" / "sql" / "step83_combat_lifecycle_bridge.sql", "combat and NPC/world-entity lifecycle procedures"),
    SqlSurface("world_identity_lifecycle", ROOT / "server" / "sql" / "step84_world_identity_lifecycle_bridge.sql", "world identity/lifecycle fallback procedures"),
    SqlSurface("save_checkpoint_quest_utf8", ROOT / "server" / "sql" / "step93_save_checkpoint_quest_utf8_bridge.sql", "quest UTF-8/idempotency save-checkpoint procedure"),
    SqlSurface("server_save_checkpoint_manifest", ROOT / "server" / "sql" / "step94_server_save_checkpoint_manifest.sql", "durable server save/checkpoint manifest"),
    SqlSurface("save_slot_catalog_continue", ROOT / "server" / "sql" / "step95_save_slot_catalog_db_continue_bridge.sql", "DB-backed save-slot catalog and Continue metadata"),
    SqlSurface("db_save_checkpoint_snapshots", ROOT / "server" / "sql" / "step96_db_save_checkpoint_snapshots.sql", "DB-native checkpoint snapshot tables/procedures"),
    SqlSurface("world_clock_foundation_pre", ROOT / "server" / "sql" / "step108_db_checkpoint_world_clock_foundation.sql", "world-clock foundation before checkpoint export coverage"),
    SqlSurface("checkpoint_export_coverage", ROOT / "server" / "sql" / "step103_db_checkpoint_export_coverage.sql", "checkpoint export coverage/world-clock fallback"),
    SqlSurface("script_state_full_export", ROOT / "server" / "sql" / "step104_db_checkpoint_script_state_full_export.sql", "full script-state checkpoint export"),
    SqlSurface("world_clock_foundation_finalize", ROOT / "server" / "sql" / "step108_db_checkpoint_world_clock_foundation.sql", "world-clock foundation final pass after script-state export"),
    SqlSurface("npc_authority_restore", ROOT / "server" / "sql" / "step120_npc_authority_restore_bridge.sql", "NPC routine/AI/path/fight current/history and recorder procedures"),
    SqlSurface("server_parity_state", ROOT / "server" / "sql" / "step121_server_parity_state_bridge.sql", "trigger queue, world transition and client correction current/history"),
    SqlSurface("server_content_pack_manifest", ROOT / "server" / "sql" / "step188_server_content_pack_manifest.sql", "server-owned content pack file manifest and client hash validation"),
    SqlSurface("server_content_pack_inventory", ROOT / "server" / "sql" / "step201_server_content_pack_inventory.sql", "server-owned content pack file roles for future ZEN/DAT/OU importers"),
    SqlSurface("server_content_archive_mounts", ROOT / "server" / "sql" / "step203_server_content_archive_mounts.sql", "server-owned VDF/MOD archive mount and pre-extract registry"),
    SqlSurface("server_content_import_jobs", ROOT / "server" / "sql" / "step206_server_content_import_jobs.sql", "server-owned content import job queue for future ZEN/DAT/OU importers"),
    SqlSurface("server_content_pack_session_gate", ROOT / "server" / "sql" / "step189_server_content_pack_session_gate.sql", "session-scoped server content pack validation gate"),
    SqlSurface("content_manifest_reject_audit", ROOT / "server" / "sql" / "step198_content_manifest_reject_audit.sql", "DB audit table/procedure/view for content manifest bootstrap rejects"),
    SqlSurface("content_manifest_reject_health_views", ROOT / "server" / "sql" / "step199_content_manifest_reject_health_views.sql", "aggregate health views for content manifest reject audits"),
)


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    db = (p.path or "/").lstrip("/")
    if not db:
        raise ValueError("database missing in mysql URL")
    return Target(
        host=p.hostname or "localhost",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=db,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
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


def redact_cmd(cmd: list[str]) -> list[str]:
    return ["-p***" if part.startswith("-p") and len(part) > 2 else part for part in cmd]


def run_mysql_file(target: Target, path: Path, *, dry_run: bool) -> dict[str, object]:
    shown = redact_cmd(mysql_cmd(target))
    print("[SQL] " + rel(path))
    print("[RUN] " + " ".join(shown))
    if dry_run:
        return {"cmd": shown, "returncode": 0, "dry_run": True, "stdout": "", "stderr": ""}
    proc = subprocess.run(mysql_cmd(target), input=path.read_text(encoding="utf-8"), text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(ROOT))
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    return {"cmd": shown, "returncode": proc.returncode, "dry_run": False, "stdout": proc.stdout, "stderr": proc.stderr}


def selected_surfaces(skip: set[str]) -> list[SqlSurface]:
    return [surface for surface in CURRENT_SQL_SURFACES if surface.key not in skip]


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply current MMO DB/server authority state to an existing MySQL database.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--output", default="")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list", action="store_true", help="Print the current SQL surface order and exit.")
    parser.add_argument("--skip", action="append", default=[], help="Skip a current SQL surface key. Repeatable; use --list to see keys.")
    args = parser.parse_args()

    skip = set(args.skip or [])
    surfaces = selected_surfaces(skip)
    if args.list:
        for surface in surfaces:
            print(f"{surface.key}\t{rel(surface.path)}\t{surface.note}")
        return 0

    target = parse_mysql_url(args.url)
    manifest: dict[str, object] = {
        "tool": "apply_current_mmo_db_state.py",
        "status": "running",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "surfaces": [],
    }

    missing = [surface for surface in surfaces if not surface.path.exists()]
    if missing:
        manifest["status"] = "failed_missing_sql"
        manifest["missing"] = [{"key": item.key, "path": rel(item.path)} for item in missing]
    else:
        for surface in surfaces:
            result = run_mysql_file(target, surface.path, dry_run=args.dry_run)
            entry = {
                "key": surface.key,
                "path": rel(surface.path),
                "note": surface.note,
                "status": "applied" if result["returncode"] == 0 else "failed",
                "result": result,
            }
            manifest["surfaces"].append(entry)
            if entry["status"] == "failed":
                manifest["status"] = "failed"
                break
        else:
            manifest["status"] = "passed"

    manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        out = Path(args.output)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={rel(out)}")
    print("status=" + str(manifest["status"]))
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())






