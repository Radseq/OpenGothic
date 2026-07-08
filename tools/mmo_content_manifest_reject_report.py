#!/usr/bin/env python3
"""Summarize MMO content manifest bootstrap rejects.

Reads the client-side runtime/mmo_server_bootstrap_reject.json file and the
server-side runtime/mmo_server_content_manifest_rejects.jsonl audit file.
The output is JSON so tests, launchers, or CI glue can consume it directly.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

_TOOLS_DIR = Path(__file__).resolve().parent
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

try:
    from _mysql_cli import resolve_mysql_exe
except Exception:  # pragma: no cover - fallback for standalone copies.
    def resolve_mysql_exe() -> str | None:
        import shutil
        return shutil.which("mysql")


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def read_json(path: Path) -> dict[str, Any] | None:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
    except FileNotFoundError:
        return None
    except json.JSONDecodeError as exc:
        return {"_parse_error": str(exc), "_path": str(path)}
    if isinstance(value, dict):
        return value
    return {"_parse_error": "top-level JSON value is not an object", "_path": str(path)}


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    try:
        handle = path.open("r", encoding="utf-8")
    except FileNotFoundError:
        return records

    with handle:
        for line_number, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                records.append({
                    "_parse_error": str(exc),
                    "_line": line_number,
                    "_path": str(path),
                })
                continue
            if isinstance(value, dict):
                records.append(value)
            else:
                records.append({
                    "_parse_error": "JSONL line is not an object",
                    "_line": line_number,
                    "_path": str(path),
                })
    return records


def summarize(records: list[dict[str, Any]]) -> dict[str, Any]:
    by_reason: Counter[str] = Counter()
    by_revision: Counter[str] = Counter()
    by_required_hash: Counter[str] = Counter()
    parse_errors = 0

    for record in records:
        if "_parse_error" in record:
            parse_errors += 1
            continue
        by_reason[str(record.get("reason") or "<empty>")] += 1
        by_revision[str(record.get("content_revision_key") or "<empty>")] += 1
        by_required_hash[str(record.get("server_required_content_hash") or "<empty>")] += 1

    return {
        "total_records": len(records),
        "valid_records": len(records) - parse_errors,
        "parse_errors": parse_errors,
        "by_reason": dict(by_reason.most_common()),
        "by_content_revision_key": dict(by_revision.most_common()),
        "by_server_required_content_hash": dict(by_required_hash.most_common()),
    }


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
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: object) -> str:
    if value is None:
        return "NULL"
    text = str(value)
    return "'" + text.replace("\\", "\\\\").replace("'", "''").replace("\0", "") + "'"


def mysql_rows(target: Target, sql: str) -> list[list[str]]:
    raw = run_mysql(target, sql)
    if not raw:
        return []
    return [line.split("\t") for line in raw.splitlines() if line.strip()]


def db_audit_report(url: str, limit: int) -> dict[str, Any]:
    target = parse_mysql_url(url)
    try:
        table_count = mysql_rows(
            target,
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema=DATABASE() AND table_type='BASE TABLE' "
            "AND table_name='mmo_content_manifest_reject_audit';",
        )
        if not table_count or table_count[0][0] != "1":
            return {"available": False, "reason": "mmo_content_manifest_reject_audit_missing"}

        health_view = mysql_rows(
            target,
            "SELECT COUNT(*) FROM information_schema.views "
            "WHERE table_schema=DATABASE() AND table_name='v_mmo_content_manifest_reject_health';",
        )
        summary_view = mysql_rows(
            target,
            "SELECT COUNT(*) FROM information_schema.views "
            "WHERE table_schema=DATABASE() AND table_name='v_mmo_content_manifest_reject_summary';",
        )
        health = None
        if health_view and health_view[0][0] == "1":
            rows = mysql_rows(
                target,
                "SELECT JSON_OBJECT("
                "'total_rejects',total_rejects,"
                "'last_hour_rejects',last_hour_rejects,"
                "'last_15m_rejects',last_15m_rejects,"
                "'total_hash_mismatches',total_hash_mismatches,"
                "'last_hour_hash_mismatches',last_hour_hash_mismatches,"
                "'total_missing_client_manifests',total_missing_client_manifests,"
                "'total_missing_server_manifests',total_missing_server_manifests,"
                "'distinct_remote_endpoints',distinct_remote_endpoints,"
                "'distinct_client_hashes',distinct_client_hashes,"
                "'last_reject_at',DATE_FORMAT(last_reject_at,'%Y-%m-%dT%H:%i:%s.%fZ')"
                ") FROM v_mmo_content_manifest_reject_health;",
            )
            if rows:
                try:
                    health = json.loads(rows[0][0])
                except json.JSONDecodeError:
                    health = {"_parse_error": "invalid health JSON_OBJECT result", "raw": rows[0][0]}

        by_reason = {
            row[0]: int(row[1] or "0")
            for row in mysql_rows(
                target,
                "SELECT COALESCE(reason,'<empty>'), COUNT(*) "
                "FROM mmo_content_manifest_reject_audit GROUP BY COALESCE(reason,'<empty>') ORDER BY COUNT(*) DESC, 1;",
            )
        }
        by_revision = {
            row[0]: int(row[1] or "0")
            for row in mysql_rows(
                target,
                "SELECT COALESCE(content_revision_key,'<empty>'), COUNT(*) "
                "FROM mmo_content_manifest_reject_audit GROUP BY COALESCE(content_revision_key,'<empty>') ORDER BY COUNT(*) DESC, 1;",
            )
        }
        summary_rows = []
        if summary_view and summary_view[0][0] == "1":
            for row in mysql_rows(
                target,
                "SELECT JSON_OBJECT("
                "'reason',reason,"
                "'content_revision_key',content_revision_key,"
                "'server_required_content_hash',server_manifest_hash,"
                "'total_count',total_count,"
                "'last_hour_count',last_hour_count,"
                "'last_15m_count',last_15m_count,"
                "'first_seen_at',DATE_FORMAT(first_seen_at,'%Y-%m-%dT%H:%i:%s.%fZ'),"
                "'last_seen_at',DATE_FORMAT(last_seen_at,'%Y-%m-%dT%H:%i:%s.%fZ')"
                ") FROM v_mmo_content_manifest_reject_summary ORDER BY total_count DESC, reason LIMIT "
                + str(max(0, limit))
                + ";",
            ):
                try:
                    summary_rows.append(json.loads(row[0]))
                except json.JSONDecodeError:
                    summary_rows.append({"_parse_error": "invalid summary JSON_OBJECT result", "raw": row[0]})
        latest_sql = (
            "SELECT JSON_OBJECT("
            "'reject_uuid',reject_uuid,"
            "'created_at',DATE_FORMAT(created_at,'%Y-%m-%dT%H:%i:%s.%fZ'),"
            "'session_uuid',session_uuid,"
            "'realm_key',realm_key,"
            "'character_key',character_key,"
            "'remote_endpoint',remote_endpoint,"
            "'reason',reason,"
            "'phase',phase,"
            "'client_content_manifest_hash',client_manifest_hash,"
            "'server_required_content_hash',server_manifest_hash,"
            "'content_revision_key',content_revision_key,"
            "'packet_sequence',packet_sequence,"
            "'local_sequence',local_sequence,"
            "'target_key',target_key"
            ") FROM v_mmo_content_manifest_reject_audit ORDER BY created_at DESC LIMIT "
            + str(max(0, limit))
            + ";"
        )
        latest = []
        for row in mysql_rows(target, latest_sql):
            try:
                latest.append(json.loads(row[0]))
            except json.JSONDecodeError:
                latest.append({"_parse_error": "invalid JSON_OBJECT result", "raw": row[0]})
        return {
            "available": True,
            "health": health,
            "by_reason": by_reason,
            "by_content_revision_key": by_revision,
            "summary_rows": summary_rows,
            "latest_records": latest,
        }
    except Exception as exc:
        return {"available": False, "reason": str(exc)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--runtime-dir",
        default="runtime",
        help="Directory containing MMO runtime diagnostic files.",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=20,
        help="Maximum number of latest server audit records to include.",
    )
    parser.add_argument(
        "--url",
        default="",
        help="Optional mysql:// URL. When set, include Step198 DB audit summary.",
    )
    args = parser.parse_args()

    runtime_dir = Path(args.runtime_dir)
    client_reject_path = runtime_dir / "mmo_server_bootstrap_reject.json"
    server_audit_path = runtime_dir / "mmo_server_content_manifest_rejects.jsonl"

    client_reject = read_json(client_reject_path)
    server_records = read_jsonl(server_audit_path)
    limit = max(0, args.limit)
    latest = server_records[-limit:] if limit else []

    report = {
        "client_reject_path": str(client_reject_path),
        "server_audit_path": str(server_audit_path),
        "client_reject_present": client_reject is not None,
        "client_reject": client_reject,
        "server_audit_summary": summarize(server_records),
        "latest_server_audit_records": latest,
        "db_audit": db_audit_report(args.url, limit) if args.url else None,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())




