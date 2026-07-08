#!/usr/bin/env python3
"""Check Step211 standalone MMO content-build database objects and data."""
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


def mysql_cmd(target: Target) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


def mysql_identifier(value: str) -> str:
    if not IDENT_RE.fullmatch(value):
        raise ValueError("invalid MySQL identifier: " + value)
    return "`" + value + "`"


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(ROOT))
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def scalar_int(target: Target, sql: str) -> int:
    output = run_mysql(target, sql).splitlines()
    if not output:
        return 0
    try:
        return int(output[-1].strip() or "0")
    except ValueError:
        return 0


def json_value(target: Target, sql: str) -> object:
    output = run_mysql(target, sql)
    if not output:
        return None
    return json.loads(output.splitlines()[-1])


def object_exists(target: Target, content_db_name: str, kind: str, name: str) -> int:
    if kind == "table":
        return scalar_int(target, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=" + sql_literal(content_db_name) + " AND table_name=" + sql_literal(name) + ";")
    if kind == "view":
        return scalar_int(target, "SELECT COUNT(*) FROM information_schema.views WHERE table_schema=" + sql_literal(content_db_name) + " AND table_name=" + sql_literal(name) + ";")
    raise ValueError(kind)


def count_table(target: Target, content_db_name: str, table: str) -> int:
    return scalar_int(target, "SELECT COUNT(*) FROM " + mysql_identifier(content_db_name) + "." + mysql_identifier(table) + ";")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Step211 standalone MMO content-build database.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--content-db-name", default="mmo_content_build")
    parser.add_argument("--expect-zen", action="store_true")
    parser.add_argument("--expect-daedalus", action="store_true")
    parser.add_argument("--expect-dialog-outputs", action="store_true")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    if not IDENT_RE.fullmatch(args.content_db_name):
        raise SystemExit("invalid --content-db-name; use only letters, digits and underscore")

    target = parse_mysql_url(args.url)
    db = mysql_identifier(args.content_db_name)
    checks: list[dict[str, object]] = []

    schema_count = scalar_int(target, "SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name=" + sql_literal(args.content_db_name) + ";")
    checks.append({"name": args.content_db_name, "kind": "schema", "status": "passed" if schema_count == 1 else "failed", "count": schema_count})

    for kind, name in (
        ("table", "content_build_schema_versions"),
        ("table", "content_build_revisions"),
        ("table", "content_build_files"),
        ("table", "content_build_imports"),
        ("table", "content_build_parser_errors"),
        ("table", "world_zen_entities"),
        ("table", "world_waypoint_edges"),
        ("table", "daedalus_symbols"),
        ("table", "daedalus_npc_templates"),
        ("table", "daedalus_item_templates"),
        ("table", "daedalus_routines"),
        ("table", "daedalus_perception_bindings"),
        ("table", "dialog_outputs"),
        ("table", "dialog_infos"),
        ("table", "content_build_runtime_exports"),
        ("view", "v_content_build_imports"),
        ("view", "v_world_zen_entities"),
        ("view", "v_world_waypoint_edges"),
        ("view", "v_daedalus_symbols"),
        ("view", "v_daedalus_npc_templates"),
        ("view", "v_daedalus_item_templates"),
        ("view", "v_daedalus_routines"),
        ("view", "v_daedalus_perception_bindings"),
        ("view", "v_dialog_outputs"),
        ("view", "v_dialog_infos"),
        ("view", "v_content_build_health"),
    ):
        count = object_exists(target, args.content_db_name, kind, name)
        checks.append({"name": name, "kind": kind, "status": "passed" if count == 1 else "failed", "count": count})

    migration_count = scalar_int(target, "SELECT COUNT(*) FROM " + db + ".content_build_schema_versions WHERE migration_key='server/sql/step211_content_build_database.sql';")
    checks.append({"name": "step211_schema_version_marker", "kind": "migration", "status": "passed" if migration_count == 1 else "failed", "count": migration_count})

    counts = {
        "revisions": count_table(target, args.content_db_name, "content_build_revisions"),
        "files": count_table(target, args.content_db_name, "content_build_files"),
        "build_imports": count_table(target, args.content_db_name, "content_build_imports"),
        "parser_errors": count_table(target, args.content_db_name, "content_build_parser_errors"),
        "zen_entities": count_table(target, args.content_db_name, "world_zen_entities"),
        "waypoint_edges": count_table(target, args.content_db_name, "world_waypoint_edges"),
        "daedalus_symbols": count_table(target, args.content_db_name, "daedalus_symbols"),
        "npc_templates": count_table(target, args.content_db_name, "daedalus_npc_templates"),
        "item_templates": count_table(target, args.content_db_name, "daedalus_item_templates"),
        "routines": count_table(target, args.content_db_name, "daedalus_routines"),
        "perception_bindings": count_table(target, args.content_db_name, "daedalus_perception_bindings"),
        "dialog_outputs": count_table(target, args.content_db_name, "dialog_outputs"),
        "dialog_infos": count_table(target, args.content_db_name, "dialog_infos"),
        "runtime_exports": count_table(target, args.content_db_name, "content_build_runtime_exports"),
    }
    checks.append({"name": "content_build_counts", "kind": "data", "status": "passed", "details": counts})

    health = json_value(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'game_code',game_code,"
        "'content_revision_key',content_revision_key,"
        "'build_status',build_status,"
        "'build_import_count',build_import_count,"
        "'imported_count',imported_count,"
        "'failed_count',failed_count,"
        "'file_count',file_count,"
        "'required_file_count',required_file_count,"
        "'blocking_error_count',blocking_error_count,"
        "'zen_entity_count',zen_entity_count,"
        "'waypoint_count',waypoint_count,"
        "'freepoint_count',freepoint_count,"
        "'vob_count',vob_count,"
        "'waypoint_edge_count',waypoint_edge_count,"
        "'daedalus_symbol_count',daedalus_symbol_count,"
        "'npc_template_count',npc_template_count,"
        "'item_template_count',item_template_count,"
        "'routine_count',routine_count,"
        "'perception_binding_count',perception_binding_count,"
        "'dialog_output_count',dialog_output_count,"
        "'dialog_info_count',dialog_info_count"
        ")),JSON_ARRAY()) FROM " + db + ".v_content_build_health;",
    )
    checks.append({"name": "content_build_health", "kind": "data", "status": "passed", "details": health})

    if args.expect_zen:
        checks.append({"name": "expected_zen_entities", "kind": "data_expectation", "status": "passed" if counts["zen_entities"] > 0 else "failed", "count": counts["zen_entities"]})
    if args.expect_daedalus:
        daedalus_count = counts["daedalus_symbols"] + counts["npc_templates"] + counts["item_templates"] + counts["routines"] + counts["perception_bindings"] + counts["dialog_infos"]
        checks.append({"name": "expected_daedalus_index", "kind": "data_expectation", "status": "passed" if daedalus_count > 0 else "failed", "count": daedalus_count})
    if args.expect_dialog_outputs:
        checks.append({"name": "expected_dialog_outputs", "kind": "data_expectation", "status": "passed" if counts["dialog_outputs"] > 0 else "failed", "count": counts["dialog_outputs"]})

    failed = [item for item in checks if item["status"] == "failed"]
    report = {"tool": "check_mmo_step211_content_build_database.py", "status": "failed" if failed else "passed", "checked_at": datetime.now(timezone.utc).isoformat(), "content_db_name": args.content_db_name, "checks": checks}
    if args.output:
        out = Path(args.output)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("artifact=" + str(out))
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())




