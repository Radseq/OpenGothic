#!/usr/bin/env python3
"""Create a server archive mount/pre-extract plan from Step202 inventory.

The tool does not implement a VDF/MOD reader. It records the operational plan:
which server-owned archives exist, how they should be mounted/extracted, and
optionally what files were found in a pre-extracted directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from pathlib import Path as _MysqlCliPath

_MYSQL_CLI_TOOLS_DIR = _MysqlCliPath(__file__).resolve().parents[1]
if str(_MYSQL_CLI_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_MYSQL_CLI_TOOLS_DIR))
try:
    from _mysql_cli import resolve_mysql_exe
except Exception:  # pragma: no cover - fallback for standalone patch bundles.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[2]

DEFAULT_EXCLUDED_DIRS = {
    ".git",
    ".vs",
    "build",
    "cmake-build-debug",
    "cmake-build-release",
    "runtime",
    "savegame",
    "savegames",
    "saves",
    "screens",
    "systempack",
}


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


@dataclass(frozen=True)
class ExtractedFile:
    logical_path: str
    byte_size: int
    sha256: str
    physical_path: str


def parse_mysql_url(url: str) -> Target:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql://user:password@host:port/database")
    database = (parsed.path or "/").lstrip("/")
    if not database:
        raise ValueError("database missing in MySQL URL")
    return Target(
        host=parsed.hostname or "localhost",
        port=parsed.port or 3306,
        user=unquote(parsed.username or ""),
        password=unquote(parsed.password or ""),
        database=database,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
        "-h",
        target.host,
        "-P",
        str(target.port),
        "-u",
        target.user,
    ]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def redact_cmd(cmd: list[str]) -> list[str]:
    return ["-p***" if item.startswith("-p") and len(item) > 2 else item for item in cmd]


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


def sql_json(value: object) -> str:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "CAST(" + sql_literal(text) + " AS JSON)"


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def normalize_path(raw: object) -> str:
    return str(raw or "").replace("\\", "/").lower().strip("/")


def load_inventory(path: Path) -> dict[str, object]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("inventory root must be a JSON object")
    if not isinstance(data.get("items"), list):
        raise ValueError("inventory JSON must contain an items array")
    return data


def parse_excluded_dirs(raw: str) -> set[str]:
    out = set(DEFAULT_EXCLUDED_DIRS)
    for item in raw.split(","):
        item = item.strip().lower()
        if item:
            out.add(item)
    return out


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def scan_extracted_root(root: Path, excluded_dirs: set[str]) -> list[ExtractedFile]:
    files: list[ExtractedFile] = []
    for current, dirs, names in os.walk(root):
        dirs[:] = [name for name in dirs if name.lower() not in excluded_dirs]
        base = Path(current)
        for name in names:
            path = base / name
            stat = path.stat()
            files.append(ExtractedFile(
                logical_path=path.relative_to(root).as_posix().lower(),
                byte_size=stat.st_size,
                sha256=file_sha256(path),
                physical_path=str(path),
            ))
    return sorted(files, key=lambda item: item.logical_path)


def extracted_manifest_hash(files: list[ExtractedFile]) -> str | None:
    if not files:
        return None
    digest = hashlib.sha256()
    for item in files:
        digest.update(f"{item.logical_path}\t{item.byte_size}\t{item.sha256}\n".encode("utf-8"))
    return digest.hexdigest()


def find_archives(inventory: dict[str, object]) -> list[dict[str, object]]:
    archives: list[dict[str, object]] = []
    for raw in inventory.get("items", []):
        if not isinstance(raw, dict):
            continue
        role = str(raw.get("file_role") or "")
        if role in {"archive_vdf", "archive_mod"}:
            item = dict(raw)
            item["logical_path"] = normalize_path(item.get("logical_path"))
            archives.append(item)
    return sorted(archives, key=lambda item: str(item.get("logical_path") or ""))


def build_plan(inventory: dict[str, object],
               extracted_root: Path | None,
               extracted_archive_logical_path: str,
               mount_strategy: str,
               mount_status: str,
               extracted_root_label: str,
               excluded_dirs: set[str]) -> dict[str, object]:
    archives = find_archives(inventory)
    extracted_files: list[ExtractedFile] = []
    if extracted_root is not None:
        extracted_files = scan_extracted_root(extracted_root, excluded_dirs)

    selected_archive = normalize_path(extracted_archive_logical_path)
    if extracted_files and not selected_archive and len(archives) == 1:
        selected_archive = normalize_path(archives[0].get("logical_path"))

    manifest_hash = extracted_manifest_hash(extracted_files)
    total_bytes = sum(item.byte_size for item in extracted_files)
    root_label = extracted_root_label or (extracted_root.name if extracted_root is not None else "")

    archive_plans: list[dict[str, object]] = []
    for archive in archives:
        logical_path = normalize_path(archive.get("logical_path"))
        receives_extracted_files = bool(extracted_files and selected_archive == logical_path)
        effective_status = mount_status
        if not effective_status:
            effective_status = "verified" if receives_extracted_files else "planned"
        archive_plans.append({
            "archive_logical_path": logical_path,
            "archive_role": archive.get("file_role"),
            "archive_sha256": archive.get("sha256"),
            "archive_byte_size": archive.get("byte_size", 0),
            "mount_strategy": mount_strategy,
            "mount_status": effective_status,
            "extracted_root_label": root_label if receives_extracted_files else "",
            "extracted_file_count": len(extracted_files) if receives_extracted_files else 0,
            "extracted_total_bytes": total_bytes if receives_extracted_files else 0,
            "extracted_manifest_hash": manifest_hash if receives_extracted_files else None,
            "receives_extracted_files": receives_extracted_files,
        })

    mapping_note = "none"
    if extracted_files and not selected_archive:
        mapping_note = "skipped: multiple archives or no --extracted-archive-logical-path"
    elif extracted_files:
        mapping_note = "mapped_to:" + selected_archive

    return {
        "schema": "mmo.server_content_archive_mount_plan.v1",
        "tool": "plan_server_content_archive_mounts.py",
        "content_revision_key": inventory.get("content_revision_key"),
        "manifest_hash": inventory.get("manifest_hash"),
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "archive_count": len(archives),
        "archive_plans": archive_plans,
        "extracted_root": str(extracted_root) if extracted_root is not None else "",
        "extracted_root_label": root_label,
        "extracted_file_count": len(extracted_files),
        "extracted_total_bytes": total_bytes,
        "extracted_manifest_hash": manifest_hash,
        "extracted_mapping_note": mapping_note,
        "extracted_files": [item.__dict__ for item in extracted_files],
    }


def build_sql(content_revision_key: str, plan: dict[str, object]) -> str:
    lines = [
        "SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;",
        "SET @mmo_content_archive_mount_id=NULL;",
        "SET @mmo_content_extracted_file_id=NULL;",
    ]
    for archive in plan.get("archive_plans", []):
        if not isinstance(archive, dict):
            continue
        notes = {
            "schema": "mmo.server_content_archive_mount.v1",
            "tool": "plan_server_content_archive_mounts.py",
            "archive_sha256": archive.get("archive_sha256"),
            "receives_extracted_files": archive.get("receives_extracted_files"),
            "extracted_mapping_note": plan.get("extracted_mapping_note"),
        }
        lines.append(
            "CALL mmo_upsert_server_content_archive_mount("
            + sql_literal(content_revision_key) + ","
            + sql_literal(archive.get("archive_logical_path")) + ","
            + sql_literal(archive.get("archive_role")) + ","
            + sql_literal(archive.get("mount_strategy")) + ","
            + sql_literal(archive.get("mount_status")) + ","
            + sql_literal(archive.get("extracted_root_label") or "") + ","
            + str(int(archive.get("extracted_file_count") or 0)) + ","
            + str(int(archive.get("extracted_total_bytes") or 0)) + ","
            + sql_literal(archive.get("extracted_manifest_hash")) + ","
            + ("UTC_TIMESTAMP(6)" if archive.get("receives_extracted_files") else "NULL") + ","
            + sql_json(notes) + ","
            + "@mmo_content_archive_mount_id);"
        )

    selected_archive = ""
    for archive in plan.get("archive_plans", []):
        if isinstance(archive, dict) and archive.get("receives_extracted_files"):
            selected_archive = normalize_path(archive.get("archive_logical_path"))
            break
    if selected_archive:
        for item in plan.get("extracted_files", []):
            if not isinstance(item, dict):
                continue
            raw_payload = {
                "schema": "mmo.server_content_extracted_file.v1",
                "physical_path": item.get("physical_path"),
                "extracted_root_label": plan.get("extracted_root_label"),
            }
            lines.append(
                "CALL mmo_upsert_server_content_extracted_file("
                + sql_literal(content_revision_key) + ","
                + sql_literal(selected_archive) + ","
                + sql_literal(item.get("logical_path")) + ","
                + str(int(item.get("byte_size") or 0)) + ","
                + sql_literal(item.get("sha256")) + ","
                + sql_literal("hashed") + ","
                + sql_literal(item.get("logical_path")) + ","
                + sql_json(raw_payload) + ","
                + "@mmo_content_extracted_file_id);"
            )
    lines.append("SELECT COUNT(*) AS archive_mount_count FROM mmo_server_content_archive_mounts;")
    return "\n".join(lines) + "\n"


def run_mysql_file(target: Target, sql_path: Path, dry_run: bool) -> dict[str, object]:
    cmd = mysql_cmd(target)
    shown = redact_cmd(cmd)
    if dry_run:
        return {"status": "dry_run", "cmd": shown, "stdout": "", "stderr": "", "returncode": 0}
    proc = subprocess.run(
        cmd,
        input=sql_path.read_text(encoding="utf-8"),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(ROOT),
    )
    return {
        "status": "applied" if proc.returncode == 0 else "failed",
        "returncode": proc.returncode,
        "cmd": shown,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Plan server content archive mounts/pre-extract state.")
    parser.add_argument("--inventory", required=True, help="Inventory JSON generated by analyze_server_content_pack_inventory.py")
    parser.add_argument("--content-revision-key", default="", help="Override inventory content_revision_key.")
    parser.add_argument("--extracted-root", default="", help="Optional server-side pre-extracted content directory to hash.")
    parser.add_argument("--extracted-archive-logical-path", default="", help="Archive logical path that produced --extracted-root.")
    parser.add_argument("--exclude-dir", default="", help="Additional comma-separated directory names skipped when scanning --extracted-root.")
    parser.add_argument("--extracted-root-label", default="", help="Stable label stored in DB instead of machine-specific path.")
    parser.add_argument("--mount-strategy", default="pre_extracted", choices=["pre_extracted", "read_direct", "extract_on_boot", "external_mount", "manual"])
    parser.add_argument("--mount-status", default="", choices=["", "planned", "mounted", "extracted", "verified", "failed", "ignored"])
    parser.add_argument("--url", default="", help="mysql://user:password@host:port/database. If omitted, only artifacts are written.")
    parser.add_argument("--output", default="runtime/step204_server_content_archive_mounts/archive_mount_plan.json")
    parser.add_argument("--sql-output", default="runtime/step204_server_content_archive_mounts/register_archive_mounts.sql")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    inventory_path = Path(args.inventory)
    if not inventory_path.is_absolute():
        inventory_path = ROOT / inventory_path
    inventory = load_inventory(inventory_path)
    content_revision_key = args.content_revision_key or str(inventory.get("content_revision_key") or "")
    if not content_revision_key:
        raise SystemExit("content revision key missing; pass --content-revision-key or use inventory with content_revision_key")

    extracted_root = Path(args.extracted_root).resolve() if args.extracted_root else None
    if extracted_root is not None and (not extracted_root.exists() or not extracted_root.is_dir()):
        raise SystemExit(f"extracted root does not exist or is not a directory: {extracted_root}")

    plan = build_plan(
        inventory,
        extracted_root,
        args.extracted_archive_logical_path,
        args.mount_strategy,
        args.mount_status,
        args.extracted_root_label,
        parse_excluded_dirs(args.exclude_dir),
    )
    plan["content_revision_key"] = content_revision_key
    plan["inventory_path"] = rel(inventory_path)

    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = ROOT / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(plan, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    sql_path = Path(args.sql_output)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path
    sql_path.parent.mkdir(parents=True, exist_ok=True)
    sql_path.write_text(build_sql(content_revision_key, plan), encoding="utf-8")

    result: dict[str, object] = {
        "status": "generated",
        "archive_count": plan["archive_count"],
        "extracted_file_count": plan["extracted_file_count"],
        "extracted_mapping_note": plan["extracted_mapping_note"],
        "output": rel(output_path),
        "sql_output": rel(sql_path),
    }
    if args.url:
        mysql_result = run_mysql_file(parse_mysql_url(args.url), sql_path, args.dry_run)
        result["mysql"] = mysql_result
        result["status"] = mysql_result["status"]
        if mysql_result.get("stdout"):
            print(mysql_result["stdout"], end="")
        if mysql_result.get("stderr"):
            print(mysql_result["stderr"], file=sys.stderr, end="")

    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if result["status"] in {"generated", "applied", "dry_run"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
