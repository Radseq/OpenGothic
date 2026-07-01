#!/usr/bin/env python3
"""Run Step53 server materialization/read-model follow-up.

This installs physical typed read-model tables, rebuilds them from current MySQL
projection sources, inspects the result and exports a compact manifest for the
future server bootstrap work.
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
    parser = argparse.ArgumentParser(description="Step53 MMO server read-model/materialization follow-up.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--output-dir", default="runtime/step53_server_materialization")
    parser.add_argument("--limit", type=int, default=50)
    parser.add_argument("--sample-limit", type=int, default=25)
    parser.add_argument("--create-only", action="store_true", help="Create read-model tables only; inspection allows empty core tables")
    args = parser.parse_args()

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    materialize_report = out_dir / "server_read_model_materialize.json"
    inspect_report = out_dir / "server_read_model_inspect.json"
    materialization_manifest = out_dir / "server_materialization_manifest.json"

    materialize_cmd = [
        sys.executable,
        str(ROOT / "tools" / "materialize_mmo_server_read_model_v1.py"),
        "--url",
        args.url,
        "--output",
        str(materialize_report),
    ]
    if args.create_only:
        materialize_cmd.append("--create-only")
    rc_materialize = run(materialize_cmd)

    inspect_cmd = [
        sys.executable,
        str(ROOT / "tools" / "inspect_mmo_server_read_model_v1.py"),
        "--url",
        args.url,
        "--output",
        str(inspect_report),
        "--limit",
        str(args.limit),
    ]
    if args.create_only:
        inspect_cmd.append("--allow-empty-core")
    rc_inspect = run(inspect_cmd)

    rc_export = 0
    if rc_materialize == 0:
        rc_export = run([
            sys.executable,
            str(ROOT / "tools" / "export_mmo_server_materialization_manifest.py"),
            "--url",
            args.url,
            "--output",
            str(materialization_manifest),
            "--sample-limit",
            str(args.sample_limit),
        ])
    else:
        rc_export = 1

    materialize = read_json(materialize_report)
    inspected = read_json(inspect_report)
    exported = read_json(materialization_manifest)
    ok = rc_materialize == 0 and rc_inspect == 0 and rc_export == 0
    manifest = {
        "step": 53,
        "tool": "run_mmo_step53_server_materialization_followup.py",
        "status": "passed" if ok else "failed",
        "artifacts": {
            "materialize": str(materialize_report),
            "inspect": str(inspect_report),
            "server_materialization_manifest": str(materialization_manifest),
        },
        "return_codes": {
            "materialize": rc_materialize,
            "inspect": rc_inspect,
            "export": rc_export,
        },
        "important": {
            "created_physical_typed_read_models": inspected.get("verdict", {}).get("server_read_model_v1_exists"),
            "read_model_has_no_json": inspected.get("verdict", {}).get("server_read_model_v1_has_no_json"),
            "read_model_uses_no_views": inspected.get("verdict", {}).get("server_read_model_v1_uses_no_views"),
            "ready_for_bootstrap_experiments": inspected.get("verdict", {}).get("server_read_model_v1_ready_for_bootstrap_experiments"),
            "still_final_production_db": False,
        },
        "materialize_summary": materialize,
        "inspect_summary": inspected,
        "export_counts": exported.get("counts", {}),
    }
    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={materialize_report}")
    print(f"artifact={inspect_report}")
    print(f"artifact={materialization_manifest}")
    print(f"artifact={manifest_path}")
    print("status=" + manifest["status"])
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
