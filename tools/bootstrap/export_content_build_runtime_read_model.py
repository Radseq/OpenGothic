#!/usr/bin/env python3
"""Export a content-build revision into a runtime/server-cache read model JSON."""
from __future__ import annotations

import argparse
import hashlib
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

_MYSQL_CLI_TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(_MYSQL_CLI_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_MYSQL_CLI_TOOLS_DIR))
try:
    from _mysql_cli import resolve_mysql_exe
except Exception:  # pragma: no cover - fallback for standalone patch bundles.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[2]
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
    return Target(parsed.hostname or "localhost", parsed.port or 3306, unquote(parsed.username or ""), unquote(parsed.password or ""), (parsed.path or "/").lstrip("/"))


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


def mysql_cmd(target: Target) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def redact_cmd(cmd: list[str]) -> list[str]:
    return ["-p***" if item.startswith("-p") and len(item) > 2 else item for item in cmd]


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(ROOT))
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"mysql exited with status {proc.returncode}")
    return proc.stdout


def json_rows(target: Target, sql: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in run_mysql(target, sql).splitlines():
        line = line.strip()
        if not line:
            continue
        value = json.loads(line)
        if isinstance(value, dict):
            rows.append(value)
    return rows


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


def rel(path: Path | None) -> str:
    if path is None:
        return ""
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def snapshot_read_model(snapshot: dict[str, object], content_revision_key: str, game_code: str) -> dict[str, object]:
    return {
        "content_revision_key": content_revision_key,
        "game_code": game_code,
        "world_zen_entities": rows(snapshot, "zen_entities"),
        "waypoint_edges": rows(snapshot, "waypoint_edges"),
        "npc_templates": rows(snapshot, "npc_templates"),
        "item_templates": rows(snapshot, "item_templates"),
        "routines": rows(snapshot, "routines"),
        "perception_bindings": rows(snapshot, "perception_bindings"),
        "dialog_infos": rows(snapshot, "dialog_infos"),
        "dialog_outputs": rows(snapshot, "dialog_outputs"),
    }


def db_select_json(view: str, fields: list[str], order_by: str, content_db_name: str, content_revision_key: str) -> str:
    pairs = ",".join(sql_literal(field) + "," + field for field in fields)
    return (
        "SELECT JSON_OBJECT(" + pairs + ") FROM " + mysql_identifier(content_db_name) + "." + mysql_identifier(view)
        + " WHERE content_revision_key=" + sql_literal(content_revision_key)
        + " ORDER BY " + order_by + ";"
    )


def db_read_model(target: Target, content_db_name: str, content_revision_key: str, game_code: str) -> dict[str, object]:
    sections: dict[str, tuple[str, list[str], str]] = {
        "world_zen_entities": ("v_world_zen_entities", ["world_name", "entity_kind", "entity_key", "entity_name", "pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z", "radius_value", "raw_payload"], "world_name, entity_kind, entity_key"),
        "waypoint_edges": ("v_world_waypoint_edges", ["world_name", "from_waypoint_key", "to_waypoint_key", "travel_cost", "edge_flags", "raw_payload"], "world_name, from_waypoint_key, to_waypoint_key"),
        "npc_templates": ("v_daedalus_npc_templates", ["npc_instance", "display_name", "guild", "level_value", "routine_symbol", "perception_symbol", "fight_tactic", "voice_symbol", "attributes_payload", "raw_payload"], "npc_instance"),
        "item_templates": ("v_daedalus_item_templates", ["item_instance", "display_name", "item_category", "main_flag", "flags_value", "value_amount", "damage_total", "raw_payload"], "item_instance"),
        "routines": ("v_daedalus_routines", ["npc_instance", "routine_symbol", "day_minute_start", "day_minute_end", "target_point_key", "action_symbol", "raw_payload"], "npc_instance, routine_symbol"),
        "perception_bindings": ("v_daedalus_perception_bindings", ["owner_symbol", "owner_kind", "perception_kind", "function_symbol", "priority_value", "raw_payload"], "owner_symbol, perception_kind, function_symbol"),
        "dialog_infos": ("v_dialog_infos", ["info_symbol", "npc_instance", "condition_symbol", "information_symbol", "permanent_flag", "important_flag", "trade_flag", "raw_payload"], "info_symbol"),
        "dialog_outputs": ("v_dialog_outputs", ["output_name", "text_value", "audio_ref", "speaker_symbol", "target_symbol", "raw_payload"], "output_name"),
    }
    model: dict[str, object] = {"content_revision_key": content_revision_key, "game_code": game_code}
    for key, (view, fields, order_by) in sections.items():
        model[key] = json_rows(target, db_select_json(view, fields, order_by, content_db_name, content_revision_key))
    return model


def summarize(model: dict[str, object]) -> dict[str, object]:
    return {
        "world_zen_entity_count": len(model.get("world_zen_entities", [])) if isinstance(model.get("world_zen_entities"), list) else 0,
        "waypoint_edge_count": len(model.get("waypoint_edges", [])) if isinstance(model.get("waypoint_edges"), list) else 0,
        "npc_template_count": len(model.get("npc_templates", [])) if isinstance(model.get("npc_templates"), list) else 0,
        "item_template_count": len(model.get("item_templates", [])) if isinstance(model.get("item_templates"), list) else 0,
        "routine_count": len(model.get("routines", [])) if isinstance(model.get("routines"), list) else 0,
        "perception_binding_count": len(model.get("perception_bindings", [])) if isinstance(model.get("perception_bindings"), list) else 0,
        "dialog_info_count": len(model.get("dialog_infos", [])) if isinstance(model.get("dialog_infos"), list) else 0,
        "dialog_output_count": len(model.get("dialog_outputs", [])) if isinstance(model.get("dialog_outputs"), list) else 0,
    }


def canonical_sha256(value: object) -> str:
    payload = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def build_export_sql(content_db_name: str,
                     content_revision_key: str,
                     export_kind: str,
                     target_runtime_schema: str,
                     payload_sha256: str,
                     marker_payload: dict[str, object]) -> str:
    db = mysql_identifier(content_db_name)
    return (
        "USE " + db + ";\n"
        "INSERT INTO content_build_runtime_exports(content_revision_key,export_kind,target_runtime_schema,export_status,payload_sha256,raw_payload) VALUES("
        + sql_literal(content_revision_key) + ","
        + sql_literal(export_kind) + ","
        + sql_literal(target_runtime_schema) + ","
        + "'generated',"
        + sql_literal(payload_sha256) + ","
        + sql_json(marker_payload)
        + ") ON DUPLICATE KEY UPDATE export_status=VALUES(export_status),payload_sha256=VALUES(payload_sha256),raw_payload=VALUES(raw_payload),updated_at=CURRENT_TIMESTAMP(6);\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Export content-build parser output into a runtime/server-cache read model JSON.")
    parser.add_argument("--snapshot", default="", help="Optional parser snapshot JSON. If omitted, --url is used.")
    parser.add_argument("--url", default="", help="Optional mysql://user:password@host:port/database for DB-backed export.")
    parser.add_argument("--content-db-name", default="mmo_content_build")
    parser.add_argument("--content-revision-key", default="", help="Required for DB export; defaults to snapshot content_revision_key for snapshot export.")
    parser.add_argument("--game-code", default="", help="Defaults to snapshot game_code or gothic2-notr.")
    parser.add_argument("--target-runtime-schema", default="server_cache")
    parser.add_argument("--export-kind", default="server_cache_read_model_json_v1")
    parser.add_argument("--output", default="runtime/content_build/runtime_read_model.json")
    parser.add_argument("--sql-output", default="runtime/content_build/register_runtime_read_model_export.sql")
    parser.add_argument("--apply-marker", action="store_true", help="Apply the content_build_runtime_exports marker SQL. Requires --url.")
    args = parser.parse_args()

    if not IDENT_RE.fullmatch(args.content_db_name):
        raise SystemExit("invalid --content-db-name; use only letters, digits and underscore")
    if args.apply_marker and not args.url:
        raise SystemExit("--apply-marker requires --url")

    snapshot: dict[str, object] | None = None
    snapshot_path: Path | None = None
    if args.snapshot:
        snapshot_path = Path(args.snapshot)
        if not snapshot_path.is_absolute():
            snapshot_path = ROOT / snapshot_path
        snapshot = load_snapshot(snapshot_path)

    content_revision_key = args.content_revision_key or (str(snapshot.get("content_revision_key") or "") if snapshot else "")
    if not content_revision_key:
        raise SystemExit("content revision key missing; pass --content-revision-key or include it in --snapshot")
    game_code = args.game_code or (str(snapshot.get("game_code") or "gothic2-notr") if snapshot else "gothic2-notr")

    if snapshot is not None:
        model = snapshot_read_model(snapshot, content_revision_key, game_code)
        source_kind = "snapshot"
    elif args.url:
        model = db_read_model(parse_mysql_url(args.url), args.content_db_name, content_revision_key, game_code)
        source_kind = "content_build_db"
    else:
        raise SystemExit("pass --snapshot or --url")

    generated_at = datetime.now(timezone.utc).isoformat()
    summary = summarize(model)
    payload_for_hash = {
        "schema": "mmo.content_build_runtime_read_model.v1",
        "content_revision_key": content_revision_key,
        "game_code": game_code,
        "sections": {key: model[key] for key in ("world_zen_entities", "waypoint_edges", "npc_templates", "item_templates", "routines", "perception_bindings", "dialog_infos", "dialog_outputs")},
    }
    payload_sha256 = canonical_sha256(payload_for_hash)
    out_model = {
        "schema": "mmo.content_build_runtime_read_model.v1",
        "tool": "export_content_build_runtime_read_model.py",
        "generated_at": generated_at,
        "source_kind": source_kind,
        "snapshot_path": rel(snapshot_path),
        "content_revision_key": content_revision_key,
        "game_code": game_code,
        "payload_sha256": payload_sha256,
        "summary": summary,
        **{key: model[key] for key in ("world_zen_entities", "waypoint_edges", "npc_templates", "item_templates", "routines", "perception_bindings", "dialog_infos", "dialog_outputs")},
    }

    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = ROOT / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(out_model, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    marker_payload = {
        "schema": "mmo.content_build_runtime_export_marker.v1",
        "export_kind": args.export_kind,
        "target_runtime_schema": args.target_runtime_schema,
        "artifact_path": rel(output_path),
        "source_kind": source_kind,
        "snapshot_path": rel(snapshot_path),
        "summary": summary,
    }
    sql = build_export_sql(args.content_db_name, content_revision_key, args.export_kind, args.target_runtime_schema, payload_sha256, marker_payload)
    sql_path = Path(args.sql_output)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path
    sql_path.parent.mkdir(parents=True, exist_ok=True)
    sql_path.write_text(sql, encoding="utf-8")

    result: dict[str, object] = {
        "status": "generated",
        "output": rel(output_path),
        "sql_output": rel(sql_path),
        "content_revision_key": content_revision_key,
        "payload_sha256": payload_sha256,
        "summary": summary,
    }
    if args.apply_marker:
        target = parse_mysql_url(args.url)
        run_mysql(target, sql)
        result["status"] = "applied"
        result["mysql_cmd"] = redact_cmd(mysql_cmd(target))
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
