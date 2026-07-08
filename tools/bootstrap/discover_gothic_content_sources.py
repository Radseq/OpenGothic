#!/usr/bin/env python3
"""Discover Gothic server content files for the ZenKit content-build importer.

This tool does not parse ZEN/DAT/OU. It scans an operator-provided Gothic
installation or pre-extracted content root, computes hashes for the selected
files and writes a runnable shell script for mmo_content_build_importer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import stat
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[2]

WORLD_PRIORITY = (
    "newworld.zen",
    "newworld/newworld.zen",
    "worlds/newworld.zen",
    "data/worlds/newworld.zen",
    "_work/data/worlds/newworld.zen",
    "newworld_part_city_01.zen",
    "oldworld.zen",
    "oldworld/oldworld.zen",
    "worlds/oldworld.zen",
    "data/worlds/oldworld.zen",
    "_work/data/worlds/oldworld.zen",
    "addonworld.zen",
    "addonworld/addonworld.zen",
    "worlds/addonworld.zen",
    "data/worlds/addonworld.zen",
    "_work/data/worlds/addonworld.zen",
)
SCRIPTS_DAT_PRIORITY = (
    "gothic.dat",
    "gothic.src.dat",
    "fight.dat",
    "camera.dat",
    "menu.dat",
)
DIALOG_OU_PRIORITY = (
    "ou.bin",
    "ou.csl",
    "ou.dat",
)
ARCHIVE_SUFFIXES = {".vdf": "archive_vdf", ".mod": "archive_mod"}


@dataclass(frozen=True)
class FoundFile:
    path: Path
    logical_path: str
    source_kind: str
    byte_size: int
    sha256: str


def rel(path: Path | None) -> str:
    if path is None:
        return ""
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def normalize_logical(path: Path, root: Path) -> str:
    try:
        value = path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        value = path.as_posix()
    return value.replace("\\", "/").lower().strip("/")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def classify(path: Path) -> str:
    suffix = path.suffix.lower()
    if suffix == ".zen":
        return "world_zen"
    if suffix == ".dat":
        return "scripts_dat"
    if suffix in {".bin", ".csl", ".ou"} and path.name.lower().startswith("ou"):
        return "dialog_ou"
    if suffix in ARCHIVE_SUFFIXES:
        return ARCHIVE_SUFFIXES[suffix]
    return "other"


def iter_candidates(root: Path) -> list[FoundFile]:
    files: list[FoundFile] = []
    for current, dirnames, filenames in os.walk(root):
        dirnames[:] = [
            name for name in dirnames
            if name.lower() not in {".git", "systempack", "saves", "screenshots"}
        ]
        current_path = Path(current)
        for filename in filenames:
            path = current_path / filename
            kind = classify(path)
            if kind == "other":
                continue
            try:
                stat_result = path.stat()
            except OSError:
                continue
            files.append(FoundFile(
                path=path,
                logical_path=normalize_logical(path, root),
                source_kind=kind,
                byte_size=stat_result.st_size,
                sha256=sha256_file(path),
            ))
    return sorted(files, key=lambda item: (item.source_kind, item.logical_path))


def iter_all_candidates(roots: list[Path]) -> list[FoundFile]:
    files: list[FoundFile] = []
    seen: set[tuple[str, str]] = set()
    for root in roots:
        for item in iter_candidates(root):
            key = (item.source_kind, str(item.path.resolve()))
            if key in seen:
                continue
            seen.add(key)
            files.append(item)
    return sorted(files, key=lambda item: (item.source_kind, item.logical_path, str(item.path)))


def is_authoritative_world_zen(item: FoundFile) -> bool:
    logical = item.logical_path.lower()
    name = item.path.name.lower()
    if "/presets/" in "/" + logical or "/presets/" in logical:
        return False
    if name in {"lensflare.zen"}:
        return False
    world_path_markers = (
        "data/worlds/",
        "_work/data/worlds/",
        "/worlds/",
    )
    known_world_names = {
        "newworld.zen",
        "oldworld.zen",
        "addonworld.zen",
    }
    return any(marker in logical for marker in world_path_markers) or name in known_world_names


def pick_by_priority(files: list[FoundFile],
                     kind: str,
                     priority: tuple[str, ...],
                     predicate: object = None) -> FoundFile | None:
    matching = [item for item in files if item.source_kind == kind]
    if predicate is not None:
        matching = [item for item in matching if predicate(item)]
    if not matching:
        return None
    by_name = {item.path.name.lower(): item for item in matching}
    by_logical = {item.logical_path.lower(): item for item in matching}
    for name in priority:
        if name in by_logical:
            return by_logical[name]
        if name in by_name:
            return by_name[name]
    return sorted(matching, key=lambda item: (len(item.logical_path), item.logical_path))[0]


def source_payload(item: FoundFile | None, role: str) -> dict[str, object] | None:
    if item is None:
        return None
    return {
        "logical_path": item.logical_path,
        "host_path": str(item.path),
        "file_role": role,
        "byte_size": item.byte_size,
        "sha256": item.sha256,
        "required_for_server_authority": True,
    }


def shell_script(args: argparse.Namespace, picks: dict[str, FoundFile | None], status: str) -> str:
    importer = args.importer_path or "./build/mmo_cpp_server/mmo_content_build_importer"
    output = args.parser_snapshot
    missing = [key for key in ("world_zen", "scripts_dat", "dialog_ou") if picks[key] is None]
    cmd = [
        importer,
        "--content-revision-key", args.content_revision_key,
        "--game-code", args.game_code,
        "--source-root-label", args.source_root_label,
        "--output", output,
    ]
    if picks["world_zen"] is not None:
        cmd += [
            "--world-name", args.world_name,
            "--world-zen", str(picks["world_zen"].path),
            "--world-zen-logical-path", picks["world_zen"].logical_path,
            "--world-zen-sha256", picks["world_zen"].sha256,
        ]
    if picks["scripts_dat"] is not None:
        cmd += [
            "--scripts-dat", str(picks["scripts_dat"].path),
            "--scripts-dat-logical-path", picks["scripts_dat"].logical_path,
            "--scripts-dat-sha256", picks["scripts_dat"].sha256,
        ]
    if picks["dialog_ou"] is not None:
        cmd += [
            "--dialog-ou", str(picks["dialog_ou"].path),
            "--dialog-ou-logical-path", picks["dialog_ou"].logical_path,
            "--dialog-ou-sha256", picks["dialog_ou"].sha256,
        ]
    quoted = " \\\n  ".join(shlex.quote(part) for part in cmd)
    import_cmd = [
        "tools/import_content_build_snapshot_database.py",
        "--snapshot", output,
        "--output", args.import_report,
        "--sql-output", args.import_sql,
    ]
    import_quoted = " \\\n  ".join(shlex.quote(part) for part in import_cmd)
    apply_cmd = import_cmd[:2] + import_cmd[2:] + ["--url", "__MYSQL_URL__"]
    apply_quoted = " \\\n  ".join(
        "${MYSQL_URL:?MYSQL_URL is not set}" if part == "__MYSQL_URL__" else shlex.quote(part)
        for part in apply_cmd
    )
    missing_text = ",".join(missing)
    guard = ""
    if status != "ready_for_importer":
        guard = (
            "if [[ \"${MMO_ALLOW_PARTIAL_CONTENT_BUILD:-0}\" != \"1\" ]]; then\n"
            "  echo \"content discovery is not ready_for_importer; missing: " + missing_text + "\" >&2\n"
            "  echo \"Set MMO_ALLOW_PARTIAL_CONTENT_BUILD=1 only if you intentionally want a DAT/OU-only snapshot.\" >&2\n"
            "  exit 3\n"
            "fi\n\n"
        )
    return (
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n\n"
        "cd \"$(dirname \"$0\")/../..\"\n\n"
        "mkdir -p runtime/content_build\n\n"
        + guard
        + quoted + "\n\n"
        "# Default: generate SQL/report only. Applying a large DAT/OU snapshot to MySQL can take a while.\n"
        "case \"${MMO_CONTENT_BUILD_DB_MODE:-generate}\" in\n"
        "  generate)\n"
        + import_quoted + "\n"
        "    ;;\n"
        "  apply)\n"
        + apply_quoted + "\n"
        "    ;;\n"
        "  skip)\n"
        "    echo \"Skipping content-build DB import because MMO_CONTENT_BUILD_DB_MODE=skip\"\n"
        "    ;;\n"
        "  *)\n"
        "    echo \"Invalid MMO_CONTENT_BUILD_DB_MODE=${MMO_CONTENT_BUILD_DB_MODE}; expected generate, apply or skip\" >&2\n"
        "    exit 2\n"
        "    ;;\n"
        "esac\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Discover loose Gothic ZEN/DAT/OU files and create a content-build importer run script.")
    parser.add_argument("--gothic-root", required=True, help="Gothic II install or pre-extracted content root.")
    parser.add_argument("--extra-root", action="append", default=[], help="Additional loose content root, e.g. runtime/content_build/vfs_extracted after VDF ZEN extraction.")
    parser.add_argument("--content-revision-key", default="gothic2-notr-server-content-dev")
    parser.add_argument("--game-code", default="gothic2-notr")
    parser.add_argument("--source-root-label", default="local-gothic2-server-content")
    parser.add_argument("--world-name", default="newworld")
    parser.add_argument("--importer-path", default="", help="Path to mmo_content_build_importer used in the generated shell script.")
    parser.add_argument("--output", default="runtime/content_build/gothic_content_sources.json")
    parser.add_argument("--run-script", default="runtime/content_build/run_content_build_importer.sh")
    parser.add_argument("--parser-snapshot", default="runtime/content_build/parser_snapshot.json")
    parser.add_argument("--import-report", default="runtime/content_build/content_build_database_report.json")
    parser.add_argument("--import-sql", default="runtime/content_build/import_content_build_database.sql")
    args = parser.parse_args()

    gothic_root = Path(args.gothic_root).expanduser()
    if not gothic_root.exists() or not gothic_root.is_dir():
        raise SystemExit("gothic root does not exist or is not a directory: " + str(gothic_root))
    extra_roots: list[Path] = []
    for raw_root in args.extra_root:
        extra_root = Path(raw_root).expanduser()
        if not extra_root.is_absolute():
            extra_root = ROOT / extra_root
        if not extra_root.exists() or not extra_root.is_dir():
            raise SystemExit("extra root does not exist or is not a directory: " + str(extra_root))
        extra_roots.append(extra_root)

    scan_roots = [gothic_root] + extra_roots
    files = iter_all_candidates(scan_roots)
    picks = {
        "world_zen": pick_by_priority(files, "world_zen", WORLD_PRIORITY, is_authoritative_world_zen),
        "scripts_dat": pick_by_priority(files, "scripts_dat", SCRIPTS_DAT_PRIORITY),
        "dialog_ou": pick_by_priority(files, "dialog_ou", DIALOG_OU_PRIORITY),
    }
    archive_files = [item for item in files if item.source_kind in {"archive_vdf", "archive_mod"}]
    ignored_world_zen_count = sum(1 for item in files if item.source_kind == "world_zen" and not is_authoritative_world_zen(item))
    loose_ready = all(picks[key] is not None for key in ("world_zen", "scripts_dat", "dialog_ou"))
    status = "ready_for_importer" if loose_ready else "needs_extract_or_vfs_mount"

    report = {
        "schema": "mmo.gothic_content_source_discovery.v1",
        "tool": "discover_gothic_content_sources.py",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "status": status,
        "gothic_root": str(gothic_root),
        "extra_roots": [str(root) for root in extra_roots],
        "content_revision_key": args.content_revision_key,
        "game_code": args.game_code,
        "source_root_label": args.source_root_label,
        "summary": {
            "candidate_count": len(files),
            "archive_count": len(archive_files),
            "world_zen_count": sum(1 for item in files if item.source_kind == "world_zen"),
            "authoritative_world_zen_count": sum(1 for item in files if item.source_kind == "world_zen" and is_authoritative_world_zen(item)),
            "ignored_world_zen_count": ignored_world_zen_count,
            "scripts_dat_count": sum(1 for item in files if item.source_kind == "scripts_dat"),
            "dialog_ou_count": sum(1 for item in files if item.source_kind == "dialog_ou"),
        },
        "selected_sources": {
            key: source_payload(item, key) for key, item in picks.items()
        },
        "archives": [
            source_payload(item, item.source_kind) for item in archive_files[:80]
        ],
        "warnings": [],
    }
    if not loose_ready:
        report["warnings"].append({
            "code": "loose_content_missing",
            "message": "The C++ importer reads loose ZEN/DAT/OU paths. This root appears to need VDF/MOD extraction or a future VFS-mounted importer mode.",
            "missing": [key for key, item in picks.items() if item is None],
        })
    if ignored_world_zen_count:
        report["warnings"].append({
            "code": "non_world_zen_ignored",
            "message": "Some .zen files were ignored because they are presets or non-world resources, not authoritative world ZEN files.",
            "ignored_count": ignored_world_zen_count,
        })

    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = ROOT / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    script_path = Path(args.run_script)
    if not script_path.is_absolute():
        script_path = ROOT / script_path
    script_path.parent.mkdir(parents=True, exist_ok=True)
    script_path.write_text(shell_script(args, picks, status), encoding="utf-8")
    script_path.chmod(script_path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    print(json.dumps({
        "status": status,
        "output": rel(output_path),
        "run_script": rel(script_path),
        "selected_sources": report["selected_sources"],
        "warnings": report["warnings"],
    }, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
