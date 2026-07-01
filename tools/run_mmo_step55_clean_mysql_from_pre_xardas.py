#!/usr/bin/env python3
"""Build a clean local MySQL MMO dev database from the pre-Xardas SQLite capture.

This wrapper is intentionally boring and explicit:
1. treat runtime/g2notr_ch1_pre_xardas.sqlite as a local capture/oracle file;
2. copy/validate it into the canonical Step54 baseline path;
3. destructively rebuild the named MySQL database from that baseline;
4. leave only logs/manifests under runtime/;
5. install the live receiver bridge, Step56b progress bridge, Step59 item/interactive/progress bridge,
   Step60 equipment bridge, Step67 interactive-use bridge, Step68 drop/loot bridge,
   Step83 combat/lifecycle bridge, and normalize collations by default;
6. write a Step70 PC_HERO_TEST live-loop readiness manifest.

It does not make SQLite the server database. The live server path remains:
OpenGothic client -> UDP receiver/server boundary -> MySQL outbox/procedures/read models.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlparse, urlunparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SQLITE_SOURCE = ROOT / "runtime" / "g2notr_ch1_pre_xardas.sqlite"
DEFAULT_BASELINE = ROOT / "runtime" / "baselines" / "g2notr_chapter1_before_xardas.sqlite"
DEFAULT_BASELINE_MANIFEST = ROOT / "runtime" / "baselines" / "g2notr_chapter1_before_xardas.manifest.json"
DEFAULT_OUTPUT_DIR = ROOT / "runtime" / "step55_clean_mysql_from_pre_xardas"
DEFAULT_MYSQL_URL = "mysql://gothic:gothic_dev_password@127.0.0.1:3306/gothic_mmo_ch1_clean"


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def abs_path(path: str | Path) -> Path:
    p = Path(path)
    return p if p.is_absolute() else ROOT / p


def redact_mysql_url(value: str) -> str:
    try:
        p = urlparse(value)
        if p.scheme not in {"mysql", "mysql+pymysql"} or not p.netloc:
            return value
        user = p.username or ""
        host = p.hostname or ""
        port = f":{p.port}" if p.port else ""
        auth = f"{user}:***@" if user else "***@"
        return urlunparse((p.scheme, auth + host + port, p.path, "", "", ""))
    except Exception:  # noqa: BLE001 - best-effort redaction only
        return value


def redact_cmd(cmd: list[str]) -> list[str]:
    out: list[str] = []
    prev = ""
    for part in cmd:
        if prev in {"--mysql-url", "--url"}:
            out.append(redact_mysql_url(part))
        elif part.startswith("mysql://") or part.startswith("mysql+pymysql://"):
            out.append(redact_mysql_url(part))
        else:
            out.append(part)
        prev = part
    return out


def run(cmd: list[str], *, dry_run: bool = False) -> dict[str, object]:
    shown = redact_cmd(cmd)
    print("[RUN] " + " ".join(shown))
    if dry_run:
        return {"cmd": shown, "returncode": 0, "dry_run": True, "stdout_tail": "", "stderr_tail": ""}
    proc = subprocess.run(cmd, cwd=str(ROOT), text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    return {
        "cmd": shown,
        "returncode": proc.returncode,
        "dry_run": False,
        "stdout_tail": proc.stdout[-8000:],
        "stderr_tail": proc.stderr[-8000:],
    }


def run_reset_mysql_in_process(reset_args: list[str], *, dry_run: bool = False) -> dict[str, object]:
    reset_path = ROOT / "tools" / "reset_mmo_mysql_from_chapter1_start.py"
    shown = redact_cmd([sys.executable, str(reset_path), *reset_args])
    print("[RUN-IN-PROCESS] " + " ".join(shown))
    if dry_run:
        return {"cmd": shown, "returncode": 0, "dry_run": True, "stdout_tail": "", "stderr_tail": ""}
    if not reset_path.exists():
        return {
            "cmd": shown,
            "returncode": 2,
            "dry_run": False,
            "stdout_tail": "",
            "stderr_tail": f"missing reset tool: {reset_path}",
        }

    spec = importlib.util.spec_from_file_location("opengothic_mmo_reset_mysql_from_chapter1_start", reset_path)
    if spec is None or spec.loader is None:
        return {
            "cmd": shown,
            "returncode": 2,
            "dry_run": False,
            "stdout_tail": "",
            "stderr_tail": f"unable to load reset tool: {reset_path}",
        }

    old_argv = sys.argv[:]
    old_module = sys.modules.get(spec.name)
    try:
        module = importlib.util.module_from_spec(spec)
        # Python 3.14 dataclasses expect the module to be visible while class
        # decorators execute during exec_module().
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        sys.argv = [str(reset_path), *reset_args]
        returncode = int(module.main())
    finally:
        sys.argv = old_argv
        if old_module is None:
            sys.modules.pop(spec.name, None)
        else:
            sys.modules[spec.name] = old_module

    return {"cmd": shown, "returncode": returncode, "dry_run": False, "stdout_tail": "", "stderr_tail": ""}


def mysql_database_name(mysql_url: str) -> str:
    path = urlparse(mysql_url).path.lstrip("/")
    return path or ""


def main() -> int:
    ap = argparse.ArgumentParser(description="Build clean MySQL dev DB from runtime/g2notr_ch1_pre_xardas.sqlite.")
    ap.add_argument("--sqlite", default=str(DEFAULT_SQLITE_SOURCE), help="Pre-Xardas SQLite capture file. Default: runtime/g2notr_ch1_pre_xardas.sqlite")
    ap.add_argument("--mysql-url", default=DEFAULT_MYSQL_URL, help="Target MySQL database URL. The database is dropped/recreated.")
    ap.add_argument("--baseline", default=str(DEFAULT_BASELINE), help="Canonical local baseline copy path.")
    ap.add_argument("--manifest", default=str(DEFAULT_BASELINE_MANIFEST), help="Canonical local baseline manifest path.")
    ap.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_DIR), help="Local report/log artifact directory.")
    ap.add_argument("--realm-key", default="local-dev")
    ap.add_argument("--realm-display-name", default="Local Dev Realm")
    ap.add_argument("--account-name", default="local-import")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--skip-audit", action="store_true", help="Skip audit_runtime_sqlite.py before capture/import.")
    ap.add_argument("--skip-step51", action="store_true", help="Do not reapply Step51 procedures after import.")
    ap.add_argument("--skip-step53", action="store_true", help="Do not rebuild Step53 server read models after import.")
    ap.add_argument("--skip-step55-live-bridge", action="store_true", help="Do not install minimal server_sessions/outbox/worker bridge for live receiver.")
    ap.add_argument("--skip-step56b-progress-bridge", action="store_true", help="Do not install progress/dialog/quest procedures required by the resolved live worker.")
    ap.add_argument("--skip-step59-item-interactive-progress-bridge", action="store_true", help="Do not install item pickup/removal, interactive state and progression procedures required by the resolved live worker.")
    ap.add_argument("--skip-step60-equipment-bridge", action="store_true", help="Do not install equip/unequip/transfer procedures required by the resolved live worker.")
    ap.add_argument("--skip-step67-interactive-use-bridge", action="store_true", help="Do not install interactive-use procedure required by the resolved live worker.")
    ap.add_argument("--skip-step68-drop-loot-bridge", action="store_true", help="Do not install drop/loot inventory procedures required by the resolved live worker.")
    ap.add_argument("--skip-step83-combat-lifecycle-bridge", action="store_true", help="Do not install combat/lifecycle procedures required by direct C++ server combat handlers.")
    ap.add_argument("--skip-step84-world-identity-lifecycle-bridge", action="store_true", help="Do not install world identity/lifecycle procedures required by direct C++ server fallback handlers.")
    ap.add_argument("--skip-step93-save-checkpoint-quest-utf8-bridge", action="store_true", help="Do not install quest UTF-8/idempotency procedure and audit table for server-bound save/checkpoint flow.")
    ap.add_argument("--skip-step94-server-save-checkpoint-manifest", action="store_true", help="Do not install durable server save/checkpoint manifest table and procedure.")
    ap.add_argument("--skip-step95-save-slot-catalog-db-continue-bridge", action="store_true", help="Do not install DB-backed save-slot catalog and .sav-free continue bridge metadata.")
    ap.add_argument("--skip-step96-db-save-checkpoint-snapshots", action="store_true", help="Do not install normalized DB-native save checkpoint snapshot tables and procedures.")
    ap.add_argument("--skip-step97-db-save-checkpoint-restore-bridge", action="store_true", help="Do not install DB-save-checkpoint restore/export bridge used by bootstrap snapshots.")
    ap.add_argument("--skip-step98-strict-db-continue-restore", action="store_true", help="Do not install strict DB-native Continue/restore validation bridge.")
    ap.add_argument("--skip-step103-db-checkpoint-export-coverage", action="store_true", help="Do not install DB checkpoint export coverage/world-clock fallback bridge.")
    ap.add_argument("--skip-step104-db-checkpoint-script-state-full-export", action="store_true", help="Do not install DB checkpoint full script-state export bridge.")
    ap.add_argument("--skip-step70-live-readiness", action="store_true", help="Do not run the clean live-loop readiness checker after a successful rebuild.")
    ap.add_argument("--skip-collation-normalize", action="store_true", help="Do not normalize MySQL table collations after the clean import and additive SQL surfaces.")
    ap.add_argument("--no-strict-baseline", action="store_true", help="Do not require zero dialog selections in the SQLite capture.")
    ap.add_argument("--no-overwrite-baseline", action="store_true", help="Do not overwrite existing canonical baseline copy.")
    ap.add_argument("--dry-run", action="store_true", help="Print planned commands without changing files/DB.")
    ap.add_argument("--i-understand-this-drops-database", action="store_true", help="Required because the target MySQL DB is dropped/recreated.")
    args = ap.parse_args()

    sqlite_source = abs_path(args.sqlite)
    baseline = abs_path(args.baseline)
    baseline_manifest = abs_path(args.manifest)
    output_dir = abs_path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    db_name = mysql_database_name(args.mysql_url)
    report: dict[str, object] = {
        "step": "55b_clean_mysql_from_pre_xardas",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "status": "running",
        "sqlite_source": rel(sqlite_source),
        "canonical_baseline": rel(baseline),
        "canonical_baseline_manifest": rel(baseline_manifest),
        "mysql_database": db_name,
        "mysql_url_redacted": redact_mysql_url(args.mysql_url),
        "commands": [],
    }

    if not args.dry_run and not sqlite_source.exists():
        print(f"ERROR: SQLite capture does not exist: {sqlite_source}", file=sys.stderr)
        print("Run the game once with -mmo-sqlite runtime/g2notr_ch1_pre_xardas.sqlite -mmo-sqlite-capture-pre-start-exit first.", file=sys.stderr)
        report["status"] = "failed_missing_sqlite_capture"
    elif not args.dry_run and not args.i_understand_this_drops_database:
        print("ERROR: this drops/recreates the target MySQL database; pass --i-understand-this-drops-database", file=sys.stderr)
        report["status"] = "failed_missing_drop_confirmation"
    else:
        if not args.skip_audit:
            audit_cmd = [sys.executable, str(ROOT / "tools" / "audit_runtime_sqlite.py"), "--db", str(sqlite_source)]
            audit = run(audit_cmd, dry_run=args.dry_run)
            report["commands"].append(audit)
            if audit["returncode"] != 0:
                report["status"] = "failed_sqlite_audit"

        if report["status"] == "running":
            capture_cmd = [
                sys.executable,
                str(ROOT / "tools" / "capture_mmo_chapter1_start_sqlite_baseline.py"),
                "--source", str(sqlite_source),
                "--output", str(baseline),
                "--manifest", str(baseline_manifest),
                "--character-key", args.character_key,
            ]
            if not args.no_strict_baseline:
                capture_cmd.append("--strict")
            if not args.no_overwrite_baseline:
                capture_cmd.append("--overwrite")
            capture = run(capture_cmd, dry_run=args.dry_run)
            report["commands"].append(capture)
            if capture["returncode"] != 0:
                report["status"] = "failed_baseline_capture"

        if report["status"] == "running":
            reset_args = [
                "--mysql-url", args.mysql_url,
                "--baseline", str(baseline),
                "--output-dir", str(output_dir / "mysql_reset"),
                "--realm-key", args.realm_key,
                "--realm-display-name", args.realm_display_name,
                "--account-name", args.account_name,
                "--character-key", args.character_key,
                "--i-understand-this-drops-database",
            ]
            if args.skip_step51:
                reset_args.append("--no-with-step51")
            if args.skip_step53:
                reset_args.append("--no-with-step53")
            if args.skip_step55_live_bridge:
                reset_args.append("--no-with-step55-live-bridge")
            if args.skip_step56b_progress_bridge:
                reset_args.append("--no-with-step56b-progress-bridge")
            if args.skip_step59_item_interactive_progress_bridge:
                reset_args.append("--no-with-step59-item-interactive-progress-bridge")
            if args.skip_step60_equipment_bridge:
                reset_args.append("--no-with-step60-equipment-bridge")
            if args.skip_step67_interactive_use_bridge:
                reset_args.append("--no-with-step67-interactive-use-bridge")
            if args.skip_step68_drop_loot_bridge:
                reset_args.append("--no-with-step68-drop-loot-bridge")
            if args.skip_step83_combat_lifecycle_bridge:
                reset_args.append("--no-with-step83-combat-lifecycle-bridge")
            if args.skip_step84_world_identity_lifecycle_bridge:
                reset_args.append("--no-with-step84-world-identity-lifecycle-bridge")
            if args.skip_step93_save_checkpoint_quest_utf8_bridge:
                reset_args.append("--no-with-step93-save-checkpoint-quest-utf8-bridge")
            if args.skip_step94_server_save_checkpoint_manifest:
                reset_args.append("--no-with-step94-server-save-checkpoint-manifest")
            if args.skip_step95_save_slot_catalog_db_continue_bridge:
                reset_args.append("--no-with-step95-save-slot-catalog-db-continue-bridge")
            if args.skip_step96_db_save_checkpoint_snapshots:
                reset_args.append("--no-with-step96-db-save-checkpoint-snapshots")
            if args.skip_step97_db_save_checkpoint_restore_bridge:
                reset_args.append("--no-with-step97-db-save-checkpoint-restore-bridge")
            if args.skip_step98_strict_db_continue_restore:
                reset_args.append("--no-with-step98-strict-db-continue-restore")
            if args.skip_step103_db_checkpoint_export_coverage:
                reset_args.append("--no-with-step103-db-checkpoint-export-coverage")
            if args.skip_step104_db_checkpoint_script_state_full_export:
                reset_args.append("--no-with-step104-db-checkpoint-script-state-full-export")
            if args.skip_collation_normalize:
                reset_args.append("--no-normalize-collation")
            if args.dry_run:
                reset_args.append("--dry-run")
            reset = run_reset_mysql_in_process(reset_args, dry_run=args.dry_run)
            report["commands"].append(reset)
            report["status"] = "passed" if reset["returncode"] == 0 else "failed_mysql_reset"

        if report["status"] == "passed" and not args.skip_step70_live_readiness:
            readiness_tool = ROOT / "tools" / "check_mmo_step70_clean_live_readiness.py"
            if readiness_tool.exists():
                readiness_cmd = [
                    sys.executable,
                    str(readiness_tool),
                    "--url",
                    args.mysql_url,
                    "--sqlite",
                    str(sqlite_source),
                    "--session-key",
                    "local-dev-PC_HERO_TEST",
                    "--character-key",
                    args.character_key,
                    "--prepare-runtime",
                    "--output",
                    str(ROOT / "runtime" / "pc_hero_test_live" / "clean_live_readiness.json"),
                ]
                if args.dry_run:
                    print("[RUN] " + " ".join(redact_cmd(readiness_cmd)))
                    readiness = {"cmd": redact_cmd(readiness_cmd), "returncode": 0, "dry_run": True, "stdout_tail": "", "stderr_tail": ""}
                else:
                    readiness = run(readiness_cmd, dry_run=False)
                report["commands"].append(readiness)
                if readiness["returncode"] != 0:
                    report["status"] = "failed_step70_live_readiness"
            else:
                report.setdefault("warnings", []).append("check_mmo_step70_clean_live_readiness.py missing; skipped Step70 readiness")

    report["finished_at"] = datetime.now(timezone.utc).isoformat()
    report_path = output_dir / "clean_mysql_from_pre_xardas_manifest.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print()
    print("Step55b clean MySQL from pre-Xardas capture")
    print(f"sqlite_source={rel(sqlite_source)}")
    print(f"canonical_baseline={rel(baseline)}")
    print(f"mysql_database={db_name}")
    print(f"artifact={rel(report_path)}")
    print("status=" + str(report["status"]))
    return 0 if report["status"] == "passed" or args.dry_run else 1


if __name__ == "__main__":
    raise SystemExit(main())











