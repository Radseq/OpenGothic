#!/usr/bin/env python3
"""Run the C++ ZenKit VFS probe/extractor for Gothic world ZEN archives."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[2]


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def resolve_probe(path_arg: str) -> Path:
    if path_arg:
        path = Path(path_arg)
        if not path.is_absolute():
            path = ROOT / path
        return path
    return ROOT / "build" / "mmo_cpp_server" / "mmo_vdf_world_zen_probe"


def main() -> int:
    parser = argparse.ArgumentParser(description="Probe/extract Gothic world ZEN files from VDF/MOD archives.")
    parser.add_argument("--gothic-root", required=True)
    parser.add_argument("--world-name", default="newworld.zen")
    parser.add_argument("--extract", action="store_true")
    parser.add_argument("--output-root", default="runtime/content_build/vfs_extracted")
    parser.add_argument("--report", default="runtime/content_build/vdf_world_zen_probe.json")
    parser.add_argument("--probe", default="", help="Path to built mmo_vdf_world_zen_probe binary.")
    args = parser.parse_args()

    probe = resolve_probe(args.probe)
    if not probe.exists():
        raise SystemExit(
            "missing probe binary: " + rel(probe) + "\n"
            "Build it first:\n"
            "  cmake -S server/cpp -B build/mmo_cpp_server -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo\n"
            "  cmake --build build/mmo_cpp_server --target mmo_vdf_world_zen_probe -j"
        )

    cmd = [
        str(probe),
        "--gothic-root", args.gothic_root,
        "--world-name", args.world_name,
        "--output-root", args.output_root,
        "--report", args.report,
    ]
    if args.extract:
        cmd.append("--extract")

    print("[RUN] " + " ".join(cmd))
    proc = subprocess.run(cmd, cwd=str(ROOT), text=True, encoding="utf-8", errors="replace")
    report_path = Path(args.report)
    if not report_path.is_absolute():
        report_path = ROOT / report_path
    if report_path.exists():
        try:
            report = json.loads(report_path.read_text(encoding="utf-8"))
            selected = report.get("selected") if isinstance(report.get("selected"), dict) else {}
            extracted = str(report.get("extracted_host_path") or "")
            print("report=" + rel(report_path))
            if selected:
                print("selected_vfs_path=" + str(selected.get("vfs_path") or ""))
            if extracted:
                print("extra_root_for_discovery=" + str(Path(extracted).parent))
        except Exception:
            pass
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
