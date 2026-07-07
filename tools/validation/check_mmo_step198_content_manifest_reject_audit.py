#!/usr/bin/env python3
"""Check Step198/199 content manifest reject DB audit state."""
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
    text = run_mysql(target, sql).splitlines()
    if not text:
        return 0
    try:
        return int(text[-1].strip() or "0")
    except ValueError:
        return 0


def exists_count(target: Target, kind: str, name: str) -> int:
    if kind == "table":
        return scalar_int(
            target,
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema=DATABASE() AND table_type='BASE TABLE' AND table_name="
            + sql_literal(name)
            + ";",
        )
    if kind == "view":
        return scalar_int(
            target,
            "SELECT COUNT(*) FROM information_schema.views "
            "WHERE table_schema=DATABASE() AND table_name="
            + sql_literal(name)
            + ";",
        )
    if kind == "procedure":
        return scalar_int(
            target,
            "SELECT COUNT(*) FROM information_schema.routines "
            "WHERE routine_schema=DATABASE() AND routine_type='PROCEDURE' AND routine_name="
            + sql_literal(name)
            + ";",
        )
    raise ValueError(f"unknown object kind: {kind}")


def call_sample_record(target: Target, session_uuid: str) -> dict[str, object]:
    session_expr = "NULL" if not session_uuid else "UUID_TO_BIN(" + sql_literal(session_uuid) + ",1)"
    sql = (
        "SET @reject_id=NULL;"
        "CALL mmo_record_content_manifest_reject("
        + session_expr
        + ","
        + sql_literal("127.0.0.1:0")
        + ","
        + sql_literal("step198-check")
        + ","
        + sql_literal("character:PC_HERO:check")
        + ",1,1,"
        + sql_literal("check")
        + ","
        + sql_literal("client_manifest_missing")
        + ",NULL,NULL,NULL,"
        + sql_literal("step198 checker sample")
        + ",JSON_OBJECT('tool','check_mmo_step198_content_manifest_reject_audit.py'),@reject_id);"
        "SELECT BIN_TO_UUID(@reject_id,1);"
    )
    output = run_mysql(target, sql)
    reject_uuid = output.splitlines()[-1] if output else ""
    return {"reject_uuid": reject_uuid, "inserted": bool(reject_uuid)}


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Step198/199 content manifest reject audit DB objects.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--session-uuid", default="", help="Optional session UUID for --write-sample.")
    parser.add_argument("--write-sample", action="store_true", help="Insert one sample audit row through the procedure.")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    checks: list[dict[str, object]] = []
    expected = [
        ("table", "mmo_content_manifest_reject_audit"),
        ("view", "v_mmo_content_manifest_reject_audit"),
        ("view", "v_mmo_content_manifest_reject_summary"),
        ("view", "v_mmo_content_manifest_reject_health"),
        ("procedure", "mmo_record_content_manifest_reject"),
    ]
    for kind, name in expected:
        count = exists_count(target, kind, name)
        checks.append({"name": name, "kind": kind, "status": "passed" if count == 1 else "failed", "count": count})

    marker_count = scalar_int(
        target,
        "SELECT COUNT(*) FROM mmo_schema_versions WHERE migration_key='server/sql/step198_content_manifest_reject_audit.sql';",
    )
    checks.append({"name": "schema_marker_step198", "kind": "schema_marker", "status": "passed" if marker_count == 1 else "failed", "count": marker_count})
    marker_199_count = scalar_int(
        target,
        "SELECT COUNT(*) FROM mmo_schema_versions WHERE migration_key='server/sql/step199_content_manifest_reject_health_views.sql';",
    )
    checks.append({"name": "schema_marker_step199", "kind": "schema_marker", "status": "passed" if marker_199_count == 1 else "failed", "count": marker_199_count})

    row_count = scalar_int(target, "SELECT COUNT(*) FROM mmo_content_manifest_reject_audit;")
    checks.append({"name": "audit_rows", "kind": "row_count", "status": "passed", "count": row_count})

    sample = None
    if args.write_sample:
        sample = call_sample_record(target, args.session_uuid)
        checks.append({"name": "sample_record", "kind": "procedure_call", "status": "passed" if sample["inserted"] else "failed", "details": sample})

    failed = [item for item in checks if item["status"] == "failed"]
    report = {
        "tool": "check_mmo_step198_content_manifest_reject_audit.py",
        "status": "failed" if failed else "passed",
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "checks": checks,
        "sample": sample,
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
