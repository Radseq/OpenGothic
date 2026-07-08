#!/usr/bin/env python3
"""Apply the Step221 content-build item template compatibility migration."""
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
except Exception:  # pragma: no cover
    def resolve_mysql_exe() -> str | None:
        return shutil.which("mysql")


ROOT = Path(__file__).resolve().parents[2]
SQL_PATH = ROOT / "server" / "sql" / "step221_content_build_item_templates_compat.sql"


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
    return Target(parsed.hostname or "localhost", parsed.port or 3306, unquote(parsed.username or ""), unquote(parsed.password or ""), database)


def mysql_cmd(target: Target, *, allow_missing: bool = False) -> list[str]:
    try:
        exe = resolve_mysql_exe()
    except RuntimeError:
        if not allow_missing:
            raise
        exe = None
    if exe is None:
        if not allow_missing:
            raise RuntimeError("mysql executable not found in PATH")
        exe = "mysql"
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


def redact_cmd(cmd: list[str]) -> list[str]:
    return ["-p***" if item.startswith("-p") and len(item) > 2 else item for item in cmd]


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def run_mysql_file(target: Target, *, dry_run: bool) -> dict[str, object]:
    shown = redact_cmd(mysql_cmd(target, allow_missing=dry_run))
    print("[SQL] " + rel(SQL_PATH))
    print("[RUN] " + " ".join(shown))
    if dry_run:
        return {"cmd": shown, "returncode": 0, "dry_run": True, "stdout": "", "stderr": ""}
    proc = subprocess.run(
        mysql_cmd(target),
        input=SQL_PATH.read_text(encoding="utf-8"),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(ROOT),
    )
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    return {"cmd": shown, "returncode": proc.returncode, "dry_run": False, "stdout": proc.stdout, "stderr": proc.stderr}


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply Step221 mmo_content_build item template compatibility migration.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/existing_database")
    parser.add_argument("--output", default="runtime/step221_content_build_item_templates_compat/apply.json")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if not SQL_PATH.exists():
        raise SystemExit("missing SQL file: " + rel(SQL_PATH))

    target = parse_mysql_url(args.url)
    result = run_mysql_file(target, dry_run=args.dry_run)
    status = "passed" if result["returncode"] == 0 else "failed"
    report = {
        "tool": "apply_content_build_item_templates_compat.py",
        "status": status,
        "sql_path": rel(SQL_PATH),
        "connection_database": target.database,
        "content_build_database": "mmo_content_build",
        "finished_at": datetime.now(timezone.utc).isoformat(),
        "result": result,
    }

    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("artifact=" + rel(output))
    print("status=" + status)
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
