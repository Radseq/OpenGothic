#!/usr/bin/env python3
"""Apply the standalone MMO AI runtime database surface."""
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


@dataclass(frozen=True)
class SqlSurface:
    key: str
    path: Path
    note: str


AI_RUNTIME_SQL_SURFACES = (
    SqlSurface(
        "ai_runtime_perception_database",
        ROOT / "server" / "sql" / "step212_ai_runtime_perception_database.sql",
        "standalone mmo_ai_runtime schema for server-side NPC perception decisions",
    ),
    SqlSurface(
        "ai_runtime_action_dispatch_contracts",
        ROOT / "server" / "sql" / "step213_ai_runtime_action_dispatch_contracts.sql",
        "dispatcher claim/apply/fail/skip contracts for NPC perception action queue",
    ),
)


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def parse_mysql_url(url: str) -> Target:
    parsed = urlparse(url)
    if parsed.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql://user:password@host:port/database")
    database = (parsed.path or "/").lstrip("/")
    if not database:
        raise ValueError("database missing in MySQL URL; use any existing admin/runtime DB as the connection target")
    return Target(parsed.hostname or "localhost", parsed.port or 3306, unquote(parsed.username or ""), unquote(parsed.password or ""), database)


def mysql_cmd(target: Target, *, allow_missing: bool = False) -> list[str]:
    exe = resolve_mysql_exe()
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
    return ["-p***" if part.startswith("-p") and len(part) > 2 else part for part in cmd]


def run_mysql_file(target: Target, path: Path, *, dry_run: bool) -> dict[str, object]:
    shown = redact_cmd(mysql_cmd(target, allow_missing=dry_run))
    print("[SQL] " + rel(path))
    print("[RUN] " + " ".join(shown))
    if dry_run:
        return {"cmd": shown, "returncode": 0, "dry_run": True, "stdout": "", "stderr": ""}
    proc = subprocess.run(
        mysql_cmd(target),
        input=path.read_text(encoding="utf-8"),
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
    parser = argparse.ArgumentParser(description="Apply standalone MMO AI runtime database SQL.")
    parser.add_argument("--url", required=True, help="mysql://user:password@host:port/existing_database")
    parser.add_argument("--output", default="")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list", action="store_true", help="Print the AI runtime SQL surface order and exit.")
    args = parser.parse_args()

    if args.list:
        for surface in AI_RUNTIME_SQL_SURFACES:
            print(f"{surface.key}\t{rel(surface.path)}\t{surface.note}")
        return 0

    target = parse_mysql_url(args.url)
    manifest: dict[str, object] = {
        "tool": "apply_ai_runtime_database.py",
        "status": "running",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "connection_database": target.database,
        "ai_runtime_database": "mmo_ai_runtime",
        "surfaces": [],
    }

    missing = [surface for surface in AI_RUNTIME_SQL_SURFACES if not surface.path.exists()]
    if missing:
        manifest["status"] = "failed_missing_sql"
        manifest["missing"] = [{"key": item.key, "path": rel(item.path)} for item in missing]
    else:
        for surface in AI_RUNTIME_SQL_SURFACES:
            result = run_mysql_file(target, surface.path, dry_run=args.dry_run)
            entry = {
                "key": surface.key,
                "path": rel(surface.path),
                "note": surface.note,
                "status": "applied" if result["returncode"] == 0 else "failed",
                "result": result,
            }
            manifest["surfaces"].append(entry)
            if entry["status"] == "failed":
                manifest["status"] = "failed"
                break
        else:
            manifest["status"] = "passed"

    manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        out = Path(args.output)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={rel(out)}")
    print("status=" + str(manifest["status"]))
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())




