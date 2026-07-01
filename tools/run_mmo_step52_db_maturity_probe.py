#!/usr/bin/env python3
"""Run Step52 DB maturity probe.

The probe should pass operationally even when it reports production-schema debt.
Debt is the expected current truth. Use --strict only if you intentionally want
CI to fail while JSON/views/large procedure-surface remain in the hot schema.
"""
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


def run(cmd: list[str]) -> int:
    print("[RUN] " + " ".join(cmd))
    proc = subprocess.run(cmd)
    return int(proc.returncode)


def read_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Step52 MMO DB maturity probe wrapper.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--output-dir", default="runtime/step52_db_maturity")
    parser.add_argument("--limit", type=int, default=50)
    parser.add_argument("--strict", action="store_true", help="Fail if production schema debt is detected")
    args = parser.parse_args()

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    schema_report = out_dir / "mysql_schema_maturity.json"
    cmd = [
        sys.executable,
        str(ROOT / "tools" / "inspect_mmo_mysql_schema_maturity.py"),
        "--url",
        args.url,
        "--output",
        str(schema_report),
        "--limit",
        str(args.limit),
    ]
    if args.strict:
        cmd.append("--strict")
    rc = run(cmd)

    report = read_json(schema_report)
    manifest = {
        "step": 52,
        "tool": "run_mmo_step52_db_maturity_probe.py",
        "status": "passed" if rc == 0 else "failed",
        "strict": bool(args.strict),
        "artifacts": {
            "schema_maturity": str(schema_report),
        },
        "schema_maturity": report,
        "important_reading": {
            "production_ready": report.get("verdict", {}).get("current_db_is_final_production_mmo_schema"),
            "dev_authority_bridge": report.get("verdict", {}).get("current_db_is_dev_authority_bridge"),
            "json_debt": report.get("verdict", {}).get("json_debt"),
            "view_debt": report.get("verdict", {}).get("view_debt"),
            "procedure_debt": report.get("verdict", {}).get("procedure_debt"),
        },
    }
    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={schema_report}")
    print(f"artifact={manifest_path}")
    print("status=" + manifest["status"])
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
