#!/usr/bin/env python3
"""Check Step208 server content build index DB objects and data."""
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


def object_exists(target: Target, kind: str, name: str) -> int:
    if kind == "table":
        return scalar_int(target, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name=" + sql_literal(name) + ";")
    if kind == "view":
        return scalar_int(target, "SELECT COUNT(*) FROM information_schema.views WHERE table_schema=DATABASE() AND table_name=" + sql_literal(name) + ";")
    raise ValueError(kind)


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Step208 server content build indexes.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--expect-zen", action="store_true")
    parser.add_argument("--expect-daedalus", action="store_true")
    parser.add_argument("--expect-dialog-outputs", action="store_true")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    checks: list[dict[str, object]] = []
    for kind, name in (
        ("table", "mmo_server_content_build_imports"),
        ("table", "mmo_server_world_zen_entities"),
        ("table", "mmo_server_daedalus_symbols"),
        ("table", "mmo_server_daedalus_npc_templates"),
        ("table", "mmo_server_dialog_outputs"),
        ("view", "v_mmo_server_content_build_imports"),
        ("view", "v_mmo_server_world_zen_entities"),
        ("view", "v_mmo_server_daedalus_symbols"),
        ("view", "v_mmo_server_daedalus_npc_templates"),
        ("view", "v_mmo_server_dialog_outputs"),
        ("view", "v_mmo_server_content_build_health"),
    ):
        count = object_exists(target, kind, name)
        checks.append({"name": name, "kind": kind, "status": "passed" if count == 1 else "failed", "count": count})

    migration_count = scalar_int(target, "SELECT COUNT(*) FROM mmo_schema_versions WHERE migration_key='server/sql/step208_server_content_build_indexes.sql';")
    checks.append({"name": "step208_schema_version_marker", "kind": "migration", "status": "passed" if migration_count == 1 else "failed", "count": migration_count})

    counts = {
        "build_imports": scalar_int(target, "SELECT COUNT(*) FROM mmo_server_content_build_imports;"),
        "zen_entities": scalar_int(target, "SELECT COUNT(*) FROM mmo_server_world_zen_entities;"),
        "daedalus_symbols": scalar_int(target, "SELECT COUNT(*) FROM mmo_server_daedalus_symbols;"),
        "npc_templates": scalar_int(target, "SELECT COUNT(*) FROM mmo_server_daedalus_npc_templates;"),
        "dialog_outputs": scalar_int(target, "SELECT COUNT(*) FROM mmo_server_dialog_outputs;"),
    }
    checks.append({"name": "content_build_counts", "kind": "data", "status": "passed", "details": counts})

    health = json_value(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'content_revision_key',content_revision_key,"
        "'is_active',is_active,"
        "'build_import_count',build_import_count,"
        "'imported_count',imported_count,"
        "'failed_count',failed_count,"
        "'zen_entity_count',zen_entity_count,"
        "'waypoint_count',waypoint_count,"
        "'freepoint_count',freepoint_count,"
        "'vob_count',vob_count,"
        "'daedalus_symbol_count',daedalus_symbol_count,"
        "'npc_template_count',npc_template_count,"
        "'dialog_output_count',dialog_output_count"
        ")),JSON_ARRAY()) FROM v_mmo_server_content_build_health;",
    )
    checks.append({"name": "content_build_health", "kind": "data", "status": "passed", "details": health})

    if args.expect_zen:
        checks.append({"name": "expected_zen_entities", "kind": "data_expectation", "status": "passed" if counts["zen_entities"] > 0 else "failed", "count": counts["zen_entities"]})
    if args.expect_daedalus:
        daedalus_count = counts["daedalus_symbols"] + counts["npc_templates"]
        checks.append({"name": "expected_daedalus_index", "kind": "data_expectation", "status": "passed" if daedalus_count > 0 else "failed", "count": daedalus_count})
    if args.expect_dialog_outputs:
        checks.append({"name": "expected_dialog_outputs", "kind": "data_expectation", "status": "passed" if counts["dialog_outputs"] > 0 else "failed", "count": counts["dialog_outputs"]})

    failed = [item for item in checks if item["status"] == "failed"]
    report = {"tool": "check_mmo_step208_server_content_build_indexes.py", "status": "failed" if failed else "passed", "checked_at": datetime.now(timezone.utc).isoformat(), "checks": checks}
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




