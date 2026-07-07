#!/usr/bin/env python3
"""Check Step203 server content archive mount DB objects and health."""
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


def json_value(target: Target, sql: str) -> object:
    output = run_mysql(target, sql)
    if not output:
        return None
    return json.loads(output.splitlines()[-1])


def object_exists(target: Target, kind: str, name: str) -> int:
    if kind == "table":
        return scalar_int(
            target,
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema=DATABASE() AND table_name=" + sql_literal(name) + ";",
        )
    if kind == "view":
        return scalar_int(
            target,
            "SELECT COUNT(*) FROM information_schema.views "
            "WHERE table_schema=DATABASE() AND table_name=" + sql_literal(name) + ";",
        )
    if kind == "procedure":
        return scalar_int(
            target,
            "SELECT COUNT(*) FROM information_schema.routines "
            "WHERE routine_schema=DATABASE() AND routine_type='PROCEDURE' AND routine_name=" + sql_literal(name) + ";",
        )
    raise ValueError(kind)


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Step203 server content archive mount DB state.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--expect-mounted-or-extracted", action="store_true")
    parser.add_argument("--fail-on-missing-mount", action="store_true")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    checks: list[dict[str, object]] = []
    for kind, name in (
        ("table", "mmo_server_content_archive_mounts"),
        ("table", "mmo_server_content_extracted_files"),
        ("view", "v_mmo_server_content_archive_mounts"),
        ("view", "v_mmo_server_content_extracted_files"),
        ("view", "v_mmo_server_content_archive_health"),
        ("procedure", "mmo_upsert_server_content_archive_mount"),
        ("procedure", "mmo_upsert_server_content_extracted_file"),
    ):
        count = object_exists(target, kind, name)
        checks.append({"name": name, "kind": kind, "status": "passed" if count == 1 else "failed", "count": count})

    migration_count = scalar_int(
        target,
        "SELECT COUNT(*) FROM mmo_schema_versions "
        "WHERE migration_key='server/sql/step203_server_content_archive_mounts.sql';",
    )
    checks.append({
        "name": "step203_schema_version_marker",
        "kind": "migration",
        "status": "passed" if migration_count == 1 else "failed",
        "count": migration_count,
    })

    mount_count = scalar_int(target, "SELECT COUNT(*) FROM mmo_server_content_archive_mounts;")
    extracted_count = scalar_int(target, "SELECT COUNT(*) FROM mmo_server_content_extracted_files;")
    checks.append({"name": "archive_mount_rows", "kind": "data", "status": "passed" if mount_count > 0 else "warning", "count": mount_count})
    checks.append({"name": "extracted_file_rows", "kind": "data", "status": "passed" if extracted_count > 0 else "warning", "count": extracted_count})

    health = json_value(
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
    checks.append({"name": "archive_health", "kind": "data", "status": "passed", "details": health})

    if args.fail_on_missing_mount:
        missing = 0
        if isinstance(health, list):
            missing = sum(int(row.get("missing_mount_count", 0) or 0) for row in health if isinstance(row, dict))
        checks.append({"name": "missing_archive_mounts", "kind": "data_expectation", "status": "passed" if missing == 0 else "failed", "count": missing})

    if args.expect_mounted_or_extracted:
        ready = scalar_int(
            target,
            "SELECT COUNT(*) FROM mmo_server_content_archive_mounts "
            "WHERE mount_status IN ('mounted','extracted','verified');",
        )
        checks.append({"name": "mounted_or_extracted_archives", "kind": "data_expectation", "status": "passed" if ready > 0 else "failed", "count": ready})

    failed = [item for item in checks if item["status"] == "failed"]
    report = {
        "tool": "check_mmo_step203_server_content_archive_mounts.py",
        "status": "failed" if failed else "passed",
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "checks": checks,
    }

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
