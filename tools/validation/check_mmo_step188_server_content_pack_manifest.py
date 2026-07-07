#!/usr/bin/env python3
"""Check Step188 server content pack manifest state."""
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


def validate_hash(target: Target, realm_key: str, client_hash: str) -> dict[str, object]:
    sql = (
        "SET @accepted=0; SET @reason=''; SET @server_hash=NULL; SET @revision_key=NULL;"
        "CALL mmo_validate_client_content_pack("
        + sql_literal(realm_key) + ","
        + sql_literal(client_hash) + ","
        + "@accepted,@reason,@server_hash,@revision_key);"
        "SELECT CONCAT(@accepted,'\\t',COALESCE(@reason,''),'\\t',COALESCE(@server_hash,''),'\\t',COALESCE(@revision_key,''));"
    )
    output = run_mysql(target, sql)
    parts = (output.splitlines()[-1] if output else "").split("\t")
    return {
        "accepted": parts[0] == "1" if len(parts) > 0 else False,
        "reason": parts[1] if len(parts) > 1 else "missing_result",
        "server_manifest_hash": parts[2] if len(parts) > 2 else "",
        "content_revision_key": parts[3] if len(parts) > 3 else "",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Step188 server content pack manifest.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    parser.add_argument("--realm-key", default="", help="Realm key to validate against.")
    parser.add_argument("--client-manifest-hash", default="", help="Client-declared manifest hash to validate.")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    checks: list[dict[str, object]] = []
    for name in ("mmo_server_content_pack_files", "mmo_server_content_pack_manifests"):
        count = scalar_int(
            target,
            "SELECT COUNT(*) FROM information_schema.tables "
            "WHERE table_schema=DATABASE() AND table_name=" + sql_literal(name) + ";",
        )
        checks.append({"name": name, "status": "passed" if count == 1 else "failed", "count": count})

    for name in ("mmo_upsert_server_content_pack_file", "mmo_set_server_content_pack_manifest", "mmo_validate_client_content_pack"):
        count = scalar_int(
            target,
            "SELECT COUNT(*) FROM information_schema.routines "
            "WHERE routine_schema=DATABASE() AND routine_type='PROCEDURE' AND routine_name=" + sql_literal(name) + ";",
        )
        checks.append({"name": name, "status": "passed" if count == 1 else "failed", "count": count})

    manifest_count = scalar_int(target, "SELECT COUNT(*) FROM mmo_server_content_pack_manifests;")
    file_count = scalar_int(target, "SELECT COUNT(*) FROM mmo_server_content_pack_files;")
    checks.append({"name": "manifest_rows", "status": "passed" if manifest_count > 0 else "warning", "count": manifest_count})
    checks.append({"name": "manifest_file_rows", "status": "passed" if file_count > 0 else "warning", "count": file_count})

    validation = None
    if args.realm_key or args.client_manifest_hash:
        validation = validate_hash(target, args.realm_key, args.client_manifest_hash)
        checks.append({
            "name": "client_manifest_validation",
            "status": "passed" if validation["accepted"] else "failed",
            "details": validation,
        })

    failed = [item for item in checks if item["status"] == "failed"]
    report = {
        "tool": "check_mmo_step188_server_content_pack_manifest.py",
        "status": "failed" if failed else "passed",
        "checked_at": datetime.now(timezone.utc).isoformat(),
        "checks": checks,
        "validation": validation,
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
