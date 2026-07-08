#!/usr/bin/env python3
"""Summarize local and DB-backed MMO server content pack inventory."""
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

ROOT = Path(__file__).resolve().parents[1]

from pathlib import Path as _MysqlCliPath

_MYSQL_CLI_TOOLS_DIR = _MysqlCliPath(__file__).resolve().parent
if str(_MYSQL_CLI_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_MYSQL_CLI_TOOLS_DIR))
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
        "--skip-column-names",
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


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(
        mysql_cmd(target),
        input=sql,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(ROOT),
    )
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


def summarize_items(items: list[dict[str, object]]) -> dict[str, object]:
    by_role: dict[str, int] = {}
    by_stage: dict[str, int] = {}
    by_status: dict[str, int] = {}
    required = 0
    total_bytes = 0
    for item in items:
        role = str(item.get("file_role") or "other")
        stage = str(item.get("loader_stage") or "unknown")
        status = str(item.get("import_status") or "not_started")
        by_role[role] = by_role.get(role, 0) + 1
        by_stage[stage] = by_stage.get(stage, 0) + 1
        by_status[status] = by_status.get(status, 0) + 1
        required += 1 if item.get("required_for_server_authority") else 0
        try:
            total_bytes += int(item.get("byte_size") or 0)
        except (TypeError, ValueError):
            pass
    return {
        "file_count": len(items),
        "required_for_server_authority_count": required,
        "total_bytes": total_bytes,
        "by_role": dict(sorted(by_role.items())),
        "by_stage": dict(sorted(by_stage.items())),
        "by_status": dict(sorted(by_status.items())),
    }


def read_local_inventory(path: Path | None) -> dict[str, object] | None:
    if path is None:
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    items = data.get("items") if isinstance(data, dict) else None
    if not isinstance(items, list):
        raise ValueError("inventory JSON must contain an items array")
    return {
        "path": str(path),
        "content_revision_key": data.get("content_revision_key"),
        "manifest_hash": data.get("manifest_hash"),
        "summary": data.get("summary") or summarize_items([item for item in items if isinstance(item, dict)]),
    }


def db_inventory_report(target: Target, limit: int) -> dict[str, object]:
    table_count = scalar_int(
        target,
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema=DATABASE() AND table_name='mmo_server_content_pack_inventory';",
    )
    if table_count != 1:
        return {"available": False, "reason": "mmo_server_content_pack_inventory_missing"}

    health_exists = scalar_int(
        target,
        "SELECT COUNT(*) FROM information_schema.views "
        "WHERE table_schema=DATABASE() AND table_name='v_mmo_server_content_pack_inventory_health';",
    )
    detail_exists = scalar_int(
        target,
        "SELECT COUNT(*) FROM information_schema.views "
        "WHERE table_schema=DATABASE() AND table_name='v_mmo_server_content_pack_inventory';",
    )

    report: dict[str, object] = {"available": True}
    if health_exists == 1:
        report["health"] = json_query(
            target,
            "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
            "'content_revision_key',content_revision_key,"
            "'is_active',is_active,"
            "'manifest_file_count',manifest_file_count,"
            "'inventoried_file_count',inventoried_file_count,"
            "'missing_inventory_count',missing_inventory_count,"
            "'world_zen_count',world_zen_count,"
            "'scripts_dat_count',scripts_dat_count,"
            "'dialog_ou_count',dialog_ou_count,"
            "'archive_count',archive_count,"
            "'required_for_authority_count',required_for_authority_count,"
            "'ready_for_parser_count',ready_for_parser_count,"
            "'unsupported_count',unsupported_count,"
            "'failed_count',failed_count"
            ")),JSON_ARRAY()) FROM v_mmo_server_content_pack_inventory_health;",
        )
    if detail_exists == 1:
        report["by_role"] = json_query(
            target,
            "SELECT COALESCE(JSON_OBJECTAGG(file_role,cnt),JSON_OBJECT()) FROM ("
            "SELECT file_role, COUNT(*) AS cnt FROM v_mmo_server_content_pack_inventory GROUP BY file_role"
            ") role_counts;",
        )
        report["latest_required"] = json_query(
            target,
            "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
            "'content_revision_key',content_revision_key,"
            "'logical_path',logical_path,"
            "'file_role',file_role,"
            "'loader_stage',loader_stage,"
            "'import_status',import_status,"
            "'sha256',sha256"
            ")),JSON_ARRAY()) FROM ("
            "SELECT content_revision_key,logical_path,file_role,loader_stage,import_status,sha256 "
            "FROM v_mmo_server_content_pack_inventory "
            "WHERE required_for_server_authority=1 "
            "ORDER BY import_priority ASC, logical_path ASC LIMIT " + str(max(0, limit)) +
            ") latest_required;",
        )
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Report local/DB MMO server content pack inventory.")
    parser.add_argument("--inventory", default="", help="Inventory JSON generated by analyze_server_content_pack_inventory.py")
    parser.add_argument("--url", default="", help="Optional mysql://user:password@host:port/database")
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    inventory_path = Path(args.inventory) if args.inventory else None
    if inventory_path is not None and not inventory_path.is_absolute():
        inventory_path = ROOT / inventory_path

    report: dict[str, object] = {
        "tool": "mmo_content_pack_inventory_report.py",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "local_inventory": read_local_inventory(inventory_path) if inventory_path else None,
        "db_inventory": db_inventory_report(parse_mysql_url(args.url), args.limit) if args.url else None,
    }

    if args.output:
        output = Path(args.output)
        if not output.is_absolute():
            output = ROOT / output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("artifact=" + str(output))
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())




