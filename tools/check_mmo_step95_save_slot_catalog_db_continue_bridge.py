#!/usr/bin/env python3
"""Check Step95 DB-backed save-slot catalog/continue bridge."""
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
REQUIRED_VIEWS = ("v_mmo_latest_save_checkpoint_manifests",)
REQUIRED_ROUTINES = ("mmo_create_save_checkpoint_manifest",)
REQUIRED_COLUMNS = (
    "save_slot_key",
    "native_save_path",
    "display_name",
    "client_world_name",
    "native_save_present",
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


def count_object(target: Target, section: str, name: str) -> int:
    if section == "table":
        sql = f"SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_type='BASE TABLE' AND table_name={sql_literal(name)};"
    elif section == "view":
        sql = f"SELECT COUNT(*) FROM information_schema.views WHERE table_schema=DATABASE() AND table_name={sql_literal(name)};"
    elif section == "routine":
        sql = f"SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=DATABASE() AND routine_name={sql_literal(name)};"
    elif section == "column":
        sql = f"SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='mmo_save_checkpoint_manifests' AND column_name={sql_literal(name)};"
    else:
        raise ValueError(section)
    return int((run_mysql(target, sql) or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, object]:
    return {
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "views": {name: count_object(target, "view", name) == 1 for name in REQUIRED_VIEWS},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
        "columns": {name: count_object(target, "column", name) == 1 for name in REQUIRED_COLUMNS},
    }


def smoke(target: Target) -> dict[str, object]:
    idem = "step95-smoke-save-slot-catalog"
    save_slot = "runtime/save_slot_step95.sav"
    display_name = "Step95 DB Save"
    world = "NEWWORLD\\NEWWORLD.ZEN"
    sql = f"""
    SET @sid=(SELECT session_id FROM server_sessions ORDER BY started_at DESC LIMIT 1);
    SET @manifest_id=NULL; SET @event_id=NULL; SET @row_version_after=NULL;
    CALL mmo_create_save_checkpoint_manifest(
      @sid,
      'character:PC_HERO:save-checkpoint',
      'native_save',
      'step95_smoke',
      789,
      JSON_OBJECT('smoke',true,'save_slot_key',{sql_literal(save_slot)},'slot_path',{sql_literal(save_slot)},'display_name',{sql_literal(display_name)},'world',{sql_literal(world)},'native_save_present',true),
      {sql_literal(idem)},
      @manifest_id,
      @event_id,
      @row_version_after
    );
    CALL mmo_create_save_checkpoint_manifest(
      @sid,
      'character:PC_HERO:save-checkpoint',
      'native_save',
      'step95_smoke',
      789,
      JSON_OBJECT('smoke',true,'save_slot_key',{sql_literal(save_slot)},'slot_path',{sql_literal(save_slot)},'display_name',{sql_literal(display_name)},'world',{sql_literal(world)},'native_save_present',true),
      {sql_literal(idem)},
      @manifest_id,
      @event_id,
      @row_version_after
    );
    SELECT CONCAT(
      BIN_TO_UUID(@manifest_id,1),'\t',
      @row_version_after,'\t',
      COALESCE(save_slot_key,''),'\t',
      COALESCE(native_save_path,''),'\t',
      COALESCE(display_name,''),'\t',
      COALESCE(client_world_name,''),'\t',
      native_save_present
    )
      FROM mmo_save_checkpoint_manifests WHERE manifest_id=@manifest_id LIMIT 1;
    SELECT CONCAT(character_key,'\t',COALESCE(save_slot_key,''),'\t',COALESCE(display_name,''),'\t',character_rank)
      FROM v_mmo_latest_save_checkpoint_manifests
     WHERE save_slot_key={sql_literal(save_slot)}
     LIMIT 1;
    """
    out = run_mysql(target, sql)
    rows = [line for line in out.splitlines() if line.strip()]
    manifest_row = rows[-2] if len(rows) >= 2 else ""
    catalog_row = rows[-1] if rows else ""
    parts = manifest_row.split("\t")
    view_parts = catalog_row.split("\t")
    ok = (
        len(parts) == 7
        and int(parts[1]) >= 1
        and parts[2] == save_slot
        and parts[3] == save_slot
        and parts[4] == display_name
        and parts[5] == world
        and parts[6] == "1"
        and len(view_parts) == 4
        and view_parts[1] == save_slot
        and view_parts[2] == display_name
    )
    return {"ok": ok, "manifest_row": manifest_row, "catalog_row": catalog_row}


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step95 DB-backed save-slot catalog/continue bridge.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()
    target = parse_mysql_url(args.url)
    result: dict[str, object] = {"step": "95_save_slot_catalog_db_continue_bridge", "started_at": datetime.now(timezone.utc).isoformat(), "database": target.database, "status": "running"}
    try:
        result.update(inspect(target))
        missing = []
        for section in ("tables", "views", "routines", "columns"):
            missing.extend(f"{section}:{name}" for name, ok in result[section].items() if not ok)  # type: ignore[index]
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
