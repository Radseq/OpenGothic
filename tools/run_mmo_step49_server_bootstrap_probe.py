#!/usr/bin/env python3
"""Step49 convenience wrapper for server-bootstrap and NPC navigation probes."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str], optional: bool = False) -> dict[str, object]:
    print("[RUN] " + " ".join(cmd))
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0 and not optional:
        raise RuntimeError(f"command failed with status {proc.returncode}: {' '.join(cmd)}")
    return {"cmd": cmd, "returncode": proc.returncode, "optional": optional}


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Step49 read-only bootstrap/navigation probes.")
    parser.add_argument("--url", help="mysql://user:password@host:port/database")
    parser.add_argument("--sqlite-db", help="Runtime SQLite DB, e.g. runtime/g2notr.sqlite")
    parser.add_argument("--character-key", default="PC_HERO")
    parser.add_argument("--output-dir", default="runtime/step49_server_bootstrap")
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args()

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, object] = {"step": 49, "outputs": {}, "runs": []}

    if args.sqlite_db:
        nav_out = out_dir / "runtime_navigation_probe.json"
        manifest["runs"].append(run([
            sys.executable,
            str(ROOT / "tools" / "inspect_mmo_runtime_navigation.py"),
            "--db", args.sqlite_db,
            "--output", str(nav_out),
            "--limit", str(args.limit),
        ]))
        manifest["outputs"]["runtime_navigation"] = str(nav_out)
    else:
        print("[SKIP] --sqlite-db not supplied; runtime waypoint/navigation probe skipped")

    if args.url:
        mysql_out = out_dir / "mysql_server_bootstrap_probe.json"
        manifest["runs"].append(run([
            sys.executable,
            str(ROOT / "tools" / "inspect_mmo_mysql_server_bootstrap_state.py"),
            "--url", args.url,
            "--character-key", args.character_key,
            "--output", str(mysql_out),
            "--limit", str(args.limit),
        ]))
        manifest["outputs"]["mysql_server_bootstrap"] = str(mysql_out)
    else:
        print("[SKIP] --url not supplied; MySQL bootstrap probe skipped")

    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"artifact={manifest_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
