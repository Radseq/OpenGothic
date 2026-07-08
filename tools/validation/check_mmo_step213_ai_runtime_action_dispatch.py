#!/usr/bin/env python3
"""Check Step213 MMO AI runtime action dispatch contracts."""
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
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


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


def object_exists(target: Target, db_name: str, kind: str, name: str) -> int:
    if kind == "table":
        return scalar_int(target, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=" + sql_literal(db_name) + " AND table_name=" + sql_literal(name) + ";")
    if kind == "view":
        return scalar_int(target, "SELECT COUNT(*) FROM information_schema.views WHERE table_schema=" + sql_literal(db_name) + " AND table_name=" + sql_literal(name) + ";")
    if kind == "procedure":
        return scalar_int(target, "SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=" + sql_literal(db_name) + " AND routine_name=" + sql_literal(name) + " AND routine_type='PROCEDURE';")
    raise ValueError(kind)


def column_exists(target: Target, db_name: str, table: str, column: str) -> int:
    return scalar_int(
        target,
        "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema="
        + sql_literal(db_name)
        + " AND table_name="
        + sql_literal(table)
        + " AND column_name="
        + sql_literal(column)
        + ";",
    )


def count_table(target: Target, db_name: str, table: str) -> int:
    return scalar_int(target, "SELECT COUNT(*) FROM " + mysql_identifier(db_name) + "." + mysql_identifier(table) + ";")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Step213 MMO AI runtime action dispatch contracts.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--ai-db-name", default="mmo_ai_runtime")
    parser.add_argument("--expect-dispatch-log", action="store_true")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    if not IDENT_RE.fullmatch(args.ai_db_name):
        raise SystemExit("invalid --ai-db-name; use only letters, digits and underscore")

    target = parse_mysql_url(args.url)
    db = mysql_identifier(args.ai_db_name)
    checks: list[dict[str, object]] = []

    schema_count = scalar_int(target, "SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name=" + sql_literal(args.ai_db_name) + ";")
    checks.append({"name": args.ai_db_name, "kind": "schema", "status": "passed" if schema_count == 1 else "failed", "count": schema_count})

    for column in (
        "max_attempts",
        "attempt_count",
        "worker_id",
        "locked_at",
        "applied_at",
        "failed_at",
        "completed_at",
        "next_attempt_at",
        "last_error_code",
        "last_error_message",
        "result_payload",
    ):
        count = column_exists(target, args.ai_db_name, "npc_perception_action_queue", column)
        checks.append({"name": "npc_perception_action_queue." + column, "kind": "column", "status": "passed" if count == 1 else "failed", "count": count})

    for kind, name in (
        ("table", "npc_perception_action_dispatch_log"),
        ("view", "v_npc_perception_pending_actions"),
        ("view", "v_npc_perception_action_dispatch_log"),
        ("view", "v_npc_perception_action_dispatch_health"),
        ("procedure", "mmo_ai_claim_next_npc_perception_action"),
        ("procedure", "mmo_ai_mark_npc_perception_action_applied"),
        ("procedure", "mmo_ai_mark_npc_perception_action_failed"),
        ("procedure", "mmo_ai_skip_npc_perception_action"),
    ):
        count = object_exists(target, args.ai_db_name, kind, name)
        checks.append({"name": name, "kind": kind, "status": "passed" if count == 1 else "failed", "count": count})

    migration_count = scalar_int(target, "SELECT COUNT(*) FROM " + db + ".ai_runtime_schema_versions WHERE migration_key='server/sql/step213_ai_runtime_action_dispatch_contracts.sql';")
    checks.append({"name": "step213_schema_version_marker", "kind": "migration", "status": "passed" if migration_count == 1 else "failed", "count": migration_count})

    counts = {
        "action_queue": count_table(target, args.ai_db_name, "npc_perception_action_queue"),
        "dispatch_log": count_table(target, args.ai_db_name, "npc_perception_action_dispatch_log"),
    }
    checks.append({"name": "ai_runtime_dispatch_counts", "kind": "data", "status": "passed", "details": counts})

    health = json_value(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'health_scope',health_scope,"
        "'pending_count',pending_count,"
        "'claimed_count',claimed_count,"
        "'applied_count',applied_count,"
        "'failed_count',failed_count,"
        "'skipped_count',skipped_count,"
        "'delayed_retry_count',delayed_retry_count,"
        "'dispatch_log_count',dispatch_log_count"
        ")),JSON_ARRAY()) FROM " + db + ".v_npc_perception_action_dispatch_health;",
    )
    checks.append({"name": "ai_runtime_dispatch_health", "kind": "data", "status": "passed", "details": health})

    if args.expect_dispatch_log:
        checks.append({"name": "expected_dispatch_log", "kind": "data_expectation", "status": "passed" if counts["dispatch_log"] > 0 else "failed", "count": counts["dispatch_log"]})

    failed = [item for item in checks if item["status"] == "failed"]
    report = {"tool": "check_mmo_step213_ai_runtime_action_dispatch.py", "status": "failed" if failed else "passed", "checked_at": datetime.now(timezone.utc).isoformat(), "ai_db_name": args.ai_db_name, "checks": checks}
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




