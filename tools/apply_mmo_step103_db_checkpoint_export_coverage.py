#!/usr/bin/env python3
"""Install Step103 DB checkpoint export coverage bridge."""
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

ROOT = Path(__file__).resolve().parents[1]
SQL_PATH = ROOT / "server" / "sql" / "step103_db_checkpoint_export_coverage.sql"

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


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
        raise ValueError("expected mysql:// URL")
    database = (parsed.path or "/").lstrip("/")
    if not database:
        raise ValueError("database is missing in mysql URL")
    return Target(
        host=parsed.hostname or "127.0.0.1",
        port=parsed.port or 3306,
        user=unquote(parsed.username or ""),
        password=unquote(parsed.password or ""),
        database=database,
    )


def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [
        exe,
        "--default-character-set=utf8mb4",
        "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
        "--batch",
        "--raw",
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


def run_mysql_file(target: Target, sql_path: Path) -> None:
    proc = subprocess.run(
        mysql_cmd(target),
        input=sql_path.read_text(encoding="utf-8"),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"mysql exited with status {proc.returncode}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Install Step103 DB checkpoint export coverage bridge.")
    parser.add_argument("--url", required=True)
    parser.add_argument("--sql", default=str(SQL_PATH))
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    result: dict[str, object] = {
        "step": "103_db_checkpoint_export_coverage",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "sql": args.sql,
        "status": "running",
    }

    try:
        target = parse_mysql_url(args.url)
        result["database"] = target.database
        sql_path = Path(args.sql)
        if not sql_path.exists():
            result["status"] = "failed"
            result["error"] = f"SQL file does not exist: {sql_path}"
        else:
            run_mysql_file(target, sql_path)
            result["status"] = "ok"
    except Exception as exc:  # noqa: BLE001 - CLI installer should report exact failure.
        result["status"] = "failed"
        result["error"] = str(exc)

    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("status=" + str(result["status"]))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())

