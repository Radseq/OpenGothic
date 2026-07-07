#!/usr/bin/env python3
"""Classify a Step188 server content manifest into importer inventory roles.

This tool is the bridge between "the server has its own files" and "the server
knows which files future ZEN/DAT/OU loaders must read". It consumes the JSON
manifest produced by register_server_content_pack_manifest.py and writes:

* an inventory JSON report, usable without a DB;
* an optional SQL script that calls Step201 procedures;
* optionally applies that SQL to MySQL.
"""
from __future__ import annotations

import argparse
import json
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
except Exception:  # pragma: no cover - standalone patch bundle fallback.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[2]

TEXTURE_EXTENSIONS = {".tex", ".tga", ".dds", ".png", ".jpg", ".jpeg"}
MESH_EXTENSIONS = {".mrm", ".3ds", ".asc", ".mdm", ".mdh", ".mmb", ".msh"}
SOUND_EXTENSIONS = {".wav", ".ogg", ".mp3"}
VIDEO_EXTENSIONS = {".bik", ".avi"}


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


@dataclass(frozen=True)
class InventoryItem:
    logical_path: str
    source_kind: str
    byte_size: int
    sha256: str
    file_role: str
    loader_stage: str
    import_priority: int
    required_for_server_authority: bool
    import_status: str
    reason: str


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


def classify(logical_path: str, source_kind: str, byte_size: int, sha256: str) -> InventoryItem:
    path = normalize_path(logical_path)
    name = Path(path).name.lower()
    suffix = Path(path).suffix.lower()
    source_kind = (source_kind or "other").lower()

    if suffix == ".zen" or source_kind == "zen":
        role = "world_zen"
        stage = "world_loader"
        priority = 10
        required = True
        status = "ready_for_parser"
        reason = "ZEN world file; future server importer must extract vobs, waypoints, freepoints and spawn positions."
    elif name in {"gothic.dat", "fight.dat"} or suffix == ".dat" or source_kind == "dat":
        role = "scripts_dat"
        stage = "script_vm"
        priority = 20
        required = True
        status = "ready_for_parser"
        reason = "Compiled Daedalus DAT; future server VM/indexer must read NPC, guild, perception, routine and dialog symbols."
    elif name in {"ou.bin", "ou.csl"} or suffix in {".ou", ".csl"} or source_kind == "ou":
        role = "dialog_ou"
        stage = "dialog_output"
        priority = 30
        required = True
        status = "ready_for_parser"
        reason = "Dialog/output unit data; future server/client handshake needs stable output IDs, subtitles and audio references."
    elif suffix == ".vdf" or source_kind == "vdf":
        role = "archive_vdf"
        stage = "archive_mount"
        priority = 5
        required = True
        status = "planned"
        reason = "VDF archive; server should mount or pre-extract it before parsing game data."
    elif suffix == ".mod" or source_kind == "mod":
        role = "archive_mod"
        stage = "archive_mount"
        priority = 5
        required = True
        status = "planned"
        reason = "MOD archive; server should mount or pre-extract it before parsing game data."
    elif suffix in {".d", ".src"} or source_kind == "script":
        role = "script_source"
        stage = "source_reference"
        priority = 80
        required = False
        status = "planned"
        reason = "Script source/reference file; useful for tooling and symbol explanations, not the first runtime source of truth."
    elif suffix == ".ini" or source_kind == "ini":
        role = "config_ini"
        stage = "server_config"
        priority = 60
        required = False
        status = "planned"
        reason = "Configuration file; useful for content pack metadata and future server launch policy."
    elif suffix in TEXTURE_EXTENSIONS or source_kind == "texture":
        role = "asset_texture"
        stage = "asset_lookup"
        priority = 200
        required = False
        status = "unsupported"
        reason = "Texture asset; server normally validates it by hash but does not need to parse it for gameplay authority."
    elif suffix in MESH_EXTENSIONS or source_kind == "mesh":
        role = "asset_mesh"
        stage = "asset_lookup"
        priority = 210
        required = False
        status = "unsupported"
        reason = "Mesh/animation asset; useful for collision/export later, not part of the first authority importer."
    elif suffix in SOUND_EXTENSIONS or source_kind == "sound":
        role = "asset_sound"
        stage = "asset_lookup"
        priority = 220
        required = False
        status = "unsupported"
        reason = "Sound asset; client presents it, server validates version only."
    elif suffix in VIDEO_EXTENSIONS or source_kind == "video":
        role = "asset_video"
        stage = "asset_lookup"
        priority = 230
        required = False
        status = "unsupported"
        reason = "Video asset; client presentation data, not server gameplay authority."
    elif suffix in {".fnt", ".ttf"} or source_kind == "font":
        role = "font"
        stage = "asset_lookup"
        priority = 240
        required = False
        status = "unsupported"
        reason = "Font asset; presentation-only for the current server authority roadmap."
    else:
        role = "other"
        stage = "unknown"
        priority = 1000
        required = False
        status = "not_started"
        reason = "Unclassified file; keep it in the manifest hash, but do not feed it to an importer yet."

    return InventoryItem(
        logical_path=path,
        source_kind=source_kind,
        byte_size=int(byte_size or 0),
        sha256=str(sha256 or "").lower(),
        file_role=role,
        loader_stage=stage,
        import_priority=priority,
        required_for_server_authority=required,
        import_status=status,
        reason=reason,
    )


