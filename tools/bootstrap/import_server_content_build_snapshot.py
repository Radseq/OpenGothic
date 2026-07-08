#!/usr/bin/env python3
"""Generate SQL for Step208 content build indexes from a parser snapshot JSON.

This is intentionally parser-agnostic. A future ZEN/DAT/OU parser only needs to
emit the documented JSON shape; this tool turns that shape into deterministic
DB writes for the MMO server content build indexes.
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
except Exception:  # pragma: no cover - fallback for standalone patch bundles.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql://user:password@host:port/database")
    database = (parsed.path or "/").lstrip("/")
    if not database:
        raise ValueError("database missing in MySQL URL")
    return Target(parsed.hostname or "localhost", parsed.port or 3306, unquote(parsed.username or ""), unquote(parsed.password or ""), database)


def mysql_cmd(target: Target) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "-h", target.host, "-P", str(target.port), "-u", target.user]
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


def sql_number(value: object) -> str:
    if value is None or value == "":
        return "NULL"
    try:
        return str(float(value))
    except (TypeError, ValueError):
        return "NULL"


def sql_int(value: object) -> str:
    if value is None or value == "":
        return "NULL"
    try:
        return str(int(value))
    except (TypeError, ValueError):
        return "NULL"


def normalize_path(value: object) -> str:
    return str(value or "").replace("\\", "/").lower().strip("/")


def safe_key(value: object, fallback: str) -> str:
    text = str(value or "").strip()
    return text if text else fallback


def rel(path: Path | None) -> str:
    if path is None:
        return ""
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def load_snapshot(path: Path) -> dict[str, object]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("snapshot root must be a JSON object")
    return data


def rows(snapshot: dict[str, object], key: str) -> list[dict[str, object]]:
    value = snapshot.get(key)
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, dict)]


def source_for(snapshot: dict[str, object], kind: str) -> dict[str, object]:
    sources = snapshot.get("sources")
    if isinstance(sources, dict) and isinstance(sources.get(kind), dict):
        return dict(sources[kind])
    return {}


def count_items(snapshot: dict[str, object], importer_key: str) -> int:
    if importer_key == "zen_world_import":
        return len(rows(snapshot, "zen_entities"))
    if importer_key == "daedalus_dat_index":
        return len(rows(snapshot, "daedalus_symbols")) + len(rows(snapshot, "npc_templates"))
    if importer_key == "dialog_ou_index":
        return len(rows(snapshot, "dialog_outputs"))
    return 0


def build_import_sql(content_revision_key: str,
                     importer_key: str,
                     source_kind: str,
                     source: dict[str, object],
                     item_count: int,
                     import_status: str,
                     payload: dict[str, object]) -> list[str]:
    source_path = normalize_path(source.get("logical_path"))
    if not source_path:
        source_path = source_kind
    source_sha = source.get("sha256")
    return [
        "SET @mmo_content_revision_id=NULL;",
        "SET @mmo_content_build_import_id=NULL;",
        "SELECT content_revision_id INTO @mmo_content_revision_id FROM content_revisions WHERE content_revision_key="
        + sql_literal(content_revision_key) + " LIMIT 1;",
        "INSERT INTO mmo_server_content_build_imports(content_revision_id,importer_key,source_kind,source_logical_path,source_sha256,import_status,item_count,raw_payload) VALUES("
        + "@mmo_content_revision_id,"
        + sql_literal(importer_key) + ","
        + sql_literal(source_kind) + ","
        + sql_literal(source_path) + ","
        + sql_literal(source_sha) + ","
        + sql_literal(import_status) + ","
        + str(item_count) + ","
        + sql_json(payload)
        + ") ON DUPLICATE KEY UPDATE import_status=VALUES(import_status),item_count=VALUES(item_count),raw_payload=VALUES(raw_payload),imported_at=CURRENT_TIMESTAMP(6),updated_at=CURRENT_TIMESTAMP(6);",
        "SELECT content_build_import_id INTO @mmo_content_build_import_id FROM mmo_server_content_build_imports WHERE content_revision_id=@mmo_content_revision_id AND importer_key="
        + sql_literal(importer_key) + " AND source_logical_path=" + sql_literal(source_path)
        + " AND ((source_sha256 IS NULL AND " + sql_literal(source_sha) + " IS NULL) OR source_sha256=" + sql_literal(source_sha) + ") LIMIT 1;",
    ]


def build_sql(snapshot: dict[str, object], content_revision_key: str, import_status: str) -> str:
    lines = ["SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;"]

    zen_source = source_for(snapshot, "world_zen")
    lines += build_import_sql(content_revision_key, "zen_world_import", "world_zen", zen_source, count_items(snapshot, "zen_world_import"), import_status, {
        "schema": "mmo.content_build_import.v1",
        "tool": "import_server_content_build_snapshot.py",
        "section": "zen_entities",
    })
    for index, item in enumerate(rows(snapshot, "zen_entities")):
        world_name = safe_key(item.get("world_name"), "unknown")
        entity_kind = safe_key(item.get("entity_kind"), "other")
        entity_key = safe_key(item.get("entity_key") or item.get("name"), f"entity_{index}")
        lines.append(
            "INSERT INTO mmo_server_world_zen_entities(content_build_import_id,content_revision_id,world_name,entity_kind,entity_key,entity_name,pos_x,pos_y,pos_z,dir_x,dir_y,dir_z,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_id,"
            + sql_literal(world_name) + ","
            + sql_literal(entity_kind) + ","
            + sql_literal(entity_key) + ","
            + sql_literal(item.get("name") or entity_key) + ","
            + sql_number(item.get("pos_x")) + ","
            + sql_number(item.get("pos_y")) + ","
            + sql_number(item.get("pos_z")) + ","
            + sql_number(item.get("dir_x")) + ","
            + sql_number(item.get("dir_y")) + ","
            + sql_number(item.get("dir_z")) + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE entity_name=VALUES(entity_name),pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),pos_z=VALUES(pos_z),dir_x=VALUES(dir_x),dir_y=VALUES(dir_y),dir_z=VALUES(dir_z),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )

    dat_source = source_for(snapshot, "scripts_dat")
    lines += build_import_sql(content_revision_key, "daedalus_dat_index", "scripts_dat", dat_source, count_items(snapshot, "daedalus_dat_index"), import_status, {
        "schema": "mmo.content_build_import.v1",
        "tool": "import_server_content_build_snapshot.py",
        "section": "daedalus",
    })
    for index, item in enumerate(rows(snapshot, "daedalus_symbols")):
        symbol_name = safe_key(item.get("symbol_name") or item.get("name"), f"symbol_{index}")
        lines.append(
            "INSERT INTO mmo_server_daedalus_symbols(content_build_import_id,content_revision_id,symbol_name,symbol_kind,data_type,parent_symbol,ordinal,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_id,"
            + sql_literal(symbol_name) + ","
            + sql_literal(item.get("symbol_kind") or item.get("kind") or "unknown") + ","
            + sql_literal(item.get("data_type") or "") + ","
            + sql_literal(item.get("parent_symbol") or "") + ","
            + str(index) + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE symbol_kind=VALUES(symbol_kind),data_type=VALUES(data_type),parent_symbol=VALUES(parent_symbol),ordinal=VALUES(ordinal),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    for item in rows(snapshot, "npc_templates"):
        npc_instance = safe_key(item.get("npc_instance") or item.get("instance"), "unknown_npc")
        lines.append(
            "INSERT INTO mmo_server_daedalus_npc_templates(content_build_import_id,content_revision_id,npc_instance,display_name,guild,level_value,routine_symbol,perception_symbol,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_id,"
            + sql_literal(npc_instance) + ","
            + sql_literal(item.get("display_name") or item.get("name") or "") + ","
            + sql_literal(item.get("guild") or "") + ","
            + sql_int(item.get("level")) + ","
            + sql_literal(item.get("routine_symbol") or "") + ","
            + sql_literal(item.get("perception_symbol") or "") + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE display_name=VALUES(display_name),guild=VALUES(guild),level_value=VALUES(level_value),routine_symbol=VALUES(routine_symbol),perception_symbol=VALUES(perception_symbol),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )

    ou_source = source_for(snapshot, "dialog_ou")
    lines += build_import_sql(content_revision_key, "dialog_ou_index", "dialog_ou", ou_source, count_items(snapshot, "dialog_ou_index"), import_status, {
        "schema": "mmo.content_build_import.v1",
        "tool": "import_server_content_build_snapshot.py",
        "section": "dialog_outputs",
    })
    for index, item in enumerate(rows(snapshot, "dialog_outputs")):
        output_name = safe_key(item.get("output_name") or item.get("name"), f"output_{index}")
        lines.append(
            "INSERT INTO mmo_server_dialog_outputs(content_build_import_id,content_revision_id,output_name,text_value,audio_ref,speaker_symbol,target_symbol,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_id,"
            + sql_literal(output_name) + ","
            + sql_literal(item.get("text") or item.get("text_value") or "") + ","
            + sql_literal(item.get("audio_ref") or "") + ","
            + sql_literal(item.get("speaker_symbol") or "") + ","
            + sql_literal(item.get("target_symbol") or "") + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE text_value=VALUES(text_value),audio_ref=VALUES(audio_ref),speaker_symbol=VALUES(speaker_symbol),target_symbol=VALUES(target_symbol),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    lines.append("SELECT COUNT(*) AS content_build_import_count FROM mmo_server_content_build_imports;")
    return "\n".join(lines) + "\n"


def summarize(snapshot: dict[str, object]) -> dict[str, object]:
    zen_entities = rows(snapshot, "zen_entities")
    by_entity_kind: dict[str, int] = {}
    for item in zen_entities:
        kind = str(item.get("entity_kind") or "other")
        by_entity_kind[kind] = by_entity_kind.get(kind, 0) + 1
    return {
        "zen_entity_count": len(zen_entities),
        "zen_by_entity_kind": dict(sorted(by_entity_kind.items())),
        "daedalus_symbol_count": len(rows(snapshot, "daedalus_symbols")),
        "npc_template_count": len(rows(snapshot, "npc_templates")),
        "dialog_output_count": len(rows(snapshot, "dialog_outputs")),
    }


def run_mysql_file(target: Target, sql_path: Path, dry_run: bool) -> dict[str, object]:
    cmd = mysql_cmd(target)
    shown = redact_cmd(cmd)
    if dry_run:
        return {"status": "dry_run", "cmd": shown, "stdout": "", "stderr": "", "returncode": 0}
    proc = subprocess.run(cmd, input=sql_path.read_text(encoding="utf-8"), text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(ROOT))
    return {"status": "applied" if proc.returncode == 0 else "failed", "returncode": proc.returncode, "cmd": shown, "stdout": proc.stdout, "stderr": proc.stderr}


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate SQL for Step208 content build indexes from a parser snapshot JSON.")
    parser.add_argument("--snapshot", required=True, help="Parser snapshot JSON.")
    parser.add_argument("--content-revision-key", default="", help="Override snapshot content_revision_key.")
    parser.add_argument("--url", default="", help="mysql://user:password@host:port/database. If omitted, only artifacts are written.")
    parser.add_argument("--import-status", default="imported", choices=["imported", "partial", "failed", "skipped"])
    parser.add_argument("--output", default="runtime/step209_server_content_build_snapshot/content_build_snapshot_report.json")
    parser.add_argument("--sql-output", default="runtime/step209_server_content_build_snapshot/import_content_build_snapshot.sql")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    snapshot_path = Path(args.snapshot)
    if not snapshot_path.is_absolute():
        snapshot_path = ROOT / snapshot_path
    snapshot = load_snapshot(snapshot_path)
    content_revision_key = args.content_revision_key or str(snapshot.get("content_revision_key") or "")
    if not content_revision_key:
        raise SystemExit("content revision key missing; pass --content-revision-key or include content_revision_key in snapshot")

    report = {
        "schema": "mmo.server_content_build_snapshot_report.v1",
        "tool": "import_server_content_build_snapshot.py",
        "content_revision_key": content_revision_key,
        "snapshot_path": rel(snapshot_path),
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "summary": summarize(snapshot),
    }
    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = ROOT / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    sql_path = Path(args.sql_output)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path
    sql_path.parent.mkdir(parents=True, exist_ok=True)
    sql_path.write_text(build_sql(snapshot, content_revision_key, args.import_status), encoding="utf-8")

    result: dict[str, object] = {"status": "generated", "output": rel(output_path), "sql_output": rel(sql_path), "summary": report["summary"]}
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




