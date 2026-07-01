#!/usr/bin/env python3
"""Step54 wrapper: capture/verify the Chapter 1 before-Xardas clean-start baseline."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def run(cmd: list[str]) -> dict[str, object]:
    print("[RUN] " + " ".join(cmd))
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    return {"cmd": cmd, "returncode": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr}


def main() -> int:
    ap = argparse.ArgumentParser(description="Capture Chapter 1 before-Xardas baseline and optionally reset MySQL from it.")
    ap.add_argument("--source", default="runtime/g2notr.sqlite")
    ap.add_argument("--baseline", default="runtime/baselines/g2notr_chapter1_before_xardas.sqlite")
    ap.add_argument("--manifest", default="runtime/baselines/g2notr_chapter1_before_xardas.manifest.json")
    ap.add_argument("--output-dir", default="runtime/step54_chapter1_clean_start")
    ap.add_argument("--character-key", default="PC_HERO")
    ap.add_argument("--strict", action="store_true", help="Require zero dialog selections in source runtime DB.")
    ap.add_argument("--overwrite", action="store_true")
    ap.add_argument("--mysql-url", default="", help="Optional: also rebuild the MySQL dev DB from the captured baseline.")
    ap.add_argument("--reset-mysql", action="store_true", help="Actually run destructive MySQL reset. Requires --mysql-url.")
    ap.add_argument("--i-understand-this-drops-database", action="store_true")
    args = ap.parse_args()

    out_dir = Path(args.output_dir)
    if not out_dir.is_absolute():
        out_dir = ROOT / out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    capture_cmd = [
        sys.executable,
        str(ROOT / "tools" / "capture_mmo_chapter1_start_sqlite_baseline.py"),
        "--source", args.source,
        "--output", args.baseline,
        "--manifest", args.manifest,
        "--character-key", args.character_key,
    ]
    if args.strict:
        capture_cmd.append("--strict")
    if args.overwrite:
        capture_cmd.append("--overwrite")

    manifest = {
        "step": 54,
        "started_at": datetime.now(timezone.utc).isoformat(),
        "status": "running",
        "commands": [],
    }
    capture = run(capture_cmd)
    manifest["commands"].append(capture)
    if capture["returncode"] != 0:
        manifest["status"] = "failed_capture"
    elif args.reset_mysql:
        if not args.mysql_url:
            print("ERROR: --reset-mysql requires --mysql-url", file=sys.stderr)
            manifest["status"] = "failed_missing_mysql_url"
        else:
            reset_cmd = [
                sys.executable,
                str(ROOT / "tools" / "reset_mmo_mysql_from_chapter1_start.py"),
                "--mysql-url", args.mysql_url,
                "--baseline", args.baseline,
                "--output-dir", str(out_dir / "mysql_reset"),
                "--character-key", args.character_key,
            ]
            if args.i_understand_this_drops_database:
                reset_cmd.append("--i-understand-this-drops-database")
            reset = run(reset_cmd)
            manifest["commands"].append(reset)
            manifest["status"] = "passed" if reset["returncode"] == 0 else "failed_mysql_reset"
    else:
        manifest["status"] = "passed"

    manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
    result_path = out_dir / "manifest.json"
    result_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={rel(result_path)}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
