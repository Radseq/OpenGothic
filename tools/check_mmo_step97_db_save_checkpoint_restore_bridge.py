#!/usr/bin/env python3
"""Check Step97 DB-save-checkpoint restore/export bridge."""
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

REQUIRED_TABLES = ("mmo_save_checkpoint_world_clock_snapshot",)
REQUIRED_ROUTINES = (
    "mmo_materialize_save_checkpoint_world_clock_snapshot_v1",
    "mmo_create_db_save_checkpoint_v1",
    "mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1",
)
REQUIRED_VIEWS = (
    "v_mmo_save_checkpoint_snapshot_domain_counts",
    "v_mmo_latest_save_checkpoint_restore_readiness",
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
    cmd = [exe, "--default-character-set=utf8mb4", "--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_0900_ai_ci",
           "--batch", "--raw", "--skip-column-names", "-h", target.host, "-P", str(target.port), "-u", target.user]
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
    if kind == "table":
        sql = f"""SELECT COUNT(*) FROM information_schema.tables
                  WHERE table_schema=DATABASE() AND table_name={sql_literal(name)} AND table_type='BASE TABLE';"""
    elif kind == "routine":
        sql = f"""SELECT COUNT(*) FROM information_schema.routines
                  WHERE routine_schema=DATABASE() AND routine_name={sql_literal(name)};"""
    elif kind == "view":
        sql = f"""SELECT COUNT(*) FROM information_schema.views
                  WHERE table_schema=DATABASE() AND table_name={sql_literal(name)};"""
    else:
        raise ValueError(kind)
    out = run_mysql(target, sql)
    return int((out or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, object]:
    return {
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
        "views": {name: count_object(target, "view", name) == 1 for name in REQUIRED_VIEWS},
    }


def smoke(target: Target) -> dict[str, object]:
    idem = "step97-smoke-db-save-checkpoint-restore"
    sql = f"""
    SET @sid=(SELECT session_id FROM server_sessions ORDER BY started_at DESC LIMIT 1);
    SET @manifest_id=NULL; SET @event_id=NULL; SET @row_version_after=NULL;
    CALL mmo_create_db_save_checkpoint_v1(
      @sid,
      'character:PC_HERO:db-save-checkpoint-restore',
      'native_save',
      'step97_smoke',
      970,
      JSON_OBJECT(
        'smoke', true,
        'save_slot_key', 'step97-smoke-slot',
        'native_save_path', 'runtime/step97-smoke.sav',
        'display_name', 'Step97 DB Save Restore Smoke',
        'client_world_name', 'NEWWORLD.ZEN',
        'native_save_present', true
      ),
      {sql_literal(idem)},
      @manifest_id,
      @event_id,
      @row_version_after
    );
    SET @snapshot_json=(SELECT COALESCE(mmo_build_latest_save_checkpoint_bootstrap_snapshot_v1(@sid), ''));
    SELECT CONCAT(
      BIN_TO_UUID(@manifest_id,1), '\t',
      BIN_TO_UUID(@event_id,1), '\t',
      COALESCE((SELECT COUNT(*) FROM mmo_save_checkpoint_character_snapshot WHERE manifest_id=@manifest_id),0), '\t',
      COALESCE((SELECT COUNT(*) FROM mmo_save_checkpoint_world_entity_snapshot WHERE manifest_id=@manifest_id),0), '\t',
      COALESCE((SELECT COUNT(*) FROM mmo_save_checkpoint_world_clock_snapshot WHERE manifest_id=@manifest_id),0), '\t',
      CHAR_LENGTH(@snapshot_json), '\t',
      JSON_UNQUOTE(JSON_EXTRACT(CAST(@snapshot_json AS JSON), '$.snapshot_source'))
    );
    """
    out = run_mysql(target, sql)
    last = out.splitlines()[-1] if out.splitlines() else ""
    parts = last.split("\t")
    ok = (
        len(parts) == 7
        and bool(parts[0])
        and bool(parts[1])
        and parts[2].isdigit() and int(parts[2]) == 1
        and parts[3].isdigit() and int(parts[3]) > 0
        and parts[5].isdigit() and int(parts[5]) > 1000
        and parts[6] == "db_save_checkpoint_v1"
    )
    return {
        "ok": ok,
        "row": last,
        "manifest_uuid": parts[0] if len(parts) >= 1 else None,
        "event_uuid": parts[1] if len(parts) >= 2 else None,
        "character_rows": int(parts[2]) if len(parts) == 7 and parts[2].isdigit() else None,
        "world_entity_rows": int(parts[3]) if len(parts) == 7 and parts[3].isdigit() else None,
        "world_clock_rows": int(parts[4]) if len(parts) == 7 and parts[4].isdigit() else None,
        "exported_bootstrap_bytes": int(parts[5]) if len(parts) == 7 and parts[5].isdigit() else None,
        "snapshot_source": parts[6] if len(parts) == 7 else None,
    }


def latest(target: Target) -> list[dict[str, object]]:
    sql = """
    SELECT CONCAT_WS('\t',
      COALESCE(manifest_uuid,''),
      COALESCE(character_key,''),
      COALESCE(save_key,''),
      COALESCE(display_name,''),
      COALESCE(character_rows,0),
      COALESCE(inventory_rows,0),
      COALESCE(quest_rows,0),
      COALESCE(known_dialog_rows,0),
      COALESCE(script_state_rows,0),
      COALESCE(world_entity_rows,0),
      COALESCE(world_inventory_rows,0),
      COALESCE(world_clock_rows,0),
      COALESCE(mover_rows,0),
      COALESCE(exported_bootstrap_bytes,0)
    )
    FROM v_mmo_latest_save_checkpoint_restore_readiness
    ORDER BY created_at DESC
    LIMIT 5;
    """
    out = run_mysql(target, sql)
    rows: list[dict[str, object]] = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) != 14:
            continue
        rows.append({
            "manifest_uuid": parts[0],
            "character_key": parts[1],
            "save_key": parts[2],
            "display_name": parts[3],
            "character_rows": int(parts[4]),
            "inventory_rows": int(parts[5]),
            "quest_rows": int(parts[6]),
            "known_dialog_rows": int(parts[7]),
            "script_state_rows": int(parts[8]),
            "world_entity_rows": int(parts[9]),
            "world_inventory_rows": int(parts[10]),
            "world_clock_rows": int(parts[11]),
            "mover_rows": int(parts[12]),
            "exported_bootstrap_bytes": int(parts[13]),
        })
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Check Step97 DB-save-checkpoint restore/export bridge.")
    ap.add_argument("--url", required=True)
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "97_db_save_checkpoint_restore_bridge",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "status": "running",
    }

    try:
        result.update(inspect(target))
        missing = []
        for section in ("tables", "routines", "views"):
            missing.extend(f"{section}:{name}" for name, ok in result[section].items() if not ok)  # type: ignore[index]
        if missing:
            result["status"] = "failed"
            result["missing"] = missing
        elif args.smoke:
            result["smoke"] = smoke(target)
            result["latest"] = latest(target)
            result["status"] = "ok" if result["smoke"]["ok"] else "failed"  # type: ignore[index]
        else:
            result["latest"] = latest(target)
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
