#!/usr/bin/env python3
"""Healthcheck content manifest reject rates from Step199 DB views."""
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

_TOOLS_DIR = Path(__file__).resolve().parent
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

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
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def scalar_json(target: Target, sql: str) -> dict[str, object]:
    raw = run_mysql(target, sql)
    line = raw.splitlines()[-1] if raw else "{}"
    value = json.loads(line)
    if not isinstance(value, dict):
        raise RuntimeError("health query did not return a JSON object")
    return value


def as_int(value: object) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check MMO content manifest reject health.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--max-last-15m-rejects", type=int, default=50)
    parser.add_argument("--max-last-hour-rejects", type=int, default=200)
    parser.add_argument("--max-last-hour-hash-mismatches", type=int, default=100)
    parser.add_argument("--fail-on-server-manifest-missing", action="store_true", default=True)
    parser.add_argument("--no-fail-on-server-manifest-missing", dest="fail_on_server_manifest_missing", action="store_false")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    sql = (
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
        ") FROM v_mmo_content_manifest_reject_health;"
    )
    health = scalar_json(target, sql)

    checks: list[dict[str, object]] = []
    checks.append({
        "name": "last_15m_rejects",
        "value": as_int(health.get("last_15m_rejects")),
        "threshold": args.max_last_15m_rejects,
        "status": "passed" if as_int(health.get("last_15m_rejects")) <= args.max_last_15m_rejects else "failed",
    })
    checks.append({
        "name": "last_hour_rejects",
        "value": as_int(health.get("last_hour_rejects")),
        "threshold": args.max_last_hour_rejects,
        "status": "passed" if as_int(health.get("last_hour_rejects")) <= args.max_last_hour_rejects else "failed",
    })
    checks.append({
        "name": "last_hour_hash_mismatches",
        "value": as_int(health.get("last_hour_hash_mismatches")),
        "threshold": args.max_last_hour_hash_mismatches,
        "status": "passed" if as_int(health.get("last_hour_hash_mismatches")) <= args.max_last_hour_hash_mismatches else "failed",
    })
    if args.fail_on_server_manifest_missing:
        missing_server = as_int(health.get("total_missing_server_manifests"))
        checks.append({
            "name": "server_manifest_missing",
            "value": missing_server,
            "threshold": 0,
            "status": "passed" if missing_server == 0 else "failed",
        })

    failed = [check for check in checks if check["status"] == "failed"]
    report = {
        "tool": "check_mmo_content_manifest_reject_health.py",
        "status": "failed" if failed else "passed",
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "health": health,
        "checks": checks,
    }
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("artifact=" + str(out))
    print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
