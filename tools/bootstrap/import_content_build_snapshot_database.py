#!/usr/bin/env python3
"""Generate/apply SQL for the standalone content-build database.

The input is a parser snapshot JSON. Today this can be a hand-written fixture;
later it should be emitted by real ZEN/DAT/OU importers. The tool writes only
to the content-build database, not to the runtime MMO database.
"""
from __future__ import annotations

import argparse
import json
import re
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
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
IDENT_RE = re.compile(r"^[A-Za-z0-9_]+$")


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


def mysql_cmd(target: Target, *, allow_missing: bool = False) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        if not allow_missing:
            raise RuntimeError("mysql executable not found in PATH")
        exe = "mysql"
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def redact_cmd(cmd: list[str]) -> list[str]:
    return ["-p***" if item.startswith("-p") and len(item) > 2 else item for item in cmd]


def mysql_identifier(value: str) -> str:
    if not IDENT_RE.fullmatch(value):
        raise ValueError("invalid MySQL identifier: " + value)
    return "`" + value + "`"


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


def sql_flag(value: object) -> str:
    return "1" if bool(value) else "0"


def normalize_path(value: object) -> str:
    return str(value or "").replace("\\", "/").lower().strip("/")


def safe_key(value: object, fallback: str) -> str:
    text = str(value or "").strip()
    return text if text else fallback


def safe_owner_kind(value: object) -> str:
    text = str(value or "npc").strip()
    return text if text in {"npc", "guild", "global", "other"} else "other"


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
        return len(rows(snapshot, "zen_entities")) + len(rows(snapshot, "waypoint_edges"))
    if importer_key == "daedalus_dat_index":
        return (
            len(rows(snapshot, "daedalus_symbols"))
            + len(rows(snapshot, "npc_templates"))
            + len(rows(snapshot, "item_templates"))
            + len(rows(snapshot, "routines"))
            + len(rows(snapshot, "perception_bindings"))
            + len(rows(snapshot, "dialog_infos"))
        )
    if importer_key == "dialog_ou_index":
        return len(rows(snapshot, "dialog_outputs"))
    return 0


def source_kind_for_file(kind: str) -> str:
    allowed = {"world_zen", "scripts_dat", "dialog_ou", "archive_vdf", "archive_mod", "texture", "mesh", "sound", "other"}
    return kind if kind in allowed else "other"


def source_kind_for_import(kind: str) -> str:
    allowed = {"world_zen", "scripts_dat", "dialog_ou", "archive_vdf", "archive_mod", "other"}
    return kind if kind in allowed else "other"


def build_revision_sql(snapshot: dict[str, object], content_revision_key: str, game_code: str, build_status: str) -> list[str]:
    manifest_hash = snapshot.get("manifest_hash")
    if manifest_hash is not None and not SHA256_RE.fullmatch(str(manifest_hash)):
        manifest_hash = None
    return [
        "INSERT INTO content_build_revisions(content_revision_key,game_code,manifest_hash,source_root_label,build_status,source_payload) VALUES("
        + sql_literal(content_revision_key) + ","
        + sql_literal(game_code) + ","
        + sql_literal(manifest_hash) + ","
        + sql_literal(snapshot.get("source_root_label") or "") + ","
        + sql_literal(build_status) + ","
        + sql_json({"schema": "mmo.content_build_snapshot.v1", "snapshot_keys": sorted(snapshot.keys())})
        + ") ON DUPLICATE KEY UPDATE game_code=VALUES(game_code),manifest_hash=VALUES(manifest_hash),source_root_label=VALUES(source_root_label),build_status=VALUES(build_status),source_payload=VALUES(source_payload),updated_at=CURRENT_TIMESTAMP(6);"
    ]


