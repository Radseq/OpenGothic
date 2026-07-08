#!/usr/bin/env python3
"""Print an operator-friendly summary for Gothic content discovery JSON."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]


def rel(path: Path | None) -> str:
    if path is None:
        return ""
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def load_json(path: Path) -> dict[str, object]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("report root must be a JSON object")
    return data


def source_line(name: str, value: object) -> str:
    if not isinstance(value, dict):
        return f"- {name}: missing"
    logical = value.get("logical_path") or ""
    size = value.get("byte_size") or 0
    sha = str(value.get("sha256") or "")
    return f"- {name}: {logical} ({size} bytes, sha256={sha[:12]}...)"


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize Gothic content discovery status.")
    parser.add_argument("--discovery", default="runtime/content_build/gothic_content_sources.json")
    parser.add_argument("--run-script", default="runtime/content_build/run_content_build_importer.sh")
    args = parser.parse_args()

    discovery_path = Path(args.discovery)
    if not discovery_path.is_absolute():
        discovery_path = ROOT / discovery_path
    data = load_json(discovery_path)

    selected = data.get("selected_sources") if isinstance(data.get("selected_sources"), dict) else {}
    warnings = data.get("warnings") if isinstance(data.get("warnings"), list) else []
    summary = data.get("summary") if isinstance(data.get("summary"), dict) else {}
    status = str(data.get("status") or "unknown")

    print("Gothic content discovery")
    print(f"- report: {rel(discovery_path)}")
    print(f"- status: {status}")
    print(f"- gothic_root: {data.get('gothic_root') or ''}")
    extra_roots = data.get("extra_roots") if isinstance(data.get("extra_roots"), list) else []
    if extra_roots:
        print("- extra_roots: " + ", ".join(str(item) for item in extra_roots))
    print(f"- content_revision_key: {data.get('content_revision_key') or ''}")
    print(f"- archives: {summary.get('archive_count', 0)}")
    print(f"- authoritative_world_zen_count: {summary.get('authoritative_world_zen_count', summary.get('world_zen_count', 0))}")
    print(f"- ignored_world_zen_count: {summary.get('ignored_world_zen_count', 0)}")
    print(source_line("world_zen", selected.get("world_zen")))
    print(source_line("scripts_dat", selected.get("scripts_dat")))
    print(source_line("dialog_ou", selected.get("dialog_ou")))

    if warnings:
        print("\nWarnings:")
        for warning in warnings:
            if isinstance(warning, dict):
                print(f"- {warning.get('code')}: {warning.get('message')}")

    run_script = Path(args.run_script)
    if not run_script.is_absolute():
        run_script = ROOT / run_script
    print("\nNext step:")
    if status == "ready_for_importer":
        print(f"- build mmo_content_build_importer, then run {rel(run_script)}")
        print("- the generated script writes parser_snapshot.json and SQL by default")
        print("- set MMO_CONTENT_BUILD_DB_MODE=apply to also apply the generated SQL to MySQL")
    elif status == "needs_extract_or_vfs_mount":
        print("- extract/mount VDF/MOD archives first, or run tools/probe_gothic_world_zen_archives.py --extract")
        print("- after extraction, rerun discovery with --extra-root runtime/content_build/vfs_extracted")
        print(f"- for an intentional DAT/OU-only snapshot: MMO_ALLOW_PARTIAL_CONTENT_BUILD=1 {rel(run_script)}")
    else:
        print("- inspect the report manually; status is not recognized")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
