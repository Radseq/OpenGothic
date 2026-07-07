#!/usr/bin/env python3
"""Register a server-owned Gothic content pack manifest in MySQL.

This is the first practical step toward a fully authoritative MMO server that
loads its own content pack instead of trusting client files. The tool scans a
server-side Gothic/mod directory, hashes selected content files, writes a local
JSON manifest, and optionally registers the manifest in MySQL through Step188
procedures.
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

DEFAULT_EXTENSIONS = {
    ".zen",
    ".bin",
    ".dat",
    ".ou",
    ".csl",
    ".vdf",
    ".mod",
    ".d",
    ".src",
    ".ini",
}

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

KIND_BY_EXTENSION = {
    ".zen": "zen",
    ".dat": "dat",
    ".ou": "ou",
    ".csl": "ou",
    ".vdf": "vdf",
    ".mod": "mod",
    ".d": "script",
    ".src": "script",
    ".ini": "ini",
}


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


@dataclass(frozen=True)
class ManifestFile:
    logical_path: str
    source_kind: str
    byte_size: int
    sha256: str
    mtime_utc: str
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
    return [
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
        *( [f"-p{target.password}"] if target.password else [] ),
        target.database,
    ]


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


def sql_json(value: object) -> str:
    return "CAST(" + sql_literal(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))) + " AS JSON)"


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def normalize_logical_path(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix().replace("\\", "/").lower()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_kind_for(path: Path) -> str:
    if path.name.lower() == "ou.bin":
        return "ou"
    return KIND_BY_EXTENSION.get(path.suffix.lower(), "other")


def parse_extensions(raw: str, include_all: bool) -> set[str] | None:
    if include_all:
        return None
    if not raw:
        return set(DEFAULT_EXTENSIONS)
    out: set[str] = set()
    for item in raw.split(","):
        item = item.strip().lower()
        if not item:
            continue
        out.add(item if item.startswith(".") else "." + item)
    return out


def parse_excluded_dirs(raw: str) -> set[str]:
    out = set(DEFAULT_EXCLUDED_DIRS)
    for item in raw.split(","):
        item = item.strip().lower()
        if item:
            out.add(item)
    return out


def iter_files(root: Path, extensions: set[str] | None, excluded_dirs: set[str]) -> list[Path]:
    found: list[Path] = []
    for current, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d.lower() not in excluded_dirs]
        base = Path(current)
        for name in files:
            path = base / name
            if extensions is not None and path.suffix.lower() not in extensions:
                continue
            found.append(path)
    return sorted(found, key=lambda p: normalize_logical_path(root, p))


def build_manifest_files(root: Path, paths: list[Path]) -> list[ManifestFile]:
    out: list[ManifestFile] = []
    for path in paths:
        stat = path.stat()
        out.append(ManifestFile(
            logical_path=normalize_logical_path(root, path),
            source_kind=source_kind_for(path),
            byte_size=stat.st_size,
            sha256=file_sha256(path),
            mtime_utc=datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
            physical_path=str(path),
        ))
    return out


def manifest_hash(files: list[ManifestFile]) -> str:
    digest = hashlib.sha256()
    for item in files:
        line = f"{item.logical_path}\t{item.source_kind}\t{item.byte_size}\t{item.sha256}\t1\n"
        digest.update(line.encode("utf-8"))
    return digest.hexdigest()


def build_sql(content_revision_key: str,
              source_root_label: str,
              files: list[ManifestFile],
              hash_value: str,
              source_payload: dict[str, object]) -> str:
    total_bytes = sum(item.byte_size for item in files)
    lines = [
        "SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;",
        "SET @mmo_content_file_id=NULL;",
        "SET @mmo_content_manifest_id=NULL;",
    ]
    for item in files:
        raw_payload = {
            "schema": "mmo.server_content_pack_file.v1",
            "physical_path": item.physical_path,
            "mtime_utc": item.mtime_utc,
        }
        lines.append(
            "CALL mmo_upsert_server_content_pack_file("
            + sql_literal(content_revision_key) + ","
            + sql_literal(item.logical_path) + ","
            + sql_literal(item.source_kind) + ","
            + str(item.byte_size) + ","
            + sql_literal(item.sha256) + ","
            + sql_literal(item.mtime_utc.replace("T", " ").replace("+00:00", "")) + ","
            + "1,"
            + sql_json(raw_payload) + ","
            + "@mmo_content_file_id);"
        )
    lines.append(
        "CALL mmo_set_server_content_pack_manifest("
        + sql_literal(content_revision_key) + ","
        + sql_literal(hash_value) + ","
        + str(len(files)) + ","
        + str(len(files)) + ","
        + str(total_bytes) + ","
        + sql_literal(source_root_label) + ","
        + sql_json(source_payload) + ","
        + "@mmo_content_manifest_id);"
    )
    lines.append("SELECT BIN_TO_UUID(@mmo_content_manifest_id,1) AS content_manifest_uuid;")
    return "\n".join(lines) + "\n"


def run_mysql_file(target: Target, sql_path: Path, dry_run: bool) -> dict[str, object]:
    cmd = mysql_cmd(target)
    shown = ["-p***" if part.startswith("-p") and len(part) > 2 else part for part in cmd]
    if dry_run:
        return {"status": "dry_run", "cmd": shown, "stdout": "", "stderr": ""}
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
    parser = argparse.ArgumentParser(description="Register server-owned Gothic content pack manifest.")
    parser.add_argument("--url", default="", help="mysql://user:password@host:port/database. If omitted, only local artifacts are written.")
    parser.add_argument("--content-root", required=True, help="Server-side Gothic/mod content root to scan.")
    parser.add_argument("--content-revision-key", required=True, help="Existing content_revisions.content_revision_key to attach the manifest to.")
    parser.add_argument("--extensions", default="", help="Comma-separated extensions. Defaults to Gothic content-critical files.")
    parser.add_argument("--include-all", action="store_true", help="Hash all regular files except excluded directories.")
    parser.add_argument("--exclude-dir", default="", help="Additional comma-separated directory names to skip.")
    parser.add_argument("--source-root-label", default="", help="Stable label stored in DB instead of a machine-specific absolute path.")
    parser.add_argument("--output", default="runtime/step188_server_content_pack_manifest/manifest.json")
    parser.add_argument("--sql-output", default="runtime/step188_server_content_pack_manifest/register.sql")
    parser.add_argument("--dry-run", action="store_true", help="Generate manifest/SQL but do not run MySQL.")
    args = parser.parse_args()

    root = Path(args.content_root).resolve()
    if not root.exists() or not root.is_dir():
        raise SystemExit(f"content root does not exist or is not a directory: {root}")

    extensions = parse_extensions(args.extensions, args.include_all)
    excluded_dirs = parse_excluded_dirs(args.exclude_dir)
    files = build_manifest_files(root, iter_files(root, extensions, excluded_dirs))
    hash_value = manifest_hash(files)
    source_root_label = args.source_root_label or root.name
    total_bytes = sum(item.byte_size for item in files)
    source_payload = {
        "schema": "mmo.server_content_pack_manifest.v1",
        "tool": "register_server_content_pack_manifest.py",
        "content_root_label": source_root_label,
        "extensions": sorted(extensions) if extensions is not None else ["*"],
        "excluded_dirs": sorted(excluded_dirs),
    }

    manifest = {
        "schema": "mmo.server_content_pack_manifest.v1",
        "content_revision_key": args.content_revision_key,
        "content_root": str(root),
        "source_root_label": source_root_label,
        "manifest_hash": hash_value,
        "file_count": len(files),
        "required_file_count": len(files),
        "total_bytes": total_bytes,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "files": [item.__dict__ for item in files],
    }

    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = ROOT / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    sql_path = Path(args.sql_output)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path
    sql_path.parent.mkdir(parents=True, exist_ok=True)
    sql_path.write_text(build_sql(args.content_revision_key, source_root_label, files, hash_value, source_payload), encoding="utf-8")

    result: dict[str, object] = {
        "status": "generated",
        "manifest_hash": hash_value,
        "file_count": len(files),
        "total_bytes": total_bytes,
        "manifest_path": rel(output_path),
        "sql_path": rel(sql_path),
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
