#!/usr/bin/env python3
"""Restore runtime/g2notr.sqlite from the Step54 Chapter 1 clean-start baseline."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE = ROOT / "runtime" / "baselines" / "g2notr_chapter1_before_xardas.sqlite"
DEFAULT_MANIFEST = ROOT / "runtime" / "baselines" / "g2notr_chapter1_before_xardas.manifest.json"
DEFAULT_TARGET = ROOT / "runtime" / "g2notr.sqlite"
BASELINE_KEY = "g2notr_chapter1_before_xardas"


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_manifest(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def sqlite_ok(path: Path) -> tuple[bool, str]:
    try:
        con = sqlite3.connect(str(path))
        try:
            row = con.execute("PRAGMA integrity_check").fetchone()
            value = "" if row is None else str(row[0])
            return value.lower() == "ok", value
        finally:
            con.close()
    except sqlite3.Error as exc:
        return False, str(exc)


def copy_db(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_suffix(target.suffix + ".tmp")
    if tmp.exists():
        tmp.unlink()
    shutil.copy2(source, tmp)
    tmp.replace(target)


def main() -> int:
    ap = argparse.ArgumentParser(description="Restore runtime SQLite from Chapter 1 before-Xardas baseline.")
    ap.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--target", type=Path, default=DEFAULT_TARGET)
    ap.add_argument("--backup-dir", type=Path, default=ROOT / "runtime" / "baseline_restore_backups")
    ap.add_argument("--force", action="store_true", help="Restore even if manifest/baseline key cannot be verified.")
    ap.add_argument("--no-backup", action="store_true", help="Do not backup the existing target before restore.")
    ap.add_argument("--result", type=Path, default=ROOT / "runtime" / "baselines" / "g2notr_chapter1_before_xardas.restore_result.json")
    args = ap.parse_args()

    baseline = args.baseline if args.baseline.is_absolute() else ROOT / args.baseline
    manifest_path = args.manifest if args.manifest.is_absolute() else ROOT / args.manifest
    target = args.target if args.target.is_absolute() else ROOT / args.target
    backup_dir = args.backup_dir if args.backup_dir.is_absolute() else ROOT / args.backup_dir
    result_path = args.result if args.result.is_absolute() else ROOT / args.result

    if not baseline.exists():
        print(f"ERROR: baseline does not exist: {baseline}", file=sys.stderr)
        return 1
    ok, integrity = sqlite_ok(baseline)
    if not ok:
        print(f"ERROR: baseline SQLite integrity_check failed: {integrity}", file=sys.stderr)
        return 1

    manifest = read_manifest(manifest_path)
    warnings: list[str] = []
    if manifest is None:
        warnings.append("manifest file missing")
    elif manifest.get("baseline_key") != BASELINE_KEY:
        warnings.append(f"unexpected manifest baseline_key={manifest.get('baseline_key')!r}")
    elif manifest.get("output", {}).get("sha256") and manifest["output"]["sha256"] != sha256(baseline):
        warnings.append("baseline sha256 differs from manifest output.sha256, possibly because the file was edited/copied")

    if warnings and not args.force:
        print("ERROR: baseline could not be verified; pass --force to restore anyway", file=sys.stderr)
        for warning in warnings:
            print(f"  - {warning}", file=sys.stderr)
        return 1

    backup_path: Path | None = None
    if target.exists() and not args.no_backup:
        backup_dir.mkdir(parents=True, exist_ok=True)
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        backup_path = backup_dir / f"{target.name}.{timestamp}.bak"
        shutil.copy2(target, backup_path)

    copy_db(baseline, target)
    result = {
        "step": 54,
        "status": "restored",
        "baseline": {"path": rel(baseline), "sha256": sha256(baseline)},
        "target": {"path": rel(target), "sha256": sha256(target)},
        "backup": None if backup_path is None else {"path": rel(backup_path), "sha256": sha256(backup_path)},
        "warnings": warnings,
        "restored_at": datetime.now(timezone.utc).isoformat(),
    }
    result_path.parent.mkdir(parents=True, exist_ok=True)
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("Step54 Chapter 1 clean-start baseline restored")
    print(f"target={rel(target)}")
    if backup_path:
        print(f"backup={rel(backup_path)}")
    print(f"result={rel(result_path)}")
    print("status=restored")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