def build_source_file_sql(snapshot: dict[str, object], content_revision_key: str) -> list[str]:
    lines: list[str] = []
    sources = snapshot.get("sources")
    if not isinstance(sources, dict):
        return lines
    for source_key, source_value in sorted(sources.items()):
        if not isinstance(source_value, dict):
            continue
        logical_path = normalize_path(source_value.get("logical_path"))
        sha256 = str(source_value.get("sha256") or "")
        if not logical_path or not SHA256_RE.fullmatch(sha256):
            continue
        lines.append(
            "INSERT INTO content_build_files(content_revision_key,logical_path,source_kind,file_role,byte_size,sha256,required_for_server_authority,raw_payload) VALUES("
            + sql_literal(content_revision_key) + ","
            + sql_literal(logical_path) + ","
            + sql_literal(source_kind_for_file(str(source_key))) + ","
            + sql_literal(str(source_value.get("file_role") or source_key)) + ","
            + str(max(0, int(source_value.get("byte_size") or 0))) + ","
            + sql_literal(sha256) + ","
            + sql_flag(source_value.get("required_for_server_authority", True))
            + ","
            + sql_json(source_value)
            + ") ON DUPLICATE KEY UPDATE source_kind=VALUES(source_kind),file_role=VALUES(file_role),byte_size=VALUES(byte_size),sha256=VALUES(sha256),required_for_server_authority=VALUES(required_for_server_authority),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    return lines


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
    if source_sha is not None and not SHA256_RE.fullmatch(str(source_sha)):
        source_sha = None
    return [
        "SET @mmo_content_revision_key=" + sql_literal(content_revision_key) + ";",
        "SET @mmo_content_build_import_id=NULL;",
        "INSERT INTO content_build_imports(content_revision_key,importer_key,source_kind,source_logical_path,source_sha256,import_status,item_count,raw_payload) VALUES("
        + "@mmo_content_revision_key,"
        + sql_literal(importer_key) + ","
        + sql_literal(source_kind_for_import(source_kind)) + ","
        + sql_literal(source_path) + ","
        + sql_literal(source_sha) + ","
        + sql_literal(import_status) + ","
        + str(item_count) + ","
        + sql_json(payload)
        + ") ON DUPLICATE KEY UPDATE import_status=VALUES(import_status),item_count=VALUES(item_count),raw_payload=VALUES(raw_payload),imported_at=CURRENT_TIMESTAMP(6),updated_at=CURRENT_TIMESTAMP(6);",
        "SELECT content_build_import_id INTO @mmo_content_build_import_id FROM content_build_imports WHERE content_revision_key=@mmo_content_revision_key AND importer_key="
        + sql_literal(importer_key) + " AND source_logical_path=" + sql_literal(source_path)
        + " AND ((source_sha256 IS NULL AND " + sql_literal(source_sha) + " IS NULL) OR source_sha256=" + sql_literal(source_sha) + ") LIMIT 1;",
    ]


def build_zen_sql(snapshot: dict[str, object], content_revision_key: str, import_status: str) -> list[str]:
    lines = build_import_sql(content_revision_key, "zen_world_import", "world_zen", source_for(snapshot, "world_zen"), count_items(snapshot, "zen_world_import"), import_status, {
        "schema": "mmo.content_build_import.v2",
        "tool": "import_content_build_snapshot_database.py",
        "section": "zen",
    })
    allowed_kinds = {"world", "waypoint", "freepoint", "vob", "trigger", "mover", "spawn", "sound", "light", "other"}
    for index, item in enumerate(rows(snapshot, "zen_entities")):
        world_name = safe_key(item.get("world_name"), "unknown")
        entity_kind = safe_key(item.get("entity_kind"), "other")
        if entity_kind not in allowed_kinds:
            entity_kind = "other"
        entity_key = safe_key(item.get("entity_key") or item.get("name"), f"entity_{index}")
        lines.append(
            "INSERT INTO world_zen_entities(content_build_import_id,content_revision_key,world_name,entity_kind,entity_key,entity_name,pos_x,pos_y,pos_z,dir_x,dir_y,dir_z,radius_value,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
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
            + sql_number(item.get("radius")) + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE entity_name=VALUES(entity_name),pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),pos_z=VALUES(pos_z),dir_x=VALUES(dir_x),dir_y=VALUES(dir_y),dir_z=VALUES(dir_z),radius_value=VALUES(radius_value),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    for index, item in enumerate(rows(snapshot, "waypoint_edges")):
        world_name = safe_key(item.get("world_name"), "unknown")
        from_key = safe_key(item.get("from_waypoint_key") or item.get("from"), f"from_{index}")
        to_key = safe_key(item.get("to_waypoint_key") or item.get("to"), f"to_{index}")
        lines.append(
            "INSERT INTO world_waypoint_edges(content_build_import_id,content_revision_key,world_name,from_waypoint_key,to_waypoint_key,travel_cost,edge_flags,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
            + sql_literal(world_name) + ","
            + sql_literal(from_key) + ","
            + sql_literal(to_key) + ","
            + sql_number(item.get("travel_cost") if item.get("travel_cost") is not None else 1) + ","
            + sql_literal(item.get("edge_flags") or "") + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE travel_cost=VALUES(travel_cost),edge_flags=VALUES(edge_flags),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    return lines


def build_daedalus_sql(snapshot: dict[str, object], content_revision_key: str, import_status: str) -> list[str]:
    lines = build_import_sql(content_revision_key, "daedalus_dat_index", "scripts_dat", source_for(snapshot, "scripts_dat"), count_items(snapshot, "daedalus_dat_index"), import_status, {
        "schema": "mmo.content_build_import.v2",
        "tool": "import_content_build_snapshot_database.py",
        "section": "daedalus",
    })
    for index, item in enumerate(rows(snapshot, "daedalus_symbols")):
        symbol_name = safe_key(item.get("symbol_name") or item.get("name"), f"symbol_{index}")
        lines.append(
            "INSERT INTO daedalus_symbols(content_build_import_id,content_revision_key,symbol_name,symbol_kind,data_type,parent_symbol,ordinal,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
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
            "INSERT INTO daedalus_npc_templates(content_build_import_id,content_revision_key,npc_instance,display_name,guild,level_value,routine_symbol,perception_symbol,fight_tactic,voice_symbol,attributes_payload,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
            + sql_literal(npc_instance) + ","
            + sql_literal(item.get("display_name") or item.get("name") or "") + ","
            + sql_literal(item.get("guild") or "") + ","
            + sql_int(item.get("level")) + ","
            + sql_literal(item.get("routine_symbol") or "") + ","
            + sql_literal(item.get("perception_symbol") or "") + ","
            + sql_literal(item.get("fight_tactic") or "") + ","
            + sql_literal(item.get("voice_symbol") or "") + ","
            + sql_json(item.get("attributes") if isinstance(item.get("attributes"), dict) else {}) + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE display_name=VALUES(display_name),guild=VALUES(guild),level_value=VALUES(level_value),routine_symbol=VALUES(routine_symbol),perception_symbol=VALUES(perception_symbol),fight_tactic=VALUES(fight_tactic),voice_symbol=VALUES(voice_symbol),attributes_payload=VALUES(attributes_payload),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    for item in rows(snapshot, "item_templates"):
        item_instance = safe_key(item.get("item_instance") or item.get("instance"), "unknown_item")
        lines.append(
            "INSERT INTO daedalus_item_templates(content_build_import_id,content_revision_key,item_instance,display_name,item_category,main_flag,flags_value,value_amount,damage_total,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
            + sql_literal(item_instance) + ","
            + sql_literal(item.get("display_name") or item.get("name") or "") + ","
            + sql_literal(item.get("item_category") or item.get("category") or "") + ","
            + sql_int(item.get("main_flag")) + ","
            + sql_int(item.get("flags_value") or item.get("flags")) + ","
            + sql_int(item.get("value_amount") or item.get("value")) + ","
            + sql_int(item.get("damage_total")) + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE display_name=VALUES(display_name),item_category=VALUES(item_category),main_flag=VALUES(main_flag),flags_value=VALUES(flags_value),value_amount=VALUES(value_amount),damage_total=VALUES(damage_total),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    for index, item in enumerate(rows(snapshot, "routines")):
        npc_instance = safe_key(item.get("npc_instance") or item.get("instance"), "unknown_npc")
        routine_symbol = safe_key(item.get("routine_symbol") or item.get("symbol"), f"routine_{index}")
        lines.append(
            "INSERT INTO daedalus_routines(content_build_import_id,content_revision_key,npc_instance,routine_symbol,day_minute_start,day_minute_end,target_point_key,action_symbol,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
            + sql_literal(npc_instance) + ","
            + sql_literal(routine_symbol) + ","
            + sql_int(item.get("day_minute_start")) + ","
            + sql_int(item.get("day_minute_end")) + ","
            + sql_literal(item.get("target_point_key") or item.get("waypoint") or "") + ","
            + sql_literal(item.get("action_symbol") or "") + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE day_minute_end=VALUES(day_minute_end),target_point_key=VALUES(target_point_key),action_symbol=VALUES(action_symbol),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    for index, item in enumerate(rows(snapshot, "perception_bindings")):
        perception_kind = safe_key(item.get("perception_kind") or item.get("kind"), f"perception_{index}")
        function_symbol = safe_key(item.get("function_symbol") or item.get("function"), "")
        if not function_symbol:
            continue
        lines.append(
            "INSERT INTO daedalus_perception_bindings(content_build_import_id,content_revision_key,owner_symbol,owner_kind,perception_kind,function_symbol,priority_value,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
            + sql_literal(item.get("owner_symbol") or item.get("npc_instance") or "") + ","
            + sql_literal(safe_owner_kind(item.get("owner_kind"))) + ","
            + sql_literal(perception_kind) + ","
            + sql_literal(function_symbol) + ","
            + sql_int(item.get("priority") or 0) + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE priority_value=VALUES(priority_value),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    for index, item in enumerate(rows(snapshot, "dialog_infos")):
        info_symbol = safe_key(item.get("info_symbol") or item.get("symbol"), f"info_{index}")
        lines.append(
            "INSERT INTO dialog_infos(content_build_import_id,content_revision_key,info_symbol,npc_instance,condition_symbol,information_symbol,permanent_flag,important_flag,trade_flag,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
            + sql_literal(info_symbol) + ","
            + sql_literal(item.get("npc_instance") or "") + ","
            + sql_literal(item.get("condition_symbol") or "") + ","
            + sql_literal(item.get("information_symbol") or "") + ","
            + sql_flag(item.get("permanent")) + ","
            + sql_flag(item.get("important")) + ","
            + sql_flag(item.get("trade")) + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE npc_instance=VALUES(npc_instance),condition_symbol=VALUES(condition_symbol),information_symbol=VALUES(information_symbol),permanent_flag=VALUES(permanent_flag),important_flag=VALUES(important_flag),trade_flag=VALUES(trade_flag),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    return lines


def build_ou_sql(snapshot: dict[str, object], content_revision_key: str, import_status: str) -> list[str]:
    lines = build_import_sql(content_revision_key, "dialog_ou_index", "dialog_ou", source_for(snapshot, "dialog_ou"), count_items(snapshot, "dialog_ou_index"), import_status, {
        "schema": "mmo.content_build_import.v2",
        "tool": "import_content_build_snapshot_database.py",
        "section": "dialog_outputs",
    })
    for index, item in enumerate(rows(snapshot, "dialog_outputs")):
        output_name = safe_key(item.get("output_name") or item.get("name"), f"output_{index}")
        lines.append(
            "INSERT INTO dialog_outputs(content_build_import_id,content_revision_key,output_name,text_value,audio_ref,speaker_symbol,target_symbol,raw_payload) VALUES("
            + "@mmo_content_build_import_id,@mmo_content_revision_key,"
            + sql_literal(output_name) + ","
            + sql_literal(item.get("text") or item.get("text_value") or "") + ","
            + sql_literal(item.get("audio_ref") or "") + ","
            + sql_literal(item.get("speaker_symbol") or "") + ","
            + sql_literal(item.get("target_symbol") or "") + ","
            + sql_json(item)
            + ") ON DUPLICATE KEY UPDATE text_value=VALUES(text_value),audio_ref=VALUES(audio_ref),speaker_symbol=VALUES(speaker_symbol),target_symbol=VALUES(target_symbol),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);"
        )
    return lines


def build_parser_error_sql(snapshot: dict[str, object], content_revision_key: str) -> list[str]:
    lines: list[str] = []
    for item in rows(snapshot, "parser_errors"):
        severity = str(item.get("severity") or "warning")
        if severity not in {"info", "warning", "error", "fatal"}:
            severity = "warning"
        lines.append(
            "INSERT INTO content_build_parser_errors(content_revision_key,severity,error_scope,error_code,source_logical_path,message_text,raw_payload) VALUES("
            + sql_literal(content_revision_key) + ","
            + sql_literal(severity) + ","
            + sql_literal(item.get("error_scope") or "parser") + ","
            + sql_literal(item.get("error_code") or "") + ","
            + sql_literal(normalize_path(item.get("source_logical_path"))) + ","
            + sql_literal(item.get("message_text") or item.get("message") or "") + ","
            + sql_json(item)
            + ");"
        )
    return lines


def build_sql(snapshot: dict[str, object], content_revision_key: str, game_code: str, content_db_name: str, build_status: str, import_status: str) -> str:
    db_name = mysql_identifier(content_db_name)
    lines = [
        "SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci;",
        "CREATE DATABASE IF NOT EXISTS " + db_name + " DEFAULT CHARACTER SET utf8mb4 DEFAULT COLLATE utf8mb4_0900_ai_ci;",
        "USE " + db_name + ";",
    ]
    lines += build_revision_sql(snapshot, content_revision_key, game_code, build_status)
    lines += build_source_file_sql(snapshot, content_revision_key)
    lines += build_zen_sql(snapshot, content_revision_key, import_status)
    lines += build_daedalus_sql(snapshot, content_revision_key, import_status)
    lines += build_ou_sql(snapshot, content_revision_key, import_status)
    lines.append("DELETE FROM content_build_parser_errors WHERE content_revision_key=" + sql_literal(content_revision_key) + ";")
    lines += build_parser_error_sql(snapshot, content_revision_key)
    lines.append("SELECT COUNT(*) AS content_build_import_count FROM content_build_imports;")
    return "\n".join(lines) + "\n"


def summarize(snapshot: dict[str, object]) -> dict[str, object]:
    by_entity_kind: dict[str, int] = {}
    for item in rows(snapshot, "zen_entities"):
        kind = str(item.get("entity_kind") or "other")
        by_entity_kind[kind] = by_entity_kind.get(kind, 0) + 1
    return {
        "source_count": len(snapshot.get("sources", {})) if isinstance(snapshot.get("sources"), dict) else 0,
        "zen_entity_count": len(rows(snapshot, "zen_entities")),
        "zen_by_entity_kind": dict(sorted(by_entity_kind.items())),
        "waypoint_edge_count": len(rows(snapshot, "waypoint_edges")),
        "daedalus_symbol_count": len(rows(snapshot, "daedalus_symbols")),
        "npc_template_count": len(rows(snapshot, "npc_templates")),
        "item_template_count": len(rows(snapshot, "item_templates")),
        "routine_count": len(rows(snapshot, "routines")),
        "perception_binding_count": len(rows(snapshot, "perception_bindings")),
        "dialog_info_count": len(rows(snapshot, "dialog_infos")),
        "dialog_output_count": len(rows(snapshot, "dialog_outputs")),
        "parser_error_count": len(rows(snapshot, "parser_errors")),
    }


def run_mysql_file(target: Target, sql_path: Path, dry_run: bool) -> dict[str, object]:
    cmd = mysql_cmd(target, allow_missing=dry_run)
    shown = redact_cmd(cmd)
    if dry_run:
        return {"status": "dry_run", "cmd": shown, "stdout": "", "stderr": "", "returncode": 0}
    proc = subprocess.run(cmd, input=sql_path.read_text(encoding="utf-8"), text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(ROOT))
    return {"status": "applied" if proc.returncode == 0 else "failed", "returncode": proc.returncode, "cmd": shown, "stdout": proc.stdout, "stderr": proc.stderr}


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate/apply SQL for standalone content-build database from parser snapshot JSON.")
    parser.add_argument("--snapshot", required=True, help="Parser snapshot JSON.")
    parser.add_argument("--content-db-name", default="mmo_content_build")
    parser.add_argument("--content-revision-key", default="", help="Override snapshot content_revision_key.")
    parser.add_argument("--game-code", default="", help="Override snapshot game_code.")
    parser.add_argument("--url", default="", help="mysql://user:password@host:port/database. If omitted, only artifacts are written.")
    parser.add_argument("--build-status", default="ready", choices=["draft", "building", "ready", "failed", "retired"])
    parser.add_argument("--import-status", default="imported", choices=["imported", "partial", "failed", "skipped"])
    parser.add_argument("--output", default="runtime/step212_content_build_database_import/content_build_database_report.json")
    parser.add_argument("--sql-output", default="runtime/step212_content_build_database_import/import_content_build_database.sql")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if not IDENT_RE.fullmatch(args.content_db_name):
        raise SystemExit("invalid --content-db-name; use only letters, digits and underscore")

    snapshot_path = Path(args.snapshot)
    if not snapshot_path.is_absolute():
        snapshot_path = ROOT / snapshot_path
    snapshot = load_snapshot(snapshot_path)
    content_revision_key = args.content_revision_key or str(snapshot.get("content_revision_key") or "")
    if not content_revision_key:
        raise SystemExit("content revision key missing; pass --content-revision-key or include content_revision_key in snapshot")
    game_code = args.game_code or str(snapshot.get("game_code") or "gothic2-notr")

    report = {
        "schema": "mmo.content_build_database_snapshot_report.v1",
        "tool": "import_content_build_snapshot_database.py",
        "content_db_name": args.content_db_name,
        "content_revision_key": content_revision_key,
        "game_code": game_code,
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
    sql_path.write_text(build_sql(snapshot, content_revision_key, game_code, args.content_db_name, args.build_status, args.import_status), encoding="utf-8")

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


