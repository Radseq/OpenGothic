#!/usr/bin/env python3
"""Rebuild a local MySQL dev database from the Step54 Chapter 1 SQLite baseline.

This is intentionally destructive and requires an explicit confirmation flag.
It creates a clean dev DB, applies the existing production/bridge migrations, imports
from the captured SQLite baseline, then optionally reapplies Step51 and Step53 helper
surfaces if those files/tools exist in the checkout. It also applies the
Step56b clean-DB progress bridge, the Step59 item/interactive/progress bridge,
the Step60 equipment bridge, the Step67 interactive-use bridge, the Step68
drop/loot bridge, the Step83 combat/lifecycle bridge, the Step84 world identity/lifecycle bridge, and normalizes MySQL
collations so destructive rebuilds do not reintroduce the live-worker failures found
during live tests.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse, urlunparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE = ROOT / "runtime" / "baselines" / "g2notr_chapter1_before_xardas.sqlite"
DEFAULT_OUTPUT_DIR = ROOT / "runtime" / "step54_mysql_reset"

BASE_MIGRATIONS = (
    ROOT / "db" / "migrations" / "mysql" / "production" / "001_gothic_mmo_production_schema.sql",
    ROOT / "db" / "migrations" / "mysql" / "production" / "002_bootstrap_import_pipeline.sql",
)
STEP51_SQL = ROOT / "server" / "sql" / "step51_authority_gap_procedures.sql"
STEP53_SQL = ROOT / "server" / "sql" / "step53_server_read_model_v1.sql"
STEP55_LIVE_BRIDGE_SQL = ROOT / "server" / "sql" / "step55_live_receiver_bridge.sql"
STEP56B_PROGRESS_BRIDGE_SQL = ROOT / "server" / "sql" / "step56b_clean_db_progress_bridge.sql"
STEP59_ITEM_INTERACTIVE_PROGRESS_BRIDGE_SQL = ROOT / "server" / "sql" / "step59_clean_db_item_interactive_progress_bridge.sql"
STEP60_EQUIPMENT_BRIDGE_SQL = ROOT / "server" / "sql" / "step60_clean_db_equipment_bridge.sql"
STEP67_INTERACTIVE_USE_BRIDGE_SQL = ROOT / "server" / "sql" / "step67_interactive_use_bridge.sql"
STEP68_DROP_LOOT_BRIDGE_SQL = ROOT / "server" / "sql" / "step68_drop_loot_inventory_bridge.sql"
STEP83_COMBAT_LIFECYCLE_BRIDGE_SQL = ROOT / "server" / "sql" / "step83_combat_lifecycle_bridge.sql"
STEP84_WORLD_IDENTITY_LIFECYCLE_BRIDGE_SQL = ROOT / "server" / "sql" / "step84_world_identity_lifecycle_bridge.sql"
STEP93_SAVE_CHECKPOINT_QUEST_UTF8_BRIDGE_SQL = ROOT / "server" / "sql" / "step93_save_checkpoint_quest_utf8_bridge.sql"
STEP94_SERVER_SAVE_CHECKPOINT_MANIFEST_SQL = ROOT / "server" / "sql" / "step94_server_save_checkpoint_manifest.sql"
STEP95_SAVE_SLOT_CATALOG_DB_CONTINUE_BRIDGE_SQL = ROOT / "server" / "sql" / "step95_save_slot_catalog_db_continue_bridge.sql"
STEP96_DB_SAVE_CHECKPOINT_SNAPSHOTS_SQL = ROOT / "server" / "sql" / "step96_db_save_checkpoint_snapshots.sql"
STEP97_DB_SAVE_CHECKPOINT_RESTORE_BRIDGE_SQL = ROOT / "server" / "sql" / "step97_db_save_checkpoint_restore_bridge.sql"
STEP98_STRICT_DB_CONTINUE_RESTORE_SQL = ROOT / "server" / "sql" / "step98_strict_db_continue_restore.sql"
STEP103_DB_CHECKPOINT_EXPORT_COVERAGE_SQL = ROOT / "server" / "sql" / "step103_db_checkpoint_export_coverage.sql"
STEP104_DB_CHECKPOINT_SCRIPT_STATE_FULL_EXPORT_SQL = ROOT / "server" / "sql" / "step104_db_checkpoint_script_state_full_export.sql"
STEP108_DB_CHECKPOINT_WORLD_CLOCK_FOUNDATION_SQL = ROOT / "server" / "sql" / "step108_db_checkpoint_world_clock_foundation.sql"


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


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
        host=p.hostname or "127.0.0.1",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=db,
    )


def mysql_url_for_database(url: str, database: str) -> str:
    p = urlparse(url)
    return urlunparse((p.scheme, p.netloc, "/" + database, "", "", ""))


def mysql_cmd(target: Target, *, include_db: bool) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
        "-h", target.host,
        "-P", str(target.port),
        "-u", target.user,
    ]
    if target.password:
        cmd.append(f"-p{target.password}")
    if include_db:
        cmd.append(target.database)
    return cmd


def quote_ident(name: str) -> str:
    return "`" + name.replace("`", "``") + "`"


def run(cmd: list[str], *, input_text: str | None = None, dry_run: bool = False, cwd: Path = ROOT) -> dict[str, object]:
    printable = " ".join(cmd)
    print(f"[RUN] {printable}")
    if dry_run:
        return {"cmd": cmd, "returncode": 0, "dry_run": True, "stdout": "", "stderr": ""}
    proc = subprocess.run(cmd, input=input_text, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(cwd))
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    return {"cmd": cmd, "returncode": proc.returncode, "dry_run": False, "stdout": proc.stdout, "stderr": proc.stderr}


def apply_sql(target: Target, path: Path, *, dry_run: bool) -> dict[str, object]:
    if not path.exists():
        return {"path": rel(path), "status": "missing_skipped"}
    result = run(mysql_cmd(target, include_db=True), input_text=path.read_text(encoding="utf-8"), dry_run=dry_run)
    status = "applied" if result["returncode"] == 0 else "failed"
    return {"path": rel(path), "status": status, "result": result}


def main() -> int:
    ap = argparse.ArgumentParser(description="Reset local MySQL dev DB from Step54 Chapter 1 SQLite baseline.")
    ap.add_argument("--mysql-url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    ap.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    ap.add_argument("--realm-key", default="local-dev")
    ap.add_argument("--realm-display-name", default="Local Dev Realm")
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--with-step51", action=argparse.BooleanOptionalAction, default=True)
    ap.add_argument("--with-step53", action=argparse.BooleanOptionalAction, default=True)
    ap.add_argument("--with-step55-live-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install minimal server_sessions/outbox/worker procedures needed by live receiver")
    ap.add_argument("--with-step56b-progress-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install progress/dialog/quest procedures required by resolved live worker")
    ap.add_argument("--with-step59-item-interactive-progress-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install item pickup/removal, interactive state and progression procedures required by resolved live worker")
    ap.add_argument("--with-step60-equipment-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install equip/unequip/transfer procedures required by resolved live worker")
    ap.add_argument("--with-step67-interactive-use-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install interactive-use procedure required by resolved live worker")
    ap.add_argument("--with-step68-drop-loot-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install drop/loot inventory procedures required by resolved live worker")
    ap.add_argument("--with-step83-combat-lifecycle-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install combat/lifecycle procedures required by direct C++ server combat handlers")
    ap.add_argument("--with-step84-world-identity-lifecycle-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install world identity/lifecycle procedures required by direct C++ server fallback handlers")
    ap.add_argument("--with-step93-save-checkpoint-quest-utf8-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install quest UTF-8/idempotency procedure and audit table for server-bound save/checkpoint flow")
    ap.add_argument("--with-step94-server-save-checkpoint-manifest", action=argparse.BooleanOptionalAction, default=True, help="Install durable server save/checkpoint manifest table and procedure")
    ap.add_argument("--with-step95-save-slot-catalog-db-continue-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install DB-backed save-slot catalog and .sav-free continue bridge metadata")
    ap.add_argument("--with-step96-db-save-checkpoint-snapshots", action=argparse.BooleanOptionalAction, default=True, help="Install normalized DB-native save checkpoint snapshot tables and procedures")
    ap.add_argument("--with-step97-db-save-checkpoint-restore-bridge", action=argparse.BooleanOptionalAction, default=True, help="Install DB-save-checkpoint restore/export bridge used by bootstrap snapshots")
    ap.add_argument("--with-step98-strict-db-continue-restore", action=argparse.BooleanOptionalAction, default=True, help="Install strict DB-native Continue/restore validation bridge")
    ap.add_argument("--with-step103-db-checkpoint-export-coverage", action=argparse.BooleanOptionalAction, default=True, help="Install DB checkpoint export coverage/world-clock fallback bridge")
    ap.add_argument("--with-step104-db-checkpoint-script-state-full-export", action=argparse.BooleanOptionalAction, default=True, help="Install DB checkpoint full script-state export bridge")
    ap.add_argument("--normalize-collation", action=argparse.BooleanOptionalAction, default=True, help="Normalize base table collations to utf8mb4_0900_ai_ci after optional SQL surfaces are installed")
    ap.add_argument("--activate-content", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--i-understand-this-drops-database", action="store_true")
    args = ap.parse_args()

    step104_procedure_export_enabled = args.with_step104_db_checkpoint_script_state_full_export
    legacy_step97_restore_enabled = args.with_step97_db_save_checkpoint_restore_bridge and not step104_procedure_export_enabled
    legacy_step98_strict_enabled = args.with_step98_strict_db_continue_restore and not step104_procedure_export_enabled

    target = parse_mysql_url(args.mysql_url)
    baseline = args.baseline if args.baseline.is_absolute() else ROOT / args.baseline
    output_dir = args.output_dir if args.output_dir.is_absolute() else ROOT / args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    if not baseline.exists():
        print(f"ERROR: baseline SQLite does not exist: {baseline}", file=sys.stderr)
        return 1
    if not args.dry_run and not args.i_understand_this_drops_database:
        print("ERROR: this drops/recreates the target database; pass --i-understand-this-drops-database", file=sys.stderr)
        return 1

    manifest: dict[str, object] = {
        "step": 54,
        "status": "running",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "baseline": rel(baseline),
        "commands": [],
        "applied_sql": [],
        "compatibility": {
            "step104_procedure_export_enabled": step104_procedure_export_enabled,
            "legacy_step97_restore_enabled": legacy_step97_restore_enabled,
            "legacy_step98_strict_enabled": legacy_step98_strict_enabled,
        },
    }

    create_sql = f"DROP DATABASE IF EXISTS {quote_ident(target.database)};\nCREATE DATABASE {quote_ident(target.database)} CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;\n"
    reset_result = run(mysql_cmd(target, include_db=False), input_text=create_sql, dry_run=args.dry_run)
    manifest["commands"].append(reset_result)
    if reset_result["returncode"] != 0:
        manifest["status"] = "failed_drop_create"
    else:
        for migration in BASE_MIGRATIONS:
            entry = apply_sql(target, migration, dry_run=args.dry_run)
            manifest["applied_sql"].append(entry)
            if entry["status"] == "failed":
                manifest["status"] = "failed_base_migration"
                break
        else:
            import_cmd = [
                sys.executable,
                str(ROOT / "tools" / "import_runtime_sqlite_to_mysql.py"),
                "--sqlite", str(baseline),
                "--mysql-url", mysql_url_for_database(args.mysql_url, target.database),
                "--realm-key", args.realm_key,
                "--realm-display-name", args.realm_display_name,
                "--account-name", args.account_name,
                "--character-key", args.character_key,
            ]
            if args.activate_content:
                import_cmd.append("--activate-content")
            import_result = run(import_cmd, dry_run=args.dry_run)
            manifest["commands"].append(import_result)
            if import_result["returncode"] != 0:
                manifest["status"] = "failed_import"
            else:
                if args.with_step51:
                    step51 = apply_sql(target, STEP51_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step51)
                    if step51["status"] == "failed":
                        manifest["status"] = "failed_step51"
                if manifest["status"] == "running" and args.with_step53:
                    # Create/materialize Step53 read models through its wrapper when available; otherwise apply SQL only.
                    step53_wrapper = ROOT / "tools" / "run_mmo_step53_server_materialization_followup.py"
                    if step53_wrapper.exists():
                        step53_cmd = [
                            sys.executable,
                            str(step53_wrapper),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                            "--output-dir", str(output_dir / "step53_materialization"),
                            "--limit", "80",
                            "--sample-limit", "25",
                        ]
                        step53_result = run(step53_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(step53_result)
                        if step53_result["returncode"] != 0:
                            manifest["status"] = "failed_step53_materialization"
                    else:
                        step53 = apply_sql(target, STEP53_SQL, dry_run=args.dry_run)
                        manifest["applied_sql"].append(step53)
                        if step53["status"] == "failed":
                            manifest["status"] = "failed_step53_sql"
                if manifest["status"] == "running" and args.with_step55_live_bridge:
                    step55_live_bridge = apply_sql(target, STEP55_LIVE_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step55_live_bridge)
                    if step55_live_bridge["status"] == "failed":
                        manifest["status"] = "failed_step55_live_bridge"
                    elif step55_live_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step55_live_bridge_sql"

                if manifest["status"] == "running" and args.with_step56b_progress_bridge:
                    step56b_progress_bridge = apply_sql(target, STEP56B_PROGRESS_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step56b_progress_bridge)
                    if step56b_progress_bridge["status"] == "failed":
                        manifest["status"] = "failed_step56b_progress_bridge"
                    elif step56b_progress_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step56b_progress_bridge_sql"

                if manifest["status"] == "running" and args.with_step59_item_interactive_progress_bridge:
                    step59_bridge = apply_sql(target, STEP59_ITEM_INTERACTIVE_PROGRESS_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step59_bridge)
                    if step59_bridge["status"] == "failed":
                        manifest["status"] = "failed_step59_item_interactive_progress_bridge"
                    elif step59_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step59_item_interactive_progress_bridge_sql"

                if manifest["status"] == "running" and args.with_step60_equipment_bridge:
                    step60_bridge = apply_sql(target, STEP60_EQUIPMENT_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step60_bridge)
                    if step60_bridge["status"] == "failed":
                        manifest["status"] = "failed_step60_equipment_bridge"
                    elif step60_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step60_equipment_bridge_sql"

                if manifest["status"] == "running" and args.with_step67_interactive_use_bridge:
                    step67_bridge = apply_sql(target, STEP67_INTERACTIVE_USE_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step67_bridge)
                    if step67_bridge["status"] == "failed":
                        manifest["status"] = "failed_step67_interactive_use_bridge"
                    elif step67_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step67_interactive_use_bridge_sql"

                if manifest["status"] == "running" and args.with_step68_drop_loot_bridge:
                    step68_bridge = apply_sql(target, STEP68_DROP_LOOT_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step68_bridge)
                    if step68_bridge["status"] == "failed":
                        manifest["status"] = "failed_step68_drop_loot_bridge"
                    elif step68_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step68_drop_loot_bridge_sql"

                if manifest["status"] == "running" and args.with_step83_combat_lifecycle_bridge:
                    step83_bridge = apply_sql(target, STEP83_COMBAT_LIFECYCLE_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step83_bridge)
                    if step83_bridge["status"] == "failed":
                        manifest["status"] = "failed_step83_combat_lifecycle_bridge"
                    elif step83_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step83_combat_lifecycle_bridge_sql"

                if manifest["status"] == "running" and args.with_step84_world_identity_lifecycle_bridge:
                    step84_bridge = apply_sql(target, STEP84_WORLD_IDENTITY_LIFECYCLE_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step84_bridge)
                    if step84_bridge["status"] == "failed":
                        manifest["status"] = "failed_step84_world_identity_lifecycle_bridge"
                    elif step84_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step84_world_identity_lifecycle_bridge_sql"

                if manifest["status"] == "running" and args.with_step93_save_checkpoint_quest_utf8_bridge:
                    step93_bridge = apply_sql(target, STEP93_SAVE_CHECKPOINT_QUEST_UTF8_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step93_bridge)
                    if step93_bridge["status"] == "failed":
                        manifest["status"] = "failed_step93_save_checkpoint_quest_utf8_bridge"
                    elif step93_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step93_save_checkpoint_quest_utf8_bridge_sql"

                if manifest["status"] == "running" and args.with_step94_server_save_checkpoint_manifest:
                    step94_manifest = apply_sql(target, STEP94_SERVER_SAVE_CHECKPOINT_MANIFEST_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step94_manifest)
                    if step94_manifest["status"] == "failed":
                        manifest["status"] = "failed_step94_server_save_checkpoint_manifest"
                    elif step94_manifest["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step94_server_save_checkpoint_manifest_sql"

                if manifest["status"] == "running" and args.with_step95_save_slot_catalog_db_continue_bridge:
                    step95_bridge = apply_sql(target, STEP95_SAVE_SLOT_CATALOG_DB_CONTINUE_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step95_bridge)
                    if step95_bridge["status"] == "failed":
                        manifest["status"] = "failed_step95_save_slot_catalog_db_continue_bridge"
                    elif step95_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step95_save_slot_catalog_db_continue_bridge_sql"

                if manifest["status"] == "running" and args.with_step96_db_save_checkpoint_snapshots:
                    step96_bridge = apply_sql(target, STEP96_DB_SAVE_CHECKPOINT_SNAPSHOTS_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step96_bridge)
                    if step96_bridge["status"] == "failed":
                        manifest["status"] = "failed_step96_db_save_checkpoint_snapshots"
                    elif step96_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step96_db_save_checkpoint_snapshots_sql"

                if manifest["status"] == "running" and step104_procedure_export_enabled:
                    step108_foundation = apply_sql(target, STEP108_DB_CHECKPOINT_WORLD_CLOCK_FOUNDATION_SQL, dry_run=args.dry_run)
                    step108_foundation["phase"] = "before_step103_step104"
                    manifest["applied_sql"].append(step108_foundation)
                    if step108_foundation["status"] == "failed":
                        manifest["status"] = "failed_step108_db_checkpoint_world_clock_foundation"
                    elif step108_foundation["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step108_db_checkpoint_world_clock_foundation_sql"

                if manifest["status"] == "running" and args.with_step97_db_save_checkpoint_restore_bridge and not legacy_step97_restore_enabled:
                    manifest["applied_sql"].append({
                        "path": rel(STEP97_DB_SAVE_CHECKPOINT_RESTORE_BRIDGE_SQL),
                        "status": "skipped_replaced_by_step104_procedure_export",
                    })

                if manifest["status"] == "running" and legacy_step97_restore_enabled:
                    step97_bridge = apply_sql(target, STEP97_DB_SAVE_CHECKPOINT_RESTORE_BRIDGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step97_bridge)
                    if step97_bridge["status"] == "failed":
                        manifest["status"] = "failed_step97_db_save_checkpoint_restore_bridge"
                    elif step97_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step97_db_save_checkpoint_restore_bridge_sql"

                if manifest["status"] == "running" and args.with_step98_strict_db_continue_restore and not legacy_step98_strict_enabled:
                    manifest["applied_sql"].append({
                        "path": rel(STEP98_STRICT_DB_CONTINUE_RESTORE_SQL),
                        "status": "skipped_replaced_by_step104_procedure_export",
                    })

                if manifest["status"] == "running" and legacy_step98_strict_enabled:
                    step98_bridge = apply_sql(target, STEP98_STRICT_DB_CONTINUE_RESTORE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step98_bridge)
                    if step98_bridge["status"] == "failed":
                        manifest["status"] = "failed_step98_strict_db_continue_restore"
                    elif step98_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step98_strict_db_continue_restore_sql"

                if manifest["status"] == "running" and args.with_step103_db_checkpoint_export_coverage:
                    step103_bridge = apply_sql(target, STEP103_DB_CHECKPOINT_EXPORT_COVERAGE_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step103_bridge)
                    if step103_bridge["status"] == "failed":
                        manifest["status"] = "failed_step103_db_checkpoint_export_coverage"
                    elif step103_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step103_db_checkpoint_export_coverage_sql"

                if manifest["status"] == "running" and args.with_step104_db_checkpoint_script_state_full_export:
                    step104_bridge = apply_sql(target, STEP104_DB_CHECKPOINT_SCRIPT_STATE_FULL_EXPORT_SQL, dry_run=args.dry_run)
                    manifest["applied_sql"].append(step104_bridge)
                    if step104_bridge["status"] == "failed":
                        manifest["status"] = "failed_step104_db_checkpoint_script_state_full_export"
                    elif step104_bridge["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step104_db_checkpoint_script_state_full_export_sql"

                if manifest["status"] == "running" and step104_procedure_export_enabled:
                    step108_finalize = apply_sql(target, STEP108_DB_CHECKPOINT_WORLD_CLOCK_FOUNDATION_SQL, dry_run=args.dry_run)
                    step108_finalize["phase"] = "after_step104"
                    manifest["applied_sql"].append(step108_finalize)
                    if step108_finalize["status"] == "failed":
                        manifest["status"] = "failed_step108_db_checkpoint_world_clock_foundation_finalize"
                    elif step108_finalize["status"] == "missing_skipped":
                        manifest["status"] = "failed_missing_step108_db_checkpoint_world_clock_foundation_sql"

                if manifest["status"] == "running" and args.normalize_collation:
                    normalize_tool = ROOT / "tools" / "normalize_mmo_mysql_collation.py"
                    if normalize_tool.exists():
                        normalize_cmd = [
                            sys.executable,
                            str(normalize_tool),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                            "--output", str(output_dir / "normalize_collation.json"),
                        ]
                        if args.dry_run:
                            normalize_cmd.append("--dry-run")
                        normalize_result = run(normalize_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(normalize_result)
                        if normalize_result["returncode"] != 0:
                            manifest["status"] = "failed_collation_normalize"
                    else:
                        manifest["status"] = "failed_missing_collation_normalizer"

                if manifest["status"] == "running":
                    check_cmd = [
                        sys.executable,
                        str(ROOT / "tools" / "check_mysql_bootstrap_import.py"),
                        "--url", mysql_url_for_database(args.mysql_url, target.database),
                        "--realm-key", args.realm_key,
                        "--character-key", args.character_key,
                    ]
                    check_result = run(check_cmd, dry_run=args.dry_run)
                    manifest["commands"].append(check_result)
                    manifest["status"] = "passed" if check_result["returncode"] == 0 else "failed_bootstrap_check"

                if manifest["status"] == "passed" and args.with_step55_live_bridge:
                    live_bridge_check = ROOT / "tools" / "check_mmo_step55_live_receiver_bridge.py"
                    if live_bridge_check.exists():
                        check_live_cmd = [
                            sys.executable,
                            str(live_bridge_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                            "--account-name", args.account_name,
                            "--character-key", args.character_key,
                        ]
                        check_live_result = run(check_live_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_live_result)
                        if check_live_result["returncode"] != 0:
                            manifest["status"] = "failed_step55_live_bridge_check"

                if manifest["status"] == "passed" and args.with_step56b_progress_bridge:
                    progress_bridge_check = ROOT / "tools" / "check_mmo_step56b_clean_db_progress_bridge.py"
                    if progress_bridge_check.exists():
                        check_progress_cmd = [
                            sys.executable,
                            str(progress_bridge_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                            "--account-name", args.account_name,
                            "--character-key", args.character_key,
                        ]
                        check_progress_result = run(check_progress_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_progress_result)
                        if check_progress_result["returncode"] != 0:
                            manifest["status"] = "failed_step56b_progress_bridge_check"

                if manifest["status"] == "passed" and args.with_step59_item_interactive_progress_bridge:
                    step59_check = ROOT / "tools" / "check_mmo_step59_clean_db_item_interactive_progress_bridge.py"
                    if step59_check.exists():
                        check_step59_cmd = [
                            sys.executable,
                            str(step59_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                            "--account-name", args.account_name,
                            "--character-key", args.character_key,
                        ]
                        check_step59_result = run(check_step59_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step59_result)
                        if check_step59_result["returncode"] != 0:
                            manifest["status"] = "failed_step59_item_interactive_progress_bridge_check"

                if manifest["status"] == "passed" and args.with_step60_equipment_bridge:
                    step60_check = ROOT / "tools" / "check_mmo_step60_clean_db_equipment_bridge.py"
                    if step60_check.exists():
                        check_step60_cmd = [
                            sys.executable,
                            str(step60_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                            "--account-name", args.account_name,
                            "--character-key", args.character_key,
                        ]
                        check_step60_result = run(check_step60_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step60_result)
                        if check_step60_result["returncode"] != 0:
                            manifest["status"] = "failed_step60_equipment_bridge_check"

                if manifest["status"] == "passed" and args.with_step67_interactive_use_bridge:
                    step67_check = ROOT / "tools" / "check_mmo_step67_interactive_use_bridge.py"
                    if step67_check.exists():
                        check_step67_cmd = [
                            sys.executable,
                            str(step67_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                            "--account-name", args.account_name,
                            "--character-key", args.character_key,
                        ]
                        check_step67_result = run(check_step67_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step67_result)
                        if check_step67_result["returncode"] != 0:
                            manifest["status"] = "failed_step67_interactive_use_bridge_check"

                if manifest["status"] == "passed" and args.with_step68_drop_loot_bridge:
                    step68_check = ROOT / "tools" / "check_mmo_step68_drop_loot_inventory_bridge.py"
                    if step68_check.exists():
                        check_step68_cmd = [
                            sys.executable,
                            str(step68_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                            "--account-name", args.account_name,
                            "--character-key", args.character_key,
                        ]
                        check_step68_result = run(check_step68_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step68_result)
                        if check_step68_result["returncode"] != 0:
                            manifest["status"] = "failed_step68_drop_loot_bridge_check"

                if manifest["status"] == "passed" and args.with_step93_save_checkpoint_quest_utf8_bridge:
                    step93_check = ROOT / "tools" / "check_mmo_step93_save_checkpoint_quest_utf8_bridge.py"
                    if step93_check.exists():
                        check_step93_cmd = [
                            sys.executable,
                            str(step93_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                        ]
                        check_step93_result = run(check_step93_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step93_result)
                        if check_step93_result["returncode"] != 0:
                            manifest["status"] = "failed_step93_save_checkpoint_quest_utf8_bridge_check"

                if manifest["status"] == "passed" and args.with_step94_server_save_checkpoint_manifest:
                    step94_check = ROOT / "tools" / "check_mmo_step94_server_save_checkpoint_manifest.py"
                    if step94_check.exists():
                        check_step94_cmd = [
                            sys.executable,
                            str(step94_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                        ]
                        check_step94_result = run(check_step94_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step94_result)
                        if check_step94_result["returncode"] != 0:
                            manifest["status"] = "failed_step94_server_save_checkpoint_manifest_check"

                if manifest["status"] == "passed" and args.with_step95_save_slot_catalog_db_continue_bridge:
                    step95_check = ROOT / "tools" / "check_mmo_step95_save_slot_catalog_db_continue_bridge.py"
                    if step95_check.exists():
                        check_step95_cmd = [
                            sys.executable,
                            str(step95_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                        ]
                        check_step95_result = run(check_step95_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step95_result)
                        if check_step95_result["returncode"] != 0:
                            manifest["status"] = "failed_step95_save_slot_catalog_db_continue_bridge_check"

                if manifest["status"] == "passed" and args.with_step96_db_save_checkpoint_snapshots:
                    step96_check = ROOT / "tools" / "check_mmo_step96_db_save_checkpoint_snapshots.py"
                    if step96_check.exists():
                        check_step96_cmd = [
                            sys.executable,
                            str(step96_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                        ]
                        check_step96_result = run(check_step96_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step96_result)
                        if check_step96_result["returncode"] != 0:
                            manifest["status"] = "failed_step96_db_save_checkpoint_snapshots_check"

                if manifest["status"] == "passed" and legacy_step97_restore_enabled:
                    step97_check = ROOT / "tools" / "check_mmo_step97_db_save_checkpoint_restore_bridge.py"
                    if step97_check.exists():
                        check_step97_cmd = [
                            sys.executable,
                            str(step97_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                        ]
                        check_step97_result = run(check_step97_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step97_result)
                        if check_step97_result["returncode"] != 0:
                            manifest["status"] = "failed_step97_db_save_checkpoint_restore_bridge_check"

                if manifest["status"] == "passed" and legacy_step98_strict_enabled:
                    step98_check = ROOT / "tools" / "check_mmo_step98_strict_db_continue_restore.py"
                    if step98_check.exists():
                        check_step98_cmd = [
                            sys.executable,
                            str(step98_check),
                            "--url", mysql_url_for_database(args.mysql_url, target.database),
                        ]
                        check_step98_result = run(check_step98_cmd, dry_run=args.dry_run)
                        manifest["commands"].append(check_step98_result)
                        if check_step98_result["returncode"] != 0:
                            manifest["status"] = "failed_step98_strict_db_continue_restore_check"

    manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
    manifest_path = output_dir / "mysql_reset_manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={rel(manifest_path)}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" or args.dry_run else 1


if __name__ == "__main__":
    raise SystemExit(main())
