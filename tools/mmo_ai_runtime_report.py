#!/usr/bin/env python3
"""Summarize standalone MMO AI runtime perception state."""
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

_MYSQL_CLI_TOOLS_DIR = _MysqlCliPath(__file__).resolve().parent
if str(_MYSQL_CLI_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_MYSQL_CLI_TOOLS_DIR))
try:
    from _mysql_cli import resolve_mysql_exe
except Exception:  # pragma: no cover - fallback for standalone patch bundles.
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[1]
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


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    return "'" + str(value).replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


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


def json_value(target: Target, sql: str) -> object:
    output = run_mysql(target, sql)
    if not output:
        return None
    return json.loads(output.splitlines()[-1])


def main() -> int:
    parser = argparse.ArgumentParser(description="Report standalone MMO AI runtime perception state.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--ai-db-name", default="mmo_ai_runtime")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    if not IDENT_RE.fullmatch(args.ai_db_name):
        raise SystemExit("invalid --ai-db-name; use only letters, digits and underscore")

    target = parse_mysql_url(args.url)
    db = mysql_identifier(args.ai_db_name)
    report: dict[str, object] = {
        "tool": "mmo_ai_runtime_report.py",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "ai_db_name": args.ai_db_name,
    }
    schema_count = json_value(target, "SELECT JSON_OBJECT('exists', COUNT(*)) FROM information_schema.schemata WHERE schema_name=" + sql_literal(args.ai_db_name) + ";")
    report["schema"] = schema_count
    report["health"] = json_value(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'health_scope',health_scope,"
        "'enabled_rule_count',enabled_rule_count,"
        "'disabled_rule_count',disabled_rule_count,"
        "'cooldown_count',cooldown_count,"
        "'decision_count',decision_count,"
        "'queued_decision_count',queued_decision_count,"
        "'cooldown_skip_count',cooldown_skip_count,"
        "'pending_action_count',pending_action_count,"
        "'failed_action_count',failed_action_count"
        ")),JSON_ARRAY()) FROM " + db + ".v_npc_perception_policy_health;",
    )
    report["recent_decisions"] = json_value(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'decision_uuid',decision_uuid,"
        "'world_instance_uuid',world_instance_uuid,"
        "'npc_entity_key',npc_entity_key,"
        "'target_key',target_key,"
        "'perception_kind',perception_kind,"
        "'decision_kind',decision_kind,"
        "'decision_status',decision_status,"
        "'server_tick',server_tick,"
        "'created_at',CAST(created_at AS CHAR)"
        ")),JSON_ARRAY()) FROM ("
        "SELECT * FROM " + db + ".v_npc_perception_decisions ORDER BY created_at DESC LIMIT 20"
        ") recent;",
    )
    report["pending_actions"] = json_value(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'action_queue_uuid',action_queue_uuid,"
        "'decision_uuid',decision_uuid,"
        "'world_instance_uuid',world_instance_uuid,"
        "'action_kind',action_kind,"
        "'target_key',target_key,"
        "'action_status',action_status,"
        "'priority_value',priority_value,"
        "'created_at',CAST(created_at AS CHAR)"
        ")),JSON_ARRAY()) FROM ("
        "SELECT * FROM " + db + ".v_npc_perception_action_queue WHERE action_status='pending' ORDER BY priority_value ASC, created_at ASC LIMIT 20"
        ") pending;",
    )
    report["dispatch_health"] = json_value(
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
    report["recent_dispatch_events"] = json_value(
        target,
        "SELECT COALESCE(JSON_ARRAYAGG(JSON_OBJECT("
        "'dispatch_log_uuid',dispatch_log_uuid,"
        "'action_queue_uuid',action_queue_uuid,"
        "'decision_uuid',decision_uuid,"
        "'worker_id',worker_id,"
        "'dispatch_event',dispatch_event,"
        "'action_status',action_status,"
        "'attempt_count',attempt_count,"
        "'error_code',error_code,"
        "'created_at',CAST(created_at AS CHAR)"
        ")),JSON_ARRAY()) FROM ("
        "SELECT * FROM " + db + ".v_npc_perception_action_dispatch_log ORDER BY created_at DESC LIMIT 20"
        ") events;",
    )

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




