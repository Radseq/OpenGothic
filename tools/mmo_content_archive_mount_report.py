#!/usr/bin/env python3
"""Summarize local and DB-backed MMO server content archive mount plans."""
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


def summarize_local_plan(plan: dict[str, object]) -> dict[str, object]:
    by_status: dict[str, int] = {}
    by_strategy: dict[str, int] = {}
    for raw in plan.get("archive_plans", []):
        if not isinstance(raw, dict):
            continue
        status = str(raw.get("mount_status") or "planned")
        strategy = str(raw.get("mount_strategy") or "pre_extracted")
        by_status[status] = by_status.get(status, 0) + 1
        by_strategy[strategy] = by_strategy.get(strategy, 0) + 1
    return {
        "archive_count": int(plan.get("archive_count") or 0),
        "extracted_file_count": int(plan.get("extracted_file_count") or 0),
        "extracted_total_bytes": int(plan.get("extracted_total_bytes") or 0),
        "extracted_manifest_hash": plan.get("extracted_manifest_hash"),
        "extracted_mapping_note": plan.get("extracted_mapping_note"),
        "by_status": dict(sorted(by_status.items())),
        "by_strategy": dict(sorted(by_strategy.items())),
    }


def read_local_plan(path: Path | None) -> dict[str, object] | None:
    if path is None:
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("archive mount plan JSON must be an object")
    return {
        "path": str(path),
        "content_revision_key": data.get("content_revision_key"),
        "manifest_hash": data.get("manifest_hash"),
        "summary": summarize_local_plan(data),
    }


def db_report(target: Target, limit: int) -> dict[str, object]:
    table_count = scalar_int(
        target,
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema=DATABASE() AND table_name='mmo_server_content_archive_mounts';",
    )
    if table_count != 1:
        return {"available": False, "reason": "mmo_server_content_archive_mounts_missing"}

    health_exists = scalar_int(
        target,
        "SELECT COUNT(*) FROM information_schema.views "
        "WHERE table_schema=DATABASE() AND table_name='v_mmo_server_content_archive_health';",
    )
    mount_view_exists = scalar_int(
        target,
        "SELECT COUNT(*) FROM information_schema.views "
        "WHERE table_schema=DATABASE() AND table_name='v_mmo_server_content_archive_mounts';",
    )

    report: dict[str, object] = {"available": True}
    if health_exists == 1:
        report["health"] = json_query(
            target,
            "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
            "'content_revision_key',content_revision_key,"
            "'is_active',is_active,"
            "'inventory_archive_count',inventory_archive_count,"
            "'archive_mount_count',archive_mount_count,"
            "'missing_mount_count',missing_mount_count,"
            "'planned_count',planned_count,"
            "'mounted_count',mounted_count,"
            "'extracted_count',extracted_count,"
            "'verified_count',verified_count,"
            "'failed_count',failed_count,"
            "'extracted_file_count',extracted_file_count,"
            "'extracted_total_bytes',extracted_total_bytes"
            ")),JSON_ARRAY()) FROM v_mmo_server_content_archive_health;",
        )
    if mount_view_exists == 1:
        report["by_status"] = json_query(
            target,
            "SELECT COALESCE(JSON_OBJECTAGG(mount_status,cnt),JSON_OBJECT()) FROM ("
            "SELECT mount_status, COUNT(*) AS cnt FROM v_mmo_server_content_archive_mounts GROUP BY mount_status"
            ") status_counts;",
        )
        report["archives"] = json_query(
            target,
            "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
            "'content_revision_key',content_revision_key,"
            "'archive_logical_path',archive_logical_path,"
            "'archive_role',archive_role,"
            "'mount_strategy',mount_strategy,"
            "'mount_status',mount_status,"
            "'extracted_root_label',extracted_root_label,"
            "'extracted_file_count',extracted_file_count,"
            "'extracted_manifest_hash',extracted_manifest_hash"
            ")),JSON_ARRAY()) FROM ("
            "SELECT content_revision_key,archive_logical_path,archive_role,mount_strategy,mount_status,"
            "extracted_root_label,extracted_file_count,extracted_manifest_hash "
            "FROM v_mmo_server_content_archive_mounts "
            "ORDER BY content_revision_key, archive_logical_path LIMIT " + str(max(0, limit)) +
            ") archive_rows;",
        )
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Report local/DB MMO server content archive mount state.")
    parser.add_argument("--plan", default="", help="Archive mount plan JSON generated by plan_server_content_archive_mounts.py")
    parser.add_argument("--url", default="", help="Optional mysql://user:password@host:port/database")
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    plan_path = Path(args.plan) if args.plan else None
    if plan_path is not None and not plan_path.is_absolute():
        plan_path = ROOT / plan_path

    report: dict[str, object] = {
        "tool": "mmo_content_archive_mount_report.py",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "local_plan": read_local_plan(plan_path) if plan_path else None,
        "db_archive_mounts": db_report(parse_mysql_url(args.url), args.limit) if args.url else None,
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
