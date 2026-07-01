#!/usr/bin/env python3
"""Apply and verify Step51 authority-gap DB procedures."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("[RUN] " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None


def main() -> int:
    ap = argparse.ArgumentParser(description="Apply/check Step51 authority-gap MySQL procedures.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--output-dir", default="runtime/step51_authority_gap")
    ap.add_argument("--skip-apply", action="store_true", help="Only run checker, do not apply SQL")
    ap.add_argument("--sql", default=str(ROOT / "server" / "sql" / "step51_authority_gap_procedures.sql"))
    args = ap.parse_args()

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    apply_json = out_dir / "apply_step51_authority_gap.json"
    check_json = out_dir / "check_step51_authority_gap.json"
    manifest_json = out_dir / "manifest.json"

    if not args.skip_apply:
        run([
            sys.executable,
            str(ROOT / "tools" / "apply_mmo_step51_authority_gap_procedures.py"),
            "--url", args.url,
            "--sql", args.sql,
            "--output", str(apply_json),
        ])

    run([
        sys.executable,
        str(ROOT / "tools" / "check_mmo_step51_authority_gap_procedures.py"),
        "--url", args.url,
        "--output", str(check_json),
    ])

    manifest = {
        "step": 51,
        "status": "passed",
        "apply": load_json(apply_json),
        "check": load_json(check_json),
        "artifacts": {
            "apply": str(apply_json),
            "check": str(check_json),
        },
    }
    if manifest["check"] and manifest["check"].get("status") != "passed":
        manifest["status"] = "failed"
    manifest_json.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={manifest_json}")
    print(f"status={manifest['status']}")
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
