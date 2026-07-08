#!/usr/bin/env python3
"""Summarize local and DB-backed standalone MMO content-build database state."""
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

ROOT = Path(__file__).resolve().parents[1]
IDENT_RE = re.compile(r"^[A-Za-z0-9_]+$")

try:
    from _mysql_cli import resolve_mysql_exe
except Exception:  # pragma: no cover - fallback for standalone patch bundles.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


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


def mysql_cmd(target: Target) -> list[str]:
    exe = resolve_mysql_exe()
    if exe is None:
        raise RuntimeError("mysql executable not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


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


def json_query(target: Target, sql: str) -> object:
    output = run_mysql(target, sql)
    if not output:
        return None
    return json.loads(output.splitlines()[-1])


def read_local_report(path: Path | None) -> dict[str, object] | None:
    if path is None:
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("content build database report JSON must be an object")
    return {"path": str(path), "content_db_name": data.get("content_db_name"), "content_revision_key": data.get("content_revision_key"), "summary": data.get("summary")}


def db_report(target: Target, content_db_name: str, limit: int) -> dict[str, object]:
    db = mysql_identifier(content_db_name)
    table_count = scalar_int(target, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='" + content_db_name.replace("'", "''") + "' AND table_name='content_build_imports';")
    if table_count != 1:
        return {"available": False, "reason": "content_build_imports_missing"}
    report: dict[str, object] = {"available": True, "content_db_name": content_db_name}
    report["health"] = json_query(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'game_code',game_code,"
        "'content_revision_key',content_revision_key,"
        "'build_status',build_status,"
        "'build_import_count',build_import_count,"
        "'file_count',file_count,"
        "'blocking_error_count',blocking_error_count,"
        "'zen_entity_count',zen_entity_count,"
        "'waypoint_count',waypoint_count,"
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
    report["recent_imports"] = json_query(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'content_revision_key',content_revision_key,"
        "'importer_key',importer_key,"
        "'source_kind',source_kind,"
        "'source_logical_path',source_logical_path,"
        "'import_status',import_status,"
        "'item_count',item_count"
        ")),JSON_ARRAY()) FROM ("
        "SELECT content_revision_key,importer_key,source_kind,source_logical_path,import_status,item_count "
        "FROM " + db + ".v_content_build_imports ORDER BY imported_at DESC LIMIT " + str(max(0, limit)) +
        ") import_rows;",
    )
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Report standalone MMO content-build database state.")
    parser.add_argument("--report", default="", help="Local report generated by import_content_build_snapshot_database.py")
    parser.add_argument("--url", default="", help="Optional mysql://user:password@host:port/database")
    parser.add_argument("--content-db-name", default="mmo_content_build")
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    if not IDENT_RE.fullmatch(args.content_db_name):
        raise SystemExit("invalid --content-db-name; use only letters, digits and underscore")

    local_path = Path(args.report) if args.report else None
    if local_path is not None and not local_path.is_absolute():
        local_path = ROOT / local_path
    report = {
        "tool": "mmo_content_build_database_report.py",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "local_report": read_local_report(local_path) if local_path else None,
        "db_content_build": db_report(parse_mysql_url(args.url), args.content_db_name, args.limit) if args.url else None,
    }
    if args.output:
        out = Path(args.output)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("artifact=" + str(out))
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())




