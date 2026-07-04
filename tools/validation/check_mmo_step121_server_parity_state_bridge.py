#!/usr/bin/env python3
"""Check Step121 server parity state bridge."""
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

REQUIRED_TABLES = (
    "mmo_world_trigger_queue_current",
    "mmo_world_trigger_queue_history",
    "mmo_character_world_transition_state_current",
    "mmo_character_world_transition_state_history",
    "mmo_client_action_correction_current",
    "mmo_client_action_correction_history",
)
REQUIRED_ROUTINES = (
    "mmo_record_trigger_queue_state",
    "mmo_record_world_transition_state",
    "mmo_record_client_action_correction",
    "mmo_ack_client_action_correction",
)


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
        parsed.hostname or "localhost",
        parsed.port or 3306,
        unquote(parsed.username or ""),
        unquote(parsed.password or ""),
        database,
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
        sql = (
            "SELECT COUNT(*) FROM information_schema.tables "
            f"WHERE table_schema=DATABASE() AND table_type='BASE TABLE' AND table_name={sql_literal(name)};"
        )
    elif kind == "routine":
        sql = (
            "SELECT COUNT(*) FROM information_schema.routines "
            f"WHERE routine_schema=DATABASE() AND routine_name={sql_literal(name)};"
        )
    else:
        raise ValueError(kind)
    return int((run_mysql(target, sql) or "0").splitlines()[-1])


def inspect(target: Target) -> dict[str, object]:
    return {
        "tables": {name: count_object(target, "table", name) == 1 for name in REQUIRED_TABLES},
        "routines": {name: count_object(target, "routine", name) == 1 for name in REQUIRED_ROUTINES},
    }


def smoke(target: Target) -> dict[str, object]:
    idem_prefix = "step121-smoke"
    sql = f"""
    SET @sid=(SELECT session_id FROM server_sessions ORDER BY started_at DESC LIMIT 1);
    SET @world_key=(SELECT rwi.world_instance_key FROM server_sessions ss JOIN realm_world_instances rwi ON rwi.world_instance_id=ss.world_instance_id WHERE ss.session_id=@sid LIMIT 1);

    SET @trigger_event_id=NULL; SET @trigger_row=NULL;
    CALL mmo_record_trigger_queue_state(
      @sid,
      'trigger:step121:smoke',
      'queued',
      'timer',
      121000,
      121,
      JSON_OBJECT('smoke', true),
      {sql_literal(idem_prefix + ":trigger-queue")},
      @trigger_event_id,
      @trigger_row
    );

    SET @transition_event_id=NULL; SET @transition_row=NULL;
    CALL mmo_record_world_transition_state(
      @sid,
      @world_key,
      @world_key,
      'visited',
      'chapter:1',
      TRUE,
      121,
      JSON_OBJECT('smoke', true),
      {sql_literal(idem_prefix + ":world-transition")},
      @transition_event_id,
      @transition_row
    );

    SET @correction_event_id=NULL; SET @correction_id=NULL;
    CALL mmo_record_client_action_correction(
      @sid,
      'movement_proposal',
      121,
      'rollback_to_authoritative_position',
      'step121_smoke',
      121,
      JSON_OBJECT('smoke', true),
      {sql_literal(idem_prefix + ":client-correction")},
      @correction_event_id,
      @correction_id
    );

    SET @ack_row=NULL;
    CALL mmo_ack_client_action_correction(
      @sid,
      'movement_proposal',
      121,
      122,
      JSON_OBJECT('smoke', true),
      {sql_literal(idem_prefix + ":client-correction-ack")},
      @ack_row
    );

    SELECT CONCAT(
      COALESCE(@trigger_row,0), '\t',
      COALESCE(@transition_row,0), '\t',
      COALESCE(BIN_TO_UUID(@correction_id,1), ''), '\t',
      COALESCE(@ack_row,0), '\t',
      (SELECT COUNT(*) FROM mmo_world_trigger_queue_current WHERE trigger_key='trigger:step121:smoke'), '\t',
      (SELECT COUNT(*) FROM mmo_character_world_transition_state_current WHERE to_world_key=@world_key), '\t',
      (SELECT COUNT(*) FROM mmo_client_action_correction_current WHERE correction_id=@correction_id AND acknowledged=TRUE)
    );
    """
    out = run_mysql(target, sql)
    last = out.splitlines()[-1] if out.splitlines() else ""
    parts = last.split("\t")
    ok = (
        len(parts) == 7
        and int(parts[0] or "0") >= 1
        and int(parts[1] or "0") >= 1
        and bool(parts[2])
        and int(parts[3] or "0") >= 1
        and int(parts[4] or "0") >= 1
        and int(parts[5] or "0") >= 1
        and int(parts[6] or "0") == 1
    )
    return {"ok": ok, "row": last}


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Step121 server parity state bridge.")
    parser.add_argument("--url", required=True)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    target = parse_mysql_url(args.url)
    result: dict[str, object] = {
        "step": "121_server_parity_state_bridge",
        "started_at": datetime.now(timezone.utc).isoformat(),
        "database": target.database,
        "status": "running",
    }

    try:
        result.update(inspect(target))
        missing = (
            [name for name, ok in result["tables"].items() if not ok] +  # type: ignore[index]
            [name for name, ok in result["routines"].items() if not ok]  # type: ignore[index]
        )
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
