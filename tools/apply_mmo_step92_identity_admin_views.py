#!/usr/bin/env python3
"""Install Step92 human-readable MySQL admin identity views."""
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

ROOT = Path(__file__).resolve().parents[1]
SQL_PATH = ROOT / "server" / "sql" / "step92_identity_admin_views.sql"
REQUIRED_VIEWS = (
    "v_mmo_admin_item_instances_readable",
    "v_mmo_admin_entity_templates_readable",
    "v_mmo_admin_world_entities_readable",
)


@dataclass(frozen=True)
class Target:
    host: str
    port: int
    user: str
    password: str
    database: str


def parse_mysql_url(url: str) -> Target:
    p = urlparse(url)
    if p.scheme not in {"mysql", "mysql+pymysql"}:
        raise ValueError("expected mysql:// URL")
    return Target(
        host=p.hostname or "127.0.0.1",
        port=p.port or 3306,
        user=unquote(p.username or ""),
        password=unquote(p.password or ""),
        database=(p.path or "/").lstrip("/"),
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
        "--skip-column-names",
        "-h", target.host,
        "-P", str(target.port),
        "-u", target.user,
    ]
    if target.password:
        cmd.append(f"-p{target.password}")
    cmd.append(target.database)
    return cmd


def run_mysql(target: Target, sql: str) -> str:
    proc = subprocess.run(mysql_cmd(target), input=sql, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, file=sys.stderr, end="")
    if proc.returncode != 0:
        raise RuntimeError(f"mysql exited with status {proc.returncode}")
    return proc.stdout.strip()


def sql_literal(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def count_view(target: Target, name: str) -> int:
    out = run_mysql(target, f"""
        SELECT COUNT(*)
          FROM information_schema.views
         WHERE table_schema=DATABASE()
           AND table_name={sql_literal(name)};
    """)
    return int((out or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, object]:
    return {"views": {name: count_view(target, name) == 1 for name in REQUIRED_VIEWS}}


def main() -> int:
    ap = argparse.ArgumentParser(description="Install Step92 human-readable MMO identity admin views.")
    ap.add_argument("--url", required=True, help="mysql://user:password@host:port/database")
    ap.add_argument("--sql", default=str(SQL_PATH))
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    sql_path = Path(args.sql)
    if not sql_path.is_absolute():
        sql_path = ROOT / sql_path

    result: dict[str, object] = {
        "step": "92_identity_admin_views",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "sql": str(sql_path),
        "status": "running",
    }

    try:
        if not sql_path.exists():
            result["status"] = "failed_missing_sql"
            result["error"] = str(sql_path)
        else:
            run_mysql(target, sql_path.read_text(encoding="utf-8"))
            result.update(inspect(target))
            missing = [name for name, ok in result["views"].items() if not ok]  # type: ignore[index]
            if missing:
                result["status"] = "failed"
                result["missing_views"] = missing
            else:
                result["status"] = "applied"
    except Exception as exc:  # noqa: BLE001 - command-line diagnostic surface
        result["status"] = "failed"
        result["error"] = str(exc)
        print(f"ERROR: {exc}", file=sys.stderr)

    result["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"artifact={out}")
    print("status=" + str(result["status"]))
    return 0 if result["status"] == "applied" else 1


if __name__ == "__main__":
    raise SystemExit(main())