def load_manifest(path: Path) -> dict[str, object]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("manifest root must be a JSON object")
    files = data.get("files")
    if not isinstance(files, list):
        raise ValueError("manifest JSON must contain a files array")
    return data


def build_inventory(manifest: dict[str, object]) -> list[InventoryItem]:
    out: list[InventoryItem] = []
    for raw in manifest.get("files", []):
        if not isinstance(raw, dict):
            continue
        out.append(classify(
            normalize_path(raw.get("logical_path")),
            str(raw.get("source_kind") or "other"),
            int(raw.get("byte_size") or 0),
            str(raw.get("sha256") or ""),
        ))
    return sorted(out, key=lambda item: (item.import_priority, item.logical_path))


def summarize(items: list[InventoryItem]) -> dict[str, object]:
    by_role: dict[str, int] = {}
    by_stage: dict[str, int] = {}
    by_status: dict[str, int] = {}
    total_bytes = 0
    required = 0
    for item in items:
        by_role[item.file_role] = by_role.get(item.file_role, 0) + 1
        by_stage[item.loader_stage] = by_stage.get(item.loader_stage, 0) + 1
        by_status[item.import_status] = by_status.get(item.import_status, 0) + 1
        total_bytes += item.byte_size
        required += 1 if item.required_for_server_authority else 0
    return {
        "file_count": len(items),
        "required_for_server_authority_count": required,
        "total_bytes": total_bytes,
        "by_role": dict(sorted(by_role.items())),
        "by_stage": dict(sorted(by_stage.items())),
        "by_status": dict(sorted(by_status.items())),
    }


def build_sql(content_revision_key: str, manifest_hash: str, items: list[InventoryItem]) -> str:
    lines = [
        "SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;",
        "SET @mmo_content_inventory_id=NULL;",
    ]
    for item in items:
        notes = {
            "schema": "mmo.server_content_pack_inventory_item.v1",
            "reason": item.reason,
            "manifest_hash": manifest_hash,
            "source_kind": item.source_kind,
            "sha256": item.sha256,
        }
        lines.append(
            "CALL mmo_upsert_server_content_pack_inventory("
            + sql_literal(content_revision_key) + ","
            + sql_literal(item.logical_path) + ","
            + sql_literal(item.file_role) + ","
            + sql_literal(item.loader_stage) + ","
            + str(item.import_priority) + ","
            + ("1" if item.required_for_server_authority else "0") + ","
            + sql_literal(item.import_status) + ","
            + sql_json(notes) + ","
            + "@mmo_content_inventory_id);"
        )
    lines.append("SELECT COUNT(*) AS inventoried_file_count FROM mmo_server_content_pack_inventory;")
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
    parser = argparse.ArgumentParser(description="Analyze a server content pack manifest into Step201 inventory roles.")
    parser.add_argument("--manifest", required=True, help="Step188 manifest.json produced by register_server_content_pack_manifest.py")
    parser.add_argument("--content-revision-key", default="", help="Override manifest content_revision_key.")
    parser.add_argument("--url", default="", help="mysql://user:password@host:port/database. If omitted, only artifacts are written.")
    parser.add_argument("--output", default="runtime/step202_server_content_pack_inventory/inventory.json")
    parser.add_argument("--sql-output", default="runtime/step202_server_content_pack_inventory/register_inventory.sql")
    parser.add_argument("--dry-run", action="store_true", help="Generate artifacts but do not run MySQL.")
    args = parser.parse_args()

    manifest_path = Path(args.manifest)
    if not manifest_path.is_absolute():
        manifest_path = ROOT / manifest_path
    manifest = load_manifest(manifest_path)
    content_revision_key = args.content_revision_key or str(manifest.get("content_revision_key") or "")
    if not content_revision_key:
        raise SystemExit("content revision key missing; pass --content-revision-key or use a Step188 manifest with content_revision_key")
    manifest_hash = str(manifest.get("manifest_hash") or "")
    items = build_inventory(manifest)

    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = ROOT / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)

    report = {
        "schema": "mmo.server_content_pack_inventory.v1",
        "tool": "analyze_server_content_pack_inventory.py",
        "content_revision_key": content_revision_key,
        "manifest_hash": manifest_hash,
        "manifest_path": rel(manifest_path),
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "summary": summarize(items),
        "items": [item.__dict__ for item in items],
    }
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    sql_path = Path(args.sql_output)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path
    sql_path.parent.mkdir(parents=True, exist_ok=True)
    sql_path.write_text(build_sql(content_revision_key, manifest_hash, items), encoding="utf-8")

    result: dict[str, object] = {
        "status": "generated",
        "inventory_path": rel(output_path),
        "sql_path": rel(sql_path),
        "summary": report["summary"],
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
