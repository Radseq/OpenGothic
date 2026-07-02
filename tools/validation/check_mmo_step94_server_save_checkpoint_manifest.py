#!/usr/bin/env python3
"""Check Step94 server save/checkpoint manifest."""
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

REQUIRED_TABLES = ("mmo_save_checkpoint_manifests",)
REQUIRED_ROUTINES = ("mmo_create_save_checkpoint_manifest",)

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
    db = (p.path or "/").lstrip("/")
    if not db:
        raise ValueError("database is missing in mysql URL")
    return Target(p.hostname or "127.0.0.1", p.port or 3306, unquote(p.username or ""), unquote(p.password or ""), db)

def mysql_cmd(target: Target) -> list[str]:
    exe = shutil.which("mysql")
    if exe is None:
        raise RuntimeError("mysql executable was not found in PATH")
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci", "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
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

def count_object(target: Target, kind: str, name: str) -> int:
    table = "tables" if kind == "table" else "routines"
    schema_col = "table_schema" if kind == "table" else "routine_schema"
    name_col = "table_name" if kind == "table" else "routine_name"
    extra = " AND table_type='BASE TABLE'" if kind == "table" else ""
    return int((run_mysql(target, f"SELECT COUNT(*) FROM information_schema.{table} WHERE {schema_col}=DATABASE(){extra} AND {name_col}={sql_literal(name)};") or "0").splitlines()[-1])

def inspect(target: Target) -> dict[str, object]:
    return {
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
    }

def smoke(target: Target) -> dict[str, object]:
    idem = "step94-smoke-save-checkpoint-manifest"
    sql = f"""
    SET @sid=(SELECT session_id FROM server_sessions ORDER BY started_at DESC LIMIT 1);
    SET @manifest_id=NULL; SET @event_id=NULL; SET @row_version_after=NULL;
    CALL mmo_create_save_checkpoint_manifest(@sid,'character:PC_HERO:save-checkpoint','native_save','step94_smoke',456,JSON_OBJECT('smoke',true),{sql_literal(idem)},@manifest_id,@event_id,@row_version_after);
    CALL mmo_create_save_checkpoint_manifest(@sid,'character:PC_HERO:save-checkpoint','native_save','step94_smoke',456,JSON_OBJECT('smoke',true),{sql_literal(idem)},@manifest_id,@event_id,@row_version_after);
    SELECT CONCAT(BIN_TO_UUID(@manifest_id,1),'\t',BIN_TO_UUID(@event_id,1),'\t',@row_version_after,'\t',checkpoint_kind,'\t',inventory_rows,'\t',mover_rows)
      FROM mmo_save_checkpoint_manifests WHERE manifest_id=@manifest_id LIMIT 1;
    """
    out = run_mysql(target, sql)
    last = out.splitlines()[-1] if out.splitlines() else ""
    parts = last.split("\t")
    return {"ok": len(parts) == 6 and parts[3] == "native_save" and int(parts[2]) >= 1, "row": last}

def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step94 server save/checkpoint manifest.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()
    target = parse_mysql_url(args.url)
    result: dict[str, object] = {"step": "94_server_save_checkpoint_manifest", "started_at": datetime.now(timezone.utc).isoformat(), "database": target.database, "status": "running"}
    try:
        result.update(inspect(target))
        missing = [name for name, ok in result["tables"].items() if not ok] + [name for name, ok in result["routines"].items() if not ok]  # type: ignore[index]
        if missing:
            result["status"] = "failed"
            result["missing"] = missing
        elif args.smoke:
            result["smoke"] = smoke(target)
            result["status"] = "ok" if result["smoke"]["ok"] else "failed"  # type: ignore[index]
        else:
            result["status"] = "ok"
    except Exception as exc:  # noqa: BLE001
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
    return 0 if result["status"] == "ok" else 1

if __name__ == "__main__":
    raise SystemExit(main())
